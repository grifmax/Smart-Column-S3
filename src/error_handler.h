/**
 * @file error_handler.h
 * @brief Централизованная обработка ошибок и исключений
 */

#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <Arduino.h>

// Коды ошибок модулей
enum ErrorModule {
    MODULE_MAIN = 0,
    MODULE_HEATER = 1,
    MODULE_PUMP = 2,
    MODULE_VALVE = 3,
    MODULE_TEMP_SENSORS = 4,
    MODULE_DISPLAY = 5,
    MODULE_WEB = 6,
    MODULE_STORAGE = 7,
    MODULE_SAFETY = 8,
    MODULE_RECTIFICATION = 9,
    MODULE_DISTILLATION = 10,
    MODULE_POWER_CONTROL = 11,
    MODULE_WATCHDOG = 12
};

// Типы ошибок
enum ErrorType {
    ERROR_TYPE_NONE = 0,
    ERROR_TYPE_WARNING = 1,      // Предупреждение
    ERROR_TYPE_ERROR = 2,        // Ошибка (не критичная)
    ERROR_TYPE_CRITICAL = 3,     // Критическая ошибка
    ERROR_TYPE_FATAL = 4         // Фатальная ошибка (требует перезагрузки)
};

// Структура информации об ошибке
struct ErrorInfo {
    ErrorModule module;
    ErrorType type;
    int errorCode;
    String message;
    String function;
    unsigned long timestamp;
};

/**
 * @brief Инициализация обработчика ошибок
 */
void initErrorHandler();

/**
 * @brief Регистрация ошибки
 * 
 * @param module Модуль, в котором произошла ошибка
 * @param type Тип ошибки
 * @param errorCode Код ошибки
 * @param message Сообщение об ошибке
 * @param function Имя функции, где произошла ошибка
 */
void reportError(ErrorModule module, ErrorType type, int errorCode, 
                const String& message, const char* function = "");

/**
 * @brief Получение последней ошибки
 * 
 * @return ErrorInfo структура с информацией об ошибке
 */
ErrorInfo getLastError();

/**
 * @brief Получение количества ошибок
 * 
 * @param type Тип ошибок (0 = все)
 * @return Количество ошибок
 */
int getErrorCount(ErrorType type = ERROR_TYPE_NONE);

/**
 * @brief Очистка истории ошибок
 */
void clearErrors();

/**
 * @brief Проверка наличия критичных ошибок
 * 
 * @return true если есть критичные ошибки
 */
bool hasCriticalErrors();

/**
 * @brief Получение имени модуля
 * 
 * @param module Модуль
 * @return Имя модуля
 */
String getModuleName(ErrorModule module);

/**
 * @brief Получение описания типа ошибки
 * 
 * @param type Тип ошибки
 * @return Описание типа
 */
String getErrorTypeName(ErrorType type);

/**
 * @brief Вывод всех ошибок в Serial
 */
void printErrorLog();

/**
 * @brief Сохранение логов ошибок
 * 
 * @return true если сохранено успешно
 */
bool saveErrorLog();

// Макросы для удобной регистрации ошибок
#define REPORT_WARNING(module, code, msg) reportError(module, ERROR_TYPE_WARNING, code, msg, __func__)
#define REPORT_ERROR(module, code, msg) reportError(module, ERROR_TYPE_ERROR, code, msg, __func__)
#define REPORT_CRITICAL(module, code, msg) reportError(module, ERROR_TYPE_CRITICAL, code, msg, __func__)
#define REPORT_FATAL(module, code, msg) reportError(module, ERROR_TYPE_FATAL, code, msg, __func__)

// Try-catch обертки для критичных функций
#define TRY_EXECUTE(func, onError) \
    do { \
        if (!(func)) { \
            onError; \
        } \
    } while(0)

#define SAFE_EXECUTE(func, module, errorMsg) \
    do { \
        if (!(func)) { \
            REPORT_ERROR(module, -1, errorMsg); \
        } \
    } while(0)

#endif // ERROR_HANDLER_H
