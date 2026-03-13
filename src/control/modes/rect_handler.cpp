#include "../fsm_utils.h"
#include "../../drivers/heater.h"
#include "../../drivers/pump.h"
#include "../../drivers/valves.h"
#include "../../drivers/sensors.h"
#include "../watt_control.h"
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

    switch (state.rectPhase) {
        case RectPhase::HEATING:
            Heater::setPower(100);
            if (state.temps.cube >= getWaterAutoStartTempC(settings)) {
                Valves::setWater(true);
            }
            if (state.temps.valid[TEMP_COLUMN_BOTTOM] && state.temps.columnBottom > 78.0f) {
                LOG_I("FSM: HEATING -> STABILIZATION");
                state.rectPhase = RectPhase::STABILIZATION;
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(state.pump.totalVolumeMl);
                bodyInitialized = false;
                MQTT::publishNotification("Фаза: Стабилизация", "Разогрев завершён, начата стабилизация колонны", "info");
            }
            break;

        case RectPhase::STABILIZATION:
            Valves::setHeads(false);
            Pump::stop();
            Valves::setWater(true);
            Heater::setPower(getProcessHeaterPower(state, settings, 70));
            if (elapsed > settings.rectParams.stabilizationMin * 60 * 1000UL) {
                LOG_I("FSM: STABILIZATION -> HEADS");
                state.rectPhase = RectPhase::HEADS;
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(state.pump.totalVolumeMl);
                MQTT::publishNotification("Фаза: Отбор голов", "Стабилизация завершена, начат отбор голов", "info");
            }
            break;

        case RectPhase::HEADS: {
            Heater::setPower(getProcessHeaterPower(state, settings, 60));
            float headsSpeed = settings.rectParams.headsSpeedMlHKw * (settings.equipment.heaterPowerW / 1000.0f);
            Pump::start(headsSpeed);
            Valves::setHeads(true);
            Valves::setWater(true);
            const float headsCollected = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
            state.stats.headsVolume = headsCollected;
            if (headsTargetMl > 0.0f && headsCollected >= headsTargetMl) {
                LOG_I("FSM: HEADS -> POST_HEADS_STABILIZATION (%.0f/%.0f ml)", headsCollected, headsTargetMl);
                state.rectPhase = RectPhase::POST_HEADS_STABILIZATION;
                setPhaseStartTime(now);
                Pump::stop();
                Valves::setHeads(false);
                MQTT::publishNotification("Фаза: Стабилизация после голов", "Отбор голов завершён, стабилизация перед отбором тела", "info");
            }
            break;
        }

        case RectPhase::POST_HEADS_STABILIZATION:
            Pump::stop();
            Valves::setHeads(false);
            Valves::setWater(true);
            Heater::setPower(getProcessHeaterPower(state, settings, 65));
            if (elapsed > 5 * 60 * 1000UL) {
                LOG_I("FSM: POST_HEADS_STABILIZATION -> PURGE");
                state.rectPhase = RectPhase::PURGE;
                setPhaseStartTime(now);
            }
            break;

        case RectPhase::PURGE:
            Pump::stop();
            Valves::closeAll();
            Valves::setWater(true);
            Heater::setPower(getProcessHeaterPower(state, settings, 65));
            if (elapsed > settings.rectParams.purgeMin * 60 * 1000UL) {
                LOG_I("FSM: PURGE -> BODY");
                state.rectPhase = RectPhase::BODY;
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(state.pump.totalVolumeMl);
                bodyInitialized = false;
                SmartDecrement::reset();
                MQTT::publishNotification("Фаза: Отбор тела", "Продувка завершена, начат отбор тела (основной продукт)", "success");
            }
            break;

        case RectPhase::BODY:
            Valves::setHeads(false);
            Valves::setWater(true);
            Heater::setPower(getProcessHeaterPower(state, settings, 60));
            if (!bodyInitialized && state.temps.valid[TEMP_COLUMN_TOP]) {
                SmartDecrement::init(state.temps.columnTop);
                bodyInitialized = true;
            }
            {
                float bodySpeed = settings.rectParams.bodySpeedMlHKw * (settings.equipment.heaterPowerW / 1000.0f);
                Pump::start(bodySpeed);
                const float bodyCollected = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
                state.stats.bodyVolume = bodyCollected;
                if ((bodyTargetMl > 0.0f && bodyCollected >= bodyTargetMl) ||
                    (bodyInitialized && SmartDecrement::update(state, settings)) ||
                    (state.temps.valid[TEMP_CUBE] && state.temps.cube >= pressureAdjustedCubeTemp(RECT_CUBE_BODY_TO_TAILS_BASE_C, state))) {
                    LOG_I("FSM: BODY -> TAILS");
                    state.rectPhase = RectPhase::TAILS;
                    setPhaseStartTime(now);
                    setPhaseStartVolumeMl(state.pump.totalVolumeMl);
                }
            }
            break;

        case RectPhase::TAILS:
            Heater::setPower(getProcessHeaterPower(state, settings, 50));
            {
                float tailsSpeed = (settings.rectParams.bodySpeedMlHKw * (settings.equipment.heaterPowerW / 1000.0f)) * 0.6f;
                Pump::start(tailsSpeed);
                const float tailsCollected = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
                state.stats.tailsVolume = tailsCollected;
                if ((tailsTargetMl > 0.0f && tailsCollected >= tailsTargetMl) ||
                    (state.temps.valid[TEMP_CUBE] && state.temps.cube >= pressureAdjustedCubeTemp(RECT_CUBE_FINISH_BASE_C, state))) {
                    LOG_I("FSM: TAILS -> FINISH");
                    state.rectPhase = RectPhase::FINISH;
                    setPhaseStartTime(now);
                }
            }
            break;

        case RectPhase::FINISH:
            Heater::setPower(0);
            Pump::stop();
            Valves::setWater(true);
            if (elapsed > 5 * 60 * 1000UL) {
                Valves::closeAll();
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
