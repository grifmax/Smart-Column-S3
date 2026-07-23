#pragma once

#include <stdint.h>

// The TFT must not call power drivers directly. This gateway is its single
// command boundary and leaves an auditable record of who requested an output,
// why, when, and whether the required safe-stop has happened.
namespace PowerCommandGateway {

enum class Owner : uint8_t {
  FSM = 0,
  SERVICE,
  MANUAL,
};

enum class Target : uint8_t {
  HEATER = 0,
  PUMP,
  STIRRER,
  WATER_VALVE,
  HEADS_VALVE,
  UNO_VALVE,
  ALL_OUTPUTS,
};

enum class Valve : uint8_t {
  WATER = 0,
  HEADS,
  UNO,
};

enum class Reason : uint8_t {
  TFT_RUNTIME_SETPOINT = 0,
  TFT_MANUAL_OUTPUT,
  TFT_SERVICE_SETTING,
  TFT_PROCESS_PAUSE,
  TFT_PROCESS_STOP,
};

struct Command {
  Owner owner = Owner::FSM;
  Target target = Target::ALL_OUTPUTS;
  Reason reason = Reason::TFT_RUNTIME_SETPOINT;
  uint32_t timestampMs = 0;
  float requestedValue = 0.0f;
  bool safeStopRequired = true;
  bool safeStopApplied = false;
};

constexpr bool requiresSafeStop(Target) { return true; }

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
  return command;
}

inline void markSafeStopApplied(Command &command) {
  command.safeStopApplied = true;
}

// Snapshot is safe to read from the display or diagnostics task.
Command getLastCommand();
bool isSafeStopPending();

// FSM setpoints are applied by the regular control loop. The TFT records the
// request here rather than bypassing owner/reason/timestamp accounting.
void noteFsmSetpoint(Target target, Reason reason, float requestedValue);

// The FSM owns the actual pause/stop sequence, including its mode-specific
// cooling policy. Record that safe-stop here before requesting that sequence.
void noteFsmSafeStop(Reason reason);

void setHeaterPower(Owner owner, Reason reason, uint8_t percent);
void setPumpSpeed(Owner owner, Reason reason, float mlPerHour);
void stopStirrer(Owner owner, Reason reason);
void setValve(Owner owner, Reason reason, Valve valve, bool open);

// Full fail-safe output state for a future TFT emergency action. Existing
// safety logic remains authoritative and may invoke the same hardware state.
void safeStopAll(Owner owner, Reason reason);

} // namespace PowerCommandGateway
