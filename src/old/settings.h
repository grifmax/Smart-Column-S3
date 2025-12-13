/**
 * @file settings.h
 * @brief Централизованное управление настройками системы
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include "config.h"

// Структура всех системных настроек
struct SystemSettings {
    // Настройки нагревателя
    struct {
        int maxPowerWatts;
        PowerControlMode controlMode;
        PIControllerSettings piSettings;
        bool pzemEnabled;
    } heaterSettings;
    
    // Настройки насоса
    PumpSettings pumpSettings;
    
    // Настройки ректификации
    RectificationParams rectificationSettings;
    
    // Настройки дистилляции
    DistillationParams distillationSettings;
    
    // Настройки безопасности
    struct {
        float maxRuntimeHours;
        float maxCubeTemp;
        float maxTempRiseRate;
        float minWaterOutTemp;
        float maxWaterOutTemp;
        bool emergencyStopEnabled;
        bool watchdogEnabled;
    } safetySettings;
    
    // Настройки датчиков температуры
    uint8_t tempSensorAddresses[MAX_TEMP_SENSORS][8];
    bool tempSensorEnabled[MAX_TEMP_SENSORS];
    float tempSensorCalibration[MAX_TEMP_SENSORS];
    
    // WiFi настройки
    char wifiSSID[32];
    char wifiPassword[64];
    bool wifiAPMode;
    
    // Настройки звука
    bool soundEnabled;
    int soundVolume;
    
    // Настройки дисплея
    DisplaySettings displaySettings;
    
    // Интервалы обновления
    int tempUpdateInterval;
    int tempReportInterval;
    int safetyCheckInterval;
    
    // Версия настроек (для миграции)
    uint16_t settingsVersion;
};

// Глобальная переменная настроек
extern SystemSettings sysSettings;

/**
 * @brief Инициализация модуля настроек
 * 
 * Устанавливает значения по умолчанию
 */
void initSettings();

/**
 * @brief Загрузка настроек из EEPROM
 * 
 * @return true если настройки успешно загружены
 */
bool loadSystemSettings();

/**
 * @brief Сохранение настроек в EEPROM
 * 
 * @return true если настройки успешно сохранены
 */
bool saveSystemSettings();

/**
 * @brief Сброс настроек к значениям по умолчанию
 */
void resetSystemSettings();

/**
 * @brief Вывод текущих настроек в Serial
 */
void printSystemSettings();

/**
 * @brief Проверка валидности настроек
 * 
 * @return true если все настройки в допустимых пределах
 */
bool validateSettings();

/**
 * @brief Применение настроек к системе
 * 
 * Обновляет все модули в соответствии с текущими настройками
 */
void applySettings();

/**
 * @brief Получение версии настроек
 * 
 * @return Версия настроек
 */
uint16_t getSettingsVersion();

/**
 * @brief Миграция настроек между версиями
 * 
 * @param fromVersion Исходная версия
 * @param toVersion Целевая версия
 * @return true если миграция успешна
 */
bool migrateSettings(uint16_t fromVersion, uint16_t toVersion);

#endif // SETTINGS_H
