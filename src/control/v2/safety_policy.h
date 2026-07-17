#ifndef CONTROL_V2_SAFETY_POLICY_H
#define CONTROL_V2_SAFETY_POLICY_H

#include "../../config.h"
#include "../../types.h"

namespace ControlV2 {

struct HeaterPowerPolicyV2 {
    uint8_t requestedPowerPercent = 0;
    uint8_t appliedPowerPercent = 0;
    bool limited = false;
};

struct ManualRectFloodPolicyV2 {
    float floodPressureMmHg = 0.0f;
    float criticalPressureMmHg = 0.0f;
    uint8_t requestedPowerPercent = 0;
    uint8_t appliedPowerPercent = 0;
    bool limitActive = false;
    bool cooldownActive = false;
    bool stepdownRecommended = false;
};

struct RectificationPressureRuntimeV2 {
    bool initialized = false;
    uint8_t stableSamples = 0;
    uint32_t lastSampleMs = 0;
    float lastPressureMmHg = 0.0f;
    bool fallbackActive = false;
    bool emergencyReported = false;
};

struct RectificationPressurePolicyV2 {
    bool enabled = false;
    bool calibrationValid = false;
    bool signalValid = false;
    bool signalStable = false;
    bool fallbackActive = false;
    bool limited = false;
    bool emergencyStop = false;
    float pressureMmHg = 0.0f;
    float workCorridorMmHg = 0.0f;
    float safetyLimitMmHg = 0.0f;
    uint8_t requestedPowerPercent = 0;
    uint8_t appliedPowerPercent = 0;
};

class SafetyPolicyV2 {
public:
    static bool isNbkPressurePowerLimitActive(const SystemState& state, const Settings& settings);
    static HeaterPowerPolicyV2 evaluateNbkHeaterPower(uint8_t requestedPowerPercent,
                                                      const SystemState& state,
                                                      const Settings& settings);
    static uint8_t getDefaultNbkHeaterPowerPercent(const SystemState& state, const Settings& settings);
    static bool isManualRectFloodPowerLimitActive(const SystemState& state, const Settings& settings);
    static ManualRectFloodPolicyV2 evaluateManualRectFloodPower(uint8_t requestedPowerPercent,
                                                                const SystemState& state,
                                                                const Settings& settings,
                                                                uint32_t nowMs,
                                                                uint32_t lastFloodTimeMs);
    static bool isPressureCalibrationValid(const PressureSensorCalibration& calibration);
    static RectificationPressurePolicyV2 evaluateRectificationPressurePower(
        uint8_t requestedPowerPercent,
        const SystemState& state,
        const Settings& settings,
        uint32_t nowMs,
        RectificationPressureRuntimeV2& runtime);
};

} // namespace ControlV2

#endif // CONTROL_V2_SAFETY_POLICY_H
