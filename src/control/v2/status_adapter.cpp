#include "status_adapter.h"

#include <string.h>

#include "../../drivers/heater.h"
#include "../../drivers/pump.h"
#include "../../drivers/valves.h"
#include "../../history.h"
#include "../../storage/logger.h"
#include "../fsm.h"
#include "../fsm_utils.h"
#include "safety_supervisor.h"
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
uint32_t g_prevPhaseStartMs = 0;
float g_prevPhaseStartVolumeMl = 0.0f;
float g_prevPhaseStartTempC = 0.0f;
bool g_initialized = false;
SafetyDecisionV2 g_prevSafetyDecision;
bool g_prevSafetyInitialized = false;

struct PendingPhaseTransitionV2 {
    bool active = false;
    Mode mode = Mode::IDLE;
    uint16_t fromPhaseId = 0;
    uint16_t toPhaseId = 0;
    ReasonCodeV2 reasonCode = ReasonCodeV2::NONE;
    char operatorMessage[96] = "";
};

PendingPhaseTransitionV2 g_pendingTransition;
struct PendingSafetyOperatorActionV2 {
    bool active = false;
    ReasonCodeV2 reasonCode = ReasonCodeV2::NONE;
    char message[96] = "";
    char operatorMessage[96] = "";
};

PendingSafetyOperatorActionV2 g_pendingSafetyAction;
const char* getPhaseToken(Mode mode, uint16_t phaseId);

const char* getHistoryProcessType(Mode mode) {
    switch (mode) {
        case Mode::RECTIFICATION: return "rectification";
        case Mode::DISTILLATION: return "distillation";
        case Mode::MANUAL_RECT: return "rectification";
        case Mode::MASHING: return "mashing";
        case Mode::HOLD: return "hold";
        case Mode::NBK: return "nbk";
        case Mode::FERMENTATION: return "fermentation";
        case Mode::IDLE:
        default:
            return "idle";
    }
}

const char* getHistoryProcessMode(Mode mode) {
    return mode == Mode::MANUAL_RECT ? "manual" : "auto";
}

float getRepresentativePhaseTemp(const SystemState& state) {
    if (state.temps.valid[TEMP_COLUMN_TOP]) {
        return state.temps.columnTop;
    }
    if (state.temps.valid[TEMP_COLUMN_BOTTOM]) {
        return state.temps.columnBottom;
    }
    if (state.temps.valid[TEMP_CUBE]) {
        return state.temps.cube;
    }
    return 0.0f;
}

bool isIdleLikePhase(Mode mode, uint16_t phaseId) {
    if (phaseId == kNoPhaseIdV2) {
        return true;
    }

    switch (mode) {
        case Mode::NBK:
            return static_cast<NbkPhase>(phaseId) == NbkPhase::IDLE;
        case Mode::FERMENTATION:
            return static_cast<FermentationPhase>(phaseId) == FermentationPhase::IDLE;
        case Mode::MASHING:
            return static_cast<MashPhase>(phaseId) == MashPhase::IDLE;
        case Mode::HOLD:
            return false;
        case Mode::RECTIFICATION:
        case Mode::DISTILLATION:
        case Mode::MANUAL_RECT:
        default:
            return static_cast<RectPhase>(phaseId) == RectPhase::IDLE;
    }
}

bool isSuccessfulCompletionReason(ReasonCodeV2 reasonCode) {
    switch (reasonCode) {
        case ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE:
        case ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE:
        case ReasonCodeV2::RC_FERM_TARGET_REACHED:
            return true;
        default:
            return false;
    }
}

ReasonCodeV2 inferModeExitReason(const SystemState& state, Mode previousMode,
                                 uint16_t previousPhaseId) {
    if (!state.safetyOk && state.currentAlarm.type != AlarmType::NONE) {
        return g_lastMetrics.safety.reasonCode;
    }

    if (isSuccessfulCompletionReason(g_lastStatus.lastReasonCode)) {
        return g_lastStatus.lastReasonCode;
    }

    switch (previousMode) {
        case Mode::RECTIFICATION:
        case Mode::DISTILLATION:
            return previousPhaseId == static_cast<uint16_t>(RectPhase::FINISH)
                       ? ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE
                       : ReasonCodeV2::RC_MODE_STOP_REQUEST;
        case Mode::NBK:
            return (previousPhaseId == static_cast<uint16_t>(NbkPhase::FINISH) ||
                    previousPhaseId == static_cast<uint16_t>(NbkPhase::COMPLETED))
                       ? ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE
                       : ReasonCodeV2::RC_MODE_STOP_REQUEST;
        default:
            return ReasonCodeV2::RC_MODE_STOP_REQUEST;
    }
}

