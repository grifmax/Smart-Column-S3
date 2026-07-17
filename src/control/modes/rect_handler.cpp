#include "../fsm_utils.h"
#include "../rect_takeoff.h"
#include "../rect_takeoff_logic.h"
#include "../../drivers/heater.h"
#include "../../drivers/valves.h"
#include "../../drivers/pump.h"
#include "../../drivers/sensors.h"
#include "../watt_control.h"
#include "../v2/reason_codes.h"
#include "../v2/safety_policy.h"
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
static ControlV2::RectificationPressureRuntimeV2 pressureControlRuntime;
static bool pressureEmergencyStopActive = false;

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

uint16_t percentToWatts(uint8_t percent, const Settings& settings) {
    const uint16_t heaterMaxW = getConfiguredHeaterPowerWatts(settings);
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(heaterMaxW) * percent) / 100U);
}

uint16_t applyRectificationHeaterPower(const SystemState& state,
                                       const Settings& settings,
                                       RectPhase phase,
                                       uint8_t fallbackPercent,
                                       uint32_t now) {
    if (!settings.rectParams.pressureControlEnabled) {
        pressureControlRuntime = ControlV2::RectificationPressureRuntimeV2{};
        pressureEmergencyStopActive = false;
        return applyProcessHeaterPower(
            state, settings,
            getPhasePowerPercent(settings.rectParams, phase, fallbackPercent));
    }

    const bool wasFallback = pressureControlRuntime.fallbackActive;
    const bool wasEmergency = pressureControlRuntime.emergencyReported;
    uint8_t requestedPercent =
        getPhasePowerPercent(settings.rectParams, phase, fallbackPercent);
    if (WattControl::isOverrideActive()) {
        requestedPercent = WattControl::update(state, settings);
    }

    const ControlV2::RectificationPressurePolicyV2 policy =
        ControlV2::SafetyPolicyV2::evaluateRectificationPressurePower(
            requestedPercent, state, settings, now, pressureControlRuntime);
    pressureEmergencyStopActive = policy.emergencyStop;

    if (policy.fallbackActive && !wasFallback) {
        LOG_W("Rectification pressure control fallback: calibrated and stable pressure signal is required");
    } else if (!policy.fallbackActive && wasFallback) {
        LOG_I("Rectification pressure control resumed after stable calibrated signal");
    }

    if (policy.emergencyStop) {
        Heater::setPowerWatts(0);
        RectTakeoff::stop();
        if (!wasEmergency) {
            pressureControlRuntime.emergencyReported = true;
            LOG_E("Rectification pressure safety limit reached: %.1f mmHg >= %.1f mmHg",
                  policy.pressureMmHg, policy.safetyLimitMmHg);
            MQTT::publishNotification(
                "Pressure safety stop",
                "Rectification heater disabled at the configured pressure safety limit",
                "error");
        }
        return 0;
    }

    pressureControlRuntime.emergencyReported = false;
    uint8_t appliedPercent = policy.appliedPowerPercent;
    const ControlV2::ActiveLimitsV2& limits =
        ControlV2::SafetySupervisorV2::getLiveLimits();
    if (limits.powerCapped && limits.maxHeaterPowerPercent < appliedPercent) {
        appliedPercent = limits.maxHeaterPowerPercent;
    }
    const uint16_t appliedWatts = percentToWatts(appliedPercent, settings);
    Heater::setPowerWatts(appliedWatts);
    return appliedWatts;
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
    command.requestedEquivalentRateMlH =
        equivalentRateMlH > 0.0f ? equivalentRateMlH : 0.0f;
    const float pumpMaxRateMlH = Pump::getMaxSpeedMlH();
    command.rateLimited =
        command.backendType == RectTakeoffBackendType::PUMP &&
        command.requestedEquivalentRateMlH > pumpMaxRateMlH;
    command.equivalentRateMlH = command.rateLimited
        ? pumpMaxRateMlH
        : command.requestedEquivalentRateMlH;
    command.enabled = command.equivalentRateMlH > 0.0f;
    command.periodicTakeoff =
        settings.rectParams.refluxMode != RectRefluxMode::ML_H;
    command.periodicTakeoffActive =
        !command.periodicTakeoff ||
        isTakeoffWindowOpen(settings.rectParams, elapsedMs, &command.periodicCycleMs,
                            &command.periodicOpenMs);
    command.fullReflux = RectTakeoffLogic::shouldUseFullReflux(
        command.enabled, command.periodicTakeoff,
        command.periodicTakeoffActive);
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

bool isActiveCollectionContainerLevelReached(const Settings& settings) {
    if (!settings.equipment.bodyLevelSensorEnabled) {
        return false;
    }

    int16_t adc = 0;
    float voltage = 0.0f;
    if (!Sensors::readAds1115Channel(ADS_CHANNEL_LEVEL_BODY, adc, voltage)) {
        return false;
    }

    return settings.equipment.bodyLevelTriggerAbove
        ? voltage >= settings.equipment.bodyLevelThresholdV
        : voltage <= settings.equipment.bodyLevelThresholdV;
}

void pauseForActiveBodyContainerLevel(SystemState& state,
                                      uint8_t containerCount,
                                      float bodyCollectedMl) {
    RectTakeoff::stop();
    state.paused = true;
    state.rectBodyContainerLevelReached = true;

    const uint8_t activeContainer = state.rectBodyContainerIndex + 1;
    const bool hasNextContainer = activeContainer < containerCount;
    if (hasNextContainer) {
        ++state.rectBodyContainerIndex;
        state.rectBodyContainerVolumeMl = 0.0f;
        state.rectBodyContainerStartVolumeMl = bodyCollectedMl;
    }
    const uint8_t targetContainer = state.rectBodyContainerIndex + 1;
    const String message =
        hasNextContainer
            ? String("Датчик уровня сработал в ёмкости ") + activeContainer +
                  " из " + containerCount + ". Установите ёмкость " +
                  targetContainer + " и перенесите датчик; отбор продолжится после снятия сигнала"
            : String("Датчик уровня сработал в последней ёмкости ") +
                  activeContainer + " из " + containerCount +
                  ". Смените тару/перенесите датчик и дождитесь снятия сигнала";
    MQTT::publishNotification("Уровень активной ёмкости тела",
                              message.c_str(), "warning");
    LOG_W("Rectification BODY active container %u/%u level sensor reached; takeoff paused",
          activeContainer, containerCount);
}

void pauseForHeadsContainerLevel(SystemState& state) {
    RectTakeoff::stop();
    state.paused = true;
    state.rectHeadsContainerLevelReached = true;
    MQTT::publishNotification(
        "Уровень ёмкости голов",
        "Датчик уровня сработал в текущей ёмкости голов. Смените тару/перенесите датчик и дождитесь снятия сигнала",
        "warning");
    LOG_W("Rectification HEADS active container level sensor reached; takeoff paused");
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

void initSession(SystemState& state, const Settings& settings) {
    headsTargetMl = 0.0f;
    bodyTargetMl = 0.0f;
    tailsTargetMl = 0.0f;
    bodyReferenceReady = false;
    bodyBaseTempC = 0.0f;
    bodyPressureConfirmStartMs = 0;
    pressureControlRuntime = ControlV2::RectificationPressureRuntimeV2{};
    pressureEmergencyStopActive = false;
    state.rectBodyContainerIndex = 0;
    state.rectBodyContainerVolumeMl = 0.0f;
    state.rectBodyContainerStartVolumeMl = 0.0f;
    state.rectBodyContainerLevelReached = false;
    state.rectHeadsContainerLevelReached = false;
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
            applyRectificationHeaterPower(
                state, settings, RectPhase::STABILIZATION, 70, now);
            if (pressureEmergencyStopActive) break;
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
            applyRectificationHeaterPower(state, settings, RectPhase::HEADS, 60, now);
            if (pressureEmergencyStopActive) break;
            const bool headsContainerLevelReached =
                isActiveCollectionContainerLevelReached(settings);
            if (state.rectHeadsContainerLevelReached) {
                if (headsContainerLevelReached) {
                    RectTakeoff::stop();
                    break;
                }
                state.rectHeadsContainerLevelReached = false;
            }
            if (headsContainerLevelReached) {
                pauseForHeadsContainerLevel(state);
                break;
            }
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
                state.rectHeadsContainerLevelReached = false;
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
            applyRectificationHeaterPower(
                state, settings, RectPhase::STABILIZATION, 65, now);
            if (pressureEmergencyStopActive) break;
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
            applyRectificationHeaterPower(
                state, settings, RectPhase::STABILIZATION, 65, now);
            if (pressureEmergencyStopActive) break;
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
            applyRectificationHeaterPower(state, settings, RectPhase::BODY, 60, now);
            if (pressureEmergencyStopActive) break;

            if (!bodyReferenceReady && state.temps.valid[TEMP_COLUMN_TOP]) {
                bodyBaseTempC = state.temps.columnTop;
                bodyReferenceReady = true;
                if (settings.rectParams.usePbMode == 0) {
                    SmartDecrement::init(bodyBaseTempC);
                }
            }

            const float bodyCollected =
                getCurrentTakeoffTotalVolumeMl(state, settings) -
                getPhaseStartVolumeMl();
            state.stats.bodyVolume = bodyCollected;
            const uint8_t containerCount =
                constrain(settings.rectParams.bodyContainerCount, 1, 8);
            const float perContainerTarget = bodyTargetMl /
                static_cast<float>(containerCount);
            state.rectBodyContainerVolumeMl = max(
                0.0f, bodyCollected - state.rectBodyContainerStartVolumeMl);

            const bool activeContainerLevelReached =
                isActiveCollectionContainerLevelReached(settings);
            if (state.rectBodyContainerLevelReached) {
                // The signal belongs to the container currently installed by
                // the operator. Do not restart takeoff until it is clear.
                if (activeContainerLevelReached) {
                    RectTakeoff::stop();
                    break;
                }
                state.rectBodyContainerLevelReached = false;
            }
            if (activeContainerLevelReached) {
                pauseForActiveBodyContainerLevel(state, containerCount,
                                                 bodyCollected);
                break;
            }

            if (containerCount > 1 &&
                state.rectBodyContainerIndex + 1 < containerCount &&
                state.rectBodyContainerVolumeMl >= perContainerTarget) {
                RectTakeoff::stop();
                ++state.rectBodyContainerIndex;
                state.rectBodyContainerVolumeMl = 0.0f;
                state.rectBodyContainerStartVolumeMl = bodyCollected;
                state.paused = true;
                const String containerMessage =
                    String("Установите ёмкость ") +
                    String(state.rectBodyContainerIndex + 1) + " из " +
                    String(containerCount) + " и нажмите Продолжить";
                MQTT::publishNotification("Смена ёмкости тела",
                                          containerMessage.c_str(), "warning");
                LOG_I("Rectification BODY container %u/%u completed; takeoff paused",
                      state.rectBodyContainerIndex, containerCount);
                break;
            }

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
                RectTakeoff::stop();
                ControlV2::notePhaseTransition(
                    Mode::RECTIFICATION, static_cast<uint16_t>(RectPhase::BODY),
                    static_cast<uint16_t>(RectPhase::TAILS), bodyExitReason);
                state.rectPhase = RectPhase::TAILS;
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(
                    getCurrentTakeoffTotalVolumeMl(state, settings));
                bodyPressureConfirmStartMs = 0;
                break;
            }

            // Priority while body is collected: safety/pause is enforced by
            // the caller, then level and container switching above, then PB
            // phase completion. Only a continuing BODY phase reaches CHIM;
            // the reflux mode finally gates the corrected request in
            // buildTakeoffCommand().
            const float baseBodySpeed =
                getDirectTakeoffSpeedMlH(settings, RectPhase::BODY);
            const float requestedBodyRateMlH =
                applyChimCompensation(baseBodySpeed, state, settings, elapsed);
            RectTakeoff::apply(buildTakeoffCommand(
                settings, RectTakeoffFraction::BODY, requestedBodyRateMlH,
                elapsed));
            break;
        }

        case RectPhase::TAILS: {
            applyBoosterHeater(state, settings, false);
            applyRectificationHeaterPower(state, settings, RectPhase::TAILS, 50, now);
            if (pressureEmergencyStopActive) break;
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
