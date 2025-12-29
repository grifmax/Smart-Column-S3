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
  MANUAL_RECT,
  MASHING,    // Затирка солода
  HOLD        // Температурные ступени (Hold режим)
};

// Фазы ректификации
enum class RectPhase : uint8_t {
  IDLE = 0,
  HEATING,
  STABILIZATION,
  HEADS,
  POST_HEADS_STABILIZATION,
  BODY,
  TAILS,
  PURGE,
  FINISH,
  COMPLETED
};

// Типы насадки
enum class PackingType : uint8_t { SPN_3_5 = 0, SPN_4_0, RASCHIG, CUSTOM };

// Уровни тревоги (forward declaration для CurrentAlarm)
enum class AlarmLevel : uint8_t { NONE = 0, INFO, WARNING, ERROR, CRITICAL };

// Тип тревоги (forward declaration для CurrentAlarm)
enum class AlarmType : uint8_t {
  NONE = 0,
  VAPOR_BREAKTHROUGH, // Прорыв паров
  WATER_OVERHEAT,     // Перегрев воды
  COLUMN_FLOOD,       // Захлёб колонны
  SENSOR_FAILURE,     // Отказ датчика
  POWER_FAILURE,      // Отказ питания
  OVERHEAT,           // Перегрев
  LOW_WATER,          // Нет воды
  EMERGENCY_STOP      // Аварийная остановка
};

// Структуры для температур
struct TemperatureData {
  float cube = 0.0f;
  float columnTop = 0.0f;
  float columnMiddle = 0.0f;
  float columnBottom = 0.0f;
  float deflegmator = 0.0f;
  float reflux = 0.0f; // Алиас для deflegmator
  float product = 0.0f;
  float tsa = 0.0f;
  float waterIn = 0.0f;
  float waterOut = 0.0f;
  bool valid[8] = {false}; // Флаги валидности датчиков
  uint32_t lastUpdate = 0; // Время последнего обновления
};

// Данные давления
struct PressureData {
  float pressure = 101.325f;   // кПа
  float atmosphere = 1013.25f; // гПа атмосферное
  float cube = 0.0f;           // мм рт.ст. в кубе
  float temperature = 25.0f;   // °C
  float critThreshold = 50.0f; // Критический порог (мм рт.ст.)
  uint32_t lastUpdate = 0;     // Время последнего обновления
  bool ok = false;
};

// Данные гидрометра
struct HydrometerData {
  float density = 1.0f;
  float abv = 0.0f;
  float temperature = 0.0f; // Температура замера
  bool valid = false;       // Данные валидны
  bool ok = false;          // Сенсор работает
  uint32_t lastUpdate = 0;  // Время последнего обновления
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
  uint32_t lastUpdate = 0; // Время последнего обновления
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
  static const uint8_t MAX_POINTS = 120; // 2 часа при записи каждую минуту
  EnergyDataPoint points[MAX_POINTS];
  uint8_t writeIndex = 0;
  uint8_t count = 0;
  uint32_t lastUpdate = 0;
};

// Событие для логирования
struct LogEvent {
  uint32_t timestamp;
  uint8_t level; // 0=INFO, 1=WARN, 2=ERROR
  char message[128];
};

// Фазы затирки солода
enum class MashPhase : uint8_t {
  IDLE = 0,
  ACID_REST,       // Кислотная пауза (35-40°C)
  PROTEIN_REST,    // Белковая пауза (45-52°C)
  BETA_AMYLASE,    // Мальтозная пауза (62-65°C)
  ALPHA_AMYLASE,   // Осахаривание (68-72°C)
  MASH_OUT,        // Мэш-аут (75-78°C)
  FINISH
};

// Профиль затирания
struct MashProfile {
  char name[32];
  uint8_t stepCount;
  struct {
    float temperature;
    uint16_t duration; // минуты
    char name[32];     // название паузы
  } steps[10];
};

// Здоровье системы
struct SystemHealth {
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
};

// Состояние насоса
struct PumpState {
  bool running = false;
  float speedMlPerHour = 0.0f;
  float totalVolumeMl = 0.0f;
};

// Текущая тревога
struct CurrentAlarm {
  AlarmType type = AlarmType::NONE;
  AlarmLevel level = AlarmLevel::NONE;
  char message[128] = "";
  uint32_t timestamp = 0;
  bool acknowledged = false;
};

// Статистика процесса
struct ProcessStats {
  uint32_t decrementCount = 0;
  float headsVolume = 0.0f;
  float bodyVolume = 0.0f;
  float tailsVolume = 0.0f;
};

// Состояние затирки
struct MashingState {
  MashPhase phase = MashPhase::IDLE;
  uint8_t currentStep = 0;
  uint32_t stepStartTime = 0;
  float targetTemp = 0.0f;
  uint32_t stepDuration = 0; // секунды
  bool active = false;
};

// Состояние Hold режима
struct HoldState {
  uint8_t currentStep = 0;
  uint8_t stepCount = 0;
  uint32_t stepStartTime = 0;
  TempStep steps[10];
  float targetTemp = 0.0f;
  bool active = false;
};

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

  SystemHealth health;
  PumpState pump;
  CurrentAlarm currentAlarm;
  ProcessStats stats;
  
  // Состояния режимов
  MashingState mashing;
  HoldState hold;
};

