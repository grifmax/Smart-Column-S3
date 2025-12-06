/**
 * Smart-Column S3 - Драйвер датчиков
 * 
 * DS18B20 (температуры), BMP280 (атм. давление), 
 * ADS1115 + MPX5010DP (давление куба), ZMPT101B + ACS712 (мощность)
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace Sensors {
    /**
     * Инициализация всех датчиков
     */
    void init();
    
    /**
     * Чтение температур DS18B20
     * @param temps Структура для записи результатов
     */
    void readTemperatures(Temperatures& temps);
    
    /**
     * Чтение давления (куб + атмосферное)
     * @param pressure Структура для записи результатов
     */
    void readPressure(Pressure& pressure);
    
    /**
     * Расчёт показаний ареометра
     * @param hydro Структура для записи результатов
     * @param temperature Температура для коррекции
     */
    void readHydrometer(Hydrometer& hydro, float temperature);
    
    /**
     * Чтение электрических параметров
     * @param power Структура для записи результатов
     */
    void readPower(Power& power);
    
    /**
     * Чтение потока воды
     * @param flow Структура для записи результатов
     */
    void readWaterFlow(WaterFlow& flow);
    
    /**
     * Применение калибровочных смещений
     * @param cal Калибровка термометров
     */
    void applyCalibration(const TempCalibration& cal);
    
    /**
     * Получение адресов DS18B20
     * @param addresses Массив для записи адресов
     * @return Количество найденных датчиков
     */
    uint8_t scanDS18B20(uint8_t addresses[][8]);
    
    /**
     * Проверка валидности датчика температуры
     * @param index Индекс датчика (0-6)
     * @return true если датчик отвечает
     */
    bool isTempSensorValid(uint8_t index);

    /**
     * Обновление информации о здоровье датчиков
     * @param health Структура для записи результатов
     */
    void updateHealth(SystemHealth& health);
}

#endif // SENSORS_H
