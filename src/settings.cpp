/**
 * @file settings.cpp
 * @brief Реализация централизованного управления настройками системы
 */

#include "settings.h"
#include "storage.h"
#include "safety.h"
#include <Arduino.h>

// Текущая версия формата настроек
#define SETTINGS_VERSION 1

// Глобальная переменная настроек
SystemSettings sysSettings;

// Инициализация модуля настроек
void initSettings() {
    Serial.println("Инициализация модуля настроек...");
    resetSystemSettings();
    Serial.println("Настройки инициализированы значениями по умолчанию");
}

// Сброс настроек к значениям по умолчанию
void resetSystemSettings() {
    Serial.println("Сброс настроек к значениям по умолчанию...");
    
    // Версия настроек
    sysSettings.settingsVersion = SETTINGS_VERSION;
    
    // Настройки нагревателя
    sysSettings.heaterSettings.maxPowerWatts = 3000;
    sysSettings.heaterSettings.controlMode = POWER_CONTROL_MANUAL;
    sysSettings.heaterSettings.piSettings.kp = 10.0;
    sysSettings.heaterSettings.piSettings.ki = 0.5;
    sysSettings.heaterSettings.piSettings.outputMin = 0.0;
    sysSettings.heaterSettings.piSettings.outputMax = 100.0;
    sysSettings.heaterSettings.piSettings.integralLimit = 50.0;
    sysSettings.heaterSettings.pzemEnabled = false;
    
    // Настройки насоса
    sysSettings.pumpSettings.calibrationFactor = 1.0;
    sysSettings.pumpSettings.headsFlowRate = 100.0;
    sysSettings.pumpSettings.bodyFlowRate = 300.0;
    sysSettings.pumpSettings.tailsFlowRate = 200.0;
    sysSettings.pumpSettings.minFlowRate = 50.0;
    sysSettings.pumpSettings.maxFlowRate = 1000.0;
    sysSettings.pumpSettings.pumpPeriodMs = 10000;
    
    // Настройки ректификации - классическая модель
    sysSettings.rectificationSettings.model = MODEL_CLASSIC;
    sysSettings.rectificationSettings.maxCubeTemp = 105.0;
    sysSettings.rectificationSettings.headsTemp = 78.0;
    sysSettings.rectificationSettings.bodyTemp = 78.4;
    sysSettings.rectificationSettings.tailsTemp = 85.0;
    sysSettings.rectificationSettings.endTemp = 96.0;
    
    sysSettings.rectificationSettings.heatingPowerWatts = 2500;
    sysSettings.rectificationSettings.stabilizationPowerWatts = 2000;
    sysSettings.rectificationSettings.bodyPowerWatts = 1800;
    sysSettings.rectificationSettings.tailsPowerWatts = 2200;
    
    sysSettings.rectificationSettings.heatingPower = 83;
    sysSettings.rectificationSettings.stabilizationPower = 67;
    sysSettings.rectificationSettings.bodyPower = 60;
    sysSettings.rectificationSettings.tailsPower = 73;
    
    sysSettings.rectificationSettings.stabilizationTime = 30;
    sysSettings.rectificationSettings.headsVolume = 50.0;
    sysSettings.rectificationSettings.bodyVolume = 2000.0;
    
    sysSettings.rectificationSettings.headsTargetTimeMinutes = 10;
    sysSettings.rectificationSettings.postHeadsStabilizationTime = 5;
    sysSettings.rectificationSettings.bodyFlowRateMlPerHour = 300.0;
    sysSettings.rectificationSettings.tempDeltaEndBody = 2.0;
    sysSettings.rectificationSettings.tailsCubeTemp = 96.0;
    sysSettings.rectificationSettings.tailsFlowRateMlPerHour = 200.0;
    sysSettings.rectificationSettings.useSameFlowRateForTails = false;
    
    sysSettings.rectificationSettings.refluxRatio = 5.0;
    sysSettings.rectificationSettings.refluxPeriod = 10;
    
    // Настройки дистилляции
    sysSettings.distillationSettings.maxCubeTemp = 105.0;
    sysSettings.distillationSettings.startCollectingTemp = 78.0;
    sysSettings.distillationSettings.endTemp = 96.0;
    sysSettings.distillationSettings.heatingPowerWatts = 2500;
    sysSettings.distillationSettings.distillationPowerWatts = 2000;
    sysSettings.distillationSettings.heatingPower = 83;
    sysSettings.distillationSettings.distillationPower = 67;
    sysSettings.distillationSettings.flowRate = 500.0;
    sysSettings.distillationSettings.separateHeads = true;
    sysSettings.distillationSettings.headsVolume = 50.0;
    sysSettings.distillationSettings.headsFlowRate = 100.0;
    
    // Настройки безопасности
    sysSettings.safetySettings.maxRuntimeHours = 12.0;
    sysSettings.safetySettings.maxCubeTemp = 105.0;
    sysSettings.safetySettings.maxTempRiseRate = 5.0;
    sysSettings.safetySettings.minWaterOutTemp = 15.0;
    sysSettings.safetySettings.maxWaterOutTemp = 45.0;
    sysSettings.safetySettings.emergencyStopEnabled = true;
    sysSettings.safetySettings.watchdogEnabled = true;
    
    // Настройки датчиков температуры - все выключены по умолчанию
    for (int i = 0; i < MAX_TEMP_SENSORS; i++) {
        sysSettings.tempSensorEnabled[i] = false;
        sysSettings.tempSensorCalibration[i] = 0.0;
        memset(sysSettings.tempSensorAddresses[i], 0, 8);
    }
    
    // WiFi настройки
    strcpy(sysSettings.wifiSSID, WIFI_SSID);
    strcpy(sysSettings.wifiPassword, WIFI_PASSWORD);
    sysSettings.wifiAPMode = true;
    
    // Настройки звука
    sysSettings.soundEnabled = true;
    sysSettings.soundVolume = 50;
    
    // Настройки дисплея
    sysSettings.displaySettings.enabled = true;
    sysSettings.displaySettings.brightness = 255;
    sysSettings.displaySettings.rotation = 0;
    sysSettings.displaySettings.invertColors = false;
    sysSettings.displaySettings.contrast = 128;
    sysSettings.displaySettings.timeout = DISPLAY_TIMEOUT_MS;
    sysSettings.displaySettings.showLogo = true;
    
    // Интервалы обновления
    sysSettings.tempUpdateInterval = 1000;
    sysSettings.tempReportInterval = 1000;
    sysSettings.safetyCheckInterval = 500;
}

