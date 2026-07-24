#pragma once

#include <cstdint>

// Hardware-independent controller for one fermentation zone. It only decides
// requested outputs; a later board-profile adapter owns dispatch and safe-stop
// of physical relays.
namespace FermentationZoneLogic {

enum class Phase : uint8_t {
  IDLE = 0,
  HEATING,
  COOLING,
  FERMENTING,
  COMPLETED,
  FAULT,
};

enum class Fault : uint8_t {
  NONE = 0,
  TEMPERATURE_SENSOR,
  SETTINGS_INVALID,
};

struct Config {
  bool enabled = false;
  float targetTempC = 28.0f;
  float hysteresisC = 0.5f;
  bool useHeater = true;
  bool useCooling = true;
  uint32_t coolingMinOnMs = 60UL * 1000UL;
  uint32_t coolingMinOffMs = 60UL * 1000UL;
  uint32_t durationMs = 0;
  bool stirrerEnabled = false;
  uint32_t stirrerPeriodMs = 0;
  uint32_t stirrerOnMs = 0;
};

struct Runtime {
  Phase phase = Phase::IDLE;
  Fault fault = Fault::NONE;
  bool started = false;
  uint32_t phaseStartedAtMs = 0;
  uint32_t startedAtMs = 0;
  bool coolingActive = false;
  bool coolingTransitionKnown = false;
  uint32_t coolingChangedAtMs = 0;
};

struct Outputs {
  bool heater = false;
  bool cooling = false;
  bool stirrer = false;
};

struct Result {
  Outputs outputs;
  bool phaseChanged = false;
  bool faultChanged = false;
};

inline bool validConfig(const Config &config) {
  if (config.targetTempC < 5.0f || config.targetTempC > 45.0f ||
      config.hysteresisC <= 0.0f || config.hysteresisC > 10.0f) {
    return false;
  }
  if (config.useCooling &&
      (config.coolingMinOnMs == 0 || config.coolingMinOffMs == 0)) {
    return false;
  }
  if (config.stirrerEnabled &&
      (config.stirrerPeriodMs == 0 || config.stirrerOnMs == 0 ||
       config.stirrerOnMs > config.stirrerPeriodMs)) {
    return false;
  }
  return true;
}

inline const char *faultToken(Fault fault) {
  switch (fault) {
  case Fault::TEMPERATURE_SENSOR:
    return "temperature_sensor";
  case Fault::SETTINGS_INVALID:
    return "settings_invalid";
  case Fault::NONE:
  default:
    return "none";
  }
}

inline void setPhase(Runtime &runtime, Phase phase, uint32_t nowMs,
                     Result &result) {
  if (runtime.phase == phase) return;
  runtime.phase = phase;
  runtime.phaseStartedAtMs = nowMs;
  result.phaseChanged = true;
}

inline void start(Runtime &runtime, uint32_t nowMs) {
  runtime = Runtime{};
  runtime.started = true;
  runtime.startedAtMs = nowMs;
  runtime.phaseStartedAtMs = nowMs;
}

inline Result update(Runtime &runtime, const Config &config,
                     bool temperatureValid, float temperatureC,
                     uint32_t nowMs) {
  Result result;
  const Phase priorPhase = runtime.phase;
  const Fault priorFault = runtime.fault;

  if (!config.enabled) {
    runtime = Runtime{};
    result.phaseChanged = priorPhase != Phase::IDLE;
    result.faultChanged = priorFault != Fault::NONE;
    return result;
  }
  if (!validConfig(config)) {
    runtime.coolingActive = false;
    runtime.fault = Fault::SETTINGS_INVALID;
    setPhase(runtime, Phase::FAULT, nowMs, result);
    result.faultChanged = priorFault != runtime.fault;
    return result;
  }
  if (!temperatureValid) {
    runtime.coolingActive = false;
    runtime.fault = Fault::TEMPERATURE_SENSOR;
    setPhase(runtime, Phase::FAULT, nowMs, result);
    result.faultChanged = priorFault != runtime.fault;
    return result;
  }
  if (runtime.phase == Phase::FAULT || runtime.phase == Phase::COMPLETED) {
    return result;
  }
  if (!runtime.started) {
    runtime.started = true;
    runtime.startedAtMs = nowMs;
    runtime.phaseStartedAtMs = nowMs;
  }

  if (config.durationMs > 0 && nowMs - runtime.startedAtMs >= config.durationMs) {
    runtime.coolingActive = false;
    setPhase(runtime, Phase::COMPLETED, nowMs, result);
    return result;
  }

  const float lower = config.targetTempC - config.hysteresisC;
  const float upper = config.targetTempC + config.hysteresisC;
  if (runtime.coolingActive) {
    if (temperatureC <= upper &&
        nowMs - runtime.coolingChangedAtMs >= config.coolingMinOnMs) {
      runtime.coolingActive = false;
      runtime.coolingTransitionKnown = true;
      runtime.coolingChangedAtMs = nowMs;
    }
  } else if (config.useCooling && temperatureC > upper &&
             (!runtime.coolingTransitionKnown ||
              nowMs - runtime.coolingChangedAtMs >= config.coolingMinOffMs)) {
    runtime.coolingActive = true;
    runtime.coolingTransitionKnown = true;
    runtime.coolingChangedAtMs = nowMs;
  }

  result.outputs.cooling = runtime.coolingActive;
  result.outputs.heater = !runtime.coolingActive && config.useHeater &&
                          temperatureC < lower;
  if (result.outputs.cooling) {
    setPhase(runtime, Phase::COOLING, nowMs, result);
  } else if (result.outputs.heater) {
    setPhase(runtime, Phase::HEATING, nowMs, result);
  } else {
    setPhase(runtime, Phase::FERMENTING, nowMs, result);
  }

  if (config.stirrerEnabled) {
    const uint32_t elapsedMs = nowMs - runtime.startedAtMs;
    result.outputs.stirrer =
        elapsedMs % config.stirrerPeriodMs < config.stirrerOnMs;
  }
  return result;
}

} // namespace FermentationZoneLogic
