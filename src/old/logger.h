/**
 * @file logger.h
 * @brief Система логирования с записью в файл и Serial
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// Уровни логирования
enum LogLevel {
    LOG_LEVEL_TRACE = 0,    // Детальная отладочная информация
    LOG_LEVEL_DEBUG = 1,    // Отладочная информация
    LOG_LEVEL_INFO = 2,     // Информационные сообщения
    LOG_LEVEL_WARNING = 3,  // Предупреждения
    LOG_LEVEL_ERROR = 4,    // Ошибки
    LOG_LEVEL_CRITICAL = 5  // Критические ошибки
};

// Категории логов
enum LogCategory {
    LOG_CAT_SYSTEM,         // Системные сообщения
    LOG_CAT_PROCESS,        // Процесс ректификации/дистилляции
    LOG_CAT_TEMPERATURE,    // Температурные данные
    LOG_CAT_POWER,          // Управление мощностью
    LOG_CAT_SAFETY,         // Безопасность
    LOG_CAT_WEB,            // Веб-сервер
    LOG_CAT_STORAGE,        // Хранение данных
    LOG_CAT_HARDWARE        // Оборудование
};

/**
 * @brief Инициализация системы логирования
 * 
 * @param enableFile Включить запись в файл
 * @param enableSerial Включить вывод в Serial
 */
void initLogger(bool enableFile = true, bool enableSerial = true);

/**
 * @brief Установка минимального уровня логирования
 * 
 * @param level Минимальный уровень
 */
void setLogLevel(LogLevel level);

/**
 * @brief Логирование сообщения
 * 
 * @param level Уровень важности
 * @param category Категория
 * @param message Сообщение
 */
void logMessage(LogLevel level, LogCategory category, const String& message);

/**
 * @brief Логирование с форматированием
 * 
 * @param level Уровень важности
 * @param category Категория
 * @param format Формат строки (как в printf)
 * @param ... Аргументы
 */
void logMessageF(LogLevel level, LogCategory category, const char* format, ...);

/**
 * @brief Очистка лог-файлов
 * 
 * @return true если успешно
 */
bool clearLogs();

/**
 * @brief Получение размера лог-файла
 * 
 * @return Размер в байтах
 */
size_t getLogFileSize();

/**
 * @brief Ротация логов (архивирование старых)
 * 
 * @return true если успешно
 */
bool rotateLogs();

/**
 * @brief Экспорт логов в строку
 * 
 * @param maxLines Максимальное количество строк
 * @return Строка с логами
 */
String exportLogs(int maxLines = 100);

/**
 * @brief Включение/выключение записи в файл
 * 
 * @param enable true для включения
 */
void setFileLogging(bool enable);

/**
 * @brief Включение/выключение вывода в Serial
 * 
 * @param enable true для включения
 */
void setSerialLogging(bool enable);

/**
 * @brief Получение имени уровня логирования
 * 
 * @param level Уровень
 * @return Имя уровня
 */
String getLogLevelName(LogLevel level);

/**
 * @brief Получение имени категории
 * 
 * @param category Категория
 * @return Имя категории
 */
String getLogCategoryName(LogCategory category);

// Макросы для удобного логирования
#define LOG_TRACE(cat, msg) logMessage(LOG_LEVEL_TRACE, cat, msg)
#define LOG_DEBUG(cat, msg) logMessage(LOG_LEVEL_DEBUG, cat, msg)
#define LOG_INFO(cat, msg) logMessage(LOG_LEVEL_INFO, cat, msg)
#define LOG_WARNING(cat, msg) logMessage(LOG_LEVEL_WARNING, cat, msg)
#define LOG_ERROR(cat, msg) logMessage(LOG_LEVEL_ERROR, cat, msg)
#define LOG_CRITICAL(cat, msg) logMessage(LOG_LEVEL_CRITICAL, cat, msg)

// Макросы с форматированием
#define LOG_TRACEF(cat, fmt, ...) logMessageF(LOG_LEVEL_TRACE, cat, fmt, ##__VA_ARGS__)
#define LOG_DEBUGF(cat, fmt, ...) logMessageF(LOG_LEVEL_DEBUG, cat, fmt, ##__VA_ARGS__)
#define LOG_INFOF(cat, fmt, ...) logMessageF(LOG_LEVEL_INFO, cat, fmt, ##__VA_ARGS__)
#define LOG_WARNINGF(cat, fmt, ...) logMessageF(LOG_LEVEL_WARNING, cat, fmt, ##__VA_ARGS__)
#define LOG_ERRORF(cat, fmt, ...) logMessageF(LOG_LEVEL_ERROR, cat, fmt, ##__VA_ARGS__)
#define LOG_CRITICALF(cat, fmt, ...) logMessageF(LOG_LEVEL_CRITICAL, cat, fmt, ##__VA_ARGS__)

// Специальные макросы для частых случаев
#define LOG_TEMP(sensor, temp) logMessageF(LOG_LEVEL_DEBUG, LOG_CAT_TEMPERATURE, \
    "Датчик %s: %.2f°C", sensor, temp)

#define LOG_POWER(watts, percent) logMessageF(LOG_LEVEL_DEBUG, LOG_CAT_POWER, \
    "Мощность: %d Вт (%d%%)", watts, percent)

#define LOG_PHASE(phase) logMessageF(LOG_LEVEL_INFO, LOG_CAT_PROCESS, \
    "Переход в фазу: %s", phase)

#endif // LOGGER_H
