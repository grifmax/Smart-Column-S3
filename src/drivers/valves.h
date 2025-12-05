/**
 * Smart-Column S3 - Драйвер клапанов и фракционника
 * 
 * Электроклапаны 12V NC через MOSFET
 * Сервопривод MG996R для фракционника
 */

#ifndef VALVES_H
#define VALVES_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace Valves {
    /**
     * Инициализация клапанов
     */
    void init();
    
    /**
     * Инициализация фракционника (вызывать если включён)
     */
    void initFractionator();
    
    // =========================================================================
    // ОСНОВНЫЕ КЛАПАНЫ
    // =========================================================================
    
    /**
     * Клапан охлаждения (вода)
     */
    void setWater(bool open);
    bool getWater();
    
    /**
     * Клапан отбора голов
     */
    void setHeads(bool open);
    bool getHeads();
    
    /**
     * Клапан УНО (непрерывный отбор)
     */
    void setUno(bool open);
    bool getUno();
    
    /**
     * Клапан старт-стоп (опциональный, ШИМ)
     * @param duty ШИМ 0-255 (0=закрыт, 255=полностью открыт)
     */
    void setStartStop(uint8_t duty);
    uint8_t getStartStop();
    
    /**
     * Закрыть все клапаны
     */
    void closeAll();
    
    // =========================================================================
    // ФРАКЦИОННИК
    // =========================================================================
    
    /**
     * Установка позиции фракционника
     * @param fraction Индекс фракции (0-4)
     * @param smooth Плавный поворот
     */
    void setFraction(Fraction fraction, bool smooth = true);
    
    /**
     * Установка угла напрямую
     * @param angle Угол 0-180°
     */
    void setFractionAngle(uint8_t angle);
    
    /**
     * Текущая фракция
     */
    Fraction getCurrentFraction();
    
    /**
     * Следующая активная фракция
     * @param settings Настройки для проверки enabled
     * @return Следующая активная фракция или текущая если нет
     */
    Fraction getNextEnabledFraction(const FractionatorSettings& settings);
    
    /**
     * Переключение на следующую фракцию
     * @param settings Настройки
     */
    void nextFraction(const FractionatorSettings& settings);
    
    // =========================================================================
    // УНО ЦИКЛ
    // =========================================================================
    
    /**
     * Обновление УНО цикла (вызывать в loop)
     * @param params Параметры цикла
     */
    void updateUno(UnoParams& params);
}

#endif // VALVES_H