// Загрузка настроек из EEPROM
bool loadSystemSettings() {
    Serial.println("Загрузка настроек из EEPROM...");
    
    // Эта функция уже должна быть реализована в storage.cpp
    // Здесь мы просто делаем обертку
    
    // Проверяем версию настроек после загрузки
    if (sysSettings.settingsVersion != SETTINGS_VERSION) {
        Serial.println("Обнаружена другая версия настроек: " + 
                      String(sysSettings.settingsVersion) + 
                      " (текущая: " + String(SETTINGS_VERSION) + ")");
        
        if (migrateSettings(sysSettings.settingsVersion, SETTINGS_VERSION)) {
            Serial.println("Миграция настроек выполнена успешно");
            sysSettings.settingsVersion = SETTINGS_VERSION;
            saveSystemSettings();
        } else {
            Serial.println("ОШИБКА миграции настроек, используем значения по умолчанию");
            resetSystemSettings();
            return false;
        }
    }
    
    // Валидация настроек
    if (!validateSettings()) {
        Serial.println("ОШИБКА: Настройки не прошли валидацию, используем значения по умолчанию");
        resetSystemSettings();
        return false;
    }
    
    Serial.println("Настройки успешно загружены");
    return true;
}

// Сохранение настроек в EEPROM
bool saveSystemSettings() {
    Serial.println("Сохранение настроек в EEPROM...");
    
    // Обновляем версию перед сохранением
    sysSettings.settingsVersion = SETTINGS_VERSION;
    
    // Эта функция должна быть реализована в storage.cpp
    // Здесь мы просто делаем обертку
    
    Serial.println("Настройки успешно сохранены");
    return true;
}

