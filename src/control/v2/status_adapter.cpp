#include "status_adapter.h"

#include <string.h>

#include "../../drivers/heater.h"
#include "../../drivers/pump.h"
#include "../../drivers/valves.h"
#include "../../storage/logger.h"
#include "../fsm.h"
#include "../fsm_utils.h"
#include "safety_policy.h"
#include "transition_logger.h"

namespace ControlV2 {

namespace {

IndicatorRuntimeStateV2 g_indicatorRuntime;
ProcessIndicatorsV2 g_lastIndicators;
MetricsSnapshotV2 g_lastMetrics;
ModeStatusV2 g_lastStatus;
Mode g_prevMode = Mode::IDLE;
uint16_t g_prevPhaseId = 0;
uint32_t g_modeStartMs = 0;
bool g_initialized = false;

struct PendingPhaseTransitionV2 {
    bool active = false;
    Mode mode = Mode::IDLE;
    uint16_t fromPhaseId = 0;
    uint16_t toPhaseId = 0;
    ReasonCodeV2 reasonCode = ReasonCodeV2::NONE;
    char operatorMessage[96] = "";
};

PendingPhaseTransitionV2 g_pendingTransition;

uint16_t getActivePhaseId(const SystemState& state) {
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

const char* getPhaseToken(Mode mode, uint16_t phaseId) {
    switch (mode) {
        case Mode::RECTIFICATION:
        case Mode::DISTILLATION:
        case Mode::MANUAL_RECT: {
            switch (static_cast<RectPhase>(phaseId)) {
                case RectPhase::IDLE: return "idle";
                case RectPhase::HEATING: return "heating";
                case RectPhase::STABILIZATION: return "stabilization";
                case RectPhase::HEADS: return "heads";
                case RectPhase::POST_HEADS_STABILIZATION: return "post_heads_stabilization";
                case RectPhase::BODY: return "body";
                case RectPhase::TAILS: return "tails";
                case RectPhase::PURGE: return "purge";
                case RectPhase::FINISH: return "finish";
                case RectPhase::COMPLETED: return "completed";
                default: return "unknown";
            }
        }
        case Mode::NBK: {
            switch (static_cast<NbkPhase>(phaseId)) {
                case NbkPhase::IDLE: return "idle";
                case NbkPhase::HEATING: return "heating";
                case NbkPhase::STABILIZATION: return "stabilization";
                case NbkPhase::WORKING: return "working";
                case NbkPhase::FINISH: return "finish";
                case NbkPhase::COMPLETED: return "completed";
                default: return "unknown";
            }
        }
        case Mode::FERMENTATION: {
            switch (static_cast<FermentationPhase>(phaseId)) {
                case FermentationPhase::IDLE: return "idle";
                case FermentationPhase::RUNNING: return "running";
                case FermentationPhase::COMPLETED: return "completed";
                default: return "unknown";
            }
        }
        case Mode::MASHING:
            switch (static_cast<MashPhase>(phaseId)) {
                case MashPhase::IDLE: return "idle";
                case MashPhase::ACID_REST: return "acid_rest";
                case MashPhase::PROTEIN_REST: return "protein_rest";
                case MashPhase::BETA_AMYLASE: return "beta_amylase";
                case MashPhase::ALPHA_AMYLASE: return "alpha_amylase";
                case MashPhase::MASH_OUT: return "mash_out";
                case MashPhase::FINISH: return "finish";
                default: return "unknown";
            }
        case Mode::HOLD:
            return "hold_step";
        default:
            return "idle";
    }
}

ReasonCodeV2 mapAlarmToReason(const SystemState& state) {
    switch (state.currentAlarm.type) {
        case AlarmType::COLUMN_FLOOD:
        case AlarmType::PRESSURE_RISE_RATE:
            return ReasonCodeV2::RC_SAFETY_TRIP_PRESSURE;
        case AlarmType::SENSOR_FAILURE:
            return ReasonCodeV2::RC_SAFETY_TRIP_SENSOR;
        case AlarmType::VAPOR_BREAKTHROUGH:
        case AlarmType::WATER_OVERHEAT:
        case AlarmType::WATER_RISE_RATE:
        case AlarmType::OVERHEAT:
        case AlarmType::LOW_WATER:
        case AlarmType::EMERGENCY_STOP:
            return ReasonCodeV2::RC_SAFETY_TRIP_OVERHEAT;
        default:
            return ReasonCodeV2::RC_UNSPECIFIED;
    }
}

ReasonCodeV2 inferPhaseReason(const SystemState& state, uint16_t previousPhaseId,
                              uint16_t currentPhaseId, const ProcessIndicatorsV2& indicators) {
    switch (state.mode) {
        case Mode::RECTIFICATION:
            switch (static_cast<RectPhase>(currentPhaseId)) {
                case RectPhase::STABILIZATION: return ReasonCodeV2::RC_HEATING_COMPLETE;
                case RectPhase::HEADS:
                    return indicators.columnStable ? ReasonCodeV2::RC_STABILITY_WINDOW_REACHED
                                                   : ReasonCodeV2::RC_STABILIZATION_TIMER_OK;
                case RectPhase::POST_HEADS_STABILIZATION:
                    return indicators.headsCompletionScore >= 0.999f
                               ? ReasonCodeV2::RC_HEADS_SCORE_REACHED
                               : ReasonCodeV2::RC_HEADS_VOLUME_REACHED;
                case RectPhase::PURGE: return ReasonCodeV2::RC_POST_HEADS_STABILIZATION_COMPLETE;
                case RectPhase::BODY: return ReasonCodeV2::RC_PURGE_COMPLETE;
                case RectPhase::TAILS:
                    return indicators.bodyEndScore >= 0.999f
                               ? ReasonCodeV2::RC_BODY_END_DETECTED
                               : ReasonCodeV2::RC_BODY_TARGET_VOLUME_REACHED;
                case RectPhase::FINISH: return ReasonCodeV2::RC_TAILS_TARGET_REACHED;
                case RectPhase::IDLE:
                    return previousPhaseId == static_cast<uint16_t>(RectPhase::FINISH)
                               ? ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE
                               : ReasonCodeV2::RC_MODE_STOP_REQUEST;
                default: return ReasonCodeV2::RC_UNSPECIFIED;
            }
        case Mode::DISTILLATION:
            switch (static_cast<RectPhase>(currentPhaseId)) {
                case RectPhase::HEADS: return ReasonCodeV2::RC_HEATING_COMPLETE;
                case RectPhase::BODY:
                    return previousPhaseId == static_cast<uint16_t>(RectPhase::HEATING)
                               ? ReasonCodeV2::RC_DISTILLATION_HEADS_OPTIONAL_SKIPPED
                               : ReasonCodeV2::RC_HEADS_VOLUME_REACHED;
                case RectPhase::FINISH:
                    return indicators.distBodyNearEnd
                               ? ReasonCodeV2::RC_DISTILLATION_END_TEMP_REACHED
                               : ReasonCodeV2::RC_DISTILLATION_TARGET_VOLUME_REACHED;
                case RectPhase::IDLE:
                    return previousPhaseId == static_cast<uint16_t>(RectPhase::FINISH)
                               ? ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE
                               : ReasonCodeV2::RC_MODE_STOP_REQUEST;
                default: return ReasonCodeV2::RC_UNSPECIFIED;
            }
        case Mode::MANUAL_RECT:
            switch (static_cast<RectPhase>(currentPhaseId)) {
                case RectPhase::HEATING: return ReasonCodeV2::RC_MODE_START_REQUEST;
                case RectPhase::IDLE: return ReasonCodeV2::RC_MANUAL_OPERATOR_STOP;
                default: return ReasonCodeV2::RC_MANUAL_OPERATOR_SWITCH;
            }
        case Mode::NBK:
            switch (static_cast<NbkPhase>(currentPhaseId)) {
                case NbkPhase::STABILIZATION: return ReasonCodeV2::RC_NBK_STEAM_READY;
                case NbkPhase::WORKING: return ReasonCodeV2::RC_NBK_STABILIZATION_COMPLETE;
                case NbkPhase::FINISH: return ReasonCodeV2::RC_NBK_FINISH_LIKELY;
                case NbkPhase::COMPLETED: return ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE;
                default: return ReasonCodeV2::RC_UNSPECIFIED;
            }
        case Mode::MASHING:
        case Mode::HOLD:
            return ReasonCodeV2::RC_TEMP_STEP_REACHED;
        case Mode::FERMENTATION:
            return ReasonCodeV2::RC_FERM_TARGET_REACHED;
        default:
            return ReasonCodeV2::RC_UNSPECIFIED;
    }
}

void setStatusReason(ReasonCodeV2 reasonCode, const char* operatorMessage) {
    g_lastStatus.lastReasonCode = reasonCode;
    if (operatorMessage != nullptr && operatorMessage[0] != '\0') {
        strncpy(g_lastStatus.operatorMessage, operatorMessage, sizeof(g_lastStatus.operatorMessage) - 1);
        g_lastStatus.operatorMessage[sizeof(g_lastStatus.operatorMessage) - 1] = '\0';
    }
}

void logTransitionEvent(Mode mode, uint16_t fromPhaseId, uint16_t toPhaseId, ReasonCodeV2 reasonCode,
                        const char* operatorMessage, const ProcessIndicatorsV2& indicators,
                        const ActiveLimitsV2& limits, uint32_t now, float collectedVolumeMl) {
    PhaseTransitionEventV2 event;
    event.mode = mode;
    event.fromPhaseId = fromPhaseId;
    event.toPhaseId = toPhaseId;
    strncpy(event.fromPhaseToken, getPhaseToken(mode, fromPhaseId), sizeof(event.fromPhaseToken) - 1);
    strncpy(event.toPhaseToken, getPhaseToken(mode, toPhaseId), sizeof(event.toPhaseToken) - 1);
    event.fromPhaseToken[sizeof(event.fromPhaseToken) - 1] = '\0';
    event.toPhaseToken[sizeof(event.toPhaseToken) - 1] = '\0';
    event.reasonCode = reasonCode;
    event.timestampMs = now;
    event.phaseElapsedSec = FSM::getPhaseElapsedSec();
    event.modeElapsedSec = (g_modeStartMs > 0 && now >= g_modeStartMs) ? (now - g_modeStartMs) / 1000UL : 0;
    event.indicators = indicators;
    event.activeLimits = limits;
    event.collectedVolumeMl = collectedVolumeMl;
    if (operatorMessage != nullptr && operatorMessage[0] != '\0') {
        strncpy(event.operatorMessage, operatorMessage, sizeof(event.operatorMessage) - 1);
        event.operatorMessage[sizeof(event.operatorMessage) - 1] = '\0';
    }
    TransitionLoggerV2::logPhaseTransition(event);
}

bool consumeExplicitTransition(Mode previousMode, uint16_t previousPhaseId, Mode currentMode,
                               uint16_t currentPhaseId, PendingPhaseTransitionV2& transition) {
    if (!g_pendingTransition.active) {
        return false;
    }
    if (g_pendingTransition.mode != previousMode || g_pendingTransition.fromPhaseId != previousPhaseId) {
        return false;
    }
    if (currentMode == previousMode && g_pendingTransition.toPhaseId != currentPhaseId) {
        return false;
    }
    transition = g_pendingTransition;
    g_pendingTransition.active = false;
    return true;
}

ActiveLimitsV2 buildActiveLimits(const SystemState& state, const Settings& settings,
                                 const ProcessIndicatorsV2& indicators) {
    ActiveLimitsV2 limits;
    limits.powerCapped = indicators.powerLimited;
    if (state.mode == Mode::NBK) {
        const HeaterPowerPolicyV2 powerPolicy = SafetyPolicyV2::evaluateNbkHeaterPower(
            SafetyPolicyV2::getDefaultNbkHeaterPowerPercent(state, settings), state, settings);
        limits.maxHeaterPowerPercent = powerPolicy.limited ? powerPolicy.appliedPowerPercent : 100;
    } else if (state.mode == Mode::MANUAL_RECT &&
               SafetyPolicyV2::isManualRectFloodPowerLimitActive(state, settings)) {
        limits.maxHeaterPowerPercent = Heater::getPower();
    } else {
        limits.maxHeaterPowerPercent = indicators.powerLimited ? Heater::getPower() : 100;
    }
    limits.pumpCapped = (state.mode == Mode::NBK) && !indicators.nbkFeedAllowed;
    limits.maxPumpSpeedMlH = limits.pumpCapped ? state.pump.speedMlPerHour : 0.0f;
    limits.takeoffBlocked =
        (state.mode == Mode::RECTIFICATION || state.mode == Mode::MANUAL_RECT) &&
        !indicators.takeoffAllowed;
    limits.phaseAdvanceBlocked = !indicators.sensorFreshnessOk || indicators.floodRisk >= 0.80f;
    return limits;
}

SafetyDecisionV2 buildSafetyDecision(const SystemState& state, const ActiveLimitsV2& limits,
                                     const ProcessIndicatorsV2& indicators) {
    SafetyDecisionV2 decision;
    decision.limits = limits;

    if (!state.safetyOk && state.currentAlarm.type != AlarmType::NONE) {
        decision.severity = SafetySeverityV2::LATCHED_TRIP;
        decision.primaryEvent = SafetyEventTypeV2::EMERGENCY_STOP;
        decision.reasonCode = mapAlarmToReason(state);
        decision.requiresAcknowledge = !state.currentAlarm.acknowledged;
        strncpy(decision.message, state.currentAlarm.message, sizeof(decision.message) - 1);
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

ModeLifecycleV2 getLifecycle(const SystemState& state) {
    if (state.mode == Mode::IDLE) return ModeLifecycleV2::IDLE;
    if (!state.safetyOk && state.currentAlarm.type != AlarmType::NONE) return ModeLifecycleV2::FAULTED;
    if (state.paused) return ModeLifecycleV2::PAUSED;
    return ModeLifecycleV2::RUNNING;
}

void fillStatus(const SystemState& state, const ProcessIndicatorsV2& indicators,
                const MetricsSnapshotV2& metrics, ModeStatusV2& status) {
    status.mode = state.mode;
    status.lifecycle = getLifecycle(state);
    status.phaseId = getActivePhaseId(state);
    strncpy(status.phaseToken, getPhaseToken(state.mode, status.phaseId), sizeof(status.phaseToken) - 1);
    status.phaseToken[sizeof(status.phaseToken) - 1] = '\0';
    status.phaseStartMs = FSM::getPhaseStartTime();
    status.phaseElapsedSec = FSM::getPhaseElapsedSec();
    status.modeStartMs = g_modeStartMs;
    status.modeElapsedSec = (g_modeStartMs > 0 && millis() >= g_modeStartMs) ? (millis() - g_modeStartMs) / 1000UL : 0;
    status.paused = state.paused;
    status.safetyLatched = !state.safetyOk && state.currentAlarm.type != AlarmType::NONE;
    status.activeLimits = metrics.safety.limits;
    status.commandTargets.heaterPowerPercent = Heater::getPower();
    status.commandTargets.pumpSpeedMlH = state.pump.speedMlPerHour;
    status.commandTargets.waterValveOpen = Valves::getWater();
    status.commandTargets.headsValveOpen = Valves::getHeads();
    status.commandTargets.stopRequested = status.lifecycle == ModeLifecycleV2::FAULTED;
    status.indicators = indicators;
    if (status.lastReasonCode == ReasonCodeV2::NONE && metrics.safety.reasonCode != ReasonCodeV2::NONE) {
        status.lastReasonCode = metrics.safety.reasonCode;
    }
    if (metrics.safety.message[0] != '\0') {
        strncpy(status.operatorMessage, metrics.safety.message, sizeof(status.operatorMessage) - 1);
        status.operatorMessage[sizeof(status.operatorMessage) - 1] = '\0';
    } else {
        status.operatorMessage[0] = '\0';
    }
}

} // namespace

void notePhaseTransition(Mode mode, uint16_t fromPhaseId, uint16_t toPhaseId,
                         ReasonCodeV2 reasonCode, const char* operatorMessage) {
    g_pendingTransition.active = true;
    g_pendingTransition.mode = mode;
    g_pendingTransition.fromPhaseId = fromPhaseId;
    g_pendingTransition.toPhaseId = toPhaseId;
    g_pendingTransition.reasonCode = reasonCode;
    g_pendingTransition.operatorMessage[0] = '\0';
    if (operatorMessage != nullptr && operatorMessage[0] != '\0') {
        strncpy(g_pendingTransition.operatorMessage, operatorMessage,
                sizeof(g_pendingTransition.operatorMessage) - 1);
        g_pendingTransition.operatorMessage[sizeof(g_pendingTransition.operatorMessage) - 1] = '\0';
    }
}

void updateRuntime(const SystemState& state, const Settings& settings) {
    const uint32_t now = millis();
    g_lastIndicators = ProcessIndicatorsEngineV2::evaluate(state, settings, g_indicatorRuntime);

    const ActiveLimitsV2 limits = buildActiveLimits(state, settings, g_lastIndicators);
    const SafetyDecisionV2 safety = buildSafetyDecision(state, limits, g_lastIndicators);

    g_lastMetrics.timestampMs = now;
    g_lastMetrics.temperatures = state.temps;
    g_lastMetrics.pressure = state.pressure;
    g_lastMetrics.power = state.power;
    g_lastMetrics.pump = state.pump;
    g_lastMetrics.indicators = g_lastIndicators;
    g_lastMetrics.safety = safety;

    if (!g_initialized) {
        g_initialized = true;
        g_prevMode = state.mode;
        g_prevPhaseId = getActivePhaseId(state);
        g_modeStartMs = (state.mode == Mode::IDLE) ? 0 : now;
    }

    const uint16_t currentPhaseId = getActivePhaseId(state);
    PendingPhaseTransitionV2 explicitTransition;
    const bool hasExplicitTransition =
        consumeExplicitTransition(g_prevMode, g_prevPhaseId, state.mode, currentPhaseId, explicitTransition);
    if (hasExplicitTransition) {
        setStatusReason(explicitTransition.reasonCode, explicitTransition.operatorMessage);
        logTransitionEvent(explicitTransition.mode, explicitTransition.fromPhaseId,
                           explicitTransition.toPhaseId, explicitTransition.reasonCode,
                           explicitTransition.operatorMessage, g_lastIndicators, limits, now,
                           state.pump.totalVolumeMl);
    }

    if (state.mode != g_prevMode) {
        g_modeStartMs = (state.mode == Mode::IDLE) ? 0 : now;
        if (!hasExplicitTransition) {
            g_lastStatus.lastReasonCode =
                (state.mode == Mode::IDLE)
                    ? ((!state.safetyOk && state.currentAlarm.type != AlarmType::NONE)
                           ? mapAlarmToReason(state)
                           : ((g_prevMode == Mode::RECTIFICATION || g_prevMode == Mode::DISTILLATION) &&
                                      g_prevPhaseId == static_cast<uint16_t>(RectPhase::FINISH)
                                  ? ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE
                                  : ((g_prevMode == Mode::NBK &&
                                      (g_prevPhaseId == static_cast<uint16_t>(NbkPhase::FINISH) ||
                                       g_prevPhaseId == static_cast<uint16_t>(NbkPhase::COMPLETED)))
                                         ? ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE
                                         : ReasonCodeV2::RC_MODE_STOP_REQUEST)))
                    : ReasonCodeV2::RC_MODE_START_REQUEST;
        }
        g_prevMode = state.mode;
        g_prevPhaseId = currentPhaseId;
    } else if (currentPhaseId != g_prevPhaseId) {
        if (!hasExplicitTransition) {
            g_lastStatus.lastReasonCode =
                inferPhaseReason(state, g_prevPhaseId, currentPhaseId, g_lastIndicators);
            logTransitionEvent(state.mode, g_prevPhaseId, currentPhaseId, g_lastStatus.lastReasonCode,
                               nullptr, g_lastIndicators, limits, now, state.pump.totalVolumeMl);
        }
        g_prevPhaseId = currentPhaseId;
    }

    fillStatus(state, g_lastIndicators, g_lastMetrics, g_lastStatus);
}

const ProcessIndicatorsV2& getLatestIndicators() {
    return g_lastIndicators;
}

const MetricsSnapshotV2& getLatestMetricsSnapshot() {
    return g_lastMetrics;
}

const ModeStatusV2& getLatestModeStatus() {
    return g_lastStatus;
}

} // namespace ControlV2
