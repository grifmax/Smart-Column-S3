/**
 * Smart-Column S3 - Watt Control & Smart Decrement
 *
 * Алгоритмы управления:
 * - Watt Control: автоматическая регулировка мощности по давлению
 * - Smart Decrement: адаптивное снижение скорости отбора
 */

#include "watt_control.h"
#include "../drivers/heater.h"
#include "../drivers/pump.h"

// =============================================================================
// WATT CONTROL - Управление мощностью по давлению
// =============================================================================

namespace WattControl {

// Глобальные переменные
static float floodPressure = 0;         // Порог захлёба (мм рт.ст.)
static float workThreshold = 0;         // Рабочий порог
static float warnThreshold = 0;         // Предупреждение
static float critThreshold = 0;         // Критический порог
static int8_t overridePower = -1;       // Override мощности (-1 = выкл)
static uint32_t lastFloodTime = 0;      // Время последнего захлёба
static uint8_t floodCount = 0;          // Счётчик захлёбов
static uint8_t powerReduction = 0;      // Накопленное снижение мощности

void init(const EquipmentSettings& settings) {
    LOG_I("WattControl: Initializing...");

    // Рассчитать порог захлёба
    floodPressure = calculateFloodPressure(
        settings.columnHeightMm,
        settings.packingCoeff
    );

    // Рассчитать рабочие пороги
    workThreshold = floodPressure * PRESSURE_WORK_MULT;
    warnThreshold = floodPressure * PRESSURE_WARN_MULT;
    critThreshold = floodPressure * PRESSURE_CRIT_MULT;

    LOG_I("WattControl: P_flood=%.1f, P_work=%.1f, P_warn=%.1f, P_crit=%.1f",
          floodPressure, workThreshold, warnThreshold, critThreshold);
}

float calculateFloodPressure(uint16_t columnHeightMm, float packingCoeff) {
    // P_захлёб = высота_м × коэфф_насадки
    float heightM = columnHeightMm / 1000.0f;
    return heightM * packingCoeff;
}

void setFloodPressure(float pressure) {
    if (pressure > 0 && pressure < 100) {
        floodPressure = pressure;

        // Пересчитать пороги
        workThreshold = floodPressure * PRESSURE_WORK_MULT;
        warnThreshold = floodPressure * PRESSURE_WARN_MULT;
        critThreshold = floodPressure * PRESSURE_CRIT_MULT;

        LOG_I("WattControl: Calibrated P_flood=%.1f", pressure);
    }
}

uint8_t update(const SystemState& state, const Settings& settings) {
    // Если override активен - использовать его
    if (overridePower >= 0) {
        return overridePower;
    }

    float pressure = state.pressure.cube;

    // Проверка захлёба
    if (pressure >= critThreshold) {
        handleFlood();
    }

    // Рассчитать рекомендуемую мощность
    uint8_t recommended = getRecommendedPower(pressure);

    // Применить снижение после захлёбов
    if (powerReduction > 0) {
        if (recommended > powerReduction) {
            recommended -= powerReduction;
        } else {
            recommended = 0;
        }
    }

    // Постепенное восстановление мощности после захлёба
    if (powerReduction > 0 && millis() - lastFloodTime > 60000) {
        // Каждую минуту восстанавливаем 5%
        if (powerReduction >= 5) {
            powerReduction -= 5;
            LOG_I("WattControl: Power reduction decreased to %d%%", powerReduction);
        } else {
            powerReduction = 0;
            LOG_I("WattControl: Power fully restored");
        }
        lastFloodTime = millis();
    }

    return recommended;
}

uint8_t getRecommendedPower(float pressure) {
    // Линейная зависимость: 0% при 0 давлении, 100% при рабочем давлении
    if (pressure <= 0) return 0;
    if (pressure >= workThreshold) return 100;

    // Интерполяция
    uint8_t power = (uint8_t)((pressure / workThreshold) * 100.0f);
    return power;
}

void setOverride(int8_t percent) {
    if (percent >= 0 && percent <= 100) {
        overridePower = percent;
        LOG_I("WattControl: Override set to %d%%", percent);
    } else {
        overridePower = -1;
        LOG_I("WattControl: Override disabled");
    }
}

bool isOverrideActive() {
    return (overridePower >= 0);
}

void handleFlood() {
    uint32_t now = millis();

    // Защита от повторных срабатываний
    if (now - lastFloodTime < 5000) {
        return;
    }

    lastFloodTime = now;
    floodCount++;

    // Снизить мощность на 15%
    uint8_t currentPower = Heater::getPower();
    uint8_t newPower = currentPower * 0.85f;

    // Минимум 30%
    if (newPower < 30) newPower = 30;

    Heater::setPower(newPower);
    powerReduction += 15;
    if (powerReduction > 50) powerReduction = 50; // Максимум -50%

    LOG_E("WattControl: FLOOD detected! Power %d%% → %d%% (reduction: %d%%)",
          currentPower, newPower, powerReduction);

    // Пауза 30 секунд
    delay(30000);

    // Если захлёбы частые - добавить больше снижения
    if (floodCount > 3) {
        powerReduction += 10;
        LOG_E("WattControl: Frequent floods (%d), extra reduction: %d%%",
              floodCount, powerReduction);
    }
}

uint8_t getPressureStatus(float pressure) {
    if (pressure >= critThreshold) return 2;       // Критическое
    if (pressure >= warnThreshold) return 1;       // Предупреждение
    return 0;                                       // Норма
}

void getThresholds(float& work, float& warn, float& crit) {
    work = workThreshold;
    warn = warnThreshold;
    crit = critThreshold;
}

} // namespace WattControl

