#pragma once

#include <stddef.h>

#include "dosing_session_logic.h"
#include "types.h"

// Thread-safe runtime owner of the service dosing workflow. A board must opt
// into a physical route in pins_config.h; the stock V2.0 profile deliberately
// exposes no route.
namespace DosingSession {

using Route = DosingSessionLogic::Route;
using Phase = DosingSessionLogic::Phase;
using FinishReason = DosingSessionLogic::FinishReason;
using Snapshot = DosingSessionLogic::State;

enum class Guard : uint8_t {
  OK = 0,
  ROUTE_UNAVAILABLE,
  MODE_NOT_IDLE,
  SAFETY_BLOCKED,
  CALIBRATION_INVALID,
  CALIBRATION_ACTIVE,
  SERVICE_SESSION_ACTIVE,
  PUMP_BUSY,
  CONFIG_INVALID,
};

struct RouteInfo {
  Route route = Route::NONE;
  bool available = false;
};

Snapshot getSnapshot();
RouteInfo getRouteInfo();
const char *guardToken(Guard guard);
Guard evaluateGuard(const SystemState &state, bool allowOwnedPump = false);

bool configure(float requestedVolumeMl, float requestedRateMlH);
bool beginPriming(const SystemState &state);
bool finishPriming();
bool beginDispensing(const SystemState &state);
bool stop(FinishReason reason);
void update(const SystemState &state);

bool outputActive();

} // namespace DosingSession
