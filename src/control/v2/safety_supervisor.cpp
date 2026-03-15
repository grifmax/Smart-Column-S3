#include "safety_supervisor.h"

#include <string.h>

#include "../../drivers/heater.h"
#include "../safety.h"
#include "safety_policy.h"

namespace ControlV2 {

namespace {

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

} // namespace

ActiveLimitsV2 SafetySupervisorV2::evaluateActiveLimits(const SystemState& state,
                                                        const Settings& settings,
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

} // namespace ControlV2
