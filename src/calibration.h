/**
 * @file calibration.h
 * @brief Автоматическая калибровка датчиков и оборудования
 */

#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <Arduino.h>

// Структура данных калибровки датчика
struct SensorCalibration {
    float offset;           // Смещение
    float multiplier;       // Множитель
    bool isCalibrated;      // Прокалиброван ли
    unsigned long calibrationTime; // Время калибровки
};

// Структура данных калибровки насоса
struct PumpCalibration {
    float mlPerPulse;       // мл на один импульс
    int pulsesPerRevolution; // Импульсов на оборот
    bool isCalibrated;
};

/**
 * @brief Инициализация модуля калибровки
 */
void initCalibration();

/**
 * @brief Автоматическая калибровка датчика температуры
 * 
 * @param sensorIndex Индекс датчика
 * @param referenceTemp Эталонная температура
 * @return true если успешно
 */
bool calibrateTempSensor(int sensorIndex, float referenceTemp);

/**
 * @brief Калибровка всех датчиков температуры
 * 
 * @param referenceTemp Эталонная температура (например, температура кипения воды)
 * @return Количество прокалиброванных датчиков
 */
int calibrateAllTempSensors(float referenceTemp = 100.0);

/**
 * @brief Калибровка насоса по объему
 * 
 * @param measuredVolume Измеренный объем в мл
 * @param targetVolume Целевой объем в мл
 * @return true если успешно
 */
bool calibratePump(float measuredVolume, float targetVolume);

/**
 * @brief Автоматическая калибровка насоса
 * 
 * Запускает насос на определенное время и запрашивает у пользователя
 * измеренный объем через Serial
 * 
 * @return true если успешно
 */
bool autoCalibrateP ump();

/**
 * @brief Получение данных калибровки датчика
 * 
 * @param sensorIndex Индекс датчика
 * @return Структура с данными калибровки
 */
SensorCalibration getSensorCalibration(int sensorIndex);

/**
 * @brief Получение данных калибровки насоса
 * 
 * @return Структура с данными калибровки
 */
PumpCalibration getPumpCalibration();

/**
 * @brief Сброс калибровки датчика
 * 
 * @param sensorIndex Индекс датчика
 */
void resetSensorCalibration(int sensorIndex);

/**
 * @brief Сброс калибровки всех датчиков
 */
void resetAllCalibrations();

/**
 * @brief Сохранение данных калибровки
 * 
 * @return true если успешно
 */
bool saveCalibrationData();

/**
 * @brief Загрузка данных калибровки
 * 
 * @return true если успешно
 */
bool loadCalibrationData();

/**
 * @brief Применение калибровки к показанию датчика
 * 
 * @param sensorIndex Индекс датчика
 * @param rawValue Сырое значение
 * @return Откалиброванное значение
 */
float applySensorCalibration(int sensorIndex, float rawValue);

/**
 * @brief Вывод данных калибровки в Serial
 */
void printCalibrationData();

#endif // CALIBRATION_H
