/**
 * @file temp_sensors.h
 * @brief Управление температурными датчиками
 * 
 * Этот модуль отвечает за работу с датчиками температуры DS18B20,
 * их обнаружение, калибровку и считывание показаний.
 */

#ifndef TEMP_SENSORS_H
#define TEMP_SENSORS_H

#include <Arduino.h>

// Максимальное количество датчиков температуры
#define MAX_TEMP_SENSORS 5

// Индексы датчиков температуры
#define TEMP_CUBE       0   // Куб
#define TEMP_COLUMN     1   // Царга
#define TEMP_REFLUX     2   // Узел отбора
#define TEMP_TSA        3   // ТСА (теплообменник)
#define TEMP_WATER_OUT  4   // Выход воды

/**
 * @brief Инициализация датчиков температуры
 */
void initTempSensors();

/**
 * @brief Обновление показаний датчиков
 */
void updateTemperatures();

/**
 * @brief Поиск и установка адресов датчиков
 * 
 * @return true если найден хотя бы один датчик
 */
bool scanForTempSensors();

/**
 * @brief Получение температуры конкретного датчика
 * 
 * @param sensorIndex Индекс датчика
 * @return Температура в градусах Цельсия
 */
float getTemperature(int sensorIndex);

/**
 * @brief Проверка подключения датчика
 * 
 * @param sensorIndex Индекс датчика
 * @return true если датчик подключен
 */
bool isSensorConnected(int sensorIndex);

/**
 * @brief Калибровка датчика температуры
 * 
 * @param sensorIndex Индекс датчика
 * @param offset Смещение для калибровки
 */
void calibrateTempSensor(int sensorIndex, float offset);

/**
 * @brief Получение количества подключенных датчиков
 * 
 * @return Количество датчиков
 */
int getConnectedSensorsCount();

/**
 * @brief Проверка температуры на достижение заданного значения с учетом гистерезиса
 * 
 * @param sensorIndex Индекс датчика
 * @param targetTemp Целевая температура
 * @param hysteresis Гистерезис
 * @return true если температура достигла целевого значения
 */
bool isTemperatureReached(int sensorIndex, float targetTemp, float hysteresis);

/**
 * @brief Проверка на превышение максимальной температуры
 * 
 * @param sensorIndex Индекс датчика
 * @param maxTemp Максимальная температура
 * @return true если температура превысила максимум
 */
bool isTemperatureExceeded(int sensorIndex, float maxTemp);

/**
 * @brief Получение скорости изменения температуры (градусов в минуту)
 * 
 * @param sensorIndex Индекс датчика
 * @param periodSeconds Период измерения в секундах
 * @return Скорость изменения температуры
 */
float getTemperatureRateOfChange(int sensorIndex, float periodSeconds);

/**
 * @brief Получение имени датчика
 * 
 * @param sensorIndex Индекс датчика
 * @return Имя датчика
 */
String getTempSensorName(int sensorIndex);

#endif // TEMP_SENSORS_H