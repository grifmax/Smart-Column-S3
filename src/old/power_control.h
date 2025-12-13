#ifndef POWER_CONTROL_H
#define POWER_CONTROL_H

#include <Arduino.h>
#include "config.h"

// Инициализация управления мощностью
void initPowerControl();

// Установка мощности в процентах (0-100%)
void setPowerPercent(int percent);

// Установка мощности в ваттах
void setPowerWatts(int watts);

// Получение текущей мощности в процентах
int getCurrentPowerPercent();

// Получение текущей мощности в ваттах
int getCurrentPowerWatts();

// Обновление управления мощностью
void updatePowerControl();

// Установка режима управления мощностью
void setPowerControlMode(PowerControlMode mode);

// Получение текущего режима управления мощностью
PowerControlMode getPowerControlMode();

// Настройка PI-регулятора
void configurePIController(float kp, float ki, float outputMin, float outputMax);

// Установка целевой температуры для PI-регулятора
void setPITargetTemperature(float targetTemp, int sensorIndex = TEMP_REFLUX);

// Получение текущей мощности от PZEM-004T (если подключен)
float getPzemPowerWatts();

// Получение текущего напряжения от PZEM-004T (если подключен)
float getPzemVoltage();

// Получение текущего тока от PZEM-004T (если подключен)
float getPzemCurrent();

// Получение текущей энергии от PZEM-004T (если подключен)
float getPzemEnergy();

// Сброс счетчика энергии PZEM-004T
void resetPzemEnergy();

#endif // POWER_CONTROL_H