/**
 * Smart-Column S3 - MQTT Client
 *
 * Интеграция с Home Assistant и другими MQTT системами
 */

#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>
#include "types.h"

namespace MQTT {
    /**
     * Инициализация MQTT клиента
     * @param server Адрес MQTT брокера
     * @param port Порт (по умолчанию 1883)
     * @param username Имя пользователя (опционально)
     * @param password Пароль (опционально)
     */
    void init(const char* server, uint16_t port = 1883,
              const char* username = nullptr, const char* password = nullptr);

    /**
     * Обработка MQTT (вызывать в loop)
     */
    void handle();

    /**
     * Публикация состояния системы
     * @param state Текущее состояние
     */
    void publishState(const SystemState& state);

    /**
     * Публикация здоровья системы
     * @param health Здоровье системы
     */
    void publishHealth(const SystemHealth& health);

    /**
     * Home Assistant Discovery
     * Автоматическое обнаружение устройства в HA
     */
    void publishDiscovery();

    /**
     * Проверка подключения к брокеру
     * @return true если подключён
     */
    bool isConnected();

    /**
     * Установка базового топика
     * @param topic Базовый топик (напр. "smartcolumn")
     */
    void setBaseTopic(const char* topic);

    /**
     * Отправка push-уведомления в Home Assistant
     * @param title Заголовок уведомления
     * @param message Текст сообщения
     * @param level Уровень важности (info, warning, error)
     */
    void publishNotification(const char* title, const char* message, const char* level = "info");
}

#endif // MQTT_H
