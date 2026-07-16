#include "../fsm_utils.h"
#include "../fraction_program_logic.h"
#include "../v2/reason_codes.h"
#include "../v2/status_adapter.h"
#include "../rect_takeoff.h"
#include "../../drivers/heater.h"
#include "../../drivers/buzzer.h"
#include "../../drivers/pump.h"
#include "../../drivers/sensors.h"
#include "../../drivers/valves.h"
#include "../../interface/mqtt.h"
#include "../../storage/logger.h"
#include <Arduino.h>

namespace FSM {
namespace Distillation {

struct ParamsRuntime {
    float speedMlH = 500.0f;
    float headsVolumeMl = 0.0f;
    float targetVolumeMl = 0.0f;
    float endTempC = 96.0f;
    uint16_t powerWatts = DEFAULT_HEATER_POWER_W;
};

static ParamsRuntime g_params;

namespace {

ControlV2::ReasonCodeV2 getBodyExitReason(bool endByTemp, bool endByVolume) {
    if (endByTemp) {
        return ControlV2::ReasonCodeV2::RC_DISTILLATION_END_TEMP_REACHED;
    }
    if (endByVolume) {
        return ControlV2::ReasonCodeV2::RC_DISTILLATION_TARGET_VOLUME_REACHED;
    }
    return ControlV2::ReasonCodeV2::RC_BODY_END_DETECTED;
}

const char* getBodyExitMessage(bool endByTemp, bool endByVolume) {
    if (endByTemp) {
        return "Distillation body ended by cube temperature";
    }
    if (endByVolume) {
        return "Distillation target volume reached";
    }
    return "Distillation body end detected";
}

RectTakeoffBackendType getCurrentTakeoffBackendType(const Settings& settings) {
    return settings.distillationUi.takeoffBackendType;
}

float getCurrentTakeoffTotalVolumeMl(const SystemState& state, const Settings& settings) {
    return getCurrentTakeoffBackendType(settings) == RectTakeoffBackendType::PUMP
        ? state.pump.totalVolumeMl
        : RectTakeoff::getFeedback().sessionVolumeMl;
}

float getCurrentFractionCollectedMl(const SystemState& state, const Settings& settings) {
    const float collectedMl =
        getCurrentTakeoffTotalVolumeMl(state, settings) - state.fractionProgram.stepStartVolumeMl;
    return collectedMl > 0.0f ? collectedMl : 0.0f;
}

RectTakeoffFraction routeIndexToTakeoffFraction(uint8_t routeIndex) {
    switch (routeIndex) {
        case static_cast<uint8_t>(Fraction::HEADS):
            return RectTakeoffFraction::HEADS;
        case static_cast<uint8_t>(Fraction::TAILS):
            return RectTakeoffFraction::TAILS;
        case static_cast<uint8_t>(Fraction::BODY):
        default:
            return RectTakeoffFraction::BODY;
    }
}

void applyFractionTakeoff(const Settings& settings, RectTakeoffFraction fraction, float rateMlH) {
    RectTakeoffCommand command{};
    command.backendType = getCurrentTakeoffBackendType(settings);
    command.fraction = fraction;
    command.equivalentRateMlH = rateMlH;
    command.enabled = rateMlH > 0.0f;
    command.fullReflux = !command.enabled;
    RectTakeoff::apply(command);
}

} // namespace

bool isFractionProgramEnabled(const Settings& settings) {
    const RectTakeoffBackendType backendType = getCurrentTakeoffBackendType(settings);
    if (!settings.fractionProgram.enabled || settings.fractionProgram.stepCount == 0 ||
        settings.fractionProgram.stepCount > FRACTION_PROGRAM_MAX_STEPS) return false;
    for (uint8_t index = 0; index < settings.fractionProgram.stepCount; ++index) {
        const FractionProgramStep& step = settings.fractionProgram.steps[index];
        if (step.pumpRateMlH <= 0.0f || step.routeIndex >= 5) return false;
        if (!RectTakeoff::isFractionRouteSupported(backendType, step.routeIndex, nullptr)) return false;
        if (step.endConditions == FRACTION_PROGRAM_END_NONE && !step.allowManualAdvance) return false;
    }
    return true;
}

static void stopFractionTakeoff() {
    RectTakeoff::stop();
}
static void notifyFractionEvent(const char* message, const char* level) {
    Buzzer::beep(2, 120);
    MQTT::publishNotification("Fraction program", message, level);
}

static bool isBodyLevelReached(const Settings& settings) {
    if (!settings.equipment.bodyLevelSensorEnabled) return false;
    int16_t adc = 0;
    float voltage = 0.0f;
    if (!Sensors::readAds1115Channel(ADS_CHANNEL_LEVEL_BODY, adc, voltage)) return false;
    return settings.equipment.bodyLevelTriggerAbove
        ? voltage >= settings.equipment.bodyLevelThresholdV
        : voltage <= settings.equipment.bodyLevelThresholdV;
}

static float getTemperatureByIndex(const TemperatureData& temperatures, uint8_t index) {
    switch (index) {
        case TEMP_CUBE: return temperatures.cube;
        case TEMP_COLUMN_BOTTOM: return temperatures.columnBottom;
        case TEMP_COLUMN_TOP: return temperatures.columnTop;
        case TEMP_REFLUX: return temperatures.reflux;
        case TEMP_TSA: return temperatures.tsa;
        case TEMP_WATER_IN: return temperatures.waterIn;
        case TEMP_WATER_OUT: return temperatures.waterOut;
        default: return 0.0f;
    }
}
static FractionProgramEndReason getFractionEndReason(const SystemState& state, const Settings& settings,
 const FractionProgramStep& step, uint32_t now) {
 const float collectedMl = getCurrentFractionCollectedMl(state, settings);
 const bool byVolume = (step.endConditions & FRACTION_PROGRAM_END_VOLUME) != 0 &&
 step.endVolumeMl > 0.0f && collectedMl >= step.endVolumeMl;
 const bool byTime = (step.endConditions & FRACTION_PROGRAM_END_TIME) != 0 &&
 step.endDurationSec > 0 &&
 now - state.fractionProgram.stepStartedAtMs >= step.endDurationSec * 1000UL;
 const bool byTemperature = (step.endConditions & FRACTION_PROGRAM_END_TEMPERATURE) != 0 &&
 step.temperatureSensorIndex < TEMP_COUNT &&
 step.endTemperatureC > 0.0f &&
 state.temps.valid[step.temperatureSensorIndex] &&
 getTemperatureByIndex(state.temps, step.temperatureSensorIndex) >= step.endTemperatureC;
 const bool byLevel = (step.endConditions & FRACTION_PROGRAM_END_LEVEL) != 0 &&
 isBodyLevelReached(settings);
 return static_cast<FractionProgramEndReason>(
 FractionProgramLogic::selectEndReason(false, byVolume, byTime, byTemperature, byLevel));
}

static void prepareCurrentFractionStep(SystemState& state, uint32_t now) {
    state.fractionProgram.stepStartedAtMs = now;
    state.fractionProgram.routingStartedAtMs = 0;
    state.fractionProgram.routing = false;
    state.fractionProgram.waitingForConfirmation = false;
    state.fractionProgram.manualAdvanceRequested = false;
    state.fractionProgram.confirmationPrompt[0] = '\0';
}

static void recordCompletedFractionVolume(SystemState& state, uint8_t routeIndex,
                                          float collectedMl) {
    if (collectedMl <= 0.0f) return;

    switch (routeIndex) {
        case static_cast<uint8_t>(Fraction::HEADS):
        case static_cast<uint8_t>(Fraction::SUBHEADS):
            state.stats.headsVolume += collectedMl;
            break;
        case static_cast<uint8_t>(Fraction::BODY):
            state.stats.bodyVolume += collectedMl;
            break;
        case static_cast<uint8_t>(Fraction::PRETAILS):
        case static_cast<uint8_t>(Fraction::TAILS):
            state.stats.tailsVolume += collectedMl;
            break;
        default:
            break;
    }
}

static bool beginFractionRouting(SystemState& state, const Settings& settings, uint32_t now) {
    const FractionProgramStep& step = settings.fractionProgram.steps[state.fractionProgram.currentStep];
    String routingDetail;
    if (!RectTakeoff::requestFractionRoute(
            getCurrentTakeoffBackendType(settings), step.routeIndex, &routingDetail)) {
        LOG_W("Distillation fraction routing rejected: %s", routingDetail.c_str());
        return false;
    }
    state.fractionProgram.routing = true;
    state.fractionProgram.routingStartedAtMs = now;
    return true;
}

bool confirmFractionProgram(SystemState& state, const Settings& settings) {
    if (state.mode != Mode::DISTILLATION || !state.fractionProgram.active ||
        !state.fractionProgram.waitingForConfirmation) return false;
    state.fractionProgram.waitingForConfirmation = false;
    state.fractionProgram.confirmationPrompt[0] = '\0';
    if (beginFractionRouting(state, settings, millis())) return true;
    stopFractionTakeoff();
    state.fractionProgram.active = false;
    state.rectPhase = RectPhase::FINISH;
    notifyFractionEvent("Fractionator unavailable; collection stopped", "error");
    return false;
}

bool advanceFractionProgram(SystemState& state, const Settings& settings) {
    if (state.mode != Mode::DISTILLATION || !state.fractionProgram.active ||
        state.fractionProgram.waitingForConfirmation ||
        state.fractionProgram.currentStep >= settings.fractionProgram.stepCount) return false;
    const FractionProgramStep& step = settings.fractionProgram.steps[state.fractionProgram.currentStep];
    if (!step.allowManualAdvance) return false;
    state.fractionProgram.manualAdvanceRequested = true;
    return true;
}
static void updateFractionProgram(SystemState& state, const Settings& settings, uint32_t now) {
    if (!state.fractionProgram.active) {
        state.fractionProgram.active = true;
        state.fractionProgram.currentStep = 0;
        state.fractionProgram.lastEndReason = FRACTION_PROGRAM_REASON_NONE;
        prepareCurrentFractionStep(state, now);
        state.fractionProgram.stepStartVolumeMl = getCurrentTakeoffTotalVolumeMl(state, settings);
    }

    const FractionProgramStep& step = settings.fractionProgram.steps[state.fractionProgram.currentStep];
    Heater::setPowerWatts(step.heaterPowerW > 0 ? step.heaterPowerW : g_params.powerWatts);
    if (state.fractionProgram.waitingForConfirmation) { stopFractionTakeoff(); return; }

    if (!state.fractionProgram.routing && state.fractionProgram.routingStartedAtMs == 0) {
        if (step.requireOperatorConfirmation) {
            snprintf(state.fractionProgram.confirmationPrompt, sizeof(state.fractionProgram.confirmationPrompt),
                     "%s", step.confirmationPrompt[0] ? step.confirmationPrompt : "Install the collection container and confirm");
            state.fractionProgram.waitingForConfirmation = true;
            stopFractionTakeoff();
            notifyFractionEvent(state.fractionProgram.confirmationPrompt, "warning");
            return;
        }
        if (!beginFractionRouting(state, settings, now)) {
            stopFractionTakeoff();
            state.fractionProgram.active = false;
            state.rectPhase = RectPhase::FINISH;
            notifyFractionEvent("Fractionator unavailable; collection stopped", "error");
        }
        return;
    }

    if (state.fractionProgram.routing) {
        stopFractionTakeoff();
        const bool routeReady = RectTakeoff::isFractionRouteReady(
            getCurrentTakeoffBackendType(settings), step.routeIndex);
        const uint32_t routingElapsedMs = now - state.fractionProgram.routingStartedAtMs;
        if (FractionProgramLogic::hasRouteTimedOut(routeReady, routingElapsedMs)) {
            stopFractionTakeoff();
            state.fractionProgram.active = false;
            state.fractionProgram.routing = false;
            setPhaseStartTime(now);
            state.rectPhase = RectPhase::FINISH;
            notifyFractionEvent("Fractionator route timeout; collection stopped", "error");
            return;
        }
        if (!FractionProgramLogic::isRouteSettled(
                routeReady, routingElapsedMs, settings.rectParams.routingSettlingMs)) return;
        state.fractionProgram.routing = false;
    }

    applyFractionTakeoff(settings, routeIndexToTakeoffFraction(step.routeIndex), step.pumpRateMlH);
    const FractionProgramEndReason endReason = state.fractionProgram.manualAdvanceRequested
        ? FRACTION_PROGRAM_REASON_MANUAL
        : getFractionEndReason(state, settings, step, now);
    if (endReason == FRACTION_PROGRAM_REASON_NONE) return;
    stopFractionTakeoff();
    recordCompletedFractionVolume(state, step.routeIndex,
                                  getCurrentFractionCollectedMl(state, settings));
    state.fractionProgram.lastEndReason = endReason;
    state.fractionProgram.manualAdvanceRequested = false;
    notifyFractionEvent("Fraction completed", "info");
    if (++state.fractionProgram.currentStep >= settings.fractionProgram.stepCount) {
        state.fractionProgram.active = false;
        setPhaseStartTime(now);
        state.rectPhase = RectPhase::FINISH;
        return;
    }
    prepareCurrentFractionStep(state, now);
    state.fractionProgram.stepStartVolumeMl = getCurrentTakeoffTotalVolumeMl(state, settings);
}

void initSession(SystemState& state, const Settings& settings) {
    RectTakeoff::beginSession(settings);
    state.fractionProgram = FractionProgramRuntime{};
}

void setParams(float speedMlH, float headsVolumeMl, float targetVolumeMl, float endTempC) {
    if (speedMlH > 0) g_params.speedMlH = speedMlH;
    if (headsVolumeMl >= 0) g_params.headsVolumeMl = headsVolumeMl;
    if (targetVolumeMl >= 0) g_params.targetVolumeMl = targetVolumeMl;
    if (endTempC > 0) g_params.endTempC = endTempC;

    LOG_I("Distillation params: speed=%.0f ml/h, heads=%.0f ml, target=%.0f ml, endTemp=%.1fC, power=%uW",
          g_params.speedMlH, g_params.headsVolumeMl, g_params.targetVolumeMl, g_params.endTempC, g_params.powerWatts);
}

void setPowerWatts(uint16_t powerWatts) {
    const uint16_t heaterMaxW = getConfiguredHeaterPowerWatts(g_settings);
    if (powerWatts > heaterMaxW) powerWatts = heaterMaxW;
    g_params.powerWatts = powerWatts;
}

void setPowerPercent(uint8_t powerPercent) {
    if (powerPercent > 100) powerPercent = 100;
    const uint16_t heaterMaxW = getConfiguredHeaterPowerWatts(g_settings);
    const uint16_t powerWatts = static_cast<uint16_t>(
        (static_cast<uint32_t>(heaterMaxW) * powerPercent) / 100U);
    setPowerWatts(powerWatts);
}

void getParams(float& speedMlH, float& headsVolumeMl, float& targetVolumeMl, float& endTempC, uint16_t& powerWatts) {
    speedMlH = g_params.speedMlH;
    headsVolumeMl = g_params.headsVolumeMl;
    targetVolumeMl = g_params.targetVolumeMl;
    endTempC = g_params.endTempC;
    powerWatts = g_params.powerWatts;
}

void update(SystemState& state, const Settings& settings) {
    uint32_t now = millis();
    uint32_t startTime = getPhaseStartTime();

    if (state.temps.cube >= getWaterAutoStartTempC(settings)) {
        Valves::setWater(true);
    }

    switch (state.rectPhase) {
        case RectPhase::HEATING: {
            applyBoosterHeater(state, settings, true);
            applyFullHeatPower(settings);
            const bool programHeating = isFractionProgramEnabled(settings);
            const uint8_t heatingSensor = programHeating
                ? settings.fractionProgram.heatingTemperatureSensorIndex
                : TEMP_CUBE;
            const float heatingTarget = programHeating
                ? settings.fractionProgram.heatingTargetTemperatureC
                : 78.0f;
            if (heatingSensor < TEMP_COUNT && heatingTarget > 0.0f &&
                state.temps.valid[heatingSensor] &&
                getTemperatureByIndex(state.temps, heatingSensor) >= heatingTarget) {
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(getCurrentTakeoffTotalVolumeMl(state, settings));
                if (programHeating) {
                    state.rectPhase = RectPhase::BODY;
                } else if (g_params.headsVolumeMl > 0.0f) {
                    ControlV2::notePhaseTransition(Mode::DISTILLATION,
                                                   static_cast<uint16_t>(RectPhase::HEATING),
                                                   static_cast<uint16_t>(RectPhase::HEADS),
                                                   ControlV2::ReasonCodeV2::RC_HEATING_COMPLETE,
                                                   "Distillation heating complete, starting heads");
                    state.rectPhase = RectPhase::HEADS;
                    LOG_I("Distillation: HEATING -> HEADS");
                } else {
                    ControlV2::notePhaseTransition(
                        Mode::DISTILLATION,
                        static_cast<uint16_t>(RectPhase::HEATING),
                        static_cast<uint16_t>(RectPhase::BODY),
                        ControlV2::ReasonCodeV2::RC_DISTILLATION_HEADS_OPTIONAL_SKIPPED,
                        "Distillation heads skipped, starting body collection");
                    state.rectPhase = RectPhase::BODY;
                    LOG_I("Distillation: HEATING -> BODY");
                }
            }
            break;
        }
        case RectPhase::HEADS: {
            applyBoosterHeater(state, settings, false);
            Heater::setPowerWatts(g_params.powerWatts);
            applyFractionTakeoff(settings, RectTakeoffFraction::HEADS, g_params.speedMlH);
            const float collected = getCurrentTakeoffTotalVolumeMl(state, settings) - getPhaseStartVolumeMl();
            state.stats.headsVolume = collected;
            if (collected >= g_params.headsVolumeMl) {
                stopFractionTakeoff();
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(getCurrentTakeoffTotalVolumeMl(state, settings));
                ControlV2::notePhaseTransition(Mode::DISTILLATION,
                                               static_cast<uint16_t>(RectPhase::HEADS),
                                               static_cast<uint16_t>(RectPhase::BODY),
                                               ControlV2::ReasonCodeV2::RC_HEADS_VOLUME_REACHED,
                                               "Distillation heads volume reached");
                state.rectPhase = RectPhase::BODY;
                LOG_I("Distillation: HEADS -> BODY");
            }
            break;
        }

        case RectPhase::BODY: {
            applyBoosterHeater(state, settings, false);
            const bool fractionProgramEnabled = isFractionProgramEnabled(settings);
            if (fractionProgramEnabled) {
                updateFractionProgram(state, settings, now);
            } else {
                state.fractionProgram = FractionProgramRuntime{};
                Heater::setPowerWatts(g_params.powerWatts);
                applyFractionTakeoff(settings, RectTakeoffFraction::BODY, g_params.speedMlH);
            }
            if (fractionProgramEnabled) break;

            const float currentTakeoffVolumeMl = getCurrentTakeoffTotalVolumeMl(state, settings);
            const float collectedBody = currentTakeoffVolumeMl - getPhaseStartVolumeMl();
            state.stats.bodyVolume = collectedBody;
            const bool endByTemp = (g_params.endTempC > 0.0f && state.temps.valid[TEMP_CUBE] && state.temps.cube >= g_params.endTempC);
            const bool endByVolume = (g_params.targetVolumeMl > 0.0f && currentTakeoffVolumeMl >= g_params.targetVolumeMl);
            if (endByTemp || endByVolume) {
                const ControlV2::ReasonCodeV2 finishReason =
                    getBodyExitReason(endByTemp, endByVolume);
                setPhaseStartTime(now);
                stopFractionTakeoff();
                ControlV2::notePhaseTransition(
                    Mode::DISTILLATION,
                    static_cast<uint16_t>(RectPhase::BODY),
                    static_cast<uint16_t>(RectPhase::FINISH),
                    finishReason,
                    getBodyExitMessage(endByTemp, endByVolume));
                state.rectPhase = RectPhase::FINISH;
                LOG_I("Distillation: BODY -> FINISH (%s%s)", endByTemp ? "temp" : "", endByVolume ? " volume" : "");
            }
            break;
        }

        case RectPhase::FINISH:
            applyBoosterHeater(state, settings, false);
            Heater::setPower(0);
            stopFractionTakeoff();
            Valves::setWater(true);
            if (now - startTime > 5 * 60 * 1000UL) {
                Valves::closeAll();
                ControlV2::notePhaseTransition(Mode::DISTILLATION,
                                               static_cast<uint16_t>(RectPhase::FINISH),
                                               static_cast<uint16_t>(RectPhase::IDLE),
                                               ControlV2::ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE,
                                               "Distillation cooldown complete");
                state.rectPhase = RectPhase::IDLE;
                state.mode = Mode::IDLE;
                LOG_I("Distillation: Process complete!");
            }
            break;
        default:
            ControlV2::notePhaseTransition(
                Mode::DISTILLATION,
                static_cast<uint16_t>(state.rectPhase),
                static_cast<uint16_t>(RectPhase::BODY),
                ControlV2::ReasonCodeV2::RC_PHASE_RECOVERY_APPLIED,
                "Recovered distillation phase to body");
            state.rectPhase = RectPhase::BODY;
            setPhaseStartTime(now);
            setPhaseStartVolumeMl(getCurrentTakeoffTotalVolumeMl(state, settings));
            break;
    }
}

} // namespace Distillation
} // namespace FSM
