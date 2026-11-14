#pragma once

#include <cstdint>

#include "../types.h"

// Публичный интерфейс обкаточного режима на воде. Отчёт живёт вне SystemState:
// он не нужен снапшоту состояния и не должен копироваться каждые 100 мс.
namespace FSM {
namespace WaterTest {

struct CheckRecord {
  uint8_t step = 0;
  uint8_t verdict = 0;
  char id[20] = "";
  char note[56] = "";
  float expected = 0.0f;
  float actual = 0.0f;
};

// Запуск. Возвращает false, если режим уже занят, включён демо-режим или
// нет обязательных данных датчика куба. Причина пишется в waterTest.lastError.
bool start(SystemState& state, const Settings& settings);

// Вызывается из FSM::update, когда mode == Mode::WATER_TEST.
void update(SystemState& state, const Settings& settings);

// Остановка по команде оператора или при аварии. Гасит ТЭН и отбор, воду
// оставляет открытой, если куб горячий.
void stop(SystemState& state);

// Подтверждение шага, ожидающего оператора.
void confirm(SystemState& state);

// Замер объёма для шага отбора (мл).
bool setMeasuredVolume(SystemState& state, float milliliters);

// Текущий отчёт. Указатель действителен до следующего запуска.
uint8_t getChecks(const CheckRecord** out);
void resetReport();

}  // namespace WaterTest
}  // namespace FSM