bool isHistorySafetyState(const SafetyDecisionV2& decision) {
    return decision.severity == SafetySeverityV2::LIMITED ||
           decision.severity == SafetySeverityV2::RECOVERY ||
           decision.severity == SafetySeverityV2::TRIP ||
           decision.severity == SafetySeverityV2::LATCHED_TRIP;
}

bool sameHistorySafetyState(const SafetyDecisionV2& left,
                            const SafetyDecisionV2& right) {
    return left.severity == right.severity &&
           left.primaryEvent == right.primaryEvent &&
           left.reasonCode == right.reasonCode &&
           left.limits.powerCapped == right.limits.powerCapped &&
           left.limits.takeoffBlocked == right.limits.takeoffBlocked &&
           left.limits.phaseAdvanceBlocked == right.limits.phaseAdvanceBlocked;
}

const char* getHistorySeverity(const SafetyDecisionV2& decision) {
    switch (decision.severity) {
        case SafetySeverityV2::TRIP:
        case SafetySeverityV2::LATCHED_TRIP:
            return "error";
        case SafetySeverityV2::RECOVERY:
            return "info";
        case SafetySeverityV2::LIMITED:
        default:
            return "warning";
    }
}

bool lastHistoryEventMatches(const char* severity, ReasonCodeV2 reasonCode,
                             const char* message, const char* operatorMessage) {
    if (!processRecorder.isRecording()) {
        return false;
    }

    ProcessHistory& history = processRecorder.getHistory();
    const std::vector<ProcessWarning>& events =
        (strcmp(severity, "error") == 0) ? history.results.errors
                                          : history.results.warnings;
    if (events.empty()) {
        return false;
    }

    const ProcessWarning& last = events.back();
    const String expectedReason = reasonCodeToString(reasonCode);
    const String expectedMessage =
        (message != nullptr && message[0] != '\0') ? String(message)
                                                   : String(expectedReason);
    const String expectedOperatorMessage =
        (operatorMessage != nullptr && operatorMessage[0] != '\0')
            ? String(operatorMessage)
            : String();

    return last.severity == severity &&
           last.reasonCode == expectedReason &&
           last.message == expectedMessage &&
           last.operatorMessage == expectedOperatorMessage;
}

void appendHistorySafetyEvent(const char* severity, ReasonCodeV2 reasonCode,
                              const char* message,
                              const char* operatorMessage = nullptr) {
    if (!processRecorder.isRecording()) {
        return;
    }

    const char* resolvedMessage =
        (message != nullptr && message[0] != '\0') ? message
                                                   : reasonCodeToString(reasonCode);
    if (lastHistoryEventMatches(severity, reasonCode, resolvedMessage,
                                operatorMessage)) {
        return;
    }

    processRecorder.addWarning(resolvedMessage, severity,
                               reasonCodeToString(reasonCode),
                               operatorMessage != nullptr ? String(operatorMessage)
                                                          : String());
}

void recordHistorySafetyDecision(const SafetyDecisionV2& decision,
                                 bool force = false) {
    if (!processRecorder.isRecording() || !isHistorySafetyState(decision)) {
        return;
    }
    if (!force && g_prevSafetyInitialized &&
        sameHistorySafetyState(decision, g_prevSafetyDecision)) {
        return;
    }

    appendHistorySafetyEvent(getHistorySeverity(decision), decision.reasonCode,
                             decision.message);
}

void recordHistorySafetyRecoveryExit(const SafetyDecisionV2& previousDecision,
                                     const SafetyDecisionV2& currentDecision) {
    if (!processRecorder.isRecording()) {
        return;
    }
    if (previousDecision.severity != SafetySeverityV2::RECOVERY ||
        currentDecision.severity == SafetySeverityV2::RECOVERY) {
        return;
    }

    appendHistorySafetyEvent(
        "info", ReasonCodeV2::RC_SAFETY_RECOVERY_EXITED,
        "Safety recovery completed, process returned to normal operation");
}

