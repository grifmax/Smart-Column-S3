#include "pump_calibration.h"

#include <freertos/FreeRTOS.h>

#include "power_command_gateway.h"
#include "../drivers/pump.h"

namespace PumpCalibrationSession {
namespace {

portMUX_TYPE g_calibrationMux = portMUX_INITIALIZER_UNLOCKED;
Snapshot g_calibration;

template <typename Callback> bool mutate(Callback callback) {
  portENTER_CRITICAL(&g_calibrationMux);
  const bool changed = callback(g_calibration);
  portEXIT_CRITICAL(&g_calibrationMux);
  return changed;
}

constexpr uint32_t kStepsPerRevolution =
    static_cast<uint32_t>(PUMP_STEPS_PER_REV) *
    static_cast<uint32_t>(PUMP_MICROSTEPS);

void setCalibrationPumpSpeed(float speedMlH) {
  PowerCommandGateway::setPumpSpeed(
      PowerCommandGateway::Owner::SERVICE,
      PowerCommandGateway::Reason::TFT_CALIBRATION, speedMlH);
}

} // namespace

Snapshot getSnapshot() {
  Snapshot snapshot;
  portENTER_CRITICAL(&g_calibrationMux);
  snapshot = g_calibration;
  portEXIT_CRITICAL(&g_calibrationMux);
  return snapshot;
}

bool beginPriming(float speedMlH) {
  const bool started = mutate([=](Snapshot &state) {
    return PumpCalibrationLogic::beginPriming(state, speedMlH, millis());
  });
  if (started) setCalibrationPumpSpeed(speedMlH);
  return started;
}

bool finishPriming() {
  setCalibrationPumpSpeed(0.0f);
  return mutate([](Snapshot &state) {
    return PumpCalibrationLogic::finishPriming(state);
  });
}

bool beginMeasurement(float speedMlH, bool allowWithoutPrime) {
  const bool started = mutate([=](Snapshot &state) {
    return PumpCalibrationLogic::beginMeasurement(state, speedMlH, millis(),
                                                   allowWithoutPrime);
  });
  if (!started) return false;
  Pump::resetVolume();
  const uint32_t startSteps = Pump::getTotalSteps();
  portENTER_CRITICAL(&g_calibrationMux);
  g_calibration.startSteps = startSteps;
  portEXIT_CRITICAL(&g_calibrationMux);
  setCalibrationPumpSpeed(speedMlH);
  return true;
}

bool stopMeasurement() {
  setCalibrationPumpSpeed(0.0f);
  const uint32_t stopSteps = Pump::getTotalSteps();
  return mutate([=](Snapshot &state) {
    return PumpCalibrationLogic::stopMeasurement(state, stopSteps, millis());
  });
}

bool ensureMeasurementCaptured() {
  const Snapshot before = getSnapshot();
  if (before.phase == Phase::READY_TO_REVIEW || before.phase == Phase::REVIEW) {
    return true;
  }
  setCalibrationPumpSpeed(0.0f);
  const uint32_t steps = Pump::getTotalSteps();
  return mutate([=](Snapshot &state) {
    if (state.phase == Phase::MEASURING) {
      return PumpCalibrationLogic::stopMeasurement(state, steps, millis());
    }
    if (state.phase == Phase::IDLE || state.phase == Phase::READY_TO_MEASURE) {
      // Сессию потеряли (таймаут, safety, повторное открытие страницы):
      // шаги с последнего сброса ещё в счётчике — замер восстанавливаем.
      state.phase = Phase::READY_TO_REVIEW;
      state.startSteps = 0;
      state.stopSteps = steps;
      state.stoppedAtMs = millis();
      return steps > 0;
    }
    return false;
  });
}

bool enterMeasuredVolume(float volumeMl) {
  return mutate([=](Snapshot &state) {
    return PumpCalibrationLogic::enterMeasuredVolume(
        state, volumeMl, kStepsPerRevolution);
  });
}

bool retry() {
  return mutate([](Snapshot &state) {
    return PumpCalibrationLogic::retry(state);
  });
}

bool mayCommit() { return PumpCalibrationLogic::mayCommit(getSnapshot()); }

float getCandidateMlPerRev() { return getSnapshot().candidateMlPerRev; }

bool outputActive() { return PumpCalibrationLogic::outputActive(getSnapshot()); }

bool hasTimedOut(uint32_t nowMs, uint32_t maxDurationMs) {
  const Snapshot snapshot = getSnapshot();
  return PumpCalibrationLogic::outputActive(snapshot) &&
         static_cast<int32_t>(nowMs - snapshot.startedAtMs) >=
             static_cast<int32_t>(maxDurationMs);
}

void cancel() {
  setCalibrationPumpSpeed(0.0f);
  mutate([](Snapshot &state) {
    PumpCalibrationLogic::cancel(state);
    return true;
  });
}

void complete() {
  mutate([](Snapshot &state) {
    if (!PumpCalibrationLogic::mayCommit(state)) return false;
    PumpCalibrationLogic::cancel(state);
    return true;
  });
}

} // namespace PumpCalibrationSession
