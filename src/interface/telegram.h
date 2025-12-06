/**
 * Smart-Column S3 - Telegram бот
 * 
 * Уведомления и удалённое управление
 */

#ifndef TELEGRAM_H
#define TELEGRAM_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace TelegramBot {
    /**
     * Инициализация бота
     * @param token Токен бота
     * @param chatId ID чата для уведомлений
     */
    void init(const char* token, const char* chatId);
    
    /**
     * Обновление (проверка входящих сообщений)
     * Вызывать в loop
     */
    void update();
    
    /**
     * Отправка текстового сообщения
     * @param message Текст
     * @return true если успешно
     */
    bool sendMessage(const char* message);
    
    /**
     * Отправка форматированного сообщения
     * @param format Формат printf
     * @param ... Аргументы
     */
    bool sendFormatted(const char* format, ...);
    
    /**
     * Отправка уведомления о смене фазы
     * @param phase Новая фаза
     * @param stats Статистика
     */
    void notifyPhaseChange(RectPhase phase, const RunStats& stats);
    
    /**
     * Отправка уведомления об аварии
     * @param alarm Данные аварии
     */
    void notifyAlarm(const Alarm& alarm);
    
    /**
     * Отправка уведомления о завершении
     * @param stats Итоговая статистика
     */
    void notifyFinish(const RunStats& stats);

    /**
     * Отправка уведомления о проблемах со здоровьем системы
     * @param health Данные о здоровье системы
     */
    void notifyHealthAlert(const SystemHealth& health);

    /**
     * Отправка скриншота дисплея
     * @return true если успешно
     */
    bool sendScreenshot();
    
    /**
     * Включение/отключение бота
     * @param enabled Состояние
     */
    void setEnabled(bool enabled);
    
    /**
     * Проверка состояния
     * @return true если бот активен
     */
    bool isEnabled();
    
    /**
     * Установка настроек уведомлений
     * @param settings Настройки Telegram
     */
    void setSettings(const TelegramSettings& settings);
}

#endif // TELEGRAM_H
