/**
 * Smart-Column S3 - Safety logic
 */

#include "safety.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "fsm.h"
#include "power_command_gateway.h"
#include "runtime_metrics.h"
#include "sensor_freshness_logic.h"
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
    PowerCommandGateway::tripSafety(
        PowerCommandGateway::Reason::SAFETY_TRIP);
}

const char* getTopologyRoleLabel(uint8_t index) {
    switch (index) {
        case TEMP_CUBE:
            return "куб";
        case TEMP_COLUMN_BOTTOM:
            return "низ колонны";
        case TEMP_COLUMN_TOP:
            return "верх колонны";
        case TEMP_REFLUX:
            return "дефлегматор";
        case TEMP_TSA:
            return "TSA";
        case TEMP_WATER_IN:
            return "вход воды";
        case TEMP_WATER_OUT:
            return "выход воды";
        default:
            return "датчик";
    }
}

bool isInstalledByIndex(const EquipmentSettings& equipment, uint8_t index) {
    switch (index) {
        case TEMP_CUBE:
            return equipment.temperatureTopology.cube;
        case TEMP_COLUMN_BOTTOM:
            return equipment.temperatureTopology.columnBottom;
        case TEMP_COLUMN_TOP:
            return equipment.temperatureTopology.columnTop;
        case TEMP_REFLUX:
            return equipment.temperatureTopology.reflux;
        case TEMP_TSA:
            return equipment.temperatureTopology.tsa;
        case TEMP_WATER_IN:
            return equipment.temperatureTopology.waterIn;
        case TEMP_WATER_OUT:
            return equipment.temperatureTopology.waterOut;
        default:
            return false;
    }
}

void appendTopologyRole(char* buffer, size_t bufferSize, bool& needComma,
                        const char* label) {
    if (!buffer || bufferSize == 0 || !label || label[0] == '\0') {
        return;
    }
    if (needComma) {
        strlcat(buffer, ", ", bufferSize);
    }
    strlcat(buffer, label, bufferSize);
    needComma = true;
}

RequiredSensorsMask buildRequiredSensorsMask(Mode mode, const Settings& settings) {
    RequiredSensorsMask required;
    const EquipmentSettings& equipment = settings.equipment;
    switch (mode) {
        case Mode::RECTIFICATION:
        case Mode::MANUAL_RECT:
            required.cubeTemp = isInstalledByIndex(equipment, TEMP_CUBE);
            required.columnBottomTemp = isInstalledByIndex(equipment, TEMP_COLUMN_BOTTOM);
            required.tsaTemp = isInstalledByIndex(equipment, TEMP_TSA);
            required.waterOutTemp = isInstalledByIndex(equipment, TEMP_WATER_OUT);
            required.pressure = settings.rectParams.pressureControlEnabled;
            break;
        case Mode::DISTILLATION:
            required.cubeTemp = isInstalledByIndex(equipment, TEMP_CUBE);
            required.tsaTemp = isInstalledByIndex(equipment, TEMP_TSA);
            required.waterOutTemp = isInstalledByIndex(equipment, TEMP_WATER_OUT);
            required.pressure = settings.rectParams.pressureControlEnabled;
            break;
        case Mode::NBK:
            required.cubeTemp = isInstalledByIndex(equipment, TEMP_CUBE);
            required.columnBottomTemp = isInstalledByIndex(equipment, TEMP_COLUMN_BOTTOM);
            required.tsaTemp = isInstalledByIndex(equipment, TEMP_TSA);
            required.waterOutTemp = isInstalledByIndex(equipment, TEMP_WATER_OUT);
            required.pressure = settings.rectParams.pressureControlEnabled;
            break;
        case Mode::MASHING:
        case Mode::HOLD:
        case Mode::FERMENTATION:
            required.cubeTemp = isInstalledByIndex(equipment, TEMP_CUBE);
            break;
        case Mode::AUTOCLAVE:
            required.cubeTemp = isInstalledByIndex(equipment, TEMP_CUBE);
            required.pressure = true;
            break;
        case Mode::IDLE:
        default:
            break;
    }
    return required;
}

