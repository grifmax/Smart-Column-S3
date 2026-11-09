#include "../fsm_utils.h"
#include "../power_command_gateway.h"
#include "../v2/reason_codes.h"
#include "../v2/status_adapter.h"
#include "../../drivers/heater.h"
#include "../../drivers/valves.h"
#include "../../drivers/stirrer.h"
#include "../../drivers/sensors.h"
#include "../../interface/mqtt.h"
#include <Arduino.h>

namespace FSM {
namespace Fermentation {

namespace {
uint32_t coolingChangedAtMs = 0;
bool coolingActive = false;

bool foamDetected(const Settings& settings) {
    if (!settings.equipment.bodyLevelSensorEnabled) return false;
    int16_t raw = 0; float voltage = 0.0f;
    if (!Sensors::readAds1115Channel(ADS_CHANNEL_LEVEL_BODY, raw, voltage)) return false;
    return settings.equipment.bodyLevelTriggerAbove
        ? voltage >= settings.equipment.bodyLevelThresholdV
        : voltage <= settings.equipment.bodyLevelThresholdV;
}

void setPhase(SystemState& state, FermentationPhase next,
              ControlV2::ReasonCodeV2 reason, const char* message) {
    if (state.fermPhase == next) return;
    ControlV2::notePhaseTransition(Mode::FERMENTATION,
        static_cast<uint16_t>(state.fermPhase), static_cast<uint16_t>(next),
        reason, message);
    state.fermPhase = next;
    setPhaseStartTime(millis());
}
}

void update(SystemState& state, const Settings& settings) {
    const uint32_t now = millis();

    if (!state.temps.valid[TEMP_CUBE]) {
        PowerCommandGateway::setHeaterPower(PowerCommandGateway::Owner::FSM,
            PowerCommandGateway::Reason::FSM_CONTROL, 0);
        PowerCommandGateway::setValve(PowerCommandGateway::Owner::FSM,
            PowerCommandGateway::Reason::FSM_CONTROL,
            PowerCommandGateway::Valve::WATER, false);
        return;
    }
    const float currentTemp = state.temps.cube;
    const float target = settings.fermentation.targetTempC;
    const float band = settings.fermentation.hysteresisC;
    const bool foam = foamDetected(settings);
    if (foam) {
        PowerCommandGateway::setHeaterPower(PowerCommandGateway::Owner::FSM,
            PowerCommandGateway::Reason::FSM_CONTROL, 0);
        if (settings.fermentation.useCooling) {
            PowerCommandGateway::setValve(PowerCommandGateway::Owner::FSM,
                PowerCommandGateway::Reason::FSM_CONTROL,
                PowerCommandGateway::Valve::WATER, true);
        }
        if (settings.stirrer.enabled && currentTemp <= MASH_STIRRER_SAFE_TEMP_C) {
            PowerCommandGateway::setStirrerSpeed(PowerCommandGateway::Owner::FSM,
                PowerCommandGateway::Reason::FSM_CONTROL,
                settings.stirrer.defaultSpeedPercent);
        }
        MQTT::publishNotification("Брожение: пена", "Нагрев отключён, включена защита от пены", "warning");
        return;
    }
    if (currentTemp < target - band) setPhase(state, FermentationPhase::HEATING, ControlV2::ReasonCodeV2::RC_TEMP_STEP_REACHED, "Температура ниже коридора");
    else if (currentTemp > target + band && settings.fermentation.useCooling) setPhase(state, FermentationPhase::COOLING, ControlV2::ReasonCodeV2::RC_TEMP_STEP_REACHED, "Температура выше коридора");
    else setPhase(state, FermentationPhase::FERMENTATION, ControlV2::ReasonCodeV2::RC_FERM_TARGET_REACHED, "Температура в рабочем коридоре");

    switch (state.fermPhase) {
        case FermentationPhase::HEATING:
            PowerCommandGateway::setValve(PowerCommandGateway::Owner::FSM,
                PowerCommandGateway::Reason::FSM_CONTROL,
                PowerCommandGateway::Valve::WATER, false); coolingActive = false;
            PowerCommandGateway::setHeaterPower(PowerCommandGateway::Owner::FSM,
                PowerCommandGateway::Reason::FSM_CONTROL,
                settings.fermentation.useHeater ? 10 : 0);
            break;
        case FermentationPhase::COOLING:
            PowerCommandGateway::setHeaterPower(PowerCommandGateway::Owner::FSM,
                PowerCommandGateway::Reason::FSM_CONTROL, 0);
            if (!coolingActive || now - coolingChangedAtMs >= settings.fermentation.coolingMinOffSec * 1000UL) {
                PowerCommandGateway::setValve(PowerCommandGateway::Owner::FSM,
                    PowerCommandGateway::Reason::FSM_CONTROL,
                    PowerCommandGateway::Valve::WATER, true); coolingActive = true; coolingChangedAtMs = now;
            }
            break;
        case FermentationPhase::FERMENTATION:
            PowerCommandGateway::setHeaterPower(PowerCommandGateway::Owner::FSM,
                PowerCommandGateway::Reason::FSM_CONTROL, 0);
            if (coolingActive && now - coolingChangedAtMs >= settings.fermentation.coolingMinOnSec * 1000UL) {
                PowerCommandGateway::setValve(PowerCommandGateway::Owner::FSM,
                    PowerCommandGateway::Reason::FSM_CONTROL,
                    PowerCommandGateway::Valve::WATER, false); coolingActive = false; coolingChangedAtMs = now;
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
                    PowerCommandGateway::setHeaterPower(PowerCommandGateway::Owner::FSM,
                        PowerCommandGateway::Reason::FSM_CONTROL, 0);
                    ControlV2::notePhaseTransition(
                        Mode::FERMENTATION,
                        static_cast<uint16_t>(FermentationPhase::FERMENTATION),
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
