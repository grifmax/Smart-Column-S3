#include "api_routes.h"

#include <string.h>

#include "config.h"
#include "control/safety.h"
#include "control/v2/reason_codes.h"
#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/sensors.h"
#include "drivers/stirrer.h"
#include "drivers/valves.h"
#include "history.h"
#include "interface/webserver_shared.h"
#include "storage/logger.h"
#include "storage/nvs_manager.h"

struct EquipmentTestingAction {
  uint32_t timestampMs = 0;
  char tone[12] = "";
  char title[72] = "";
  char detail[160] = "";
};

static constexpr uint8_t EQUIPMENT_TESTING_ACTION_CAPACITY = 10;
static EquipmentTestingAction
    g_equipmentTestingActions[EQUIPMENT_TESTING_ACTION_CAPACITY];
static uint8_t g_equipmentTestingActionCount = 0;
static uint8_t g_equipmentTestingActionNext = 0;

static const char *equipmentTestingToneToHistorySeverity(const char *tone) {
  if (!tone) {
    return "info";
  }
  if (strcmp(tone, "danger") == 0 || strcmp(tone, "warning") == 0) {
    return "warning";
  }
  return "info";
}

static uint8_t equipmentTestingToneToLogLevel(const char *tone) {
  return (tone && strcmp(tone, "danger") == 0) ? 1 : 0;
}

static void sanitizeEquipmentTestingLogValue(char *dest, size_t destSize,
                                             const char *src) {
  if (!dest || destSize == 0) {
    return;
  }
  if (!src) {
    dest[0] = '\0';
    return;
  }

  size_t out = 0;
  while (*src != '\0' && out + 1 < destSize) {
    char ch = *src++;
    if (ch == '"' || ch == '\r' || ch == '\n') {
      ch = '\'';
    }
    dest[out++] = ch;
  }
  dest[out] = '\0';
}

static void appendEquipmentTestingHistoryAction(const char *tone,
                                                const char *title,
                                                const char *detail) {
  if (!processRecorder.isRecording()) {
    return;
  }

  processRecorder.addWarning(
      detail ? detail : "Сервисное действие оператора",
      equipmentTestingToneToHistorySeverity(tone),
      ControlV2::reasonCodeToString(
          ControlV2::ReasonCodeV2::RC_OPERATOR_SERVICE_ACTION),
      title ? String(title) : String("Сервисное действие"));
}

static void appendEquipmentTestingSystemLog(const char *tone, const char *title,
                                            const char *detail) {
  char safeTone[16];
  char safeTitle[56];
  char safeDetail[104];
  sanitizeEquipmentTestingLogValue(safeTone, sizeof(safeTone),
                                   tone ? tone : "info");
  sanitizeEquipmentTestingLogValue(safeTitle, sizeof(safeTitle),
                                   title ? title : "Сервисное действие");
  sanitizeEquipmentTestingLogValue(safeDetail, sizeof(safeDetail),
                                   detail ? detail : "");

  LogEvent event{};
  event.timestamp = millis();
  event.level = equipmentTestingToneToLogLevel(tone);
  snprintf(event.message, sizeof(event.message),
           "equipment_test tone=%s title=\"%s\" detail=\"%s\"", safeTone,
           safeTitle, safeDetail);
  Logger::log(event);
}

static void recordEquipmentTestingAction(const char *tone, const char *title,
                                         const char *detail) {
  EquipmentTestingAction &entry =
      g_equipmentTestingActions[g_equipmentTestingActionNext];
  entry.timestampMs = millis();
  strlcpy(entry.tone, tone ? tone : "info", sizeof(entry.tone));
  strlcpy(entry.title, title ? title : "Сервисное действие",
          sizeof(entry.title));
  strlcpy(entry.detail, detail ? detail : "", sizeof(entry.detail));

  g_equipmentTestingActionNext =
      (g_equipmentTestingActionNext + 1) % EQUIPMENT_TESTING_ACTION_CAPACITY;
  if (g_equipmentTestingActionCount < EQUIPMENT_TESTING_ACTION_CAPACITY) {
    g_equipmentTestingActionCount++;
  }

  appendEquipmentTestingSystemLog(tone, title, detail);
  appendEquipmentTestingHistoryAction(tone, title, detail);
}

static bool isEquipmentTestingBlocked(char *reason, size_t reasonSize) {
  if (g_state.mode != Mode::IDLE) {
    snprintf(reason, reasonSize, "Тестирование доступно только в режиме простоя");
    return true;
  }

  if (Safety::isLatched(g_state)) {
    snprintf(reason, reasonSize, "Сначала снимите защелкнутую аварию безопасности");
    return true;
  }

  if (g_state.currentAlarm.type != AlarmType::NONE) {
    snprintf(reason, reasonSize, "Активна авария: %s", g_state.currentAlarm.message);
    return true;
  }

  if (!g_state.safetyOk) {
    snprintf(reason, reasonSize, "Система находится в небезопасном состоянии");
    return true;
  }

  reason[0] = '\0';
  return false;
}