RequiredSensorsMask buildBlockingSensorFailureMask(Mode mode, const Settings& settings) {
    RequiredSensorsMask required = buildRequiredSensorsMask(mode, settings);
    return required;
}

bool buildModeTopologySupport(Mode mode, const EquipmentSettings& equipment,
                              char* reason, size_t reasonSize) {
    bool supported = true;
    bool needComma = false;
    char missing[96] = "";

    auto requireRole = [&](uint8_t index) {
        if (!isInstalledByIndex(equipment, index)) {
            appendTopologyRole(missing, sizeof(missing), needComma, getTopologyRoleLabel(index));
            supported = false;
        }
    };

    switch (mode) {
        case Mode::RECTIFICATION:
        case Mode::MANUAL_RECT:
            requireRole(TEMP_CUBE);
            if (!isInstalledByIndex(equipment, TEMP_COLUMN_BOTTOM) &&
                !isInstalledByIndex(equipment, TEMP_COLUMN_TOP)) {
                appendTopologyRole(missing, sizeof(missing), needComma, "колонна");
                supported = false;
            }
            requireRole(TEMP_TSA);
            break;
        case Mode::DISTILLATION:
            requireRole(TEMP_CUBE);
            requireRole(TEMP_TSA);
            break;
        case Mode::NBK:
            requireRole(TEMP_CUBE);
            requireRole(TEMP_COLUMN_BOTTOM);
            requireRole(TEMP_TSA);
            break;
        case Mode::MASHING:
        case Mode::HOLD:
        case Mode::FERMENTATION:
            requireRole(TEMP_CUBE);
            break;
        case Mode::AUTOCLAVE:
            requireRole(TEMP_CUBE);
            break;
        case Mode::IDLE:
        default:
            break;
    }

    if (supported) {
        writeReason(reason, reasonSize, "");
        return true;
    }

    char buffer[160];
    snprintf(buffer, sizeof(buffer),
             "Топология датчиков не подходит для режима: не отмечены %s",
             missing[0] != '\0' ? missing : "обязательные роли");
    writeReason(reason, reasonSize, buffer);
    return false;
}

bool isTemperatureFresh(const SystemState& state, uint8_t index,
                        uint32_t now) {
    if (index >= TEMP_COUNT) return false;
    return SensorFreshnessLogic::isRecentSample(
        state.temps.lastUpdateBySensor[index], now,
        SAFETY_SENSOR_TIMEOUT_MS);
}

bool isPressureFresh(const SystemState& state, uint32_t now) {
    return SensorFreshnessLogic::isFresh(
        state.pressure.ok, state.pressure.lastUpdate, now,
        SAFETY_SENSOR_TIMEOUT_MS);
}

bool areRequiredSensorsFresh(const SystemState& state,
                             const RequiredSensorsMask& required,
                             uint32_t now) {
    return (!required.cubeTemp || isTemperatureFresh(state, TEMP_CUBE, now)) &&
           (!required.columnBottomTemp ||
            isTemperatureFresh(state, TEMP_COLUMN_BOTTOM, now)) &&
           (!required.tsaTemp || isTemperatureFresh(state, TEMP_TSA, now)) &&
           (!required.waterOutTemp ||
            isTemperatureFresh(state, TEMP_WATER_OUT, now)) &&
           SensorFreshnessLogic::isAvailableWhenRequired(
               required.pressure, state.pressure.ok,
               state.pressure.lastUpdate, now, SAFETY_SENSOR_TIMEOUT_MS);
}

uint8_t getMissingRequiredSensorsMask(const SystemState& state,
                                     const RequiredSensorsMask& required,
                                     uint32_t now) {
    uint8_t missing = 0;
    if (required.cubeTemp && !isTemperatureFresh(state, TEMP_CUBE, now)) {
        missing |= 1U << 0;
    }
    if (required.columnBottomTemp &&
        !isTemperatureFresh(state, TEMP_COLUMN_BOTTOM, now)) {
        missing |= 1U << 1;
    }
    if (required.tsaTemp && !isTemperatureFresh(state, TEMP_TSA, now)) {
        missing |= 1U << 2;
    }
    if (required.waterOutTemp &&
        !isTemperatureFresh(state, TEMP_WATER_OUT, now)) {
        missing |= 1U << 3;
    }
    if (required.pressure && !isPressureFresh(state, now)) {
        missing |= 1U << 4;
    }
    return missing;
}

