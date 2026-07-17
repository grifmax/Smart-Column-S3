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

enum class RectRefluxMode : uint8_t {
  ML_H = 0,
  SR_RATIO,
  AUTONOMOUS
};

enum class RectTakeoffBackendType : uint8_t {
  PUMP = 0,
  VALVE_MULTI,
  VALVE_SINGLE_SWITCHED
};

enum class RectTakeoffFraction : uint8_t {
  NONE = 0,
  HEADS,
  BODY,
  TAILS
};

struct RectTakeoffCommand {
  RectTakeoffBackendType backendType = RectTakeoffBackendType::PUMP;
  RectTakeoffFraction fraction = RectTakeoffFraction::NONE;
  float requestedEquivalentRateMlH = 0.0f;
  float equivalentRateMlH = 0.0f;
  bool rateLimited = false;
  bool enabled = false;
  bool fullReflux = true;
  bool periodicTakeoff = false;
  bool periodicTakeoffActive = false;
  uint32_t periodicCycleMs = 0;
  uint32_t periodicOpenMs = 0;
};

struct RectTakeoffFeedback {
  RectTakeoffBackendType backendType = RectTakeoffBackendType::PUMP;
  bool backendActive = false;
  bool routingReady = true;
  float requestedEquivalentRateMlH = 0.0f;
  float actualEquivalentRateMlH = 0.0f;
  bool rateLimited = false;
  uint8_t actualDuty = 0;
  float sessionVolumeMl = 0.0f;
  RectTakeoffFraction requestedFraction = RectTakeoffFraction::NONE;
  RectTakeoffFraction routedFraction = RectTakeoffFraction::NONE;
  RectTakeoffFraction activeFraction = RectTakeoffFraction::NONE;
  RectTakeoffFraction activeValve = RectTakeoffFraction::NONE;
};

