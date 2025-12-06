/**
 * @file safety.cpp
 * @brief Реализация системы безопасности для ректификационной колонны
 */

#include "safety.h"
#include "config.h"
#include "temp_sensors.h"
#include "heater.h"
#include "pump.h"
#include "valve.h"
#include <esp_system.h>

// Текущий статус безопасности
static SafetyStatus currentStatus;

// Настройки безопасности
static struct {
    float maxRuntimeHours = 12.0;
    float maxCubeTemp = 105.0;
    float maxTempRiseRate = 5.0;  // °C/мин
    float minWaterOutTemp = 15.0;
    float maxWaterOutTemp = 45.0;
    bool enabled = true;
} safetySettings;

// Переменные для отслеживания
static unsigned long processStartTime = 0;
static float lastCubeTemp = 0.0;
static unsigned long lastTempCheckTime = 0;
static bool emergencyStopActive = false;
static int errorCount = 0;

// Инициализация системы безопасности
void initSafety() {
    Serial.println("Инициализация системы безопасности...");
    
    // Сброс статуса
    currentStatus.isSystemSafe = true;
    currentStatus.errorCode = SAFETY_OK;
    currentStatus.errorTime = 0;
    currentStatus.errorDescription = "";
    currentStatus.isSensorError = false;
    currentStatus.isTemperatureError = false;
    currentStatus.isWaterFlowError = false;
    currentStatus.isPressureError = false;
    currentStatus.isRuntimeError = false;
    currentStatus.isEmergencyStop = false;
    currentStatus.isWatchdogReset = false;
    
    // Проверка причины последнего сброса
    esp_reset_reason_t resetReason = esp_reset_reason();
    if (resetReason == ESP_RST_WDT || resetReason == ESP_RST_TASK_WDT || 
        resetReason == ESP_RST_INT_WDT) {
        currentStatus.isWatchdogReset = true;
        Serial.println("ПРЕДУПРЕЖДЕНИЕ: Обнаружена перезагрузка по Watchdog!");
    }
    
    processStartTime = millis();
    lastTempCheckTime = millis();
    emergencyStopActive = false;
    errorCount = 0;
    
    Serial.println("Система безопасности инициализирована");
}

