/**
 * @file logger.cpp
 * @brief Реализация системы логирования
 */

#include "logger.h"
#include <LittleFS.h>
#include <time.h>

// Настройки логирования
static LogLevel currentLogLevel = LOG_LEVEL_DEBUG;
static bool fileLoggingEnabled = true;
static bool serialLoggingEnabled = true;

// Пути к файлам
#define LOG_FILE "/system.log"
#define LOG_ARCHIVE "/system_old.log"
#define MAX_LOG_SIZE 100000  // 100 KB

// Инициализация системы логирования
void initLogger(bool enableFile, bool enableSerial) {
    Serial.println("Инициализация системы логирования...");
    
    fileLoggingEnabled = enableFile;
    serialLoggingEnabled = enableSerial;
    
    if (fileLoggingEnabled) {
        if (!LittleFS.begin(true)) {
            Serial.println("ОШИБКА: Не удалось инициализировать LittleFS");
            fileLoggingEnabled = false;
        } else {
            Serial.println("LittleFS инициализирована");
            
            // Проверяем размер лога
            if (getLogFileSize() > MAX_LOG_SIZE) {
                Serial.println("Лог-файл слишком большой, выполняем ротацию...");
                rotateLogs();
            }
        }
    }
    
    // Логируем старт системы
    logMessage(LOG_LEVEL_INFO, LOG_CAT_SYSTEM, "=== СИСТЕМА ЗАПУЩЕНА ===");
    logMessageF(LOG_LEVEL_INFO, LOG_CAT_SYSTEM, "Версия прошивки: %s", FIRMWARE_VERSION);
    logMessageF(LOG_LEVEL_INFO, LOG_CAT_SYSTEM, "Свободная память: %d байт", ESP.getFreeHeap());
    
    Serial.println("Система логирования инициализирована");
}

// Установка минимального уровня логирования
void setLogLevel(LogLevel level) {
    currentLogLevel = level;
    logMessageF(LOG_LEVEL_INFO, LOG_CAT_SYSTEM, 
                "Уровень логирования изменен на: %s", 
                getLogLevelName(level).c_str());
}

// Логирование сообщения
void logMessage(LogLevel level, LogCategory category, const String& message) {
    // Проверяем уровень
    if (level < currentLogLevel) {
        return;
    }
    
    // Формируем временную метку
    unsigned long timestamp = millis();
    unsigned long seconds = timestamp / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu.%03lu", 
             hours % 24, minutes % 60, seconds % 60, timestamp % 1000);
    
    // Формируем строку лога
    String levelStr = getLogLevelName(level);
    String catStr = getLogCategoryName(category);
    
    String logLine = String(timeStr) + " [" + levelStr + "] " + 
                     catStr + ": " + message;
    
    // Вывод в Serial
    if (serialLoggingEnabled) {
        // Цветной вывод в зависимости от уровня (ANSI escape codes)
        switch (level) {
            case LOG_LEVEL_TRACE:
                Serial.print("\033[37m"); // Белый
                break;
            case LOG_LEVEL_DEBUG:
                Serial.print("\033[36m"); // Голубой
                break;
            case LOG_LEVEL_INFO:
                Serial.print("\033[32m"); // Зеленый
                break;
            case LOG_LEVEL_WARNING:
                Serial.print("\033[33m"); // Желтый
                break;
            case LOG_LEVEL_ERROR:
                Serial.print("\033[31m"); // Красный
                break;
            case LOG_LEVEL_CRITICAL:
                Serial.print("\033[35m"); // Пурпурный
                break;
        }
        Serial.println(logLine);
        Serial.print("\033[0m"); // Сброс цвета
    }
    
    // Запись в файл
    if (fileLoggingEnabled) {
        File logFile = LittleFS.open(LOG_FILE, "a");
        if (logFile) {
            logFile.println(logLine);
            logFile.close();
            
            // Проверяем размер после записи
            if (getLogFileSize() > MAX_LOG_SIZE) {
                rotateLogs();
            }
        }
    }
}

