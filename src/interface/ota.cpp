/**
 * Smart-Column S3 - OTA Updates
 *
 * ArduinoOTA для беспроводного обновления прошивки
 */

#include "ota.h"
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include "config.h"

static bool otaInProgress = false;

namespace OTA {

void init() {
    LOG_I("OTA: Initializing...");

    // Установка hostname для идентификации в сети
    ArduinoOTA.setHostname(MDNS_HOSTNAME);

    // Порт по умолчанию 3232
    ArduinoOTA.setPort(3232);

    // Колбэк начала обновления
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "firmware";
        } else { // U_SPIFFS
            type = "filesystem";
            // ВАЖНО: остановить SPIFFS перед обновлением
            SPIFFS.end();
        }

        otaInProgress = true;
        LOG_I("OTA: Starting update (%s)...", type.c_str());
    });

    // Колбэк завершения
    ArduinoOTA.onEnd([]() {
        LOG_I("\nOTA: Update complete! Rebooting...");
        otaInProgress = false;
    });

    // Колбэк прогресса
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint8_t lastPercent = 0;
        uint8_t percent = (progress * 100) / total;

        // Выводить каждые 10%
        if (percent >= lastPercent + 10) {
            LOG_I("OTA: Progress: %u%%", percent);
            lastPercent = percent;
        }
    });

    // Колбэк ошибки
    ArduinoOTA.onError([](ota_error_t error) {
        otaInProgress = false;

        LOG_E("OTA: Error[%u]: ", error);
        switch (error) {
            case OTA_AUTH_ERROR:
                LOG_E("Auth Failed");
                break;
            case OTA_BEGIN_ERROR:
                LOG_E("Begin Failed");
                break;
            case OTA_CONNECT_ERROR:
                LOG_E("Connect Failed");
                break;
            case OTA_RECEIVE_ERROR:
                LOG_E("Receive Failed");
                break;
            case OTA_END_ERROR:
                LOG_E("End Failed");
                break;
            default:
                LOG_E("Unknown Error");
        }
    });

    // Запуск OTA
    ArduinoOTA.begin();

    LOG_I("OTA: Ready (hostname: %s, port: 3232)", MDNS_HOSTNAME);
}

void handle() {
    ArduinoOTA.handle();
}

void setPassword(const char* password) {
    if (password && strlen(password) > 0) {
        ArduinoOTA.setPassword(password);
        LOG_I("OTA: Password protection enabled");
    }
}

bool isUpdating() {
    return otaInProgress;
}

} // namespace OTA
