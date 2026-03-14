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
#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
#include "driver/gptimer.h"
#include "driver/gpio.h"
#endif

namespace Heater {
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
     * Получение текущей мощности
     * @return Мощность 0-100%
     */
    uint8_t getPower();

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
