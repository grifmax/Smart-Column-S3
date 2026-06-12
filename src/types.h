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
  HOLD,       // Температурные ступени (Hold режим)
  NBK,        // Непрерывная бражная колонна
  FERMENTATION// Ферментация (брожение)
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

// Фазы НБК
enum class NbkPhase : uint8_t {
  IDLE = 0,
  HEATING,
  STABILIZATION,
  WORKING,
  FINISH,
  COMPLETED
};

// Фазы Ферментации
enum class FermentationPhase : uint8_t {
  IDLE = 0,
  RUNNING,
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
  WATER_RISE_RATE,    // Резкий рост температуры воды
  COLUMN_FLOOD,       // Захлёб колонны
  PRESSURE_RISE_RATE, // Резкий рост давления в кубе
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
  float sensorVoltage = 0.0f;
  int16_t sensorAdc = 0;
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
  float pressure = 0.0f;
  float density = 0.0f;     // Плотность кг/м³
  float abv = 0.0f;
  float temperature = 0.0f; // Температура замера
  bool valid = false;       // Данные валидны
  bool ok = false;          // Сенсор работает
  uint32_t lastUpdate = 0;  // Время последнего обновления
  float abvPoints[10] = {0.0f};
  float pressurePoints[10] = {0.0f};
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
  uint32_t sequence = 0;
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
  uint8_t tempSensorsOk = 0;    // количество рабочих датчиков
  uint8_t tempSensorsTotal = 0;
  uint16_t tempErrors[7] = {0}; // ошибки по каждому датчику (Analysis Step 14)
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
  uint8_t lastRebootReason = 0; // esp_reset_reason_t
  uint32_t lastUpdate = 0;

  // Взвешенные оценки подсистем для детального анализа: 0=SENSORS, 1=WIFI, 2=POWER, 3=STORAGE, 4=OTA, 5=SAFETY
  float healthScores[6] = {100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f};
  // Весовые коэффициенты для подсистем (должны в сумме давать 1.0)
  static const float healthWeights[6];
};

// Структура для отслеживания перезагрузок
struct RebootTracker {
  uint32_t totalReboots = 0;
  uint32_t wdtReboots = 0;
  uint32_t crashReboots = 0;
  uint32_t userReboots = 0;
  uint8_t lastReason = 0;
  char lastReasonStr[32] = "Unknown";
};

extern RebootTracker g_rebootTracker;

// Состояние насоса
struct PumpState {
  bool running = false;
  float speedMlPerHour = 0.0f;
  float totalVolumeMl = 0.0f;
};

// Состояние мешалки (0-10В через MCP4725)
struct StirrerState {
  bool running = false;
  uint8_t speedPercent = 0;  // 0–100%
  bool available = false;    // MCP4725 обнаружен
  bool autoMode = false;     // работает в авто-режиме (из FSM)
  uint32_t lastUpdate = 0;
};

// Настройки мешалки
struct StirrerSettings {
  bool enabled = false;
  uint8_t defaultSpeedPercent = 50;   // % скорости по умолчанию
  bool autoMashing = true;             // авто-запуск при затирании
  bool autoFermentation = false;       // авто-запуск при ферментации
  bool autoNbk = false;                // авто-запуск при НБК
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

// Шаг температурной программы (Hold)
struct TempStep {
  float temperature = 0.0f;
  uint16_t duration = 0; // ??????
  bool useCooling = false;
};

// Состояние затирки
struct MashingState {
  MashPhase phase = MashPhase::IDLE;
  uint8_t stepCount = 0;
  uint8_t currentStep = 0;
  uint32_t stepStartTime = 0;
  // Таймер выдержки считаем только когда температура в допуске
  bool tempInRange = false;
  uint32_t inRangeStartTime = 0;
  float targetTemp = 0.0f;
  char stepName[32] = "";
  uint32_t stepDuration = 0; // секунды
  bool active = false;
};

// Состояние Hold режима
struct HoldState {
  uint8_t currentStep = 0;
  uint8_t stepCount = 0;
  uint32_t stepStartTime = 0;
  // Таймер выдержки считаем только когда температура в допуске
  bool tempInRange = false;
  uint32_t inRangeStartTime = 0;
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
  StirrerState stirrer;
  CurrentAlarm currentAlarm;
  ProcessStats stats;
  
