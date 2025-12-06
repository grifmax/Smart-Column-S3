/**
 * @file valve.h
 * @brief Интерфейс управления клапаном
 */

#ifndef VALVE_H
#define VALVE_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Инициализация клапана
 * 
 * @return true если инициализация прошла успешно
 */
bool initValve();

/**
 * @brief Открытие клапана
 */
void valveOpen();

/**
 * @brief Закрытие клапана
 */
void valveClose();

/**
 * @brief Проверка состояния клапана
 * 
 * @return true если клапан открыт
 */
bool isValveOpen();

/**
 * @brief Переключение состояния клапана
 * 
 * @return Новое состояние клапана (true - открыт, false - закрыт)
 */
bool toggleValve();

/**
 * @brief Открытие клапана на заданное время
 * 
 * @param openTimeMs Время открытия клапана в миллисекундах
 */
void valveOpenTimed(unsigned long openTimeMs);

/**
 * @brief Обновление состояния клапана
 * 
 * Вызывается периодически для обработки временного открытия клапана
 */
void updateValve();

/**
 * @brief Установка обратной логики работы клапана
 * 
 * @param invert true если логика работы клапана обратная
 */
void setValveLogicInverted(bool invert);

/**
 * @brief Проверка обратной логики работы клапана
 * 
 * @return true если используется обратная логика
 */
bool isValveLogicInverted();

/**
 * @brief Проверка занятости клапана
 * 
 * @return true если клапан выполняет временную операцию
 */
bool isValveBusy();

/**
 * @brief Отмена временной операции клапана
 */
void cancelValveOperation();

/**
 * @brief Установка задержки переключения клапана
 * 
 * @param delayMs Задержка в миллисекундах
 */
void setValveSwitchDelay(unsigned long delayMs);

/**
 * @brief Получение времени последнего переключения клапана
 * 
 * @return Время в миллисекундах
 */
unsigned long getValveLastSwitchTime();

/**
 * @brief Калибровка клапана
 * 
 * @return true если калибровка прошла успешно
 */
bool calibrateValve();

#endif // VALVE_H
