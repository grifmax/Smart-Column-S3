#include "../fsm_utils.h"
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


} // namespace

bool isFractionProgramEnabled(const Settings& settings) {
    if (!settings.fractionProgram.enabled || settings.fractionProgram.stepCount == 0 ||
        settings.fractionProgram.stepCount > FRACTION_PROGRAM_MAX_STEPS) return false;
    for (uint8_t index = 0; index < settings.fractionProgram.stepCount; ++index) {
        const FractionProgramStep& step = settings.fractionProgram.steps[index];
        if (step.pumpRateMlH <= 0.0f || step.routeIndex >= 5) return false;
        if (step.endConditions == FRACTION_PROGRAM_END_NONE && !step.allowManualAdvance) return false;
    }
    return true;
}

static void applyFractionPump(float rateMlH) {
    RectTakeoffCommand command{};
    command.backendType = RectTakeoffBackendType::PUMP;
    command.fraction = RectTakeoffFraction::BODY;
    command.equivalentRateMlH = rateMlH;
    command.enabled = rateMlH > 0.0f;
    command.fullReflux = !command.enabled;
    RectTakeoff::apply(command);
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
    const float collectedMl = state.pump.totalVolumeMl - state.fractionProgram.stepStartVolumeMl;
    if ((step.endConditions & FRACTION_PROGRAM_END_VOLUME) != 0 && step.endVolumeMl > 0.0f && collectedMl >= step.endVolumeMl) return FRACTION_PROGRAM_REASON_VOLUME;
    if ((step.endConditions & FRACTION_PROGRAM_END_TIME) != 0 && step.endDurationSec > 0 && now - state.fractionProgram.stepStartedAtMs >= step.endDurationSec * 1000UL) return FRACTION_PROGRAM_REASON_TIME;
    if ((step.endConditions & FRACTION_PROGRAM_END_TEMPERATURE) != 0 && step.temperatureSensorIndex < TEMP_COUNT && step.endTemperatureC > 0.0f && state.temps.valid[step.temperatureSensorIndex] && getTemperatureByIndex(state.temps, step.temperatureSensorIndex) >= step.endTemperatureC) return FRACTION_PROGRAM_REASON_TEMPERATURE;
    if ((step.endConditions & FRACTION_PROGRAM_END_LEVEL) != 0 && isBodyLevelReached(settings)) return FRACTION_PROGRAM_REASON_LEVEL;
    return FRACTION_PROGRAM_REASON_NONE;
}
static bool isCurrentFractionFinished(const SystemState& state, const Settings& settings,
                                      const FractionProgramStep& step, uint32_t now) {
    const float collectedMl = state.pump.totalVolumeMl - state.fractionProgram.stepStartVolumeMl;
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
    return byVolume || byTime || byTemperature || byLevel;
}

static void prepareCurrentFractionStep(SystemState& state, uint32_t now) {
    state.fractionProgram.stepStartedAtMs = now;
    state.fractionProgram.stepStartVolumeMl = state.pump.totalVolumeMl;
    state.fractionProgram.routingStartedAtMs = 0;
    state.fractionProgram.routing = false;
    state.fractionProgram.waitingForConfirmation = false;
    state.fractionProgram.confirmationPrompt[0] = '\0';
}

static bool beginFractionRouting(SystemState& state, const Settings& settings, uint32_t now) {
    const FractionProgramStep& step = settings.fractionProgram.steps[state.fractionProgram.currentStep];
    if (!settings.fractionator.enabled || !Valves::isFractionatorEnabled()) return false;
    Valves::setFraction(static_cast<Fraction>(step.routeIndex));
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
    Pump::stop();
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
        prepareCurrentFractionStep(state, now);
    }

    const FractionProgramStep& step = settings.fractionProgram.steps[state.fractionProgram.currentStep];
    Heater::setPowerWatts(step.heaterPowerW > 0 ? step.heaterPowerW : g_params.powerWatts);
    if (state.fractionProgram.waitingForConfirmation) { Pump::stop(); return; }

    if (!state.fractionProgram.routing && state.fractionProgram.routingStartedAtMs == 0) {
        if (step.requireOperatorConfirmation) {
            snprintf(state.fractionProgram.confirmationPrompt, sizeof(state.fractionProgram.confirmationPrompt),
                     "%s", step.confirmationPrompt[0] ? step.confirmationPrompt : "Install the collection container and confirm");
            state.fractionProgram.waitingForConfirmation = true;
            Pump::stop();
            notifyFractionEvent(state.fractionProgram.confirmationPrompt, "warning");
            return;
        }
        if (!beginFractionRouting(state, settings, now)) {
            Pump::stop();
            state.fractionProgram.active = false;
            state.rectPhase = RectPhase::FINISH;
            notifyFractionEvent("Fractionator unavailable; collection stopped", "error");
        }
        return;
    }

    if (state.fractionProgram.routing) {
        Pump::stop();
        if (!RectTakeoff::isFractionRouteReady(step.routeIndex) || now - state.fractionProgram.routingStartedAtMs < settings.rectParams.routingSettlingMs) return;
        state.fractionProgram.routing = false;
    }

    Pump::start(step.pumpRateMlH);
    if (!isCurrentFractionFinished(state, settings, step, now)) return;
    Pump::stop();
    notifyFractionEvent("Fraction completed", "info");
    if (++state.fractionProgram.currentStep >= settings.fractionProgram.stepCount) {
        state.fractionProgram.active = false;
        state.rectPhase = RectPhase::FINISH;
        return;
    }
    prepareCurrentFractionStep(state, now);
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
                setPhaseStartVolumeMl(state.pump.totalVolumeMl);
                if (g_params.headsVolumeMl > 0.0f) {
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
            Pump::start(g_params.speedMlH);
            Valves::setHeads(true);
            const float collected = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
            state.stats.headsVolume = collected;
            if (collected >= g_params.headsVolumeMl) {
                Valves::setHeads(false);
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(state.pump.totalVolumeMl);
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
            Heater::setPowerWatts(g_params.powerWatts);
            Valves::setHeads(false);
            Pump::start(g_params.speedMlH);
            const float collectedBody = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
            state.stats.bodyVolume = collectedBody;
            const bool endByTemp = (g_params.endTempC > 0.0f && state.temps.valid[TEMP_CUBE] && state.temps.cube >= g_params.endTempC);
            const bool endByVolume = (g_params.targetVolumeMl > 0.0f && state.pump.totalVolumeMl >= g_params.targetVolumeMl);
            if (endByTemp || endByVolume) {
                const ControlV2::ReasonCodeV2 finishReason =
                    getBodyExitReason(endByTemp, endByVolume);
                setPhaseStartTime(now);
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
            Pump::stop();
            Valves::setHeads(false);
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
            setPhaseStartVolumeMl(state.pump.totalVolumeMl);
            break;
    }
}

} // namespace Distillation
} // namespace FSM
