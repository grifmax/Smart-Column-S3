/**
 * Smart-Column S3 - Система безопасности
 * 
 * Проверка аварийных условий и защитные действия
 */

#ifndef SAFETY_H
#define SAFETY_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace Safety {
    /**
     * Проверка всех аварийных условий
     * @param state Состояние системы (модифицируется при аварии)
     * @param settings Настройки
     */
    void check(SystemState& state, const Settings& settings);
    
    /**
     * Аварийная остановка всего
     * @param state Состояние системы
     * @param alarm Описание аварии
     */
    void emergencyStop(SystemState& state, const Alarm& alarm);
    
    /**
     * Сброс аварии (после устранения)
     * @param state Состояние системы
     */
    void resetAlarm(SystemState& state);
    
    /**
     * Проверка прорыва паров
     * @param temps Температуры
     * @return true если авария
     */
    bool checkVaporBreakthrough(const Temperatures& temps);
    
    /**
     * Проверка перегрева воды
     * @param temps Температуры
     * @return true если авария
     */
    bool checkWaterOverheat(const Temperatures& temps);
    
    /**
     * Проверка потока воды
     * @param flow Данные потока
     * @param heaterOn Нагреватель включён
     * @return true если авария
     */
    bool checkWaterFlow(const WaterFlow& flow, bool heaterOn);
    
    /**
     * Проверка захлёба колонны
     * @param pressure Давление
     * @return true если авария
     */
    bool checkColumnFlood(const Pressure& pressure);
    
    /**
     * Проверка напряжения сети
     * @param power Параметры питания
     * @return AlarmLevel (NONE, WARNING, CRITICAL)
     */
    AlarmLevel checkVoltage(const Power& power);
    
    /**
     * Проверка валидности датчиков
     * @param temps Температуры
     * @return true если есть сбой
     */
    bool checkSensorFailure(const Temperatures& temps);
    
    /**
     * Проверка работоспособности ТЭНа
     * @param power Параметры питания
     * @param targetPercent Заданная мощность
     * @return true если ТЭН неисправен
     */
    bool checkHeaterFailure(const Power& power, uint8_t targetPercent);
    
    /**
     * Обработка захлёба с авторестартом
     * @param state Состояние системы
     * @param settings Настройки
     */
    void handleFlood(SystemState& state, const Settings& settings);
}

#endif // SAFETY_H