void appendMissingRequiredSensors(const SystemState& state,
                                  const RequiredSensorsMask& required,
                                 uint32_t now,
                                  char* buffer,
                                  size_t bufferSize) {
    if (!buffer || bufferSize == 0) {
        return;
    }

    buffer[0] = '\0';
    bool needComma = false;
    const auto appendToken = [&](const char* token) {
        if (!token || token[0] == '\0') {
            return;
        }
        if (needComma) {
            strlcat(buffer, ", ", bufferSize);
        }
        strlcat(buffer, token, bufferSize);
        needComma = true;
    };

    const uint8_t missing = getMissingRequiredSensorsMask(state, required, now);
    if (missing & (1U << 0)) appendToken("CUBE");
    if (missing & (1U << 1)) appendToken("BASE");
    if (missing & (1U << 2)) appendToken("TSA");
    if (missing & (1U << 3)) appendToken("WATER");
    if (missing & (1U << 4)) appendToken("PRESS");
}

void clearCurrentAlarm(SystemState& state) {
    state.currentAlarm = CurrentAlarm{};
}

void latchAlarm(SystemState& state, AlarmType type, AlarmLevel level,
                const char* message, uint32_t now) {
    // Пишем в лог только если это новая авария или изменился тип
    if (state.currentAlarm.type != type) {
        Logger::logf(2, "Авария защиты зафиксирована: %s", message ? message : "Авария защиты");
        // Форсированная запись лога при аварии
        Logger::writeData(state);
    }

    uint32_t sampleAgeMs = UINT32_MAX;
    switch (type) {
        case AlarmType::COLUMN_FLOOD:
        case AlarmType::PRESSURE_RISE_RATE:
            sampleAgeMs = ControlRuntimeMetricsLogic::sampleAgeMs(
                now, state.pressure.lastUpdate, state.pressure.ok);
            break;
        case AlarmType::WATER_OVERHEAT:
        case AlarmType::WATER_RISE_RATE:
            sampleAgeMs = ControlRuntimeMetricsLogic::sampleAgeMs(
                now, state.temps.lastUpdateBySensor[TEMP_WATER_OUT],
                state.temps.valid[TEMP_WATER_OUT]);
            break;
        case AlarmType::VAPOR_BREAKTHROUGH:
            sampleAgeMs = ControlRuntimeMetricsLogic::sampleAgeMs(
                now, state.temps.lastUpdateBySensor[TEMP_TSA],
                state.temps.valid[TEMP_TSA]);
            break;
        default:
            break;
    }
    const uint32_t outputStopStartedAtUs = micros();
    forceSafeOutputs();
    const uint32_t outputStopUs = micros() - outputStopStartedAtUs;
    ControlRuntimeMetrics::recordSafetyTrip(sampleAgeMs, outputStopUs);
    Logger::logf(1,
                 "Safety response timing: sample_age_ms=%lu, "
                 "output_stop_us=%lu",
                 static_cast<unsigned long>(sampleAgeMs),
                 static_cast<unsigned long>(outputStopUs));
    state.safetyOk = false;

    state.currentAlarm.type = type;
    state.currentAlarm.level = level;
    state.currentAlarm.timestamp = now;
    state.currentAlarm.acknowledged = false;
    snprintf(state.currentAlarm.message, sizeof(state.currentAlarm.message), "%s",
             message ? message : "Авария защиты");

    if (state.mode != Mode::IDLE) {
        FSM::abortMode(state);
    }
}

