#pragma once

#include <cstdint>
#include <cmath>

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
  bool measurementCompleted = false;
  bool stepCounterValid = false;
  float measuredVolumeMl = 0.0f;
  float candidateMlPerRev = 0.0f;
};

enum class ChangeStatus : uint8_t {
  Accepted = 0,
  Invalid,
  OutOfRange,
  ConfirmationRequired,
};

struct TimeFallbackAssessment {
  ChangeStatus status = ChangeStatus::Invalid;
  uint32_t durationMs = 0;
  float expectedVolumeMl = 0.0f;
  float candidateMlPerRev = 0.0f;
  float changeRatio = 0.0f;
};

constexpr uint32_t kTimeFallbackMinDurationMs = 30000;
constexpr float kTimeFallbackMinExpectedVolumeMl = 5.0f;
constexpr float kCalibrationMinChangeRatio = 0.25f;
constexpr float kCalibrationMaxChangeRatio = 4.0f;
constexpr float kCalibrationConfirmMinRatio = 0.5f;
constexpr float kCalibrationConfirmMaxRatio = 2.0f;

inline ChangeStatus assessCalibrationChange(float currentMlPerRev,
                                           float candidateMlPerRev,
                                           bool confirmedLargeChange) {
  if (!std::isfinite(currentMlPerRev) || !std::isfinite(candidateMlPerRev) ||
      currentMlPerRev <= 0.0f || candidateMlPerRev <= 0.0f) {
    return ChangeStatus::Invalid;
  }
  const float ratio = candidateMlPerRev / currentMlPerRev;
  if (ratio < kCalibrationMinChangeRatio ||
      ratio > kCalibrationMaxChangeRatio) {
    return ChangeStatus::OutOfRange;
  }
  if (!confirmedLargeChange &&
      (ratio < kCalibrationConfirmMinRatio ||
       ratio > kCalibrationConfirmMaxRatio)) {
    return ChangeStatus::ConfirmationRequired;
  }
  return ChangeStatus::Accepted;
}

inline TimeFallbackAssessment assessTimeFallback(
    const State &state, float measuredVolumeMl, float currentMlPerRev,
    bool confirmedLargeChange) {
  TimeFallbackAssessment result;
  if (state.phase != Phase::READY_TO_REVIEW || !state.measurementCompleted ||
      !state.stepCounterValid || state.stopSteps != state.startSteps) {
    return result;
  }
  if (!std::isfinite(state.speedMlH) || state.speedMlH <= 0.0f ||
      !std::isfinite(measuredVolumeMl) || measuredVolumeMl <= 0.0f ||
      !std::isfinite(currentMlPerRev) || currentMlPerRev <= 0.0f) {
    return result;
  }

  result.durationMs = state.stoppedAtMs - state.startedAtMs;
  if (result.durationMs < kTimeFallbackMinDurationMs) {
    return result;
  }
  result.expectedVolumeMl =
      state.speedMlH * static_cast<float>(result.durationMs) / 3600000.0f;
  if (!std::isfinite(result.expectedVolumeMl) ||
      result.expectedVolumeMl < kTimeFallbackMinExpectedVolumeMl) {
    return result;
  }

  result.candidateMlPerRev =
      currentMlPerRev * (measuredVolumeMl / result.expectedVolumeMl);
  if (!std::isfinite(result.candidateMlPerRev) ||
      result.candidateMlPerRev <= 0.0f) {
    return result;
  }
  result.changeRatio = result.candidateMlPerRev / currentMlPerRev;
  result.status = assessCalibrationChange(currentMlPerRev,
                                          result.candidateMlPerRev,
                                          confirmedLargeChange);
  return result;
}

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
  state.measurementCompleted = false;
  state.stepCounterValid = false;
  state.measuredVolumeMl = 0.0f;
  state.candidateMlPerRev = 0.0f;
  return true;
}

inline bool stopMeasurement(State &state, uint32_t stopSteps,
                            uint32_t nowMs) {
  if (state.phase != Phase::MEASURING) return false;
  state.stopSteps = stopSteps;
  state.stoppedAtMs = nowMs;
  state.measurementCompleted = true;
  state.stepCounterValid = stopSteps >= state.startSteps;
  state.phase = Phase::READY_TO_REVIEW;
  return state.stepCounterValid;
}

inline bool enterMeasuredVolume(State &state, float volumeMl,
                                uint32_t stepsPerRevolution) {
  if (state.phase != Phase::READY_TO_REVIEW || !state.measurementCompleted ||
      !state.stepCounterValid || volumeMl <= 0.0f || stepsPerRevolution == 0 ||
      state.stopSteps <= state.startSteps) {
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
  state.measurementCompleted = false;
  state.stepCounterValid = false;
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
