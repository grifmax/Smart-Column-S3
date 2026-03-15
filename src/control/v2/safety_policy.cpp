#include "safety_policy.h"

#include "../fsm_utils.h"
#include "../watt_control.h"

namespace ControlV2 {

namespace {

constexpr uint32_t kManualRectFloodCooldownMs = 5000;

uint8_t clampPowerPercent(int value) {
    if (value < 0) return 0;
    if (value > 100) return 100;
    return static_cast<uint8_t>(value);
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

} // namespace ControlV2
