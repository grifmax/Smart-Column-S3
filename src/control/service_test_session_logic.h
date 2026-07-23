#pragma once

#include <cstdint>

// Pure lifecycle for a service test that temporarily owns outputs of an
// already running FSM. Driver and FSM calls deliberately live outside this
// header so the transition contract remains host-testable.
namespace ServiceTestSessionLogic {

enum class Phase : uint8_t {
  CLOSED = 0,
  REQUESTED,
  SAFE_PAUSED,
  TEST_ACTIVE,
  STOPPING,
  RESTORE_READY,
  FAULTED,
};

enum class Target : uint8_t {
  NONE = 0,
  HEATER,
  PUMP,
  STIRRER,
  WATER_VALVE,
  HEADS_VALVE,
  BODY_VALVE,
  TAILS_VALVE,
  UNO_VALVE,
  COOLING_PWM,
  FRACTIONATOR,
};

struct State {
  Phase phase = Phase::CLOSED;
  Target requestedTarget = Target::NONE;
  Target activeTarget = Target::NONE;
  uint8_t originalMode = 0;
  bool resumeProcess = false;
};

inline bool request(State &state, uint8_t mode, bool processWasRunning,
                    bool alreadyPaused, Target target) {
  if (state.phase != Phase::CLOSED || !processWasRunning ||
      target == Target::NONE) {
    return false;
  }
  state.originalMode = mode;
  state.resumeProcess = !alreadyPaused;
  state.requestedTarget = target;
  state.activeTarget = Target::NONE;
  state.phase = Phase::REQUESTED;
  return true;
}

inline bool markSafePaused(State &state) {
  if (state.phase != Phase::REQUESTED) return false;
  state.phase = Phase::SAFE_PAUSED;
  return true;
}

inline bool startTest(State &state, Target target) {
  if (state.phase != Phase::SAFE_PAUSED || target == Target::NONE) {
    return false;
  }
  state.activeTarget = target;
  state.phase = Phase::TEST_ACTIVE;
  return true;
}

inline bool beginStopping(State &state) {
  if (state.phase != Phase::SAFE_PAUSED && state.phase != Phase::TEST_ACTIVE) {
    return false;
  }
  state.activeTarget = Target::NONE;
  state.phase = Phase::STOPPING;
  return true;
}

inline bool markRestoreReady(State &state) {
  if (state.phase != Phase::STOPPING) return false;
  state.phase = Phase::RESTORE_READY;
  return true;
}

inline bool fault(State &state) {
  if (state.phase == Phase::CLOSED) return false;
  state.activeTarget = Target::NONE;
  state.phase = Phase::FAULTED;
  return true;
}

inline bool mayUsePausedProcess(const State &state, uint8_t currentMode,
                                bool paused) {
  return (state.phase == Phase::SAFE_PAUSED ||
          state.phase == Phase::TEST_ACTIVE) &&
         paused && state.originalMode == currentMode;
}

inline bool mayResumeProcess(const State &state) {
  return state.phase == Phase::RESTORE_READY && state.resumeProcess;
}

inline bool mayCloseWithoutResume(const State &state) {
  return state.phase == Phase::RESTORE_READY && !state.resumeProcess;
}

inline bool blocksProcessResume(const State &state) {
  return state.phase != Phase::CLOSED;
}

inline void close(State &state) { state = State{}; }

} // namespace ServiceTestSessionLogic