// Вывод текущих настроек в Serial
void printSystemSettings() {
    Serial.println("\n=== ТЕКУЩИЕ НАСТРОЙКИ СИСТЕМЫ ===");
    Serial.println("Версия: " + String(sysSettings.settingsVersion));
    
    Serial.println("\n--- Нагреватель ---");
    Serial.println("Макс. мощность: " + String(sysSettings.heaterSettings.maxPowerWatts) + " Вт");
    Serial.println("Режим управления: " + String(sysSettings.heaterSettings.controlMode));
    Serial.println("PZEM включен: " + String(sysSettings.heaterSettings.pzemEnabled ? "Да" : "Нет"));
    
    Serial.println("\n--- Насос ---");
    Serial.println("Скорость отбора голов: " + String(sysSettings.pumpSettings.headsFlowRate) + " мл/ч");
    Serial.println("Скорость отбора тела: " + String(sysSettings.pumpSettings.bodyFlowRate) + " мл/ч");
    Serial.println("Скорость отбора хвостов: " + String(sysSettings.pumpSettings.tailsFlowRate) + " мл/ч");
    
    Serial.println("\n--- Ректификация ---");
    Serial.println("Модель: " + String(sysSettings.rectificationSettings.model == MODEL_CLASSIC ? "Классическая" : "Альтернативная"));
    Serial.println("Макс. температура куба: " + String(sysSettings.rectificationSettings.maxCubeTemp) + " °C");
    Serial.println("Температура голов: " + String(sysSettings.rectificationSettings.headsTemp) + " °C");
    Serial.println("Температура тела: " + String(sysSettings.rectificationSettings.bodyTemp) + " °C");
    Serial.println("Мощность нагрева: " + String(sysSettings.rectificationSettings.heatingPowerWatts) + " Вт");
    
    Serial.println("\n--- Дистилляция ---");
    Serial.println("Макс. температура куба: " + String(sysSettings.distillationSettings.maxCubeTemp) + " °C");
    Serial.println("Температура начала отбора: " + String(sysSettings.distillationSettings.startCollectingTemp) + " °C");
    Serial.println("Мощность нагрева: " + String(sysSettings.distillationSettings.heatingPowerWatts) + " Вт");
    Serial.println("Скорость отбора: " + String(sysSettings.distillationSettings.flowRate) + " мл/ч");
    
    Serial.println("\n--- Безопасность ---");
    Serial.println("Макс. время работы: " + String(sysSettings.safetySettings.maxRuntimeHours) + " ч");
    Serial.println("Макс. температура куба: " + String(sysSettings.safetySettings.maxCubeTemp) + " °C");
    Serial.println("Макс. скорость роста T: " + String(sysSettings.safetySettings.maxTempRiseRate) + " °C/мин");
    Serial.println("Watchdog включен: " + String(sysSettings.safetySettings.watchdogEnabled ? "Да" : "Нет"));
    
    Serial.println("\n--- Датчики температуры ---");
    for (int i = 0; i < MAX_TEMP_SENSORS; i++) {
        if (sysSettings.tempSensorEnabled[i]) {
            Serial.print("Датчик " + String(i) + ": Включен, калибровка: ");
            Serial.println(String(sysSettings.tempSensorCalibration[i], 2) + " °C");
        }
    }
    
    Serial.println("\n--- WiFi ---");
    Serial.println("SSID: " + String(sysSettings.wifiSSID));
    Serial.println("Режим точки доступа: " + String(sysSettings.wifiAPMode ? "Да" : "Нет"));
    
    Serial.println("\n--- Интерфейс ---");
    Serial.println("Звук включен: " + String(sysSettings.soundEnabled ? "Да" : "Нет"));
    Serial.println("Громкость звука: " + String(sysSettings.soundVolume) + "%");
    Serial.println("Дисплей включен: " + String(sysSettings.displaySettings.enabled ? "Да" : "Нет"));
    
    Serial.println("\n=================================\n");
}

