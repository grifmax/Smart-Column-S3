/**
 * Smart-Column S3 - Watt Control
 * 
 * Автоматическое управление мощностью по давлению в колонне
 */

#ifndef WATT_CONTROL_H
#define WATT_CONTROL_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace WattControl {
    /**
     * Инициализация контроллера
     * @param settings Настройки оборудования
     */
    void init(const EquipmentSettings& settings);
    
    /**
     * Расчёт порога захлёба
     * @param columnHeightMm Высота колонны в мм
     * @param packingCoeff Коэффициент насадки (мм рт.ст./м)
     * @return Давление захлёба в мм рт.ст.
     */
    float calculateFloodPressure(uint16_t columnHeightMm, float packingCoeff);
    
    /**
     * Установка калиброванного порога захлёба
     * @param pressure Давление захлёба (из калибровки)
     */
    void setFloodPressure(float pressure);
    
    /**
     * Обновление (вызывать в loop)
     * @param state Состояние системы
     * @param settings Настройки
     * @return Рекомендуемая мощность 0-100%
     */
    uint8_t update(const SystemState& state, const Settings& settings);
    
    /**
     * Получение рабочей мощности (без override)
     * @param pressure Текущее давление
     * @return Мощность 0-100%
     */
    uint8_t getRecommendedPower(float pressure);
    
    /**
     * Ручной override мощности
     * @param percent Мощность 0-100%, или -1 для отключения override
     */
    void setOverride(int8_t percent);
    
    /**
     * Проверка режима override
     * @return true если override активен
     */
    bool isOverrideActive();
    
    /**
     * Обработка захлёба
     * Снижает мощность и запускает таймер восстановления
     */
    void handleFlood();
    
    /**
     * Получение статуса давления
     * @param pressure Текущее давление
     * @return 0=норма, 1=предупреждение, 2=критическое
     */
    uint8_t getPressureStatus(float pressure);
    
    /**
     * Получение текущих порогов
     * @param work Рабочий порог (выход)
     * @param warn Порог предупреждения (выход)
     * @param crit Критический порог (выход)
     */
    void getThresholds(float& work, float& warn, float& crit);
}

namespace SmartDecrement {
    /**
     * Инициализация
     * @param baseTemp Базовая температура царги (T_base)
     */
    void init(float baseTemp);
    
    /**
     * Обновление (вызывать в loop во время отбора тела)
     * @param state Состояние системы
     * @param settings Настройки
     * @return true если нужно перейти в хвосты
     */
    bool update(SystemState& state, const Settings& settings);
    
    /**
     * Проверка условия срабатывания
     * @param currentTemp Текущая температура царги
     * @param baseTemp Базовая температура
     * @return true если нужно снизить скорость
     */
    bool shouldDecrement(float currentTemp, float baseTemp);
    
    /**
     * Проверка условия возобновления
     * @param currentTemp Текущая температура царги
     * @param baseTemp Базовая температура
     * @return true если можно продолжить отбор
     */
    bool canResume(float currentTemp, float baseTemp);
    
    /**
     * Получение текущего состояния
     * @return Структура состояния
     */
    const DecrementState& getState();
    
    /**
     * Сброс состояния
     */
    void reset();
}

#endif // WATT_CONTROL_H
