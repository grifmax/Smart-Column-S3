#include "safety_supervisor.h"

#include <string.h>

#include "../../drivers/heater.h"
#include "../safety.h"
#include "safety_policy.h"

namespace ControlV2 {

namespace {

struct AntiOscillationRuntimeV2 {
    bool initialized = false;
    Mode lastMode = Mode::IDLE;
    uint16_t lastPhaseId = kNoPhaseIdV2;
    bool lastPowerCapped = false;
    bool lastPumpCapped = false;
    bool lastTakeoffBlocked = false;
    bool lastPhaseAdvanceBlocked = false;
    uint32_t lastLimitFlipMs = 0;
    uint8_t limitFlipScore = 0;
    uint32_t lastPhaseChangeMs = 0;
    uint8_t rapidPhaseChangeCount = 0;
    uint32_t guardUntilMs = 0;
    ActiveLimitsV2 stickyLimits;
};

AntiOscillationRuntimeV2 g_antiOscillationRuntime;
ActiveLimitsV2 g_liveLimits;

ReasonCodeV2 mapAlarmToReason(const SystemState& state) {
    switch (state.currentAlarm.type) {
        case AlarmType::COLUMN_FLOOD:
        case AlarmType::PRESSURE_RISE_RATE:
            return ReasonCodeV2::RC_SAFETY_TRIP_PRESSURE;
        case AlarmType::SENSOR_FAILURE:
            return ReasonCodeV2::RC_SAFETY_TRIP_SENSOR;
        case AlarmType::POWER_FAILURE:
            return ReasonCodeV2::RC_SAFETY_TRIP_POWER;
        case AlarmType::VAPOR_BREAKTHROUGH:
        case AlarmType::WATER_OVERHEAT:
        case AlarmType::WATER_RISE_RATE:
        case AlarmType::OVERHEAT:
        case AlarmType::LOW_WATER:
        case AlarmType::EMERGENCY_STOP:
            return ReasonCodeV2::RC_SAFETY_TRIP_OVERHEAT;
        default:
            return ReasonCodeV2::RC_SAFETY_TRIP_GENERIC;
    }
}

SafetyEventTypeV2 mapAlarmToEvent(const SystemState& state) {
    switch (state.currentAlarm.type) {
        case AlarmType::COLUMN_FLOOD: return SafetyEventTypeV2::PRESSURE_HIGH;
        case AlarmType::PRESSURE_RISE_RATE: return SafetyEventTypeV2::PRESSURE_RISE_FAST;
        case AlarmType::SENSOR_FAILURE: return SafetyEventTypeV2::SENSOR_FAILURE;
        case AlarmType::POWER_FAILURE: return SafetyEventTypeV2::POWER_FAILURE;
        case AlarmType::VAPOR_BREAKTHROUGH:
        case AlarmType::OVERHEAT:
            return SafetyEventTypeV2::OVERHEAT;
        case AlarmType::WATER_OVERHEAT:
        case AlarmType::WATER_RISE_RATE:
            return SafetyEventTypeV2::COOLING_OVERHEAT;
        case AlarmType::LOW_WATER:
        case AlarmType::EMERGENCY_STOP:
            return SafetyEventTypeV2::EMERGENCY_STOP;
        default:
            return SafetyEventTypeV2::NONE;
    }
}

bool isColumnMode(const Mode mode) {
    return mode == Mode::RECTIFICATION ||
           mode == Mode::MANUAL_RECT ||
           mode == Mode::DISTILLATION;
}

uint16_t getCurrentPhaseId(const SystemState& state) {
    switch (state.mode) {
        case Mode::NBK:
            return static_cast<uint16_t>(state.nbkPhase);
        case Mode::FERMENTATION:
            return static_cast<uint16_t>(state.fermPhase);
        case Mode::MASHING:
            return static_cast<uint16_t>(state.mashing.phase);
        case Mode::HOLD:
            return static_cast<uint16_t>(state.hold.currentStep);
        case Mode::RECTIFICATION:
        case Mode::DISTILLATION:
        case Mode::MANUAL_RECT:
        default:
            return static_cast<uint16_t>(state.rectPhase);
    }
}

bool isJitteryContext(const SystemState& state,
                      const ProcessIndicatorsV2& indicators) {
    if (!indicators.sensorFreshnessOk || !indicators.pressureStable) {
        return true;
    }
    if (indicators.stabilityIndex < 0.45f || indicators.floodRisk >= 0.60f) {
        return true;
    }
    return isColumnMode(state.mode) && indicators.coolingMarginC <= 0.0f;
}

uint8_t countLimitFlips(const ActiveLimitsV2& limits,
                        const AntiOscillationRuntimeV2& runtime) {
    uint8_t flips = 0;
    if (limits.powerCapped != runtime.lastPowerCapped) ++flips;
    if (limits.pumpCapped != runtime.lastPumpCapped) ++flips;
    if (limits.takeoffBlocked != runtime.lastTakeoffBlocked) ++flips;
    if (limits.phaseAdvanceBlocked != runtime.lastPhaseAdvanceBlocked) ++flips;
    return flips;
}

uint16_t getGuardHoldSec(const uint32_t nowMs) {
    if (g_antiOscillationRuntime.guardUntilMs <= nowMs) {
        return 0;
    }
    const uint32_t remainingMs = g_antiOscillationRuntime.guardUntilMs - nowMs;
    return static_cast<uint16_t>((remainingMs + 999UL) / 1000UL);
}

void rememberObservedState(const SystemState& state,
                           const ActiveLimitsV2& limits,
                           const uint16_t phaseId) {
    g_antiOscillationRuntime.initialized = true;
    g_antiOscillationRuntime.lastMode = state.mode;
    g_antiOscillationRuntime.lastPhaseId = phaseId;
    g_antiOscillationRuntime.lastPowerCapped = limits.powerCapped;
    g_antiOscillationRuntime.lastPumpCapped = limits.pumpCapped;
    g_antiOscillationRuntime.lastTakeoffBlocked = limits.takeoffBlocked;
    g_antiOscillationRuntime.lastPhaseAdvanceBlocked = limits.phaseAdvanceBlocked;
}

void resetRuntime(const SystemState& state,
                  const ActiveLimitsV2& limits,
                  const uint16_t phaseId) {
    g_antiOscillationRuntime = AntiOscillationRuntimeV2{};
    rememberObservedState(state, limits, phaseId);
}

void captureStickyLimits(const SystemState& state,
                         const ActiveLimitsV2& rawLimits) {
    ActiveLimitsV2 sticky = rawLimits;
    sticky.phaseAdvanceBlocked = true;
    sticky.antiOscillationActive = true;
    sticky.antiOscillationHoldSec = 0;

    const uint8_t currentHeaterPower = Heater::getPower();
    if (currentHeaterPower > 0) {
        sticky.powerCapped = true;
        const uint8_t rawCap = rawLimits.powerCapped ? rawLimits.maxHeaterPowerPercent : 100;
        sticky.maxHeaterPowerPercent = rawCap < currentHeaterPower ? rawCap : currentHeaterPower;
    }

    if (isColumnMode(state.mode)) {
        sticky.takeoffBlocked = true;
    }

    if (state.mode == Mode::NBK) {
        sticky.pumpCapped = true;
        sticky.maxPumpSpeedMlH = state.pump.speedMlPerHour;
        if (rawLimits.pumpCapped &&
            rawLimits.maxPumpSpeedMlH > 0.0f &&
            (sticky.maxPumpSpeedMlH <= 0.0f || rawLimits.maxPumpSpeedMlH < sticky.maxPumpSpeedMlH)) {
            sticky.maxPumpSpeedMlH = rawLimits.maxPumpSpeedMlH;
        }
    }

    g_antiOscillationRuntime.stickyLimits = sticky;
}

void updateAntiOscillation(const SystemState& state,
                           const ProcessIndicatorsV2& indicators,
                           const ActiveLimitsV2& rawLimits,
                           const uint32_t nowMs) {
    const uint16_t phaseId = getCurrentPhaseId(state);
    if (state.mode == Mode::IDLE || state.paused) {
        resetRuntime(state, rawLimits, phaseId);
        return;
    }

    if (!g_antiOscillationRuntime.initialized ||
        g_antiOscillationRuntime.lastMode != state.mode) {
        resetRuntime(state, rawLimits, phaseId);
        return;
    }

    const bool jittery = isJitteryContext(state, indicators);
    const uint8_t flips = countLimitFlips(rawLimits, g_antiOscillationRuntime);
    if (flips > 0) {
        if (!jittery ||
            nowMs - g_antiOscillationRuntime.lastLimitFlipMs > V2_ANTI_OSC_GUARD_WINDOW_MS) {
            g_antiOscillationRuntime.limitFlipScore = flips;
        } else {
            const uint16_t boostedScore =
                static_cast<uint16_t>(g_antiOscillationRuntime.limitFlipScore) + flips;
            g_antiOscillationRuntime.limitFlipScore =
                boostedScore > 255U ? 255U : static_cast<uint8_t>(boostedScore);
        }
        g_antiOscillationRuntime.lastLimitFlipMs = nowMs;
    } else if (nowMs - g_antiOscillationRuntime.lastLimitFlipMs > V2_ANTI_OSC_GUARD_WINDOW_MS) {
        g_antiOscillationRuntime.limitFlipScore = 0;
    }

    if (phaseId != g_antiOscillationRuntime.lastPhaseId) {
        if (jittery &&
            nowMs - g_antiOscillationRuntime.lastPhaseChangeMs <= V2_ANTI_OSC_GUARD_WINDOW_MS) {
            ++g_antiOscillationRuntime.rapidPhaseChangeCount;
        } else {
            g_antiOscillationRuntime.rapidPhaseChangeCount = 1;
        }
        g_antiOscillationRuntime.lastPhaseChangeMs = nowMs;
    } else if (nowMs - g_antiOscillationRuntime.lastPhaseChangeMs > V2_ANTI_OSC_GUARD_WINDOW_MS) {
        g_antiOscillationRuntime.rapidPhaseChangeCount = 0;
    }

    if (jittery &&
        (g_antiOscillationRuntime.limitFlipScore >= V2_ANTI_OSC_LIMIT_FLIP_THRESHOLD ||
         g_antiOscillationRuntime.rapidPhaseChangeCount >= V2_ANTI_OSC_PHASE_FLIP_THRESHOLD)) {
        g_antiOscillationRuntime.guardUntilMs = nowMs + V2_ANTI_OSC_GUARD_HOLD_MS;
        captureStickyLimits(state, rawLimits);
        g_antiOscillationRuntime.limitFlipScore = 0;
        g_antiOscillationRuntime.rapidPhaseChangeCount = 0;
    }

    rememberObservedState(state, rawLimits, phaseId);
}

ActiveLimitsV2 applyGuardedLimits(const ActiveLimitsV2& rawLimits,
                                  const uint32_t nowMs) {
    ActiveLimitsV2 guarded = rawLimits;
    if (g_antiOscillationRuntime.guardUntilMs <= nowMs) {
        guarded.antiOscillationActive = false;
        guarded.antiOscillationHoldSec = 0;
        return guarded;
    }

    const ActiveLimitsV2& sticky = g_antiOscillationRuntime.stickyLimits;
    guarded.powerCapped = guarded.powerCapped || sticky.powerCapped;
    if (guarded.powerCapped) {
        const uint8_t rawCap = rawLimits.powerCapped ? rawLimits.maxHeaterPowerPercent : 100;
        const uint8_t stickyCap = sticky.powerCapped ? sticky.maxHeaterPowerPercent : 100;
        guarded.maxHeaterPowerPercent = rawCap < stickyCap ? rawCap : stickyCap;
    }

    guarded.pumpCapped = guarded.pumpCapped || sticky.pumpCapped;
    if (guarded.pumpCapped) {
        if (rawLimits.pumpCapped && sticky.pumpCapped &&
            rawLimits.maxPumpSpeedMlH > 0.0f && sticky.maxPumpSpeedMlH > 0.0f) {
            guarded.maxPumpSpeedMlH =
                rawLimits.maxPumpSpeedMlH < sticky.maxPumpSpeedMlH
                    ? rawLimits.maxPumpSpeedMlH
                    : sticky.maxPumpSpeedMlH;
        } else if (sticky.pumpCapped) {
            guarded.maxPumpSpeedMlH = sticky.maxPumpSpeedMlH;
        }
    }

    guarded.takeoffBlocked = guarded.takeoffBlocked || sticky.takeoffBlocked;
    guarded.phaseAdvanceBlocked = true;
    guarded.antiOscillationActive = true;
    guarded.antiOscillationHoldSec = getGuardHoldSec(nowMs);
    return guarded;
}

} // namespace

ActiveLimitsV2 SafetySupervisorV2::evaluateActiveLimits(const SystemState& state,
                                                        const Settings& settings,
                                                        const ProcessIndicatorsV2& indicators) {
    ActiveLimitsV2 rawLimits;
    rawLimits.powerCapped = indicators.powerLimited;
    if (state.mode == Mode::NBK) {
        const HeaterPowerPolicyV2 powerPolicy = SafetyPolicyV2::evaluateNbkHeaterPower(
            SafetyPolicyV2::getDefaultNbkHeaterPowerPercent(state, settings), state, settings);
        rawLimits.maxHeaterPowerPercent = powerPolicy.limited ? powerPolicy.appliedPowerPercent : 100;
    } else if (state.mode == Mode::MANUAL_RECT &&
               SafetyPolicyV2::isManualRectFloodPowerLimitActive(state, settings)) {
        rawLimits.maxHeaterPowerPercent = Heater::getPower();
    } else {
        rawLimits.maxHeaterPowerPercent = indicators.powerLimited ? Heater::getPower() : 100;
    }
    rawLimits.pumpCapped = (state.mode == Mode::NBK) && !indicators.nbkFeedAllowed;
    rawLimits.maxPumpSpeedMlH = rawLimits.pumpCapped ? state.pump.speedMlPerHour : 0.0f;
    rawLimits.takeoffBlocked =
        (state.mode == Mode::RECTIFICATION || state.mode == Mode::MANUAL_RECT) &&
        !indicators.takeoffAllowed;
    rawLimits.phaseAdvanceBlocked = !indicators.sensorFreshnessOk || indicators.floodRisk >= 0.80f;

    const uint32_t nowMs = millis();
    updateAntiOscillation(state, indicators, rawLimits, nowMs);
    g_liveLimits = applyGuardedLimits(rawLimits, nowMs);
    return g_liveLimits;
}

SafetyDecisionV2 SafetySupervisorV2::evaluateDecision(const SystemState& state,
                                                      const Settings& settings,
                                                      const ProcessIndicatorsV2& indicators,
                                                      const ActiveLimitsV2& limits) {
    SafetyDecisionV2 decision;
    decision.limits = limits;

    if (Safety::isLatched(state)) {
        char recoveryReason[96] = "";
        const bool recoveryReady = Safety::canResetNow(state, settings, recoveryReason, sizeof(recoveryReason));
        decision.severity = recoveryReady ? SafetySeverityV2::RECOVERY : SafetySeverityV2::LATCHED_TRIP;
        decision.primaryEvent = mapAlarmToEvent(state);
        decision.reasonCode =
            recoveryReady ? ReasonCodeV2::RC_SAFETY_RECOVERY_ENTERED : mapAlarmToReason(state);
        decision.requiresAcknowledge = !state.currentAlarm.acknowledged;
        if (recoveryReady) {
            const char* message = state.currentAlarm.acknowledged
                                      ? "Safety conditions recovered, reset is available"
                                      : "Safety conditions recovered, acknowledge and reset are available";
            strncpy(decision.message, message, sizeof(decision.message) - 1);
            decision.message[sizeof(decision.message) - 1] = '\0';
        } else {
            strncpy(decision.message,
                    recoveryReason[0] != '\0' ? recoveryReason : state.currentAlarm.message,
                    sizeof(decision.message) - 1);
            decision.message[sizeof(decision.message) - 1] = '\0';
        }
        return decision;
    }

    if (limits.antiOscillationActive) {
        decision.severity = SafetySeverityV2::LIMITED;
        decision.primaryEvent = SafetyEventTypeV2::ANTI_OSCILLATION_GUARD;
        decision.reasonCode = ReasonCodeV2::RC_SAFETY_ANTI_OSCILLATION_GUARD;
        strncpy(decision.message,
                "Anti-oscillation guard is holding power, pump or phase changes until indicators stabilize",
                sizeof(decision.message) - 1);
        decision.message[sizeof(decision.message) - 1] = '\0';
        return decision;
    }

    if (limits.powerCapped || limits.takeoffBlocked || limits.phaseAdvanceBlocked) {
        decision.severity = SafetySeverityV2::LIMITED;
        decision.primaryEvent = limits.powerCapped ? SafetyEventTypeV2::POWER_LIMIT_APPLIED
                                                   : (limits.takeoffBlocked ? SafetyEventTypeV2::TAKEOFF_LIMIT_APPLIED
                                                                            : SafetyEventTypeV2::PHASE_ADVANCE_BLOCKED);
        decision.reasonCode = limits.powerCapped ? ReasonCodeV2::RC_SAFETY_LIMIT_POWER
                                                 : (limits.takeoffBlocked ? ReasonCodeV2::RC_SAFETY_LIMIT_TAKEOFF
                                                                          : ReasonCodeV2::RC_SAFETY_PHASE_BLOCKED);
        const char* message = limits.powerCapped
                                  ? "Power is capped by safety margin"
                                  : (limits.takeoffBlocked ? "Takeoff is blocked by process limits"
                                                           : "Phase advance is blocked by sensor or flood constraints");
        strncpy(decision.message, message, sizeof(decision.message) - 1);
        decision.message[sizeof(decision.message) - 1] = '\0';
        return decision;
    }

    if (!indicators.sensorFreshnessOk) {
        decision.severity = SafetySeverityV2::WARNING;
        decision.primaryEvent = SafetyEventTypeV2::SENSOR_STALE;
        decision.reasonCode = ReasonCodeV2::RC_PRECHECK_FAIL_SENSOR;
        strncpy(decision.message, "Sensor data is stale", sizeof(decision.message) - 1);
        decision.message[sizeof(decision.message) - 1] = '\0';
    }

    return decision;
}

void SafetySupervisorV2::applyDecisionToIndicators(const SafetyDecisionV2& decision,
                                                   ProcessIndicatorsV2& indicators) {
    indicators.recoveryActive = decision.severity == SafetySeverityV2::RECOVERY;
}

const ActiveLimitsV2& SafetySupervisorV2::getLiveLimits() {
    return g_liveLimits;
}

} // namespace ControlV2
