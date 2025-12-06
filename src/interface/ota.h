/**
 * Smart-Column S3 - OTA Updates
 *
 * Беспроводное обновление прошивки через WiFi
 */

#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

namespace OTA {
    /**
     * Инициализация OTA
     */
    void init();

    /**
     * Обработка OTA (вызывать в loop)
     */
    void handle();

    /**
     * Установка пароля для OTA
     * @param password Пароль (опционально)
     */
    void setPassword(const char* password);

    /**
     * Проверка, идёт ли обновление
     * @return true если идёт загрузка прошивки
     */
    bool isUpdating();
}

#endif // OTA_H
