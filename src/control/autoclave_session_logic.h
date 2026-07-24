#pragma once

#include <cstdint>

// Hardware-independent phase machine for the autoclave process. Output
// ownership deliberately lives in the handler (D-024); this logic establishes
// the sequencing and the sterilization timer contract for host-side tests.
namespace AutoclaveSessionLogic {

enum class Phase : uint8_t {
  IDLE = 0,
  PRECHECK,
  HEATING,
  STERILIZING,
  COOLING,
  READY,
  FINISH,
  FAULT,
};

struct Config {
  float sterilizationTempC = 121.0f;
  float temperatureBandC = 1.0f;
  uint32_t sterilizationDurationMs = 15UL * 60UL * 1000UL;
  float coolingStopTempC = 40.0f;
};

struct State {
  Phase phase = Phase::IDLE;
  bool operatorReadyConfirmed = false;
  uint32_t sterilizationAccumulatedMs = 0;
  uint32_t phaseStartedAtMs = 0;
  uint32_t lastInBandAtMs = 0;
};

inline bool isTerminal(Phase phase) {
  return phase == Phase::FINISH || phase == Phase::FAULT;
}

inline void enter(State &state, Phase phase, uint32_t nowMs) {
  state.phase = phase;
  state.phaseStartedAtMs = nowMs;
  state.lastInBandAtMs = 0;
}

inline bool start(State &state, uint32_t nowMs) {
  if (state.phase != Phase::IDLE && !isTerminal(state.phase)) return false;
  state = State{};
  enter(state, Phase::PRECHECK, nowMs);
  return true;
}

inline bool confirmPrecheck(State &state, bool sensorsReady, uint32_t nowMs) {
  if (state.phase != Phase::PRECHECK || !sensorsReady) return false;
  state.operatorReadyConfirmed = true;
  enter(state, Phase::HEATING, nowMs);
  return true;
}

inline bool inSterilizationBand(const Config &config, float temperatureC) {
  return temperatureC >= config.sterilizationTempC - config.temperatureBandC &&
         temperatureC <= config.sterilizationTempC + config.temperatureBandC;
}

inline bool update(State &state, const Config &config, bool temperatureValid,
                   float temperatureC, uint32_t nowMs) {
  if (state.phase == Phase::IDLE || isTerminal(state.phase)) return false;
  if (state.phase == Phase::PRECHECK) return false;
  if (!temperatureValid || config.temperatureBandC <= 0.0f ||
      config.sterilizationDurationMs == 0 || config.coolingStopTempC <= 0.0f) {
    enter(state, Phase::FAULT, nowMs);
    return true;
  }

  switch (state.phase) {
  case Phase::HEATING:
    if (temperatureC >= config.sterilizationTempC - config.temperatureBandC) {
      enter(state, Phase::STERILIZING, nowMs);
      return true;
    }
    return false;
  case Phase::STERILIZING:
    if (!inSterilizationBand(config, temperatureC)) {
      state.lastInBandAtMs = 0;
      return false;
    }
    if (state.lastInBandAtMs == 0) {
      state.lastInBandAtMs = nowMs;
      return false;
    }
    state.sterilizationAccumulatedMs += nowMs - state.lastInBandAtMs;
    state.lastInBandAtMs = nowMs;
    if (state.sterilizationAccumulatedMs >= config.sterilizationDurationMs) {
      state.sterilizationAccumulatedMs = config.sterilizationDurationMs;
      enter(state, Phase::COOLING, nowMs);
      return true;
    }
    return false;
  case Phase::COOLING:
    if (temperatureC <= config.coolingStopTempC) {
      enter(state, Phase::READY, nowMs);
      return true;
    }
    return false;
  case Phase::READY:
  case Phase::FINISH:
  case Phase::FAULT:
  case Phase::IDLE:
  default:
    return false;
  }
}

inline bool finish(State &state, uint32_t nowMs) {
  if (state.phase != Phase::READY) return false;
  enter(state, Phase::FINISH, nowMs);
  return true;
}

} // namespace AutoclaveSessionLogic
