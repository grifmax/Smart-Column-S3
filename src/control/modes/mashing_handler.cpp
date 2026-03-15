#include "../fsm_utils.h"
#include "../v2/reason_codes.h"
#include "../v2/status_adapter.h"
#include "../../drivers/heater.h"
#include "../../drivers/pump.h"
#include "../../drivers/valves.h"
#include "../../interface/mqtt.h"
#include "../../storage/logger.h"
#include <Arduino.h>
#include <algorithm>

namespace FSM {
namespace Mashing {

static const MashProfile* currentProfile = nullptr;

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

void nextStep(SystemState& state) {
    if (!currentProfile) return;
    const uint8_t completedStep = state.mashing.currentStep;
    const MashPhase previousPhase = state.mashing.phase;
    state.mashing.currentStep++;
    if (state.mashing.currentStep < currentProfile->stepCount) {
        state.mashing.targetTemp = currentProfile->steps[state.mashing.currentStep].temperature;
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
            ControlV2::ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE,
            getStepAdvanceMessage(completedStep, currentProfile->stepCount));
        
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

void start(SystemState& state, const MashProfile* profile) {
    if (!profile || profile->stepCount == 0) return;
    Pump::stop();
    Valves::closeAll();
    setProfile(profile);
    state.mashing.active = true;
    state.mashing.stepCount = profile->stepCount;
    state.mashing.currentStep = 0;
    state.mashing.targetTemp = profile->steps[0].temperature;
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
