#pragma once

#include <stdint.h>

#include "power_command_gateway.h"

namespace PowerOutputStateLogic {

inline bool sequenceIsNewerOrEqual(uint32_t candidate, uint32_t current) {
  return current == 0 || static_cast<int32_t>(candidate - current) >= 0;
}

inline void record(PowerCommandGateway::OutputState &state,
                   const PowerCommandGateway::Command &command) {
  if (sequenceIsNewerOrEqual(command.sequence, state.requestedCommandId)) {
    state.requestedCommandId = command.sequence;
    state.requestedAtMs = command.timestampMs;
    state.requestedValue = command.requestedValue;
    state.requestAccepted = command.accepted;
    state.requestQueued = command.queued;
    state.rejectReason = command.rejectReason;
  }
  if (command.accepted && command.applied) {
    state.appliedCommandId = command.sequence;
    state.appliedAtMs = command.timestampMs;
    state.appliedValue = command.appliedValue;
    state.hasAppliedValue = true;
  }
}

}  // namespace PowerOutputStateLogic