void fillEquipmentModulesJson(JsonObject modules) {
  JsonObject bmp280Primary = modules["bmp280Primary"].to<JsonObject>();
  bmp280Primary["label"] = "BMP280 #1";
  bmp280Primary["available"] = Sensors::isBmp280PrimaryAvailable();
  bmp280Primary["expected"] = true;
  bmp280Primary["bus"] = "I2C";
  bmp280Primary["address"] = "0x76";
  bmp280Primary["role"] = "Атмосфера, основной";

  JsonObject bmp280Secondary = modules["bmp280Secondary"].to<JsonObject>();
  bmp280Secondary["label"] = "BMP280 #2";
  bmp280Secondary["available"] = Sensors::isBmp280SecondaryAvailable();
  bmp280Secondary["expected"] = false;
  bmp280Secondary["bus"] = "I2C";
  bmp280Secondary["address"] = "0x77";
  bmp280Secondary["role"] = "Атмосфера, резерв";

  JsonObject ads1115 = modules["ads1115"].to<JsonObject>();
  ads1115["label"] = "ADS1115";
  ads1115["available"] = Sensors::isAds1115Available();
  ads1115["expected"] = true;
  ads1115["bus"] = "I2C";
  ads1115["address"] = "0x48";
  ads1115["role"] = "A0 ареометр, A1 давление куба, A2 уровень тела, A3 протечка";

  JsonObject ads1115Secondary = modules["ads1115Secondary"].to<JsonObject>();
  ads1115Secondary["label"] = "ADS1115 #2";
  ads1115Secondary["available"] = false;
  ads1115Secondary["expected"] = false;
  ads1115Secondary["planned"] = true;
  ads1115Secondary["bus"] = "I2C";
  ads1115Secondary["address"] = "0x49";
  ads1115Secondary["role"] =
      "Резерв под датчики уровня приёмных ёмкостей и будущие аналоговые каналы";

  JsonObject ds2482 = modules["ds2482"].to<JsonObject>();
  ds2482["label"] = "DS2482S-100";
  ds2482["available"] = Sensors::isDs2482Available();
  ds2482["expected"] = g_settings.equipment.useDs2482ForTemps;
  ds2482["bus"] = "I2C";
  char ds2482Address[8];
  snprintf(ds2482Address, sizeof(ds2482Address), "0x%02X",
           Sensors::getDs2482Address());
  ds2482["address"] = ds2482Address;
  ds2482["role"] = g_settings.equipment.useDs2482ForTemps
                       ? "Активный мастер 1-Wire для DS18B20"
                       : "Резервный I2C-to-1-Wire мост под DS18B20";

  JsonObject mcp4725 = modules["mcp4725"].to<JsonObject>();
  mcp4725["label"] = "MCP4725";
  mcp4725["available"] = Stirrer::isAvailable();
  mcp4725["expected"] = false;
  mcp4725["bus"] = "I2C";
  mcp4725["address"] = "0x60";
  mcp4725["role"] = "DAC мешалки 0-10В";

  JsonObject pzemModule = modules["pzem004t"].to<JsonObject>();
  pzemModule["label"] = "PZEM-004T";
  pzemModule["available"] = Sensors::isPzemAvailable();
  pzemModule["expected"] = true;
  pzemModule["bus"] = "UART";
  pzemModule["address"] = "UART1";
  pzemModule["role"] = "Монитор сети и нагрева";
  pzemModule["baudRate"] = PZEM_BAUD_RATE;
  pzemModule["rxPin"] = PIN_PZEM_RX;
  pzemModule["txPin"] = PIN_PZEM_TX;
}

