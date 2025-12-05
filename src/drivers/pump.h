/**
 * Smart-Column S3 - Драйвер перистальтического насоса
 * 
 * Шаговый двигатель NEMA17 + TMC2209
 */

#ifndef PUMP_H
#define PUMP_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace Pump {
    /**
     * Инициализация драйвера
     */
    void init();
    
    /**
     * Запуск насоса с заданной скоростью
     * @param mlPerHour Скорость в мл/час
     */
    void start(float mlPerHour);
    
    /**
     * Остановка насоса
     */
    void stop();
    
    /**
     * Установка скорости (без остановки)
     * @param mlPerHour Скорость в мл/час
     */
    void setSpeed(float mlPerHour);
    
    /**
     * Получение текущей скорости
     * @return Скорость в мл/час
     */
    float getSpeed();
    
    /**
     * Проверка работы
     * @return true если насос работает
     */
    bool isRunning();
    
    /**
     * Получение общего объёма
     * @return Прокачано мл
     */
    float getTotalVolume();
    
    /**
     * Сброс счётчика объёма
     */
    void resetVolume();
    
    /**
     * Установка калибровки
     * @param mlPerRev Миллилитров на оборот
     */
    void setCalibration(float mlPerRev);
    
    /**
     * Обработчик (вызывать в loop или по таймеру)
     */
    void update();
}

#endif // PUMP_H
