#include "../fsm_utils.h"
#include "../rect_takeoff.h"
#include "../../drivers/heater.h"
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

namespace {

static float headsTargetMl = 0.0f;
static float bodyTargetMl = 0.0f;
static float tailsTargetMl = 0.0f;
static bool bodyReferenceReady = false;
static float bodyBaseTempC = 0.0f;
static uint32_t bodyPressureConfirmStartMs = 0;

uint8_t getPhasePowerPercent(const RectParams& params, RectPhase phase,
                             uint8_t fallbackPercent) {
    switch (phase) {
        case RectPhase::STABILIZATION:
        case RectPhase::POST_HEADS_STABILIZATION:
        case RectPhase::PURGE:
            return params.phasePowerPercent[RECT_POWER_STABILIZATION];
        case RectPhase::HEADS:
            return params.phasePowerPercent[RECT_POWER_HEADS];
        case RectPhase::BODY:
            return params.phasePowerPercent[RECT_POWER_BODY];
        case RectPhase::TAILS:
            return params.phasePowerPercent[RECT_POWER_TAILS];
        default:
            return fallbackPercent;
    }
}

float getDirectTakeoffSpeedMlH(const Settings& settings, RectPhase phase) {
    return getRectificationDirectTakeoffSpeedMlH(settings, phase);
}

float getCurrentTakeoffTotalVolumeMl(const SystemState& state,
                                     const Settings& settings) {
    if (settings.rectParams.takeoffBackendType == RectTakeoffBackendType::PUMP) {
        return state.pump.totalVolumeMl;
    }
    return RectTakeoff::getFeedback().sessionVolumeMl;
}

bool isTakeoffWindowOpen(const RectParams& params, uint32_t elapsedMs,
                         uint32_t* cycleMsOut = nullptr,
                         uint32_t* openMsOut = nullptr) {
    if (params.refluxMode == RectRefluxMode::ML_H) {
        if (cycleMsOut) {
            *cycleMsOut = 0;
        }
        if (openMsOut) {
            *openMsOut = 0;
        }
        return true;
    }

    if (params.refluxMode == RectRefluxMode::SR_RATIO) {
        if (params.srRatio <= 0.0f) {
            if (cycleMsOut) {
                *cycleMsOut = RECT_SR_CONTROL_CYCLE_SEC * 1000UL;
            }
            if (openMsOut) {
                *openMsOut = 0;
            }
            return false;
        }
        const uint32_t cycleMs = RECT_SR_CONTROL_CYCLE_SEC * 1000UL;
        const float onMsFloat = static_cast<float>(cycleMs) / (params.srRatio + 1.0f);
        uint32_t onMs = static_cast<uint32_t>(onMsFloat);
        if (onMs == 0) {
            onMs = 1;
        }
        if (cycleMsOut) {
            *cycleMsOut = cycleMs;
        }
        if (openMsOut) {
            *openMsOut = onMs;
        }
        return (elapsedMs % cycleMs) < onMs;
    }

    const uint32_t cycleSec =
        params.autonomousCycleSec > 0 ? params.autonomousCycleSec : 1;
    const uint32_t pauseSec =
        params.autonomousPauseSec < cycleSec ? params.autonomousPauseSec : (cycleSec - 1);
    const uint32_t cycleMs = cycleSec * 1000UL;
    const uint32_t onMs = (cycleSec - pauseSec) * 1000UL;
    if (cycleMsOut) {
        *cycleMsOut = cycleMs;
    }
    if (openMsOut) {
        *openMsOut = onMs;
    }
    return (elapsedMs % cycleMs) < onMs;
}

RectTakeoffCommand buildTakeoffCommand(const Settings& settings,
                                       RectTakeoffFraction fraction,
                                       float equivalentRateMlH,
                                       uint32_t elapsedMs) {
    RectTakeoffCommand command;
    command.backendType = settings.rectParams.takeoffBackendType;
    command.fraction = fraction;
    command.equivalentRateMlH = equivalentRateMlH > 0.0f ? equivalentRateMlH : 0.0f;
    command.enabled = command.equivalentRateMlH > 0.0f;
    command.fullReflux = !command.enabled;
    command.periodicTakeoff =
        settings.rectParams.refluxMode != RectRefluxMode::ML_H;
    command.periodicTakeoffActive =
        !command.periodicTakeoff ||
        isTakeoffWindowOpen(settings.rectParams, elapsedMs, &command.periodicCycleMs,
                            &command.periodicOpenMs);
    return command;
}

float applyChimCompensation(float baseSpeedMlH, const SystemState& state,
                            const Settings& settings, uint32_t elapsedMs) {
    if (baseSpeedMlH <= 0.0f) {
        return 0.0f;
    }

    const RectParams& params = settings.rectParams;
    const bool chimEnabled =
        fabsf(params.chimAutoPercent) > 0.001f ||
        fabsf(params.chimTimePerH) > 0.001f ||
        fabsf(params.chimBegPercent) > 0.001f;
    if (!chimEnabled || !bodyReferenceReady ||
        !state.temps.valid[TEMP_COLUMN_TOP]) {
        return baseSpeedMlH;
    }

    const float deltaTempC = state.temps.columnTop - bodyBaseTempC;
    const float tempCorrectionPercent = max(0.0f, deltaTempC) * params.chimAutoPercent;
    const float timeCorrectionMlH =
        (static_cast<float>(elapsedMs) / 3600000.0f) * params.chimTimePerH;
    const float adjustedPercent =
        100.0f + params.chimBegPercent - tempCorrectionPercent;

    float adjustedSpeed = baseSpeedMlH * (adjustedPercent / 100.0f);
    adjustedSpeed -= timeCorrectionMlH;

    const float minSpeed = baseSpeedMlH * (params.chimMinPercent / 100.0f);
    if (adjustedSpeed < minSpeed) {
        adjustedSpeed = minSpeed;
    }
    if (adjustedSpeed < 0.0f) {
        adjustedSpeed = 0.0f;
    }
    return adjustedSpeed;
}

bool confirmBodyEndByPressure(const SystemState& state, const Settings& settings,
                              uint32_t now) {
    if (!state.pressure.ok) {
        bodyPressureConfirmStartMs = 0;
        return false;
    }

    float workThreshold = 0.0f;
    float warnThreshold = 0.0f;
    float critThreshold = 0.0f;
    WattControl::getThresholds(workThreshold, warnThreshold, critThreshold);

    const float threshold = warnThreshold > 0.0f
        ? warnThreshold
        : settings.safety.pressureMaxMmHg;
    if (state.pressure.cube < threshold) {
        bodyPressureConfirmStartMs = 0;
        return false;
    }

    if (bodyPressureConfirmStartMs == 0) {
        bodyPressureConfirmStartMs = now;
    }

    return settings.rectParams.timpPbMs == 0 ||
           (now - bodyPressureConfirmStartMs) >= settings.rectParams.timpPbMs;
}

void calculateTargets(const SystemState& state, const Settings& settings) {
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

    if (headsTargetMl < 10.0f) {
        headsTargetMl = 10.0f;
    }
}

} // namespace

