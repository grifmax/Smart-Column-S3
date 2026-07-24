#pragma once

#include <cstdint>

namespace PumpCalibrationLogic {

enum class Phase : uint8_t {
  IDLE = 0,
  PRIMING,
  READY_TO_MEASURE,
  MEASURING,
  READY_TO_REVIEW,
  REVIEW,
};

struct State {
  Phase phase = Phase::IDLE;
  float speedMlH = 0.0f;
  uint32_t startSteps = 0;
  uint32_t stopSteps = 0;
  uint32_t startedAtMs = 0;
  uint32_t stoppedAtMs = 0;
  float measuredVolumeMl = 0.0f;
  float candidateMlPerRev = 0.0f;
};

inline bool beginPriming(State &state, float speedMlH, uint32_t nowMs) {
  if (state.phase != Phase::IDLE || speedMlH <= 0.0f) return false;
  state = State{};
  state.phase = Phase::PRIMING;
  state.speedMlH = speedMlH;
  state.startedAtMs = nowMs;
  return true;
}

inline bool finishPriming(State &state) {
  if (state.phase != Phase::PRIMING) return false;
  state.phase = Phase::READY_TO_MEASURE;
  return true;
}

inline bool beginMeasurement(State &state, float speedMlH, uint32_t nowMs,
                             bool allowWithoutPrime = false) {
  if ((state.phase != Phase::READY_TO_MEASURE &&
       !(allowWithoutPrime && state.phase == Phase::IDLE)) ||
      speedMlH <= 0.0f) {
    return false;
  }
  state.phase = Phase::MEASURING;
  state.speedMlH = speedMlH;
  state.startSteps = 0;
  state.stopSteps = 0;
  state.startedAtMs = nowMs;
  state.stoppedAtMs = 0;
  state.measuredVolumeMl = 0.0f;
  state.candidateMlPerRev = 0.0f;
  return true;
}

inline bool stopMeasurement(State &state, uint32_t stopSteps,
                            uint32_t nowMs) {
  if (state.phase != Phase::MEASURING) return false;
  state.stopSteps = stopSteps;
  state.stoppedAtMs = nowMs;
  state.phase = Phase::READY_TO_REVIEW;
  return stopSteps > state.startSteps;
}

inline bool enterMeasuredVolume(State &state, float volumeMl,
                                uint32_t stepsPerRevolution) {
  if (state.phase != Phase::READY_TO_REVIEW || volumeMl <= 0.0f ||
      stepsPerRevolution == 0 || state.stopSteps <= state.startSteps) {
    return false;
  }
  const float revolutions =
      static_cast<float>(state.stopSteps - state.startSteps) /
      static_cast<float>(stepsPerRevolution);
  if (revolutions <= 0.0f) return false;
  state.measuredVolumeMl = volumeMl;
  state.candidateMlPerRev = volumeMl / revolutions;
  state.phase = state.candidateMlPerRev > 0.0f ? Phase::REVIEW : Phase::READY_TO_REVIEW;
  return state.phase == Phase::REVIEW;
}

inline bool retry(State &state) {
  if (state.phase != Phase::REVIEW && state.phase != Phase::READY_TO_REVIEW) {
    return false;
  }
  state.phase = Phase::READY_TO_MEASURE;
  state.startSteps = 0;
  state.stopSteps = 0;
  state.measuredVolumeMl = 0.0f;
  state.candidateMlPerRev = 0.0f;
  return true;
}

inline bool mayCommit(const State &state) {
  return state.phase == Phase::REVIEW && state.candidateMlPerRev > 0.0f;
}

inline bool outputActive(const State &state) {
  return state.phase == Phase::PRIMING || state.phase == Phase::MEASURING;
}

inline void cancel(State &state) { state = State{}; }

} // namespace PumpCalibrationLogic
