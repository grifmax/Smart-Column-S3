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
    struct RuntimeStats {
        uint32_t framesRendered = 0;
        uint32_t slowFrames = 0;
        uint32_t watchdogRecoveries = 0;
        uint32_t hardWatchdogRecoveries = 0;
        uint32_t hardWatchdogFailures = 0;
        uint16_t lastFrameMs = 0;
        uint16_t maxFrameMs = 0;
        uint32_t lastFrameAtMs = 0;
        uint16_t lastUpdateGapMs = 0;
        uint16_t maxUpdateGapMs = 0;
        uint32_t updateGapOverruns = 0;
    };

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
     * Получение скриншота
     * @param buffer Буфер для JPEG
     * @param maxSize Максимальный размер
     * @return Размер данных
     */
    size_t getScreenshot(uint8_t* buffer, size_t maxSize);

    /**
     * Нужно ли запускать калибровку тача
     */
    bool needsTouchCalibration();

    /**
     * Запустить мастер калибровки тача
     */
    void startTouchCalibration();

    /**
     * Идёт ли калибровка тача
     */
    bool isTouchCalibrating();

    /**
     * Внутренняя телеметрия рендера TFT (для диагностики стабильности)
     */
    RuntimeStats getRuntimeStats();
}

#endif // DISPLAY_H
