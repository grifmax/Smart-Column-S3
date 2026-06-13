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

void update(SystemState& state, const Settings& settings) {
    uint32_t now = millis();
    uint32_t startTime = getPhaseStartTime();
    uint32_t elapsed = now - startTime;
    const ControlV2::ActiveLimitsV2& liveLimits =
        ControlV2::SafetySupervisorV2::getLiveLimits();

    switch (state.nbkPhase) {
        case NbkPhase::HEATING:
            applyBoosterHeater(state, settings, true);
            Heater::setPower(100);
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
            Heater::setPower(settings.equipment.heaterPowerW > 0 ? (getProcessHeaterPower(state, settings, 70)) : 70); 
            if (!liveLimits.phaseAdvanceBlocked && elapsed > 5 * 60 * 1000UL) {
                LOG_I("NBK: STABILIZATION -> WORKING");
                ControlV2::notePhaseTransition(Mode::NBK,
                                               static_cast<uint16_t>(NbkPhase::STABILIZATION),
                                               static_cast<uint16_t>(NbkPhase::WORKING),
                                               ControlV2::ReasonCodeV2::RC_NBK_STABILIZATION_COMPLETE);
                state.nbkPhase = NbkPhase::WORKING;
                setPhaseStartTime(now);
                MQTT::publishNotification("НБК: Работа", "Подача браги включена", "info");
            }
            break;
            
        case NbkPhase::WORKING: {
            applyBoosterHeater(state, settings, false);
            // Плавный разгон насоса (Ramp-up) в течение 60 секунд
            float targetSpeed = settings.nbk.pumpSpeedMlH;
            if (elapsed < 60000UL) {
                targetSpeed *= (elapsed / 60000.0f);
            }
            if (liveLimits.pumpCapped) {
                if (liveLimits.maxPumpSpeedMlH > 0.0f &&
                    targetSpeed > liveLimits.maxPumpSpeedMlH) {
                    targetSpeed = liveLimits.maxPumpSpeedMlH;
                } else if (liveLimits.maxPumpSpeedMlH <= 0.0f) {
                    targetSpeed = 0.0f;
                }
            }

            if (liveLimits.pumpCapped || liveLimits.antiOscillationActive) {
                Pump::stop();
            } else if (state.temps.valid[TEMP_COLUMN_BOTTOM]) {
                if (state.temps.columnBottom < settings.nbk.columnBottomTempThresholdC) {
                    Pump::stop();
                    LOG_W("NBK: Temp %.1f < %.1f. Pump stopped.", state.temps.columnBottom, settings.nbk.columnBottomTempThresholdC);
                } else if (state.temps.columnBottom > settings.nbk.columnBottomTempThresholdC + 0.5f) {
                    Pump::start(targetSpeed);
                }
            } else {
                Pump::stop();
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
