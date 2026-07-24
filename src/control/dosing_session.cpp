#include "dosing_session.h"

#include <string.h>

#include <freertos/FreeRTOS.h>

#include "../drivers/pump.h"
#include "../drivers/valves.h"
#include "../interface/editable_field_descriptors.h"
#include "../storage/logger.h"
#include "power_command_gateway.h"
#include "pump_calibration.h"
#include "safety.h"
#include "service_test_session.h"

namespace DosingSession {
namespace {

portMUX_TYPE g_dosingMux = portMUX_INITIALIZER_UNLOCKED;
Snapshot g_dosing;

template <typename Callback> bool mutate(Callback callback) {
  portENTER_CRITICAL(&g_dosingMux);
  const bool changed = callback(g_dosing);
  portEXIT_CRITICAL(&g_dosingMux);
  return changed;
}

RectTakeoffFraction toProductFraction(Route route) {
  switch (route) {
  case Route::HEADS:
    return RectTakeoffFraction::HEADS;
  case Route::BODY:
    return RectTakeoffFraction::BODY;
  case Route::TAILS:
    return RectTakeoffFraction::TAILS;
  case Route::NONE:
  default:
    return RectTakeoffFraction::NONE;
  }
}

Route configuredRoute() {
#if BOARD_DOSING_ROUTE == BOARD_DOSING_ROUTE_HEADS
  return Route::HEADS;
#elif BOARD_DOSING_ROUTE == BOARD_DOSING_ROUTE_BODY
  return Route::BODY;
#elif BOARD_DOSING_ROUTE == BOARD_DOSING_ROUTE_TAILS
  return Route::TAILS;
#else
  return Route::NONE;
#endif
}

bool isCalibrationValid(float calibration) {
  const EditableFields::Descriptor &descriptor = EditableFields::get(
      EditableFields::Id::PUMP_CALIBRATION_ML_PER_REV);
  return calibration >= descriptor.minValue && calibration <= descriptor.maxValue;
}

bool isDosingConfigValid(const Snapshot &snapshot) {
  const EditableFields::Descriptor &volume = EditableFields::get(
      EditableFields::Id::DOSING_TARGET_VOLUME_ML);
  const EditableFields::Descriptor &rate = EditableFields::get(
      EditableFields::Id::DOSING_RATE_ML_H);
  return snapshot.route != Route::NONE &&
         snapshot.requestedVolumeMl >= volume.minValue &&
         snapshot.requestedVolumeMl <= volume.maxValue &&
         snapshot.requestedRateMlH >= rate.minValue &&
         snapshot.requestedRateMlH <= rate.maxValue &&
         snapshot.requestedRateMlH <= BOARD_DOSING_MAX_RATE_ML_H;
}

bool routeMatches(const Snapshot &snapshot) {
  const RectTakeoffFraction fraction = toProductFraction(snapshot.route);
  return fraction != RectTakeoffFraction::NONE &&
         Valves::isProductValveAvailable(fraction) &&
         Valves::getActiveProductValve() == fraction;
}

void applyRouteAndPump(const Snapshot &snapshot) {
  PowerCommandGateway::safeStopAll(PowerCommandGateway::Owner::SERVICE,
                                   PowerCommandGateway::Reason::TFT_DOSING);
  PowerCommandGateway::setProductRoute(
      PowerCommandGateway::Owner::SERVICE,
      PowerCommandGateway::Reason::TFT_DOSING,
      toProductFraction(snapshot.route), true);
  if (!routeMatches(snapshot)) return;
  PowerCommandGateway::setPumpSpeed(PowerCommandGateway::Owner::SERVICE,
                                    PowerCommandGateway::Reason::TFT_DOSING,
                                    snapshot.requestedRateMlH);
}

void logSnapshot(const char *event, const Snapshot &snapshot,
                 const char *tone = "info") {
  Logger::logf(
      tone != nullptr && strcmp(tone, "danger") == 0 ? 2
      : (tone != nullptr && strcmp(tone, "warning") == 0 ? 1 : 0),
      "dosing e=%s r=%s v=%.1f q=%.1f c=%.3f a=%.1f s=%lu f=%lu x=%s",
      event != nullptr ? event : "unknown",
      DosingSessionLogic::routeToken(snapshot.route),
      static_cast<double>(snapshot.requestedVolumeMl),
      static_cast<double>(snapshot.requestedRateMlH),
      static_cast<double>(snapshot.calibrationAtStart),
      static_cast<double>(snapshot.actualVolumeMl),
      static_cast<unsigned long>(snapshot.startedAtMs),
      static_cast<unsigned long>(snapshot.finishedAtMs),
      DosingSessionLogic::finishReasonToken(snapshot.finishReason));
}

bool finishActive(FinishReason reason) {
  const Snapshot before = getSnapshot();
  if (!DosingSessionLogic::isOutputActive(before)) return false;
  PowerCommandGateway::safeStopAll(PowerCommandGateway::Owner::SERVICE,
                                   PowerCommandGateway::Reason::TFT_DOSING);
  const float actual = before.phase == Phase::DISPENSING
                           ? Pump::getTotalVolume() - before.startPumpVolumeMl
                           : 0.0f;
  mutate([=](Snapshot &state) {
    DosingSessionLogic::updateActualVolume(state, actual);
    return DosingSessionLogic::finish(state, reason, millis());
  });
  const Snapshot after = getSnapshot();
  const char *event = after.phase == Phase::COMPLETED
                          ? "completed"
                          : (after.phase == Phase::FAULTED ? "faulted"
                                                            : "cancelled");
  logSnapshot(event, after,
              after.phase == Phase::FAULTED
                  ? "danger"
                  : (after.phase == Phase::CANCELLED ? "warning" : "success"));
  return true;
}

} // namespace

Snapshot getSnapshot() {
  Snapshot snapshot;
  portENTER_CRITICAL(&g_dosingMux);
  snapshot = g_dosing;
  portEXIT_CRITICAL(&g_dosingMux);
  return snapshot;
}

RouteInfo getRouteInfo() {
  RouteInfo info;
  info.route = configuredRoute();
  info.available = info.route != Route::NONE &&
                   Valves::isProductValveAvailable(toProductFraction(info.route));
  return info;
}

const char *guardToken(Guard guard) {
  switch (guard) {
  case Guard::ROUTE_UNAVAILABLE:
    return "route_unavailable";
  case Guard::MODE_NOT_IDLE:
    return "mode_not_idle";
  case Guard::SAFETY_BLOCKED:
    return "safety_blocked";
  case Guard::CALIBRATION_INVALID:
    return "calibration_invalid";
  case Guard::CALIBRATION_ACTIVE:
    return "calibration_active";
  case Guard::SERVICE_SESSION_ACTIVE:
    return "service_session_active";
  case Guard::PUMP_BUSY:
    return "pump_busy";
  case Guard::CONFIG_INVALID:
    return "config_invalid";
  case Guard::OK:
  default:
    return "ok";
  }
}

Guard evaluateGuard(const SystemState &state, bool allowOwnedPump) {
  const RouteInfo route = getRouteInfo();
  if (!route.available) return Guard::ROUTE_UNAVAILABLE;
  if (state.mode != Mode::IDLE) return Guard::MODE_NOT_IDLE;
  if (!state.safetyOk || Safety::isLatched(state) ||
      state.currentAlarm.type != AlarmType::NONE || g_settings.demoMode) {
    return Guard::SAFETY_BLOCKED;
  }
  if (!isCalibrationValid(g_settings.pumpCal.mlPerRevolution)) {
    return Guard::CALIBRATION_INVALID;
  }
  if (PumpCalibrationSession::getSnapshot().phase !=
      PumpCalibrationSession::Phase::IDLE) {
    return Guard::CALIBRATION_ACTIVE;
  }
  if (ServiceTestSession::getSnapshot().phase !=
      ServiceTestSession::Phase::CLOSED) {
    return Guard::SERVICE_SESSION_ACTIVE;
  }
  if (!allowOwnedPump && Pump::isRunning()) return Guard::PUMP_BUSY;
  if (!isDosingConfigValid(getSnapshot())) return Guard::CONFIG_INVALID;
  return Guard::OK;
}

bool configure(float requestedVolumeMl, float requestedRateMlH) {
  const RouteInfo route = getRouteInfo();
  if (!route.available) return false;
  const float volume = EditableFields::normalize(
      EditableFields::Id::DOSING_TARGET_VOLUME_ML, requestedVolumeMl);
  const float rate = EditableFields::normalize(EditableFields::Id::DOSING_RATE_ML_H,
                                                requestedRateMlH);
  const bool configured = mutate([=](Snapshot &state) {
    return DosingSessionLogic::configure(state, route.route, volume, rate);
  });
  if (configured) logSnapshot("configured", getSnapshot());
  return configured;
}

bool beginPriming(const SystemState &state) {
  const Guard guard = evaluateGuard(state);
  if (guard != Guard::OK) {
    logSnapshot("rejected", getSnapshot(), "warning");
    return false;
  }
  const float calibration = g_settings.pumpCal.mlPerRevolution;
  const bool started = mutate([=](Snapshot &session) {
    return DosingSessionLogic::beginPriming(session, calibration, millis());
  });
  if (!started) return false;
  const Snapshot snapshot = getSnapshot();
  applyRouteAndPump(snapshot);
  if (!routeMatches(snapshot) || !Pump::isRunning()) {
    (void)finishActive(FinishReason::ROUTE_LOST);
    return false;
  }
  logSnapshot("prime_started", snapshot, "success");
  return true;
}

bool finishPriming() {
  const Snapshot before = getSnapshot();
  if (before.phase != Phase::PRIME) return false;
  PowerCommandGateway::safeStopAll(PowerCommandGateway::Owner::SERVICE,
                                   PowerCommandGateway::Reason::TFT_DOSING);
  const bool finished = mutate([](Snapshot &session) {
    return DosingSessionLogic::finishPriming(session);
  });
  if (finished) logSnapshot("prime_stopped", getSnapshot(), "success");
  return finished;
}

bool beginDispensing(const SystemState &state) {
  const Guard guard = evaluateGuard(state);
  if (guard != Guard::OK) {
    logSnapshot("rejected", getSnapshot(), "warning");
    return false;
  }
  const float calibration = g_settings.pumpCal.mlPerRevolution;
  const float startVolume = Pump::getTotalVolume();
  const bool started = mutate([=](Snapshot &session) {
    return DosingSessionLogic::beginDispensing(session, calibration,
                                                startVolume, millis());
  });
  if (!started) return false;
  const Snapshot snapshot = getSnapshot();
  applyRouteAndPump(snapshot);
  if (!routeMatches(snapshot) || !Pump::isRunning()) {
    (void)finishActive(FinishReason::ROUTE_LOST);
    return false;
  }
  logSnapshot("started", snapshot, "success");
  return true;
}

bool stop(FinishReason reason) { return finishActive(reason); }

void update(const SystemState &state) {
  const Snapshot before = getSnapshot();
  if (!DosingSessionLogic::isOutputActive(before)) return;

  const Guard guard = evaluateGuard(state, true);
  if (guard != Guard::OK) {
    (void)finishActive(guard == Guard::ROUTE_UNAVAILABLE
                           ? FinishReason::ROUTE_LOST
                           : FinishReason::SAFETY);
    return;
  }
  if (!routeMatches(before)) {
    (void)finishActive(FinishReason::ROUTE_LOST);
    return;
  }
  if (static_cast<int32_t>(millis() - before.startedAtMs) >=
      static_cast<int32_t>(BOARD_DOSING_MAX_DURATION_MS)) {
    (void)finishActive(FinishReason::TIMEOUT);
    return;
  }
  if (before.phase != Phase::DISPENSING) return;

  const float actual = Pump::getTotalVolume() - before.startPumpVolumeMl;
  mutate([=](Snapshot &session) {
    DosingSessionLogic::updateActualVolume(session, actual);
    return true;
  });
  if (DosingSessionLogic::targetReached(getSnapshot())) {
    (void)finishActive(FinishReason::TARGET_REACHED);
  }
}

bool outputActive() { return DosingSessionLogic::isOutputActive(getSnapshot()); }

} // namespace DosingSession