void flushPendingSafetyOperatorAction() {
    if (!g_pendingSafetyAction.active) {
        return;
    }

    appendHistorySafetyEvent("info", g_pendingSafetyAction.reasonCode,
                             g_pendingSafetyAction.message,
                             g_pendingSafetyAction.operatorMessage[0] != '\0'
                                 ? g_pendingSafetyAction.operatorMessage
                                 : nullptr);
    g_pendingSafetyAction.active = false;
}

void startHistoryTracking(const SystemState& state) {
    if (state.mode == Mode::IDLE) {
        return;
    }
    if (!processRecorder.isRecording()) {
        processRecorder.startRecording(getHistoryProcessType(state.mode),
                                       getHistoryProcessMode(state.mode));
    }
    g_prevPhaseStartMs = millis();
    g_prevPhaseStartVolumeMl = state.pump.totalVolumeMl;
    g_prevPhaseStartTempC = getRepresentativePhaseTemp(state);
}

void recordCompletedPhase(Mode mode, uint16_t phaseId, ReasonCodeV2 reasonCode,
                          const char* operatorMessage, const SystemState& state,
                          uint32_t nowMs) {
    if (!processRecorder.isRecording() || mode == Mode::IDLE ||
        isIdleLikePhase(mode, phaseId)) {
        return;
    }

    const uint32_t startMs = g_prevPhaseStartMs > 0 ? g_prevPhaseStartMs : nowMs;
    const uint32_t durationSec = (nowMs >= startMs) ? (nowMs - startMs) / 1000UL : 0;
    float phaseVolumeMl = state.pump.totalVolumeMl - g_prevPhaseStartVolumeMl;
    if (phaseVolumeMl < 0.0f) {
        phaseVolumeMl = 0.0f;
    }

    ProcessPhase phase;
    phase.name = getPhaseToken(mode, phaseId);
    phase.startTime = startMs / 1000UL;
    phase.endTime = nowMs / 1000UL;
    phase.duration = durationSec;
    phase.startTemp = g_prevPhaseStartTempC;
    phase.endTemp = getRepresentativePhaseTemp(state);
    phase.volume = static_cast<uint16_t>(phaseVolumeMl);
    phase.avgSpeed =
        (durationSec > 0 && phaseVolumeMl > 0.0f)
            ? static_cast<uint16_t>((phaseVolumeMl * 3600.0f) / durationSec)
            : 0;
    phase.reasonCode = reasonCodeToString(reasonCode);
    if (operatorMessage != nullptr && operatorMessage[0] != '\0') {
        phase.operatorMessage = operatorMessage;
    }

    processRecorder.addPhase(phase);
}

void stopHistoryTracking(bool success, ReasonCodeV2 reasonCode,
                         const char* operatorMessage, const SystemState& state) {
    if (!processRecorder.isRecording()) {
        return;
    }

    if (!state.safetyOk && state.currentAlarm.type != AlarmType::NONE) {
        String message = g_lastMetrics.safety.message[0] != '\0'
                             ? String(g_lastMetrics.safety.message)
                             : String("Safety stop");
        if (!lastHistoryEventMatches("error", reasonCode, message.c_str(),
                                     operatorMessage)) {
            processRecorder.addWarning(
                message, "error", reasonCodeToString(reasonCode),
                operatorMessage != nullptr ? String(operatorMessage) : String());
        }
    }

    processRecorder.stopRecording(success);
}

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
    if (phaseId == kNoPhaseIdV2) {
        return "idle";
    }

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

    const bool isModeStartTransition =
        previousMode == Mode::IDLE &&
        g_pendingTransition.mode == currentMode &&
        (g_pendingTransition.fromPhaseId == previousPhaseId ||
         g_pendingTransition.fromPhaseId == kNoPhaseIdV2) &&
        g_pendingTransition.toPhaseId == currentPhaseId;

    if (!isModeStartTransition) {
        if (g_pendingTransition.mode != previousMode ||
            g_pendingTransition.fromPhaseId != previousPhaseId) {
            return false;
        }
        if (currentMode == previousMode &&
            g_pendingTransition.toPhaseId != currentPhaseId) {
            return false;
        }
    }

    transition = g_pendingTransition;
    g_pendingTransition.active = false;
    return true;
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