void getTargets(float& heads, float& body, float& tails) {
    heads = headsTargetMl;
    body = bodyTargetMl;
    tails = tailsTargetMl;
}

void initSession(const SystemState& state, const Settings& settings) {
    headsTargetMl = 0.0f;
    bodyTargetMl = 0.0f;
    tailsTargetMl = 0.0f;
    bodyReferenceReady = false;
    bodyBaseTempC = 0.0f;
    bodyPressureConfirmStartMs = 0;
    calculateTargets(state, settings);
    RectTakeoff::beginSession(settings);
}

void update(SystemState& state, const Settings& settings) {
    const uint32_t now = millis();
    const uint32_t startTime = getPhaseStartTime();
    const uint32_t elapsed = now - startTime;
    const ControlV2::ActiveLimitsV2& liveLimits =
        ControlV2::SafetySupervisorV2::getLiveLimits();

    switch (state.rectPhase) {
        case RectPhase::HEATING:
            applyBoosterHeater(state, settings, true);
            applyFullHeatPower(settings);
            if (state.temps.cube >= getWaterAutoStartTempC(settings)) {
                Valves::setWater(true);
            }
            if (state.temps.valid[TEMP_COLUMN_BOTTOM] &&
                state.temps.columnBottom > 78.0f) {
                LOG_I("FSM: HEATING -> STABILIZATION");
                ControlV2::notePhaseTransition(
                    Mode::RECTIFICATION,
                    static_cast<uint16_t>(RectPhase::HEATING),
                    static_cast<uint16_t>(RectPhase::STABILIZATION),
                    ControlV2::ReasonCodeV2::RC_HEATING_COMPLETE);
                state.rectPhase = RectPhase::STABILIZATION;
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(
                    getCurrentTakeoffTotalVolumeMl(state, settings));
                bodyReferenceReady = false;
                bodyPressureConfirmStartMs = 0;
                MQTT::publishNotification(
                    "Р¤Р°Р·Р°: РЎС‚Р°Р±РёР»РёР·Р°С†РёСЏ",
                    "Р Р°Р·РѕРіСЂРµРІ Р·Р°РІРµСЂС€С‘РЅ, РЅР°С‡Р°С‚Р° СЃС‚Р°Р±РёР»РёР·Р°С†РёСЏ РєРѕР»РѕРЅРЅС‹",
                    "info");
            }
            break;

        case RectPhase::STABILIZATION:
            applyBoosterHeater(state, settings, false);
            RectTakeoff::stop();
            Valves::setWater(true);
            applyProcessHeaterPower(
                state, settings,
                getPhasePowerPercent(settings.rectParams, RectPhase::STABILIZATION, 70));
            if (!liveLimits.phaseAdvanceBlocked &&
                elapsed > settings.rectParams.stabilizationMin * 60 * 1000UL) {
                LOG_I("FSM: STABILIZATION -> HEADS");
                ControlV2::notePhaseTransition(
                    Mode::RECTIFICATION,
                    static_cast<uint16_t>(RectPhase::STABILIZATION),
                    static_cast<uint16_t>(RectPhase::HEADS),
                    ControlV2::ReasonCodeV2::RC_STABILIZATION_TIMER_OK);
                state.rectPhase = RectPhase::HEADS;
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(
                    getCurrentTakeoffTotalVolumeMl(state, settings));
                bodyPressureConfirmStartMs = 0;
                MQTT::publishNotification(
                    "Р¤Р°Р·Р°: РћС‚Р±РѕСЂ РіРѕР»РѕРІ",
                    "РЎС‚Р°Р±РёР»РёР·Р°С†РёСЏ Р·Р°РІРµСЂС€РµРЅР°, РЅР°С‡Р°С‚ РѕС‚Р±РѕСЂ РіРѕР»РѕРІ",
                    "info");
            }
            break;

        case RectPhase::HEADS: {
            applyBoosterHeater(state, settings, false);
            applyProcessHeaterPower(
                state, settings,
                getPhasePowerPercent(settings.rectParams, RectPhase::HEADS, 60));
            const float headsSpeed = getDirectTakeoffSpeedMlH(settings, RectPhase::HEADS);
            RectTakeoff::apply(buildTakeoffCommand(
                settings, RectTakeoffFraction::HEADS, headsSpeed, elapsed));
            Valves::setWater(true);
            const float headsCollected =
                getCurrentTakeoffTotalVolumeMl(state, settings) -
                getPhaseStartVolumeMl();
            state.stats.headsVolume = headsCollected;
            if (headsTargetMl > 0.0f && headsCollected >= headsTargetMl) {
                LOG_I("FSM: HEADS -> POST_HEADS_STABILIZATION (%.0f/%.0f ml)",
                      headsCollected, headsTargetMl);
                ControlV2::notePhaseTransition(
                    Mode::RECTIFICATION,
                    static_cast<uint16_t>(RectPhase::HEADS),
                    static_cast<uint16_t>(RectPhase::POST_HEADS_STABILIZATION),
                    ControlV2::ReasonCodeV2::RC_HEADS_VOLUME_REACHED);
                state.rectPhase = RectPhase::POST_HEADS_STABILIZATION;
                setPhaseStartTime(now);
                RectTakeoff::stop();
                MQTT::publishNotification(
                    "Р¤Р°Р·Р°: РЎС‚Р°Р±РёР»РёР·Р°С†РёСЏ РїРѕСЃР»Рµ РіРѕР»РѕРІ",
                    "РћС‚Р±РѕСЂ РіРѕР»РѕРІ Р·Р°РІРµСЂС€С‘РЅ, СЃС‚Р°Р±РёР»РёР·Р°С†РёСЏ РїРµСЂРµРґ РѕС‚Р±РѕСЂРѕРј С‚РµР»Р°",
                    "info");
            }
            break;
        }

        case RectPhase::POST_HEADS_STABILIZATION:
            applyBoosterHeater(state, settings, false);
            RectTakeoff::stop();
            Valves::setWater(true);
            applyProcessHeaterPower(
                state, settings,
                getPhasePowerPercent(settings.rectParams, RectPhase::STABILIZATION, 65));
            if (!liveLimits.phaseAdvanceBlocked && elapsed > 5 * 60 * 1000UL) {
                LOG_I("FSM: POST_HEADS_STABILIZATION -> PURGE");
                ControlV2::notePhaseTransition(
                    Mode::RECTIFICATION,
                    static_cast<uint16_t>(RectPhase::POST_HEADS_STABILIZATION),
                    static_cast<uint16_t>(RectPhase::PURGE),
                    ControlV2::ReasonCodeV2::RC_POST_HEADS_STABILIZATION_COMPLETE);
                state.rectPhase = RectPhase::PURGE;
                setPhaseStartTime(now);
            }
            break;

        case RectPhase::PURGE:
            applyBoosterHeater(state, settings, false);
            RectTakeoff::stop();
            Valves::closeAll();
            Valves::setWater(true);
            applyProcessHeaterPower(
                state, settings,
                getPhasePowerPercent(settings.rectParams, RectPhase::STABILIZATION, 65));
            if (!liveLimits.phaseAdvanceBlocked &&
                elapsed > settings.rectParams.purgeMin * 60 * 1000UL) {
                LOG_I("FSM: PURGE -> BODY");
                ControlV2::notePhaseTransition(
                    Mode::RECTIFICATION, static_cast<uint16_t>(RectPhase::PURGE),
                    static_cast<uint16_t>(RectPhase::BODY),
                    ControlV2::ReasonCodeV2::RC_PURGE_COMPLETE);
                state.rectPhase = RectPhase::BODY;
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(
                    getCurrentTakeoffTotalVolumeMl(state, settings));
                bodyReferenceReady = false;
                bodyPressureConfirmStartMs = 0;
                SmartDecrement::reset();
                MQTT::publishNotification(
                    "Р¤Р°Р·Р°: РћС‚Р±РѕСЂ С‚РµР»Р°",
                    "РџСЂРѕРґСѓРІРєР° Р·Р°РІРµСЂС€РµРЅР°, РЅР°С‡Р°С‚ РѕС‚Р±РѕСЂ С‚РµР»Р° (РѕСЃРЅРѕРІРЅРѕР№ РїСЂРѕРґСѓРєС‚)",
                    "success");
            }
            break;

        case RectPhase::BODY: {
            applyBoosterHeater(state, settings, false);
            Valves::setWater(true);
            applyProcessHeaterPower(
                state, settings,
                getPhasePowerPercent(settings.rectParams, RectPhase::BODY, 60));

            if (!bodyReferenceReady && state.temps.valid[TEMP_COLUMN_TOP]) {
                bodyBaseTempC = state.temps.columnTop;
                bodyReferenceReady = true;
                if (settings.rectParams.usePbMode == 0) {
                    SmartDecrement::init(bodyBaseTempC);
                }
            }

            const float baseBodySpeed =
                getDirectTakeoffSpeedMlH(settings, RectPhase::BODY);
            const float bodySpeed =
                applyChimCompensation(baseBodySpeed, state, settings, elapsed);
            RectTakeoff::apply(buildTakeoffCommand(
                settings, RectTakeoffFraction::BODY, bodySpeed, elapsed));

            const float bodyCollected =
                getCurrentTakeoffTotalVolumeMl(state, settings) -
                getPhaseStartVolumeMl();
            state.stats.bodyVolume = bodyCollected;

            ControlV2::ReasonCodeV2 bodyExitReason =
                ControlV2::ReasonCodeV2::NONE;
            if (bodyTargetMl > 0.0f && bodyCollected >= bodyTargetMl) {
                bodyExitReason =
                    ControlV2::ReasonCodeV2::RC_BODY_TARGET_VOLUME_REACHED;
            } else {
                const bool cubeThresholdReached =
                    state.temps.valid[TEMP_CUBE] &&
                    state.temps.cube >= pressureAdjustedCubeTemp(
                        RECT_CUBE_BODY_TO_TAILS_BASE_C, state);

                if (settings.rectParams.usePbMode == 0) {
                    if (bodyReferenceReady &&
                        SmartDecrement::update(state, settings)) {
                        bodyExitReason =
                            ControlV2::ReasonCodeV2::RC_BODY_END_DETECTED;
                    } else if (cubeThresholdReached) {
                        bodyExitReason =
                            ControlV2::ReasonCodeV2::RC_BODY_END_DETECTED;
                    }
                } else if (settings.rectParams.usePbMode == 2) {
                    if (confirmBodyEndByPressure(state, settings, now)) {
                        bodyExitReason =
                            ControlV2::ReasonCodeV2::RC_BODY_END_DETECTED;
                    }
                } else if (settings.rectParams.usePbMode == 3) {
                    if (cubeThresholdReached &&
                        confirmBodyEndByPressure(state, settings, now)) {
                        bodyExitReason =
                            ControlV2::ReasonCodeV2::RC_BODY_END_DETECTED;
                    } else if (!cubeThresholdReached) {
                        bodyPressureConfirmStartMs = 0;
                    }
                }
            }

            if (bodyExitReason != ControlV2::ReasonCodeV2::NONE) {
                LOG_I("FSM: BODY -> TAILS");
                ControlV2::notePhaseTransition(
                    Mode::RECTIFICATION, static_cast<uint16_t>(RectPhase::BODY),
                    static_cast<uint16_t>(RectPhase::TAILS), bodyExitReason);
                state.rectPhase = RectPhase::TAILS;
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(
                    getCurrentTakeoffTotalVolumeMl(state, settings));
                bodyPressureConfirmStartMs = 0;
            }
            break;
        }

        case RectPhase::TAILS: {
            applyBoosterHeater(state, settings, false);
            applyProcessHeaterPower(
                state, settings,
                getPhasePowerPercent(settings.rectParams, RectPhase::TAILS, 50));
            const float tailsSpeed =
                getDirectTakeoffSpeedMlH(settings, RectPhase::TAILS);
            RectTakeoff::apply(buildTakeoffCommand(
                settings, RectTakeoffFraction::TAILS, tailsSpeed, elapsed));
            const float tailsCollected =
                getCurrentTakeoffTotalVolumeMl(state, settings) -
                getPhaseStartVolumeMl();
            state.stats.tailsVolume = tailsCollected;
            ControlV2::ReasonCodeV2 tailsExitReason =
                ControlV2::ReasonCodeV2::NONE;
            if (tailsTargetMl > 0.0f && tailsCollected >= tailsTargetMl) {
                tailsExitReason =
                    ControlV2::ReasonCodeV2::RC_TAILS_TARGET_REACHED;
            } else if (state.temps.valid[TEMP_CUBE] &&
                       state.temps.cube >= pressureAdjustedCubeTemp(
                           RECT_CUBE_FINISH_BASE_C, state)) {
                tailsExitReason =
                    ControlV2::ReasonCodeV2::RC_TAILS_TARGET_REACHED;
            }
            if (tailsExitReason != ControlV2::ReasonCodeV2::NONE) {
                LOG_I("FSM: TAILS -> FINISH");
                ControlV2::notePhaseTransition(
                    Mode::RECTIFICATION,
                    static_cast<uint16_t>(RectPhase::TAILS),
                    static_cast<uint16_t>(RectPhase::FINISH), tailsExitReason);
                state.rectPhase = RectPhase::FINISH;
                setPhaseStartTime(now);
            }
            break;
        }

        case RectPhase::FINISH:
            applyBoosterHeater(state, settings, false);
            Heater::setPower(0);
            RectTakeoff::stop();
            Valves::setWater(true);
            if (elapsed > 5 * 60 * 1000UL) {
                Valves::closeAll();
                ControlV2::notePhaseTransition(
                    Mode::RECTIFICATION,
                    static_cast<uint16_t>(RectPhase::FINISH),
                    static_cast<uint16_t>(RectPhase::IDLE),
                    ControlV2::ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE);
                state.rectPhase = RectPhase::IDLE;
                state.mode = Mode::IDLE;
                MQTT::publishNotification(
                    "РџСЂРѕС†РµСЃСЃ Р·Р°РІРµСЂС€С‘РЅ",
                    "РџСЂРѕС†РµСЃСЃ Р·Р°РІРµСЂС€С‘РЅ! РћС…Р»Р°Р¶РґРµРЅРёРµ РІС‹РєР»СЋС‡РµРЅРѕ.",
                    "success");
            }
            break;

        default:
            break;
    }
}

} // namespace Rectification
} // namespace FSM
