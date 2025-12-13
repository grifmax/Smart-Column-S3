/**
 * Smart-Column S3 - Система безопасности
 *
 * Проверка аварийных условий и защита оборудования
 */

#include "safety.h"
#include "../drivers/heater.h"
#include "../drivers/pump.h"
#include "../drivers/valves.h"
#include "../interface/mqtt.h"

namespace Safety {

void check(SystemState& state, const Settings& settings) {
    bool emergencyStop = false;
    AlarmType alarmType = AlarmType::NONE;
    AlarmLevel alarmLevel = AlarmLevel::NONE;

    // Проверка прорыва паров (T_TSA > 55°C)
    if (state.temps.valid[TEMP_TSA] && state.temps.tsa > SAFETY_TEMP_TSA_MAX) {
        LOG_E("SAFETY: Vapor breakthrough! T_TSA=%.1f°C", state.temps.tsa);
        emergencyStop = true;
        alarmType = AlarmType::VAPOR_BREAKTHROUGH;
        alarmLevel = AlarmLevel::CRITICAL;

        // Отправка критического уведомления
        char msg[128];
        snprintf(msg, sizeof(msg), "Прорыв паров! Температура TSA: %.1f°C", state.temps.tsa);
        MQTT::publishNotification("КРИТИЧЕСКАЯ ОШИБКА", msg, "error");
    }

    // Проверка перегрева воды (T_water_out > 70°C)
    if (state.temps.valid[TEMP_WATER_OUT] && state.temps.waterOut > SAFETY_TEMP_WATER_OUT_MAX) {
        LOG_E("SAFETY: Water overheat! T_water_out=%.1f°C", state.temps.waterOut);
        Heater::emergencyStop();
        alarmType = AlarmType::WATER_OVERHEAT;
        alarmLevel = AlarmLevel::CRITICAL;

        // Отправка критического уведомления
        char msg[128];
        snprintf(msg, sizeof(msg), "Перегрев воды! Температура: %.1f°C", state.temps.waterOut);
        MQTT::publishNotification("КРИТИЧЕСКАЯ ОШИБКА", msg, "error");
    }

    // Проверка захлёба колонны
    if (state.pressure.cube > state.pressure.critThreshold) {
        LOG_E("SAFETY: Column flood! P=%.1f mmHg", state.pressure.cube);
        Heater::setPower(Heater::getPower() * 0.85); // Снизить мощность
        alarmType = AlarmType::COLUMN_FLOOD;
        alarmLevel = AlarmLevel::CRITICAL;

        // Отправка предупреждения
        char msg[128];
        snprintf(msg, sizeof(msg), "Захлёб колонны! Давление: %.1f mmHg. Мощность снижена", state.pressure.cube);
        MQTT::publishNotification("ПРЕДУПРЕЖДЕНИЕ", msg, "warning");
    }

    // Проверка сбоя датчиков
    uint32_t now = millis();
    if (now - state.temps.lastUpdate > SAFETY_SENSOR_TIMEOUT_MS) {
        LOG_E("SAFETY: Temperature sensor timeout!");
        emergencyStop = true;
        alarmType = AlarmType::SENSOR_FAILURE;
        alarmLevel = AlarmLevel::CRITICAL;

        // Отправка критического уведомления
        MQTT::publishNotification(
            "КРИТИЧЕСКАЯ ОШИБКА",
            "Потеря связи с датчиками температуры! Система остановлена",
            "error"
        );
    }

    // Аварийная остановка
    if (emergencyStop) {
        Heater::emergencyStop();
        Pump::stop();
        Valves::closeAll();
        state.safetyOk = false;

        // Записать аварию
        state.currentAlarm.type = alarmType;
        state.currentAlarm.level = alarmLevel;
        state.currentAlarm.timestamp = now;
        state.currentAlarm.acknowledged = false;
        snprintf(state.currentAlarm.message, sizeof(state.currentAlarm.message),
                 "Emergency stop triggered");
    } else {
        state.safetyOk = true;
    }
}

void acknowledge() {
    LOG_I("SAFETY: Alarm acknowledged");
}

void reset() {
    LOG_I("SAFETY: System reset - safety OK");
}

} // namespace Safety