void fillSafetyChannelsJson(JsonObject channels) {
  const bool adsOnline = Sensors::isAds1115Available();
  const float maxAdsVoltage = 4.096f;

  int16_t bodyLevelAdc = 0;
  float bodyLevelVoltage = 0.0f;
  const bool bodyLevelReadable = Sensors::readAds1115Channel(
      ADS_CHANNEL_LEVEL_BODY, bodyLevelAdc, bodyLevelVoltage);
  const bool bodyLevelTriggered =
      g_settings.equipment.bodyLevelSensorEnabled && bodyLevelReadable &&
      (g_settings.equipment.bodyLevelTriggerAbove
           ? (bodyLevelVoltage >= g_settings.equipment.bodyLevelThresholdV)
           : (bodyLevelVoltage <= g_settings.equipment.bodyLevelThresholdV));

  int16_t leakAdc = 0;
  float leakVoltage = 0.0f;
  const bool leakReadable =
      Sensors::readAds1115Channel(ADS_CHANNEL_LEAK, leakAdc, leakVoltage);
  const bool leakTriggered =
      g_settings.equipment.leakSensorEnabled && leakReadable &&
      (g_settings.equipment.leakTriggerAbove
           ? (leakVoltage >= g_settings.equipment.leakThresholdV)
           : (leakVoltage <= g_settings.equipment.leakThresholdV));

  JsonObject bodyLevel = channels["bodyLevel"].to<JsonObject>();
  bodyLevel["label"] = "Уровень банки тела";
  bodyLevel["status"] =
      !adsOnline
          ? "offline"
          : (!bodyLevelReadable
                 ? "no_signal"
                 : (g_settings.equipment.bodyLevelSensorEnabled
                        ? (bodyLevelTriggered ? "triggered" : "armed")
                        : "ready"));
  bodyLevel["available"] = adsOnline;
  bodyLevel["expected"] = g_settings.equipment.bodyLevelSensorEnabled;
  bodyLevel["planned"] = true;
  bodyLevel["liveReadable"] = bodyLevelReadable;
  bodyLevel["bus"] = "ADS1115";
  bodyLevel["address"] = "0x48 / A2";
  bodyLevel["role"] = "Канал для датчика уровня приёмной банки тела";
  bodyLevel["source"] = "ADS1115 A2";
  bodyLevel["channel"] = ADS_CHANNEL_LEVEL_BODY;
  bodyLevel["enabled"] = g_settings.equipment.bodyLevelSensorEnabled;
  bodyLevel["thresholdV"] = g_settings.equipment.bodyLevelThresholdV;
  bodyLevel["triggerAbove"] = g_settings.equipment.bodyLevelTriggerAbove;
  bodyLevel["triggerMode"] =
      g_settings.equipment.bodyLevelTriggerAbove ? "above" : "below";
  bodyLevel["triggered"] = bodyLevelTriggered;
  bodyLevel["maxVoltage"] = maxAdsVoltage;
  bodyLevel["adc"] = bodyLevelReadable ? bodyLevelAdc : 0;
  bodyLevel["voltage"] = bodyLevelReadable ? bodyLevelVoltage : 0.0f;

  JsonObject leak = channels["leak"].to<JsonObject>();
  leak["label"] = "Протечка / поддон";
  leak["status"] =
      !adsOnline
          ? "offline"
          : (!leakReadable
                 ? "no_signal"
                 : (g_settings.equipment.leakSensorEnabled
                        ? (leakTriggered ? "triggered" : "armed")
                        : "ready"));
  leak["available"] = adsOnline;
  leak["expected"] = g_settings.equipment.leakSensorEnabled;
  leak["planned"] = true;
  leak["liveReadable"] = leakReadable;
  leak["bus"] = "ADS1115";
  leak["address"] = "0x48 / A3";
  leak["role"] = "Канал для датчика протечки под колонной или в поддоне";
  leak["source"] = "ADS1115 A3";
  leak["channel"] = ADS_CHANNEL_LEAK;
  leak["enabled"] = g_settings.equipment.leakSensorEnabled;
  leak["thresholdV"] = g_settings.equipment.leakThresholdV;
  leak["triggerAbove"] = g_settings.equipment.leakTriggerAbove;
  leak["triggerMode"] =
      g_settings.equipment.leakTriggerAbove ? "above" : "below";
  leak["triggered"] = leakTriggered;
  leak["maxVoltage"] = maxAdsVoltage;
  leak["adc"] = leakReadable ? leakAdc : 0;
  leak["voltage"] = leakReadable ? leakVoltage : 0.0f;

  JsonObject vaporPrimary = channels["vaporPrimary"].to<JsonObject>();
  vaporPrimary["label"] = "Газ / пар #1";
  vaporPrimary["status"] = "reserved";
  vaporPrimary["available"] = false;
  vaporPrimary["expected"] = false;
  vaporPrimary["planned"] = true;
  vaporPrimary["liveReadable"] = false;
  vaporPrimary["bus"] = "ESP32 ADC";
  vaporPrimary["address"] = "GPIO1";
  vaporPrimary["role"] =
      "Резерв под датчик газа или паров; пока канал только подготовлен";
  vaporPrimary["source"] = "GPIO1";
  vaporPrimary["pin"] = PIN_VAPOR_SENSOR_ADC_1;
  vaporPrimary["enabled"] = false;
  vaporPrimary["triggered"] = false;

  JsonObject vaporSecondary = channels["vaporSecondary"].to<JsonObject>();
  vaporSecondary["label"] = "Газ / пар #2";
  vaporSecondary["status"] = "reserved";
  vaporSecondary["available"] = false;
  vaporSecondary["expected"] = false;
  vaporSecondary["planned"] = true;
  vaporSecondary["liveReadable"] = false;
  vaporSecondary["bus"] = "ESP32 ADC";
  vaporSecondary["address"] = "GPIO3";
  vaporSecondary["role"] =
      "Резервный ADC-канал для второго датчика газа или паров";
  vaporSecondary["source"] = "GPIO3";
  vaporSecondary["pin"] = PIN_VAPOR_SENSOR_ADC_2;
  vaporSecondary["enabled"] = false;
  vaporSecondary["triggered"] = false;
}

static void fillEquipmentTestingHealthJson(JsonObject health) {
  health["overall"] = g_state.health.overallHealth;
  health["tempSensorsOk"] = g_state.health.tempSensorsOk;
  health["tempSensorsTotal"] = g_state.health.tempSensorsTotal;
  health["bmp280"] = g_state.health.bmp280Ok;
  health["ads1115"] = g_state.health.ads1115Ok;
  health["pzem"] = g_state.health.pzemOk;
  health["wifiConnected"] = g_state.health.wifiConnected;
  health["wifiRSSI"] = g_state.health.wifiRSSI;
  health["freeHeap"] = g_state.health.freeHeap;
  health["cpuTemp"] = g_state.health.cpuTemp;
  health["pzemSpikes"] = g_state.health.pzemSpikeCount;
  health["tempErrors"] = g_state.health.tempReadErrors;
  health["lastUpdate"] = g_state.health.lastUpdate;
  health["rebootReason"] = g_rebootTracker.lastReason;
  health["rebootReasonStr"] = g_rebootTracker.lastReasonStr;
}