bool canResetAlarm(const SystemState& state, const Settings& settings, uint32_t now,
                   char* reason, size_t reasonSize) {
    const float tsaMaxC = clampSafety(settings.safety.tsaMaxC, 35.0f, 120.0f);
    const float waterOutMaxC = clampSafety(settings.safety.waterOutMaxC, 30.0f, 120.0f);
    const float pressureMaxMmHg = clampSafety(settings.safety.pressureMaxMmHg, 5.0f, 200.0f);

    RequiredSensorsMask resetRequired;
    resetRequired.cubeTemp = isInstalledByIndex(settings.equipment, TEMP_CUBE);
    resetRequired.tsaTemp = isInstalledByIndex(settings.equipment, TEMP_TSA);
    resetRequired.waterOutTemp = isInstalledByIndex(settings.equipment, TEMP_WATER_OUT);
    resetRequired.pressure = settings.rectParams.pressureControlEnabled;
    if (!areRequiredSensorsFresh(state, resetRequired, now) &&
        now > TEMP_SENSOR_STARTUP_GRACE_MS) {
        writeReason(reason, reasonSize, "Temperature sensors are still offline or stale");
        return false;
    }

    if (isTemperatureFresh(state, TEMP_TSA, now) &&
        state.temps.tsa > tsaMaxC) {
        char buffer[96];
        snprintf(buffer, sizeof(buffer), "TSA temperature is still too high: %.1fC", state.temps.tsa);
        writeReason(reason, reasonSize, buffer);
        return false;
    }

    if (isTemperatureFresh(state, TEMP_WATER_OUT, now) &&
        state.temps.waterOut > waterOutMaxC) {
        char buffer[96];
        snprintf(buffer, sizeof(buffer), "Выход охлаждающей воды всё ещё слишком горячий: %.1f °C", state.temps.waterOut);
        writeReason(reason, reasonSize, buffer);
        return false;
    }

    if (settings.rectParams.pressureControlEnabled &&
        isPressureFresh(state, now) && state.pressure.cube > pressureMaxMmHg) {
        char buffer[96];
        snprintf(buffer, sizeof(buffer), "Pressure is still above the safe limit: %.1f mmHg", state.pressure.cube);
        writeReason(reason, reasonSize, buffer);
        return false;
    }

    writeReason(reason, reasonSize, "");
    return true;
}

} // namespace

bool isTempSensorInstalled(const EquipmentSettings& equipment, uint8_t index) {
    return isInstalledByIndex(equipment, index);
}

uint8_t getInstalledTempSensorCount(const EquipmentSettings& equipment) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < TEMP_COUNT; ++i) {
        if (isInstalledByIndex(equipment, i)) {
            ++count;
        }
    }
    return count;
}

bool isModeTemperatureTopologySupported(Mode mode, const EquipmentSettings& equipment,
                                        char* reason, size_t reasonSize) {
    return buildModeTopologySupport(mode, equipment, reason, reasonSize);
}

RequiredSensorsMask getRequiredSensorsForMode(Mode mode, const Settings& settings) {
    return buildRequiredSensorsMask(mode, settings);
}

bool isTemperatureSampleFresh(const SystemState& state, uint8_t index,
                              uint32_t nowMs) {
    return isTemperatureFresh(state, index, nowMs);
}

bool isPressureSampleFresh(const SystemState& state, uint32_t nowMs) {
    return isPressureFresh(state, nowMs);
}

