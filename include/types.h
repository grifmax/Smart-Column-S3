/**
 * Smart-Column S3 - Типы данных
 * 
 * Структуры для состояний, настроек и сенсоров
 */

#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include "config.h"

// =============================================================================
// ПЕРЕЧИСЛЕНИЯ
// =============================================================================

/**
 * Режимы работы системы
 */
enum class Mode : uint8_t {
    IDLE = 0,           // Ожидание
    RECTIFICATION,      // Авто-ректификация
    MANUAL_RECT,        // Ручная ректификация
    DISTILLATION,       // Дистилляция
    MASHING,            // Затирка солода
    HOLD                // Температурные ступени
};

/**
 * Фазы авто-ректификации (FSM)
 */
enum class RectPhase : uint8_t {
    IDLE = 0,           // Ожидание
    HEATING,            // Разгон
    STABILIZATION,      // Стабилизация "на себя"
    HEADS,              // Отбор голов
    PURGE,              // Продувка
    BODY,               // Отбор тела
    TAILS,              // Отбор хвостов
    FINISH,             // Завершение
    ERROR               // Ошибка
};

/**
 * Фазы затирки солода
 */
enum class MashPhase : uint8_t {
    IDLE = 0,
    ACID_REST,          // Кислотная пауза
    PROTEIN_REST,       // Белковая пауза
    BETA_AMYLASE,       // Мальтозная пауза
    ALPHA_AMYLASE,      // Осахаривание
    MASH_OUT,           // Мэш-аут
    FINISH
};

/**
 * Тип насадки колонны
 */
enum class PackingType : uint8_t {
    SPN_3_5 = 0,        // СПН 3.5 мм
    SPN_4_0,            // СПН 4.0 мм
    RPN,                // РПН (регулярная)
    CUSTOM              // Пользовательская
};

/**
 * Уровень аварии
 */
enum class AlarmLevel : uint8_t {
    NONE = 0,
    INFO,               // Информация
    WARNING,            // Предупреждение
    CRITICAL            // Критическая
};

/**
 * Тип аварии
 */
enum class AlarmType : uint8_t {
    NONE = 0,
    VAPOR_BREAKTHROUGH,     // Прорыв паров
    WATER_OVERHEAT,         // Перегрев воды
    NO_WATER_FLOW,          // Нет потока воды
    COLUMN_FLOOD,           // Захлёб колонны
    CONTAINER_OVERFLOW,     // Переполнение ёмкости
    SENSOR_FAILURE,         // Сбой датчика
    LOW_VOLTAGE,            // Низкое напряжение
    HIGH_VOLTAGE,           // Высокое напряжение
    HEATER_FAILURE          // Неисправность ТЭНа
};

/**
 * Индекс фракции
 */
enum class Fraction : uint8_t {
    HEADS = 0,          // Головы
    SUBHEADS,           // Подголовники
    BODY,               // Тело
    PRETAILS,           // Предхвостье
    TAILS               // Хвосты
};

// =============================================================================
// СТРУКТУРЫ ДАННЫХ
// =============================================================================

/**
 * Показания температур
 */
struct TemperatureData {
    float cube;             // Куб
    float columnBottom;     // Царга низ (T_base)
    float columnTop;        // Царга верх
    float reflux;           // Дефлегматор
    float tsa;              // Выход ТСА
    float waterIn;          // Вода вход
    float waterOut;         // Вода выход
    bool valid[TEMP_COUNT]; // Валидность каждого датчика
    uint32_t lastUpdate;    // Время последнего обновления
};
typedef TemperatureData Temperatures;

/**
 * Показания давления
 */
struct PressureData {
    float cube;             // Давление в кубе (мм рт.ст.)
    float atmosphere;       // Атмосферное давление (гПа)
    float floodThreshold;   // Порог захлёба (калибр.)
    float workThreshold;    // Рабочий порог
    float warnThreshold;    // Порог предупреждения
    float critThreshold;    // Критический порог
    uint32_t lastUpdate;
};
typedef PressureData Pressure;

/**
 * Показания электрических параметров (PZEM-004T)
 */
struct PowerData {
    float voltage;          // Напряжение (V RMS)
    float current;          // Ток (A RMS)
    float power;            // Активная мощность (W)
    float energy;           // Потреблённая энергия (кВт·ч)
    float frequency;        // Частота сети (Гц)
    float powerFactor;      // Коэффициент мощности (0.0-1.0)
    float powerTarget;      // Заданная мощность (%)
    uint32_t lastUpdate;
};
typedef PowerData Power;

/**
 * Электронный ареометр
 */
