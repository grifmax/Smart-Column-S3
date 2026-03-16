/**
 * Smart-Column S3 - Demo Simulator
 *
 * Simulates sensors and process telemetry in demo mode.
 * Real pump hardware is synchronized separately with a throttled update path so
 * demo mode does not chatter the stepper driver on every loop iteration.
 */

#include "demo_simulator.h"

#include <Arduino.h>
#include <math.h>

#include "drivers/pump.h"
#include "drivers/valves.h"

namespace DemoSimulator {

static struct {
  uint32_t startTime = 0;
  uint32_t lastUpdate = 0;
  uint32_t lastPumpProfileUpdate = 0;
  float cubeTemp = 25.0f;
  float columnTemp = 25.0f;
  float phase = 0.0f;
  float pumpTargetSpeedMlH = 0.0f;
  float pumpSpeedMlH = 0.0f;
  bool running = false;
} sim;

namespace {

constexpr uint32_t kDemoPumpProfileUpdateMs = 5000;
constexpr float kDemoPumpMinSpeedMlH = 150.0f;
constexpr float kDemoPumpMaxSpeedMlH = 250.0f;

void applyDemoHealth(SystemState &state) {
  for (uint8_t i = 0; i < TEMP_COUNT; i++) {
    state.temps.valid[i] = true;
  }

  state.health.tempSensorsOk = true;
  state.health.tempSensorsTotal = TEMP_COUNT;
  state.health.bmp280Ok = true;
  state.health.ads1115Ok = true;
  state.health.pzemOk = true;
}

float computeDemoPumpTargetSpeed(uint32_t nowMs) {
  const float baseSpeed = 200.0f + sinf(sim.phase * 0.1f) * 35.0f;
  const float drift = static_cast<float>(random(-10, 11));
  const float profileBias = sinf(static_cast<float>(nowMs) / 15000.0f) * 10.0f;
  return constrain(baseSpeed + drift + profileBias, kDemoPumpMinSpeedMlH,
                   kDemoPumpMaxSpeedMlH);
}

void resetIdleState(SystemState &state, uint32_t now) {
  reset();
  state.temps.cube = 25.0f + (random(0, 100) / 100.0f);
  state.temps.columnBottom = 24.0f + (random(0, 100) / 100.0f);
  state.temps.columnTop = 23.0f + (random(0, 100) / 100.0f);
  state.temps.deflegmator = 22.0f + (random(0, 100) / 100.0f);
  state.temps.reflux = state.temps.deflegmator;
  state.temps.product = 21.0f + (random(0, 100) / 100.0f);
  state.temps.tsa = 24.0f + (random(0, 80) / 100.0f);
  state.temps.waterIn = 15.0f + (random(0, 50) / 100.0f);
  state.temps.waterOut = 18.0f + (random(0, 50) / 100.0f);
  state.temps.lastUpdate = now;

  state.pressure.cube = 0.5f + (random(0, 50) / 100.0f);
  state.pressure.ok = true;
  state.pressure.lastUpdate = now;

  state.power.power = 0.0f;
  state.power.voltage = 220.0f + random(-5, 5);
  state.power.current = 0.0f;
  state.power.frequency = 50.0f;
  state.power.powerFactor = 0.98f;
  state.power.ok = true;
  state.power.lastUpdate = now;

  state.pump.speedMlPerHour = 0.0f;
  state.pump.running = false;

  state.hydrometer.valid = false;
  state.hydrometer.lastUpdate = now;
  applyDemoHealth(state);
}

void updatePumpModel(SystemState &state, float dt, uint32_t now) {
  if (sim.phase <= 50.0f) {
    sim.pumpTargetSpeedMlH = 0.0f;
    sim.pumpSpeedMlH = 0.0f;
    state.pump.speedMlPerHour = 0.0f;
    state.pump.running = false;
    return;
  }

  if (sim.lastPumpProfileUpdate == 0 ||
      (now - sim.lastPumpProfileUpdate) >= kDemoPumpProfileUpdateMs) {
    sim.lastPumpProfileUpdate = now;
    sim.pumpTargetSpeedMlH = computeDemoPumpTargetSpeed(now);
  }

  const float speedSmoothing = constrain(dt * 0.35f, 0.0f, 1.0f);
  sim.pumpSpeedMlH +=
      (sim.pumpTargetSpeedMlH - sim.pumpSpeedMlH) * speedSmoothing;
  sim.pumpSpeedMlH = roundf(sim.pumpSpeedMlH / 5.0f) * 5.0f;

  state.pump.speedMlPerHour = sim.pumpSpeedMlH;
  state.pump.running = sim.pumpSpeedMlH > 0.0f;

  const float addedVolume = (sim.pumpSpeedMlH / 3600.0f) * dt;
  if (Valves::getHeads()) {
    state.stats.headsVolume += addedVolume;
    return;
  }

  const Fraction current = Valves::getCurrentFraction();
  if (current == Fraction::TAILS) {
    state.stats.tailsVolume += addedVolume;
  } else {
    state.stats.bodyVolume += addedVolume;
  }
}

} // namespace

void init() {
  sim.startTime = millis();
  sim.lastUpdate = millis();
  sim.lastPumpProfileUpdate = 0;
  sim.cubeTemp = 25.0f;
  sim.columnTemp = 25.0f;
  sim.phase = 0.0f;
  sim.pumpTargetSpeedMlH = 0.0f;
  sim.pumpSpeedMlH = 0.0f;
  sim.running = true;
}

void reset() {
  sim.running = false;
  sim.phase = 0.0f;
  sim.cubeTemp = 25.0f;
  sim.columnTemp = 25.0f;
  sim.lastPumpProfileUpdate = 0;
  sim.pumpTargetSpeedMlH = 0.0f;
  sim.pumpSpeedMlH = 0.0f;
}

void update(SystemState &state, const Settings &settings) {
  if (!settings.demoMode) {
    return;
  }

  const uint32_t now = millis();
  if (sim.lastUpdate == 0) {
    sim.lastUpdate = now;
    return;
  }

  float dt = (now - sim.lastUpdate) / 1000.0f;
  sim.lastUpdate = now;
  if (dt > 1.0f) {
    dt = 1.0f;
  }

  const bool processRunning = state.mode != Mode::IDLE;
  if (processRunning && !sim.running) {
    init();
  } else if (!processRunning) {
    resetIdleState(state, now);
    return;
  }

  sim.phase += dt * 0.15f;

  float targetCube = 25.0f;
  float targetColumn = 25.0f;
  float targetDefleg = 25.0f;

  if (sim.phase < 30.0f) {
    targetCube = 25.0f + (sim.phase / 30.0f) * 70.0f;
    targetColumn = 25.0f + (sim.phase / 30.0f) * 50.0f;
    targetDefleg = 25.0f + (sim.phase / 30.0f) * 30.0f;
  } else if (sim.phase < 50.0f) {
    targetCube = 95.0f + (sim.phase - 30.0f) * 0.1f;
    targetColumn = 75.0f + ((sim.phase - 30.0f) / 20.0f) * 5.0f;
    targetDefleg = 55.0f + ((sim.phase - 30.0f) / 20.0f) * 20.0f;
  } else {
    targetCube = 97.0f + sinf(sim.phase * 0.1f) * 0.5f;
    targetColumn = 78.5f + sinf(sim.phase * 0.15f) * 0.3f;
    targetDefleg = 75.0f + sinf(sim.phase * 0.12f) * 0.2f;
  }

  if (state.mode == Mode::NBK) {
    if (sim.phase < 30.0f) {
      targetCube = 25.0f + (sim.phase / 30.0f) * 75.0f;
      targetColumn = 25.0f + (sim.phase / 30.0f) * 20.0f;
    } else if (sim.phase < 50.0f) {
      targetCube = 100.0f;
      targetColumn = 25.0f + ((sim.phase - 30.0f) / 20.0f) * 75.0f;
    } else {
      targetCube = 100.0f;
      targetColumn = 98.0f + sinf(sim.phase * 0.15f) * 0.5f;
    }
  }

  sim.cubeTemp += (targetCube - sim.cubeTemp) * 0.02f;
  sim.columnTemp += (targetColumn - sim.columnTemp) * 0.02f;

  const float noise = random(-50, 50) / 100.0f;
  state.temps.cube = sim.cubeTemp + noise;
  state.temps.columnBottom = sim.columnTemp + noise * 0.5f;
  state.temps.columnTop = sim.columnTemp - 3.0f + noise * 0.7f;
  state.temps.deflegmator = targetDefleg + noise * 0.3f;
  state.temps.reflux = state.temps.deflegmator;
  state.temps.product = state.temps.columnTop - 2.0f + noise * 0.5f;

  const bool coolingOn = Valves::getWater();
  float targetTsa = coolingOn ? 24.0f + (sim.phase * 0.05f)
                              : 32.0f + (sim.phase * 0.08f);
  if (state.mode == Mode::DISTILLATION) {
    targetTsa += 2.0f;
  } else if (state.mode == Mode::NBK) {
    targetTsa += 1.0f;
  }
  targetTsa = constrain(targetTsa, 22.0f, 42.0f);

  state.temps.tsa = targetTsa + noise * 0.2f;
  state.temps.waterIn = 15.0f + noise * 0.2f;
  state.temps.waterOut = 35.0f + (sim.phase / 100.0f) * 10.0f + noise * 0.3f;

  if (state.mode == Mode::FERMENTATION) {
    targetCube =
        settings.fermentation.targetTempC + sinf(sim.phase * 0.05f) * 0.2f;
    sim.cubeTemp += (targetCube - sim.cubeTemp) * 0.05f;
    state.temps.cube = sim.cubeTemp + noise * 0.1f;
  }

  applyDemoHealth(state);
  state.temps.lastUpdate = now;

  state.pressure.cube = 1.0f + (sim.phase / 100.0f) * 8.0f +
                        (random(-20, 20) / 100.0f);
  state.pressure.atmosphere = 1013.25f + (random(-50, 50) / 10.0f);
  state.pressure.pressure = 101.325f;
  state.pressure.temperature = 25.0f;
  state.pressure.ok = true;
  state.pressure.lastUpdate = now;

  float targetPower = settings.equipment.heaterPowerW * 0.8f;
  if (sim.phase < 30.0f) {
    targetPower = settings.equipment.heaterPowerW * 0.95f;
  } else if (sim.phase > 50.0f) {
    targetPower =
        settings.equipment.heaterPowerW * (0.5f + sinf(sim.phase * 0.05f) * 0.1f);
  }

  state.power.voltage = 220.0f + (random(-10, 10) / 10.0f);
  state.power.power = targetPower + random(-50, 50);
  state.power.current = state.power.power / state.power.voltage;
  state.power.frequency = 50.0f + (random(-5, 5) / 100.0f);
  state.power.powerFactor = 0.95f + (random(-5, 5) / 100.0f);
  state.power.energy += (state.power.power / 1000.0f) * (dt / 3600.0f);
  state.power.ok = true;
  state.power.lastUpdate = now;

  updatePumpModel(state, dt, now);

  if (sim.phase > 55.0f) {
    state.hydrometer.valid = true;
    const float progress = min((sim.phase - 55.0f) / 100.0f, 1.0f);
    state.hydrometer.abv = 96.0f - progress * 56.0f;
    state.hydrometer.pressure = 100.0f + progress * 5.0f;
  }

  applyDemoHealth(state);
}

void syncHardware(SystemState &state, const Settings &settings) {
  (void)state;
  (void)settings;
}

} // namespace DemoSimulator
