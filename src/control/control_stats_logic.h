#pragma once

// Control statistics accumulated at 1 Hz while a process runs.
//
// The history timeseries keeps one point per minute, which is too coarse to
// judge how the control loop behaved: heater saturation, unpowered request,
// time in the temperature band and operator pauses all live between the
// minute samples. This header accumulates compact per-second aggregates so
// every phase carries a small "control" block in the history JSON and the
// whole process carries a run-level summary.
//
// Header-only and Arduino-free on purpose: the same code is covered by the
// native test harness.

#include <math.h>
#include <stdint.h>

namespace ControlStatsLogic {

// One 1 Hz sample. Everything the accumulator needs, already resolved by the
// caller (no mode/settings lookups here).
struct Sample {
  float heaterRequestedW = 0.0f;
  float heaterAppliedPercent = 0.0f; // driver duty actually applied, 0..100
  float heaterCapacityW = 0.0f;
  float powerMeasuredW = 0.0f;
  bool powerValid = false;

  float pumpRequestedMlH = 0.0f;
  float pumpActualMlH = 0.0f;

  float tempC = 0.0f;
  bool tempValid = false;
  float tempTargetC = 0.0f;
  bool hasTempTarget = false;
  float tempBandC = 0.5f;

  float coolingMarginC = 0.0f;
  bool coolingValid = false;
  bool pressureValid = false;
  float floodRisk = 0.0f;
  float stabilityIndex = 0.0f;
  float processHealth = 0.0f;
  float heatingRateCPerMin = 0.0f;
  float pressureRateMmHgPerMin = 0.0f;
  float pressureMarginMmHg = 0.0f;

  bool paused = false;
  bool safetyOk = true;
  bool degraded = false;
  bool takeoffAllowed = false;
};

// Accumulated values. Averages are derived (sum / durationSec or / samples)
// so the structure stays small and the JSON carries only what is used.
struct Stats {
  // Time base. durationSec counts covered seconds, not wall-clock span, so a
  // skipped control cycle cannot inflate it.
  uint32_t samples = 0;
  uint32_t durationSec = 0;
  uint32_t pausedSec = 0;
  uint32_t degradedSec = 0;
  uint32_t saturationSec = 0;       // heater applied at ~100 % of capacity
  uint32_t unpoweredSec = 0;        // power requested, little/none measured
  uint32_t takeoffAllowedSec = 0;
  uint32_t inBandSec = 0;           // only when the mode has a temp target
  uint16_t safetyTrips = 0;

  float heaterRequestedSumW = 0.0f;
  float heaterAppliedPercentSum = 0.0f;
  float powerSumW = 0.0f;
  float pumpSumMlH = 0.0f;

  float heaterRequestedMaxW = 0.0f;
  float heaterAppliedPercentMax = 0.0f;
  float powerMaxW = 0.0f;
  float pumpMaxMlH = 0.0f;

  float energyWh = 0.0f;

  float tempMinC = 0.0f;
  float tempMaxC = 0.0f;
  float tempSumC = 0.0f;
  uint32_t tempSamples = 0;
  float overshootMaxC = 0.0f;

  float healthMin = 0.0f;
  float healthSum = 0.0f;
  uint32_t healthSamples = 0;

  float stabilitySum = 0.0f;
  uint32_t stabilitySamples = 0;

  float floodRiskMax = 0.0f;
  float coolingMarginMin = 0.0f;
  bool coolingSeen = false;

  float pressureRateAbsSum = 0.0f;
  float pressureRateAbsMax = 0.0f;
  uint32_t pressureSamples = 0;
  float pressureMarginMin = 0.0f;
  bool pressureSeen = false;

