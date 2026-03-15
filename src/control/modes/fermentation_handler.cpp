#include "../fsm_utils.h"
#include "../v2/reason_codes.h"
#include "../v2/status_adapter.h"
#include "../../drivers/heater.h"
#include "../../interface/mqtt.h"
#include <Arduino.h>

namespace FSM {
namespace Fermentation {

void update(SystemState& state, const Settings& settings) {
    const uint32_t now = millis();

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
            if (settings.fermentation.durationHours > 0) {
                const uint32_t durationSec =
                    static_cast<uint32_t>(settings.fermentation.durationHours) *
                    3600UL;
                const uint32_t elapsedSec =
                    (now >= getPhaseStartTime())
                        ? (now - getPhaseStartTime()) / 1000UL
                        : 0;
                if (elapsedSec >= durationSec) {
                    Heater::setPower(0);
                    ControlV2::notePhaseTransition(
                        Mode::FERMENTATION,
                        static_cast<uint16_t>(FermentationPhase::RUNNING),
                        static_cast<uint16_t>(FermentationPhase::COMPLETED),
                        ControlV2::ReasonCodeV2::RC_FERM_TARGET_REACHED,
                        "Fermentation duration completed");
                    state.fermPhase = FermentationPhase::COMPLETED;
                    state.mode = Mode::IDLE;
                    MQTT::publishNotification("Ферментация завершена",
                                              "Заданная длительность ферментации достигнута",
                                              "success");
                }
            }
            break;
        default: break;
    }
}

} // namespace Fermentation
} // namespace FSM
