/**
 * Smart-Column S3 - Физические кнопки
 * 
 * 4 кнопки с поддержкой debounce и long press
 */

#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

// Коды событий кнопок
enum class ButtonEvent : uint8_t {
    NONE = 0,
    BTN1_SHORT,     // Старт/Пауза
    BTN1_LONG,      // Стоп
    BTN2_SHORT,     // Следующая фракция
    BTN2_LONG,      // Меню режимов
    BTN3_SHORT,     // Скорость +
    BTN3_LONG,      // Мощность +
    BTN4_SHORT,     // Скорость -
    BTN4_LONG       // Мощность -
};

// Callback тип
typedef void (*ButtonCallback)(ButtonEvent event);

namespace Buttons {
    /**
     * Инициализация кнопок
     */
    void init();
    
    /**
     * Обновление состояния (вызывать в loop)
     */
    void update();
    
    /**
     * Установка callback для событий
     * @param callback Функция обработчик
     */
    void setCallback(ButtonCallback callback);
    
    /**
     * Получение последнего события (и сброс)
     * @return Событие кнопки
     */
    ButtonEvent getEvent();
    
    /**
     * Проверка нажатия кнопки
     * @param button Номер кнопки 1-4
     * @return true если нажата
     */
    bool isPressed(uint8_t button);
    
    /**
     * Блокировка кнопок (во время критических операций)
     * @param locked Состояние блокировки
     */
    void setLocked(bool locked);
}

#endif // BUTTONS_H
