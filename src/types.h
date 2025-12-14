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

// Типы насадки
enum class PackingType : uint8_t {
    SPN_3_5 = 0,
    SPN_4_0,
    RASCHIG,
    CUSTOM
};

// Структуры для температур
struct TemperatureData {
    float cube = 0.0f;
    float columnTop = 0.0f;
    float columnMiddle = 0.0f;
    float columnBottom = 0.0f;
    float deflegmator = 0.0f;
    float product = 0.0f;
    float tsa = 0.0f;
    float waterOut = 0.0f;
};

// Данные давления
struct PressureData {
    float pressure = 101.325f;      // кПа
    float temperature = 25.0f;       // °C
    bool ok = false;
};

// Данные гидрометра
struct HydrometerData {
    float density = 1.0f;
    float abv = 0.0f;
    bool ok = false;
    uint8_t abvPoints[10] = {0};
    uint8_t pressurePoints[10] = {0};
};

// Данные мощности
struct PowerData {
    float voltage = 0.0f;
    float current = 0.0f;
    float power = 0.0f;
    float energy = 0.0f;
    float frequency = 0.0f;
    float powerFactor = 0.0f;
    bool ok = false;
};

// Точка данных энергопотребления
struct EnergyDataPoint {
    uint32_t timestamp = 0;
    float power = 0.0f;
    float energy = 0.0f;
    float voltage = 0.0f;
    float current = 0.0f;
};

// История энергопотребления
struct EnergyHistory {
    static const uint8_t MAX_POINTS = 120;  // 2 часа при записи каждую минуту
    EnergyDataPoint points[MAX_POINTS];
    uint8_t writeIndex = 0;
    uint8_t count = 0;
};

// Событие для логирования
struct LogEvent {
    uint32_t timestamp;
    uint8_t level;          // 0=INFO, 1=WARN, 2=ERROR
    char message[128];
};

// Профиль затирания
struct MashProfile {
    char name[32];
    uint8_t stepCount;
    struct {
        float temperature;
        uint16_t duration;
    } steps[10];
};

// Здоровье системы (для совместимости)
typedef struct {
    bool tempSensorsOk;
    uint8_t tempSensorsTotal;
    bool bmp280Ok;
    bool ads1115Ok;
    bool pzemOk;
    bool wifiConnected;
    int8_t wifiRSSI;
    uint32_t uptime;
    uint32_t freeHeap;
    float cpuTemp;
    uint16_t pzemSpikeCount;
    uint16_t tempReadErrors;
    uint8_t overallHealth;
    uint32_t lastUpdate;
} SystemHealth;

// Состояние системы (полная версия)
struct SystemState {
    Mode mode = Mode::IDLE;
    RectPhase rectPhase = RectPhase::IDLE;
    bool paused = false;
    bool safetyOk = true;
    uint32_t uptime = 0;

    TemperatureData temps;
    PressureData pressure;
    HydrometerData hydrometer;
    PowerData power;

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

// Настройки (полная версия)
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

    struct {
        bool enabled = false;
        char server[64] = "";
        uint16_t port = 1883;
        char username[32] = "";
        char password[64] = "";
        char baseTopic[32] = "smart-column";
        uint32_t publishInterval = 10000;
    } mqtt;

    struct {
        bool enabled = false;
        char token[64] = "";
        char chatId[32] = "";
    } telegram;

    struct {
        uint16_t columnHeightMm = 1000;
        PackingType packingType = PackingType::SPN_3_5;
        float packingCoeff = 3.5f;
        uint16_t heaterPowerW = 2000;
        float cubeVolumeL = 20.0f;
    } equipment;

    struct {
        bool enabled = false;
        uint16_t angles[5] = {0, 45, 90, 135, 180};
        bool positionsEnabled[5] = {true, false, true, false, true};
    } fractionator;

    struct {
        float headsPercent = 3.0f;
        float headsSpeedMlHKw = 300.0f;
        float bodySpeedMlHKw = 600.0f;
        uint16_t stabilizationMin = 30;
        uint16_t purgeMin = 5;
    } rectParams;

    uint8_t language = 0;           // 0=RU, 1=EN
    uint8_t theme = 0;              // 0=Light, 1=Dark
    bool soundEnabled = true;
};

// Глобальные переменные
extern SystemState g_state;
extern Settings g_settings;

#endif // TYPES_H