// Обновление состояния системы безопасности
void updateSafety() {
    if (!safetySettings.enabled) {
        return;
    }
    
    unsigned long currentTime = millis();
    bool safetyViolation = false;
    SafetyErrorCode errorCode = SAFETY_OK;
    String errorDescription = "";
    
    // Сброс флагов ошибок
    currentStatus.isSensorError = false;
    currentStatus.isTemperatureError = false;
    currentStatus.isWaterFlowError = false;
    currentStatus.isPressureError = false;
    currentStatus.isRuntimeError = false;
    
    // 1. Проверка подключения критических датчиков
    if (!isSensorConnected(TEMP_CUBE)) {
        safetyViolation = true;
        errorCode = SAFETY_SENSOR_DISCONNECTED;
        errorDescription = "Датчик температуры куба отключен";
        currentStatus.isSensorError = true;
        Serial.println("ОШИБКА БЕЗОПАСНОСТИ: " + errorDescription);
    }
    
    if (!isSensorConnected(TEMP_REFLUX)) {
        safetyViolation = true;
        errorCode = SAFETY_SENSOR_DISCONNECTED;
        errorDescription = "Датчик температуры дефлегматора отключен";
        currentStatus.isSensorError = true;
        Serial.println("ОШИБКА БЕЗОПАСНОСТИ: " + errorDescription);
    }
    
    // 2. Проверка температуры куба
    float cubeTemp = getTemperature(TEMP_CUBE);
    if (cubeTemp > safetySettings.maxCubeTemp) {
        safetyViolation = true;
        errorCode = SAFETY_TEMP_TOO_HIGH;
        errorDescription = "Превышена максимальная температура куба: " + 
                          String(cubeTemp, 1) + "°C (макс: " + 
                          String(safetySettings.maxCubeTemp, 1) + "°C)";
        currentStatus.isTemperatureError = true;
        Serial.println("ОШИБКА БЕЗОПАСНОСТИ: " + errorDescription);
    }
    
    // 3. Проверка скорости роста температуры
    if (currentTime - lastTempCheckTime >= 60000) { // Проверка каждую минуту
        float tempRise = cubeTemp - lastCubeTemp;
        float riseRate = tempRise; // °C/мин
        
        if (riseRate > safetySettings.maxTempRiseRate) {
            safetyViolation = true;
            errorCode = SAFETY_TEMP_RISE_TOO_FAST;
            errorDescription = "Слишком быстрый рост температуры: " + 
                              String(riseRate, 2) + "°C/мин (макс: " + 
                              String(safetySettings.maxTempRiseRate, 1) + "°C/мин)";
            currentStatus.isTemperatureError = true;
            Serial.println("ОШИБКА БЕЗОПАСНОСТИ: " + errorDescription);
        }
        
        lastCubeTemp = cubeTemp;
        lastTempCheckTime = currentTime;
    }
    
    // 4. Проверка температуры воды охлаждения (если датчик подключен)
    if (isSensorConnected(TEMP_WATER_OUT)) {
        float waterTemp = getTemperature(TEMP_WATER_OUT);
        
        if (waterTemp < safetySettings.minWaterOutTemp) {
            safetyViolation = true;
            errorCode = SAFETY_WATER_FLOW_LOW;
            errorDescription = "Температура воды слишком низкая: " + 
                              String(waterTemp, 1) + "°C (мин: " + 
                              String(safetySettings.minWaterOutTemp, 1) + "°C)";
            currentStatus.isWaterFlowError = true;
            Serial.println("ПРЕДУПРЕЖДЕНИЕ: " + errorDescription);
        }
        
        if (waterTemp > safetySettings.maxWaterOutTemp) {
            safetyViolation = true;
            errorCode = SAFETY_WATER_FLOW_LOW;
            errorDescription = "Температура воды слишком высокая: " + 
                              String(waterTemp, 1) + "°C (макс: " + 
                              String(safetySettings.maxWaterOutTemp, 1) + "°C) - возможен недостаточный поток";
            currentStatus.isWaterFlowError = true;
            Serial.println("ОШИБКА БЕЗОПАСНОСТИ: " + errorDescription);
        }
    }
    
    // 5. Проверка максимального времени работы
    unsigned long runtimeSeconds = (currentTime - processStartTime) / 1000;
    float runtimeHours = runtimeSeconds / 3600.0;
    
    if (runtimeHours > safetySettings.maxRuntimeHours) {
        safetyViolation = true;
        errorCode = SAFETY_RUNTIME_EXCEEDED;
        errorDescription = "Превышено максимальное время работы: " + 
                          String(runtimeHours, 1) + " ч (макс: " + 
                          String(safetySettings.maxRuntimeHours, 1) + " ч)";
        currentStatus.isRuntimeError = true;
        Serial.println("ОШИБКА БЕЗОПАСНОСТИ: " + errorDescription);
    }
    
    // 6. Проверка аварийной остановки
    if (emergencyStopActive) {
        safetyViolation = true;
        errorCode = SAFETY_EMERGENCY_STOP;
        errorDescription = "Активна аварийная остановка";
        currentStatus.isEmergencyStop = true;
    }
    
    // Обновление статуса
    if (safetyViolation) {
        if (currentStatus.isSystemSafe) {
            // Первое обнаружение ошибки
            currentStatus.isSystemSafe = false;
            currentStatus.errorCode = errorCode;
            currentStatus.errorTime = currentTime;
            currentStatus.errorDescription = errorDescription;
            errorCount++;
            
            // АВАРИЙНОЕ ОТКЛЮЧЕНИЕ
            Serial.println("\n=== АВАРИЙНОЕ ОТКЛЮЧЕНИЕ ===");
            Serial.println("Причина: " + errorDescription);
            Serial.println("Код ошибки: " + String(errorCode));
            Serial.println("Время: " + String(currentTime / 1000) + " сек");
            Serial.println("===========================\n");
            
            // Отключаем все исполнительные устройства
            heaterOff();
            pumpStop();
            valveClose();
            
            // Устанавливаем флаг аварийной остановки
            emergencyStopActive = true;
        }
    } else {
        // Все в порядке
        if (!currentStatus.isSystemSafe && !emergencyStopActive) {
            // Восстановление после временной ошибки
            currentStatus.isSystemSafe = true;
            currentStatus.errorCode = SAFETY_OK;
            currentStatus.errorDescription = "";
        }
    }
}

// Получение текущего статуса безопасности
SafetyStatus getSafetyStatus() {
    return currentStatus;
}

// Проверка условий безопасности
bool checkSafetyConditions() {
    updateSafety();
    return currentStatus.isSystemSafe;
}

// Установка максимального времени работы
void setSafetyMaxRuntime(float hours) {
    safetySettings.maxRuntimeHours = hours;
    Serial.println("Установлено максимальное время работы: " + String(hours, 1) + " ч");
}

