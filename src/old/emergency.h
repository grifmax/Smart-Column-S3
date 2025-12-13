/**
 * @file emergency.h
 * @brief Система аварийного восстановления
 */

#ifndef EMERGENCY_H
#define EMERGENCY_H

#include <Arduino.h>

// Уровни аварийных ситуаций
enum EmergencyLevel {
    EMERGENCY_NONE,      // Нет аварии
    EMERGENCY_WARNING,   // Предупреждение
    EMERGENCY_ERROR,     // Ошибка
    EMERGENCY_CRITICAL   // Критическая ситуация
};

// Структура информации об аварии
struct EmergencyInfo {
    EmergencyLevel level;
    String reason;
    unsigned long timestamp;
    bool systemShutdown;
};

/**
 * @brief Инициализация системы аварийного восстановления
 */
void initEmergency();

/**
 * @brief Срабатывание аварийной ситуации
 * 
 * @param level Уровень аварийной ситуации
 * @param reason Причина срабатывания
 */
void triggerEmergency(EmergencyLevel level, const String& reason);

/**
 * @brief Сброс аварийной ситуации
 * 
 * @return true если сброс выполнен успешно
 */
bool resetEmergency();

/**
 * @brief Проверка активности аварийной ситуации
 * 
 * @return true если активна аварийная ситуация
 */
bool isEmergencyActive();

/**
 * @brief Получение информации о текущей аварии
 * 
 * @return Структура с информацией об аварии
 */
EmergencyInfo getEmergencyInfo();

/**
 * @brief Полное отключение всех систем
 * 
 * Отключает нагрев, насос, клапан
 */
void shutdownAll();

/**
 * @brief Логирование аварийной ситуации
 * 
 * @param message Сообщение для логирования
 */
void logEmergency(const String& message);

/**
 * @brief Уведомление пользователя об аварии
 * 
 * @param message Сообщение для пользователя
 */
void notifyUserEmergency(const String& message);

/**
 * @brief Получение времени с момента аварии
 * 
 * @return Время в секундах
 */
unsigned long getTimeSinceEmergency();

/**
 * @brief Проверка возможности сброса аварии
 * 
 * @return true если сброс возможен
 */
bool canResetEmergency();

#endif // EMERGENCY_H
