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
namespace Hold {

namespace {

const char* getStepCompleteMessage(uint8_t completedStep, uint8_t totalSteps) {
    static char message[96];
    snprintf(message, sizeof(message), "Hold step %u of %u completed",
             completedStep + 1, totalSteps);
    return message;
}

} // namespace

void update(SystemState& state, const Settings& settings) {
    if (!state.hold.active || state.hold.stepCount == 0) return;
    uint32_t now = millis();
    float currentTemp = state.temps.cube;
    Pump::stop();
    Valves::setHeads(false);
    
    if (state.hold.currentStep < state.hold.stepCount) {
        float targetTemp = state.hold.targetTemp;
        float error = targetTemp - currentTemp;
        float Kp = 2.0f;
        int powerDelta = (int)(error * Kp);
        int currentPower = (int)Heater::getPower();
        int newPower = (int)(currentPower + powerDelta);
        if (newPower > 100) newPower = 100;
        if (newPower < 0) newPower = 0;
        Heater::setPower((uint8_t)newPower);
        
        const bool tempReached = fabs(currentTemp - targetTemp) < 1.0f;
        if (tempReached) {
            if (!state.hold.tempInRange) {
                state.hold.tempInRange = true;
                state.hold.inRangeStartTime = now;
            }
        } else {
            state.hold.tempInRange = false;
            state.hold.inRangeStartTime = 0;
        }

        const uint32_t stepDurationSec = (uint32_t)state.hold.steps[state.hold.currentStep].duration * 60UL;
        const bool timeElapsed = state.hold.tempInRange && state.hold.inRangeStartTime > 0 &&
            (now - state.hold.inRangeStartTime) >= (stepDurationSec * 1000UL);

        if (timeElapsed) {
            const uint8_t completedStep = state.hold.currentStep;
            state.hold.currentStep++;
            if (state.hold.currentStep < state.hold.stepCount) {
                ControlV2::notePhaseTransition(
                    Mode::HOLD, completedStep, state.hold.currentStep,
                    ControlV2::ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE,
                    getStepCompleteMessage(completedStep, state.hold.stepCount));
                state.hold.targetTemp = state.hold.steps[state.hold.currentStep].temperature;
                state.hold.stepStartTime = now;
                state.hold.tempInRange = false;
                state.hold.inRangeStartTime = 0;
                LOG_I("Hold: Step %d/%d - Target %.1f°C", state.hold.currentStep + 1, state.hold.stepCount, state.hold.targetTemp);
                MQTT::publishNotification("Hold: Новый шаг", "Переход к следующей ступени", "info");
            } else {
                ControlV2::notePhaseTransition(
                    Mode::HOLD, completedStep, state.hold.currentStep,
                    ControlV2::ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE,
                    "Hold program completed");
                state.hold.active = false;
                state.mode = Mode::IDLE;
                Heater::setPower(0);
                LOG_I("Hold: Process complete!");
                MQTT::publishNotification("Hold режим завершён", "Все температурные ступени завершены", "success");
            }
        }
    }
}

void start(SystemState& state, const TempStep* steps, uint8_t count) {
    if (!steps || count == 0 || count > 10) return;
    Pump::stop();
    Valves::closeAll();
    for (uint8_t i = 0; i < count; i++) state.hold.steps[i] = steps[i];
    state.hold.stepCount = count;
    state.hold.currentStep = 0;
    state.hold.targetTemp = steps[0].temperature;
    state.hold.stepStartTime = millis();
    state.hold.tempInRange = false;
    state.hold.inRangeStartTime = 0;
    state.hold.active = true;
    state.mode = Mode::HOLD;
    state.rectPhase = RectPhase::IDLE;
    ControlV2::notePhaseTransition(
        Mode::HOLD, 0, 0, ControlV2::ReasonCodeV2::RC_MODE_START_REQUEST,
        "Hold program started");
    LOG_I("Hold: Started with %d steps", count);
}

} // namespace Hold
} // namespace FSM
