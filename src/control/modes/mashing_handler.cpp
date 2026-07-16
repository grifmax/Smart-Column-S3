#include "../fsm_utils.h"
#include "../v2/reason_codes.h"
#include "../v2/status_adapter.h"
#include "../../drivers/heater.h"
#include "../../drivers/pump.h"
#include "../../drivers/valves.h"
#include "../../drivers/stirrer.h"
#include "../../drivers/sensors.h"
#include "../../interface/mqtt.h"
#include "../../storage/logger.h"
#include <Arduino.h>
#include <algorithm>

namespace FSM {
namespace Mashing {

static const MashProfile* currentProfile = nullptr;
static bool mashFoamAlarmActive = false;
static uint8_t boilHopNotificationMask = 0;

namespace {

MashPhase getPhaseForStep(uint8_t stepIndex) {
    switch (stepIndex) {
        case 0: return MashPhase::ACID_REST;
        case 1: return MashPhase::PROTEIN_REST;
        case 2: return MashPhase::BETA_AMYLASE;
        case 3: return MashPhase::ALPHA_AMYLASE;
        case 4: return MashPhase::MASH_OUT;
        default: return MashPhase::FINISH;
    }
}

const char* getStepAdvanceMessage(uint8_t completedStep, uint8_t totalSteps) {
    static char message[96];
    snprintf(message, sizeof(message), "Mashing step %u of %u completed",
             completedStep + 1, totalSteps);
    return message;
}

} // namespace

void setProfile(const MashProfile* profile) {
    currentProfile = profile;
    LOG_I("Mashing: Profile set to %s", profile ? profile->name : "NULL");
}

void nextStep(SystemState& state,
              ControlV2::ReasonCodeV2 reason = ControlV2::ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE,
              const char* message = nullptr) {
    if (!currentProfile) return;
    const uint8_t completedStep = state.mashing.currentStep;
    const MashPhase previousPhase = state.mashing.phase;
    state.mashing.currentStep++;
    if (state.mashing.currentStep < currentProfile->stepCount) {
        state.mashing.targetTemp = currentProfile->steps[state.mashing.currentStep].temperature;
        state.mashing.stepType = currentProfile->steps[state.mashing.currentStep].type;
        boilHopNotificationMask = 0;
        state.mashing.waitingForOperator = false;
        state.mashing.stepDuration = currentProfile->steps[state.mashing.currentStep].duration * 60;
        state.mashing.stepStartTime = millis();
        state.mashing.tempInRange = false;
        state.mashing.inRangeStartTime = 0;
        state.mashing.stepCount = currentProfile->stepCount;
        strncpy(state.mashing.stepName, currentProfile->steps[state.mashing.currentStep].name, sizeof(state.mashing.stepName) - 1);
        state.mashing.stepName[sizeof(state.mashing.stepName) - 1] = '\0';
        state.mashing.phase = getPhaseForStep(state.mashing.currentStep);
        ControlV2::notePhaseTransition(
            Mode::MASHING, static_cast<uint16_t>(previousPhase),
            static_cast<uint16_t>(state.mashing.phase),
            reason, message != nullptr
                ? message
                : getStepAdvanceMessage(completedStep, currentProfile->stepCount));
        
        LOG_I("Mashing: Step %d/%d - Target %.1f°C, Duration %d min",
              state.mashing.currentStep + 1, currentProfile->stepCount,
              state.mashing.targetTemp, currentProfile->steps[state.mashing.currentStep].duration);
        
        char msg[128];
        snprintf(msg, sizeof(msg), "Пауза %d/%d: %.1f°C (%d мин)",
                 state.mashing.currentStep + 1, currentProfile->stepCount,
                 state.mashing.targetTemp, currentProfile->steps[state.mashing.currentStep].duration);
        MQTT::publishNotification("Затирка: Новая пауза", msg, "info");
    }
}

bool isMashFoamLevelReached(const Settings& settings) {
    if (!settings.equipment.bodyLevelSensorEnabled) return false;
    int16_t adc = 0;
    float voltage = 0.0f;
    if (!Sensors::readAds1115Channel(ADS_CHANNEL_LEVEL_BODY, adc, voltage)) return false;
    return settings.equipment.bodyLevelTriggerAbove
        ? voltage >= settings.equipment.bodyLevelThresholdV
        : voltage <= settings.equipment.bodyLevelThresholdV;
}

bool requestManualAdvance(SystemState& state) {
    if (!state.mashing.active || !state.paused ||
        state.mashing.stepType != MashStepType::OPERATOR_WAIT) {
        return false;
    }
    state.mashing.manualAdvanceRequested = true;
    return true;
}

void start(SystemState& state, const MashProfile* profile) {
    if (!profile || profile->stepCount == 0) return;
    Pump::stop();
    Valves::closeAll();
    setProfile(profile);
    state.mashing.active = true;
    state.mashing.stepCount = profile->stepCount;
    state.mashing.currentStep = 0;
    state.mashing.targetTemp = profile->steps[0].temperature;
    state.mashing.stepType = profile->steps[0].type;
    state.mashing.waitingForOperator = false;
    state.mashing.manualAdvanceRequested = false;
    mashFoamAlarmActive = false;
    boilHopNotificationMask = 0;
    state.mashing.stepDuration = profile->steps[0].duration * 60;
    state.mashing.stepStartTime = millis();
    state.mashing.tempInRange = false;
    state.mashing.inRangeStartTime = 0;
    state.mashing.phase = MashPhase::ACID_REST;
    strncpy(state.mashing.stepName, profile->steps[0].name, sizeof(state.mashing.stepName) - 1);
    state.mashing.stepName[sizeof(state.mashing.stepName) - 1] = '\0';
    state.mode = Mode::MASHING;
    state.rectPhase = RectPhase::IDLE;
    ControlV2::notePhaseTransition(
        Mode::MASHING, static_cast<uint16_t>(MashPhase::IDLE),
        static_cast<uint16_t>(MashPhase::ACID_REST),
        ControlV2::ReasonCodeV2::RC_MODE_START_REQUEST,
        "Mashing started");
    LOG_I("Mashing: Started profile '%s' (%d steps)", profile->name, profile->stepCount);
}

void update(SystemState& state, const Settings& settings) {
    if (!state.mashing.active || !currentProfile) return;
    uint32_t now = millis();
    float currentTemp = state.temps.cube;
    Pump::stop();
    Valves::setHeads(false);
    float targetTemp = state.mashing.targetTemp;
    const MashStepType stepType = state.mashing.stepType;

    const bool foamLevelReached = isMashFoamLevelReached(settings);
    if (foamLevelReached) {
        Heater::setPower(0);
        Valves::setWater(true);
        if (settings.stirrer.enabled && currentTemp <= MASH_STIRRER_SAFE_TEMP_C) {
            Stirrer::start(settings.stirrer.defaultSpeedPercent);
        } else {
            Stirrer::stop();
        }
        if (!mashFoamAlarmActive) {
            mashFoamAlarmActive = true;
            MQTT::publishNotification(
                "Затор: верхний уровень / пена",
                "Нагрев отключён. Охлаждение включено; мешалка разрешена только ниже безопасной температуры.",
                "warning");
        }
        return;
    }
    mashFoamAlarmActive = false;

    if (stepType != MashStepType::STIR && stepType != MashStepType::COOL) {
        Stirrer::stop();
    }
    if (stepType == MashStepType::STIR) {
        Heater::setPower(0);
        if (settings.stirrer.enabled && currentTemp <= MASH_STIRRER_SAFE_TEMP_C) {
            Stirrer::start(settings.stirrer.defaultSpeedPercent);
        } else {
            Stirrer::stop();
        }
    }

    if (state.mashing.manualAdvanceRequested) {
        state.mashing.manualAdvanceRequested = false;
        state.mashing.waitingForOperator = false;
        nextStep(state, ControlV2::ReasonCodeV2::RC_MANUAL_OPERATOR_SWITCH,
                 "Оператор подтвердил переход к следующему шагу затора");
        return;
    }

    if (stepType == MashStepType::FINISH) {
        state.mashing.currentStep = currentProfile->stepCount;
    } else if (stepType == MashStepType::OPERATOR_WAIT) {
        Heater::setPower(0);
        Valves::setWater(false);
        if (!state.mashing.waitingForOperator) {
            state.mashing.waitingForOperator = true;
            state.paused = true;
            MQTT::publishNotification("Затор: действие оператора",
                                      state.mashing.stepName, "warning");
        } else if (!state.paused) {
            nextStep(state);
        }
        return;
    } else if (stepType == MashStepType::COOL) {
        Heater::setPower(0);
        const uint32_t coolingCycleSec =
            MASH_COOLING_ON_SEC_DEFAULT + MASH_COOLING_OFF_SEC_DEFAULT;
        const bool coolingPulseOn = coolingCycleSec == 0 ||
            ((now - state.mashing.stepStartTime) / 1000UL) % coolingCycleSec <
                MASH_COOLING_ON_SEC_DEFAULT;
        Valves::setWater(coolingPulseOn);
        if (settings.stirrer.enabled && coolingPulseOn) {
            Stirrer::start(settings.stirrer.defaultSpeedPercent);
        } else if (!coolingPulseOn) {
            Stirrer::stop();
        }
        if (currentTemp <= targetTemp) {
            Valves::setWater(false);
            Stirrer::stop();
            if (state.mashing.currentStep + 1 >= currentProfile->stepCount) {
                MQTT::publishNotification(
                    "Затор охлаждён",
                    "Температура достигнута. Можно вручную перейти к ферментации; дрожжи автоматически не дозируются.",
                    "info");
            }
            nextStep(state);
        }
        return;
    } else if (stepType == MashStepType::STIR) {
        if (state.mashing.stepStartTime > 0 &&
            now - state.mashing.stepStartTime >= state.mashing.stepDuration * 1000UL) {
            nextStep(state);
        }
        return;
    }
    if (stepType == MashStepType::BOIL) {
        Heater::setPower(MASH_BOIL_POWER_PERCENT_DEFAULT);
        const bool vaporValid = state.temps.valid[TEMP_TSA];
        const bool vaporBoiling = vaporValid && state.temps.tsa >= targetTemp;
        const bool fallbackBoiling = currentTemp >= targetTemp;
        const bool boilingReached = vaporBoiling || fallbackBoiling;
        const uint32_t holdMs = state.mashing.stepDuration * 1000UL;
        const uint32_t timeoutMs = holdMs * 3UL;
        if (!boilingReached) {
            if (holdMs > 0 && now - state.mashing.stepStartTime >= timeoutMs) {
                Heater::setPower(0);
                state.paused = true;
                MQTT::publishNotification(
                    "Затор: кипение не подтверждено",
                    vaporValid
                        ? "Не достигнут порог кипения по датчику пара в заданный таймаут."
                        : "Датчик пара недоступен, а резервный порог куба не достигнут в заданный таймаут.",
                    "warning");
            }
            return;
        }
        if (!state.mashing.tempInRange) {
            state.mashing.tempInRange = true;
            state.mashing.inRangeStartTime = now;
        }
        const uint32_t boilElapsedMs = now - state.mashing.inRangeStartTime;
        const uint32_t hopMomentsMs[] = {
            0,
            holdMs / 2UL,
            holdMs > 300000UL ? holdMs - 300000UL : holdMs
        };
        for (uint8_t index = 0; index < 3; ++index) {
            const uint8_t flag = static_cast<uint8_t>(1U << index);
            if ((boilHopNotificationMask & flag) == 0 &&
                boilElapsedMs >= hopMomentsMs[index]) {
                boilHopNotificationMask |= flag;
                char message[96];
                snprintf(message, sizeof(message),
                         "Кипение идёт %lu мин. Внесение хмеля: этап %u из 3.",
                         static_cast<unsigned long>(boilElapsedMs / 60000UL),
                         index + 1);
                MQTT::publishNotification("Затор: внесение хмеля", message,
                                          "info");
            }
        }
        if (boilElapsedMs >= holdMs) {
            nextStep(state);
        }
        return;
    }
    float error = targetTemp - currentTemp;
    float Kp = 2.0f;
    int powerDelta = (int)(error * Kp);
    int currentPower = (int)Heater::getPower();
    int newPower = (int)(currentPower + powerDelta);
    if (newPower > 100) newPower = 100;
    if (newPower < 0) newPower = 0;
    Heater::setPower((uint8_t)newPower);
    
    if (state.mashing.currentStep < currentProfile->stepCount) {
        const bool tempReached = fabs(currentTemp - targetTemp) < 1.0f;
        if (tempReached) {
            if (stepType == MashStepType::HEAT) {
                nextStep(state);
                return;
            }
            if (!state.mashing.tempInRange) {
                state.mashing.tempInRange = true;
                state.mashing.inRangeStartTime = now;
            }
        } else {
            state.mashing.tempInRange = false;
            state.mashing.inRangeStartTime = 0;
        }
        const bool timeElapsed = state.mashing.tempInRange && state.mashing.inRangeStartTime > 0 &&
            (now - state.mashing.inRangeStartTime) >= (state.mashing.stepDuration * 1000UL);
        if (timeElapsed) nextStep(state);
    } else {
        const MashPhase previousPhase = state.mashing.phase;
        state.mashing.phase = MashPhase::FINISH;
        ControlV2::notePhaseTransition(
            Mode::MASHING, static_cast<uint16_t>(previousPhase),
            static_cast<uint16_t>(MashPhase::FINISH),
            ControlV2::ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE,
            "Mashing program completed");
        state.mashing.active = false;
        state.mode = Mode::IDLE;
        Heater::setPower(0);
        LOG_I("Mashing: Process complete!");
        MQTT::publishNotification("Затирка завершена", "Все температурные паузы завершены", "success");
    }
}

} // namespace Mashing
} // namespace FSM
