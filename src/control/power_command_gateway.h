#pragma once

#include <stdint.h>

enum class Fraction : uint8_t;
enum class RectTakeoffFraction : uint8_t;

// The TFT must not call power drivers directly. This gateway is its single
// command boundary and leaves an auditable record of who requested an output,
// why, when, and whether the required safe-stop has happened.
namespace PowerCommandGateway {

enum class Owner : uint8_t {
  SAFETY = 0,
  FSM,
  SERVICE,
  MANUAL,
};

enum class Target : uint8_t {
  HEATER = 0,
  HEATER_TRIAC_DELAY,
  PUMP,
  STIRRER,
  WATER_VALVE,
  HEADS_VALVE,
  BODY_VALVE,
  TAILS_VALVE,
  UNO_VALVE,
  PRODUCT_ROUTE,
  ALL_VALVES,
  START_STOP_PWM,
  FRACTIONATOR,
  ALL_OUTPUTS,
};

enum class Valve : uint8_t {
  WATER = 0,
  HEADS,
  BODY,
  TAILS,
  UNO,
};

enum class Reason : uint8_t {
  TFT_RUNTIME_SETPOINT = 0,
  TFT_MANUAL_OUTPUT,
  TFT_SERVICE_SETTING,
  TFT_SERVICE_TEST,
  TFT_CALIBRATION,
  TFT_DOSING,
  FSM_CONTROL,
  FSM_AUTOCLAVE,
  TFT_PROCESS_PAUSE,
  TFT_PROCESS_STOP,
  SAFETY_TRIP,
};

enum class RejectReason : uint8_t {
  NONE = 0,
  OUTPUT_MUTEX_TIMEOUT,
  SAFETY_TRIP_LATCHED,
  OWNER_PRIORITY,
  COMMAND_QUEUE_FULL,
  SUPERSEDED,
};

struct Command {
  Owner owner = Owner::FSM;
  Target target = Target::ALL_OUTPUTS;
  Reason reason = Reason::TFT_RUNTIME_SETPOINT;
  uint32_t timestampMs = 0;
  float requestedValue = 0.0f;
  bool safeStopRequired = true;
  bool safeStopApplied = false;
  bool accepted = true;
  bool applied = false;
  bool queued = false;
  float appliedValue = 0.0f;
  RejectReason rejectReason = RejectReason::NONE;
  uint32_t sequence = 0;
};

// Gateway's software view of an output target. `appliedValue` means the driver
// call returned successfully; it is not independent electrical feedback.
struct OutputState {
  uint32_t requestedCommandId = 0;
  uint32_t requestedAtMs = 0;
  float requestedValue = 0.0f;
  bool requestAccepted = false;
  bool requestQueued = false;
  RejectReason rejectReason = RejectReason::NONE;
  uint32_t appliedCommandId = 0;
  uint32_t appliedAtMs = 0;
  float appliedValue = 0.0f;
  bool hasAppliedValue = false;
};

constexpr bool requiresSafeStop(Target) { return true; }

inline bool safeStopAppliedForRequest(Target target, float requestedValue) {
  switch (target) {
  case Target::HEATER:
  case Target::PUMP:
  case Target::STIRRER:
  case Target::WATER_VALVE:
  case Target::HEADS_VALVE:
  case Target::BODY_VALVE:
  case Target::TAILS_VALVE:
  case Target::UNO_VALVE:
  case Target::PRODUCT_ROUTE:
  case Target::ALL_VALVES:
  case Target::START_STOP_PWM:
    return requestedValue <= 0.0f;
  case Target::HEATER_TRIAC_DELAY:
  case Target::FRACTIONATOR:
    return false;
  case Target::ALL_OUTPUTS:
    return true;
  default:
    return false;
  }
}

inline bool safeStopAppliedForPulse(Target target) {
  switch (target) {
  case Target::WATER_VALVE:
  case Target::HEADS_VALVE:
  case Target::BODY_VALVE:
  case Target::TAILS_VALVE:
  case Target::UNO_VALVE:
    return true;
  default:
    return false;
  }
}

inline bool isDeenergizingRequest(Target target, float requestedValue) {
  switch (target) {
  case Target::HEATER:
  case Target::PUMP:
  case Target::STIRRER:
  case Target::WATER_VALVE:
  case Target::HEADS_VALVE:
  case Target::BODY_VALVE:
  case Target::TAILS_VALVE:
  case Target::UNO_VALVE:
  case Target::PRODUCT_ROUTE:
  case Target::ALL_VALVES:
  case Target::START_STOP_PWM:
  case Target::ALL_OUTPUTS:
    return requestedValue <= 0.0f;
  case Target::FRACTIONATOR:
  default:
    return false;
  }
}

inline uint8_t ownerPriority(Owner owner) {
  switch (owner) {
  case Owner::SAFETY:
    return 4;
  case Owner::FSM:
    return 3;
  case Owner::SERVICE:
    return 2;
  case Owner::MANUAL:
  default:
    return 1;
  }
}

inline bool ownerMeetsFloor(Owner owner, Owner floor) {
  return ownerPriority(owner) >= ownerPriority(floor);
}

inline bool ownerFloorAllows(Owner owner, Owner floor, Target target,
                             float requestedValue) {
  return ownerMeetsFloor(owner, floor) ||
         isDeenergizingRequest(target, requestedValue);
}

inline bool safetyTripAllows(bool safetyTripLatched, Owner owner,
                             Target target, float requestedValue) {
  return !safetyTripLatched || owner == Owner::SAFETY ||
         isDeenergizingRequest(target, requestedValue);
}

inline Command makeCommand(Owner owner, Target target, Reason reason,
                           uint32_t timestampMs, float requestedValue,
                           bool safeStopApplied = false) {
  Command command;
  command.owner = owner;
  command.target = target;
  command.reason = reason;
  command.timestampMs = timestampMs;
  command.requestedValue = requestedValue;
  command.safeStopRequired = requiresSafeStop(target);
  command.safeStopApplied = safeStopApplied;
  command.accepted = true;
  command.applied = false;
  return command;
}

inline void markSafeStopApplied(Command &command) {
  command.safeStopApplied = true;
}

// Snapshot is safe to read from the display or diagnostics task.
bool init();
// Capture the Arduino loop task. Calls from network tasks are queued and
// applied by processPendingCommands() in that control context.
void setControlTaskToCurrent();
void setOwnerFloor(Owner owner);
uint8_t processPendingCommands(uint8_t maxCommands = 16);
uint16_t pendingCommandCount();
uint32_t droppedCommandCount();
uint32_t cancelledCommandCount();
uint32_t coalescedCommandCount();
uint32_t ownerRejectedCommandCount();
uint32_t safetyRejectedCommandCount();
uint32_t appliedCommandCount();
Command getLastCommand();
bool getCommandStatus(uint32_t commandId, Command &out);
const char *rejectReasonString(RejectReason reason);
bool getOutputState(Target target, OutputState &out);
bool isSafeStopPending();
bool isSafetyTripLatched();

// A safety trip blocks all energizing requests until reset validation succeeds.
void tripSafety(Reason reason);
void clearSafetyTrip();

// FSM setpoints are applied by the regular control loop. The TFT records the
// request here rather than bypassing owner/reason/timestamp accounting.
void noteFsmSetpoint(Target target, Reason reason, float requestedValue);

// The FSM owns the actual pause/stop sequence, including its mode-specific
// cooling policy. Record that safe-stop here before requesting that sequence.
void noteFsmSafeStop(Reason reason);

void setHeaterPower(Owner owner, Reason reason, uint8_t percent);
Command setHeaterPowerWatts(Owner owner, Reason reason, uint16_t watts);
void setHeaterBoosterEnabled(Owner owner, Reason reason, bool enabled);
void setHeaterTriacDelay(Owner owner, Reason reason, uint16_t delayUs);
Command setPumpSpeed(Owner owner, Reason reason, float mlPerHour);
Command setStirrerSpeed(Owner owner, Reason reason, uint8_t percent);
Command stopStirrer(Owner owner, Reason reason);
Command setValve(Owner owner, Reason reason, Valve valve, bool open);
void setProductRoute(Owner owner, Reason reason, RectTakeoffFraction fraction,
                     bool open);
void closeProductRoutes(Owner owner, Reason reason);
Command closeAllValves(Owner owner, Reason reason);
Command pulseValve(Owner owner, Reason reason, Valve valve, uint32_t durationMs);
Command setCoolingPwm(Owner owner, Reason reason, uint8_t duty);
Command setFraction(Owner owner, Reason reason, Fraction fraction,
                    bool smooth = true);
Command setFractionAngle(Owner owner, Reason reason, uint8_t angle);

// Full fail-safe output state for a future TFT emergency action. Existing
// safety logic remains authoritative and may invoke the same hardware state.
Command safeStopAll(Owner owner, Reason reason);

} // namespace PowerCommandGateway
