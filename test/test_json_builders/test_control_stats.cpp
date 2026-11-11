#include <unity.h>

#include <../../src/control/control_stats_logic.h>

using ControlStatsLogic::Sample;
using ControlStatsLogic::Stats;

void test_control_stats_accumulates_heater_and_power() {
  Stats stats;
  Sample sample;
  sample.heaterCapacityW = 2000.0f;
  sample.heaterRequestedW = 1500.0f;
  sample.heaterAppliedPercent = 75.0f;
  sample.powerMeasuredW = 1400.0f;
  sample.powerValid = true;
  sample.pumpActualMlH = 300.0f;

  ControlStatsLogic::addSample(stats, sample, 1);
  ControlStatsLogic::addSample(stats, sample, 1);
  ControlStatsLogic::addSample(stats, sample, 1);

  TEST_ASSERT_EQUAL_UINT32(3, stats.samples);
  TEST_ASSERT_EQUAL_UINT32(3, stats.durationSec);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1500.0f,
                           ControlStatsLogic::heaterRequestedAvgW(stats));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.0f,
                           ControlStatsLogic::heaterAppliedAvgPercent(stats));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1400.0f,
                           ControlStatsLogic::powerAvgW(stats));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 4200.0f / 3600.0f, stats.energyWh);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 300.0f,
                           ControlStatsLogic::pumpAvgMlH(stats));
  TEST_ASSERT_EQUAL_UINT32(0, stats.saturationSec);
}

void test_control_stats_counts_saturation_and_unpowered_seconds() {
  Stats stats;
  Sample saturated;
  saturated.heaterCapacityW = 2000.0f;
  saturated.heaterRequestedW = 2000.0f;
  saturated.heaterAppliedPercent = 100.0f;
  saturated.powerMeasuredW = 1950.0f;
  saturated.powerValid = true;

  ControlStatsLogic::addSample(stats, saturated, 1);
  ControlStatsLogic::addSample(stats, saturated, 1);
  TEST_ASSERT_EQUAL_UINT32(2, stats.saturationSec);
  TEST_ASSERT_EQUAL_UINT32(0, stats.unpoweredSec);

  // Request stays, measured power collapses: that is the "heater asked for,
  // nothing delivered" case the operator keeps missing.
  Sample unpowered = saturated;
  unpowered.powerMeasuredW = 0.0f;
  ControlStatsLogic::addSample(stats, unpowered, 1);
  TEST_ASSERT_EQUAL_UINT32(1, stats.unpoweredSec);

  // No valid measurement at all counts as unpowered too.
  Sample noMeter = saturated;
  noMeter.powerValid = false;
  ControlStatsLogic::addSample(stats, noMeter, 1);
  TEST_ASSERT_EQUAL_UINT32(2, stats.unpoweredSec);
  TEST_ASSERT_EQUAL_UINT32(4, stats.saturationSec);
}

void test_control_stats_tracks_band_overshoot_pauses_and_degraded() {
  Stats stats;
  Sample sample;
  sample.tempC = 78.2f;
  sample.tempValid = true;
  sample.tempTargetC = 78.0f;
  sample.hasTempTarget = true;
  sample.tempBandC = 0.5f;
  sample.paused = true;

  ControlStatsLogic::addSample(stats, sample, 1);
  TEST_ASSERT_EQUAL_UINT32(1, stats.inBandSec);
  TEST_ASSERT_EQUAL_UINT32(1, stats.pausedSec);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, stats.overshootMaxC);

  Sample hot = sample;
  hot.tempC = 80.0f;
  hot.paused = false;
  hot.degraded = true;
  ControlStatsLogic::addSample(stats, hot, 2);
  TEST_ASSERT_EQUAL_UINT32(1, stats.inBandSec);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, stats.overshootMaxC);
  TEST_ASSERT_EQUAL_UINT32(2, stats.degradedSec);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, (78.2f + 80.0f * 2.0f) / 3.0f,
                           ControlStatsLogic::tempAvgC(stats));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f / 3.0f,
                           ControlStatsLogic::inBandShare(stats));
}

void test_control_stats_counts_safety_trips_on_edges_only() {
  Stats stats;
  Sample sample;
  sample.safetyOk = true;
  ControlStatsLogic::addSample(stats, sample, 1);

  sample.safetyOk = false;
  ControlStatsLogic::addSample(stats, sample, 1);
  ControlStatsLogic::addSample(stats, sample, 1);
  TEST_ASSERT_EQUAL_UINT16(1, stats.safetyTrips);

  sample.safetyOk = true;
  ControlStatsLogic::addSample(stats, sample, 1);
  sample.safetyOk = false;
  ControlStatsLogic::addSample(stats, sample, 1);
  TEST_ASSERT_EQUAL_UINT16(2, stats.safetyTrips);
}

void test_control_stats_clamps_long_gaps() {
  Stats stats;
  Sample sample;
  sample.paused = true;
  ControlStatsLogic::addSample(stats, sample, 60);
  TEST_ASSERT_EQUAL_UINT32(5, stats.durationSec);
  TEST_ASSERT_EQUAL_UINT32(5, stats.pausedSec);

  // Zero is treated as one second, never as a free sample.
  Stats zeroStats;
  ControlStatsLogic::addSample(zeroStats, sample, 0);
  TEST_ASSERT_EQUAL_UINT32(1, zeroStats.durationSec);
}

void test_control_stats_ignores_absent_pressure_and_cooling_sensors() {
  Stats stats;
  Sample sample;
  sample.pressureValid = false;
  sample.coolingValid = false;
  sample.pressureRateMmHgPerMin = 12.0f;
  sample.pressureMarginMmHg = -5.0f;
  sample.coolingMarginC = -3.0f;

  ControlStatsLogic::addSample(stats, sample, 1);
  TEST_ASSERT_EQUAL_UINT32(0, stats.pressureSamples);
  TEST_ASSERT_FALSE(stats.coolingSeen);

  sample.pressureValid = true;
  sample.coolingValid = true;
  ControlStatsLogic::addSample(stats, sample, 1);
  TEST_ASSERT_EQUAL_UINT32(1, stats.pressureSamples);
  TEST_ASSERT_TRUE(stats.coolingSeen);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f,
                           ControlStatsLogic::pressureRateAvg(stats));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.0f, stats.pressureMarginMin);
}