// Структуры настроек (именованные для typedef)
struct WiFiSettings {
  char ssid[64] = "";
  char password[64] = "";
  bool apMode = true;
};

struct PumpCalibration {
  float mlPerRevolution = 0.1f;
  uint16_t stepsPerRevolution = 200;
  uint8_t microsteps = 32;
};

struct TempCalibration {
  float offsets[8] = {0};
  uint8_t addresses[8][8] = {0};
};

struct MqttSettings {
  bool enabled = false;
  char server[64] = "";
  uint16_t port = 1883;
  char username[32] = "";
  char password[64] = "";
  char baseTopic[32] = "smart-column";
  uint32_t publishInterval = 10000;
};

struct TelegramSettings {
  bool enabled = false;
  char token[64] = "";
  char chatId[32] = "";
};

struct EquipmentSettings {
  uint16_t columnHeightMm = 1000;
  PackingType packingType = PackingType::SPN_3_5;
  float packingCoeff = 3.5f;
  uint16_t heaterPowerW = 2000;
  float cubeVolumeL = 20.0f;
};

struct FractionatorSettings {
  bool enabled = false;
  uint16_t angles[5] = {0, 45, 90, 135, 180};
  bool positionsEnabled[5] = {true, false, true, false, true};
};

// Калибровка гидрометра
struct HydrometerCalibration {
  float densityOffset = 0.0f;
  uint8_t pointCount = 0; // Количество точек калибровки
  uint8_t abvPoints[10] = {0};
  uint8_t pressurePoints[10] = {0};
};

struct RectParams {
  float headsPercent = 3.0f;
  float headsSpeedMlHKw = 300.0f;
  float bodySpeedMlHKw = 600.0f;
  uint16_t stabilizationMin = 30;
  uint16_t purgeMin = 5;
};

// Настройки (полная версия)
struct Settings {
  WiFiSettings wifi;
  PumpCalibration pumpCal;
  TempCalibration tempCal;
  HydrometerCalibration hydroCal; // Калибровка гидрометра
  MqttSettings mqtt;
  TelegramSettings telegram;
  EquipmentSettings equipment;
  FractionatorSettings fractionator;
  RectParams rectParams;

  uint8_t language = 0; // 0=RU, 1=EN
  uint8_t theme = 0;    // 0=Light, 1=Dark
  bool soundEnabled = true;
  bool demoMode = false; // Демо-режим (симуляция данных)
};

// Глобальные переменные
extern SystemState g_state;
extern Settings g_settings;

// ============================================================================
// Aliases и дополнительные типы для совместимости с драйверами
// ============================================================================

// Короткие имена для структур данных (используются в драйверах)
typedef TemperatureData Temperatures;
typedef PressureData Pressure;
typedef HydrometerData Hydrometer;
typedef PowerData Power;

// Дополнительные типы для драйверов
struct WaterFlow {
  float flowRate = 0.0f;     // л/мин
  float litersPerMin = 0.0f; // Алиас
  float totalVolume = 0.0f;  // л
  float totalLiters = 0.0f;  // Алиас
  bool flowing = false;      // Вода течёт
  uint32_t lastPulse = 0;    // Время последнего импульса
  bool ok = false;
};

// Фракции для фракционатора
enum class Fraction : uint8_t {
  HEADS = 0,
  SUBHEADS,
  BODY,
  PRETAILS,
  TAILS,
  UNKNOWN
};

// Тревога (использует AlarmLevel определённый выше)
struct Alarm {
  AlarmLevel level = AlarmLevel::NONE;
  char message[128] = "";
  uint32_t timestamp = 0;
  bool active = false;
};

// Шаг температурной программы
struct TempStep {
  float temperature = 0.0f;
  uint16_t duration = 0; // минуты
};

// Статистика запуска
struct RunStats {
  uint32_t startTime = 0;
  uint32_t endTime = 0;
  float headsVolume = 0.0f;
  float bodyVolume = 0.0f;
  float tailsVolume = 0.0f;
  float totalEnergy = 0.0f;
};

// Параметры для Arduino Uno (фракционатор)
struct UnoParams {
  Fraction currentFraction = Fraction::UNKNOWN;
  uint16_t targetAngle = 0;
  bool motorEnabled = false;
  bool enabled = false;     // УНО режим включён
  bool state = false;       // Текущее состояние клапана
  uint16_t onSeconds = 3;   // Время открытия (сек)
  uint16_t offSeconds = 60; // Время закрытия (сек)
  uint32_t lastToggle = 0;  // Время последнего переключения
};

// Состояние декремента (для контроля мощности)
struct DecrementState {
  float currentPower = 0.0f;
  float targetPower = 0.0f;
  float baseTemp = 0.0f; // Базовая температура
  uint32_t startTime = 0;
  uint32_t waitStart = 0;      // Начало ожидания
  uint32_t decrementCount = 0; // Счётчик декрементов
  bool active = false;
};

#endif // TYPES_H