// Логирование с форматированием
void logMessageF(LogLevel level, LogCategory category, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logMessage(level, category, String(buffer));
}

// Очистка лог-файлов
bool clearLogs() {
    if (!fileLoggingEnabled) {
        return false;
    }
    
    Serial.println("Очистка лог-файлов...");
    
    bool success = true;
    
    if (LittleFS.exists(LOG_FILE)) {
        success &= LittleFS.remove(LOG_FILE);
    }
    
    if (LittleFS.exists(LOG_ARCHIVE)) {
        success &= LittleFS.remove(LOG_ARCHIVE);
    }
    
    if (success) {
        logMessage(LOG_LEVEL_INFO, LOG_CAT_SYSTEM, "Логи очищены");
    }
    
    return success;
}

// Получение размера лог-файла
size_t getLogFileSize() {
    if (!fileLoggingEnabled || !LittleFS.exists(LOG_FILE)) {
        return 0;
    }
    
    File logFile = LittleFS.open(LOG_FILE, "r");
    if (!logFile) {
        return 0;
    }
    
    size_t size = logFile.size();
    logFile.close();
    
    return size;
}

// Ротация логов
bool rotateLogs() {
    if (!fileLoggingEnabled) {
        return false;
    }
    
    Serial.println("Ротация лог-файлов...");
    
    // Удаляем старый архив
    if (LittleFS.exists(LOG_ARCHIVE)) {
        LittleFS.remove(LOG_ARCHIVE);
    }
    
    // Переименовываем текущий лог в архив
    if (LittleFS.exists(LOG_FILE)) {
        LittleFS.rename(LOG_FILE, LOG_ARCHIVE);
    }
    
    logMessage(LOG_LEVEL_INFO, LOG_CAT_SYSTEM, "Ротация логов выполнена");
    
    return true;
}

// Экспорт логов в строку
String exportLogs(int maxLines) {
    if (!fileLoggingEnabled || !LittleFS.exists(LOG_FILE)) {
        return "Лог-файл пуст или не доступен";
    }
    
    File logFile = LittleFS.open(LOG_FILE, "r");
    if (!logFile) {
        return "Ошибка открытия лог-файла";
    }
    
    String result = "";
    int lineCount = 0;
    
    while (logFile.available() && lineCount < maxLines) {
        String line = logFile.readStringUntil('\n');
        result += line + "\n";
        lineCount++;
    }
    
    logFile.close();
    
    return result;
}

// Включение/выключение записи в файл
void setFileLogging(bool enable) {
    fileLoggingEnabled = enable;
    logMessageF(LOG_LEVEL_INFO, LOG_CAT_SYSTEM, 
                "Запись в файл %s", enable ? "включена" : "выключена");
}

// Включение/выключение вывода в Serial
void setSerialLogging(bool enable) {
    serialLoggingEnabled = enable;
}

// Получение имени уровня логирования
String getLogLevelName(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_TRACE:
            return "TRACE";
        case LOG_LEVEL_DEBUG:
            return "DEBUG";
        case LOG_LEVEL_INFO:
            return "INFO ";
        case LOG_LEVEL_WARNING:
            return "WARN ";
        case LOG_LEVEL_ERROR:
            return "ERROR";
        case LOG_LEVEL_CRITICAL:
            return "CRIT ";
        default:
            return "?????";
    }
}

// Получение имени категории
String getLogCategoryName(LogCategory category) {
    switch (category) {
        case LOG_CAT_SYSTEM:
            return "SYSTEM    ";
        case LOG_CAT_PROCESS:
            return "PROCESS   ";
        case LOG_CAT_TEMPERATURE:
            return "TEMP      ";
        case LOG_CAT_POWER:
            return "POWER     ";
        case LOG_CAT_SAFETY:
            return "SAFETY    ";
        case LOG_CAT_WEB:
            return "WEB       ";
        case LOG_CAT_STORAGE:
            return "STORAGE   ";
        case LOG_CAT_HARDWARE:
            return "HARDWARE  ";
        default:
            return "UNKNOWN   ";
    }
}
