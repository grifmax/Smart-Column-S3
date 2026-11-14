#include "power_command_gateway.h"
#include "power_command_gateway_logic.h"
#include "power_output_state_logic.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/stirrer.h"
#include "drivers/valves.h"
#include "storage/logger.h"
#include "runtime_metrics.h"

namespace PowerCommandGateway {
namespace {

portMUX_TYPE g_commandMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t g_outputMutex = nullptr;
Command g_lastCommand;
bool g_safetyTripLatched = false;
uint32_t g_commandSequence = 0;
TaskHandle_t g_controlTask = nullptr;
Owner g_ownerFloor = Owner::MANUAL;
uint32_t g_droppedCommands = 0;
uint32_t g_cancelledCommands = 0;
uint32_t g_coalescedCommands = 0;
uint32_t g_ownerRejectedCommands = 0;
uint32_t g_safetyRejectedCommands = 0;
uint32_t g_appliedCommands = 0;
uint32_t g_replaySequence = 0;
uint32_t g_replayTimestampMs = 0;
constexpr uint8_t kCommandHistoryCapacity = 32;
Command g_commandHistory[kCommandHistoryCapacity];
uint8_t g_commandHistoryCursor = 0;
constexpr uint8_t kOutputTargetCount =
    static_cast<uint8_t>(Target::ALL_OUTPUTS) + 1;
OutputState g_outputStates[kOutputTargetCount];
using PowerCommandGatewayLogic::Operation;
using PowerCommandGatewayLogic::PendingCommand;
PowerCommandGatewayLogic::PendingQueue g_pendingCommands;

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

const char *rejectReasonToken(RejectReason reason) {
  switch (reason) {
  case RejectReason::OUTPUT_MUTEX_TIMEOUT:
    return "output_mutex_timeout";
  case RejectReason::SAFETY_TRIP_LATCHED:
    return "safety_trip_latched";
  case RejectReason::OWNER_PRIORITY:
    return "owner_priority";
  case RejectReason::COMMAND_QUEUE_FULL:
    return "command_queue_full";
  case RejectReason::SUPERSEDED:
    return "superseded";
  case RejectReason::NONE:
  default:
    return "none";
  }
}

void rememberCommandLocked(const Command &command) {
  if (command.sequence == 0) return;
  for (Command &entry : g_commandHistory) {
    if (entry.sequence == command.sequence) {
      entry = command;
      return;
    }
  }
  g_commandHistory[g_commandHistoryCursor] = command;
  g_commandHistoryCursor = static_cast<uint8_t>(
      (g_commandHistoryCursor + 1) % kCommandHistoryCapacity);
}

void updateOutputStateLocked(const Command &command) {
  if (command.target == Target::ALL_OUTPUTS) {
    for (uint8_t i = 0; i < kOutputTargetCount; ++i) {
      PowerOutputStateLogic::record(g_outputStates[i], command);
    }
    return;
  }
  const uint8_t index = static_cast<uint8_t>(command.target);
  if (index < kOutputTargetCount) {
    PowerOutputStateLogic::record(g_outputStates[index], command);
  }
}

void rejectSupersededLocked(uint32_t sequence) {
  if (sequence == 0) return;
  for (Command &entry : g_commandHistory) {
    if (entry.sequence == sequence && entry.queued) {
      entry.accepted = false;
      entry.queued = false;
      entry.applied = false;
      entry.appliedValue = 0.0f;
      entry.rejectReason = RejectReason::SUPERSEDED;
      return;
    }
  }
}

Command record(Owner owner, Target target, Reason reason, float requestedValue,
               bool safeStopApplied, bool accepted, bool applied,
               RejectReason rejectReason = RejectReason::NONE) {
  Command command = makeCommand(owner, target, reason, millis(),
                                requestedValue, safeStopApplied);
  command.accepted = accepted;
  command.applied = applied;
  command.queued = false;
  command.appliedValue = applied ? requestedValue : 0.0f;
  command.rejectReason = rejectReason;
  portENTER_CRITICAL(&g_commandMux);
  if (g_replaySequence != 0) {
    command.sequence = g_replaySequence;
    command.timestampMs = g_replayTimestampMs;
    if (g_lastCommand.sequence == command.sequence) {
      g_lastCommand = command;
    }
  } else {
    command.sequence = ++g_commandSequence;
    g_lastCommand = command;
  }
  rememberCommandLocked(command);
  updateOutputStateLocked(command);
  if (accepted && applied) {
    ++g_appliedCommands;
  }
  portEXIT_CRITICAL(&g_commandMux);
  if (!accepted) {
    Logger::logf(1,
                 "Output command rejected: seq=%lu owner=%u target=%u "
                 "reason=%s value=%.2f",
                 static_cast<unsigned long>(command.sequence),
                 static_cast<unsigned>(owner),
                 static_cast<unsigned>(target),
                 rejectReasonToken(rejectReason), requestedValue);
  }
  return command;
}

bool shouldQueueForControlTask() {
  return g_controlTask != nullptr &&
         xTaskGetCurrentTaskHandle() != g_controlTask;
}

bool isControlTask() {
  return g_controlTask == nullptr ||
         xTaskGetCurrentTaskHandle() == g_controlTask;
}

void recordQueued(PendingCommand &pending, bool accepted,
                  RejectReason rejectReason) {
  Command &command = pending.command;
  command.accepted = accepted;
  command.applied = false;
  command.queued = accepted;
  command.appliedValue = 0.0f;
  command.rejectReason = rejectReason;

  portENTER_CRITICAL(&g_commandMux);
  command.sequence = ++g_commandSequence;
  if (accepted) {
    const PowerCommandGatewayLogic::EnqueueResult result =
        g_pendingCommands.enqueue(pending);
    g_cancelledCommands += result.cancelled;
    if (result.coalesced) ++g_coalescedCommands;
    if (!result.accepted) {
      accepted = false;
      command.accepted = false;
      command.queued = false;
      command.rejectReason = RejectReason::COMMAND_QUEUE_FULL;
      ++g_droppedCommands;
    } else if (result.coalesced) {
      // The latest id owns the coalesced request; older requests are superseded.
    }
    for (uint8_t i = 0; i < result.cancelledSequenceCount; ++i) {
      rejectSupersededLocked(result.cancelledSequences[i]);
    }
  } else {
    ++g_droppedCommands;
  }
  g_lastCommand = command;
  rememberCommandLocked(command);
  updateOutputStateLocked(command);
  portEXIT_CRITICAL(&g_commandMux);
  if (!command.accepted) {
    Logger::logf(1,
                 "Output command rejected: seq=%lu owner=%u target=%u "
                 "reason=%s value=%.2f",
                 static_cast<unsigned long>(command.sequence),
                 static_cast<unsigned>(command.owner),
                 static_cast<unsigned>(command.target),
                 rejectReasonToken(command.rejectReason),
                 command.requestedValue);
  }
}

bool queueIfOutsideControlTask(PendingCommand pending,
                               Command *queuedCommand = nullptr) {
  if (!shouldQueueForControlTask()) return false;
  bool accepted = true;
  RejectReason rejectReason = RejectReason::NONE;
  portENTER_CRITICAL(&g_commandMux);
  if (g_safetyTripLatched &&
      !safetyTripAllows(true, pending.command.owner,
                        pending.command.target,
                        pending.command.requestedValue)) {
    accepted = false;
    rejectReason = RejectReason::SAFETY_TRIP_LATCHED;
    ++g_safetyRejectedCommands;
  } else if (!ownerFloorAllows(pending.command.owner, g_ownerFloor,
                               pending.command.target,
                               pending.command.requestedValue)) {
    accepted = false;
    rejectReason = RejectReason::OWNER_PRIORITY;
    ++g_ownerRejectedCommands;
  }
  portEXIT_CRITICAL(&g_commandMux);
  recordQueued(pending, accepted, rejectReason);
  if (queuedCommand != nullptr) *queuedCommand = pending.command;
  return true;
}

PendingCommand makePending(Owner owner, Target target, Reason reason,
                           float requestedValue, bool safeStopApplied,
                           Operation operation, uint8_t argument = 0,
                           bool flag = false, uint32_t durationMs = 0,
                           bool smooth = true) {
  PendingCommand pending;
  pending.command = makeCommand(owner, target, reason, millis(),
                                requestedValue, safeStopApplied);
  pending.operation = operation;
  pending.argument = argument;
  pending.flag = flag;
  pending.durationMs = durationMs;
  pending.smooth = smooth;
  return pending;
}

bool tripLatched() {
  portENTER_CRITICAL(&g_commandMux);
  const bool latched = g_safetyTripLatched;
  portEXIT_CRITICAL(&g_commandMux);
  return latched;
}

Owner ownerFloor() {
  portENTER_CRITICAL(&g_commandMux);
  const Owner floor = g_ownerFloor;
  portEXIT_CRITICAL(&g_commandMux);
  return floor;
}

void applySafeStopAll();

template <typename Apply>
Command applyRequest(Owner owner, Target target, Reason reason,
                     float requestedValue, bool safeStopApplied,
                     Apply apply) {
  if (g_outputMutex == nullptr ||
      xSemaphoreTake(g_outputMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return record(owner, target, reason, requestedValue, safeStopApplied,
                  false, false, RejectReason::OUTPUT_MUTEX_TIMEOUT);
  }

  bool accepted = false;
  RejectReason rejectReason = RejectReason::NONE;
  if (!safetyTripAllows(tripLatched(), owner, target, requestedValue)) {
    rejectReason = RejectReason::SAFETY_TRIP_LATCHED;
  } else if (!ownerFloorAllows(owner, ownerFloor(), target,
                               requestedValue)) {
    rejectReason = RejectReason::OWNER_PRIORITY;
  } else {
    accepted = true;
  }
  bool applied = false;
  if (accepted) {
    apply();
    applied = true;
    // A safety trip can latch while a driver call is in progress if the
    // emergency path cannot take the mutex immediately. Reassert the safe
    // state before releasing ownership of the output bus.
    if (owner != Owner::SAFETY && tripLatched() &&
        !isDeenergizingRequest(target, requestedValue)) {
      applySafeStopAll();
      accepted = false;
      applied = false;
      rejectReason = RejectReason::SAFETY_TRIP_LATCHED;
    }
  }
  const Command result = record(owner, target, reason, requestedValue,
                                safeStopApplied, accepted, applied,
                                rejectReason);
  if (rejectReason == RejectReason::SAFETY_TRIP_LATCHED) {
    portENTER_CRITICAL(&g_commandMux);
    ++g_safetyRejectedCommands;
    portEXIT_CRITICAL(&g_commandMux);
  } else if (rejectReason == RejectReason::OWNER_PRIORITY) {
    portENTER_CRITICAL(&g_commandMux);
    ++g_ownerRejectedCommands;
    portEXIT_CRITICAL(&g_commandMux);
  }
  xSemaphoreGive(g_outputMutex);
  return result;
}

void applySafeStopAll() {
  Heater::emergencyStop();
  Pump::stop();
  Stirrer::stop();
  Valves::closeAll();
  if (Valves::isFractionatorEnabled()) {
    Valves::setFractionAngle(Valves::getFractionAngle());
  }
}

} // namespace

bool init() {
  if (g_outputMutex != nullptr) return true;
  g_outputMutex = xSemaphoreCreateMutex();
  g_pendingCommands.clear();
  return g_outputMutex != nullptr;
}

void setControlTaskToCurrent() {
  portENTER_CRITICAL(&g_commandMux);
  g_controlTask = xTaskGetCurrentTaskHandle();
  portEXIT_CRITICAL(&g_commandMux);
}

void setOwnerFloor(Owner owner) {
  portENTER_CRITICAL(&g_commandMux);
  g_ownerFloor = owner;
  portEXIT_CRITICAL(&g_commandMux);
}

uint16_t pendingCommandCount() {
  portENTER_CRITICAL(&g_commandMux);
  const uint16_t count = g_pendingCommands.pending();
  portEXIT_CRITICAL(&g_commandMux);
  return count;
}

uint32_t droppedCommandCount() {
  portENTER_CRITICAL(&g_commandMux);
  const uint32_t count = g_droppedCommands;
  portEXIT_CRITICAL(&g_commandMux);
  return count;
}

uint32_t cancelledCommandCount() {
  portENTER_CRITICAL(&g_commandMux);
  const uint32_t count = g_cancelledCommands;
  portEXIT_CRITICAL(&g_commandMux);
  return count;
}

uint32_t coalescedCommandCount() {
  portENTER_CRITICAL(&g_commandMux);
  const uint32_t count = g_coalescedCommands;
  portEXIT_CRITICAL(&g_commandMux);
  return count;
}

uint32_t ownerRejectedCommandCount() {
  portENTER_CRITICAL(&g_commandMux);
  const uint32_t count = g_ownerRejectedCommands;
  portEXIT_CRITICAL(&g_commandMux);
  return count;
}

uint32_t safetyRejectedCommandCount() {
  portENTER_CRITICAL(&g_commandMux);
  const uint32_t count = g_safetyRejectedCommands;
  portEXIT_CRITICAL(&g_commandMux);
  return count;
}

uint32_t appliedCommandCount() {
  portENTER_CRITICAL(&g_commandMux);
  const uint32_t count = g_appliedCommands;
  portEXIT_CRITICAL(&g_commandMux);
  return count;
}

Command getLastCommand() {
  Command snapshot;
  portENTER_CRITICAL(&g_commandMux);
  snapshot = g_lastCommand;
  portEXIT_CRITICAL(&g_commandMux);
  return snapshot;
}

bool getCommandStatus(uint32_t commandId, Command &out) {
  if (commandId == 0) return false;
  bool found = false;
  portENTER_CRITICAL(&g_commandMux);
  for (const Command &entry : g_commandHistory) {
    if (entry.sequence == commandId) {
      out = entry;
      found = true;
      break;
    }
  }
  portEXIT_CRITICAL(&g_commandMux);
  return found;
}

const char *rejectReasonString(RejectReason reason) {
  return rejectReasonToken(reason);
}

bool getOutputState(Target target, OutputState &out) {
  const uint8_t index = static_cast<uint8_t>(target);
  if (index >= kOutputTargetCount) return false;
  portENTER_CRITICAL(&g_commandMux);
  out = g_outputStates[index];
  portEXIT_CRITICAL(&g_commandMux);
  return true;
}

uint8_t processPendingCommands(uint8_t maxCommands) {
  if (maxCommands == 0 || !isControlTask()) return 0;
  uint8_t processed = 0;
  while (processed < maxCommands) {
    PendingCommand pending;
    portENTER_CRITICAL(&g_commandMux);
    if (!g_pendingCommands.dequeue(pending)) {
      portEXIT_CRITICAL(&g_commandMux);
      break;
    }
    g_replaySequence = pending.command.sequence;
    g_replayTimestampMs = pending.command.timestampMs;
    portEXIT_CRITICAL(&g_commandMux);

    switch (pending.operation) {
    case Operation::HeaterPercent:
      setHeaterPower(pending.command.owner, pending.command.reason,
                     static_cast<uint8_t>(pending.command.requestedValue));
      break;
    case Operation::HeaterWatts:
      setHeaterPowerWatts(pending.command.owner, pending.command.reason,
                          static_cast<uint16_t>(pending.command.requestedValue));
      break;
    case Operation::HeaterBooster:
      setHeaterBoosterEnabled(pending.command.owner, pending.command.reason,
                              pending.flag);
      break;
    case Operation::HeaterTriacDelay:
      setHeaterTriacDelay(pending.command.owner, pending.command.reason,
                          static_cast<uint16_t>(pending.command.requestedValue));
      break;
    case Operation::PumpSpeed:
      setPumpSpeed(pending.command.owner, pending.command.reason,
                   pending.command.requestedValue);
      break;
    case Operation::StirrerSpeed:
      setStirrerSpeed(pending.command.owner, pending.command.reason,
                      static_cast<uint8_t>(pending.command.requestedValue));
      break;
    case Operation::Valve:
      setValve(pending.command.owner, pending.command.reason,
               static_cast<Valve>(pending.argument), pending.flag);
      break;
    case Operation::ProductRoute:
      setProductRoute(pending.command.owner, pending.command.reason,
                      static_cast<RectTakeoffFraction>(pending.argument),
                      pending.flag);
      break;
    case Operation::CloseProductRoutes:
      closeProductRoutes(pending.command.owner, pending.command.reason);
      break;
    case Operation::CloseAllValves:
      closeAllValves(pending.command.owner, pending.command.reason);
      break;
    case Operation::PulseValve:
      pulseValve(pending.command.owner, pending.command.reason,
                 static_cast<Valve>(pending.argument), pending.durationMs);
      break;
    case Operation::CoolingPwm:
      setCoolingPwm(pending.command.owner, pending.command.reason,
                    static_cast<uint8_t>(pending.command.requestedValue));
      break;
    case Operation::Fraction:
      setFraction(pending.command.owner, pending.command.reason,
                  static_cast<Fraction>(pending.argument), pending.smooth);
      break;
    case Operation::FractionAngle:
      setFractionAngle(pending.command.owner, pending.command.reason,
                       static_cast<uint8_t>(pending.command.requestedValue));
      break;
    case Operation::SafeStopAll:
      safeStopAll(pending.command.owner, pending.command.reason);
      break;
    }

    portENTER_CRITICAL(&g_commandMux);
    g_replaySequence = 0;
    g_replayTimestampMs = 0;
    portEXIT_CRITICAL(&g_commandMux);
    ++processed;
    if (pending.operation == Operation::SafeStopAll ||
        (pending.operation == Operation::PumpSpeed &&
         pending.command.requestedValue <= 0.0f)) {
      break;
    }
  }
  return processed;
}

bool isSafeStopPending() {
  const Command command = getLastCommand();
  return command.safeStopRequired && !command.safeStopApplied;
}

bool isSafetyTripLatched() { return tripLatched(); }

void tripSafety(Reason reason) {
  if (g_outputMutex == nullptr ||
      xSemaphoreTake(g_outputMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    portENTER_CRITICAL(&g_commandMux);
    const bool alreadyLatched = g_safetyTripLatched;
    g_safetyTripLatched = true;
    if (!alreadyLatched) {
      PowerCommandGatewayLogic::EnqueueResult cancelled;
      g_cancelledCommands += g_pendingCommands.clear(&cancelled);
      for (uint8_t i = 0; i < cancelled.cancelledSequenceCount; ++i) {
        rejectSupersededLocked(cancelled.cancelledSequences[i]);
      }
    }
    portEXIT_CRITICAL(&g_commandMux);
    if (alreadyLatched) {
      ControlRuntimeMetrics::recordRepeatedSafetyTrip();
      return;
    }
    applySafeStopAll();
    record(Owner::SAFETY, Target::ALL_OUTPUTS, reason, 0.0f, true, true,
           true);
    return;
  }

  portENTER_CRITICAL(&g_commandMux);
  const bool alreadyLatched = g_safetyTripLatched;
  g_safetyTripLatched = true;
  if (!alreadyLatched) {
    PowerCommandGatewayLogic::EnqueueResult cancelled;
    g_cancelledCommands += g_pendingCommands.clear(&cancelled);
    for (uint8_t i = 0; i < cancelled.cancelledSequenceCount; ++i) {
      rejectSupersededLocked(cancelled.cancelledSequences[i]);
    }
  }
  portEXIT_CRITICAL(&g_commandMux);
  if (alreadyLatched) {
    ControlRuntimeMetrics::recordRepeatedSafetyTrip();
    xSemaphoreGive(g_outputMutex);
    return;
  }
  applySafeStopAll();
  record(Owner::SAFETY, Target::ALL_OUTPUTS, reason, 0.0f, true, true, true);
  xSemaphoreGive(g_outputMutex);
  LOG_W("Power gateway: safety trip latched");
}

void clearSafetyTrip() {
  if (g_outputMutex == nullptr ||
      xSemaphoreTake(g_outputMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }
  portENTER_CRITICAL(&g_commandMux);
  g_safetyTripLatched = false;
  portEXIT_CRITICAL(&g_commandMux);
  xSemaphoreGive(g_outputMutex);
}

void noteFsmSetpoint(Target target, Reason reason, float requestedValue) {
  record(Owner::FSM, target, reason, requestedValue, false, true, false);
}

void noteFsmSafeStop(Reason reason) {
  record(Owner::FSM, Target::ALL_OUTPUTS, reason, 0.0f,
         safeStopAppliedForRequest(Target::ALL_OUTPUTS, 0.0f), true, false);
}

void setHeaterPower(Owner owner, Reason reason, uint8_t percent) {
  const float requestedValue = static_cast<float>(percent);
  const bool safeStop =
      safeStopAppliedForRequest(Target::HEATER, requestedValue);
  if (queueIfOutsideControlTask(makePending(
          owner, Target::HEATER, reason, requestedValue, safeStop,
          Operation::HeaterPercent))) {
    return;
  }
  applyRequest(owner, Target::HEATER, reason, requestedValue, safeStop,
               [percent]() { Heater::setPower(percent); });
}

Command setHeaterPowerWatts(Owner owner, Reason reason, uint16_t watts) {
  const float requestedValue = static_cast<float>(watts);
  const bool safeStop =
      safeStopAppliedForRequest(Target::HEATER, requestedValue);
  Command queuedCommand;
  if (queueIfOutsideControlTask(makePending(
          owner, Target::HEATER, reason, requestedValue, safeStop,
          Operation::HeaterWatts), &queuedCommand)) {
    return queuedCommand;
  }
  return applyRequest(owner, Target::HEATER, reason, requestedValue, safeStop,
               [watts]() { Heater::setPowerWatts(watts); });
}

void setHeaterBoosterEnabled(Owner owner, Reason reason, bool enabled) {
  const float requestedValue = enabled ? 1.0f : 0.0f;
  if (queueIfOutsideControlTask(makePending(
          owner, Target::HEATER, reason, requestedValue, false,
          Operation::HeaterBooster, 0, enabled))) {
    return;
  }
  applyRequest(owner, Target::HEATER, reason, requestedValue, false,
               [enabled]() { Heater::setBoosterEnabled(enabled); });
}

void setHeaterTriacDelay(Owner owner, Reason reason, uint16_t delayUs) {
  const float requestedValue = static_cast<float>(delayUs);
  if (queueIfOutsideControlTask(makePending(
          owner, Target::HEATER_TRIAC_DELAY, reason, requestedValue, false,
          Operation::HeaterTriacDelay))) {
    return;
  }
  applyRequest(owner, Target::HEATER_TRIAC_DELAY, reason,
               requestedValue, false,
               [delayUs]() { Heater::setTriacDelay(delayUs); });
}

Command setPumpSpeed(Owner owner, Reason reason, float mlPerHour) {
  const bool safeStop = safeStopAppliedForRequest(Target::PUMP, mlPerHour);
  PendingCommand pending = makePending(owner, Target::PUMP, reason, mlPerHour,
                                       safeStop, Operation::PumpSpeed);
  Command queuedCommand;
  if (queueIfOutsideControlTask(pending, &queuedCommand)) return queuedCommand;
  return applyRequest(owner, Target::PUMP, reason, mlPerHour, safeStop,
                      [mlPerHour, safeStop]() {
                        if (safeStop) {
                          Pump::stop();
                        } else {
                          Pump::start(mlPerHour);
                        }
                      });
}

Command setStirrerSpeed(Owner owner, Reason reason, uint8_t percent) {
  const float requestedValue = static_cast<float>(percent);
  const bool safeStop =
      safeStopAppliedForRequest(Target::STIRRER, requestedValue);
  Command queuedCommand;
  if (queueIfOutsideControlTask(makePending(
          owner, Target::STIRRER, reason, requestedValue, safeStop,
          Operation::StirrerSpeed), &queuedCommand)) {
    return queuedCommand;
  }
  return applyRequest(owner, Target::STIRRER, reason, requestedValue, safeStop,
               [percent, safeStop]() {
                 if (safeStop) {
                   Stirrer::stop();
                 } else {
                   Stirrer::start(percent);
                 }
               });
}

Command stopStirrer(Owner owner, Reason reason) {
  return setStirrerSpeed(owner, reason, 0);
}

Command setValve(Owner owner, Reason reason, Valve valve, bool open) {
  const Target target = targetForValve(valve);
  const float requestedValue = open ? 1.0f : 0.0f;
  Command queuedCommand;
  if (queueIfOutsideControlTask(makePending(
          owner, target, reason, requestedValue,
          safeStopAppliedForRequest(target, requestedValue), Operation::Valve,
          static_cast<uint8_t>(valve), open), &queuedCommand)) {
    return queuedCommand;
  }
  return applyRequest(owner, target, reason, requestedValue,
               safeStopAppliedForRequest(target, requestedValue),
               [valve, open]() {
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
               });
}

void setProductRoute(Owner owner, Reason reason, RectTakeoffFraction fraction,
                     bool open) {
  const Target target = targetForProductFraction(fraction);
  const float requestedValue = open ? 1.0f : 0.0f;
  if (queueIfOutsideControlTask(makePending(
          owner, target, reason, requestedValue,
          safeStopAppliedForRequest(target, requestedValue),
          Operation::ProductRoute, static_cast<uint8_t>(fraction), open))) {
    return;
  }
  applyRequest(owner, target, reason, requestedValue,
               safeStopAppliedForRequest(target, requestedValue),
               [fraction, open]() { Valves::setProductValve(fraction, open); });
}

void closeProductRoutes(Owner owner, Reason reason) {
  if (queueIfOutsideControlTask(makePending(
          owner, Target::PRODUCT_ROUTE, reason, 0.0f, true,
          Operation::CloseProductRoutes))) {
    return;
  }
  applyRequest(owner, Target::PRODUCT_ROUTE, reason, 0.0f, true,
               []() { Valves::closeProductValves(); });
}

Command closeAllValves(Owner owner, Reason reason) {
  Command queuedCommand;
  if (queueIfOutsideControlTask(makePending(
          owner, Target::ALL_VALVES, reason, 0.0f, true,
          Operation::CloseAllValves), &queuedCommand)) {
    return queuedCommand;
  }
  return applyRequest(owner, Target::ALL_VALVES, reason, 0.0f, true,
               []() { Valves::closeAll(); });
}

Command pulseValve(Owner owner, Reason reason, Valve valve, uint32_t durationMs) {
  const Target target = targetForValve(valve);
  Command queuedCommand;
  if (queueIfOutsideControlTask(makePending(
          owner, target, reason, static_cast<float>(durationMs),
          safeStopAppliedForPulse(target), Operation::PulseValve,
          static_cast<uint8_t>(valve), false, durationMs), &queuedCommand)) {
    return queuedCommand;
  }
  return applyRequest(owner, target, reason, static_cast<float>(durationMs),
               safeStopAppliedForPulse(target), [valve, durationMs]() {
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
               });
}

Command setCoolingPwm(Owner owner, Reason reason, uint8_t duty) {
  const float requestedValue = static_cast<float>(duty);
  Command queuedCommand;
  if (queueIfOutsideControlTask(makePending(
          owner, Target::START_STOP_PWM, reason, requestedValue,
          safeStopAppliedForRequest(Target::START_STOP_PWM, requestedValue),
          Operation::CoolingPwm), &queuedCommand)) {
    return queuedCommand;
  }
  return applyRequest(owner, Target::START_STOP_PWM, reason, requestedValue,
               safeStopAppliedForRequest(Target::START_STOP_PWM,
                                         requestedValue),
               [duty]() { Valves::setStartStop(duty); });
}

Command setFraction(Owner owner, Reason reason, Fraction fraction, bool smooth) {
  const float requestedValue =
      static_cast<float>(static_cast<uint8_t>(fraction));
  Command queuedCommand;
  if (queueIfOutsideControlTask(makePending(
          owner, Target::FRACTIONATOR, reason, requestedValue, false,
          Operation::Fraction, static_cast<uint8_t>(fraction), false, 0,
          smooth), &queuedCommand)) {
    return queuedCommand;
  }
  return applyRequest(owner, Target::FRACTIONATOR, reason, requestedValue, false,
               [fraction, smooth]() { Valves::setFraction(fraction, smooth); });
}

Command setFractionAngle(Owner owner, Reason reason, uint8_t angle) {
  const float requestedValue = static_cast<float>(angle);
  Command queuedCommand;
  if (queueIfOutsideControlTask(makePending(
          owner, Target::FRACTIONATOR, reason, requestedValue, false,
          Operation::FractionAngle), &queuedCommand)) {
    return queuedCommand;
  }
  return applyRequest(owner, Target::FRACTIONATOR, reason, requestedValue, false,
               [angle]() { Valves::setFractionAngle(angle); });
}

Command safeStopAll(Owner owner, Reason reason) {
  Command queuedCommand;
  if (queueIfOutsideControlTask(makePending(
          owner, Target::ALL_OUTPUTS, reason, 0.0f, true,
          Operation::SafeStopAll), &queuedCommand)) {
    return queuedCommand;
  }
  if (g_outputMutex == nullptr ||
      xSemaphoreTake(g_outputMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    applySafeStopAll();
    return record(owner, Target::ALL_OUTPUTS, reason, 0.0f, true, true, true);
  }
  applySafeStopAll();
  Command command = record(owner, Target::ALL_OUTPUTS, reason, 0.0f, true,
                           true, true);
  xSemaphoreGive(g_outputMutex);
  LOG_W("Power gateway: safe-stop applied");
  return command;
}

} // namespace PowerCommandGateway
