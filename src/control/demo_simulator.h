/**
 * Smart-Column S3 - Demo Simulator
 *
 * Симуляция данных датчиков для демо-режима
 */

#ifndef DEMO_SIMULATOR_H
#define DEMO_SIMULATOR_H

#include "../types.h"

namespace DemoSimulator {

/**
 * Инициализация симулятора
 */
void init();

/**
 * Обновление симулированных данных
 * Вызывается в главном цикле если demoMode = true
 */
void update(SystemState &state, const Settings &settings);

/**
 * Сброс симуляции (при остановке демо-режима)
 */
void reset();

} // namespace DemoSimulator

#endif // DEMO_SIMULATOR_H
