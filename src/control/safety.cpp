/**
 * Smart-Column S3 - Safety logic
 */

#include "safety.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "fsm.h"
#include "v2/status_adapter.h"
#include "../drivers/heater.h"
#include "../drivers/pump.h"
#include "../drivers/stirrer.h"
#include "../drivers/valves.h"
#include "../interface/mqtt.h"
#include "../storage/logger.h"

namespace Safety {

SemaphoreHandle_t g_safetyMutex = nullptr;

void init() {
    if (g_safetyMutex == nullptr) {
        g_safetyMutex = xSemaphoreCreateMutex();
    }
}

namespace {

constexpr uint32_t TEMP_SENSOR_STARTUP_GRACE_MS = 15000;

float clampSafety(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

void writeReason(char* reason, size_t reasonSize, const char* message) {
    if (!reason || reasonSize == 0) {
        return;
    }
    snprintf(reason, reasonSize, "%s", message ? message : "");
}

void forceSafeOutputs() {
    Heater::emergencyStop();
    Pump::stop();
    Stirrer::stop(); // аварийная остановка мешалки
    Valves::closeAll();
}

void clearCurrentAlarm(SystemState& state) {
    state.currentAlarm = CurrentAlarm{};
}

void latchAlarm(SystemState& state, AlarmType type, AlarmLevel level,
                const char* message, uint32_t now) {
    // Пишем в лог только если это новая авария или изменился тип
    if (state.currentAlarm.type != type) {
        Logger::logf(2, "Safety alarm latched: %s", message ? message : "Safety alarm");
        // Форсированная запись лога при аварии
        Logger::writeData(state);
    }

    forceSafeOutputs();
    state.safetyOk = false;

    state.currentAlarm.type = type;
    state.currentAlarm.level = level;
    state.currentAlarm.timestamp = now;
    state.currentAlarm.acknowledged = false;
    snprintf(state.currentAlarm.message, sizeof(state.currentAlarm.message), "%s",
             message ? message : "Safety alarm");

    if (state.mode != Mode::IDLE) {
        FSM::abortMode(state);
    }
}

bool hasTempSensorTimeout(const SystemState& state, uint32_t now) {
    if (state.temps.lastUpdate == 0) {
        return now > TEMP_SENSOR_STARTUP_GRACE_MS;
    }
    return (now - state.temps.lastUpdate > SAFETY_SENSOR_TIMEOUT_MS);
}

bool canResetAlarm(const SystemState& state, const Settings& settings, uint32_t now,
                   char* reason, size_t reasonSize) {
    const float tsaMaxC = clampSafety(settings.safety.tsaMaxC, 35.0f, 120.0f);
    const float waterOutMaxC = clampSafety(settings.safety.waterOutMaxC, 30.0f, 120.0f);
    const float pressureMaxMmHg = clampSafety(settings.safety.pressureMaxMmHg, 5.0f, 200.0f);

    if (hasTempSensorTimeout(state, now)) {
        writeReason(reason, reasonSize, "Temperature sensors are still offline or stale");
        return false;
    }

    if (state.temps.valid[TEMP_TSA] && state.temps.tsa > tsaMaxC) {
        char buffer[96];
        snprintf(buffer, sizeof(buffer), "TSA temperature is still too high: %.1fC", state.temps.tsa);
        writeReason(reason, reasonSize, buffer);
        return false;
    }

    if (state.temps.valid[TEMP_WATER_OUT] && state.temps.waterOut > waterOutMaxC) {
        char buffer[96];
        snprintf(buffer, sizeof(buffer), "Cooling water outlet is still too hot: %.1fC", state.temps.waterOut);
        writeReason(reason, reasonSize, buffer);
        return false;
    }

    if (state.pressure.ok && state.pressure.cube > pressureMaxMmHg) {
        char buffer[96];
        snprintf(buffer, sizeof(buffer), "Pressure is still above the safe limit: %.1f mmHg", state.pressure.cube);
        writeReason(reason, reasonSize, buffer);
        return false;
    }

    writeReason(reason, reasonSize, "");
    return true;
}

} // namespace

const char* getAlarmTypeToken(AlarmType type) {
    switch (type) {
        case AlarmType::VAPOR_BREAKTHROUGH: return "vapor_breakthrough";
        case AlarmType::WATER_OVERHEAT: return "water_overheat";
        case AlarmType::WATER_RISE_RATE: return "water_rise_rate";
        case AlarmType::COLUMN_FLOOD: return "column_flood";
        case AlarmType::PRESSURE_RISE_RATE: return "pressure_rise_rate";
        case AlarmType::SENSOR_FAILURE: return "sensor_failure";
        case AlarmType::POWER_FAILURE: return "power_failure";
        case AlarmType::OVERHEAT: return "overheat";
        case AlarmType::LOW_WATER: return "low_water";
        case AlarmType::EMERGENCY_STOP: return "emergency_stop";
        default: return "none";
    }
}

const char* getAlarmLevelToken(AlarmLevel level) {
    switch (level) {
        case AlarmLevel::INFO: return "info";
        case AlarmLevel::WARNING: return "warning";
        case AlarmLevel::ERROR: return "error";
        case AlarmLevel::CRITICAL: return "critical";
        default: return "none";
    }
}

bool isLatched(const SystemState& state) {
    return !state.safetyOk && state.currentAlarm.type != AlarmType::NONE;
}

bool canResetNow(const SystemState& state, const Settings& settings, char* reason, size_t reasonSize) {
    const uint32_t now = millis();
    if (!isLatched(state)) {
        writeReason(reason, reasonSize, "");
        return true;
    }
    return canResetAlarm(state, settings, now, reason, reasonSize);
}

void check(SystemState& state, const Settings& settings) {
    if (g_safetyMutex == nullptr || xSemaphoreTake(g_safetyMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    static bool riseBaselineReady = false;
    static float prevWaterOutC = 0.0f;
    static float prevPressureMmHg = 0.0f;
    static uint32_t prevRiseTsMs = 0;
    static bool prevWaterOutValid = false;
    static bool prevPressureValid = false;
    static bool waterRateArmed = false;
    static bool pressureRateArmed = false;

    bool emergencyStop = false;
    AlarmType alarmType = AlarmType::NONE;
    AlarmLevel alarmLevel = AlarmLevel::NONE;
    char alarmMessage[128] = "";
    const uint32_t now = millis();

    const float tsaMaxC = clampSafety(settings.safety.tsaMaxC, 35.0f, 120.0f);
    const float waterOutMaxC = clampSafety(settings.safety.waterOutMaxC, 30.0f, 120.0f);
    const float pressureMaxMmHg = clampSafety(settings.safety.pressureMaxMmHg, 5.0f, 200.0f);
    const float waterOutRiseRateCMin = clampSafety(settings.safety.waterOutRiseRateCMin, 0.5f, 60.0f);
    const float pressureRiseRateMmHgMin = clampSafety(settings.safety.pressureRiseRateMmHgMin, 1.0f, 200.0f);
    state.pressure.critThreshold = pressureMaxMmHg;

    static bool sensorAlarmLogged = false;

    if (isLatched(state)) {
        // Если включен демо-режим - автоматически сбрасываем аварии датчиков
        if (settings.demoMode && state.currentAlarm.type == AlarmType::SENSOR_FAILURE) {
            LOG_I("SAFETY: Demo mode active, clearing sensor alarm");
            clearCurrentAlarm(state);
            state.safetyOk = true;
            sensorAlarmLogged = false;
            xSemaphoreGive(g_safetyMutex);
            return;
        }
        
        if (state.currentAlarm.type == AlarmType::SENSOR_FAILURE && state.mode == Mode::IDLE) {
            // В IDLE режиме мы просто сбрасываем флаг ошибки, чтобы не блокировать систему,
            // но логируем это только один раз при возникновении.
            clearCurrentAlarm(state);
            state.safetyOk = true;
            xSemaphoreGive(g_safetyMutex);
            return;
        }
        forceSafeOutputs();
        xSemaphoreGive(g_safetyMutex);
        return;
    }

    // Если ошибки нет - сбрасываем флаг логирования
    if (state.temps.valid[TEMP_CUBE] && state.temps.valid[TEMP_COLUMN_BOTTOM]) {
        sensorAlarmLogged = false;
    }

    float waterOutRiseRate = 0.0f;
    float pressureRiseRate = 0.0f;
    bool waterOutRateValid = false;
    bool pressureRateValid = false;

    if (riseBaselineReady && prevRiseTsMs > 0 && now > prevRiseTsMs) {
        const float dtMin = static_cast<float>(now - prevRiseTsMs) / 60000.0f;
        if (dtMin >= 0.016f) {
            if (prevWaterOutValid && state.temps.valid[TEMP_WATER_OUT]) {
                const float currentWaterOutRiseRate = (state.temps.waterOut - prevWaterOutC) / dtMin;
                if (waterRateArmed) {
                    waterOutRiseRate = currentWaterOutRiseRate;
                    waterOutRateValid = true;
                } else {
                    waterRateArmed = true;
                }
            } else {
                waterRateArmed = false;
            }

            if (prevPressureValid && state.pressure.ok) {
                const float currentPressureRiseRate = (state.pressure.cube - prevPressureMmHg) / dtMin;
                if (pressureRateArmed) {
                    pressureRiseRate = currentPressureRiseRate;
                    pressureRateValid = true;
                } else {
                    pressureRateArmed = true;
                }
            } else {
                pressureRateArmed = false;
            }
        }
    }

    if (state.temps.valid[TEMP_WATER_OUT]) {
        prevWaterOutC = state.temps.waterOut;
        prevWaterOutValid = true;
    } else {
        prevWaterOutValid = false;
        waterRateArmed = false;
    }

    if (state.pressure.ok) {
        prevPressureMmHg = state.pressure.cube;
        prevPressureValid = true;
    } else {
        prevPressureValid = false;
        pressureRateArmed = false;
    }

    prevRiseTsMs = now;
    riseBaselineReady = true;

    if (state.temps.valid[TEMP_TSA] && state.temps.tsa > tsaMaxC) {
        emergencyStop = true;
        alarmType = AlarmType::VAPOR_BREAKTHROUGH;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage), "Vapor breakthrough detected at TSA: %.1fC", state.temps.tsa);
    }

    if (!emergencyStop && state.temps.valid[TEMP_WATER_OUT] && state.temps.waterOut > waterOutMaxC) {
        emergencyStop = true;
        alarmType = AlarmType::WATER_OVERHEAT;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage), "Cooling water overheat: %.1fC", state.temps.waterOut);
    }

