#include "pump_calibration.h"

#include <freertos/FreeRTOS.h>

#include "power_command_gateway.h"
#include "../drivers/pump.h"

namespace PumpCalibrationSession {
namespace {

portMUX_TYPE g_calibrationMux = portMUX_INITIALIZER_UNLOCKED;
Snapshot g_calibration;
bool g_measurementStartPending = false;
bool g_measurementStartedApplied = false;
bool g_measurementStopPending = false;
bool g_primingStopPending = false;
bool g_cancelPending = false;

template <typename Callback> bool mutate(Callback callback) {
  portENTER_CRITICAL(&g_calibrationMux);
  const bool changed = callback(g_calibration);
  portEXIT_CRITICAL(&g_calibrationMux);
  return changed;
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
  if (!started) return false;
  const PowerCommandGateway::Command command =
      PowerCommandGateway::setPumpSpeed(
          PowerCommandGateway::Owner::SERVICE,
          PowerCommandGateway::Reason::TFT_CALIBRATION, speedMlH);
  if (!command.accepted) {
    mutate([](Snapshot &state) {
      PumpCalibrationLogic::cancel(state);
      return true;
    });
    return false;
  }
  return true;
}

bool finishPriming() {
  if (getSnapshot().phase != Phase::PRIMING) return false;
  const PowerCommandGateway::Command command =
      PowerCommandGateway::setPumpSpeed(
          PowerCommandGateway::Owner::SERVICE,
          PowerCommandGateway::Reason::TFT_CALIBRATION, 0.0f);
  if (!command.accepted && Pump::isRunning()) return false;
  if (Pump::isRunning()) {
    portENTER_CRITICAL(&g_calibrationMux);
    g_primingStopPending = true;
    portEXIT_CRITICAL(&g_calibrationMux);
    return true;
  }
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
  const PowerCommandGateway::Command command =
      PowerCommandGateway::setPumpSpeed(
          PowerCommandGateway::Owner::SERVICE,
          PowerCommandGateway::Reason::TFT_CALIBRATION, speedMlH);
  if (!command.accepted) {
    mutate([](Snapshot &state) {
      PumpCalibrationLogic::cancel(state);
      return true;
    });
    return false;
  }
  const uint32_t startSteps =
      command.queued ? 0 : Pump::getTotalSteps();
  const bool startedApplied = !command.queued && Pump::isRunning();
  portENTER_CRITICAL(&g_calibrationMux);
  g_measurementStopPending = false;
  g_primingStopPending = false;
  g_cancelPending = false;
  g_measurementStartPending = command.queued;
  g_measurementStartedApplied = startedApplied;
  if (!command.queued) g_calibration.startSteps = startSteps;
  portEXIT_CRITICAL(&g_calibrationMux);
  return true;
}

namespace {

bool captureMeasurementAfterStop() {
  if (Pump::isRunning()) return false;
  const uint32_t stopSteps = Pump::getTotalSteps();
  const bool captured = mutate([=](Snapshot &state) {
    if (state.phase != Phase::MEASURING) return false;
    (void)PumpCalibrationLogic::stopMeasurement(state, stopSteps, millis());
    return true;
  });
  portENTER_CRITICAL(&g_calibrationMux);
  g_measurementStopPending = false;
  g_measurementStartPending = false;
  g_measurementStartedApplied = false;
  portEXIT_CRITICAL(&g_calibrationMux);
  return captured;
}

} // namespace

bool stopMeasurement() {
  if (getSnapshot().phase != Phase::MEASURING) return false;
  const PowerCommandGateway::Command command =
      PowerCommandGateway::setPumpSpeed(
          PowerCommandGateway::Owner::SERVICE,
          PowerCommandGateway::Reason::TFT_CALIBRATION, 0.0f);
  if (!command.accepted && Pump::isRunning()) return false;
  if (Pump::isRunning()) {
    portENTER_CRITICAL(&g_calibrationMux);
    g_measurementStopPending = true;
    portEXIT_CRITICAL(&g_calibrationMux);
    return true;
  }

  bool startedApplied;
  portENTER_CRITICAL(&g_calibrationMux);
  startedApplied = g_measurementStartedApplied;
  portEXIT_CRITICAL(&g_calibrationMux);
  if (!startedApplied) {
    mutate([](Snapshot &state) {
      PumpCalibrationLogic::cancel(state);
      return true;
    });
    portENTER_CRITICAL(&g_calibrationMux);
    g_measurementStartPending = false;
    g_measurementStopPending = false;
    g_measurementStartedApplied = false;
    portEXIT_CRITICAL(&g_calibrationMux);
    return false;
  }
  return captureMeasurementAfterStop();
}

bool isStopPending() {
  portENTER_CRITICAL(&g_calibrationMux);
  const bool pending = g_measurementStopPending || g_primingStopPending ||
                       g_cancelPending;
  portEXIT_CRITICAL(&g_calibrationMux);
  return pending;
}

void update() {
  const Snapshot snapshot = getSnapshot();
  if (snapshot.phase == Phase::MEASURING && Pump::isRunning()) {
    const uint32_t startSteps = Pump::getTotalSteps();
    const uint32_t startedAtMs = millis();
    portENTER_CRITICAL(&g_calibrationMux);
    if (g_measurementStartPending &&
        g_calibration.phase == Phase::MEASURING) {
      g_calibration.startSteps = startSteps;
      g_calibration.startedAtMs = startedAtMs;
      g_measurementStartPending = false;
    }
    g_measurementStartedApplied = true;
    portEXIT_CRITICAL(&g_calibrationMux);
  }
  if (Pump::isRunning()) return;

  bool cancelPending;
  bool primingStopPending;
  bool measurementStopPending;
  portENTER_CRITICAL(&g_calibrationMux);
  cancelPending = g_cancelPending;
  primingStopPending = g_primingStopPending;
  measurementStopPending = g_measurementStopPending;
  g_cancelPending = false;
  g_primingStopPending = false;
  portEXIT_CRITICAL(&g_calibrationMux);

  if (cancelPending) {
    mutate([](Snapshot &state) {
      PumpCalibrationLogic::cancel(state);
      return true;
    });
    portENTER_CRITICAL(&g_calibrationMux);
    g_measurementStartPending = false;
    g_measurementStopPending = false;
    g_measurementStartedApplied = false;
    portEXIT_CRITICAL(&g_calibrationMux);
    return;
  }
  if (primingStopPending) {
    mutate([](Snapshot &state) {
      return PumpCalibrationLogic::finishPriming(state);
    });
  }
  if (measurementStopPending) (void)captureMeasurementAfterStop();
}

bool ensureMeasurementCaptured() {
  const Snapshot before = getSnapshot();
  if ((before.phase == Phase::READY_TO_REVIEW ||
       before.phase == Phase::REVIEW) &&
      before.measurementCompleted) {
    return true;
  }
  if (before.phase == Phase::PRIMING) {
    cancel();
    return false;
  }
  if (before.phase != Phase::MEASURING) {
    return false;
  }

  (void)stopMeasurement();
  const Snapshot after = getSnapshot();
  return (after.phase == Phase::READY_TO_REVIEW ||
          after.phase == Phase::REVIEW) &&
         after.measurementCompleted;
}

bool enterMeasuredVolume(float volumeMl) {
  return mutate([=](Snapshot &state) {
    return PumpCalibrationLogic::enterMeasuredVolume(
        state, volumeMl, Pump::getStepsPerRevolution());
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
  const PowerCommandGateway::Command command =
      PowerCommandGateway::setPumpSpeed(
          PowerCommandGateway::Owner::SERVICE,
          PowerCommandGateway::Reason::TFT_CALIBRATION, 0.0f);
  if (Pump::isRunning()) {
    if (!command.accepted) return;
    portENTER_CRITICAL(&g_calibrationMux);
    g_cancelPending = true;
    portEXIT_CRITICAL(&g_calibrationMux);
    return;
  }
  mutate([](Snapshot &state) {
    PumpCalibrationLogic::cancel(state);
    return true;
  });
  portENTER_CRITICAL(&g_calibrationMux);
  g_measurementStartPending = false;
  g_measurementStartedApplied = false;
  g_measurementStopPending = false;
  g_primingStopPending = false;
  g_cancelPending = false;
  portEXIT_CRITICAL(&g_calibrationMux);
}

void complete() {
  mutate([](Snapshot &state) {
    if (!PumpCalibrationLogic::mayCommit(state)) return false;
    PumpCalibrationLogic::cancel(state);
    return true;
  });
}

} // namespace PumpCalibrationSession
