/**
 * Smart-Column S3 - Types
 * Определения типов данных
 */

#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>

// Режимы работы
enum class Mode : uint8_t {
    IDLE = 0,
    RECTIFICATION,
    DISTILLATION,
    MANUAL_RECT
};

// Фазы ректификации
enum class RectPhase : uint8_t {
    IDLE = 0,
    HEATING,
    STABILIZATION,
    HEADS,
    BODY,
    TAILS,
    COMPLETED
};

// Состояние системы (упрощённая версия)
struct SystemState {
    Mode mode = Mode::IDLE;
    RectPhase rectPhase = RectPhase::IDLE;
    bool paused = false;

    struct {
        bool tempSensorsOk = false;
        uint8_t tempSensorsTotal = 0;
        bool bmp280Ok = false;
        bool ads1115Ok = false;
        bool pzemOk = false;
        bool wifiConnected = false;
        int8_t wifiRSSI = 0;
        uint32_t uptime = 0;
        uint32_t freeHeap = 0;
        float cpuTemp = 0;
        uint16_t pzemSpikeCount = 0;
        uint16_t tempReadErrors = 0;
        uint8_t overallHealth = 100;
        uint32_t lastUpdate = 0;
    } health;
};

// Настройки (упрощённая версия)
struct Settings {
    struct {
        char ssid[64] = "";
        char password[64] = "";
        bool apMode = true;
    } wifi;

    struct {
        float mlPerRevolution = 0.1f;
        uint16_t stepsPerRevolution = 200;
        uint8_t microsteps = 32;
    } pumpCal;

    struct {
        float offsets[8] = {0};
        uint8_t addresses[8][8] = {0};
    } tempCal;
};

// Глобальные переменные
extern SystemState g_state;
extern Settings g_settings;

#endif // TYPES_H
