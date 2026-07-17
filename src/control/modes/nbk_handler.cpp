#include "../fsm_utils.h"
#include "../v2/reason_codes.h"
#include "../v2/safety_policy.h"
#include "../v2/safety_supervisor.h"
#include "../v2/status_adapter.h"
#include "../../drivers/heater.h"
#include "../../drivers/pump.h"
#include "../../drivers/valves.h"
#include "../../interface/mqtt.h"
#include "../../storage/logger.h"
#include <Arduino.h>

namespace FSM {
namespace Nbk {

namespace {
float topTempFeedCorrection = 1.0f;
uint32_t lastTopTempCorrectionMs = 0;
uint32_t feedStableSinceMs = 0;
bool feedPausedByProtection = false;

void setFeedRuntime(SystemState& state, float requested, float corrected,
                    const char* limitingFactor) {
    state.nbk.requestedFeedMlH = requested;
    state.nbk.correctedFeedMlH = corrected;
    state.nbk.feedCorrectionPercent = requested > 0.0f
        ? ((corrected / requested) - 1.0f) * 100.0f
        : 0.0f;
    strncpy(state.nbk.limitingFactor, limitingFactor,
            sizeof(state.nbk.limitingFactor) - 1);
    state.nbk.limitingFactor[sizeof(state.nbk.limitingFactor) - 1] = '\0';
}

float getCorrectedFeedRate(float baseRate, const SystemState& state,
                           const Settings& settings, uint32_t now) {
    if (!settings.nbk.topTempCorrectionEnabled ||
        !state.temps.valid[TEMP_COLUMN_TOP]) {
        topTempFeedCorrection = 1.0f;
        return baseRate;
    }
    // The correction moves only once every 30 seconds and by no more than
    // 5% per pass. A hotter top reduces feed, a colder top raises it.
    if (now - lastTopTempCorrectionMs >= 30000UL) {
        const float error = state.temps.columnTop - settings.nbk.columnTopTargetTempC;
        const float step = constrain(-error * 0.02f, -0.05f, 0.05f);
        topTempFeedCorrection = constrain(topTempFeedCorrection + step, 0.85f, 1.15f);
        lastTopTempCorrectionMs = now;
    }
    return baseRate * topTempFeedCorrection;
}

bool feedRecoveryReady(const SystemState& state, const Settings& settings,
                       const ControlV2::ActiveLimitsV2& limits, uint32_t now) {
    const bool conditionsStable = !limits.pumpCapped &&
        !limits.antiOscillationActive && state.temps.valid[TEMP_COLUMN_BOTTOM] &&
        state.pressure.ok &&
        state.temps.columnBottom > settings.nbk.columnBottomTempThresholdC + 0.5f &&
        state.pressure.cube < settings.safety.pressureMaxMmHg * 0.85f;
    if (!conditionsStable) {
        feedStableSinceMs = 0;
        if (!feedPausedByProtection) {
            feedPausedByProtection = true;
            MQTT::publishNotification(
                "НБК: подача остановлена защитой",
                "Подача возобновится только после 30 секунд устойчивых температуры и давления.",
                "warning");
        }
        return false;
    }
    if (feedStableSinceMs == 0) {
        feedStableSinceMs = now;
        return false;
    }
    if (now - feedStableSinceMs < 30000UL) return false;
    if (feedPausedByProtection) {
        feedPausedByProtection = false;
        MQTT::publishNotification("НБК: подача разрешена",
                                  "Условия устойчивы 30 секунд, возобновляем подачу браги.",
                                  "info");
    }
    return true;
}
}

void update(SystemState& state, const Settings& settings) {
    uint32_t now = millis();
    uint32_t startTime = getPhaseStartTime();
    uint32_t elapsed = now - startTime;
    const ControlV2::ActiveLimitsV2& liveLimits =
        ControlV2::SafetySupervisorV2::getLiveLimits();

    switch (state.nbkPhase) {
        case NbkPhase::HEATING:
            topTempFeedCorrection = 1.0f;
            lastTopTempCorrectionMs = now;
            feedStableSinceMs = 0;
            feedPausedByProtection = false;
            setFeedRuntime(state, settings.nbk.pumpSpeedMlH, 0.0f, "heating");
            applyBoosterHeater(state, settings, true);
            applyFullHeatPower(settings);
            if (state.temps.cube >= getWaterAutoStartTempC(settings)) {
                Valves::setWater(true);
            }
            if (state.temps.valid[TEMP_CUBE] && state.temps.cube > 98.0f) {
                LOG_I("NBK: HEATING -> STABILIZATION");
                ControlV2::notePhaseTransition(Mode::NBK,
                                               static_cast<uint16_t>(NbkPhase::HEATING),
                                               static_cast<uint16_t>(NbkPhase::STABILIZATION),
                                               ControlV2::ReasonCodeV2::RC_NBK_STEAM_READY);
                state.nbkPhase = NbkPhase::STABILIZATION;
                setPhaseStartTime(now);
                MQTT::publishNotification("НБК: Стабилизация", "Парогенератор разогрет, стабилизация колонны", "info");
            }
            break;
            
        case NbkPhase::STABILIZATION:
            applyBoosterHeater(state, settings, false);
            applyProcessHeaterPower(state, settings, 70);
            setFeedRuntime(state, settings.nbk.pumpSpeedMlH, 0.0f, "stabilization");
            if (!liveLimits.phaseAdvanceBlocked && elapsed > 5 * 60 * 1000UL) {
                LOG_I("NBK: STABILIZATION -> FEED_RAMP");
                ControlV2::notePhaseTransition(Mode::NBK,
                                               static_cast<uint16_t>(NbkPhase::STABILIZATION),
                                               static_cast<uint16_t>(NbkPhase::FEED_RAMP),
                                               ControlV2::ReasonCodeV2::RC_NBK_STABILIZATION_COMPLETE);
                state.nbkPhase = NbkPhase::FEED_RAMP;
                setPhaseStartTime(now);
                MQTT::publishNotification("НБК: Плавный запуск", "Начат плавный разгон подачи браги", "info");
            }
            break;

        case NbkPhase::FEED_RAMP: {
            applyBoosterHeater(state, settings, false);
            const float rampRatio = min(1.0f, elapsed / 60000.0f);
            const float feedRate = settings.nbk.pumpSpeedMlH * rampRatio;
            if (feedRecoveryReady(state, settings, liveLimits, now)) {
                Pump::start(feedRate);
                setFeedRuntime(state, settings.nbk.pumpSpeedMlH, feedRate, "feed ramp");
            } else {
                Pump::stop();
                setFeedRuntime(state, settings.nbk.pumpSpeedMlH, 0.0f, "safety recovery");
            }
            if (rampRatio >= 1.0f) {
                Pump::stop();
                ControlV2::notePhaseTransition(Mode::NBK,
                    static_cast<uint16_t>(NbkPhase::FEED_RAMP),
                    static_cast<uint16_t>(NbkPhase::WORKING),
                    ControlV2::ReasonCodeV2::RC_NBK_FEED_ENABLED,
                    "Подача браги вышла на рабочий расход");
                state.nbkPhase = NbkPhase::WORKING;
                setPhaseStartTime(now);
            }
            break;
        }
            
        case NbkPhase::WORKING: {
            applyBoosterHeater(state, settings, false);
            if (settings.nbk.targetVolumeMl > 0.0f &&
                state.pump.totalVolumeMl >= settings.nbk.targetVolumeMl) {
                Pump::stop();
                setFeedRuntime(state, settings.nbk.pumpSpeedMlH, 0.0f, "target volume");
                ControlV2::notePhaseTransition(
                    Mode::NBK, static_cast<uint16_t>(NbkPhase::WORKING),
                    static_cast<uint16_t>(NbkPhase::FINISH),
                    ControlV2::ReasonCodeV2::RC_NBK_FINISH_LIKELY,
                    "Достигнут заданный объём поданной браги");
                state.nbkPhase = NbkPhase::FINISH;
                setPhaseStartTime(now);
                MQTT::publishNotification("НБК: подача завершена",
                                          "Достигнут заданный объём браги, начинается охлаждение.",
                                          "info");
                break;
            }
            float targetSpeed = getCorrectedFeedRate(settings.nbk.pumpSpeedMlH,
                                                      state, settings, now);
            if (liveLimits.pumpCapped) {
                if (liveLimits.maxPumpSpeedMlH > 0.0f &&
                    targetSpeed > liveLimits.maxPumpSpeedMlH) {
                    targetSpeed = liveLimits.maxPumpSpeedMlH;
                } else if (liveLimits.maxPumpSpeedMlH <= 0.0f) {
                    targetSpeed = 0.0f;
                }
            }

            if (feedRecoveryReady(state, settings, liveLimits, now)) {
                Pump::start(targetSpeed);
                setFeedRuntime(state, settings.nbk.pumpSpeedMlH, targetSpeed,
                    settings.nbk.topTempCorrectionEnabled ? "top temperature correction" : "none");
            } else {
                Pump::stop();
                setFeedRuntime(state, settings.nbk.pumpSpeedMlH, 0.0f, "safety recovery");
            }

            // Защита по давлению (интеллектуальное снижение мощности)
            const uint8_t requestedPower =
                ControlV2::SafetyPolicyV2::getDefaultNbkHeaterPowerPercent(state, settings);
            const ControlV2::HeaterPowerPolicyV2 powerPolicy =
                ControlV2::SafetyPolicyV2::evaluateNbkHeaterPower(requestedPower, state, settings);
            if (powerPolicy.limited) {
                static uint32_t lastPressWarn = 0;
                if (now - lastPressWarn > 30000UL) {
                    MQTT::publishNotification("НБК: Высокое давление", "Мощность снижена для стабилизации давления", "warning");
                    lastPressWarn = now;
                }
            }
            uint8_t appliedPower = powerPolicy.appliedPowerPercent;
            if (liveLimits.powerCapped &&
                liveLimits.maxHeaterPowerPercent < appliedPower) {
                appliedPower = liveLimits.maxHeaterPowerPercent;
            }
            Heater::setPower(appliedPower);
            break;
        }
            
        case NbkPhase::FINISH:
            applyBoosterHeater(state, settings, false);
            Pump::stop();
            setFeedRuntime(state, settings.nbk.pumpSpeedMlH, 0.0f, "finish cooldown");
            Heater::setPower(0);
            if (elapsed > 5 * 60 * 1000UL) {
                Valves::setWater(false);
                ControlV2::notePhaseTransition(Mode::NBK,
                                               static_cast<uint16_t>(NbkPhase::FINISH),
                                               static_cast<uint16_t>(NbkPhase::COMPLETED),
                                               ControlV2::ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE);
                state.nbkPhase = NbkPhase::COMPLETED;
                state.mode = Mode::IDLE;
                LOG_I("NBK: Process complete");
                MQTT::publishNotification("НБК: Завершено", "Перегонка браги завершена", "success");
            }
            break;
        default: break;
    }
}

} // namespace Nbk
} // namespace FSM