static void fillEquipmentTestingStatus(JsonDocument &doc) {
  char reason[160] = "";
  const bool blocked = isEquipmentTestingBlocked(reason, sizeof(reason));
  const bool processActive = g_state.mode != Mode::IDLE;
  const bool latched = Safety::isLatched(g_state);
  const bool alarmActive = (g_state.currentAlarm.type != AlarmType::NONE);
  const Heater::Diagnostics heaterDiag = Heater::getDiagnostics();
  const bool heaterActive = heaterDiag.active;
  const bool servoAvailable =
      g_settings.fractionator.enabled && Valves::isFractionatorEnabled();
  const Pump::Diagnostics pumpDiag = Pump::getDiagnostics();

  syncStirrerState();

  doc["success"] = true;
  doc["mode"] = getModeString(g_state.mode);
  doc["processActive"] = processActive;
  doc["testingAllowed"] = !blocked;
  doc["availabilityReason"] = blocked ? reason : "";
  doc["demoMode"] = g_settings.demoMode;
  doc["physicalActuationAllowed"] = !blocked && !g_settings.demoMode;
  doc["alarmActive"] = alarmActive;
  doc["alarmMessage"] = alarmActive ? g_state.currentAlarm.message : "";
  doc["safetyOk"] = g_state.safetyOk;
  doc["safetyLatched"] = latched;

  JsonObject activeTests = doc["activeTests"].to<JsonObject>();
  activeTests["pump"] = Pump::isRunning();
  activeTests["stirrer"] = g_state.stirrer.running;
  activeTests["heater"] = heaterActive;
  activeTests["waterValve"] = Valves::getWater();
  activeTests["headsValve"] = Valves::getHeads();
  activeTests["unoValve"] = Valves::getUno();
  activeTests["startStopDuty"] = Valves::getStartStop() > 0;
  activeTests["servoMoving"] = Valves::isServoMoving();

  JsonObject pump = doc["pump"].to<JsonObject>();
  pump["running"] = Pump::isRunning();
  pump["speedMlH"] = Pump::getSpeed();
  pump["targetSpeedMlH"] = pumpDiag.speedMlH;
  pump["appliedSpeedMlH"] = pumpDiag.appliedSpeedMlH;
  pump["totalVolumeMl"] = Pump::getTotalVolume();
  pump["totalSteps"] = pumpDiag.totalSteps;
  pump["taskAlive"] = pumpDiag.taskAlive;
  pump["mutexReady"] = pumpDiag.mutexReady;
  pump["lockTimeoutCount"] = pumpDiag.lockTimeoutCount;
  pump["taskLoopCount"] = pumpDiag.taskLoopCount;
  pump["cooperativeSleepCount"] = pumpDiag.cooperativeSleepCount;
  pump["fastYieldCount"] = pumpDiag.fastYieldCount;

  JsonObject stirrer = doc["stirrer"].to<JsonObject>();
  fillStirrerJson(stirrer, g_state);
  stirrer["enabled"] = g_settings.stirrer.enabled;
  stirrer["defaultSpeedPercent"] = g_settings.stirrer.defaultSpeedPercent;

  JsonObject heater = doc["heater"].to<JsonObject>();
  heater["active"] = heaterActive;
  heater["powerPercent"] = heaterDiag.mainPowerPercent;
  heater["powerSetPercent"] = heaterDiag.powerSetPercent;
  heater["powerSetW"] = heaterDiag.targetPowerWatts;
  heater["actualPowerW"] = heaterDiag.actualPowerWatts;
  heater["powerErrorW"] = heaterDiag.powerErrorWatts;
  heater["mainPowerW"] = g_settings.equipment.heaterPowerW;
  heater["boosterConfigured"] = g_settings.equipment.boosterHeaterEnabled;
  heater["boosterPowerW"] = g_settings.equipment.boosterHeaterPowerW;
  heater["boosterStopCubeTempC"] =
      g_settings.equipment.boosterHeaterStopCubeTempC;
  heater["backend"] = heaterDiag.triacMode ? "triac" : "ssr";
  heater["boosterEnabled"] = heaterDiag.boosterEnabled;
  heater["closedLoopActive"] = heaterDiag.closedLoopActive;
  heater["zeroCrossSeen"] = heaterDiag.zeroCrossSeen;
  heater["zeroCrossCount"] = heaterDiag.zeroCrossCount;
  heater["triacFireCount"] = heaterDiag.triacFireCount;
  heater["triacDelayUs"] = heaterDiag.triacDelayUs;
  heater["minSubmergeLiters"] = g_settings.equipment.minHeaterSubmergeL;

  JsonObject valves = doc["valves"].to<JsonObject>();
  valves["water"] = Valves::getWater();
  valves["heads"] = Valves::getHeads();
  valves["uno"] = Valves::getUno();
  valves["startStopDuty"] = Valves::getStartStop();
  JsonObject coolingSettings = doc["coolingSettings"].to<JsonObject>();
  coolingSettings["enabled"] = g_settings.equipment.coolingPwmEnabled;
  coolingSettings["minDuty"] = g_settings.equipment.coolingPwmMinDuty;
  coolingSettings["maxDuty"] = g_settings.equipment.coolingPwmMaxDuty;
  coolingSettings["startupDuty"] = g_settings.equipment.coolingPwmStartupDuty;
  JsonObject waterPulse = valves["waterPulse"].to<JsonObject>();
  waterPulse["active"] = Valves::isPulseActive(Valves::ValveId::WATER);
  waterPulse["remainingMs"] =
      Valves::getPulseRemainingMs(Valves::ValveId::WATER);
  JsonObject headsPulse = valves["headsPulse"].to<JsonObject>();
  headsPulse["active"] = Valves::isPulseActive(Valves::ValveId::HEADS);
  headsPulse["remainingMs"] =
      Valves::getPulseRemainingMs(Valves::ValveId::HEADS);
  JsonObject unoPulse = valves["unoPulse"].to<JsonObject>();
  unoPulse["active"] = Valves::isPulseActive(Valves::ValveId::UNO);
  unoPulse["remainingMs"] =
      Valves::getPulseRemainingMs(Valves::ValveId::UNO);

  JsonObject servo = doc["servo"].to<JsonObject>();
  servo["enabled"] = g_settings.fractionator.enabled;
  servo["available"] = servoAvailable;
  servo["moving"] = Valves::isServoMoving();
  servo["fraction"] = getFractionToken(Valves::getCurrentFraction());
  servo["fractionLabel"] = getFractionLabel(Valves::getCurrentFraction());
  servo["angle"] = Valves::getFractionAngle();
  JsonArray presets = servo["presets"].to<JsonArray>();
  for (uint8_t i = 0; i < FRACTION_COUNT; ++i) {
    JsonObject preset = presets.add<JsonObject>();
    preset["index"] = i;
    preset["token"] = getFractionToken(static_cast<Fraction>(i));
    preset["label"] = getFractionLabel(static_cast<Fraction>(i));
    preset["enabled"] = g_settings.fractionator.positionsEnabled[i];
    preset["angle"] = g_settings.fractionator.angles[i];
  }

  JsonArray temps = doc["temperatures"].to<JsonArray>();
  for (uint8_t i = 0; i < TEMP_COUNT; ++i) {
    JsonObject temp = temps.add<JsonObject>();
    temp["index"] = i;
    temp["label"] = getTempSensorLabel(i);
    appendTempSensorMeta(temp, i);
    temp["installed"] = Safety::isTempSensorInstalled(g_settings.equipment, i);
    temp["valid"] = g_state.temps.valid[i];
    switch (i) {
    case TEMP_CUBE:
      temp["value"] = g_state.temps.cube;
      break;
    case TEMP_COLUMN_BOTTOM:
      temp["value"] = g_state.temps.columnBottom;
      break;
    case TEMP_COLUMN_TOP:
      temp["value"] = g_state.temps.columnTop;
      break;
    case TEMP_REFLUX:
      temp["value"] = g_state.temps.reflux;
      break;
    case TEMP_TSA:
      temp["value"] = g_state.temps.tsa;
      break;
    case TEMP_WATER_IN:
      temp["value"] = g_state.temps.waterIn;
      break;
    case TEMP_WATER_OUT:
      temp["value"] = g_state.temps.waterOut;
      break;
    default:
      temp["value"] = 0.0f;
      break;
    }
  }

  JsonObject temperatureTopology = doc["temperatureTopology"].to<JsonObject>();
  fillTemperatureTopologyJson(temperatureTopology, g_settings.equipment);
  JsonObject supportedModes = doc["supportedModes"].to<JsonObject>();
  fillTemperatureModeSupportJson(supportedModes, g_settings);

  JsonObject pressure = doc["pressure"].to<JsonObject>();
  pressure["cubeMmHg"] = g_state.pressure.cube;
  pressure["atmosphere"] = g_state.pressure.atmosphere;
  pressure["ok"] = g_state.pressure.ok;
  pressure["ads1115Available"] = g_state.health.ads1115Ok;
  pressure["sensorVoltage"] = g_state.pressure.sensorVoltage;
  pressure["sensorAdc"] = g_state.pressure.sensorAdc;
  pressure["source"] = "ADS1115 A1";
  pressure["lastUpdate"] = g_state.pressure.lastUpdate;

  JsonObject hydrometer = doc["hydrometer"].to<JsonObject>();
  hydrometer["pressure"] = g_state.hydrometer.pressure;
  hydrometer["density"] = g_state.hydrometer.density;
  hydrometer["abv"] = g_state.hydrometer.abv;
  hydrometer["temperature"] = g_state.hydrometer.temperature;
  hydrometer["valid"] = g_state.hydrometer.valid;
  hydrometer["ok"] = g_state.hydrometer.ok;
  hydrometer["lastUpdate"] = g_state.hydrometer.lastUpdate;

  JsonObject power = doc["power"].to<JsonObject>();
  power["available"] = g_state.health.pzemOk;
  power["voltage"] = g_state.power.voltage;
  power["current"] = g_state.power.current;
  power["power"] = g_state.power.power;
  power["energy"] = g_state.power.energy;
  power["frequency"] = g_state.power.frequency;
  power["powerFactor"] = g_state.power.powerFactor;

  JsonObject modules = doc["modules"].to<JsonObject>();
  fillEquipmentModulesJson(modules);

  JsonObject safetyChannels = doc["safetyChannels"].to<JsonObject>();
  fillSafetyChannelsJson(safetyChannels);

  JsonObject health = doc["health"].to<JsonObject>();
  fillEquipmentTestingHealthJson(health);

  JsonArray recentActions = doc["recentActions"].to<JsonArray>();
  for (uint8_t i = 0; i < g_equipmentTestingActionCount; ++i) {
    const int16_t rawIndex =
        static_cast<int16_t>(g_equipmentTestingActionNext) - 1 - i;
    const uint8_t index = static_cast<uint8_t>(
        (rawIndex + EQUIPMENT_TESTING_ACTION_CAPACITY) %
        EQUIPMENT_TESTING_ACTION_CAPACITY);
    const EquipmentTestingAction &entry = g_equipmentTestingActions[index];
    JsonObject item = recentActions.add<JsonObject>();
    item["timestampMs"] = entry.timestampMs;
    item["tone"] = entry.tone;
    item["title"] = entry.title;
    item["detail"] = entry.detail;
  }
}