  // Состояния режимов
  MashingState mashing;
  HoldState hold;
  NbkPhase nbkPhase = NbkPhase::IDLE;
  FermentationPhase fermPhase = FermentationPhase::IDLE;
};

// Структуры настроек (именованные для typedef)
static constexpr uint8_t WIFI_MAX_PROFILES = 8;

struct WiFiProfile {
  bool enabled = true;
  char ssid[64] = "";
  char password[64] = "";
  bool useStaticIp = false;
  char ip[16] = "";
  char gateway[16] = "";
  char subnet[16] = "255.255.255.0";
  char dns1[16] = "";
  char dns2[16] = "";
};

struct WiFiSettings {
  char ssid[64] = "";
  char password[64] = "";
  bool apMode = true;
  uint8_t profileCount = 0;
  WiFiProfile profiles[WIFI_MAX_PROFILES];
};

struct PumpCalibration {
  float mlPerRevolution = 0.1f;
  uint16_t stepsPerRevolution = 200;
  uint8_t microsteps = 32;
};

struct TouchCalibration {
  int16_t xMin = 0;
  int16_t xMax = 0;
  int16_t yMin = 0;
  int16_t yMax = 0;
  bool valid = false;
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
  bool discovery = true;
};

// Cloud tunnel (IoT модель, без входящих портов на ESP32)
struct CloudSettings {
  bool enabled = false;
  char tunnelUrl[128] = ""; // например: wss://tunnel.example.com/ws/device
  char token[96] = "";      // device token после привязки
  char tokenId[64] = "";    // tokenId для отладки/ротации
};

struct EquipmentSettings {
  uint16_t columnHeightMm = 1000;
  PackingType packingType = PackingType::SPN_3_5;
  float packingCoeff = 3.5f;
  uint16_t heaterPowerW = 2000;
  float cubeVolumeL = 37.0f;
  float minHeaterSubmergeL = 7.5f;
  float waterAutoStartCubeTempC = 45.0f;
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
  float abvPoints[10] = {0.0f};
  float pressurePoints[10] = {0.0f};
};

struct PressureSensorCalibration {
  uint8_t pointCount = 0;
  float voltagePoints[5] = {0.0f};
  float pressurePoints[5] = {0.0f};
};

struct RectParams {
  // Сырьё/затор для спирта-сырца (для дефолтов по фракциям)
  uint8_t feedstock = 0; // 0=sugar,1=grain/flour,2=malt,3=fruit,4=molasses,5=grape,6=honey,7=other
  float feedVolumeL = 37.0f;
  float feedAbvPercent = 40.0f;
  float headsPercent = 8.0f;
  float bodyPercent = 84.0f;
  float tailsPercent = 8.0f;
  float headsSpeedMlHKw = 300.0f;
  float bodySpeedMlHKw = 600.0f;
  uint16_t stabilizationMin = 30;
  uint16_t purgeMin = 5;
  bool baroCorrectionEnabled = true;
};

struct DistillationUiSettings {
  float speedMlH = 500.0f;
  float headsVolumeMl = 0.0f;
  float targetVolumeMl = 3000.0f;
  float endTempC = 96.0f;
  float powerPercent = 100.0f;
  float tailsVolumeMl = 0.0f;
};

struct SecuritySettings {
  bool authEnabled = false;
  bool rateLimitEnabled = true;
  char username[32] = "admin";
  char password[64] = "";
};

struct SafetySettings {
  float pressureMaxMmHg = 50.0f;
  float tsaMaxC = 55.0f;
  float waterOutMaxC = 70.0f;
  float waterOutRiseRateCMin = 8.0f;
  float pressureRiseRateMmHgMin = 20.0f;
};

struct NbkSettings {
  float powerW = 2500.0f;
  float pumpSpeedMlH = 20000.0f; // 20 л/ч
  float columnBottomTempThresholdC = 95.0f;
  float targetVolumeMl = 0.0f;   // #4 fix: целевой объём (0 = неизвестно, прогресс без него — 0%)
};

struct FermentationSettings {
  float targetTempC = 28.0f;
  float hysteresisC = 0.5f;
  bool useHeater = true;
  uint16_t durationHours = 0;    // #4 fix: плановая длительность (0 = неизвестно, прогресс без неё — 0%)
};

enum class DisplayRefreshProfile : uint8_t {
  NORMAL = 0, // 1000ms
  SAFE = 1,   // 5000ms
  FAST = 2    // 500ms
};

// Настройки дисплея
struct DisplaySettings {
  bool enabled = true;      // Включен ли дисплей
  uint8_t brightness = 255; // Яркость (0-255)
  int8_t rotation = 1;      // Поворот дисплея (0, 1, 2, 3)
  bool invertColors = false; // Инвертировать цвета
  uint8_t contrast = 128;   // Контрастность (0-255)
  int timeout = 0;          // Таймаут отключения (0 = не отключать)
  bool showLogo = true;     // Показывать логотип при запуске
  DisplayRefreshProfile refreshProfile = DisplayRefreshProfile::NORMAL;
};

// Настройки (полная версия)
struct Settings {
  WiFiSettings wifi;
  PumpCalibration pumpCal;
  TouchCalibration touchCal;
  TempCalibration tempCal;
  PressureSensorCalibration pressureCal;
  HydrometerCalibration hydroCal; // Калибровка гидрометра
  MqttSettings mqtt;
  CloudSettings cloud;
  EquipmentSettings equipment;
  FractionatorSettings fractionator;
  DisplaySettings displaySettings; // Настройки дисплея
  RectParams rectParams;
  DistillationUiSettings distillationUi;
  SecuritySettings security;
  SafetySettings safety;
  NbkSettings nbk;
  FermentationSettings fermentation;
  StirrerSettings stirrer;

  uint8_t language = 0; // 0=RU, 1=EN
  uint8_t theme = 0;    // 0=Light, 1=Dark
  bool soundEnabled = true;
  uint8_t lastRebootReason = 0; // Store last CRITICAL reboot reason
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