// Установка максимальной температуры куба
void setSafetyMaxCubeTemp(float temp) {
    safetySettings.maxCubeTemp = temp;
    Serial.println("Установлена максимальная температура куба: " + String(temp, 1) + " °C");
}

// Установка максимальной скорости роста температуры
void setSafetyMaxTempRiseRate(float rate) {
    safetySettings.maxTempRiseRate = rate;
    Serial.println("Установлена максимальная скорость роста температуры: " + String(rate, 1) + " °C/мин");
}

// Установка минимальной температуры воды на выходе
void setSafetyMinWaterOutTemp(float temp) {
    safetySettings.minWaterOutTemp = temp;
    Serial.println("Установлена минимальная температура воды: " + String(temp, 1) + " °C");
}

// Установка максимальной температуры воды на выходе
void setSafetyMaxWaterOutTemp(float temp) {
    safetySettings.maxWaterOutTemp = temp;
    Serial.println("Установлена максимальная температура воды: " + String(temp, 1) + " °C");
}

// Сброс ошибок безопасности
bool resetSafetyErrors() {
    if (!emergencyStopActive) {
        return false; // Нечего сбрасывать
    }
    
    Serial.println("Сброс ошибок безопасности...");
    
    // Проверяем, устранена ли причина ошибки
    updateSafety();
    
    if (!currentStatus.isSensorError && 
        !currentStatus.isTemperatureError && 
        !currentStatus.isWaterFlowError) {
        // Ошибки устранены, можно сбросить
        emergencyStopActive = false;
        currentStatus.isSystemSafe = true;
        currentStatus.errorCode = SAFETY_OK;
        currentStatus.errorTime = 0;
        currentStatus.errorDescription = "";
        currentStatus.isEmergencyStop = false;
        
        Serial.println("Ошибки безопасности успешно сброшены");
        return true;
    } else {
        Serial.println("ОШИБКА: Невозможно сбросить ошибки - причина не устранена");
        return false;
    }
}

// Инициирование аварийной остановки
void triggerEmergencyStop(const String& reason) {
    Serial.println("\n=== АВАРИЙНАЯ ОСТАНОВКА ===");
    Serial.println("Причина: " + reason);
    Serial.println("===========================\n");
    
    currentStatus.isSystemSafe = false;
    currentStatus.errorCode = SAFETY_EMERGENCY_STOP;
    currentStatus.errorTime = millis();
    currentStatus.errorDescription = reason;
    currentStatus.isEmergencyStop = true;
    emergencyStopActive = true;
    errorCount++;
    
    // Отключаем все исполнительные устройства
    heaterOff();
    pumpStop();
    valveClose();
}

// Получение описания кода ошибки
String getSafetyErrorDescription(SafetyErrorCode errorCode) {
    switch (errorCode) {
        case SAFETY_OK:
            return "Нет ошибок";
        case SAFETY_SENSOR_DISCONNECTED:
            return "Датчик отключен";
        case SAFETY_TEMP_TOO_HIGH:
            return "Температура слишком высока";
        case SAFETY_TEMP_RISE_TOO_FAST:
            return "Слишком быстрый рост температуры";
        case SAFETY_WATER_FLOW_LOW:
            return "Недостаточный поток воды охлаждения";
        case SAFETY_RUNTIME_EXCEEDED:
            return "Превышено максимальное время работы";
        case SAFETY_EMERGENCY_STOP:
            return "Аварийная остановка";
        case SAFETY_WATCHDOG_RESET:
            return "Сброс по Watchdog";
        case SAFETY_PRESSURE_ERROR:
            return "Ошибка давления";
        case SAFETY_MULTIPLE_ERRORS:
            return "Множественные ошибки";
        default:
            return "Неизвестная ошибка";
    }
}

// Проверка, была ли система перезагружена из-за watchdog
bool wasWatchdogReset() {
    return currentStatus.isWatchdogReset;
}

// Включение/выключение системы безопасности
void setSafetyEnabled(bool enabled) {
    safetySettings.enabled = enabled;
    if (enabled) {
        Serial.println("Система безопасности ВКЛЮЧЕНА");
    } else {
        Serial.println("ВНИМАНИЕ: Система безопасности ВЫКЛЮЧЕНА!");
    }
}

// Проверка, включена ли система безопасности
bool isSafetyEnabled() {
    return safetySettings.enabled;
}

// Получение времени с момента последней ошибки
unsigned long getTimeSinceLastError() {
    if (currentStatus.errorTime == 0) {
        return 0;
    }
    return (millis() - currentStatus.errorTime) / 1000;
}