bool areRequiredSensorsAvailable(const SystemState& state,
                                 const RequiredSensorsMask& required,
                                 uint32_t nowMs) {
    return areRequiredSensorsFresh(state, required, nowMs);
}

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

    static float prevWaterOutC = 0.0f;
    static float prevPressureMmHg = 0.0f;
    static uint32_t prevWaterOutSampleMs = 0;
    static uint32_t prevPressureSampleMs = 0;
    static bool prevWaterOutValid = false;
    static bool prevPressureValid = false;

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
    const RequiredSensorsMask required = buildRequiredSensorsMask(state.mode, settings);
    const RequiredSensorsMask blockingSensorFailure =
        buildBlockingSensorFailureMask(state.mode, settings);
    state.pressure.critThreshold = pressureMaxMmHg;

    static bool sensorAlarmLogged = false;

    if (isLatched(state)) {
        // Если включен демо-режим - автоматически сбрасываем аварии датчиков
        if (settings.demoMode && state.currentAlarm.type == AlarmType::SENSOR_FAILURE) {
            LOG_I("SAFETY: Demo mode active, clearing sensor alarm");
            clearCurrentAlarm(state);
            state.safetyOk = true;
            PowerCommandGateway::clearSafetyTrip();
            sensorAlarmLogged = false;
            xSemaphoreGive(g_safetyMutex);
            return;
        }
        
        if (state.currentAlarm.type == AlarmType::SENSOR_FAILURE && state.mode == Mode::IDLE) {
            // В IDLE режиме мы просто сбрасываем флаг ошибки, чтобы не блокировать систему,
            // но логируем это только один раз при возникновении.
            clearCurrentAlarm(state);
            state.safetyOk = true;
            PowerCommandGateway::clearSafetyTrip();
            xSemaphoreGive(g_safetyMutex);
            return;
        }
        forceSafeOutputs();
        xSemaphoreGive(g_safetyMutex);
        return;
    }

    // Если ошибки нет - сбрасываем флаг логирования и счетчик отвалов
    if (areRequiredSensorsAvailable(state, blockingSensorFailure, now)) {
        sensorAlarmLogged = false;
    }

    float waterOutRiseRate = 0.0f;
    float pressureRiseRate = 0.0f;
    bool waterOutRateValid = false;
    bool pressureRateValid = false;

    const uint32_t waterOutSampleMs =
        state.temps.lastUpdateBySensor[TEMP_WATER_OUT];
    if (required.waterOutTemp &&
        isTemperatureFresh(state, TEMP_WATER_OUT, now) &&
        waterOutSampleMs != prevWaterOutSampleMs) {
        if (prevWaterOutValid) {
            const uint32_t sampleDeltaMs = waterOutSampleMs - prevWaterOutSampleMs;
            if (sampleDeltaMs >= 960) {
                waterOutRiseRate =
                    (state.temps.waterOut - prevWaterOutC) * 60000.0f /
                    static_cast<float>(sampleDeltaMs);
                waterOutRateValid = true;
            }
        }
        prevWaterOutC = state.temps.waterOut;
        prevWaterOutSampleMs = waterOutSampleMs;
        prevWaterOutValid = true;
    } else if (required.waterOutTemp &&
               !isTemperatureFresh(state, TEMP_WATER_OUT, now)) {
        prevWaterOutValid = false;
        prevWaterOutSampleMs = 0;
    }

    if (required.pressure && isPressureFresh(state, now) &&
        state.pressure.lastUpdate != prevPressureSampleMs) {
        if (prevPressureValid) {
            const uint32_t sampleDeltaMs =
                state.pressure.lastUpdate - prevPressureSampleMs;
            if (sampleDeltaMs >= 960) {
                pressureRiseRate =
                    (state.pressure.cube - prevPressureMmHg) * 60000.0f /
                    static_cast<float>(sampleDeltaMs);
                pressureRateValid = true;
            }
        }
        prevPressureMmHg = state.pressure.cube;
        prevPressureSampleMs = state.pressure.lastUpdate;
        prevPressureValid = true;
    } else if (required.pressure && !isPressureFresh(state, now)) {
        prevPressureValid = false;
        prevPressureSampleMs = 0;
    }

    if (required.tsaTemp && isTemperatureFresh(state, TEMP_TSA, now) &&
        state.temps.tsa > tsaMaxC) {
        emergencyStop = true;
        alarmType = AlarmType::VAPOR_BREAKTHROUGH;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage), "Прорыв паров обнаружен на ТСА: %.1f °C", state.temps.tsa);
    }

    if (!emergencyStop && required.waterOutTemp &&
        isTemperatureFresh(state, TEMP_WATER_OUT, now) &&
        state.temps.waterOut > waterOutMaxC) {
        emergencyStop = true;
        alarmType = AlarmType::WATER_OVERHEAT;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage), "Перегрев воды охлаждения: %.1f °C", state.temps.waterOut);
    }

    if (!emergencyStop && required.pressure &&
        isPressureFresh(state, now) && state.pressure.cube > pressureMaxMmHg) {
        emergencyStop = true;
        alarmType = AlarmType::COLUMN_FLOOD;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage), "Давление превысило безопасный предел: %.1f мм рт. ст.", state.pressure.cube);
    }

    if (!emergencyStop && settings.equipment.leakSensorEnabled &&
        state.leak.lastUpdate > 0 && !state.leak.valid) {
        emergencyStop = true;
        alarmType = AlarmType::SENSOR_FAILURE;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage),
                 "Опциональный датчик протечки недоступен");
    }

    if (!emergencyStop && settings.equipment.leakSensorEnabled &&
        state.leak.valid && state.leak.triggered) {
        emergencyStop = true;
        alarmType = AlarmType::EMERGENCY_STOP;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage),
                 "Сработал датчик протечки: %.3f В", state.leak.voltage);
    }

