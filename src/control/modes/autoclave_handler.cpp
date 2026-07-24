#include "../autoclave_hardware_profile.h"
#include "../fsm_utils.h"
#include "../power_command_gateway.h"
#include "../v2/reason_codes.h"
#include "../v2/status_adapter.h"
#include "../../storage/logger.h"

namespace FSM {
namespace Autoclave {
namespace {

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

} // namespace

void start(SystemState &state, const Settings &) {
  state.autoclave = AutoclaveRuntimeState{};
  enterPhase(state, AutoclavePhase::PRECHECK,
             ControlV2::ReasonCodeV2::RC_MODE_START_REQUEST,
             "Autoclave hardware profile accepted; awaiting pre-flight");
}

void update(SystemState &state, const Settings &) {
  // D-022 creates the separate mode and persistent state. D-023/D-025 add
  // phase control and the full safety contract. A profile that becomes
  // invalid after the start boundary can never leave a latent output command.
  if (AutoclaveHardwareProfile::isSteamAvailable()) return;

  PowerCommandGateway::safeStopAll(PowerCommandGateway::Owner::FSM,
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
}

} // namespace Autoclave
} // namespace FSM
