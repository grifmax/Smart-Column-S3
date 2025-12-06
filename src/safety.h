/**
 * @file safety.h
 * @brief Система безопасности для ректификационной колонны
 */

#ifndef SAFETY_H
#define SAFETY_H

#include <Arduino.h>

// Коды ошибок безопасности
enum SafetyErrorCode {
    SAFETY_OK = 0,                      // Все в порядке
    SAFETY_SENSOR_DISCONNECTED = 1,     // Датчик отключен
    SAFETY_TEMP_TOO_HIGH = 2,           // Температура слишком высока
    SAFETY_TEMP_RISE_TOO_FAST = 3,      // Слишком быстрый рост температуры
    SAFETY_WATER_FLOW_LOW = 4,          // Низкий поток воды охлаждения
    SAFETY_RUNTIME_EXCEEDED = 5,        // Превышено максимальное время работы
    SAFETY_EMERGENCY_STOP = 6,          // Аварийная остановка
    SAFETY_WATCHDOG_RESET = 7,          // Сброс по watchdog
    SAFETY_PRESSURE_ERROR = 8,          // Ошибка давления
    SAFETY_MULTIPLE_ERRORS = 99         // Множественные ошибки
};

// Структура статуса безопасности
struct SafetyStatus {
    bool isSystemSafe;                  // Безопасна ли система
    int errorCode;                      // Код ошибки
    unsigned long errorTime;            // Время возникновения ошибки
    String errorDescription;            // Описание ошибки
    
    // Типы ошибок (флаги)
    bool isSensorError;                 // Ошибка датчика
    bool isTemperatureError;            // Ошибка температуры
    bool isWaterFlowError;              // Ошибка потока воды
    bool isPressureError;               // Ошибка давления
    bool isRuntimeError;                // Ошибка времени работы
    bool isEmergencyStop;               // Аварийная остановка
    bool isWatchdogReset;               // Сброс по watchdog
};

/**
 * @brief Инициализация системы безопасности
 */
void initSafety();

/**
 * @brief Обновление состояния системы безопасности
 * 
 * Вызывается периодически для проверки всех параметров безопасности
 */
void updateSafety();

/**
 * @brief Получение текущего статуса безопасности
 * 
 * @return SafetyStatus структура со статусом безопасности
 */
SafetyStatus getSafetyStatus();

/**
 * @brief Проверка условий безопасности
 * 
 * @return true если все условия безопасности соблюдены
 */
bool checkSafetyConditions();

/**
 * @brief Установка максимального времени работы
 * 
 * @param hours Максимальное время работы в часах
 */
void setSafetyMaxRuntime(float hours);

/**
 * @brief Установка максимальной температуры куба
 * 
 * @param temp Максимальная температура в градусах Цельсия
 */
void setSafetyMaxCubeTemp(float temp);

/**
 * @brief Установка максимальной скорости роста температуры
 * 
 * @param rate Максимальная скорость в °C/минуту
 */
void setSafetyMaxTempRiseRate(float rate);

/**
 * @brief Установка минимальной температуры воды на выходе
 * 
 * @param temp Минимальная температура воды в градусах Цельсия
 */
void setSafetyMinWaterOutTemp(float temp);

/**
 * @brief Установка максимальной температуры воды на выходе
 * 
 * @param temp Максимальная температура воды в градусах Цельсия
 */
void setSafetyMaxWaterOutTemp(float temp);

/**
 * @brief Сброс ошибок безопасности
 * 
 * @return true если ошибки успешно сброшены
 */
bool resetSafetyErrors();

/**
 * @brief Инициирование аварийной остановки
 * 
 * @param reason Причина аварийной остановки
 */
void triggerEmergencyStop(const String& reason);

/**
 * @brief Получение описания кода ошибки
 * 
 * @param errorCode Код ошибки
 * @return Строка с описанием ошибки
 */
String getSafetyErrorDescription(SafetyErrorCode errorCode);

/**
 * @brief Проверка, была ли система перезагружена из-за watchdog
 * 
 * @return true если была перезагрузка по watchdog
 */
bool wasWatchdogReset();

/**
 * @brief Включение/выключение системы безопасности
 * 
 * @param enabled true для включения, false для выключения
 */
void setSafetyEnabled(bool enabled);

/**
 * @brief Проверка, включена ли система безопасности
 * 
 * @return true если система безопасности включена
 */
bool isSafetyEnabled();

/**
 * @brief Получение времени с момента последней ошибки
 * 
 * @return Время в секундах с момента последней ошибки
 */
unsigned long getTimeSinceLastError();

#endif // SAFETY_H
