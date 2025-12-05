/**
 * Smart-Column S3 - Logger
 * 
 * Логирование данных на SPIFFS в формате CSV
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace Logger {
    /**
     * Инициализация логгера
     * @return true если успешно
     */
    bool init();
    
    /**
     * Начало нового лога (при старте режима)
     * @param mode Режим работы
     * @return true если успешно
     */
    bool startNewLog(Mode mode);
    
    /**
     * Запись данных
     * @param state Состояние системы
     */
    void writeData(const SystemState& state);
    
    /**
     * Запись события
     * @param event Событие
     */
    void log(const LogEvent& event);
    
    /**
     * Завершение текущего лога
     */
    void closeLog();
    
    /**
     * Получение списка файлов логов
     * @param files Массив имён файлов
     * @param maxCount Максимум файлов
     * @return Количество найденных
     */
    uint8_t getLogFiles(char files[][32], uint8_t maxCount);
    
    /**
     * Удаление лог-файла
     * @param filename Имя файла
     * @return true если успешно
     */
    bool deleteLog(const char* filename);
    
    /**
     * Удаление старых логов (оставить последние N)
     * @param keepCount Сколько оставить
     */
    void cleanupOldLogs(uint8_t keepCount);
    
    /**
     * Экспорт лога в буфер
     * @param filename Имя файла
     * @param buffer Буфер для данных
     * @param maxSize Максимальный размер
     * @return Размер данных
     */
    size_t exportLog(const char* filename, uint8_t* buffer, size_t maxSize);
    
    /**
     * Получение последних N записей
     * @param events Массив событий
     * @param count Количество
     * @return Реальное количество
     */
    uint8_t getLastEvents(LogEvent* events, uint8_t count);
    
    /**
     * Получение использованного места
     * @return Байт использовано
     */
    size_t getUsedSpace();
    
    /**
     * Получение свободного места
     * @return Байт свободно
     */
    size_t getFreeSpace();
    
    /**
     * Получение имени текущего лог-файла
     * @return Имя файла или nullptr
     */
    const char* getCurrentLogFile();
}

#endif // LOGGER_H
