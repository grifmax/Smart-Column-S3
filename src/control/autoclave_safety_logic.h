#pragma once

#include <cstdint>

#include "autoclave_session_logic.h"

// Pure watchdog for the autoclave phase machine. It does not choose a vent,
// drain or cooling output; a fault only tells the owning handler to apply the
// already-approved full safe-stop policy.
namespace AutoclaveSafetyLogic {

enum class Fault : uint8_t {
  NONE = 0,
  TEMPERATURE_SENSOR,
  PRESSURE_SENSOR,
  PRESSURE_LIMIT,
  PRESSURE_RISE_RATE,
  HEATING_TIMEOUT,
  SETTINGS_INVALID,
};

struct Config {
  float pressureMaxMmHg = 0.0f;
  float pressureRiseRateMaxMmHgMin = 0.0f;
  uint32_t heatingTimeoutMs = 0;
};

struct State {
  bool pressureBaselineValid = false;
  float lastPressureMmHg = 0.0f;
  uint32_t lastPressureAtMs = 0;
};

inline const char *faultToken(Fault fault) {
  switch (fault) {
  case Fault::TEMPERATURE_SENSOR:
    return "temperature_sensor";
  case Fault::PRESSURE_SENSOR:
    return "pressure_sensor";
  case Fault::PRESSURE_LIMIT:
    return "pressure_limit";
  case Fault::PRESSURE_RISE_RATE:
    return "pressure_rise_rate";
  case Fault::HEATING_TIMEOUT:
    return "heating_timeout";
  case Fault::SETTINGS_INVALID:
    return "settings_invalid";
  case Fault::NONE:
  default:
    return "none";
  }
}

inline bool requiresLiveSensors(AutoclaveSessionLogic::Phase phase) {
  return phase == AutoclaveSessionLogic::Phase::PRECHECK ||
         phase == AutoclaveSessionLogic::Phase::HEATING ||
         phase == AutoclaveSessionLogic::Phase::STERILIZING ||
         phase == AutoclaveSessionLogic::Phase::COOLING ||
         phase == AutoclaveSessionLogic::Phase::READY;
}

inline Fault evaluate(State &state, const Config &config,
                      AutoclaveSessionLogic::Phase phase,
                      uint32_t phaseStartedAtMs, bool temperatureValid,
                      bool pressureValid, float pressureMmHg,
                      uint32_t nowMs) {
  if (!requiresLiveSensors(phase)) return Fault::NONE;
  if (config.pressureMaxMmHg <= 0.0f ||
      config.pressureRiseRateMaxMmHgMin <= 0.0f ||
      config.heatingTimeoutMs == 0) {
    return Fault::SETTINGS_INVALID;
  }
  if (!temperatureValid) return Fault::TEMPERATURE_SENSOR;
  if (!pressureValid) return Fault::PRESSURE_SENSOR;
  if (pressureMmHg > config.pressureMaxMmHg) return Fault::PRESSURE_LIMIT;

  if (state.pressureBaselineValid && nowMs != state.lastPressureAtMs) {
    const uint32_t elapsedMs = nowMs - state.lastPressureAtMs;
    const float riseRateMmHgMin =
        (pressureMmHg - state.lastPressureMmHg) * 60000.0f /
        static_cast<float>(elapsedMs);
    if (riseRateMmHgMin > config.pressureRiseRateMaxMmHgMin) {
      return Fault::PRESSURE_RISE_RATE;
    }
  }
  state.pressureBaselineValid = true;
  state.lastPressureMmHg = pressureMmHg;
  state.lastPressureAtMs = nowMs;

  if (phase == AutoclaveSessionLogic::Phase::HEATING &&
      nowMs - phaseStartedAtMs >= config.heatingTimeoutMs) {
    return Fault::HEATING_TIMEOUT;
  }
  return Fault::NONE;
}

} // namespace AutoclaveSafetyLogic