struct HydrometerData {
    float abv;              // Крепость (%)
    float density;          // Плотность (г/мл)
    float temperature;      // Температура измерения
    bool valid;
    uint32_t lastUpdate;
};
typedef HydrometerData Hydrometer;

/**
 * Состояние насоса
 */
struct PumpState {
    bool running;           // Работает
    float speedMlPerHour;   // Скорость (мл/час)
    float targetSpeed;      // Заданная скорость
    uint32_t totalSteps;    // Всего шагов
    float totalVolumeMl;    // Всего прокачано (мл)
};

/**
 * Состояние клапанов
 */
struct ValvesState {
    bool water;             // Охлаждение
    bool heads;             // Отбор голов
    bool uno;               // УНО
    bool startStop;         // Старт-стоп (опция)
    uint8_t startStopPwm;   // ШИМ старт-стоп (0-255)
};

/**
 * Состояние фракционника
 */
struct FractionatorState {
    bool enabled;                           // Включён
    Fraction current;                       // Текущая фракция
    uint8_t angle;                          // Текущий угол
    uint8_t angles[FRACTION_COUNT];         // Углы позиций
    bool positionsEnabled[FRACTION_COUNT];  // Активные позиции
    float volumes[FRACTION_COUNT];          // Объёмы фракций (мл)
};

/**
 * Состояние датчика потока воды
 */
struct WaterFlow {
    float litersPerMin;     // Расход (л/мин)
    float totalLiters;      // Всего прошло (л)
    bool flowing;           // Есть поток
    uint32_t lastPulse;     // Время последнего импульса
};

/**
 * Текущая авария
 */
struct Alarm {
    AlarmType type;
    AlarmLevel level;
    char message[64];
    uint32_t timestamp;
    bool acknowledged;
};

/**
 * Статистика погона
 */
struct RunStats {
    uint32_t startTime;             // Время старта (epoch)
    uint32_t duration;              // Длительность (сек)
    float headsVolume;              // Объём голов (мл)
    float bodyVolume;               // Объём тела (мл)
    float tailsVolume;              // Объём хвостов (мл)
    float totalVolume;              // Общий объём (мл)
    float avgBodyAbv;               // Средняя крепость тела (%)
    float energyUsedKwh;            // Потрачено энергии (кВт·ч)
    uint8_t decrementCount;         // Число снижений скорости
};

/**
 * Параметры УНО-цикла
 */
struct UnoParams {
    bool enabled;
    bool motorEnabled;              // Мотор включен (для совместимости)
    uint16_t onSeconds;             // Время открытия
    uint16_t offSeconds;            // Время закрытия
    uint32_t lastToggle;            // Время последнего переключения
    bool state;                     // Текущее состояние
};

/**
 * Параметры Smart Decrement
 */
struct DecrementState {
    bool active;                    // Активен (ожидание)
    float baseTemp;                 // T_base (зафиксированная)
    uint8_t decrementCount;         // Счётчик снижений
    uint32_t waitStart;             // Начало ожидания
};

/**
 * Здоровье системы (System Health)
 */
struct SystemHealth {
    // Датчики температуры (DS18B20)
    uint8_t tempSensorsOk;          // Количество рабочих датчиков температуры
    uint8_t tempSensorsTotal;       // Всего обнаружено датчиков

    // Датчики давления и другие
    bool bmp280Ok;                  // Барометр работает
    bool ads1115Ok;                 // АЦП работает
    bool pzemOk;                    // PZEM-004T работает

    // Связь
    bool wifiConnected;             // WiFi подключен
    int8_t wifiRSSI;                // Уровень сигнала WiFi (dBm)

    // Системная информация
    uint32_t uptime;                // Время работы (секунды)
    uint32_t freeHeap;              // Свободная память (байты)
    uint8_t cpuTemp;                // Температура CPU (°C)

    // Счётчики ошибок
    uint16_t pzemSpikeCount;        // Количество отброшенных выбросов PZEM
    uint16_t tempReadErrors;        // Ошибки чтения температур

    // Общая оценка здоровья (0-100%)
    uint8_t overallHealth;
    uint32_t lastUpdate;
};

/**
 * Полное состояние системы
 */
struct SystemState {
    Mode mode;
    RectPhase rectPhase;
    MashPhase mashPhase;

    TemperatureData temps;
    PressureData pressure;
    PowerData power;
    HydrometerData hydrometer;
    
    PumpState pump;
    ValvesState valves;
    FractionatorState fractionator;
    WaterFlow waterFlow;
    
    UnoParams uno;
    DecrementState decrement;
    
    Alarm currentAlarm;
    RunStats stats;
    SystemHealth health;

    bool paused;
    bool safetyOk;
    uint32_t uptime;
};

// =============================================================================
// НАСТРОЙКИ (NVS)
// =============================================================================