  // Not serialized: edge detection for safety trips.
  bool prevSafetyOk = true;
};

inline uint32_t clampStepSec(uint32_t dtSec) {
  if (dtSec < 1) return 1;
  // A long gap (blocked loop, restart) must not be booked as minutes of
  // "paused" or "saturated"; one step at most keeps the averages honest.
  if (dtSec > 5) return 5;
  return dtSec;
}

inline void addSample(Stats &stats, const Sample &sample, uint32_t dtSec) {
  const uint32_t dt = clampStepSec(dtSec);

  stats.samples += 1;
  stats.durationSec += dt;

  if (sample.paused) stats.pausedSec += dt;
  if (sample.degraded) stats.degradedSec += dt;
  if (sample.takeoffAllowed) stats.takeoffAllowedSec += dt;

  if (!sample.safetyOk && stats.prevSafetyOk && stats.safetyTrips < 0xFFFF) {
    ++stats.safetyTrips;
  }
  stats.prevSafetyOk = sample.safetyOk;

  if (sample.heaterAppliedPercent >= 98.0f) {
    stats.saturationSec += dt;
  }

  if (sample.heaterRequestedW >= 100.0f) {
    const float floorW = sample.heaterRequestedW * 0.1f > 20.0f
                             ? sample.heaterRequestedW * 0.1f
                             : 20.0f;
    if (!sample.powerValid || sample.powerMeasuredW < floorW) {
      stats.unpoweredSec += dt;
    }
  }

  stats.heaterRequestedSumW += sample.heaterRequestedW * static_cast<float>(dt);
  stats.heaterAppliedPercentSum +=
      sample.heaterAppliedPercent * static_cast<float>(dt);
  stats.pumpSumMlH += sample.pumpActualMlH * static_cast<float>(dt);

  if (sample.heaterRequestedW > stats.heaterRequestedMaxW) {
    stats.heaterRequestedMaxW = sample.heaterRequestedW;
  }
  if (sample.heaterAppliedPercent > stats.heaterAppliedPercentMax) {
    stats.heaterAppliedPercentMax = sample.heaterAppliedPercent;
  }
  if (sample.pumpActualMlH > stats.pumpMaxMlH) {
    stats.pumpMaxMlH = sample.pumpActualMlH;
  }

  if (sample.powerValid && sample.powerMeasuredW >= 0.0f) {
    stats.powerSumW += sample.powerMeasuredW * static_cast<float>(dt);
    stats.energyWh +=
        sample.powerMeasuredW * static_cast<float>(dt) / 3600.0f;
    if (sample.powerMeasuredW > stats.powerMaxW) {
      stats.powerMaxW = sample.powerMeasuredW;
    }
  }

  if (sample.tempValid) {
    if (stats.tempSamples == 0 || sample.tempC < stats.tempMinC) {
      stats.tempMinC = sample.tempC;
    }
    if (stats.tempSamples == 0 || sample.tempC > stats.tempMaxC) {
      stats.tempMaxC = sample.tempC;
    }
    stats.tempSumC += sample.tempC * static_cast<float>(dt);
    stats.tempSamples += dt;

    if (sample.hasTempTarget) {
      const float deviation = sample.tempC - sample.tempTargetC;
      if (deviation > stats.overshootMaxC) stats.overshootMaxC = deviation;
      const float band = sample.tempBandC > 0.0f ? sample.tempBandC : 0.5f;
      if (fabsf(deviation) <= band) stats.inBandSec += dt;
    }
  }

  if (sample.processHealth > 0.0f) {
    if (stats.healthSamples == 0 || sample.processHealth < stats.healthMin) {
      stats.healthMin = sample.processHealth;
    }
    stats.healthSum += sample.processHealth;
    ++stats.healthSamples;
  }

  if (sample.stabilityIndex > 0.0f) {
    stats.stabilitySum += sample.stabilityIndex;
    ++stats.stabilitySamples;
  }

  if (sample.floodRisk > stats.floodRiskMax) {
    stats.floodRiskMax = sample.floodRisk;
  }

  if (sample.coolingValid &&
      (!stats.coolingSeen || sample.coolingMarginC < stats.coolingMarginMin)) {
    stats.coolingMarginMin = sample.coolingMarginC;
    stats.coolingSeen = true;
  }

  if (sample.pressureValid) {
    const float pressureRate = fabsf(sample.pressureRateMmHgPerMin);
    stats.pressureRateAbsSum += pressureRate;
    if (pressureRate > stats.pressureRateAbsMax) {
      stats.pressureRateAbsMax = pressureRate;
    }
    if (!stats.pressureSeen ||
        sample.pressureMarginMmHg < stats.pressureMarginMin) {
      stats.pressureMarginMin = sample.pressureMarginMmHg;
      stats.pressureSeen = true;
    }
    ++stats.pressureSamples;
  }
}

inline float perSecond(float sum, const Stats &stats) {
  return stats.durationSec > 0
             ? sum / static_cast<float>(stats.durationSec)
             : 0.0f;
}

inline float heaterRequestedAvgW(const Stats &stats) {
  return perSecond(stats.heaterRequestedSumW, stats);
}

inline float heaterAppliedAvgPercent(const Stats &stats) {
  return perSecond(stats.heaterAppliedPercentSum, stats);
}

inline float powerAvgW(const Stats &stats) {
  return perSecond(stats.powerSumW, stats);
}

inline float pumpAvgMlH(const Stats &stats) {
  return perSecond(stats.pumpSumMlH, stats);
}

inline float tempAvgC(const Stats &stats) {
  return stats.tempSamples > 0
             ? stats.tempSumC / static_cast<float>(stats.tempSamples)
             : 0.0f;
}

inline float healthAvg(const Stats &stats) {
  return stats.healthSamples > 0
             ? stats.healthSum / static_cast<float>(stats.healthSamples)
             : 0.0f;
}

inline float stabilityAvg(const Stats &stats) {
  return stats.stabilitySamples > 0
             ? stats.stabilitySum / static_cast<float>(stats.stabilitySamples)
             : 0.0f;
}

inline float pressureRateAvg(const Stats &stats) {
  return stats.pressureSamples > 0
             ? stats.pressureRateAbsSum /
                   static_cast<float>(stats.pressureSamples)
             : 0.0f;
}

// Share of the phase spent inside the temperature band, 0..1. Zero when the
// mode has no temperature target, so callers can skip the field.
inline float inBandShare(const Stats &stats) {
  return stats.durationSec > 0
             ? static_cast<float>(stats.inBandSec) /
                   static_cast<float>(stats.durationSec)
             : 0.0f;
}

} // namespace ControlStatsLogic
