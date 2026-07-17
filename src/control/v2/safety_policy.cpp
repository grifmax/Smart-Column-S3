#include "safety_policy.h"

#include "../fsm_utils.h"
#include "../watt_control.h"

#include <math.h>

namespace ControlV2 {

namespace {

constexpr uint32_t kManualRectFloodCooldownMs = 5000;
constexpr uint8_t kPressureStableSampleCount = 3;
constexpr float kPressureSignalMinStepMmHg = 1.0f;

uint8_t clampPowerPercent(int value) {
    if (value < 0) return 0;
    if (value > 100) return 100;
    return static_cast<uint8_t>(value);
}

float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

bool isFreshPressureSignal(const SystemState& state, uint32_t nowMs) {
    if (!state.pressure.ok || state.pressure.lastUpdate == 0 ||
        nowMs < state.pressure.lastUpdate ||
        nowMs - state.pressure.lastUpdate > SAFETY_SENSOR_TIMEOUT_MS) {
        return false;
    }
    return isfinite(state.pressure.cube) && state.pressure.cube >= 0.0f &&
           state.pressure.cube <= 75.0f && isfinite(state.pressure.sensorVoltage) &&
           state.pressure.sensorVoltage >= -0.1f && state.pressure.sensorVoltage <= 5.1f;
}

} // namespace

bool SafetyPolicyV2::isNbkPressurePowerLimitActive(const SystemState& state, const Settings& settings) {
    return state.mode == Mode::NBK && state.pressure.ok &&
           state.pressure.cube > settings.safety.pressureMaxMmHg * 0.85f;
}

HeaterPowerPolicyV2 SafetyPolicyV2::evaluateNbkHeaterPower(uint8_t requestedPowerPercent,
                                                           const SystemState& state,
                                                           const Settings& settings) {
    HeaterPowerPolicyV2 result;
    result.requestedPowerPercent = requestedPowerPercent;
    result.appliedPowerPercent = requestedPowerPercent;

    if (!isNbkPressurePowerLimitActive(state, settings)) {
        return result;
    }

    result.limited = true;
    int limitedPower = static_cast<int>(requestedPowerPercent * 0.8f + 0.5f);
    if (limitedPower < 30) {
        limitedPower = 30;
    }
    result.appliedPowerPercent = clampPowerPercent(limitedPower);
    return result;
}

uint8_t SafetyPolicyV2::getDefaultNbkHeaterPowerPercent(const SystemState& state,
                                                        const Settings& settings) {
    return settings.equipment.heaterPowerW > 0 ? FSM::getProcessHeaterPower(state, settings, 70) : 70;
}

bool SafetyPolicyV2::isManualRectFloodPowerLimitActive(const SystemState& state, const Settings& settings) {
    const float floodPressure = WattControl::calculateFloodPressure(settings.equipment.columnHeightMm,
                                                                    settings.equipment.packingCoeff);
    const float criticalPressure = floodPressure * PRESSURE_CRIT_MULT;
    return state.mode == Mode::MANUAL_RECT && state.pressure.ok && state.pressure.cube >= criticalPressure;
}

ManualRectFloodPolicyV2 SafetyPolicyV2::evaluateManualRectFloodPower(uint8_t requestedPowerPercent,
                                                                     const SystemState& state,
                                                                     const Settings& settings,
                                                                     uint32_t nowMs,
                                                                     uint32_t lastFloodTimeMs) {
    ManualRectFloodPolicyV2 result;
    result.requestedPowerPercent = requestedPowerPercent;
    result.appliedPowerPercent = requestedPowerPercent;
    result.floodPressureMmHg = WattControl::calculateFloodPressure(settings.equipment.columnHeightMm,
                                                                   settings.equipment.packingCoeff);
    result.criticalPressureMmHg = result.floodPressureMmHg * PRESSURE_CRIT_MULT;
    result.limitActive = state.mode == Mode::MANUAL_RECT && state.pressure.ok &&
                         state.pressure.cube >= result.criticalPressureMmHg;

    if (!result.limitActive) {
        return result;
    }

    result.cooldownActive = nowMs >= lastFloodTimeMs &&
                            (nowMs - lastFloodTimeMs) <= kManualRectFloodCooldownMs;
    if (result.cooldownActive) {
        return result;
    }

    result.stepdownRecommended = true;
    int limitedPower = static_cast<int>(requestedPowerPercent * 0.85f);
    if (limitedPower < 30) {
        limitedPower = 30;
    }
    result.appliedPowerPercent = clampPowerPercent(limitedPower);
    return result;
}

bool SafetyPolicyV2::isPressureCalibrationValid(
    const PressureSensorCalibration& calibration) {
    if (calibration.pointCount < 2 || calibration.pointCount > 5) {
        return false;
    }

    for (uint8_t index = 0; index < calibration.pointCount; ++index) {
        if (!isfinite(calibration.voltagePoints[index]) ||
            !isfinite(calibration.pressurePoints[index])) {
            return false;
        }
        if (index > 0 &&
            (calibration.voltagePoints[index] <= calibration.voltagePoints[index - 1] ||
             calibration.pressurePoints[index] < calibration.pressurePoints[index - 1])) {
            return false;
        }
    }
    return true;
}

RectificationPressurePolicyV2 SafetyPolicyV2::evaluateRectificationPressurePower(
    uint8_t requestedPowerPercent,
    const SystemState& state,
    const Settings& settings,
    uint32_t nowMs,
    RectificationPressureRuntimeV2& runtime) {
    RectificationPressurePolicyV2 result;
    result.enabled = settings.rectParams.pressureControlEnabled;
    result.requestedPowerPercent = clampPowerPercent(requestedPowerPercent);
    result.appliedPowerPercent = result.requestedPowerPercent;
    if (!result.enabled) {
        runtime = RectificationPressureRuntimeV2{};
        return result;
    }

    result.calibrationValid = isPressureCalibrationValid(settings.pressureCal);
    result.signalValid = result.calibrationValid && isFreshPressureSignal(state, nowMs);
    if (!result.signalValid) {
        runtime.initialized = false;
        runtime.stableSamples = 0;
        runtime.lastSampleMs = 0;
        runtime.fallbackActive = true;
        result.fallbackActive = true;
        return result;
    }

    result.pressureMmHg = state.pressure.cube;
    const uint32_t sensorSampleMs = state.pressure.lastUpdate;
    if (!runtime.initialized || sensorSampleMs < runtime.lastSampleMs) {
        runtime.initialized = true;
        runtime.stableSamples = 1;
    } else if (sensorSampleMs > runtime.lastSampleMs) {
        const float elapsedMin = static_cast<float>(sensorSampleMs - runtime.lastSampleMs) / 60000.0f;
        const float maxStep = fmaxf(
            kPressureSignalMinStepMmHg,
            clampFloat(settings.safety.pressureRiseRateMmHgMin, 1.0f, 200.0f) *
                fmaxf(elapsedMin, 0.016f) * 1.5f);
        if (fabsf(state.pressure.cube - runtime.lastPressureMmHg) <= maxStep) {
            if (runtime.stableSamples < kPressureStableSampleCount) {
                ++runtime.stableSamples;
            }
        } else {
            runtime.stableSamples = 1;
        }
    }
    runtime.lastPressureMmHg = state.pressure.cube;
    runtime.lastSampleMs = sensorSampleMs;
    runtime.fallbackActive = false;
    result.signalStable = runtime.stableSamples >= kPressureStableSampleCount;
    if (!result.signalStable) {
        result.fallbackActive = true;
        runtime.fallbackActive = true;
        return result;
    }

    result.safetyLimitMmHg = clampFloat(settings.safety.pressureMaxMmHg, 5.0f, 200.0f);
    const float calculatedFloodPressure = WattControl::calculateFloodPressure(
        settings.equipment.columnHeightMm, settings.equipment.packingCoeff);
    result.workCorridorMmHg = calculatedFloodPressure * PRESSURE_WORK_MULT;
    if (!isfinite(result.workCorridorMmHg) || result.workCorridorMmHg <= 0.0f) {
        result.workCorridorMmHg = result.safetyLimitMmHg * 0.5f;
    }
    // Keep a real control corridor even when equipment values are inconsistent.
    if (result.workCorridorMmHg >= result.safetyLimitMmHg * 0.9f) {
        result.workCorridorMmHg = result.safetyLimitMmHg * 0.75f;
    }

    const uint8_t minPower = clampPowerPercent(settings.rectParams.pressureMinPowerPercent);
    if (result.pressureMmHg >= result.safetyLimitMmHg) {
        result.emergencyStop = true;
        result.appliedPowerPercent = 0;
        return result;
    }
    if (result.pressureMmHg <= result.workCorridorMmHg) {
        return result;
    }

    const float pressureSpan = result.safetyLimitMmHg - result.workCorridorMmHg;
    const float pressureRatio = pressureSpan > 0.0f
        ? clampFloat((result.pressureMmHg - result.workCorridorMmHg) / pressureSpan, 0.0f, 1.0f)
        : 1.0f;
    const float cap = 100.0f - pressureRatio * (100.0f - minPower);
    const uint8_t pressureCap = clampPowerPercent(static_cast<int>(cap + 0.5f));
    result.appliedPowerPercent = result.requestedPowerPercent < pressureCap
        ? result.requestedPowerPercent
        : pressureCap;
    result.limited = result.appliedPowerPercent < result.requestedPowerPercent;
    return result;
}

} // namespace ControlV2
