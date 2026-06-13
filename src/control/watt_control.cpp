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
#include "v2/process_indicators.h"

#include <math.h>

// =============================================================================
// WATT CONTROL - Управление мощностью по давлению
// =============================================================================

namespace WattControl {

static float floodPressure = 0.0f;      // Порог захлёба, мм рт.ст.
static float workThreshold = 0.0f;      // Рабочий порог
static float warnThreshold = 0.0f;      // Предупредительный порог
static float critThreshold = 0.0f;      // Критический порог
static int8_t overridePower = -1;       // Override мощности (-1 = выкл)
static uint32_t lastFloodTime = 0;      // Время последнего захлёба
static uint32_t floodPauseUntil = 0;    // Пауза после захлёба
static uint8_t floodCount = 0;          // Счётчик захлёбов
static uint8_t powerReduction = 0;      // Накопленное снижение мощности

void init(const EquipmentSettings& settings) {
    LOG_I("WattControl: Initializing...");

    floodPressure =
        calculateFloodPressure(settings.columnHeightMm, settings.packingCoeff);
    workThreshold = floodPressure * PRESSURE_WORK_MULT;
    warnThreshold = floodPressure * PRESSURE_WARN_MULT;
    critThreshold = floodPressure * PRESSURE_CRIT_MULT;

    LOG_I("WattControl: P_flood=%.1f, P_work=%.1f, P_warn=%.1f, P_crit=%.1f",
          floodPressure, workThreshold, warnThreshold, critThreshold);
}

float calculateFloodPressure(uint16_t columnHeightMm, float packingCoeff) {
    const float heightM = columnHeightMm / 1000.0f;
    return heightM * packingCoeff;
}

void setFloodPressure(float pressure) {
    if (pressure <= 0.0f || pressure >= 100.0f) {
        return;
    }

    floodPressure = pressure;
    workThreshold = floodPressure * PRESSURE_WORK_MULT;
    warnThreshold = floodPressure * PRESSURE_WARN_MULT;
    critThreshold = floodPressure * PRESSURE_CRIT_MULT;

    LOG_I("WattControl: Calibrated P_flood=%.1f", pressure);
}

uint8_t update(const SystemState& state, const Settings& settings) {
    const uint32_t now = millis();

    if (overridePower >= 0) {
        return overridePower;
    }

    const float pressure = state.pressure.cube;
    if (pressure >= critThreshold) {
        handleFlood();
    }

    if (now < floodPauseUntil) {
        return Heater::getPower();
    }

    uint8_t recommended = getRecommendedPower(pressure);
    if (powerReduction > 0) {
        recommended =
            recommended > powerReduction ? recommended - powerReduction : 0;
    }

    if (powerReduction > 0 && now - lastFloodTime > 60000UL) {
        if (powerReduction >= 5) {
            powerReduction -= 5;
            LOG_I("WattControl: Power reduction decreased to %d%%",
                  powerReduction);
        } else {
            powerReduction = 0;
            LOG_I("WattControl: Power fully restored");
        }
        lastFloodTime = now;
    }

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
    const float currentVolt = state.power.voltage > 0 ? state.power.voltage : 230.0f;
    const uint16_t delayUs = calculateTriacDelay(recommended, currentVolt);
    Heater::setTriacDelay(delayUs);
#endif

    (void)settings;
    return recommended;
}

uint8_t getRecommendedPower(float pressure) {
    if (pressure <= 0.0f) return 0;
    if (pressure >= workThreshold) return 100;
    return static_cast<uint8_t>((pressure / workThreshold) * 100.0f);
}

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
uint16_t calculateTriacDelay(uint8_t targetPowerPercent, float currentVoltage) {
    if (targetPowerPercent == 0) return TRIAC_MAX_ALPHA_US;
    if (targetPowerPercent >= 100) return TRIAC_MIN_ALPHA_US;

    const float targetPowerW =
        (targetPowerPercent / 100.0f) * TRIAC_MAX_POWER_W;
    const float heaterResistance =
        (230.0f * 230.0f) / TRIAC_MAX_POWER_W;
    const float maxPossiblePowerW =
        (currentVoltage * currentVoltage) / heaterResistance;

    if (targetPowerW >= maxPossiblePowerW) {
        return TRIAC_MIN_ALPHA_US;
    }

    float powerRatio = targetPowerW / maxPossiblePowerW;
    if (powerRatio < 0.0f) powerRatio = 0.0f;
    if (powerRatio > 1.0f) powerRatio = 1.0f;

    const uint16_t delayUs = static_cast<uint16_t>(
        10000.0f * acosf(2.0f * powerRatio - 1.0f) / PI);

    if (delayUs > TRIAC_MAX_ALPHA_US) return TRIAC_MAX_ALPHA_US;
    if (delayUs < TRIAC_MIN_ALPHA_US) return TRIAC_MIN_ALPHA_US;
    return delayUs;
}
#endif

void setOverride(int8_t percent) {
    if (percent >= 0 && percent <= 100) {
        overridePower = percent;
        LOG_I("WattControl: Override set to %d%%", percent);
        return;
    }

    overridePower = -1;
    LOG_I("WattControl: Override disabled");
}

bool isOverrideActive() {
    return overridePower >= 0;
}

void handleFlood() {
    const uint32_t now = millis();
    if (now - lastFloodTime < 5000UL) {
        return;
    }

    lastFloodTime = now;
    floodCount++;

    const uint8_t currentPower = Heater::getPower();
    uint8_t newPower = static_cast<uint8_t>(currentPower * 0.85f);
    if (newPower < 30) newPower = 30;

    Heater::setPower(newPower);
    powerReduction += 15;
    if (powerReduction > 50) powerReduction = 50;

    LOG_E("WattControl: FLOOD detected! Power %d%% -> %d%% (reduction: %d%%)",
          currentPower, newPower, powerReduction);

    floodPauseUntil = now + 30000UL;

    if (floodCount > 3) {
        powerReduction += 10;
        if (powerReduction > 50) powerReduction = 50;
        LOG_E("WattControl: Frequent floods (%d), extra reduction: %d%%",
              floodCount, powerReduction);
    }
}

uint8_t getPressureStatus(float pressure) {
    if (pressure >= critThreshold) return 2;
    if (pressure >= warnThreshold) return 1;
    return 0;
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

static DecrementState state;
static ControlV2::IndicatorRuntimeStateV2 indicatorRuntime;

namespace {

float clampUnit(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

bool hasAdaptiveContext(const ControlV2::ProcessIndicatorsV2& indicators) {
    if (!indicators.adaptiveControlAllowed) {
        return false;
    }

    return indicators.takeoffConfidence >= 0.0f ||
           indicators.bodyEndConfidence >= 0.0f ||
           indicators.headsEndConfidence >= 0.0f ||
           indicators.stabilityIndex > 0.0f ||
           indicators.floodRisk > 0.0f ||
           fabsf(indicators.coolingMarginC) > 0.01f;
}

float coolingPenalty(const ControlV2::ProcessIndicatorsV2& indicators) {
    if (indicators.coolingMarginC >= 5.0f) return 0.0f;
    if (indicators.coolingMarginC <= 0.0f) return 1.0f;
    return clampUnit((5.0f - indicators.coolingMarginC) / 5.0f);
}

float adaptiveTriggerDelta(const ControlV2::ProcessIndicatorsV2& indicators) {
    float delta = DECREMENT_TRIGGER_DELTA;
    delta += clampUnit(indicators.stabilityIndex) * 0.02f;
    delta -= clampUnit(indicators.floodRisk) * 0.05f;
    delta -= coolingPenalty(indicators) * 0.02f;
    return constrain(delta, 0.08f, 0.22f);
}

float adaptiveResumeDelta(const ControlV2::ProcessIndicatorsV2& indicators) {
    float delta = DECREMENT_RESUME_DELTA;
    delta += clampUnit(indicators.stabilityIndex) * 0.01f;
    delta -= clampUnit(indicators.floodRisk) * 0.03f;
    delta -= coolingPenalty(indicators) * 0.01f;
    return constrain(delta, 0.04f, 0.12f);
}

float adaptiveSpeedMultiplier(const ControlV2::ProcessIndicatorsV2& indicators,
                              uint32_t decrementCount) {
    float multiplier = DECREMENT_SPEED_MULT;
    multiplier += clampUnit(indicators.stabilityIndex) * 0.04f;
    multiplier -= clampUnit(indicators.floodRisk) * 0.10f;
    multiplier -= clampUnit(indicators.bodyEndConfidence) * 0.06f;
    multiplier -= fminf(static_cast<float>(decrementCount), 4.0f) * 0.03f;
    return constrain(multiplier, 0.68f, 0.92f);
}

uint32_t adaptiveWaitTimeoutMs(const ControlV2::ProcessIndicatorsV2& indicators,
                               uint32_t decrementCount) {
    float timeoutFactor = 1.0f;
    timeoutFactor -= clampUnit(indicators.floodRisk) * 0.25f;
    timeoutFactor -= coolingPenalty(indicators) * 0.15f;
    timeoutFactor -= clampUnit(indicators.bodyEndConfidence) * 0.20f;
    timeoutFactor -= fminf(static_cast<float>(decrementCount), 4.0f) * 0.08f;
    timeoutFactor = constrain(timeoutFactor, 0.35f, 1.0f);
    return static_cast<uint32_t>(DECREMENT_WAIT_MAX_SEC * 1000UL * timeoutFactor);
}

bool shouldForceTailsTransition(const ControlV2::ProcessIndicatorsV2& indicators,
                                uint32_t decrementCount) {
    const float bodyEndConfidence = clampUnit(indicators.bodyEndConfidence);
    if (bodyEndConfidence >= 0.92f) {
        return true;
    }
    if (decrementCount >= 2 && bodyEndConfidence >= 0.82f) {
        return true;
    }
    return decrementCount >= 4 && indicators.floodRisk >= 0.80f;
}

bool canResumeAdaptive(float currentTemp, float baseTemp,
                       const ControlV2::ProcessIndicatorsV2& indicators) {
    if (currentTemp >= baseTemp + adaptiveResumeDelta(indicators)) {
        return false;
    }
    if (!hasAdaptiveContext(indicators)) {
        return canResume(currentTemp, baseTemp);
    }

    const bool coolingOk = indicators.coolingMarginC > 0.0f;
    const bool floodOk = indicators.floodRisk < 0.72f;
    const bool stableEnough =
        indicators.takeoffAllowed || indicators.stabilityIndex >= 0.55f;
    return coolingOk && floodOk && stableEnough;
}

} // namespace

void init(float baseTemp) {
    state.active = false;
    state.baseTemp = baseTemp;
    state.decrementCount = 0;
    state.waitStart = 0;
    indicatorRuntime = ControlV2::IndicatorRuntimeStateV2{};

    LOG_I("SmartDecrement: Init, T_base=%.2f°C", baseTemp);
}

bool update(SystemState& sysState, const Settings& settings) {
    if (!sysState.temps.valid[TEMP_COLUMN_TOP]) {
        return false;
    }

    const float currentTemp = sysState.temps.columnTop;
    const ControlV2::ProcessIndicatorsV2 indicators =
        ControlV2::ProcessIndicatorsEngineV2::evaluate(
            sysState, settings, indicatorRuntime);
    const bool adaptiveMode = hasAdaptiveContext(indicators);
    const float triggerDelta =
        adaptiveMode ? adaptiveTriggerDelta(indicators) : DECREMENT_TRIGGER_DELTA;
    const float speedMultiplier =
        adaptiveMode ? adaptiveSpeedMultiplier(indicators, state.decrementCount)
                     : DECREMENT_SPEED_MULT;
    const uint32_t waitTimeoutMs =
        adaptiveMode ? adaptiveWaitTimeoutMs(indicators, state.decrementCount)
                     : (DECREMENT_WAIT_MAX_SEC * 1000UL);

    if (!state.active && currentTemp > state.baseTemp + triggerDelta) {
        LOG_I("SmartDecrement: Triggered! T_column=%.2f°C, delta=%.3f°C, stability=%.2f, flood=%.2f, cooling=%.1f",
              currentTemp, triggerDelta, indicators.stabilityIndex,
              indicators.floodRisk, indicators.coolingMarginC);

        Pump::stop();
        state.active = true;
        state.waitStart = millis();
        return false;
    }

    if (!state.active) {
        return false;
    }

    const uint32_t elapsed = millis() - state.waitStart;
    if (adaptiveMode &&
        shouldForceTailsTransition(indicators, state.decrementCount)) {
        LOG_I("SmartDecrement: Adaptive tails transition, bodyEndConfidence=%.2f, flood=%.2f, count=%lu",
              indicators.bodyEndConfidence, indicators.floodRisk,
              static_cast<unsigned long>(state.decrementCount));
        return true;
    }

    if (elapsed > waitTimeoutMs) {
        LOG_E("SmartDecrement: Timeout! Transition to TAILS");
        return true;
    }

    const bool resumeAllowed =
        (adaptiveMode &&
         canResumeAdaptive(currentTemp, state.baseTemp, indicators)) ||
        (!adaptiveMode && canResume(currentTemp, state.baseTemp));
    if (!resumeAllowed) {
        return false;
    }

    LOG_I("SmartDecrement: Resume! T_column=%.2f°C", currentTemp);

    const float currentSpeed = Pump::getSpeed();
    const float newSpeed = currentSpeed * speedMultiplier;
    const float minSpeed =
        DECREMENT_MIN_SPEED_ML_H_KW * (settings.equipment.heaterPowerW / 1000.0f);
    if (newSpeed < minSpeed) {
        LOG_I("SmartDecrement: Speed too low, transition to TAILS");
        return true;
    }

    Pump::start(newSpeed);
    state.active = false;
    state.decrementCount++;
    sysState.stats.decrementCount = state.decrementCount;

    LOG_I("SmartDecrement: Speed %.0f -> %.0f ml/h (count: %d, mult=%.2f, bodyEnd=%.2f)",
          currentSpeed, newSpeed, state.decrementCount, speedMultiplier,
          indicators.bodyEndConfidence);

    return false;
}

bool shouldDecrement(float currentTemp, float baseTemp) {
    return currentTemp > baseTemp + DECREMENT_TRIGGER_DELTA;
}

bool canResume(float currentTemp, float baseTemp) {
    return currentTemp < baseTemp + DECREMENT_RESUME_DELTA;
}

const DecrementState& getState() {
    return state;
}

void reset() {
    state.active = false;
    state.decrementCount = 0;
    state.waitStart = 0;
    indicatorRuntime = ControlV2::IndicatorRuntimeStateV2{};
    LOG_I("SmartDecrement: Reset");
}

} // namespace SmartDecrement
