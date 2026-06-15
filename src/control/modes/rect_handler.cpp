#include "../fsm_utils.h"
#include "../../drivers/heater.h"
#include "../../drivers/pump.h"
#include "../../drivers/valves.h"
#include "../../drivers/sensors.h"
#include "../watt_control.h"
#include "../v2/reason_codes.h"
#include "../v2/safety_supervisor.h"
#include "../v2/status_adapter.h"
#include "../../interface/mqtt.h"
#include "../../storage/logger.h"
#include <Arduino.h>

namespace FSM {
namespace Rectification {

static float headsTargetMl = 0.0f;
static float bodyTargetMl = 0.0f;
static float tailsTargetMl = 0.0f;
static bool bodyInitialized = false;

static void calculateTargets(const SystemState& state, const Settings& settings) {
    float volumeL = settings.rectParams.feedVolumeL;
    if (volumeL <= 0.1f) {
        volumeL = settings.equipment.cubeVolumeL;
    }
    volumeL = clampFloat(volumeL, 1.0f, 250.0f);

    float abv = settings.rectParams.feedAbvPercent;
    if (abv <= 0.0f || abv >= 100.0f) {
        abv = estimateChargeAbvPercent(state);
    }
    abv = clampFloat(abv, 1.0f, 96.0f);

    float headsPct = clampFloat(settings.rectParams.headsPercent, 0.0f, 40.0f);
    float bodyPct = clampFloat(settings.rectParams.bodyPercent, 0.0f, 100.0f);
    float tailsPct = clampFloat(settings.rectParams.tailsPercent, 0.0f, 100.0f);

    const float aaMl = volumeL * 1000.0f * (abv / 100.0f);
    headsTargetMl = aaMl * (headsPct / 100.0f);
    bodyTargetMl = aaMl * (bodyPct / 100.0f);
    tailsTargetMl = aaMl * (tailsPct / 100.0f);

    if (headsTargetMl < 10.0f) headsTargetMl = 10.0f;
}

void getTargets(float& heads, float& body, float& tails) {
    heads = headsTargetMl;
    body = bodyTargetMl;
    tails = tailsTargetMl;
}

void initSession(const SystemState& state, const Settings& settings) {
    headsTargetMl = 0.0f;
    bodyTargetMl = 0.0f;
    tailsTargetMl = 0.0f;
    bodyInitialized = false;
    calculateTargets(state, settings);
}

void update(SystemState& state, const Settings& settings) {
    uint32_t now = millis();
    uint32_t startTime = getPhaseStartTime();
    uint32_t elapsed = now - startTime;
    const ControlV2::ActiveLimitsV2& liveLimits =
        ControlV2::SafetySupervisorV2::getLiveLimits();

    switch (state.rectPhase) {
        case RectPhase::HEATING:
            applyBoosterHeater(state, settings, true);
            applyFullHeatPower(settings);
            if (state.temps.cube >= getWaterAutoStartTempC(settings)) {
                Valves::setWater(true);
            }
            if (state.temps.valid[TEMP_COLUMN_BOTTOM] && state.temps.columnBottom > 78.0f) {
                LOG_I("FSM: HEATING -> STABILIZATION");
                ControlV2::notePhaseTransition(Mode::RECTIFICATION,
                                               static_cast<uint16_t>(RectPhase::HEATING),
                                               static_cast<uint16_t>(RectPhase::STABILIZATION),
                                               ControlV2::ReasonCodeV2::RC_HEATING_COMPLETE);
                state.rectPhase = RectPhase::STABILIZATION;
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(state.pump.totalVolumeMl);
                bodyInitialized = false;
                MQTT::publishNotification("Фаза: Стабилизация", "Разогрев завершён, начата стабилизация колонны", "info");
            }
            break;

        case RectPhase::STABILIZATION:
            applyBoosterHeater(state, settings, false);
            Valves::setHeads(false);
            Pump::stop();
            Valves::setWater(true);
            applyProcessHeaterPower(state, settings, 70);
            if (!liveLimits.phaseAdvanceBlocked &&
                elapsed > settings.rectParams.stabilizationMin * 60 * 1000UL) {
                LOG_I("FSM: STABILIZATION -> HEADS");
                ControlV2::notePhaseTransition(Mode::RECTIFICATION,
                                               static_cast<uint16_t>(RectPhase::STABILIZATION),
                                               static_cast<uint16_t>(RectPhase::HEADS),
                                               ControlV2::ReasonCodeV2::RC_STABILIZATION_TIMER_OK);
                state.rectPhase = RectPhase::HEADS;
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(state.pump.totalVolumeMl);
                MQTT::publishNotification("Фаза: Отбор голов", "Стабилизация завершена, начат отбор голов", "info");
            }
            break;

        case RectPhase::HEADS: {
            applyBoosterHeater(state, settings, false);
            applyProcessHeaterPower(state, settings, 60);
            float headsSpeed = settings.rectParams.headsSpeedMlHKw * (settings.equipment.heaterPowerW / 1000.0f);
            Pump::start(headsSpeed);
            Valves::setHeads(true);
            Valves::setWater(true);
            const float headsCollected = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
            state.stats.headsVolume = headsCollected;
            if (headsTargetMl > 0.0f && headsCollected >= headsTargetMl) {
                LOG_I("FSM: HEADS -> POST_HEADS_STABILIZATION (%.0f/%.0f ml)", headsCollected, headsTargetMl);
                ControlV2::notePhaseTransition(Mode::RECTIFICATION,
                                               static_cast<uint16_t>(RectPhase::HEADS),
                                               static_cast<uint16_t>(RectPhase::POST_HEADS_STABILIZATION),
                                               ControlV2::ReasonCodeV2::RC_HEADS_VOLUME_REACHED);
                state.rectPhase = RectPhase::POST_HEADS_STABILIZATION;
                setPhaseStartTime(now);
                Pump::stop();
                Valves::setHeads(false);
                MQTT::publishNotification("Фаза: Стабилизация после голов", "Отбор голов завершён, стабилизация перед отбором тела", "info");
            }
            break;
        }

        case RectPhase::POST_HEADS_STABILIZATION:
            applyBoosterHeater(state, settings, false);
            Pump::stop();
            Valves::setHeads(false);
            Valves::setWater(true);
            applyProcessHeaterPower(state, settings, 65);
            if (!liveLimits.phaseAdvanceBlocked && elapsed > 5 * 60 * 1000UL) {
                LOG_I("FSM: POST_HEADS_STABILIZATION -> PURGE");
                ControlV2::notePhaseTransition(Mode::RECTIFICATION,
                                               static_cast<uint16_t>(RectPhase::POST_HEADS_STABILIZATION),
                                               static_cast<uint16_t>(RectPhase::PURGE),
                                               ControlV2::ReasonCodeV2::RC_POST_HEADS_STABILIZATION_COMPLETE);
                state.rectPhase = RectPhase::PURGE;
                setPhaseStartTime(now);
            }
            break;

        case RectPhase::PURGE:
            applyBoosterHeater(state, settings, false);
            Pump::stop();
            Valves::closeAll();
            Valves::setWater(true);
            applyProcessHeaterPower(state, settings, 65);
            if (!liveLimits.phaseAdvanceBlocked &&
                elapsed > settings.rectParams.purgeMin * 60 * 1000UL) {
                LOG_I("FSM: PURGE -> BODY");
                ControlV2::notePhaseTransition(Mode::RECTIFICATION,
                                               static_cast<uint16_t>(RectPhase::PURGE),
                                               static_cast<uint16_t>(RectPhase::BODY),
                                               ControlV2::ReasonCodeV2::RC_PURGE_COMPLETE);
                state.rectPhase = RectPhase::BODY;
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(state.pump.totalVolumeMl);
                bodyInitialized = false;
                SmartDecrement::reset();
                MQTT::publishNotification("Фаза: Отбор тела", "Продувка завершена, начат отбор тела (основной продукт)", "success");
            }
            break;

        case RectPhase::BODY:
            applyBoosterHeater(state, settings, false);
            Valves::setHeads(false);
            Valves::setWater(true);
            applyProcessHeaterPower(state, settings, 60);
            if (!bodyInitialized && state.temps.valid[TEMP_COLUMN_TOP]) {
                SmartDecrement::init(state.temps.columnTop);
                bodyInitialized = true;
            }
            {
                float bodySpeed = settings.rectParams.bodySpeedMlHKw * (settings.equipment.heaterPowerW / 1000.0f);
                Pump::start(bodySpeed);
                const float bodyCollected = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
                state.stats.bodyVolume = bodyCollected;
                ControlV2::ReasonCodeV2 bodyExitReason = ControlV2::ReasonCodeV2::NONE;
                if (bodyTargetMl > 0.0f && bodyCollected >= bodyTargetMl) {
                    bodyExitReason = ControlV2::ReasonCodeV2::RC_BODY_TARGET_VOLUME_REACHED;
                } else if (bodyInitialized && SmartDecrement::update(state, settings)) {
                    bodyExitReason = ControlV2::ReasonCodeV2::RC_BODY_END_DETECTED;
                } else if (state.temps.valid[TEMP_CUBE] &&
                           state.temps.cube >= pressureAdjustedCubeTemp(RECT_CUBE_BODY_TO_TAILS_BASE_C, state)) {
                    bodyExitReason = ControlV2::ReasonCodeV2::RC_BODY_END_DETECTED;
                }
                if (bodyExitReason != ControlV2::ReasonCodeV2::NONE) {
                    LOG_I("FSM: BODY -> TAILS");
                    ControlV2::notePhaseTransition(Mode::RECTIFICATION,
                                                   static_cast<uint16_t>(RectPhase::BODY),
                                                   static_cast<uint16_t>(RectPhase::TAILS),
                                                   bodyExitReason);
                    state.rectPhase = RectPhase::TAILS;
                    setPhaseStartTime(now);
                    setPhaseStartVolumeMl(state.pump.totalVolumeMl);
                }
            }
            break;

        case RectPhase::TAILS:
            applyBoosterHeater(state, settings, false);
            applyProcessHeaterPower(state, settings, 50);
            {
                float tailsSpeed = (settings.rectParams.bodySpeedMlHKw * (settings.equipment.heaterPowerW / 1000.0f)) * 0.6f;
                Pump::start(tailsSpeed);
                const float tailsCollected = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
                state.stats.tailsVolume = tailsCollected;
                ControlV2::ReasonCodeV2 tailsExitReason = ControlV2::ReasonCodeV2::NONE;
                if (tailsTargetMl > 0.0f && tailsCollected >= tailsTargetMl) {
                    tailsExitReason = ControlV2::ReasonCodeV2::RC_TAILS_TARGET_REACHED;
                } else if (state.temps.valid[TEMP_CUBE] &&
                           state.temps.cube >= pressureAdjustedCubeTemp(RECT_CUBE_FINISH_BASE_C, state)) {
                    tailsExitReason = ControlV2::ReasonCodeV2::RC_TAILS_TARGET_REACHED;
                }
                if (tailsExitReason != ControlV2::ReasonCodeV2::NONE) {
                    LOG_I("FSM: TAILS -> FINISH");
                    ControlV2::notePhaseTransition(Mode::RECTIFICATION,
                                                   static_cast<uint16_t>(RectPhase::TAILS),
                                                   static_cast<uint16_t>(RectPhase::FINISH),
                                                   tailsExitReason);
                    state.rectPhase = RectPhase::FINISH;
                    setPhaseStartTime(now);
                }
            }
            break;

        case RectPhase::FINISH:
            applyBoosterHeater(state, settings, false);
            Heater::setPower(0);
            Pump::stop();
            Valves::setWater(true);
            if (elapsed > 5 * 60 * 1000UL) {
                Valves::closeAll();
                ControlV2::notePhaseTransition(Mode::RECTIFICATION,
                                               static_cast<uint16_t>(RectPhase::FINISH),
                                               static_cast<uint16_t>(RectPhase::IDLE),
                                               ControlV2::ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE);
                state.rectPhase = RectPhase::IDLE;
                state.mode = Mode::IDLE;
                MQTT::publishNotification("Процесс завершён", "Процесс завершён! Охлаждение выключено.", "success");
            }
            break;
        default: break;
    }
}

} // namespace Rectification
} // namespace FSM
