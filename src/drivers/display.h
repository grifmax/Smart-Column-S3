/**
 * Smart-Column S3 - Драйвер дисплея
 * 
 * TFT 3.5" ILI9488 (основной) + OLED 0.96" (резервный)
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace Display {
    /**
     * Инициализация дисплея
     */
    void init();
    
    /**
     * Обновление экрана
     * @param state Текущее состояние системы
     */
    void update(const SystemState& state);
    
    /**
     * Отображение главного экрана
     */
    void showMain(const SystemState& state);
    
    /**
     * Отображение экрана режима
     */
    void showMode(const SystemState& state);
    
    /**
     * Отображение экрана настроек
     */
    void showSettings();
    
    /**
     * Отображение аварии
     */
    void showAlarm(const Alarm& alarm);
    
    /**
     * Отображение сообщения
     * @param title Заголовок
     * @param message Текст
     * @param type 0=info, 1=warning, 2=error
     */
    void showMessage(const char* title, const char* message, uint8_t type);
    
    /**
     * Установка яркости подсветки
     * @param percent Яркость 0-100%
     */
    void setBrightness(uint8_t percent);
    
    /**
     * Переключение темы
     * @param dark true = тёмная тема
     */
    void setTheme(bool dark);
    
    /**
     * Получение скриншота (для Telegram)
     * @param buffer Буфер для JPEG
     * @param maxSize Максимальный размер
     * @return Размер данных
     */
    size_t getScreenshot(uint8_t* buffer, size_t maxSize);
}

#endif // DISPLAY_H
