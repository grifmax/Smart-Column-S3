/**
 * Smart-Column S3 - NVS Manager
 * 
 * Хранение настроек в энергонезависимой памяти
 */

#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace NVSManager {
    /**
     * Инициализация NVS
     * @return true если успешно
     */
    bool init();
    
    /**
     * Загрузка всех настроек
     * @param settings Структура для записи
     * @return true если успешно
     */
    bool loadSettings(Settings& settings);
    
    /**
     * Сохранение всех настроек
     * @param settings Настройки для сохранения
     * @return true если успешно
     */
    bool saveSettings(const Settings& settings);
    
    /**
     * Сброс к заводским настройкам
     * @return true если успешно
     */
    bool resetToDefaults();
    
    // =========================================================================
    // ОТДЕЛЬНЫЕ СЕКЦИИ
    // =========================================================================
    
    bool loadWiFi(WiFiSettings& wifi);
    bool saveWiFi(const WiFiSettings& wifi);
    
    bool loadTelegram(TelegramSettings& tg);
    bool saveTelegram(const TelegramSettings& tg);
    
    bool loadEquipment(EquipmentSettings& eq);
    bool saveEquipment(const EquipmentSettings& eq);
    
    bool loadTempCalibration(TempCalibration& cal);
    bool saveTempCalibration(const TempCalibration& cal);
    
    bool loadHydrometerCalibration(HydrometerCalibration& cal);
    bool saveHydrometerCalibration(const HydrometerCalibration& cal);
    
    bool loadPumpCalibration(PumpCalibration& cal);
    bool savePumpCalibration(const PumpCalibration& cal);

    // PowerCalibration removed - not used in new architecture
    // bool loadPowerCalibration(PowerCalibration& cal);
    // bool savePowerCalibration(const PowerCalibration& cal);

    bool loadFractionator(FractionatorSettings& frac);
    bool saveFractionator(const FractionatorSettings& frac);
    
    bool loadRectParams(RectificationParams& params);
    bool saveRectParams(const RectificationParams& params);
    
    bool loadMashProfiles(MashProfile* profiles, uint8_t count);
    bool saveMashProfiles(const MashProfile* profiles, uint8_t count);
    
    // =========================================================================
    // УТИЛИТЫ
    // =========================================================================
    
    /**
     * Получение свободного места в NVS
     * @return Байт свободно
     */
    size_t getFreeSpace();
    
    /**
     * Очистка NVS
     */
    void eraseAll();
}

#endif // NVS_MANAGER_H
