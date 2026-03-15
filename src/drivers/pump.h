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
    struct Diagnostics {
        bool taskAlive = false;
        bool mutexReady = false;
        uint32_t taskLoopCount = 0;
        uint32_t counterUpdateCount = 0;
        uint32_t cooperativeSleepCount = 0;
        uint32_t fastYieldCount = 0;
        uint32_t lockTimeoutCount = 0;
        uint32_t lastLoopAtMs = 0;
        uint32_t lastYieldAtMs = 0;
        float speedMlH = 0.0f;
        uint32_t totalSteps = 0;
        float totalVolumeMl = 0.0f;
    };

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
     * Получение общего количества шагов
     * @return Количество шагов
     */
    uint32_t getTotalSteps();

    /**
     * Получение максимальной скорости (мл/час)
     * @return Максимальная скорость насоса
     */
    float getMaxSpeedMlH();
    
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

    Diagnostics getDiagnostics();
}

#endif // PUMP_H
