/**
 * @file error_handler.cpp
 * @brief Реализация централизованной обработки ошибок
 */

#include "error_handler.h"
#include <vector>

// История ошибок
#define MAX_ERROR_HISTORY 100
static std::vector<ErrorInfo> errorHistory;
static int totalErrors = 0;
static int warningCount = 0;
static int errorCount = 0;
static int criticalCount = 0;
static int fatalCount = 0;

// Инициализация обработчика ошибок
void initErrorHandler() {
    Serial.println("Инициализация обработчика ошибок...");
    errorHistory.clear();
    errorHistory.reserve(MAX_ERROR_HISTORY);
    
    totalErrors = 0;
    warningCount = 0;
    errorCount = 0;
    criticalCount = 0;
    fatalCount = 0;
    
    Serial.println("Обработчик ошибок инициализирован");
}

// Регистрация ошибки
void reportError(ErrorModule module, ErrorType type, int errorCode, 
                const String& message, const char* function) {
    
    // Создаем запись об ошибке
    ErrorInfo error;
    error.module = module;
    error.type = type;
    error.errorCode = errorCode;
    error.message = message;
    error.function = String(function);
    error.timestamp = millis();
    
    // Добавляем в историю
    if (errorHistory.size() >= MAX_ERROR_HISTORY) {
        errorHistory.erase(errorHistory.begin()); // Удаляем самую старую
    }
    errorHistory.push_back(error);
    
    // Обновляем счетчики
    totalErrors++;
    switch (type) {
        case ERROR_TYPE_WARNING:
            warningCount++;
            break;
        case ERROR_TYPE_ERROR:
            errorCount++;
            break;
        case ERROR_TYPE_CRITICAL:
            criticalCount++;
            break;
        case ERROR_TYPE_FATAL:
            fatalCount++;
            break;
        default:
            break;
    }
    
    // Вывод в Serial
    String typeStr = getErrorTypeName(type);
    String moduleStr = getModuleName(module);
    
    Serial.println();
    Serial.println("╔═══════════════════════════════════════════════════════╗");
    Serial.println("║                    ОШИБКА СИСТЕМЫ                     ║");
    Serial.println("╚═══════════════════════════════════════════════════════╝");
    Serial.println("Тип:      " + typeStr);
    Serial.println("Модуль:   " + moduleStr);
    Serial.println("Код:      " + String(errorCode));
    Serial.println("Сообщение: " + message);
    if (function && strlen(function) > 0) {
        Serial.println("Функция:  " + String(function));
    }
    Serial.println("Время:    " + String(millis() / 1000) + " сек");
    Serial.println("═════════════════════════════════════════════════════════");
    Serial.println();
    
    // Специальные действия для критичных ошибок
    if (type == ERROR_TYPE_CRITICAL || type == ERROR_TYPE_FATAL) {
        // Сохраняем лог
        saveErrorLog();
        
        // Для фатальных ошибок - перезагрузка через 5 секунд
        if (type == ERROR_TYPE_FATAL) {
            Serial.println("⚠️ ФАТАЛЬНАЯ ОШИБКА! Система будет перезагружена через 5 секунд...");
            delay(5000);
            ESP.restart();
        }
    }
}

// Получение последней ошибки
ErrorInfo getLastError() {
    if (errorHistory.empty()) {
        ErrorInfo empty;
        empty.type = ERROR_TYPE_NONE;
        empty.errorCode = 0;
        empty.message = "Нет ошибок";
        return empty;
    }
    return errorHistory.back();
}

// Получение количества ошибок
int getErrorCount(ErrorType type) {
    if (type == ERROR_TYPE_NONE) {
        return totalErrors;
    }
    
    switch (type) {
        case ERROR_TYPE_WARNING:
            return warningCount;
        case ERROR_TYPE_ERROR:
            return errorCount;
        case ERROR_TYPE_CRITICAL:
            return criticalCount;
        case ERROR_TYPE_FATAL:
            return fatalCount;
        default:
            return 0;
    }
}

// Очистка истории ошибок
void clearErrors() {
    Serial.println("Очистка истории ошибок...");
    errorHistory.clear();
    totalErrors = 0;
    warningCount = 0;
    errorCount = 0;
    criticalCount = 0;
    fatalCount = 0;
}

// Проверка наличия критичных ошибок
bool hasCriticalErrors() {
    return (criticalCount > 0 || fatalCount > 0);
}

// Получение имени модуля
String getModuleName(ErrorModule module) {
    switch (module) {
        case MODULE_MAIN:
            return "Main";
        case MODULE_HEATER:
            return "Heater";
        case MODULE_PUMP:
            return "Pump";
        case MODULE_VALVE:
            return "Valve";
        case MODULE_TEMP_SENSORS:
            return "Temperature Sensors";
        case MODULE_DISPLAY:
            return "Display";
        case MODULE_WEB:
            return "Web Server";
        case MODULE_STORAGE:
            return "Storage";
        case MODULE_SAFETY:
            return "Safety";
        case MODULE_RECTIFICATION:
            return "Rectification";
        case MODULE_DISTILLATION:
            return "Distillation";
        case MODULE_POWER_CONTROL:
            return "Power Control";
        case MODULE_WATCHDOG:
            return "Watchdog";
        default:
            return "Unknown";
    }
}

// Получение описания типа ошибки
String getErrorTypeName(ErrorType type) {
    switch (type) {
        case ERROR_TYPE_NONE:
            return "Нет ошибки";
        case ERROR_TYPE_WARNING:
            return "ПРЕДУПРЕЖДЕНИЕ";
        case ERROR_TYPE_ERROR:
            return "ОШИБКА";
        case ERROR_TYPE_CRITICAL:
            return "КРИТИЧЕСКАЯ ОШИБКА";
        case ERROR_TYPE_FATAL:
            return "ФАТАЛЬНАЯ ОШИБКА";
        default:
            return "Неизвестный тип";
    }
}

// Вывод всех ошибок в Serial
void printErrorLog() {
    Serial.println("\n╔═══════════════════════════════════════════════════════╗");
    Serial.println("║              ИСТОРИЯ ОШИБОК СИСТЕМЫ                   ║");
    Serial.println("╚═══════════════════════════════════════════════════════╝");
    Serial.println("Всего ошибок: " + String(totalErrors));
    Serial.println("Предупреждений: " + String(warningCount));
    Serial.println("Ошибок: " + String(errorCount));
    Serial.println("Критических: " + String(criticalCount));
    Serial.println("Фатальных: " + String(fatalCount));
    Serial.println("═════════════════════════════════════════════════════════");
    
    if (errorHistory.empty()) {
        Serial.println("История пуста");
    } else {
        Serial.println("\nПоследние " + String(errorHistory.size()) + " ошибок:\n");
        
        for (size_t i = 0; i < errorHistory.size(); i++) {
            const ErrorInfo& err = errorHistory[i];
            
            Serial.println("─────────────────────────────────────────────────────────");
            Serial.println("#" + String(i + 1) + " [" + String(err.timestamp / 1000) + "s] " + 
                          getErrorTypeName(err.type));
            Serial.println("Модуль: " + getModuleName(err.module));
            Serial.println("Код: " + String(err.errorCode));
            Serial.println("Сообщение: " + err.message);
            if (err.function.length() > 0) {
                Serial.println("Функция: " + err.function);
            }
        }
        Serial.println("═════════════════════════════════════════════════════════\n");
    }
}

// Сохранение логов ошибок
bool saveErrorLog() {
    // TODO: Реализовать сохранение в файл
    // Будет реализовано в модуле логирования
    Serial.println("Сохранение лога ошибок...");
    return true;
}
