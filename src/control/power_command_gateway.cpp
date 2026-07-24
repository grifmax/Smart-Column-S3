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
  record(Owner::FSM, Target::ALL_OUTPUTS, reason, 0.0f, true);
}

void setHeaterPower(Owner owner, Reason reason, uint8_t percent) {
  const bool safeStop = percent == 0;
  record(owner, Target::HEATER, reason, static_cast<float>(percent), safeStop);
  Heater::setPower(percent);
}

void setPumpSpeed(Owner owner, Reason reason, float mlPerHour) {
  const bool safeStop = mlPerHour <= 0.0f;
  record(owner, Target::PUMP, reason, mlPerHour, safeStop);
  if (safeStop) {
    Pump::stop();
  } else {
    Pump::start(mlPerHour);
  }
}

void setStirrerSpeed(Owner owner, Reason reason, uint8_t percent) {
  const bool safeStop = percent == 0;
  record(owner, Target::STIRRER, reason, static_cast<float>(percent),
         safeStop);
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
  record(owner, targetForValve(valve), reason, open ? 1.0f : 0.0f, !open);
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
  record(owner, targetForProductFraction(fraction), reason, open ? 1.0f : 0.0f,
         !open);
  Valves::setProductValve(fraction, open);
}

void pulseValve(Owner owner, Reason reason, Valve valve, uint32_t durationMs) {
  record(owner, targetForValve(valve), reason, static_cast<float>(durationMs),
         false);
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
  record(owner, Target::START_STOP_PWM, reason, static_cast<float>(duty),
         duty == 0);
  Valves::setStartStop(duty);
}

void setFraction(Owner owner, Reason reason, Fraction fraction, bool smooth) {
  record(owner, Target::FRACTIONATOR, reason,
         static_cast<float>(static_cast<uint8_t>(fraction)), false);
  Valves::setFraction(fraction, smooth);
}

void safeStopAll(Owner owner, Reason reason) {
  record(owner, Target::ALL_OUTPUTS, reason, 0.0f, true);
  Heater::emergencyStop();
  Pump::stop();
  Stirrer::stop();
  Valves::closeAll();
  // closeAll() does not cancel an already interpolated servo move. Holding its
  // actual angle makes safe-stop observable before a paused FSM may resume.
  if (Valves::isFractionatorEnabled() && Valves::isServoMoving()) {
    Valves::setFractionAngle(Valves::getFractionAngle());
  }
  LOG_W("Power gateway: safe-stop applied");
}

} // namespace PowerCommandGateway
