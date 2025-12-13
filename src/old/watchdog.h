/**
 * @file watchdog.h
 * @brief Watchdog Timer для защиты от зависаний системы
 */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <Arduino.h>

/**
 * @brief Инициализация Watchdog Timer
 * 
 * @param timeoutSeconds Таймаут в секундах (по умолчанию 30)
 */
void initWatchdog(uint32_t timeoutSeconds = 30);

/**
 * @brief Сброс (подкормка) Watchdog Timer
 * 
 * Должна вызываться регулярно чтобы предотвратить перезагрузку
 */
void feedWatchdog();

/**
 * @brief Включение Watchdog Timer
 */
void enableWatchdog();

/**
 * @brief Выключение Watchdog Timer
 */
void disableWatchdog();

/**
 * @brief Проверка, включен ли Watchdog
 * 
 * @return true если Watchdog включен
 */
bool isWatchdogEnabled();

/**
 * @brief Получение оставшегося времени до срабатывания
 * 
 * @return Оставшееся время в миллисекундах
 */
uint32_t getWatchdogTimeLeft();

#endif // WATCHDOG_H
