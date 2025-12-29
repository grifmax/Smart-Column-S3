/**
 * Smart-Column S3 - Demo Simulator
 *
 * Симуляция данных датчиков для демо-режима:
 * - Температуры постепенно растут (имитация нагрева)
 * - Давление в кубе немного увеличивается
 * - Мощность показывает реалистичные значения
 * - Скорость отбора работает
 */

#include "demo_simulator.h"
#include <Arduino.h>
#include <math.h>

namespace DemoSimulator {

// Внутреннее состояние симуляции
static struct {
  uint32_t startTime = 0;
  uint32_t lastUpdate = 0;
  float cubeTemp = 25.0f;
  float columnTemp = 25.0f;
  float phase = 0.0f; // Фаза симуляции: 0-100 (нагрев-работа)
  bool running = false;
} sim;

void init() {
  sim.startTime = millis();
  sim.lastUpdate = millis();
  sim.cubeTemp = 25.0f;
  sim.columnTemp = 25.0f;
  sim.phase = 0.0f;
  sim.running = true;
}

void reset() {
  sim.running = false;
  sim.phase = 0.0f;
  sim.cubeTemp = 25.0f;
  sim.columnTemp = 25.0f;
}

void update(SystemState &state, const Settings &settings) {
  if (!settings.demoMode) {
    return;
  }

  uint32_t now = millis();

  // При первом вызове инициализируем время
  if (sim.lastUpdate == 0) {
    sim.lastUpdate = now;
    return; // Пропускаем первый цикл
  }

  float dt =
      (now - sim.lastUpdate) / 1000.0f; // Время с последнего обновления (сек)
  sim.lastUpdate = now;

  // Защита от слишком большого dt
  if (dt > 1.0f)
    dt = 1.0f;

  // Проверяем запущен ли процесс
  bool processRunning = (state.mode != Mode::IDLE);

  if (processRunning && !sim.running) {
    init(); // Начинаем симуляцию при старте процесса
  } else if (!processRunning) {
    reset();
    // Возвращаем комнатную температуру
    state.temps.cube = 25.0f + (random(0, 100) / 100.0f);
    state.temps.columnBottom = 24.0f + (random(0, 100) / 100.0f);
    state.temps.columnTop = 23.0f + (random(0, 100) / 100.0f);
    state.temps.deflegmator = 22.0f + (random(0, 100) / 100.0f);
    state.temps.product = 21.0f + (random(0, 100) / 100.0f);
    state.pressure.cube = 0.5f + (random(0, 50) / 100.0f);
    state.power.power = 0.0f;
    state.power.voltage = 220.0f + (random(-5, 5));
    state.power.current = 0.0f;
    state.pump.speedMlPerHour = 0.0f;
    state.pump.running = false;
    return;
  }

  // ==========================================
  // СИМУЛЯЦИЯ НАГРЕВА И РАБОТЫ
  // ==========================================

  // Увеличиваем фазу (0-100+ за ~10 минут симуляции)
  sim.phase += dt * 0.15f; // ~10 мин до фазы 100

  // Целевые температуры в зависимости от фазы
  float targetCube, targetColumn, targetDefleg;

  if (sim.phase < 30) {
    // Фаза нагрева: 25°C → 95°C
    targetCube = 25.0f + (sim.phase / 30.0f) * 70.0f;
    targetColumn = 25.0f + (sim.phase / 30.0f) * 50.0f;
    targetDefleg = 25.0f + (sim.phase / 30.0f) * 30.0f;
  } else if (sim.phase < 50) {
    // Фаза стабилизации: колонна догоняет куб
    targetCube = 95.0f + (sim.phase - 30.0f) * 0.1f;
    targetColumn = 75.0f + ((sim.phase - 30.0f) / 20.0f) * 5.0f;
    targetDefleg = 55.0f + ((sim.phase - 30.0f) / 20.0f) * 20.0f;
  } else {
    // Рабочий режим: стабильные температуры с небольшими колебаниями
    targetCube = 97.0f + sin(sim.phase * 0.1f) * 0.5f;
    targetColumn = 78.5f + sin(sim.phase * 0.15f) * 0.3f;
    targetDefleg = 75.0f + sin(sim.phase * 0.12f) * 0.2f;
  }

  // Плавное приближение к целевым температурам
  float smoothFactor = 0.02f; // Скорость изменения
  sim.cubeTemp += (targetCube - sim.cubeTemp) * smoothFactor;
  sim.columnTemp += (targetColumn - sim.columnTemp) * smoothFactor;

  // Заполняем структуру температур с небольшим шумом
  float noise = (random(-50, 50) / 100.0f); // ±0.5°C шум
  state.temps.cube = sim.cubeTemp + noise;
  state.temps.columnBottom = sim.columnTemp + noise * 0.5f;
  state.temps.columnTop = sim.columnTemp - 3.0f + noise * 0.7f;
  state.temps.deflegmator = targetDefleg + noise * 0.3f;
  state.temps.reflux = state.temps.deflegmator;
  state.temps.product = state.temps.columnTop - 2.0f + noise * 0.5f;
  state.temps.tsa = (state.temps.cube + state.temps.columnBottom) / 2.0f;
  state.temps.waterIn = 15.0f + noise * 0.2f;
  state.temps.waterOut = 35.0f + (sim.phase / 100.0f) * 10.0f + noise * 0.3f;

  // Все датчики "валидны" в демо-режиме
  for (int i = 0; i < 8; i++) {
    state.temps.valid[i] = true;
  }
  state.temps.lastUpdate = now;

  // ==========================================
  // СИМУЛЯЦИЯ ДАВЛЕНИЯ
  // ==========================================
  float basePressure = 1.0f + (sim.phase / 100.0f) * 8.0f; // 1-9 мм рт.ст.
  state.pressure.cube = basePressure + (random(-20, 20) / 100.0f);
  state.pressure.atmosphere = 1013.25f + (random(-50, 50) / 10.0f);
  state.pressure.pressure = 101.325f;
  state.pressure.temperature = 25.0f;
  state.pressure.ok = true;
  state.pressure.lastUpdate = now;

  // ==========================================
  // СИМУЛЯЦИЯ МОЩНОСТИ
  // ==========================================
  float targetPower =
      settings.equipment.heaterPowerW * 0.8f; // 80% макс мощности
  if (sim.phase < 30) {
    // Полная мощность при нагреве
    targetPower = settings.equipment.heaterPowerW * 0.95f;
  } else if (sim.phase > 50) {
    // Уменьшенная мощность в рабочем режиме
    targetPower = settings.equipment.heaterPowerW *
                  (0.5f + sin(sim.phase * 0.05f) * 0.1f);
  }

  state.power.voltage = 220.0f + (random(-10, 10) / 10.0f);
  state.power.power = targetPower + (random(-50, 50));
  state.power.current = state.power.power / state.power.voltage;
  state.power.frequency = 50.0f + (random(-5, 5) / 100.0f);
  state.power.powerFactor = 0.95f + (random(-5, 5) / 100.0f);
  state.power.energy += (state.power.power / 1000.0f) * (dt / 3600.0f); // кВт·ч
  state.power.ok = true;
  state.power.lastUpdate = now;

  // ==========================================
  // СИМУЛЯЦИЯ НАСОСА
  // ==========================================
  if (sim.phase > 50) {
    // Начинаем отбор после стабилизации
    float targetSpeed = 200.0f + sin(sim.phase * 0.1f) * 50.0f; // 150-250 мл/ч
    state.pump.speedMlPerHour = targetSpeed + (random(-20, 20));
    state.pump.running = true;
    state.pump.totalVolumeMl += (state.pump.speedMlPerHour / 3600.0f) * dt;
  } else {
    state.pump.speedMlPerHour = 0.0f;
    state.pump.running = false;
  }

  // ==========================================
  // СИМУЛЯЦИЯ АРЕОМЕТРА
  // ==========================================
  if (sim.phase > 55) {
    state.hydrometer.valid = true;
    // ABV постепенно снижается с 96% до 40%
    float progress = min((sim.phase - 55.0f) / 100.0f, 1.0f);
    state.hydrometer.abv = 96.0f - progress * 56.0f; // 96% → 40%
    state.hydrometer.density = 0.78f + progress * 0.15f;
  }

  // Обновляем health - всё работает в демо
  state.health.tempSensorsOk = 8;
  state.health.tempSensorsTotal = 8;
  state.health.bmp280Ok = true;
  state.health.ads1115Ok = true;
  state.health.pzemOk = true;
}

} // namespace DemoSimulator
