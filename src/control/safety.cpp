/**
 * Smart-Column S3 - Safety logic
 */

#include "safety.h"
#include "../drivers/heater.h"
#include "../drivers/pump.h"
#include "../drivers/valves.h"
#include "../interface/mqtt.h"

namespace Safety {

static float clampSafety(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

void check(SystemState& state, const Settings& settings) {
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
    const uint32_t now = millis();

    const float tsaMaxC = clampSafety(settings.safety.tsaMaxC, 35.0f, 120.0f);
    const float waterOutMaxC = clampSafety(settings.safety.waterOutMaxC, 30.0f, 120.0f);
    const float pressureMaxMmHg = clampSafety(settings.safety.pressureMaxMmHg, 5.0f, 200.0f);
    const float waterOutRiseRateCMin = clampSafety(settings.safety.waterOutRiseRateCMin, 0.5f, 60.0f);
    const float pressureRiseRateMmHgMin = clampSafety(settings.safety.pressureRiseRateMmHgMin, 1.0f, 200.0f);
    state.pressure.critThreshold = pressureMaxMmHg;

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
        LOG_E("SAFETY: Vapor breakthrough! T_TSA=%.1fC", state.temps.tsa);
        emergencyStop = true;
        alarmType = AlarmType::VAPOR_BREAKTHROUGH;
        alarmLevel = AlarmLevel::CRITICAL;

        char msg[128];
        snprintf(msg, sizeof(msg), "Vapor breakthrough! TSA: %.1fC", state.temps.tsa);
        MQTT::publishNotification("CRITICAL", msg, "error");
    }

    if (state.temps.valid[TEMP_WATER_OUT] && state.temps.waterOut > waterOutMaxC) {
        LOG_E("SAFETY: Water overheat! T_water_out=%.1fC", state.temps.waterOut);
        Heater::emergencyStop();
        alarmType = AlarmType::WATER_OVERHEAT;
        alarmLevel = AlarmLevel::CRITICAL;

        char msg[128];
        snprintf(msg, sizeof(msg), "Cooling water overheat! Temp: %.1fC", state.temps.waterOut);
        MQTT::publishNotification("CRITICAL", msg, "error");
    }

    if (state.pressure.cube > pressureMaxMmHg) {
        LOG_E("SAFETY: Column flood! P=%.1f mmHg", state.pressure.cube);
        Heater::setPower(Heater::getPower() * 0.85f);
        alarmType = AlarmType::COLUMN_FLOOD;
        alarmLevel = AlarmLevel::CRITICAL;

        char msg[160];
        snprintf(msg, sizeof(msg), "Column flood! P=%.1f mmHg. Heater power reduced", state.pressure.cube);
        MQTT::publishNotification("WARNING", msg, "warning");
    }

    if (!emergencyStop && waterOutRateValid && state.temps.waterOut > 30.0f &&
        waterOutRiseRate > waterOutRiseRateCMin) {
        LOG_E("SAFETY: Fast water temperature rise! dT/dt=%.1f C/min", waterOutRiseRate);
        emergencyStop = true;
        alarmType = AlarmType::WATER_RISE_RATE;
        alarmLevel = AlarmLevel::CRITICAL;

        char msg[160];
        snprintf(msg, sizeof(msg), "Fast water temp rise: %.1f C/min (limit %.1f)",
                 waterOutRiseRate, waterOutRiseRateCMin);
        MQTT::publishNotification("CRITICAL", msg, "error");
    }

    if (!emergencyStop && pressureRateValid && state.pressure.cube > 5.0f &&
        pressureRiseRate > pressureRiseRateMmHgMin) {
        LOG_E("SAFETY: Fast pressure rise! dP/dt=%.1f mmHg/min", pressureRiseRate);
        emergencyStop = true;
        alarmType = AlarmType::PRESSURE_RISE_RATE;
        alarmLevel = AlarmLevel::CRITICAL;

        char msg[160];
        snprintf(msg, sizeof(msg), "Fast pressure rise: %.1f mmHg/min (limit %.1f)",
                 pressureRiseRate, pressureRiseRateMmHgMin);
        MQTT::publishNotification("CRITICAL", msg, "error");
    }

    if (now - state.temps.lastUpdate > SAFETY_SENSOR_TIMEOUT_MS) {
        LOG_E("SAFETY: Temperature sensor timeout!");
        emergencyStop = true;
        alarmType = AlarmType::SENSOR_FAILURE;
        alarmLevel = AlarmLevel::CRITICAL;

        MQTT::publishNotification(
            "CRITICAL",
            "Temperature sensor timeout! System stopped",
            "error"
        );
    }

    if (emergencyStop) {
        Heater::emergencyStop();
        Pump::stop();
        Valves::closeAll();
        state.safetyOk = false;

        state.currentAlarm.type = alarmType;
        state.currentAlarm.level = alarmLevel;
        state.currentAlarm.timestamp = now;
        state.currentAlarm.acknowledged = false;
        snprintf(
            state.currentAlarm.message,
            sizeof(state.currentAlarm.message),
            "Emergency stop (alarm=%u)",
            static_cast<unsigned>(alarmType)
        );
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