static bool parseFractionPresetToken(const String &token, Fraction &fraction) {
  if (token == "heads") {
    fraction = Fraction::HEADS;
    return true;
  }
  if (token == "subheads") {
    fraction = Fraction::SUBHEADS;
    return true;
  }
  if (token == "body") {
    fraction = Fraction::BODY;
    return true;
  }
  if (token == "pretails") {
    fraction = Fraction::PRETAILS;
    return true;
  }
  if (token == "tails") {
    fraction = Fraction::TAILS;
    return true;
  }
  return false;
}

void registerTestingRoutes(AsyncWebServer &server) {
  server.on("/api/testing/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    fillEquipmentTestingStatus(doc);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/testing/stop-all", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              Pump::stop();
              g_state.stirrer.autoMode = false;
              Stirrer::stop();
              Heater::setPower(0);
              Valves::closeAll();
              if (Valves::isFractionatorEnabled()) {
                Valves::setFractionAngle(Valves::getFractionAngle());
              }
              recordEquipmentTestingAction(
                  "warning", "Остановить все тесты",
                  "Все ручные сервисные воздействия остановлены одной командой.");
              request->send(200, "application/json",
                            "{\"success\":true,\"message\":\"Все тесты остановлены\"}");
            });

  server.on(
      "/api/testing/pump", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Invalid JSON\"}");
          return;
        }

        const String action = doc["action"] | "";
        if (action != "start" && action != "stop") {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Invalid pump action\"}");
          return;
        }

        if (action == "start") {
          char reason[160] = "";
          if (isEquipmentTestingBlocked(reason, sizeof(reason))) {
            JsonDocument resp;
            resp["success"] = false;
            resp["message"] = reason;
            String json;
            serializeJson(resp, json);
            request->send(409, "application/json", json);
            return;
          }

          const float speed = doc["speedMlH"] | 0.0f;
          if (speed <= 0.0f) {
            request->send(
                400, "application/json",
                "{\"success\":false,\"message\":\"Speed must be greater than zero\"}");
            return;
          }

          Pump::start(speed);
          char detail[128];
          snprintf(detail, sizeof(detail),
                   "Насос запущен вручную со скоростью %.1f мл/ч.", speed);
          recordEquipmentTestingAction("success", "Тест насоса", detail);
        } else {
          Pump::stop();
          recordEquipmentTestingAction("warning", "Тест насоса",
                                       "Насос остановлен из сервисного экрана.");
        }

        JsonDocument resp;
        fillEquipmentTestingStatus(resp);
        String json;
        serializeJson(resp, json);
        request->send(200, "application/json", json);
      });

  server.on(
      "/api/testing/stirrer", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Invalid JSON\"}");
          return;
        }

        const String action = doc["action"] | "";
        if (action != "start" && action != "stop" && action != "set") {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Invalid stirrer action\"}");
          return;
        }

        if (action == "start" || action == "set") {
          char reason[160] = "";
          if (isEquipmentTestingBlocked(reason, sizeof(reason))) {
            JsonDocument resp;
            resp["success"] = false;
            resp["message"] = reason;
            String json;
            serializeJson(resp, json);
            request->send(409, "application/json", json);
            return;
          }

          if (!g_settings.stirrer.enabled) {
            request->send(
                409, "application/json",
                "{\"success\":false,\"message\":\"Stirrer is disabled in settings\"}");
            return;
          }

          if (!Stirrer::isAvailable()) {
            request->send(
                409, "application/json",
                "{\"success\":false,\"message\":\"Stirrer DAC is not available\"}");
            return;
          }
        }

        if (action == "start") {
          int speedPercent = g_settings.stirrer.defaultSpeedPercent;
          if (!doc["speedPercent"].isNull()) {
            speedPercent = doc["speedPercent"].as<int>();
          } else if (!doc["speed"].isNull()) {
            speedPercent = doc["speed"].as<int>();
          }
          speedPercent = clampU8Range(speedPercent, 1, 100);
          g_state.stirrer.autoMode = false;
          Stirrer::start(static_cast<uint8_t>(speedPercent));

          char detail[128];
          snprintf(detail, sizeof(detail),
                   "Мешалка запущена вручную на %d%%.", speedPercent);
          recordEquipmentTestingAction("success", "Тест мешалки", detail);
        } else if (action == "set") {
          if (!Stirrer::isRunning()) {
            request->send(
                409, "application/json",
                "{\"success\":false,\"message\":\"Stirrer is not running\"}");
            return;
          }

          int speedPercent = 0;
          if (!doc["speedPercent"].isNull()) {
            speedPercent = doc["speedPercent"].as<int>();
          } else if (!doc["speed"].isNull()) {
            speedPercent = doc["speed"].as<int>();
          }
          if (speedPercent < 1 || speedPercent > 100) {
            request->send(
                400, "application/json",
                "{\"success\":false,\"message\":\"Speed must be between 1 and 100\"}");
            return;
          }

          g_state.stirrer.autoMode = false;
          Stirrer::setSpeed(static_cast<uint8_t>(speedPercent));

          char detail[128];
          snprintf(detail, sizeof(detail),
                   "Скорость мешалки изменена до %d%%.", speedPercent);
          recordEquipmentTestingAction("info", "Тест мешалки", detail);
        } else {
          g_state.stirrer.autoMode = false;
          Stirrer::stop();
          recordEquipmentTestingAction(
              "warning", "Тест мешалки",
              "Мешалка остановлена из сервисного экрана.");
        }

        JsonDocument resp;
        fillEquipmentTestingStatus(resp);
        String json;
        serializeJson(resp, json);
        request->send(200, "application/json", json);
      });

  server.on(
      "/api/testing/heater", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Invalid JSON\"}");
          return;
        }

        const String action = doc["action"] | "";
        if (action != "start" && action != "stop") {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Invalid heater action\"}");
          return;
        }

        if (action == "start") {
          char reason[160] = "";
          if (isEquipmentTestingBlocked(reason, sizeof(reason))) {
            JsonDocument resp;
            resp["success"] = false;
            resp["message"] = reason;
            String json;
            serializeJson(resp, json);
            request->send(409, "application/json", json);
            return;
          }

          if (!(doc["confirmed"] | false)) {
            request->send(
                400, "application/json",
                "{\"success\":false,\"message\":\"Heater start requires confirmation\"}");
            return;
          }

          const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
                                          ? g_settings.equipment.heaterPowerW
                                          : DEFAULT_HEATER_POWER_W;
          uint16_t powerWatts = 0;
          if (!doc["powerW"].isNull()) {
            powerWatts =
                clampU16Range(doc["powerW"].as<uint32_t>(), 1, heaterMaxW);
          } else {
            int powerPercent = doc["powerPercent"] | 0;
            if (powerPercent < 1) powerPercent = 1;
            if (powerPercent > 100) powerPercent = 100;
            powerWatts = static_cast<uint16_t>(
                (static_cast<uint32_t>(heaterMaxW) * powerPercent) / 100U);
          }
          Heater::setPowerWatts(powerWatts);
          const unsigned power = powerWatts;
          char detail[128];
          snprintf(detail, sizeof(detail),
                   "ТЭН включен вручную на %d%% мощности после подтверждения.",
                   power);
          recordEquipmentTestingAction("danger", "Тест ТЭНа", detail);
        } else {
          Heater::setPower(0);
          recordEquipmentTestingAction("warning", "Тест ТЭНа",
                                       "ТЭН выключен из сервисного экрана.");
        }

        JsonDocument resp;
        fillEquipmentTestingStatus(resp);
        String json;
        serializeJson(resp, json);
        request->send(200, "application/json", json);
      });

  server.on(
      "/api/testing/valves", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Invalid JSON\"}");
          return;
        }

        char reason[160] = "";
        if (isEquipmentTestingBlocked(reason, sizeof(reason))) {
          JsonDocument resp;
          resp["success"] = false;
          resp["message"] = reason;
          String json;
          serializeJson(resp, json);
          request->send(409, "application/json", json);
          return;
        }

        const String target = doc["target"] | "";
        const String action = doc["action"] | "";
        const bool open = doc["open"] | false;
        uint32_t durationMs = doc["durationMs"] | 0;
        const uint8_t duty = clampU8Range(doc["duty"] | 0, 0, 255);

        if (target == "all") {
          Valves::closeAll();
          recordEquipmentTestingAction("warning", "Клапаны",
                                       "Все клапаны закрыты одной сервисной командой.");
        } else if (target == "water") {
          if (action == "pulse") {
            if (durationMs < 100) durationMs = 100;
            if (durationMs > 10000) durationMs = 10000;
            Valves::pulse(Valves::ValveId::WATER, durationMs);
            char detail[128];
            snprintf(detail, sizeof(detail), "Клапан воды дал импульс %lu мс.",
                     (unsigned long)durationMs);
            recordEquipmentTestingAction("info", "Клапан воды", detail);
          } else {
            Valves::setWater(open);
            recordEquipmentTestingAction("info", "Клапан воды",
                                         open ? "Клапан воды открыт вручную."
                                              : "Клапан воды закрыт вручную.");
          }
        } else if (target == "heads") {
          if (action == "pulse") {
            if (durationMs < 100) durationMs = 100;
            if (durationMs > 10000) durationMs = 10000;
            Valves::pulse(Valves::ValveId::HEADS, durationMs);
            char detail[128];
            snprintf(detail, sizeof(detail), "Клапан голов дал импульс %lu мс.",
                     (unsigned long)durationMs);
            recordEquipmentTestingAction("info", "Клапан голов", detail);
          } else {
            Valves::setHeads(open);
            recordEquipmentTestingAction("info", "Клапан голов",
                                         open ? "Клапан голов открыт вручную."
                                              : "Клапан голов закрыт вручную.");
          }
        } else if (target == "uno") {
          if (action == "pulse") {
            if (durationMs < 100) durationMs = 100;
            if (durationMs > 10000) durationMs = 10000;
            Valves::pulse(Valves::ValveId::UNO, durationMs);
            char detail[128];
            snprintf(detail, sizeof(detail), "УНО дало импульс %lu мс.",
                     (unsigned long)durationMs);
            recordEquipmentTestingAction("info", "УНО", detail);
          } else {
            Valves::setUno(open);
            recordEquipmentTestingAction("info", "УНО",
                                         open ? "УНО открыто вручную."
                                              : "УНО закрыто вручную.");
          }
        } else if (target == "startStop") {
          Valves::setStartStop(duty);
          char detail[160];
          snprintf(detail, sizeof(detail),
                   "PWM-канал охлаждения установлен на %u/255 (%u%%).",
                   (unsigned)duty,
                   (unsigned)((static_cast<uint32_t>(duty) * 100U) / 255U));
          recordEquipmentTestingAction(duty > 0 ? "info" : "warning",
                                       "PWM охлаждения", detail);
        } else {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Unknown valve target\"}");
          return;
        }

        JsonDocument resp;
        fillEquipmentTestingStatus(resp);
        String json;
        serializeJson(resp, json);
        request->send(200, "application/json", json);
      });

  server.on(
      "/api/testing/servo", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Invalid JSON\"}");
          return;
        }

        const String action = doc["action"] | "";
        if (action.isEmpty()) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Missing servo action\"}");
          return;
        }

        if (action == "saveConfig") {
          JsonArray angles = doc["angles"].as<JsonArray>();
          JsonArray enabled = doc["enabled"].as<JsonArray>();
          if (angles.isNull() || enabled.isNull() ||
              angles.size() != FRACTION_COUNT ||
              enabled.size() != FRACTION_COUNT) {
            request->send(
                400, "application/json",
                "{\"success\":false,\"message\":\"Invalid servo config payload\"}");
            return;
          }

          for (uint8_t i = 0; i < FRACTION_COUNT; ++i) {
            int angle = angles[i].as<int>();
            if (angle < 0) angle = 0;
            if (angle > 180) angle = 180;
            g_settings.fractionator.angles[i] = static_cast<uint16_t>(angle);
            g_settings.fractionator.positionsEnabled[i] = enabled[i].as<bool>();
          }
          NVSManager::saveSettings(g_settings);
          recordEquipmentTestingAction(
              "success", "Сервопривод",
              "Сервисные пресеты сервопривода сохранены в настройках.");

          JsonDocument resp;
          fillEquipmentTestingStatus(resp);
          String json;
          serializeJson(resp, json);
          request->send(200, "application/json", json);
          return;
        }

        char reason[160] = "";
        if (isEquipmentTestingBlocked(reason, sizeof(reason))) {
          JsonDocument resp;
          resp["success"] = false;
          resp["message"] = reason;
          String json;
          serializeJson(resp, json);
          request->send(409, "application/json", json);
          return;
        }

        if (!g_settings.fractionator.enabled || !Valves::isFractionatorEnabled()) {
          request->send(
              409, "application/json",
              "{\"success\":false,\"message\":\"Fractionator is not enabled in configuration\"}");
          return;
        }

        if (action == "preset") {
          Fraction fraction = Fraction::UNKNOWN;
          if (!parseFractionPresetToken(doc["preset"] | "", fraction) ||
              fraction == Fraction::UNKNOWN) {
            request->send(
                400, "application/json",
                "{\"success\":false,\"message\":\"Unknown servo preset\"}");
            return;
          }
          Valves::setFraction(fraction, true);
          char detail[128];
          snprintf(detail, sizeof(detail), "Сервопривод переведён в позицию %s.",
                   getFractionLabel(fraction));
          recordEquipmentTestingAction("success", "Сервопривод", detail);
        } else if (action == "angle") {
          int angle = doc["angle"] | 0;
          if (angle < 0) angle = 0;
          if (angle > 180) angle = 180;
          Valves::setFractionAngle(static_cast<uint8_t>(angle));
          char detail[128];
          snprintf(detail, sizeof(detail),
                   "Сервопривод переведён в ручной угол %d°.", angle);
          recordEquipmentTestingAction("success", "Сервопривод", detail);
        } else {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Unknown servo action\"}");
          return;
        }

        JsonDocument resp;
        fillEquipmentTestingStatus(resp);
        String json;
        serializeJson(resp, json);
        request->send(200, "application/json", json);
      });
}
