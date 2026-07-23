#pragma once

#include <cstdint>

// Small platform-independent state machine for TFT modal actions. Keeping the
// claim separate from rendering makes a repeated tap harmless even when a
// redraw is still pending after the first command.
namespace TftOperatorOverlayLogic {

enum class Action : uint8_t {
  None = 0,
  ProcessStop,
  AlarmAcknowledge,
  MashOperatorWait,
  FractionConfirmation,
  ContainerChange,
  ServiceHeaterStart,
  ServicePauseForTest,
};

struct State {
  Action action = Action::None;
  bool actionIssued = false;
};

inline bool isActive(const State &state) { return state.action != Action::None; }

inline bool mayDispatch(const State &state) {
  return isActive(state) && !state.actionIssued;
}

inline bool claimAction(State &state) {
  if (!mayDispatch(state)) {
    return false;
  }
  state.actionIssued = true;
  return true;
}

inline void open(State &state, Action action) {
  state.action = action;
  state.actionIssued = false;
}

inline void close(State &state) { state = State{}; }

inline bool isAutomatic(Action action) {
  return action == Action::MashOperatorWait ||
         action == Action::FractionConfirmation ||
         action == Action::ContainerChange;
}

} // namespace TftOperatorOverlayLogic