void noteSafetyOperatorAction(ReasonCodeV2 reasonCode, const char* message,
                              const char* operatorMessage) {
    g_pendingSafetyAction.active = true;
    g_pendingSafetyAction.reasonCode = reasonCode;
    g_pendingSafetyAction.message[0] = '\0';
    g_pendingSafetyAction.operatorMessage[0] = '\0';

    if (message != nullptr && message[0] != '\0') {
        strncpy(g_pendingSafetyAction.message, message,
                sizeof(g_pendingSafetyAction.message) - 1);
        g_pendingSafetyAction.message[sizeof(g_pendingSafetyAction.message) - 1] = '\0';
    }

    if (operatorMessage != nullptr && operatorMessage[0] != '\0') {
        strncpy(g_pendingSafetyAction.operatorMessage, operatorMessage,
                sizeof(g_pendingSafetyAction.operatorMessage) - 1);
        g_pendingSafetyAction.operatorMessage[sizeof(g_pendingSafetyAction.operatorMessage) - 1] = '\0';
    }
}

void updateRuntime(const SystemState& state, const Settings& settings) {
    const uint32_t now = millis();
    g_lastIndicators = ProcessIndicatorsEngineV2::evaluate(state, settings, g_indicatorRuntime);

    const ActiveLimitsV2 limits =
        SafetySupervisorV2::evaluateActiveLimits(state, settings, g_lastIndicators);
    const SafetyDecisionV2 safety =
        SafetySupervisorV2::evaluateDecision(state, settings, g_lastIndicators, limits);
    SafetySupervisorV2::applyDecisionToIndicators(safety, g_lastIndicators);

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
        g_prevPhaseStartMs = now;
        g_prevPhaseStartVolumeMl = state.pump.totalVolumeMl;
        g_prevPhaseStartTempC = getRepresentativePhaseTemp(state);
    }
    if (!g_prevSafetyInitialized) {
        g_prevSafetyDecision = safety;
        g_prevSafetyInitialized = true;
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
        ReasonCodeV2 modeChangeReason = explicitTransition.reasonCode;
        if (!hasExplicitTransition) {
            modeChangeReason = (state.mode == Mode::IDLE)
                                   ? inferModeExitReason(state, g_prevMode, g_prevPhaseId)
                                   : ReasonCodeV2::RC_MODE_START_REQUEST;
            g_lastStatus.lastReasonCode = modeChangeReason;
        }
        if (g_prevMode != Mode::IDLE) {
            recordCompletedPhase(g_prevMode, g_prevPhaseId, modeChangeReason,
                                 hasExplicitTransition ? explicitTransition.operatorMessage : nullptr,
                                 state, now);
        }
        if (state.mode == Mode::IDLE) {
            recordHistorySafetyDecision(safety);
            stopHistoryTracking(isSuccessfulCompletionReason(modeChangeReason),
                                modeChangeReason,
                                hasExplicitTransition
                                    ? explicitTransition.operatorMessage
                                    : nullptr,
                                state);
        } else {
            startHistoryTracking(state);
            recordHistorySafetyDecision(safety, true);
        }
        g_prevMode = state.mode;
        g_prevPhaseId = currentPhaseId;
        g_prevPhaseStartMs = now;
        g_prevPhaseStartVolumeMl = state.pump.totalVolumeMl;
        g_prevPhaseStartTempC = getRepresentativePhaseTemp(state);
    } else if (currentPhaseId != g_prevPhaseId) {
        ReasonCodeV2 phaseReason = explicitTransition.reasonCode;
        if (!hasExplicitTransition) {
            phaseReason = inferPhaseReason(state, g_prevPhaseId, currentPhaseId, g_lastIndicators);
            g_lastStatus.lastReasonCode = phaseReason;
            logTransitionEvent(state.mode, g_prevPhaseId, currentPhaseId, phaseReason,
                               nullptr, g_lastIndicators, limits, now, state.pump.totalVolumeMl);
        }
        recordCompletedPhase(state.mode, g_prevPhaseId, phaseReason,
                             hasExplicitTransition ? explicitTransition.operatorMessage : nullptr,
                             state, now);
        g_prevPhaseId = currentPhaseId;
        g_prevPhaseStartMs = now;
        g_prevPhaseStartVolumeMl = state.pump.totalVolumeMl;
        g_prevPhaseStartTempC = getRepresentativePhaseTemp(state);
    }

    recordHistorySafetyRecoveryExit(g_prevSafetyDecision, safety);
    recordHistorySafetyDecision(safety);
    flushPendingSafetyOperatorAction();

    fillStatus(state, g_lastIndicators, g_lastMetrics, g_lastStatus);
    g_prevSafetyDecision = safety;
    g_prevSafetyInitialized = true;
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
