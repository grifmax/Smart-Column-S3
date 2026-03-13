#include "../fsm_utils.h"
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

    switch (state.nbkPhase) {
        case NbkPhase::HEATING:
            Heater::setPower(100);
            if (state.temps.cube >= getWaterAutoStartTempC(settings)) {
                Valves::setWater(true);
            }
            if (state.temps.valid[TEMP_CUBE] && state.temps.cube > 98.0f) {
                LOG_I("NBK: HEATING -> STABILIZATION");
                state.nbkPhase = NbkPhase::STABILIZATION;
                setPhaseStartTime(now);
                MQTT::publishNotification("НБК: Стабилизация", "Парогенератор разогрет, стабилизация колонны", "info");
            }
            break;
            
        case NbkPhase::STABILIZATION:
            Heater::setPower(settings.equipment.heaterPowerW > 0 ? (getProcessHeaterPower(state, settings, 70)) : 70); 
            if (elapsed > 5 * 60 * 1000UL) {
                LOG_I("NBK: STABILIZATION -> WORKING");
                state.nbkPhase = NbkPhase::WORKING;
                setPhaseStartTime(now);
                MQTT::publishNotification("НБК: Работа", "Подача браги включена", "info");
            }
            break;
            
        case NbkPhase::WORKING: {
            // Плавный разгон насоса (Ramp-up) в течение 60 секунд
            float targetSpeed = settings.nbk.pumpSpeedMlH;
            if (elapsed < 60000UL) {
                targetSpeed *= (elapsed / 60000.0f);
            }

            if (state.temps.valid[TEMP_COLUMN_BOTTOM]) {
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
            uint8_t power = settings.equipment.heaterPowerW > 0 ? (getProcessHeaterPower(state, settings, 70)) : 70;
            if (state.pressure.ok && state.pressure.cube > settings.safety.pressureMaxMmHg * 0.85f) {
                power = (uint8_t)(power * 0.8f);
                if (power < 30) power = 30;
                static uint32_t lastPressWarn = 0;
                if (now - lastPressWarn > 30000UL) {
                    MQTT::publishNotification("НБК: Высокое давление", "Мощность снижена для стабилизации давления", "warning");
                    lastPressWarn = now;
                }
            }
            Heater::setPower(power);
            break;
        }
            
        case NbkPhase::FINISH:
            Pump::stop();
            Heater::setPower(0);
            if (elapsed > 5 * 60 * 1000UL) {
                Valves::setWater(false);
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