#if VAPOR_SENSOR_ENABLED
    if (!emergencyStop &&
        ((!state.vaporPrimary.valid && state.vaporPrimary.lastUpdate > 0) ||
         (!state.vaporSecondary.valid && state.vaporSecondary.lastUpdate > 0))) {
        emergencyStop = true;
        alarmType = AlarmType::SENSOR_FAILURE;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage),
                 "Опциональный датчик паров недоступен");
    }
    if (!emergencyStop &&
        ((state.vaporPrimary.valid && state.vaporPrimary.triggered) ||
         (state.vaporSecondary.valid && state.vaporSecondary.triggered))) {
        emergencyStop = true;
        alarmType = AlarmType::EMERGENCY_STOP;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage),
                 "Сработал опциональный датчик паров");
    }
#endif

    if (!emergencyStop && !settings.demoMode && required.waterOutTemp &&
        waterOutRateValid && state.temps.waterOut > 30.0f &&
        waterOutRiseRate > waterOutRiseRateCMin) {
        emergencyStop = true;
        alarmType = AlarmType::WATER_RISE_RATE;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage), "Температура воды растёт слишком быстро: %.1f °C/мин", waterOutRiseRate);
    }

    if (!emergencyStop && !settings.demoMode && required.pressure &&
        pressureRateValid && state.pressure.cube > 5.0f &&
        pressureRiseRate > pressureRiseRateMmHgMin) {
        emergencyStop = true;
        alarmType = AlarmType::PRESSURE_RISE_RATE;
        alarmLevel = AlarmLevel::CRITICAL;
        snprintf(alarmMessage, sizeof(alarmMessage), "Давление растёт слишком быстро: %.1f мм рт. ст./мин", pressureRiseRate);
    }

    // 6. Проверка обязательных датчиков по режиму
    if (!settings.demoMode) {
        char missingSensors[64] = "";
        appendMissingRequiredSensors(state, blockingSensorFailure, now,
                                     missingSensors, sizeof(missingSensors));

        if (!areRequiredSensorsAvailable(state, blockingSensorFailure, now)) {
            ControlRuntimeMetrics::recordStaleSensorCheck();
            alarmType = AlarmType::SENSOR_FAILURE;
            alarmLevel = AlarmLevel::CRITICAL;
            snprintf(alarmMessage, sizeof(alarmMessage), "Критическая ошибка датчиков: %s",
                     missingSensors[0] != '\0' ? missingSensors : "обязательные датчики");

            if (state.mode == Mode::IDLE) {
                if (!sensorAlarmLogged) {
                    Logger::logf(2, "Защита: %s (подавлено в ожидании)", alarmMessage);
                    sensorAlarmLogged = true;
                }
            } else {
                emergencyStop = true;
            }
        }
    }

    if (emergencyStop) {
        latchAlarm(state, alarmType, alarmLevel, alarmMessage, now);
        MQTT::publishNotification("Критическая авария", alarmMessage, "error");
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
        Logger::logf(0, "Авария защиты подтверждена: %s", getAlarmTypeToken(state.currentAlarm.type));
        ControlV2::noteSafetyOperatorAction(
            ControlV2::ReasonCodeV2::RC_SAFETY_ACKNOWLEDGED,
            "Авария защиты подтверждена оператором",
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
        PowerCommandGateway::clearSafetyTrip();
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
    PowerCommandGateway::clearSafetyTrip();
    Logger::logf(0, "Авария защиты сброшена");
    ControlV2::noteSafetyOperatorAction(
        ControlV2::ReasonCodeV2::RC_SAFETY_RESET_COMPLETED,
        "Авария защиты сброшена оператором", previousAlarmMessage);
    xSemaphoreGive(g_safetyMutex);
    return true;
}

} // namespace Safety