/**
 * Настройки WiFi
 */
struct WiFiSettings {
    char ssid[33];
    char password[65];
    bool apMode;
};

/**
 * Настройки Telegram
 */
struct TelegramSettings {
    char token[64];
    char chatId[16];
    bool enabled;
    bool notifyPhaseChange;
    bool notifyAlarm;
    bool notifyFinish;
};

/**
 * Настройки MQTT
 */
struct MqttSettings {
    char server[64];                    // Адрес брокера
    uint16_t port;                      // Порт (1883)
    char username[32];                  // Имя пользователя
    char password[64];                  // Пароль
    char baseTopic[32];                 // Базовый топик
    bool enabled;                       // Включён
    bool discovery;                     // Home Assistant Discovery
    uint16_t publishInterval;           // Интервал публикации (мс)
};

/**
 * Настройки безопасности
 */
struct SecuritySettings {
    char webUsername[32];               // Имя для веб-доступа
    char webPassword[64];               // Пароль для веб-доступа
    bool authEnabled;                   // Включить аутентификацию
    bool rateLimitEnabled;              // Включить rate limiting
};

/**
 * Настройки оборудования
 */
struct EquipmentSettings {
    uint16_t columnHeightMm;
    PackingType packingType;
    float packingCoeff;
    uint16_t heaterPowerW;
    uint16_t cubeVolumeL;
};

/**
 * Калибровка термометров
 */
struct TempCalibration {
    float offsets[TEMP_COUNT];
    uint8_t addresses[TEMP_COUNT][8];   // ROM адреса DS18B20
};

/**
 * Калибровка ареометра
 */
struct HydrometerCalibration {
    uint8_t pointCount;
    float abvPoints[5];                 // Известные значения ABV
    float pressurePoints[5];            // Соотв. давления
};

/**
 * Калибровка насоса
 */
struct PumpCalibration {
    float mlPerRevolution;
    uint16_t stepsPerRevolution;
    uint8_t microsteps;
};

/**
 * Настройки фракционника
 */
struct FractionatorSettings {
    bool enabled;
    uint8_t angles[FRACTION_COUNT];
    bool positionsEnabled[FRACTION_COUNT];
};

/**
 * Параметры авто-ректификации
 */
struct RectificationParams {
    uint8_t headsPercent;               // % голов от АС
    uint16_t headsSpeedMlHKw;           // Скорость голов
    uint16_t bodySpeedMlHKw;            // Скорость тела
    uint8_t stabilizationMin;           // Время стабилизации
    uint8_t purgeMin;                   // Время продувки
};

/**
 * Ступень температуры (Hold/Mash)
 */
struct TempStep {
    float temperature;
    uint16_t durationMin;
    char name[16];
};

/**
 * Профиль затирки
 */
struct MashProfile {
    char name[24];
    uint8_t stepCount;
    TempStep steps[7];
};

/**
 * Все настройки
 */
struct Settings {
    WiFiSettings wifi;
    TelegramSettings telegram;
    MqttSettings mqtt;
    SecuritySettings security;
    EquipmentSettings equipment;
    TempCalibration tempCal;
    HydrometerCalibration hydroCal;
    PumpCalibration pumpCal;
    FractionatorSettings fractionator;
    RectificationParams rectParams;
    MashProfile mashProfiles[3];

    uint8_t language;                   // 0=RU, 1=EN
    uint8_t theme;                      // 0=light, 1=dark
    bool soundEnabled;
};

// =============================================================================
// API / WEBSOCKET СООБЩЕНИЯ
// =============================================================================

/**
 * Команда от клиента
 */
struct Command {
    char action[16];
    char param[32];
    int32_t value;
};

/**
 * Событие лога
 */
struct LogEvent {
    uint32_t timestamp;
    uint8_t type;                       // 0=info, 1=warning, 2=error, 3=phase
    char message[48];
};

/**
 * Точка данных для графика энергопотребления
 */
struct EnergyDataPoint {
    uint32_t timestamp;                 // Время (секунды с запуска)
    float power;                        // Мощность (Вт)
    float energy;                       // Накопленная энергия (кВт·ч)
    float voltage;                      // Напряжение (В)
    float current;                      // Ток (А)
};

/**
 * История энергопотребления
 */
struct EnergyHistory {
    static const uint16_t MAX_POINTS = 288;  // 24 часа по 5 минут = 288 точек
    EnergyDataPoint points[MAX_POINTS];
    uint16_t count;                     // Текущее количество точек
    uint16_t writeIndex;                // Индекс для записи (циклический буфер)
    uint32_t lastUpdate;                // Время последнего обновления
};

#endif // TYPES_H
