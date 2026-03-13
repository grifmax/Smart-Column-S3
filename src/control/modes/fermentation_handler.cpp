#include "../fsm_utils.h"
#include "../../drivers/heater.h"
#include <Arduino.h>

namespace FSM {
namespace Fermentation {

void update(SystemState& state, const Settings& settings) {
    switch (state.fermPhase) {
        case FermentationPhase::RUNNING:
            if (settings.fermentation.useHeater) {
                float currentTemp = state.temps.cube;
                if (state.temps.valid[TEMP_CUBE]) {
                    if (currentTemp < settings.fermentation.targetTempC - settings.fermentation.hysteresisC) {
                        Heater::setPower(10);
                    } else if (currentTemp > settings.fermentation.targetTempC) {
                        Heater::setPower(0);
                    }
                }
            }
            break;
        default: break;
    }
}

} // namespace Fermentation
} // namespace FSM