    if (!emergencyStop && state.pressure.ok && state.pressure.cube > pressureMaxMmHg) {
        emergencyStop = true;
        alarmType = AlarmType::COLUMN_FLOOD;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage), "Pressure exceeded safe limit: %.1f mmHg", state.pressure.cube);
    }

    if (!emergencyStop && !settings.demoMode && waterOutRateValid && state.temps.waterOut > 30.0f && waterOutRiseRate > waterOutRiseRateCMin) {
        emergencyStop = true;
        alarmType = AlarmType::WATER_RISE_RATE;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage), "Water temp rises too fast: %.1f C/min", waterOutRiseRate);
    }

    if (!emergencyStop && !settings.demoMode && pressureRateValid && state.pressure.cube > 5.0f && pressureRiseRate > pressureRiseRateMmHgMin) {
        emergencyStop = true;
        alarmType = AlarmType::PRESSURE_RISE_RATE;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage), "Pressure rises too fast: %.1f mmHg/min", pressureRiseRate);
    }

    // 6. Проверка датчиков температуры (Critical vs Safety)
    if (!settings.demoMode) {
        if (!state.temps.valid[TEMP_CUBE] || !state.temps.valid[TEMP_COLUMN_BOTTOM]) {
            alarmType = AlarmType::SENSOR_FAILURE;
            alarmLevel = AlarmLevel::CRITICAL;
            snprintf(alarmMessage, sizeof(alarmMessage), "CRITICAL sensor failure: %s %s",
                     !state.temps.valid[TEMP_CUBE] ? "CUBE" : "",
                     !state.temps.valid[TEMP_COLUMN_BOTTOM] ? "BASE" : "");
            
            if (state.mode == Mode::IDLE) {
                if (!sensorAlarmLogged) {
                    Logger::logf(2, "Safety: %s (Suppressed in IDLE)", alarmMessage);
                    sensorAlarmLogged = true;
                }
                // В IDLE не переходим в состояние emergencyStop, чтобы не блокировать UI
            } else {
                emergencyStop = true;
            }
        }
        else if (state.mode != Mode::IDLE) {
            if (!state.temps.valid[TEMP_TSA] || !state.temps.valid[TEMP_WATER_OUT] || !state.pressure.ok) {
                alarmType = AlarmType::SENSOR_FAILURE;
                alarmLevel = AlarmLevel::ERROR;
                snprintf(alarmMessage, sizeof(alarmMessage), "Safety sensor failure: %s %s %s",
                         !state.temps.valid[TEMP_TSA] ? "TSA" : "",
                         !state.temps.valid[TEMP_WATER_OUT] ? "WATER" : "",
                         !state.pressure.ok ? "PRESS" : "");
                
                if (!isLatched(state)) {
                    latchAlarm(state, alarmType, alarmLevel, alarmMessage, now);
                }
            }
        }
    }

    if (emergencyStop) {
        latchAlarm(state, alarmType, alarmLevel, alarmMessage, now);
        MQTT::publishNotification("CRITICAL", alarmMessage, "error");
    } else {
        // Если мы не в аварии, но были ошибки (которые мы подавили в IDLE), 
        // state.safetyOk все равно должен быть true для IDLE.
        state.safetyOk = (alarmLevel != AlarmLevel::ERROR && alarmLevel != AlarmLevel::CRITICAL) || (state.mode == Mode::IDLE);
    }

    xSemaphoreGive(g_safetyMutex);
}

