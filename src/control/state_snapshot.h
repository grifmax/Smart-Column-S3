#pragma once

#include <stdint.h>

#include "types.h"

// Публикация согласованного снимка SystemState для читателей, живущих в
// другой задаче (AsyncWebServer/AsyncTCP). Снимок копируется под отдельным
// mutex; сетевые обработчики не читают изменяемый g_state.
namespace StateSnapshot {

// Создать mutex до запуска сетевых обработчиков.
bool init();

// Публикация текущего состояния. Вызывается из loop() с ограничением по
// времени (STATE_SNAPSHOT_INTERVAL_MS), а не на каждой итерации. Возвращает
// false, если mutex занят и публикация пропущена.
bool publish(const SystemState &state);

// Согласованное копирование снимка. Возвращает false, если снимок ещё не
// публиковался или mutex не удалось получить за ограниченное время.
bool read(SystemState &out);

// Признак того, что хотя бы одна публикация уже произошла.
bool isReady();

// Номер последней публикации (чётный — снимок стабилен).
uint32_t sequence();
uint32_t ageMs(uint32_t nowMs);

// Счётчики диагностических пропусков.
uint32_t retryCount();
uint32_t publishSkipCount();

}  // namespace StateSnapshot
