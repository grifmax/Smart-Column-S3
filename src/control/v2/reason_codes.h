#ifndef CONTROL_V2_REASON_CODES_H
#define CONTROL_V2_REASON_CODES_H

#include <Arduino.h>

namespace ControlV2 {

enum class ReasonCodeV2 : uint16_t {
    NONE = 0,
    RC_MODE_START_REQUEST,
    RC_MODE_STOP_REQUEST,
    RC_PRECHECK_OK,
    RC_PRECHECK_FAIL_SENSOR,
    RC_PRECHECK_FAIL_SAFETY_LATCH,
    RC_HEATING_COMPLETE,
    RC_STABILIZATION_TIMER_OK,
    RC_STABILITY_WINDOW_REACHED,
    RC_HEADS_VOLUME_REACHED,
    RC_HEADS_SCORE_REACHED,
    RC_POST_HEADS_STABILIZATION_COMPLETE,
    RC_PURGE_COMPLETE,
    RC_BODY_TARGET_VOLUME_REACHED,
    RC_BODY_END_DETECTED,
    RC_TAILS_TARGET_REACHED,
    RC_FINISH_COOLDOWN_COMPLETE,
    RC_DISTILLATION_HEADS_OPTIONAL_SKIPPED,
    RC_DISTILLATION_END_TEMP_REACHED,
    RC_DISTILLATION_TARGET_VOLUME_REACHED,
    RC_NBK_STEAM_READY,
    RC_NBK_STABILIZATION_COMPLETE,
    RC_NBK_FEED_ENABLED,
    RC_NBK_FINISH_LIKELY,
    RC_TEMP_STEP_REACHED,
    RC_TEMP_STEP_HOLD_COMPLETE,
    RC_TEMP_STEP_TIMEOUT,
    RC_FERM_TARGET_REACHED,
    RC_SAFETY_LIMIT_POWER,
    RC_SAFETY_LIMIT_TAKEOFF,
    RC_SAFETY_PHASE_BLOCKED,
    RC_SAFETY_RECOVERY_ENTERED,
    RC_SAFETY_RECOVERY_EXITED,
    RC_SAFETY_TRIP_PRESSURE,
    RC_SAFETY_TRIP_SENSOR,
    RC_SAFETY_TRIP_OVERHEAT,
    RC_SAFETY_TRIP_POWER,
    RC_SAFETY_TRIP_GENERIC,
    RC_SAFETY_ACKNOWLEDGED,
    RC_SAFETY_RESET_COMPLETED,
    RC_MANUAL_OPERATOR_SWITCH,
    RC_MANUAL_OPERATOR_STOP,
    RC_PHASE_RECOVERY_APPLIED,
    RC_PHASE_TRANSITION_INFERRED,
    RC_UNSPECIFIED
};

enum class SafetySeverityV2 : uint8_t {
    NONE = 0,
    INFO,
    WARNING,
    LIMITED,
    RECOVERY,
    TRIP,
    LATCHED_TRIP
};

enum class SafetyEventTypeV2 : uint8_t {
    NONE = 0,
    SENSOR_STALE,
    SENSOR_FAILURE,
    PRESSURE_HIGH,
    PRESSURE_RISE_FAST,
    COOLING_OVERHEAT,
    COOLING_MARGIN_LOW,
    COLUMN_FLOOD_RISK,
    OVERHEAT,
    POWER_FAILURE,
    EMERGENCY_STOP,
    POWER_LIMIT_APPLIED,
    TAKEOFF_LIMIT_APPLIED,
    PHASE_ADVANCE_BLOCKED
};

inline const char* reasonCodeToString(ReasonCodeV2 code) {
    switch (code) {
        case ReasonCodeV2::NONE: return "RC_NONE";
        case ReasonCodeV2::RC_MODE_START_REQUEST: return "RC_MODE_START_REQUEST";
        case ReasonCodeV2::RC_MODE_STOP_REQUEST: return "RC_MODE_STOP_REQUEST";
        case ReasonCodeV2::RC_PRECHECK_OK: return "RC_PRECHECK_OK";
        case ReasonCodeV2::RC_PRECHECK_FAIL_SENSOR: return "RC_PRECHECK_FAIL_SENSOR";
        case ReasonCodeV2::RC_PRECHECK_FAIL_SAFETY_LATCH: return "RC_PRECHECK_FAIL_SAFETY_LATCH";
        case ReasonCodeV2::RC_HEATING_COMPLETE: return "RC_HEATING_COMPLETE";
        case ReasonCodeV2::RC_STABILIZATION_TIMER_OK: return "RC_STABILIZATION_TIMER_OK";
        case ReasonCodeV2::RC_STABILITY_WINDOW_REACHED: return "RC_STABILITY_WINDOW_REACHED";
        case ReasonCodeV2::RC_HEADS_VOLUME_REACHED: return "RC_HEADS_VOLUME_REACHED";
        case ReasonCodeV2::RC_HEADS_SCORE_REACHED: return "RC_HEADS_SCORE_REACHED";
        case ReasonCodeV2::RC_POST_HEADS_STABILIZATION_COMPLETE: return "RC_POST_HEADS_STABILIZATION_COMPLETE";
        case ReasonCodeV2::RC_PURGE_COMPLETE: return "RC_PURGE_COMPLETE";
        case ReasonCodeV2::RC_BODY_TARGET_VOLUME_REACHED: return "RC_BODY_TARGET_VOLUME_REACHED";
        case ReasonCodeV2::RC_BODY_END_DETECTED: return "RC_BODY_END_DETECTED";
        case ReasonCodeV2::RC_TAILS_TARGET_REACHED: return "RC_TAILS_TARGET_REACHED";
        case ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE: return "RC_FINISH_COOLDOWN_COMPLETE";
        case ReasonCodeV2::RC_DISTILLATION_HEADS_OPTIONAL_SKIPPED: return "RC_DISTILLATION_HEADS_OPTIONAL_SKIPPED";
        case ReasonCodeV2::RC_DISTILLATION_END_TEMP_REACHED: return "RC_DISTILLATION_END_TEMP_REACHED";
        case ReasonCodeV2::RC_DISTILLATION_TARGET_VOLUME_REACHED: return "RC_DISTILLATION_TARGET_VOLUME_REACHED";
        case ReasonCodeV2::RC_NBK_STEAM_READY: return "RC_NBK_STEAM_READY";
        case ReasonCodeV2::RC_NBK_STABILIZATION_COMPLETE: return "RC_NBK_STABILIZATION_COMPLETE";
        case ReasonCodeV2::RC_NBK_FEED_ENABLED: return "RC_NBK_FEED_ENABLED";
        case ReasonCodeV2::RC_NBK_FINISH_LIKELY: return "RC_NBK_FINISH_LIKELY";
        case ReasonCodeV2::RC_TEMP_STEP_REACHED: return "RC_TEMP_STEP_REACHED";
        case ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE: return "RC_TEMP_STEP_HOLD_COMPLETE";
        case ReasonCodeV2::RC_TEMP_STEP_TIMEOUT: return "RC_TEMP_STEP_TIMEOUT";
        case ReasonCodeV2::RC_FERM_TARGET_REACHED: return "RC_FERM_TARGET_REACHED";
        case ReasonCodeV2::RC_SAFETY_LIMIT_POWER: return "RC_SAFETY_LIMIT_POWER";
        case ReasonCodeV2::RC_SAFETY_LIMIT_TAKEOFF: return "RC_SAFETY_LIMIT_TAKEOFF";
        case ReasonCodeV2::RC_SAFETY_PHASE_BLOCKED: return "RC_SAFETY_PHASE_BLOCKED";
        case ReasonCodeV2::RC_SAFETY_RECOVERY_ENTERED: return "RC_SAFETY_RECOVERY_ENTERED";
        case ReasonCodeV2::RC_SAFETY_RECOVERY_EXITED: return "RC_SAFETY_RECOVERY_EXITED";
        case ReasonCodeV2::RC_SAFETY_TRIP_PRESSURE: return "RC_SAFETY_TRIP_PRESSURE";
        case ReasonCodeV2::RC_SAFETY_TRIP_SENSOR: return "RC_SAFETY_TRIP_SENSOR";
        case ReasonCodeV2::RC_SAFETY_TRIP_OVERHEAT: return "RC_SAFETY_TRIP_OVERHEAT";
        case ReasonCodeV2::RC_SAFETY_TRIP_POWER: return "RC_SAFETY_TRIP_POWER";
        case ReasonCodeV2::RC_SAFETY_TRIP_GENERIC: return "RC_SAFETY_TRIP_GENERIC";
        case ReasonCodeV2::RC_SAFETY_ACKNOWLEDGED: return "RC_SAFETY_ACKNOWLEDGED";
        case ReasonCodeV2::RC_SAFETY_RESET_COMPLETED: return "RC_SAFETY_RESET_COMPLETED";
        case ReasonCodeV2::RC_MANUAL_OPERATOR_SWITCH: return "RC_MANUAL_OPERATOR_SWITCH";
        case ReasonCodeV2::RC_MANUAL_OPERATOR_STOP: return "RC_MANUAL_OPERATOR_STOP";
        case ReasonCodeV2::RC_PHASE_RECOVERY_APPLIED: return "RC_PHASE_RECOVERY_APPLIED";
        case ReasonCodeV2::RC_PHASE_TRANSITION_INFERRED: return "RC_PHASE_TRANSITION_INFERRED";
        case ReasonCodeV2::RC_UNSPECIFIED: return "RC_UNSPECIFIED";
        default: return "RC_UNKNOWN";
    }
}

inline const char* safetySeverityToString(SafetySeverityV2 severity) {
    switch (severity) {
        case SafetySeverityV2::NONE: return "none";
        case SafetySeverityV2::INFO: return "info";
        case SafetySeverityV2::WARNING: return "warning";
        case SafetySeverityV2::LIMITED: return "limited";
        case SafetySeverityV2::RECOVERY: return "recovery";
        case SafetySeverityV2::TRIP: return "trip";
        case SafetySeverityV2::LATCHED_TRIP: return "latched_trip";
        default: return "unknown";
    }
}

inline const char* safetyEventTypeToString(SafetyEventTypeV2 type) {
    switch (type) {
        case SafetyEventTypeV2::NONE: return "none";
        case SafetyEventTypeV2::SENSOR_STALE: return "sensor_stale";
        case SafetyEventTypeV2::SENSOR_FAILURE: return "sensor_failure";
        case SafetyEventTypeV2::PRESSURE_HIGH: return "pressure_high";
        case SafetyEventTypeV2::PRESSURE_RISE_FAST: return "pressure_rise_fast";
        case SafetyEventTypeV2::COOLING_OVERHEAT: return "cooling_overheat";
        case SafetyEventTypeV2::COOLING_MARGIN_LOW: return "cooling_margin_low";
        case SafetyEventTypeV2::COLUMN_FLOOD_RISK: return "column_flood_risk";
        case SafetyEventTypeV2::OVERHEAT: return "overheat";
        case SafetyEventTypeV2::POWER_FAILURE: return "power_failure";
        case SafetyEventTypeV2::EMERGENCY_STOP: return "emergency_stop";
        case SafetyEventTypeV2::POWER_LIMIT_APPLIED: return "power_limit_applied";
        case SafetyEventTypeV2::TAKEOFF_LIMIT_APPLIED: return "takeoff_limit_applied";
        case SafetyEventTypeV2::PHASE_ADVANCE_BLOCKED: return "phase_advance_blocked";
        default: return "unknown";
    }
}

} // namespace ControlV2

#endif // CONTROL_V2_REASON_CODES_H
