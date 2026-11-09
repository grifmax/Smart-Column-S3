#pragma once

#include <stdint.h>

enum class Fraction : uint8_t;
enum class RectTakeoffFraction : uint8_t;

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
void setHeaterPowerWatts(Owner owner, Reason reason, uint16_t watts);
void setHeaterBoosterEnabled(Owner owner, Reason reason, bool enabled);
void setHeaterTriacDelay(Owner owner, Reason reason, uint16_t delayUs);
void setPumpSpeed(Owner owner, Reason reason, float mlPerHour);
void setStirrerSpeed(Owner owner, Reason reason, uint8_t percent);
void stopStirrer(Owner owner, Reason reason);
void setValve(Owner owner, Reason reason, Valve valve, bool open);
void setProductRoute(Owner owner, Reason reason, RectTakeoffFraction fraction,
                     bool open);
void closeProductRoutes(Owner owner, Reason reason);
void closeAllValves(Owner owner, Reason reason);
void pulseValve(Owner owner, Reason reason, Valve valve, uint32_t durationMs);
void setCoolingPwm(Owner owner, Reason reason, uint8_t duty);
void setFraction(Owner owner, Reason reason, Fraction fraction,
                 bool smooth = true);
void setFractionAngle(Owner owner, Reason reason, uint8_t angle);

// Full fail-safe output state for a future TFT emergency action. Existing
// safety logic remains authoritative and may invoke the same hardware state.
void safeStopAll(Owner owner, Reason reason);

} // namespace PowerCommandGateway