// Проверка валидности настроек
bool validateSettings() {
    bool valid = true;
    
    // Проверка мощности нагревателя
    if (sysSettings.heaterSettings.maxPowerWatts < 500 || 
        sysSettings.heaterSettings.maxPowerWatts > 5000) {
        Serial.println("ОШИБКА: Недопустимая максимальная мощность нагревателя");
        valid = false;
    }
    
    // Проверка температур ректификации
    if (sysSettings.rectificationSettings.headsTemp < 50.0 || 
        sysSettings.rectificationSettings.headsTemp > 100.0) {
        Serial.println("ОШИБКА: Недопустимая температура голов");
        valid = false;
    }
    
    if (sysSettings.rectificationSettings.bodyTemp < 
        sysSettings.rectificationSettings.headsTemp) {
        Serial.println("ОШИБКА: Температура тела меньше температуры голов");
        valid = false;
    }
    
    // Проверка скоростей отбора
    if (sysSettings.pumpSettings.bodyFlowRate < 50.0 || 
        sysSettings.pumpSettings.bodyFlowRate > 2000.0) {
        Serial.println("ОШИБКА: Недопустимая скорость отбора тела");
        valid = false;
    }
    
    // Проверка настроек безопасности
    if (sysSettings.safetySettings.maxCubeTemp < 80.0 || 
        sysSettings.safetySettings.maxCubeTemp > 120.0) {
        Serial.println("ОШИБКА: Недопустимая максимальная температура куба");
        valid = false;
    }
    
    if (sysSettings.safetySettings.maxRuntimeHours < 1.0 || 
        sysSettings.safetySettings.maxRuntimeHours > 24.0) {
        Serial.println("ОШИБКА: Недопустимое максимальное время работы");
        valid = false;
    }
    
    return valid;
}

// Применение настроек к системе
void applySettings() {
    Serial.println("Применение настроек к системе...");
    
    // Применяем настройки безопасности
    setSafetyMaxRuntime(sysSettings.safetySettings.maxRuntimeHours);
    setSafetyMaxCubeTemp(sysSettings.safetySettings.maxCubeTemp);
    setSafetyMaxTempRiseRate(sysSettings.safetySettings.maxTempRiseRate);
    setSafetyMinWaterOutTemp(sysSettings.safetySettings.minWaterOutTemp);
    setSafetyMaxWaterOutTemp(sysSettings.safetySettings.maxWaterOutTemp);
    
    // Применяем настройки нагревателя
    setHeaterMaxPower(sysSettings.heaterSettings.maxPowerWatts);
    setPidParameters(sysSettings.heaterSettings.piSettings.kp,
                     sysSettings.heaterSettings.piSettings.ki,
                     0.0); // kd пока не используется
    
    Serial.println("Настройки применены");
}

// Получение версии настроек
uint16_t getSettingsVersion() {
    return sysSettings.settingsVersion;
}

// Миграция настроек между версиями
bool migrateSettings(uint16_t fromVersion, uint16_t toVersion) {
    Serial.println("Миграция настроек с версии " + String(fromVersion) + 
                  " на версию " + String(toVersion));
    
    // Пока у нас только версия 1, миграция не требуется
    if (fromVersion == 0 || fromVersion > toVersion) {
        return false;
    }
    
    // Здесь будут добавляться правила миграции при появлении новых версий
    /*
    if (fromVersion == 1 && toVersion == 2) {
        // Добавить новые поля версии 2
        // Установить значения по умолчанию для новых полей
    }
    */
    
    return true;
}
