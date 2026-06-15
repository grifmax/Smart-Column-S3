/**
 * Smart-Column S3 - Драйвер нагревателя
 * 
 * ШИМ управление SSR через PC817
 */

#ifndef HEATER_H
#define HEATER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace Heater {
    struct Diagnostics {
        bool triacMode = (HEATER_CONTROL_MODE == HEATER_MODE_TRIAC);
        bool active = false;
        uint8_t mainPowerPercent = 0;
        uint8_t powerSetPercent = 0;
        uint16_t targetPowerWatts = 0;
        float actualPowerWatts = 0.0f;
        float powerErrorWatts = 0.0f;
        bool closedLoopActive = false;
        bool boosterEnabled = false;
        bool zeroCrossSeen = false;
        uint32_t zeroCrossCount = 0;
        uint32_t triacFireCount = 0;
        uint16_t triacDelayUs = TRIAC_MAX_ALPHA_US;
    };

    /**
     * Инициализация ШИМ канала
     */
    void init();
    
    /**
     * Установка мощности
     * @param percent Мощность 0-100%
     */
    void setPower(uint8_t percent);

    /**
     * Установка мощности в ваттах.
     * Для TRIAC-канала это целевая уставка, которую контроллер старается держать
     * по реальной телеметрии PZEM.
     */
    void setPowerWatts(uint16_t watts);
    
    /**
     * Получение текущей мощности
     * @return Мощность 0-100%
     */
    uint8_t getPower();

    /**
     * Получение текущей целевой уставки в ваттах.
     */
    uint16_t getTargetPowerWatts();

    void setBoosterEnabled(bool enabled);
    bool isBoosterEnabled();
    Diagnostics getDiagnostics();

    /**
     * Установка угла отсечки для симистора (только для HEATER_MODE_TRIAC)
     * @param delayUs Задержка в микросекундах (0-10000) после перехода через ноль
     */
    void setTriacDelay(uint16_t delayUs);
    
    /**
     * Аварийное отключение
     */
    void emergencyStop();
    
    /**
     * Плавный разгон до заданной мощности
     * @param targetPercent Целевая мощность
     * @param rampTimeMs Время разгона (мс)
     */
    void rampTo(uint8_t targetPercent, uint32_t rampTimeMs);
    
    /**
     * Проверка состояния (для диагностики)
     * @param actualPower Реальная мощность от датчиков
     * @return true если ТЭН работает корректно
     */
    bool checkHealth(float actualPower);

    /**
     * Обновление плавного разгона (вызывать из loop каждую итерацию)
     * PERF-3 fix: активирует логику rampTo() через millis()
     */
    void update();
}

#endif // HEATER_H
