#include "../autoclave_hardware_profile.h"
#include "../autoclave_session_logic.h"
#include "../fsm_utils.h"
#include "../power_command_gateway.h"
#include "../v2/reason_codes.h"
#include "../v2/status_adapter.h"
#include "../../storage/logger.h"

namespace FSM {
namespace Autoclave {
namespace {

AutoclaveSessionLogic::State readSession(const SystemState &state) {
  AutoclaveSessionLogic::State session;
  session.phase = static_cast<AutoclaveSessionLogic::Phase>(
      state.autoclave.phase);
  session.operatorReadyConfirmed = state.autoclave.operatorReadyConfirmed;
  session.sterilizationAccumulatedMs =
      state.autoclave.sterilizationAccumulatedMs;
  session.phaseStartedAtMs = state.autoclave.phaseStartedAtMs;
  session.lastInBandAtMs = state.autoclave.lastInBandAtMs;
  return session;
}

AutoclaveSessionLogic::Config readConfig(const Settings &settings) {
  AutoclaveSessionLogic::Config config;
  config.sterilizationTempC = settings.autoclave.sterilizationTempC;
  config.temperatureBandC = settings.autoclave.temperatureBandC;
  config.sterilizationDurationMs =
      static_cast<uint32_t>(settings.autoclave.sterilizationDurationMin) *
      60UL * 1000UL;
  config.coolingStopTempC = settings.autoclave.coolingStopTempC;
  return config;
}

ControlV2::ReasonCodeV2 reasonForPhase(AutoclavePhase phase) {
  switch (phase) {
  case AutoclavePhase::HEATING:
    return ControlV2::ReasonCodeV2::RC_PRECHECK_OK;
  case AutoclavePhase::STERILIZING:
    return ControlV2::ReasonCodeV2::RC_TEMP_STEP_REACHED;
  case AutoclavePhase::COOLING:
    return ControlV2::ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE;
  case AutoclavePhase::READY:
    return ControlV2::ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE;
  case AutoclavePhase::FINISH:
    return ControlV2::ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE;
  case AutoclavePhase::FAULT:
    return ControlV2::ReasonCodeV2::RC_SAFETY_TRIP_SENSOR;
  case AutoclavePhase::PRECHECK:
    return ControlV2::ReasonCodeV2::RC_MODE_START_REQUEST;
  case AutoclavePhase::IDLE:
  default:
    return ControlV2::ReasonCodeV2::RC_UNSPECIFIED;
  }
}

const char *messageForPhase(AutoclavePhase phase) {
  switch (phase) {
  case AutoclavePhase::HEATING:
    return "Autoclave pre-flight confirmed; heating phase entered";
  case AutoclavePhase::STERILIZING:
    return "Sterilization timer started inside temperature band";
  case AutoclavePhase::COOLING:
    return "Sterilization duration reached; cooling phase entered";
  case AutoclavePhase::READY:
    return "Removal temperature reached";
  case AutoclavePhase::FINISH:
    return "Autoclave process completed";
  case AutoclavePhase::FAULT:
    return "Autoclave sensor/configuration fault";
  case AutoclavePhase::PRECHECK:
    return "Autoclave hardware profile accepted; awaiting pre-flight";
  case AutoclavePhase::IDLE:
  default:
    return "Autoclave state updated";
  }
}

void enterPhase(SystemState &state, AutoclavePhase phase,
                ControlV2::ReasonCodeV2 reason, const char *message) {
  if (state.autoclave.phase == phase) return;
  ControlV2::notePhaseTransition(
      Mode::AUTOCLAVE, static_cast<uint16_t>(state.autoclave.phase),
      static_cast<uint16_t>(phase), reason, message);
  state.autoclave.phase = phase;
  state.autoclave.phaseStartedAtMs = millis();
  setPhaseStartTime(state.autoclave.phaseStartedAtMs);
}

void writeSession(SystemState &state,
                  const AutoclaveSessionLogic::State &session) {
  const AutoclavePhase phase =
      static_cast<AutoclavePhase>(session.phase);
  if (state.autoclave.phase != phase) {
    enterPhase(state, phase, reasonForPhase(phase), messageForPhase(phase));
  }
  state.autoclave.operatorReadyConfirmed = session.operatorReadyConfirmed;
  state.autoclave.sterilizationAccumulatedMs =
      session.sterilizationAccumulatedMs;
  state.autoclave.phaseStartedAtMs = session.phaseStartedAtMs;
  state.autoclave.lastInBandAtMs = session.lastInBandAtMs;
}

} // namespace

void start(SystemState &state, const Settings &) {
  state.autoclave = AutoclaveRuntimeState{};
  AutoclaveSessionLogic::State session;
  (void)AutoclaveSessionLogic::start(session, millis());
  writeSession(state, session);
}

bool confirmReadiness(SystemState &state, const Settings &) {
  if (!AutoclaveHardwareProfile::isSteamAvailable()) return false;
  AutoclaveSessionLogic::State session = readSession(state);
  const bool sensorsReady = state.temps.valid[TEMP_CUBE] && state.pressure.ok;
  if (!AutoclaveSessionLogic::confirmPrecheck(session, sensorsReady, millis())) {
    return false;
  }
  writeSession(state, session);
  return true;
}

bool finish(SystemState &state) {
  AutoclaveSessionLogic::State session = readSession(state);
  if (!AutoclaveSessionLogic::finish(session, millis())) return false;
  writeSession(state, session);
  return true;
}

void update(SystemState &state, const Settings &settings) {
  // A profile that changes after the start boundary can never leave a latent
  // output command. D-024 owns approved heating/cooling outputs.
  if (!AutoclaveHardwareProfile::isSteamAvailable()) {
    PowerCommandGateway::safeStopAll(
        PowerCommandGateway::Owner::FSM,
        PowerCommandGateway::Reason::FSM_AUTOCLAVE);
    if (state.autoclave.phase != AutoclavePhase::FAULT) {
      strlcpy(state.autoclave.faultReason,
              AutoclaveHardwareProfileLogic::validationToken(
                  AutoclaveHardwareProfile::validate()),
              sizeof(state.autoclave.faultReason));
      enterPhase(state, AutoclavePhase::FAULT,
                 ControlV2::ReasonCodeV2::RC_PRECHECK_FAIL_SAFETY_LATCH,
                 "Autoclave hardware profile became unavailable");
      Logger::logf(2, "autoclave event=profile_fault detail=%s",
                   state.autoclave.faultReason);
    }
    return;
  }

  AutoclaveSessionLogic::State session = readSession(state);
  const bool changed = AutoclaveSessionLogic::update(
      session, readConfig(settings), state.temps.valid[TEMP_CUBE],
      state.temps.cube, millis());
  if (!changed) return;
  writeSession(state, session);
  if (state.autoclave.phase == AutoclavePhase::FAULT) {
    PowerCommandGateway::safeStopAll(
        PowerCommandGateway::Owner::FSM,
        PowerCommandGateway::Reason::FSM_AUTOCLAVE);
    strlcpy(state.autoclave.faultReason, "sensor_or_settings_invalid",
            sizeof(state.autoclave.faultReason));
    Logger::logf(2, "autoclave event=phase_fault detail=%s",
                 state.autoclave.faultReason);
  }
}

} // namespace Autoclave
} // namespace FSM
