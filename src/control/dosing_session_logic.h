#pragma once

#include <cstdint>

// Platform-independent lifecycle for a finite service dosing operation. The
// driver calls live in dosing_session.cpp so the transition contract can be
// exercised by native tests without hardware.
namespace DosingSessionLogic {

enum class Route : uint8_t {
  NONE = 0,
  HEADS,
  BODY,
  TAILS,
};

enum class Phase : uint8_t {
  IDLE = 0,
  PRIME,
  READY,
  DISPENSING,
  COMPLETED,
  CANCELLED,
  FAULTED,
};

enum class FinishReason : uint8_t {
  NONE = 0,
  TARGET_REACHED,
  OPERATOR_STOP,
  PAGE_EXIT,
  SAFETY,
  ROUTE_LOST,
  PRIME_TIMEOUT,
  TIMEOUT,
  CONFIG_INVALID,
};

struct State {
  Phase phase = Phase::IDLE;
  Route route = Route::NONE;
  float requestedVolumeMl = 0.0f;
  float requestedRateMlH = 0.0f;
  float calibrationAtStart = 0.0f;
  float startPumpVolumeMl = 0.0f;
  float estimatedVolumeMl = 0.0f;
  uint32_t startedAtMs = 0;
  uint32_t finishedAtMs = 0;
  FinishReason finishReason = FinishReason::NONE;
};

inline const char *routeToken(Route route) {
  switch (route) {
  case Route::HEADS:
    return "heads";
  case Route::BODY:
    return "body";
  case Route::TAILS:
    return "tails";
  case Route::NONE:
  default:
    return "none";
  }
}

inline const char *phaseToken(Phase phase) {
  switch (phase) {
  case Phase::PRIME:
    return "prime";
  case Phase::READY:
    return "ready";
  case Phase::DISPENSING:
    return "dispensing";
  case Phase::COMPLETED:
    return "completed";
  case Phase::CANCELLED:
    return "cancelled";
  case Phase::FAULTED:
    return "faulted";
  case Phase::IDLE:
  default:
    return "idle";
  }
}

inline const char *finishReasonToken(FinishReason reason) {
  switch (reason) {
  case FinishReason::TARGET_REACHED:
    return "target_reached";
  case FinishReason::OPERATOR_STOP:
    return "operator_stop";
  case FinishReason::PAGE_EXIT:
    return "page_exit";
  case FinishReason::SAFETY:
    return "safety";
  case FinishReason::ROUTE_LOST:
    return "route_lost";
  case FinishReason::PRIME_TIMEOUT:
    return "prime_timeout";
  case FinishReason::TIMEOUT:
    return "timeout";
  case FinishReason::CONFIG_INVALID:
    return "config_invalid";
  case FinishReason::NONE:
  default:
    return "none";
  }
}

inline bool isOutputActive(const State &state) {
  return state.phase == Phase::PRIME || state.phase == Phase::DISPENSING;
}

inline bool mayConfigure(const State &state) {
  return state.phase == Phase::IDLE || state.phase == Phase::COMPLETED ||
         state.phase == Phase::CANCELLED || state.phase == Phase::FAULTED;
}

inline bool configure(State &state, Route route, float requestedVolumeMl,
                      float requestedRateMlH) {
  if (!mayConfigure(state) || route == Route::NONE ||
      requestedVolumeMl <= 0.0f || requestedRateMlH <= 0.0f) {
    return false;
  }
  state = State{};
  state.route = route;
  state.requestedVolumeMl = requestedVolumeMl;
  state.requestedRateMlH = requestedRateMlH;
  return true;
}

inline bool beginPriming(State &state, float calibrationMlPerRev,
                         uint32_t nowMs) {
  if (state.phase != Phase::IDLE || state.route == Route::NONE ||
      state.requestedVolumeMl <= 0.0f || state.requestedRateMlH <= 0.0f ||
      calibrationMlPerRev <= 0.0f) {
    return false;
  }
  state.phase = Phase::PRIME;
  state.calibrationAtStart = calibrationMlPerRev;
  state.startPumpVolumeMl = 0.0f;
  state.estimatedVolumeMl = 0.0f;
  state.startedAtMs = nowMs;
  state.finishedAtMs = 0;
  state.finishReason = FinishReason::NONE;
  return true;
}

inline bool finishPriming(State &state) {
  if (state.phase != Phase::PRIME) return false;
  state.phase = Phase::READY;
  return true;
}

inline bool beginDispensing(State &state, float calibrationMlPerRev,
                            float startPumpVolumeMl, uint32_t nowMs) {
  if (state.phase != Phase::READY || state.route == Route::NONE ||
      state.requestedVolumeMl <= 0.0f || state.requestedRateMlH <= 0.0f ||
      calibrationMlPerRev <= 0.0f) {
    return false;
  }
  state.phase = Phase::DISPENSING;
  state.calibrationAtStart = calibrationMlPerRev;
  state.startPumpVolumeMl = startPumpVolumeMl;
  state.estimatedVolumeMl = 0.0f;
  state.startedAtMs = nowMs;
  state.finishedAtMs = 0;
  state.finishReason = FinishReason::NONE;
  return true;
}

inline void updateEstimatedVolume(State &state, float estimatedVolumeMl) {
  if (state.phase != Phase::DISPENSING || estimatedVolumeMl < 0.0f) return;
  state.estimatedVolumeMl = estimatedVolumeMl;
}

inline bool targetReached(const State &state) {
  return state.phase == Phase::DISPENSING &&
         state.requestedVolumeMl > 0.0f &&
         state.estimatedVolumeMl >= state.requestedVolumeMl;
}

inline bool timedOut(const State &state, uint32_t nowMs,
                     uint32_t maxPrimeDurationMs,
                     uint32_t maxDosingDurationMs) {
  if (state.phase != Phase::PRIME && state.phase != Phase::DISPENSING) {
    return false;
  }
  const uint32_t limit = state.phase == Phase::PRIME
                             ? maxPrimeDurationMs
                             : maxDosingDurationMs;
  return limit > 0 &&
         static_cast<uint32_t>(nowMs - state.startedAtMs) >= limit;
}

inline bool finish(State &state, FinishReason reason, uint32_t nowMs) {
  if (!isOutputActive(state) || reason == FinishReason::NONE) return false;
  state.finishedAtMs = nowMs;
  state.finishReason = reason;
  if (reason == FinishReason::TARGET_REACHED) {
    state.phase = Phase::COMPLETED;
  } else if (reason == FinishReason::SAFETY ||
             reason == FinishReason::ROUTE_LOST ||
             reason == FinishReason::PRIME_TIMEOUT ||
             reason == FinishReason::TIMEOUT ||
             reason == FinishReason::CONFIG_INVALID) {
    state.phase = Phase::FAULTED;
  } else {
    state.phase = Phase::CANCELLED;
  }
  return true;
}

} // namespace DosingSessionLogic
