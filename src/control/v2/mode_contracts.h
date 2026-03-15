#ifndef CONTROL_V2_MODE_CONTRACTS_H
#define CONTROL_V2_MODE_CONTRACTS_H

#include <Arduino.h>

#include "../../config.h"
#include "../../types.h"
#include "process_indicators.h"
#include "reason_codes.h"

namespace ControlV2 {

enum class ModeLifecycleV2 : uint8_t {
    IDLE = 0,
    STARTING,
    RUNNING,
    PAUSED,
    STOPPING,
    COMPLETED,
    FAULTED
};

inline const char* modeLifecycleToString(ModeLifecycleV2 lifecycle) {
    switch (lifecycle) {
        case ModeLifecycleV2::IDLE: return "idle";
        case ModeLifecycleV2::STARTING: return "starting";
        case ModeLifecycleV2::RUNNING: return "running";
        case ModeLifecycleV2::PAUSED: return "paused";
        case ModeLifecycleV2::STOPPING: return "stopping";
        case ModeLifecycleV2::COMPLETED: return "completed";
        case ModeLifecycleV2::FAULTED: return "faulted";
        default: return "unknown";
    }
}

struct ActiveLimitsV2 {
    bool powerCapped = false;
    uint8_t maxHeaterPowerPercent = 100;
    bool pumpCapped = false;
    float maxPumpSpeedMlH = 0.0f;
    bool takeoffBlocked = false;
    bool phaseAdvanceBlocked = false;
};

struct CommandTargetsV2 {
    uint8_t heaterPowerPercent = 0;
    float pumpSpeedMlH = 0.0f;
    bool waterValveOpen = false;
    bool headsValveOpen = false;
    bool stopRequested = false;
};

struct SafetyDecisionV2 {
    SafetySeverityV2 severity = SafetySeverityV2::NONE;
    SafetyEventTypeV2 primaryEvent = SafetyEventTypeV2::NONE;
    ActiveLimitsV2 limits;
    ReasonCodeV2 reasonCode = ReasonCodeV2::NONE;
    bool requiresAcknowledge = false;
    char message[96] = "";
};

struct MetricsSnapshotV2 {
    uint32_t timestampMs = 0;
    TemperatureData temperatures;
    PressureData pressure;
    PowerData power;
    PumpState pump;
    ProcessIndicatorsV2 indicators;
    SafetyDecisionV2 safety;
};

struct ModeStatusV2 {
    Mode mode = Mode::IDLE;
    ModeLifecycleV2 lifecycle = ModeLifecycleV2::IDLE;
    uint16_t phaseId = 0;
    char phaseToken[32] = "";
    uint32_t phaseStartMs = 0;
    uint32_t phaseElapsedSec = 0;
    uint32_t modeStartMs = 0;
    uint32_t modeElapsedSec = 0;
    bool paused = false;
    bool safetyLatched = false;
    ActiveLimitsV2 activeLimits;
    CommandTargetsV2 commandTargets;
    ProcessIndicatorsV2 indicators;
    ReasonCodeV2 lastReasonCode = ReasonCodeV2::NONE;
    char operatorMessage[96] = "";
};

struct ModeContextV2 {
    SystemState* state = nullptr;
    const Settings* settings = nullptr;
    uint32_t nowMs = 0;
    MetricsSnapshotV2 metrics;
    SafetyDecisionV2 safety;
};

class IModeControllerV2 {
public:
    virtual ~IModeControllerV2() = default;

    virtual const char* getModeToken() const = 0;

    virtual void init(SystemState& state, const Settings& settings) = 0;
    virtual void start(SystemState& state, const Settings& settings) = 0;
    virtual void update(ModeContextV2& context) = 0;
    virtual void pause(SystemState& state) = 0;
    virtual void resume(SystemState& state) = 0;
    virtual void stop(SystemState& state) = 0;

    virtual ModeStatusV2 getStatus() const = 0;
    virtual MetricsSnapshotV2 getMetricsSnapshot() const = 0;
    virtual const char* getCurrentPhaseToken() const = 0;
    virtual ReasonCodeV2 getLastReasonCode() const = 0;
};

} // namespace ControlV2

#endif // CONTROL_V2_MODE_CONTRACTS_H