void acknowledge(SystemState& state) {
    if (g_safetyMutex == nullptr || xSemaphoreTake(g_safetyMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    if (state.currentAlarm.type != AlarmType::NONE && !state.currentAlarm.acknowledged) {
        state.currentAlarm.acknowledged = true;
        Logger::logf(0, "Safety alarm acknowledged: %s", getAlarmTypeToken(state.currentAlarm.type));
        ControlV2::noteSafetyOperatorAction(
            ControlV2::ReasonCodeV2::RC_SAFETY_ACKNOWLEDGED,
            "Safety alarm acknowledged by operator",
            state.currentAlarm.message);
    }
    xSemaphoreGive(g_safetyMutex);
}

bool reset(SystemState& state, const Settings& settings, char* reason, size_t reasonSize) {
    if (g_safetyMutex == nullptr || xSemaphoreTake(g_safetyMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        writeReason(reason, reasonSize, "Internal error (mutex)");
        return false;
    }

    const uint32_t now = millis();
    if (!isLatched(state)) {
        clearCurrentAlarm(state);
        state.safetyOk = true;
        xSemaphoreGive(g_safetyMutex);
        return true;
    }

    if (!canResetAlarm(state, settings, now, reason, reasonSize)) {
        forceSafeOutputs();
        xSemaphoreGive(g_safetyMutex);
        return false;
    }

    forceSafeOutputs();
    char previousAlarmMessage[sizeof(state.currentAlarm.message)] = "";
    snprintf(previousAlarmMessage, sizeof(previousAlarmMessage), "%s",
             state.currentAlarm.message);
    clearCurrentAlarm(state);
    state.safetyOk = true;
    Logger::logf(0, "Safety alarm cleared");
    ControlV2::noteSafetyOperatorAction(
        ControlV2::ReasonCodeV2::RC_SAFETY_RESET_COMPLETED,
        "Safety alarm reset by operator", previousAlarmMessage);
    xSemaphoreGive(g_safetyMutex);
    return true;
}

} // namespace Safety
