/**
 * @file heater.h
 * @brief Интерфейс управления нагревателем
 */

#ifndef HEATER_H
#define HEATER_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Инициализация нагревателя
 * 
 * @return true если инициализация прошла успешно
 */
bool initHeater();

/**
 * @brief Установка мощности нагревателя в ваттах
 * 
 * @param powerWatts Требуемая мощность в ваттах
 */
void setHeaterPower(int powerWatts);

/**
 * @brief Установка мощности нагревателя в процентах
 * 
 * @param powerPercent Требуемая мощность в процентах (0-100)
 */
void setHeaterPowerPercent(int powerPercent);

/**
 * @brief Получение текущей мощности нагревателя в ваттах
 * 
 * @return Текущая мощность в ваттах
 */
int getHeaterPowerWatts();

/**
 * @brief Получение текущей мощности нагревателя в процентах
 * 
 * @return Текущая мощность в процентах (0-100)
 */
int getHeaterPowerPercent();

/**
 * @brief Выключение нагревателя
 */
void heaterOff();

/**
 * @brief Плавное изменение мощности нагревателя
 * 
 * @param targetPowerWatts Целевая мощность в ваттах
 * @param stepWatts Шаг изменения мощности в ваттах
 * @param delayMs Задержка между шагами в миллисекундах
 */
void rampHeaterPower(int targetPowerWatts, int stepWatts = 50, int delayMs = 100);

/**
 * @brief Обновление состояния нагревателя
 * 
 * Вызывается периодически для обработки плавных изменений мощности
 */
void updateHeater();

/**
 * @brief Получение максимальной мощности нагревателя
 * 
 * @return Максимальная мощность нагревателя в ваттах
 */
int getHeaterMaxPower();

/**
 * @brief Установка максимальной мощности нагревателя
 * 
 * @param maxPowerWatts Максимальная мощность в ваттах
 */
void setHeaterMaxPower(int maxPowerWatts);

/**
 * @brief Проверка состояния нагревателя
 * 
 * @return true если нагреватель включен
 */
bool isHeaterOn();

/**
 * @brief Проверка состояния нагревателя на полной мощности
 * 
 * @return true если нагреватель работает на максимальной мощности
 */
bool isHeaterAtFullPower();

/**
 * @brief ПИД-регулирование мощности для достижения целевой температуры
 * 
 * @param currentTemp Текущая температура
 * @param targetTemp Целевая температура
 * @param maxPower Максимальная мощность для регулирования (ватты)
 */
void pidControlHeaterPower(float currentTemp, float targetTemp, int maxPower);

/**
 * @brief Настройка параметров ПИД-регулятора
 * 
 * @param kp Пропорциональный коэффициент
 * @param ki Интегральный коэффициент
 * @param kd Дифференциальный коэффициент
 */
void setPidParameters(float kp, float ki, float kd);

/**
 * @brief Получение текущих параметров ПИД-регулятора
 * 
 * @param kp Указатель для сохранения пропорционального коэффициента
 * @param ki Указатель для сохранения интегрального коэффициента
 * @param kd Указатель для сохранения дифференциального коэффициента
 */
void getPidParameters(float* kp, float* ki, float* kd);

/**
 * @brief Сброс ПИД-регулятора (интегральной составляющей)
 */
void resetPidController();

#endif // HEATER_H