enum RectPhasePowerIndex : uint8_t {
  RECT_POWER_STABILIZATION = 0,
  RECT_POWER_HEADS = 1,
  RECT_POWER_BODY = 2,
  RECT_POWER_TAILS = 3,
  RECT_POWER_COUNT = 4
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
  PREPARE,
  HEATING,
  COOLING,
  FERMENTATION,
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

// Types of recipe actions. Legacy temperature rests map to HEAT_AND_HOLD.
enum class MashStepType : uint8_t {
  HEAT_AND_HOLD = 0, HEAT, HOLD, OPERATOR_WAIT, BOIL, COOL, STIR, FINISH
};

inline const char* mashStepTypeToString(MashStepType type) {
  switch (type) {
    case MashStepType::HEAT: return "heat";
    case MashStepType::HOLD: return "hold";
    case MashStepType::OPERATOR_WAIT: return "operator_wait";
    case MashStepType::BOIL: return "boil";
    case MashStepType::COOL: return "cool";
    case MashStepType::STIR: return "stir";
    case MashStepType::FINISH: return "finish";
    default: return "heat_hold";
  }
}

inline MashStepType mashStepTypeFromString(const char* value) {
  if (value == nullptr) return MashStepType::HEAT_AND_HOLD;
  if (strcmp(value, "heat") == 0) return MashStepType::HEAT;
  if (strcmp(value, "hold") == 0) return MashStepType::HOLD;
  if (strcmp(value, "operator_wait") == 0) return MashStepType::OPERATOR_WAIT;
  if (strcmp(value, "boil") == 0) return MashStepType::BOIL;
  if (strcmp(value, "cool") == 0) return MashStepType::COOL;
  if (strcmp(value, "stir") == 0) return MashStepType::STIR;
  if (strcmp(value, "finish") == 0) return MashStepType::FINISH;
  return MashStepType::HEAT_AND_HOLD;
}

// Профиль затирания
struct MashProfile {
  char name[32];
  uint8_t stepCount;
  struct {
    MashStepType type = MashStepType::HEAT_AND_HOLD;
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

static constexpr uint8_t BOOT_GPIO_CHECK_MAX = 12;

struct BootGpioCheckItem {
  int16_t pin = -1;
  uint8_t mode = 0;       // 0=OUTPUT_LOW, 1=OUTPUT_HIGH, 2=INPUT, 3=INPUT_PULLUP
  int8_t expectedLevel = -1;
  int8_t actualLevel = -1;
  bool ok = false;
  char label[24] = "";
};

struct BootGpioSelfTest {
  bool completed = false;
  bool overallOk = false;
  uint8_t checkedCount = 0;
  uint32_t timestampMs = 0;
  char boardRev[16] = "";
  BootGpioCheckItem items[BOOT_GPIO_CHECK_MAX];
};

extern BootGpioSelfTest g_bootGpioSelfTest;

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
  MashStepType stepType = MashStepType::HEAT_AND_HOLD;
  bool waitingForOperator = false;
  bool manualAdvanceRequested = false;
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

struct FractionProgramRuntime {
  bool active = false;
  bool waitingForConfirmation = false;
  bool routing = false;
  uint8_t currentStep = 0;
  uint32_t stepStartedAtMs = 0;
  float stepStartVolumeMl = 0.0f;
  uint32_t routingStartedAtMs = 0;
  bool manualAdvanceRequested = false;
  uint8_t lastEndReason = 0;
  char confirmationPrompt[64] = {};
};

// Состояние системы (полная версия)
struct SystemState {
  Mode mode = Mode::IDLE;
  RectPhase rectPhase = RectPhase::IDLE;
  bool paused = false;
  bool safetyOk = true;
  uint8_t rectBodyContainerIndex = 0;
  float rectBodyContainerVolumeMl = 0.0f;
  float rectBodyContainerStartVolumeMl = 0.0f;
  bool rectBodyContainerLevelReached = false;
  bool rectHeadsContainerLevelReached = false;
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
  FractionProgramRuntime fractionProgram;
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

struct TemperatureTopologySettings {
  bool cube = true;
  bool columnBottom = true;
  bool columnTop = true;
  bool reflux = true;
  bool tsa = true;
  bool waterIn = true;
  bool waterOut = true;
};

struct EquipmentSettings {
  uint16_t columnHeightMm = 1000;
  PackingType packingType = PackingType::SPN_3_5;
  float packingCoeff = 3.5f;
  uint16_t heaterPowerW = 2000;
  float cubeVolumeL = 37.0f;
  float minHeaterSubmergeL = 7.5f;
  float waterAutoStartCubeTempC = 45.0f;
  bool boosterHeaterEnabled = false;
  uint16_t boosterHeaterPowerW = 3000;
  float boosterHeaterStopCubeTempC = 78.0f;
  bool coolingPwmEnabled = false;
  uint8_t coolingPwmMinDuty = 0;
  uint8_t coolingPwmMaxDuty = 255;
  uint8_t coolingPwmStartupDuty = 96;
  bool useDs2482ForTemps = false;
  uint8_t ds2482Address = 0x18;
  bool bodyLevelSensorEnabled = false;
  float bodyLevelThresholdV = 1.5f;
  bool bodyLevelTriggerAbove = true;
  bool leakSensorEnabled = false;
  float leakThresholdV = 1.5f;
  bool leakTriggerAbove = true;
  TemperatureTopologySettings temperatureTopology;
};

struct FractionatorSettings {
  bool enabled = false;
  uint16_t angles[5] = {0, 36, 72, 108, 144};
  bool positionsEnabled[5] = {true, false, true, false, true};
};

static constexpr uint8_t FRACTION_PROGRAM_MAX_STEPS = 8;

enum FractionProgramEndReason : uint8_t {
  FRACTION_PROGRAM_REASON_NONE = 0,
  FRACTION_PROGRAM_REASON_VOLUME,
  FRACTION_PROGRAM_REASON_TIME,
  FRACTION_PROGRAM_REASON_TEMPERATURE,
  FRACTION_PROGRAM_REASON_LEVEL,
  FRACTION_PROGRAM_REASON_MANUAL
};

enum FractionProgramEndCondition : uint8_t {
  FRACTION_PROGRAM_END_NONE = 0,
  FRACTION_PROGRAM_END_VOLUME = 1 << 0,
  FRACTION_PROGRAM_END_TIME = 1 << 1,
  FRACTION_PROGRAM_END_TEMPERATURE = 1 << 2,
  FRACTION_PROGRAM_END_LEVEL = 1 << 3
};

struct FractionProgramStep {
  char name[24] = {};
  uint8_t routeIndex = 0;
  float pumpRateMlH = 0.0f;
  uint16_t heaterPowerW = 0;
  bool requireOperatorConfirmation = false;
  char confirmationPrompt[64] = {};
  uint8_t endConditions = FRACTION_PROGRAM_END_NONE;
  float endVolumeMl = 0.0f;
  uint32_t endDurationSec = 0;
  uint8_t temperatureSensorIndex = 0;
  float endTemperatureC = 0.0f;
  bool allowManualAdvance = false;
};

struct FractionProgram {
  uint8_t schemaVersion = 2;
  bool enabled = false;
  uint8_t stepCount = 0;
  uint8_t heatingTemperatureSensorIndex = 0;
  float heatingTargetTemperatureC = 78.0f;
  FractionProgramStep steps[FRACTION_PROGRAM_MAX_STEPS];
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
  float zeroOffsetMmHg = 0.0f;
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
  RectTakeoffBackendType takeoffBackendType = RectTakeoffBackendType::PUMP;
  RectRefluxMode refluxMode = RectRefluxMode::ML_H;
  float srRatio = 0.0f;
  uint16_t autonomousCycleSec = 900;
  uint16_t autonomousPauseSec = 90;
  float chimAutoPercent = 0.0f;
  float chimTimePerH = 0.0f;
  float chimBegPercent = 0.0f;
  float chimMinPercent = 35.0f;
  uint8_t phasePowerPercent[RECT_POWER_COUNT] = {70, 60, 60, 50};
  uint8_t usePbMode = 0;
  uint32_t timpPbMs = 15000;
  uint16_t valvePulsePeriodMs = 1000;
  uint16_t valvePulseMinOpenMs = 80;
  uint16_t valvePulseMaxOpenMs = 900;
  uint16_t routingSettlingMs = 1500;
  uint16_t routingRetargetMinMs = 3000;
  uint8_t bodyContainerCount = 1;
};

struct DistillationUiSettings {
  float speedMlH = 500.0f;
  float headsVolumeMl = 0.0f;
  float targetVolumeMl = 3000.0f;
  float endTempC = 96.0f;
  float powerW = 3000.0f;
  float powerPercent = 100.0f;
  float tailsVolumeMl = 0.0f;
  RectTakeoffBackendType takeoffBackendType = RectTakeoffBackendType::PUMP;
  bool valveSafeVentConfirmed = false;
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
  bool useCooling = true;
  uint16_t coolingMinOnSec = 60;
  uint16_t coolingMinOffSec = 60;
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
  FractionProgram fractionProgram;
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
  uint32_t rebootCountTotal = 0;
  uint32_t rebootCountWdt = 0;
  uint32_t rebootCountCrash = 0;
  uint32_t rebootCountUser = 0;
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
