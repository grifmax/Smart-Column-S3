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
  case Valve::UNO:
    return Target::UNO_VALVE;
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

void stopStirrer(Owner owner, Reason reason) {
  record(owner, Target::STIRRER, reason, 0.0f, true);
  Stirrer::stop();
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
  case Valve::UNO:
    Valves::setUno(open);
    break;
  }
}

void safeStopAll(Owner owner, Reason reason) {
  record(owner, Target::ALL_OUTPUTS, reason, 0.0f, true);
  Heater::emergencyStop();
  Pump::stop();
  Stirrer::stop();
  Valves::closeAll();
  LOG_W("Power gateway: safe-stop applied");
}

} // namespace PowerCommandGateway
