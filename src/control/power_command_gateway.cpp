#include "power_command_gateway.h"

#include <freertos/FreeRTOS.h>

#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/stirrer.h"
#include "drivers/valves.h"
#include "storage/logger.h"

namespace PowerCommandGateway {
namespace {

portMUX_TYPE g_commandMux = portMUX_INITIALIZER_UNLOCKED;
Command g_lastCommand;

Target targetForValve(Valve valve) {
  switch (valve) {
  case Valve::WATER:
    return Target::WATER_VALVE;
  case Valve::HEADS:
    return Target::HEADS_VALVE;
  case Valve::BODY:
    return Target::BODY_VALVE;
  case Valve::TAILS:
    return Target::TAILS_VALVE;
  case Valve::UNO:
    return Target::UNO_VALVE;
  default:
    return Target::ALL_OUTPUTS;
  }
}

Target targetForProductFraction(RectTakeoffFraction fraction) {
  switch (fraction) {
  case RectTakeoffFraction::HEADS:
    return Target::HEADS_VALVE;
  case RectTakeoffFraction::BODY:
    return Target::BODY_VALVE;
  case RectTakeoffFraction::TAILS:
    return Target::TAILS_VALVE;
  case RectTakeoffFraction::NONE:
  default:
    return Target::ALL_OUTPUTS;
  }
}

void record(Owner owner, Target target, Reason reason, float requestedValue,
            bool safeStopApplied) {
  const Command command = makeCommand(owner, target, reason, millis(),
                                      requestedValue, safeStopApplied);
  portENTER_CRITICAL(&g_commandMux);
  g_lastCommand = command;
  portEXIT_CRITICAL(&g_commandMux);
}

} // namespace

Command getLastCommand() {
  Command snapshot;
  portENTER_CRITICAL(&g_commandMux);
  snapshot = g_lastCommand;
  portEXIT_CRITICAL(&g_commandMux);
  return snapshot;
}

bool isSafeStopPending() {
  const Command command = getLastCommand();
  return command.safeStopRequired && !command.safeStopApplied;
}

void noteFsmSetpoint(Target target, Reason reason, float requestedValue) {
  record(Owner::FSM, target, reason, requestedValue, false);
}

void noteFsmSafeStop(Reason reason) {
  record(Owner::FSM, Target::ALL_OUTPUTS, reason, 0.0f,
         safeStopAppliedForRequest(Target::ALL_OUTPUTS, 0.0f));
}

void setHeaterPower(Owner owner, Reason reason, uint8_t percent) {
  const float requestedValue = static_cast<float>(percent);
  const bool safeStop =
      safeStopAppliedForRequest(Target::HEATER, requestedValue);
  record(owner, Target::HEATER, reason, requestedValue, safeStop);
  Heater::setPower(percent);
}

void setHeaterPowerWatts(Owner owner, Reason reason, uint16_t watts) {
  const float requestedValue = static_cast<float>(watts);
  const bool safeStop =
      safeStopAppliedForRequest(Target::HEATER, requestedValue);
  record(owner, Target::HEATER, reason, requestedValue, safeStop);
  Heater::setPowerWatts(watts);
}

void setHeaterBoosterEnabled(Owner owner, Reason reason, bool enabled) {
  const float requestedValue = enabled ? 1.0f : 0.0f;
  // The booster relay is an auxiliary heater channel, not a full heater stop.
  record(owner, Target::HEATER, reason, requestedValue, false);
  Heater::setBoosterEnabled(enabled);
}

void setHeaterTriacDelay(Owner owner, Reason reason, uint16_t delayUs) {
  record(owner, Target::HEATER, reason, static_cast<float>(delayUs), false);
  Heater::setTriacDelay(delayUs);
}

void setPumpSpeed(Owner owner, Reason reason, float mlPerHour) {
  const bool safeStop = safeStopAppliedForRequest(Target::PUMP, mlPerHour);
  record(owner, Target::PUMP, reason, mlPerHour, safeStop);
  if (safeStop) {
    Pump::stop();
  } else {
    Pump::start(mlPerHour);
  }
}

void setStirrerSpeed(Owner owner, Reason reason, uint8_t percent) {
  const float requestedValue = static_cast<float>(percent);
  const bool safeStop =
      safeStopAppliedForRequest(Target::STIRRER, requestedValue);
  record(owner, Target::STIRRER, reason, requestedValue, safeStop);
  if (safeStop) {
    Stirrer::stop();
  } else {
    Stirrer::start(percent);
  }
}

void stopStirrer(Owner owner, Reason reason) {
  setStirrerSpeed(owner, reason, 0);
}

void setValve(Owner owner, Reason reason, Valve valve, bool open) {
  const Target target = targetForValve(valve);
  const float requestedValue = open ? 1.0f : 0.0f;
  record(owner, target, reason, requestedValue,
         safeStopAppliedForRequest(target, requestedValue));
  switch (valve) {
  case Valve::WATER:
    Valves::setWater(open);
    break;
  case Valve::HEADS:
    Valves::setHeads(open);
    break;
  case Valve::BODY:
    Valves::setBody(open);
    break;
  case Valve::TAILS:
    Valves::setTails(open);
    break;
  case Valve::UNO:
    Valves::setUno(open);
    break;
  }
}

void setProductRoute(Owner owner, Reason reason, RectTakeoffFraction fraction,
                     bool open) {
  const Target target = targetForProductFraction(fraction);
  const float requestedValue = open ? 1.0f : 0.0f;
  record(owner, target, reason, requestedValue,
         safeStopAppliedForRequest(target, requestedValue));
  Valves::setProductValve(fraction, open);
}

void closeProductRoutes(Owner owner, Reason reason) {
  record(owner, Target::PRODUCT_ROUTE, reason, 0.0f,
         safeStopAppliedForRequest(Target::PRODUCT_ROUTE, 0.0f));
  Valves::closeProductValves();
}

void closeAllValves(Owner owner, Reason reason) {
  record(owner, Target::ALL_VALVES, reason, 0.0f,
         safeStopAppliedForRequest(Target::ALL_VALVES, 0.0f));
  Valves::closeAll();
}

void pulseValve(Owner owner, Reason reason, Valve valve, uint32_t durationMs) {
  const Target target = targetForValve(valve);
  // Pulse requests carry their own driver-level stop condition.
  record(owner, target, reason, static_cast<float>(durationMs),
         safeStopAppliedForPulse(target));
  switch (valve) {
  case Valve::WATER:
    Valves::pulse(Valves::ValveId::WATER, durationMs);
    break;
  case Valve::HEADS:
    Valves::pulse(Valves::ValveId::HEADS, durationMs);
    break;
  case Valve::BODY:
    Valves::pulse(Valves::ValveId::BODY, durationMs);
    break;
  case Valve::TAILS:
    Valves::pulse(Valves::ValveId::TAILS, durationMs);
    break;
  case Valve::UNO:
    Valves::pulse(Valves::ValveId::UNO, durationMs);
    break;
  }
}

void setCoolingPwm(Owner owner, Reason reason, uint8_t duty) {
  const float requestedValue = static_cast<float>(duty);
  record(owner, Target::START_STOP_PWM, reason, requestedValue,
         safeStopAppliedForRequest(Target::START_STOP_PWM, requestedValue));
  Valves::setStartStop(duty);
}

void setFraction(Owner owner, Reason reason, Fraction fraction, bool smooth) {
  record(owner, Target::FRACTIONATOR, reason,
         static_cast<float>(static_cast<uint8_t>(fraction)), false);
  Valves::setFraction(fraction, smooth);
}

void setFractionAngle(Owner owner, Reason reason, uint8_t angle) {
  record(owner, Target::FRACTIONATOR, reason, static_cast<float>(angle), false);
  Valves::setFractionAngle(angle);
}

void safeStopAll(Owner owner, Reason reason) {
  record(owner, Target::ALL_OUTPUTS, reason, 0.0f,
         safeStopAppliedForRequest(Target::ALL_OUTPUTS, 0.0f));
  Heater::emergencyStop();
  Pump::stop();
  Stirrer::stop();
  Valves::closeAll();
  // closeAll() does not reassert the fractionator hold position on its own.
  // Reapplying the current angle keeps service safe-stop behavior stable and
  // makes the held position observable before a paused FSM may resume.
  if (Valves::isFractionatorEnabled()) {
    Valves::setFractionAngle(Valves::getFractionAngle());
  }
  LOG_W("Power gateway: safe-stop applied");
}

} // namespace PowerCommandGateway
