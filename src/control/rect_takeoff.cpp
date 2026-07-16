#include "rect_takeoff.h"
#include "rect_takeoff_logic.h"

#include <Arduino.h>
#include <math.h>

#include "../drivers/pump.h"
#include "../drivers/valves.h"
#include "../storage/logger.h"

namespace RectTakeoff {
namespace {

RectTakeoffFeedback g_feedback;
float g_sessionVolumeMl = 0.0f;
uint32_t g_lastVolumeUpdateMs = 0;
RectTakeoffFraction g_singleSwitchedTargetFraction =
    RectTakeoffFraction::NONE;
uint32_t g_singleSwitchedRouteChangedAtMs = 0;
uint32_t g_singleSwitchedLastRetargetAtMs = 0;
RectTakeoffFraction g_singleSwitchedBlockedFraction =
    RectTakeoffFraction::NONE;

constexpr uint32_t VALVE_PULSE_PERIOD_MIN_MS = 100UL;
constexpr uint32_t VALVE_PULSE_PERIOD_MAX_MS = 5000UL;
constexpr uint32_t VALVE_PULSE_MIN_OPEN_MAX_MS = 5000UL;

const char* getBackendLabel(RectTakeoffBackendType backendType);

uint32_t getSingleSwitchedSettlingMs() {
  return constrain(static_cast<uint32_t>(g_settings.rectParams.routingSettlingMs),
                   0UL, 10000UL);
}

uint32_t getSingleSwitchedMinRetargetIntervalMs() {
  return constrain(
      static_cast<uint32_t>(g_settings.rectParams.routingRetargetMinMs), 0UL,
      30000UL);
}

uint32_t getValvePulsePeriodMs() {
  return constrain(
      static_cast<uint32_t>(g_settings.rectParams.valvePulsePeriodMs),
      VALVE_PULSE_PERIOD_MIN_MS, VALVE_PULSE_PERIOD_MAX_MS);
}

uint32_t getValvePulseMinOpenMs(uint32_t periodMs) {
  return constrain(
      static_cast<uint32_t>(g_settings.rectParams.valvePulseMinOpenMs), 0UL,
      min(periodMs, VALVE_PULSE_MIN_OPEN_MAX_MS));
}

uint32_t getValvePulseMaxOpenMs(uint32_t periodMs, uint32_t minOpenMs) {
  return constrain(
      static_cast<uint32_t>(g_settings.rectParams.valvePulseMaxOpenMs),
      minOpenMs, periodMs);
}

float getValveReferenceRateMlH(RectTakeoffFraction fraction) {
  switch (fraction) {
  case RectTakeoffFraction::HEADS:
    return g_settings.rectParams.headsSpeedMlHKw;
  case RectTakeoffFraction::BODY:
  case RectTakeoffFraction::TAILS:
    return g_settings.rectParams.bodySpeedMlHKw;
  case RectTakeoffFraction::NONE:
  default:
    return 0.0f;
  }
}

struct ValvePulseState {
  bool valveOpenNow = false;
  bool backendActive = false;
  uint8_t actualDuty = 0;
  float actualEquivalentRateMlH = 0.0f;
};

ValvePulseState buildValvePulseState(const RectTakeoffCommand &command,
                                     RectTakeoffFraction fraction,
                                     bool phaseTakeoffEnabled) {
  ValvePulseState state;
  if (!phaseTakeoffEnabled || fraction == RectTakeoffFraction::NONE) {
    return state;
  }
  if (command.periodicTakeoff && !command.periodicTakeoffActive) {
    return state;
  }

  const float referenceRateMlH = getValveReferenceRateMlH(fraction);
  if (referenceRateMlH <= 0.0f) {
    return state;
  }

  float requestedDuty = command.equivalentRateMlH / referenceRateMlH;
  if (!isfinite(requestedDuty)) {
    requestedDuty = 0.0f;
  }
  if (requestedDuty <= 0.0f) {
    return state;
  }
  if (requestedDuty > 1.0f) {
    requestedDuty = 1.0f;
  }

  const uint32_t periodMs = getValvePulsePeriodMs();
  const uint32_t minOpenMs = getValvePulseMinOpenMs(periodMs);
  const uint32_t maxOpenMs = getValvePulseMaxOpenMs(periodMs, minOpenMs);

  uint32_t openTimeMs = static_cast<uint32_t>(
      lroundf(requestedDuty * static_cast<float>(periodMs)));
  if (openTimeMs == 0 && requestedDuty > 0.0f) {
    openTimeMs = 1;
  }
  if (openTimeMs > 0 && openTimeMs < minOpenMs) {
    openTimeMs = minOpenMs;
  }
  if (openTimeMs > maxOpenMs) {
    openTimeMs = maxOpenMs;
  }
  if (openTimeMs > periodMs) {
    openTimeMs = periodMs;
  }
  if (openTimeMs == 0) {
    return state;
  }

  const float actualDutyRatio =
      static_cast<float>(openTimeMs) / static_cast<float>(periodMs);
  const uint32_t cyclePositionMs = millis() % periodMs;

  state.valveOpenNow = cyclePositionMs < openTimeMs;
  state.backendActive = actualDutyRatio > 0.0f;
  state.actualEquivalentRateMlH = referenceRateMlH * actualDutyRatio;
  state.actualDuty =
      static_cast<uint8_t>(lroundf(actualDutyRatio * 255.0f));
  if (state.actualDuty == 0 && state.backendActive) {
    state.actualDuty = 1;
  }
  return state;
}

RectTakeoffBackendType sanitizeBackend(RectTakeoffBackendType backendType) {
  switch (backendType) {
  case RectTakeoffBackendType::PUMP:
  case RectTakeoffBackendType::VALVE_MULTI:
  case RectTakeoffBackendType::VALVE_SINGLE_SWITCHED:
    return backendType;
  default:
    return RectTakeoffBackendType::PUMP;
  }
}

RectTakeoffFraction sanitizeFraction(RectTakeoffFraction fraction) {
  switch (fraction) {
  case RectTakeoffFraction::NONE:
  case RectTakeoffFraction::HEADS:
  case RectTakeoffFraction::BODY:
  case RectTakeoffFraction::TAILS:
    return fraction;
  default:
    return RectTakeoffFraction::NONE;
  }
}

Fraction toRoutingFraction(RectTakeoffFraction fraction) {
  switch (fraction) {
  case RectTakeoffFraction::HEADS:
    return Fraction::HEADS;
  case RectTakeoffFraction::BODY:
    return Fraction::BODY;
  case RectTakeoffFraction::TAILS:
    return Fraction::TAILS;
  case RectTakeoffFraction::NONE:
  default:
    return Fraction::UNKNOWN;
  }
}

RectTakeoffFraction fromRoutingFraction(Fraction fraction) {
  switch (fraction) {
  case Fraction::HEADS:
    return RectTakeoffFraction::HEADS;
  case Fraction::BODY:
    return RectTakeoffFraction::BODY;
  case Fraction::TAILS:
    return RectTakeoffFraction::TAILS;
  default:
    return RectTakeoffFraction::NONE;
  }
}

bool isSupportedRouteIndex(uint8_t routeIndex) {
  return routeIndex == static_cast<uint8_t>(Fraction::HEADS) ||
         routeIndex == static_cast<uint8_t>(Fraction::BODY) ||
         routeIndex == static_cast<uint8_t>(Fraction::TAILS);
}

RectTakeoffFraction routeIndexToTakeoffFraction(uint8_t routeIndex) {
  switch (static_cast<Fraction>(routeIndex)) {
  case Fraction::HEADS:
    return RectTakeoffFraction::HEADS;
  case Fraction::BODY:
    return RectTakeoffFraction::BODY;
  case Fraction::TAILS:
    return RectTakeoffFraction::TAILS;
  default:
    return RectTakeoffFraction::NONE;
  }
}

bool isRouteSupportedByBackend(RectTakeoffBackendType backendType,
                               uint8_t routeIndex, String* detail) {
  const RectTakeoffBackendType sanitizedBackend = sanitizeBackend(backendType);
  const RectTakeoffFraction fraction = routeIndexToTakeoffFraction(routeIndex);
  auto setDetail = [&](const String& message) {
    if (detail != nullptr) {
      *detail = message;
    }
  };

  if (fraction == RectTakeoffFraction::NONE) {
    setDetail("Fraction program route must target HEADS, BODY, or TAILS.");
    return false;
  }

  switch (sanitizedBackend) {
  case RectTakeoffBackendType::PUMP:
    setDetail("Pump backend accepts HEADS, BODY, and TAILS routes.");
    return true;
  case RectTakeoffBackendType::VALVE_MULTI:
    if (!Valves::isProductValveAvailable(fraction)) {
      setDetail(String("Backend '") + getBackendLabel(sanitizedBackend) +
                "' does not expose the requested route.");
      return false;
    }
    setDetail("Dedicated product valve is available for the requested route.");
    return true;
  case RectTakeoffBackendType::VALVE_SINGLE_SWITCHED:
    if (!Valves::isFractionatorEnabled()) {
      setDetail(String("Backend '") + getBackendLabel(sanitizedBackend) +
                "' requires an enabled fractionator.");
      return false;
    }
    if (!Valves::hasHeadsValve()) {
      setDetail(String("Backend '") + getBackendLabel(sanitizedBackend) +
                "' requires the HEADS valve as the shared takeoff channel.");
      return false;
    }
    setDetail("Shared takeoff valve and fractionator route are available.");
    return true;
  default:
    setDetail("Unknown takeoff backend route.");
    return false;
  }
}

const char* getBackendLabel(RectTakeoffBackendType backendType) {
  switch (backendType) {
  case RectTakeoffBackendType::VALVE_MULTI:
    return "3 valves by fractions";
  case RectTakeoffBackendType::VALVE_SINGLE_SWITCHED:
    return "1 valve with routing switch";
  case RectTakeoffBackendType::PUMP:
  default:
    return "pump";
  }
}

void integrateSessionVolume() {
  const uint32_t now = millis();
  if (g_lastVolumeUpdateMs == 0) {
    g_lastVolumeUpdateMs = now;
    return;
  }

  const uint32_t elapsedMs = now - g_lastVolumeUpdateMs;
  g_lastVolumeUpdateMs = now;
  if (!RectTakeoffLogic::shouldIntegrateVolume(
          g_feedback.backendActive, g_feedback.actualEquivalentRateMlH,
          elapsedMs)) {
    return;
  }

  g_sessionVolumeMl +=
      g_feedback.actualEquivalentRateMlH *
      (static_cast<float>(elapsedMs) / 3600000.0f);
}

void finalizeFeedback() {
  g_feedback.backendType = sanitizeBackend(g_feedback.backendType);
  g_feedback.requestedFraction = sanitizeFraction(g_feedback.requestedFraction);
  g_feedback.routedFraction = sanitizeFraction(g_feedback.routedFraction);
  g_feedback.activeFraction = sanitizeFraction(g_feedback.activeFraction);
  g_feedback.activeValve = sanitizeFraction(g_feedback.activeValve);
  if (g_sessionVolumeMl < 0.0f) {
    g_sessionVolumeMl = 0.0f;
  }
  g_feedback.sessionVolumeMl = g_sessionVolumeMl;
}

void resetFeedback(RectTakeoffBackendType backendType) {
  g_feedback = RectTakeoffFeedback{};
  g_feedback.backendType = sanitizeBackend(backendType);
  g_feedback.sessionVolumeMl = g_sessionVolumeMl;
}

void stopPumpBackend() {
  Pump::stop();
  Valves::closeProductValves();
}

void stopValveMultiBackend() {
  Pump::stop();
  Valves::closeProductValves();
}

void stopValveSingleSwitchedBackend() {
  Pump::stop();
  Valves::setHeads(false);
}

bool requestSingleSwitchedRouting(RectTakeoffFraction fraction) {
  const RectTakeoffFraction sanitizedFraction = sanitizeFraction(fraction);
  if (sanitizedFraction == RectTakeoffFraction::NONE) {
    g_singleSwitchedTargetFraction = RectTakeoffFraction::NONE;
    g_singleSwitchedBlockedFraction = RectTakeoffFraction::NONE;
    return true;
  }

  if (g_singleSwitchedTargetFraction == sanitizedFraction) {
    g_singleSwitchedBlockedFraction = RectTakeoffFraction::NONE;
    return true;
  }

  const uint32_t now = millis();
  const uint32_t minRetargetIntervalMs =
      getSingleSwitchedMinRetargetIntervalMs();
  if (g_singleSwitchedLastRetargetAtMs > 0 &&
      (now - g_singleSwitchedLastRetargetAtMs) < minRetargetIntervalMs) {
    if (g_singleSwitchedBlockedFraction != sanitizedFraction) {
      LOG_W(
          "RectTakeoff: VALVE_SINGLE_SWITCHED delayed retarget to fraction %u "
          "due to anti-chatter window",
          static_cast<unsigned>(sanitizedFraction));
    }
    g_singleSwitchedBlockedFraction = sanitizedFraction;
    return false;
  }

  stopValveSingleSwitchedBackend();
  g_singleSwitchedTargetFraction = sanitizedFraction;
  g_singleSwitchedBlockedFraction = RectTakeoffFraction::NONE;
  g_singleSwitchedLastRetargetAtMs = now;
  g_singleSwitchedRouteChangedAtMs = now;
  Valves::setFraction(toRoutingFraction(sanitizedFraction), true);
  return true;
}

void applyPumpBackend(const RectTakeoffCommand &command) {
  const RectTakeoffFraction requestedFraction =
      sanitizeFraction(command.fraction);
  const bool phaseTakeoffEnabled = command.enabled && !command.fullReflux;
  const bool headsValveOpen =
      phaseTakeoffEnabled && requestedFraction == RectTakeoffFraction::HEADS;
  const bool shouldPumpRun =
      phaseTakeoffEnabled && command.equivalentRateMlH > 0.0f &&
      (!command.periodicTakeoff || command.periodicTakeoffActive);

  Valves::setProductValve(requestedFraction, headsValveOpen);

  if (shouldPumpRun) {
    Pump::start(command.equivalentRateMlH);
  } else {
    Pump::stop();
  }

  resetFeedback(RectTakeoffBackendType::PUMP);
  g_feedback.backendActive = shouldPumpRun;
  g_feedback.routingReady = true;
  g_feedback.actualEquivalentRateMlH =
      shouldPumpRun ? command.equivalentRateMlH : 0.0f;
  g_feedback.actualDuty =
      shouldPumpRun ? 255 : (phaseTakeoffEnabled ? 1 : 0);
  g_feedback.requestedFraction =
      phaseTakeoffEnabled ? requestedFraction : RectTakeoffFraction::NONE;
  g_feedback.routedFraction =
      shouldPumpRun && requestedFraction == RectTakeoffFraction::HEADS
          ? RectTakeoffFraction::HEADS
          : RectTakeoffFraction::NONE;
  g_feedback.activeFraction =
      phaseTakeoffEnabled ? requestedFraction : RectTakeoffFraction::NONE;
  g_feedback.activeValve =
      headsValveOpen ? RectTakeoffFraction::HEADS : RectTakeoffFraction::NONE;
  finalizeFeedback();
}

void applyValveMultiBackend(const RectTakeoffCommand &command) {
  const RectTakeoffFraction requestedFraction =
      sanitizeFraction(command.fraction);
  const bool phaseTakeoffEnabled = command.enabled && !command.fullReflux;

  if (phaseTakeoffEnabled &&
      !Valves::isProductValveAvailable(requestedFraction)) {
    stopValveMultiBackend();
    resetFeedback(RectTakeoffBackendType::VALVE_MULTI);
    g_feedback.routingReady = false;
    g_feedback.requestedFraction = requestedFraction;
    LOG_W("RectTakeoff: VALVE_MULTI fraction %u is unavailable on this board",
          static_cast<unsigned>(requestedFraction));
    finalizeFeedback();
    return;
  }

  const ValvePulseState pulseState =
      buildValvePulseState(command, requestedFraction, phaseTakeoffEnabled);

  Pump::stop();
  Valves::setProductValve(requestedFraction, pulseState.valveOpenNow);

  resetFeedback(RectTakeoffBackendType::VALVE_MULTI);
  g_feedback.backendActive = pulseState.backendActive;
  g_feedback.routingReady = true;
  g_feedback.actualEquivalentRateMlH = pulseState.actualEquivalentRateMlH;
  g_feedback.actualDuty = pulseState.actualDuty;
  g_feedback.requestedFraction =
      phaseTakeoffEnabled ? requestedFraction : RectTakeoffFraction::NONE;
  g_feedback.routedFraction =
      pulseState.backendActive ? requestedFraction : RectTakeoffFraction::NONE;
  g_feedback.activeFraction =
      pulseState.backendActive ? requestedFraction : RectTakeoffFraction::NONE;
  g_feedback.activeValve =
      pulseState.valveOpenNow ? requestedFraction : RectTakeoffFraction::NONE;
  finalizeFeedback();
}

void applyValveSingleSwitchedBackend(const RectTakeoffCommand &command) {
  const RectTakeoffFraction requestedFraction =
      sanitizeFraction(command.fraction);
  const uint32_t now = millis();
  const bool phaseTakeoffEnabled = command.enabled && !command.fullReflux;

  resetFeedback(RectTakeoffBackendType::VALVE_SINGLE_SWITCHED);
  g_feedback.requestedFraction =
      phaseTakeoffEnabled ? requestedFraction : RectTakeoffFraction::NONE;

  if (!phaseTakeoffEnabled || requestedFraction == RectTakeoffFraction::NONE) {
    g_singleSwitchedTargetFraction = RectTakeoffFraction::NONE;
    g_singleSwitchedBlockedFraction = RectTakeoffFraction::NONE;
    stopValveSingleSwitchedBackend();
    finalizeFeedback();
    return;
  }

  if (!Valves::isFractionatorEnabled()) {
    stopValveSingleSwitchedBackend();
    g_feedback.routingReady = false;
    LOG_W("RectTakeoff: VALVE_SINGLE_SWITCHED requires an enabled fractionator");
    finalizeFeedback();
    return;
  }

  const bool retargetAccepted = requestSingleSwitchedRouting(requestedFraction);
  if (!retargetAccepted) {
    stopValveSingleSwitchedBackend();
    g_feedback.routingReady = false;
    g_feedback.routedFraction =
        fromRoutingFraction(Valves::getCurrentFraction());
    finalizeFeedback();
    return;
  }

  const RectTakeoffFraction routedFraction =
      fromRoutingFraction(Valves::getCurrentFraction());
  const bool servoReady =
      !Valves::isServoMoving() && routedFraction == requestedFraction;
  const uint32_t settlingMs = getSingleSwitchedSettlingMs();
  const bool settlingReady =
      g_singleSwitchedRouteChangedAtMs == 0 ||
      (now - g_singleSwitchedRouteChangedAtMs) >= settlingMs;
  const bool routingReady = servoReady && settlingReady;

  g_feedback.routedFraction = routedFraction;
  if (!routingReady) {
    stopValveSingleSwitchedBackend();
    g_feedback.routingReady = false;
    finalizeFeedback();
    return;
  }

  const ValvePulseState pulseState =
      buildValvePulseState(command, requestedFraction, phaseTakeoffEnabled);

  Pump::stop();
  Valves::setHeads(pulseState.valveOpenNow);

  g_feedback.backendActive = pulseState.backendActive;
  g_feedback.routingReady = true;
  g_feedback.actualEquivalentRateMlH = pulseState.actualEquivalentRateMlH;
  g_feedback.actualDuty = pulseState.actualDuty;
  g_feedback.routedFraction = requestedFraction;
  g_feedback.activeFraction =
      pulseState.backendActive ? requestedFraction : RectTakeoffFraction::NONE;
  g_feedback.activeValve =
      pulseState.valveOpenNow ? RectTakeoffFraction::HEADS
                              : RectTakeoffFraction::NONE;
  finalizeFeedback();
}

} // namespace

void beginSession(const Settings& settings) {
  g_sessionVolumeMl = 0.0f;
  g_lastVolumeUpdateMs = millis();
  g_singleSwitchedTargetFraction = RectTakeoffFraction::NONE;
  g_singleSwitchedRouteChangedAtMs = 0;
  g_singleSwitchedLastRetargetAtMs = 0;
  g_singleSwitchedBlockedFraction = RectTakeoffFraction::NONE;
  resetFeedback(settings.rectParams.takeoffBackendType);
  finalizeFeedback();
}

void apply(const RectTakeoffCommand& command) {
  integrateSessionVolume();
  const RectTakeoffBackendType backendType =
      sanitizeBackend(command.backendType);
  switch (backendType) {
  case RectTakeoffBackendType::PUMP:
    applyPumpBackend(command);
    break;
  case RectTakeoffBackendType::VALVE_MULTI:
    applyValveMultiBackend(command);
    break;
  case RectTakeoffBackendType::VALVE_SINGLE_SWITCHED:
    applyValveSingleSwitchedBackend(command);
    break;
  }
  g_feedback.requestedEquivalentRateMlH =
      command.requestedEquivalentRateMlH;
  g_feedback.rateLimited = command.rateLimited;
  finalizeFeedback();
}

void stop() {
  integrateSessionVolume();
  stopPumpBackend();
  g_singleSwitchedTargetFraction = RectTakeoffFraction::NONE;
  g_singleSwitchedRouteChangedAtMs = 0;
  g_singleSwitchedLastRetargetAtMs = 0;
  g_singleSwitchedBlockedFraction = RectTakeoffFraction::NONE;
  resetFeedback(g_feedback.backendType);
  finalizeFeedback();
}

RectTakeoffFeedback getFeedback() { return g_feedback; }

bool requestFractionRoute(RectTakeoffBackendType backendType, uint8_t routeIndex,
                          String* detail) {
  const RectTakeoffBackendType sanitizedBackend = sanitizeBackend(backendType);
  if (!isRouteSupportedByBackend(sanitizedBackend, routeIndex, detail)) {
    return false;
  }

  if (sanitizedBackend != RectTakeoffBackendType::VALVE_SINGLE_SWITCHED) {
    return true;
  }

  Valves::setFraction(static_cast<Fraction>(routeIndex), true);
  if (detail != nullptr) {
    *detail = "Fractionator retarget requested.";
  }
  return true;
}

bool isFractionRouteReady(RectTakeoffBackendType backendType, uint8_t routeIndex) {
  const RectTakeoffBackendType sanitizedBackend = sanitizeBackend(backendType);
  if (!isSupportedRouteIndex(routeIndex)) {
    return false;
  }
  if (sanitizedBackend != RectTakeoffBackendType::VALVE_SINGLE_SWITCHED) {
    return isRouteSupportedByBackend(sanitizedBackend, routeIndex, nullptr);
  }
  return Valves::isFractionatorEnabled() && !Valves::isServoMoving() &&
         static_cast<uint8_t>(Valves::getCurrentFraction()) == routeIndex;
}

bool isFractionRouteSupported(RectTakeoffBackendType backendType, uint8_t routeIndex,
                              String* detail) {
  return isRouteSupportedByBackend(backendType, routeIndex, detail);
}

bool requiresSafeVent(RectTakeoffBackendType backendType) {
  const RectTakeoffBackendType sanitizedBackend = sanitizeBackend(backendType);
  return sanitizedBackend == RectTakeoffBackendType::VALVE_MULTI ||
         sanitizedBackend == RectTakeoffBackendType::VALVE_SINGLE_SWITCHED;
}

bool validateBackendConfiguration(RectTakeoffBackendType backendType,
                                  String* detail) {
  const RectTakeoffBackendType sanitizedBackend = sanitizeBackend(backendType);
  auto setDetail = [&](const String& message) {
    if (detail != nullptr) {
      *detail = message;
    }
  };

  switch (sanitizedBackend) {
  case RectTakeoffBackendType::PUMP:
    setDetail("Takeoff uses the pump backend.");
    return true;
  case RectTakeoffBackendType::VALVE_MULTI: {
    String missing;
    if (!Valves::hasHeadsValve()) missing += missing.isEmpty() ? "HEADS" : ", HEADS";
    if (!Valves::hasBodyValve()) missing += missing.isEmpty() ? "BODY" : ", BODY";
    if (!Valves::hasTailsValve()) missing += missing.isEmpty() ? "TAILS" : ", TAILS";
    if (!missing.isEmpty()) {
      setDetail(String("Backend '") + getBackendLabel(sanitizedBackend) +
                "' requires separate HEADS/BODY/TAILS product valves. Missing: " +
                missing + ".");
      return false;
    }
    setDetail("Separate HEADS/BODY/TAILS valves are available for fraction takeoff.");
    return true;
  }
  case RectTakeoffBackendType::VALVE_SINGLE_SWITCHED:
    if (!Valves::hasHeadsValve()) {
      setDetail(String("Backend '") + getBackendLabel(sanitizedBackend) +
                "' uses the HEADS channel as the shared takeoff valve, but that output is unavailable.");
      return false;
    }
    if (!Valves::isFractionatorEnabled()) {
      setDetail(String("Backend '") + getBackendLabel(sanitizedBackend) +
                "' requires an enabled fractionator for route switching.");
      return false;
    }
    setDetail("Shared takeoff valve and fractionator routing are available.");
    return true;
  default:
    setDetail("Unknown takeoff backend configuration.");
    return false;
  }
}

} // namespace RectTakeoff