// =============================================================================
// SMART DECREMENT - Адаптивное снижение скорости отбора
// =============================================================================

namespace SmartDecrement {

// Глобальные переменные
static DecrementState state;

void init(float baseTemp) {
    state.active = false;
    state.baseTemp = baseTemp;
    state.decrementCount = 0;
    state.waitStart = 0;

    LOG_I("SmartDecrement: Init, T_base=%.2f°C", baseTemp);
}

bool update(SystemState& sysState, const Settings& settings) {
    float currentTemp = sysState.temps.columnTop;

    // Проверка валидности температуры
    if (!sysState.temps.valid[TEMP_COLUMN_TOP]) {
        return false;
    }

    // Проверка условия срабатывания
    if (!state.active && shouldDecrement(currentTemp, state.baseTemp)) {
        LOG_I("SmartDecrement: Triggered! T_column=%.2f°C > T_base+%.2f°C",
              currentTemp, DECREMENT_TRIGGER_DELTA);

        // Остановить насос
        Pump::stop();
        state.active = true;
        state.waitStart = millis();

        return false; // Продолжаем работу
    }

    // Если активен - ждём снижения температуры
    if (state.active) {
        uint32_t elapsed = millis() - state.waitStart;

        // Проверка таймаута
        if (elapsed > DECREMENT_WAIT_MAX_SEC * 1000UL) {
            LOG_E("SmartDecrement: Timeout! Transition to TAILS");
            return true; // Переход в хвосты
        }

        // Проверка условия возобновления
        if (canResume(currentTemp, state.baseTemp)) {
            LOG_I("SmartDecrement: Resume! T_column=%.2f°C", currentTemp);

            // Снизить скорость
            float currentSpeed = Pump::getSpeed();
            float newSpeed = currentSpeed * DECREMENT_SPEED_MULT;

            // Проверка минимума
            if (newSpeed < DECREMENT_MIN_SPEED_ML_H_KW *
                          (settings.equipment.heaterPowerW / 1000.0f)) {
                LOG_I("SmartDecrement: Speed too low, transition to TAILS");
                return true; // Переход в хвосты
            }

            // Установить новую скорость
            Pump::start(newSpeed);
            state.active = false;
            state.decrementCount++;

            LOG_I("SmartDecrement: Speed %.0f → %.0f ml/h (count: %d)",
                  currentSpeed, newSpeed, state.decrementCount);

            // Обновить статистику
            sysState.stats.decrementCount = state.decrementCount;
        }
    }

    return false; // Продолжаем работу
}

bool shouldDecrement(float currentTemp, float baseTemp) {
    return (currentTemp > baseTemp + DECREMENT_TRIGGER_DELTA);
}

bool canResume(float currentTemp, float baseTemp) {
    return (currentTemp < baseTemp + DECREMENT_RESUME_DELTA);
}

const DecrementState& getState() {
    return state;
}

void reset() {
    state.active = false;
    state.decrementCount = 0;
    state.waitStart = 0;
    LOG_I("SmartDecrement: Reset");
}

} // namespace SmartDecrement
