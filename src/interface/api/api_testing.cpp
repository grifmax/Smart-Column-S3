#include "api_routes.h"

#include <string.h>

#include "config.h"
#include "control/control_command_queue.h"
#include "control/equipment_testing_logic.h"
#include "control/power_command_gateway.h"
#include "control/safety.h"
#include "control/v2/reason_codes.h"
#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/sensors.h"
#include "drivers/stirrer.h"
#include "drivers/valves.h"
#include "history.h"
#include "control/state_snapshot.h"
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

static void fillEquipmentTestingStatus(JsonDocument &doc);

static void sendTestingStatusWithOutputCommands(
    AsyncWebServerRequest *request,
    const PowerCommandGateway::Command *commands, size_t count,
    const ControlCommandQueue::EnqueueResult *controlResult = nullptr,
    uint32_t controlCommandId = 0,
    ControlCommandQueue::Status controlStatus =
        ControlCommandQueue::Status::Unknown) {
  JsonDocument resp;
  fillEquipmentTestingStatus(resp);
  const bool hasControlCommand = controlResult != nullptr;
  const bool controlAccepted = !hasControlCommand ||
      *controlResult == ControlCommandQueue::EnqueueResult::Accepted ||
      *controlResult == ControlCommandQueue::EnqueueResult::Duplicate;
  const bool controlApplied = !hasControlCommand ||
      controlStatus == ControlCommandQueue::Status::Applied;
  const bool controlQueued = hasControlCommand &&
      (controlStatus == ControlCommandQueue::Status::Queued ||
       controlStatus == ControlCommandQueue::Status::Processing);
  bool accepted = controlAccepted && (count > 0 || hasControlCommand);
  bool applied = controlApplied && (count > 0 || hasControlCommand);
  bool queued = controlQueued;
  JsonArray commandList = resp["commands"].to<JsonArray>();
  if (hasControlCommand) {
    JsonObject item = commandList.add<JsonObject>();
    const bool commandAccepted = controlAccepted;
    item["commandType"] = "control";
    item["commandId"] = controlCommandId;
    item["accepted"] = commandAccepted;
    item["queued"] = controlQueued;
    item["applied"] = controlApplied;
    item["success"] = commandAccepted && controlApplied;
    item["status"] = ControlCommandQueue::statusToken(controlStatus);
  }
  for (size_t i = 0; i < count; ++i) {
    const auto &command = commands[i];
    accepted = accepted && command.accepted;
    applied = applied && command.applied;
    queued = queued || command.queued;
    JsonObject item = commandList.add<JsonObject>();
    item["commandType"] = "output";
    item["commandId"] = command.sequence;
    item["accepted"] = command.accepted;
    item["queued"] = command.queued;
    item["applied"] = command.applied;
    item["success"] = command.accepted && command.applied;
    item["status"] = !command.accepted ? "rejected"
                      : command.applied ? "applied" : "queued";
    item["target"] = static_cast<uint8_t>(command.target);
    item["rejectReason"] = PowerCommandGateway::rejectReasonString(
        command.rejectReason);
  }
  resp["accepted"] = accepted;
  resp["queued"] = queued;
  resp["applied"] = applied;
  resp["success"] = accepted && applied;
  resp["status"] = !accepted ? "rejected" : applied ? "applied" : "queued";
  if (count == 1) {
    resp["commandId"] = commands[0].sequence;
    resp["rejectReason"] = PowerCommandGateway::rejectReasonString(
        commands[0].rejectReason);
  }
  const int httpStatus = !accepted
                             ? hasControlCommand &&
                                       *controlResult == ControlCommandQueue::EnqueueResult::Full
                                   ? 429
                                   : hasControlCommand &&
                                             *controlResult == ControlCommandQueue::EnqueueResult::NotInitialized
                                         ? 503
                                         : 409
                         : queued ? 202 : 200;
  String json;
  serializeJson(resp, json);
  request->send(httpStatus, "application/json", json);
}

static void sendTestingStatusWithOutputCommand(
    AsyncWebServerRequest *request,
    const PowerCommandGateway::Command &command,
    const ControlCommandQueue::EnqueueResult *controlResult = nullptr,
    uint32_t controlCommandId = 0,
    ControlCommandQueue::Status controlStatus =
        ControlCommandQueue::Status::Unknown) {
  sendTestingStatusWithOutputCommands(request, &command, 1, controlResult,
                                      controlCommandId, controlStatus);
}

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

static bool isEquipmentTestingBlocked(const SystemState &state, char *reason,
                                     size_t reasonSize) {
  const EquipmentTestingLogic::BlockReason blockReason =
      EquipmentTestingLogic::evaluateGuard(
          state.mode != Mode::IDLE, Safety::isLatched(state),
          state.currentAlarm.type != AlarmType::NONE, state.safetyOk,
          g_settings.demoMode);
  switch (blockReason) {
    case EquipmentTestingLogic::BlockReason::PROCESS_ACTIVE:
      snprintf(reason, reasonSize,
               "Тестирование доступно только в режиме простоя");
      break;
    case EquipmentTestingLogic::BlockReason::SAFETY_LATCHED:
      snprintf(reason, reasonSize,
               "Сначала снимите защелкнутую аварию безопасности");
      break;
    case EquipmentTestingLogic::BlockReason::ALARM_ACTIVE:
      snprintf(reason, reasonSize, "Активна авария: %s",
               state.currentAlarm.message);
      break;
    case EquipmentTestingLogic::BlockReason::SAFETY_NOT_OK:
      snprintf(reason, reasonSize,
               "Система находится в небезопасном состоянии");
      break;
    case EquipmentTestingLogic::BlockReason::DEMO_MODE:
      snprintf(reason, reasonSize,
               "Физические тесты недоступны в demo mode");
      break;
    case EquipmentTestingLogic::BlockReason::NONE:
    default:
      reason[0] = '\0';
      return false;
  }
  return true;
}

static bool isEquipmentTestingBlocked(char *reason, size_t reasonSize) {
  SystemState state;
  if (!StateSnapshot::read(state)) {
    snprintf(reason, reasonSize, "Снимок состояния недоступен");
    return true;
  }
  return isEquipmentTestingBlocked(state, reason, reasonSize);
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

void fillSafetyChannelsJson(JsonObject channels, const SystemState &state) {
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

  const int16_t leakAdc = state.leak.adc;
  const float leakVoltage = state.leak.voltage;
  const bool leakReadable = state.leak.valid;
  const bool leakTriggered = state.leak.triggered;

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
  vaporPrimary["status"] = VAPOR_SENSOR_ENABLED
      ? (state.vaporPrimary.valid
             ? (state.vaporPrimary.triggered ? "triggered" : "armed")
             : "no_signal")
      : "reserved";
  vaporPrimary["available"] = VAPOR_SENSOR_ENABLED != 0;
  vaporPrimary["expected"] = false;
  vaporPrimary["planned"] = true;
  vaporPrimary["liveReadable"] = state.vaporPrimary.valid;
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
  vaporSecondary["status"] = VAPOR_SENSOR_ENABLED
      ? (state.vaporSecondary.valid
             ? (state.vaporSecondary.triggered ? "triggered" : "armed")
             : "no_signal")
      : "reserved";
  vaporSecondary["available"] = VAPOR_SENSOR_ENABLED != 0;
  vaporSecondary["expected"] = false;
  vaporSecondary["planned"] = true;
  vaporSecondary["liveReadable"] = state.vaporSecondary.valid;
  vaporSecondary["bus"] = "ESP32 ADC";
  vaporSecondary["address"] = "GPIO3";
  vaporSecondary["role"] =
      "Резервный ADC-канал для второго датчика газа или паров";
  vaporSecondary["source"] = "GPIO3";
  vaporSecondary["pin"] = PIN_VAPOR_SENSOR_ADC_2;
  vaporSecondary["enabled"] = false;
  vaporSecondary["triggered"] = false;
}

static void fillEquipmentTestingHealthJson(JsonObject health,
                                           const SystemState &state) {
  const SystemHealth &status = state.health;
  health["snapshotAvailable"] = true;
  health["overall"] = status.overallHealth;
  health["tempSensorsOk"] = status.tempSensorsOk;
  health["tempSensorsTotal"] = status.tempSensorsTotal;
  health["bmp280"] = status.bmp280Ok;
  health["ads1115"] = status.ads1115Ok;
  health["pzem"] = status.pzemOk;
  health["wifiConnected"] = status.wifiConnected;
  health["wifiRSSI"] = status.wifiRSSI;
  health["freeHeap"] = status.freeHeap;
  health["cpuTemp"] = status.cpuTemp;
  health["pzemSpikes"] = status.pzemSpikeCount;
  health["tempErrors"] = status.tempReadErrors;
  health["lastUpdate"] = status.lastUpdate;
  health["rebootReason"] = g_rebootTracker.lastReason;
  health["rebootReasonStr"] = g_rebootTracker.lastReasonStr;
}

static void fillEquipmentTestingStatus(JsonDocument &doc) {
  // Снимок состояния: главный цикл пишет g_state на другом ядре.
  SystemState snapshot;
  if (!StateSnapshot::read(snapshot)) {
    doc["snapshotAvailable"] = false;
    return;
  }
  doc["snapshotAvailable"] = true;
  const SystemState &st = snapshot;

  char reason[160] = "";
  const bool blocked = isEquipmentTestingBlocked(st, reason, sizeof(reason));
  const bool processActive = st.mode != Mode::IDLE;
  const bool latched = Safety::isLatched(st);
  const bool alarmActive = (st.currentAlarm.type != AlarmType::NONE);
  const Heater::Diagnostics heaterDiag = Heater::getDiagnostics();
  const bool heaterActive = heaterDiag.active;
  const bool servoAvailable =
      g_settings.fractionator.enabled && Valves::isFractionatorEnabled();
  const Pump::Diagnostics pumpDiag = Pump::getDiagnostics();

  doc["success"] = true;
  doc["mode"] = getModeString(st.mode);
  doc["processActive"] = processActive;
  doc["testingAllowed"] = !blocked;
  doc["availabilityReason"] = blocked ? reason : "";
  doc["demoMode"] = g_settings.demoMode;
  doc["physicalActuationAllowed"] = !blocked && !g_settings.demoMode;
  doc["alarmActive"] = alarmActive;
  doc["alarmMessage"] = alarmActive ? st.currentAlarm.message : "";
  doc["safetyOk"] = st.safetyOk;
  doc["safetyLatched"] = latched;

  JsonObject activeTests = doc["activeTests"].to<JsonObject>();
  activeTests["pump"] = Pump::isRunning();
  activeTests["stirrer"] = st.stirrer.running;
  activeTests["heater"] = heaterActive;
  activeTests["waterValve"] = Valves::getWater();
  activeTests["headsValve"] = Valves::getHeads();
  activeTests["bodyValve"] = Valves::getBody();
  activeTests["tailsValve"] = Valves::getTails();
  activeTests["unoValve"] = Valves::getUno();
  activeTests["startStopDuty"] = Valves::getStartStop() > 0;
  activeTests["servoMoving"] = Valves::isServoMoving();

  JsonObject pump = doc["pump"].to<JsonObject>();
  pump["running"] = Pump::isRunning();
  pump["speedMlH"] = Pump::getSpeed();
  pump["targetSpeedMlH"] = pumpDiag.speedMlH;
  pump["appliedSpeedMlH"] = pumpDiag.appliedSpeedMlH;
  pump["requestedStepHz"] = pumpDiag.requestedStepHz;
  pump["outputStepHz"] = pumpDiag.outputStepHz;
  pump["stepsPerRevolution"] = pumpDiag.stepsPerRevolution;
  pump["stepOutputOk"] = pumpDiag.stepOutputOk;
  pump["totalVolumeMl"] = Pump::getTotalVolume();
  pump["totalSteps"] = pumpDiag.totalSteps;
  pump["taskAlive"] = pumpDiag.taskAlive;
  pump["mutexReady"] = pumpDiag.mutexReady;
  pump["lockTimeoutCount"] = pumpDiag.lockTimeoutCount;
  pump["taskLoopCount"] = pumpDiag.taskLoopCount;
  pump["cooperativeSleepCount"] = pumpDiag.cooperativeSleepCount;
  pump["fastYieldCount"] = pumpDiag.fastYieldCount;

  JsonObject stirrer = doc["stirrer"].to<JsonObject>();
  fillStirrerJson(stirrer, st);
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
  valves["body"] = Valves::getBody();
  valves["bodyAvailable"] = Valves::hasBodyValve();
  valves["tailsAvailable"] = Valves::hasTailsValve();
  valves["tails"] = Valves::getTails();
  valves["uno"] = Valves::getUno();
  valves["startStopDuty"] = Valves::getStartStop();
  JsonObject coolingSettings = doc["coolingSettings"].to<JsonObject>();
  coolingSettings["enabled"] = g_settings.equipment.coolingPwmEnabled;
  coolingSettings["holdPercent"] =
      g_settings.equipment.coolingPwmHoldPercent;
  coolingSettings["pullMs"] = g_settings.equipment.coolingPwmPullMs;
  JsonObject waterPulse = valves["waterPulse"].to<JsonObject>();
  waterPulse["active"] = Valves::isPulseActive(Valves::ValveId::WATER);
  waterPulse["remainingMs"] =
      Valves::getPulseRemainingMs(Valves::ValveId::WATER);
  JsonObject headsPulse = valves["headsPulse"].to<JsonObject>();
  headsPulse["active"] = Valves::isPulseActive(Valves::ValveId::HEADS);
  headsPulse["remainingMs"] =
      Valves::getPulseRemainingMs(Valves::ValveId::HEADS);
  JsonObject bodyPulse = valves["bodyPulse"].to<JsonObject>();
  bodyPulse["active"] = Valves::isPulseActive(Valves::ValveId::BODY);
  bodyPulse["remainingMs"] =
      Valves::getPulseRemainingMs(Valves::ValveId::BODY);
  JsonObject tailsPulse = valves["tailsPulse"].to<JsonObject>();
  tailsPulse["active"] = Valves::isPulseActive(Valves::ValveId::TAILS);
  tailsPulse["remainingMs"] =
      Valves::getPulseRemainingMs(Valves::ValveId::TAILS);
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
    temp["valid"] = st.temps.valid[i];
    switch (i) {
    case TEMP_CUBE:
      temp["value"] = st.temps.cube;
      break;
    case TEMP_COLUMN_BOTTOM:
      temp["value"] = st.temps.columnBottom;
      break;
    case TEMP_COLUMN_TOP:
      temp["value"] = st.temps.columnTop;
      break;
    case TEMP_REFLUX:
      temp["value"] = st.temps.reflux;
      break;
    case TEMP_TSA:
      temp["value"] = st.temps.tsa;
      break;
    case TEMP_WATER_IN:
      temp["value"] = st.temps.waterIn;
      break;
    case TEMP_WATER_OUT:
      temp["value"] = st.temps.waterOut;
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
  pressure["cubeMmHg"] = st.pressure.cube;
  pressure["atmosphere"] = st.pressure.atmosphere;
  pressure["ok"] = st.pressure.ok;
  pressure["ads1115Available"] = st.health.ads1115Ok;
  pressure["sensorVoltage"] = st.pressure.sensorVoltage;
  pressure["sensorAdc"] = st.pressure.sensorAdc;
  pressure["source"] = "ADS1115 A1";
  pressure["lastUpdate"] = st.pressure.lastUpdate;

  JsonObject hydrometer = doc["hydrometer"].to<JsonObject>();
  hydrometer["pressure"] = st.hydrometer.pressure;
  hydrometer["density"] = st.hydrometer.density;
  hydrometer["abv"] = st.hydrometer.abv;
  hydrometer["temperature"] = st.hydrometer.temperature;
  hydrometer["valid"] = st.hydrometer.valid;
  hydrometer["ok"] = st.hydrometer.ok;
  hydrometer["lastUpdate"] = st.hydrometer.lastUpdate;

  JsonObject power = doc["power"].to<JsonObject>();
  power["available"] = st.health.pzemOk;
  power["voltage"] = st.power.voltage;
  power["current"] = st.power.current;
  power["power"] = st.power.power;
  power["energy"] = st.power.energy;
  power["frequency"] = st.power.frequency;
  power["powerFactor"] = st.power.powerFactor;

  JsonObject modules = doc["modules"].to<JsonObject>();
  fillEquipmentModulesJson(modules);

  JsonObject safetyChannels = doc["safetyChannels"].to<JsonObject>();
  fillSafetyChannelsJson(safetyChannels, st);

  JsonObject health = doc["health"].to<JsonObject>();
  fillEquipmentTestingHealthJson(health, st);

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

void registerTestingApiRoutes(AsyncWebServer &server) {
  server.on("/api/testing/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    fillEquipmentTestingStatus(doc);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/testing/stop-all", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              uint32_t controlCommandId = 0;
              ControlCommandQueue::Status controlStatus =
                  ControlCommandQueue::Status::Unknown;
              const auto controlResult = ControlCommandQueue::enqueueAction(
                  ControlCommandQueue::Type::SetStirrerAutoMode,
                  ControlCommandQueue::Source::Api, 0, 0.0f, 0,
                  ControlCommandQueue::Reason::OperatorRequest,
                  &controlCommandId, &controlStatus);
              const PowerCommandGateway::Command outputCommand =
                  PowerCommandGateway::safeStopAll(
                  PowerCommandGateway::Owner::SERVICE,
                  PowerCommandGateway::Reason::TFT_SERVICE_TEST);
              recordEquipmentTestingAction(
                  "warning", "Остановить все тесты",
                  "Все ручные сервисные воздействия остановлены одной командой.");
              sendTestingStatusWithOutputCommand(request, outputCommand,
                                                 &controlResult,
                                                 controlCommandId,
                                                 controlStatus);
              return;
            });

  server.on(
      "/api/testing/pump", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"message\":\"Invalid JSON\"}")) {
          return;
        }

        const String action = doc["action"] | "";
        if (action != "start" && action != "stop") {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Invalid pump action\"}");
          return;
        }

        PowerCommandGateway::Command outputCommand;
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

          outputCommand = PowerCommandGateway::setPumpSpeed(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST, speed);
          char detail[128];
          snprintf(detail, sizeof(detail),
                   "Насос запущен вручную со скоростью %.1f мл/ч.", speed);
          recordEquipmentTestingAction("success", "Тест насоса", detail);
        } else {
          outputCommand = PowerCommandGateway::setPumpSpeed(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST, 0.0f);
          recordEquipmentTestingAction("warning", "Тест насоса",
                                       "Насос остановлен из сервисного экрана.");
        }

        sendTestingStatusWithOutputCommand(request, outputCommand);
      });

  server.on(
      "/api/testing/stirrer", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"message\":\"Invalid JSON\"}")) {
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

        PowerCommandGateway::Command outputCommand;
        if (action == "start") {
          int speedPercent = g_settings.stirrer.defaultSpeedPercent;
          if (!doc["speedPercent"].isNull()) {
            speedPercent = doc["speedPercent"].as<int>();
          } else if (!doc["speed"].isNull()) {
            speedPercent = doc["speed"].as<int>();
          }
          speedPercent = clampU8Range(speedPercent, 1, 100);
          uint32_t controlCommandId = 0;
          ControlCommandQueue::Status controlStatus =
              ControlCommandQueue::Status::Unknown;
          const auto controlResult = ControlCommandQueue::enqueueAction(
              ControlCommandQueue::Type::SetStirrerAutoMode,
              ControlCommandQueue::Source::Api, 0, 0.0f, 0,
              ControlCommandQueue::Reason::OperatorRequest,
              &controlCommandId, &controlStatus);
          if (controlResult != ControlCommandQueue::EnqueueResult::Accepted &&
              controlResult != ControlCommandQueue::EnqueueResult::Duplicate) {
            sendTestingStatusWithOutputCommands(request, nullptr, 0,
                                                &controlResult,
                                                controlCommandId,
                                                controlStatus);
            return;
          }
          outputCommand = PowerCommandGateway::setStirrerSpeed(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST,
              static_cast<uint8_t>(speedPercent));

          char detail[128];
          snprintf(detail, sizeof(detail),
                   "Мешалка запущена вручную на %d%%.", speedPercent);
          recordEquipmentTestingAction("success", "Тест мешалки", detail);
          sendTestingStatusWithOutputCommand(request, outputCommand,
                                             &controlResult,
                                             controlCommandId,
                                             controlStatus);
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

          uint32_t controlCommandId = 0;
          ControlCommandQueue::Status controlStatus =
              ControlCommandQueue::Status::Unknown;
          const auto controlResult = ControlCommandQueue::enqueueAction(
              ControlCommandQueue::Type::SetStirrerAutoMode,
              ControlCommandQueue::Source::Api, 0, 0.0f, 0,
              ControlCommandQueue::Reason::OperatorRequest,
              &controlCommandId, &controlStatus);
          if (controlResult != ControlCommandQueue::EnqueueResult::Accepted &&
              controlResult != ControlCommandQueue::EnqueueResult::Duplicate) {
            sendTestingStatusWithOutputCommands(request, nullptr, 0,
                                                &controlResult,
                                                controlCommandId,
                                                controlStatus);
            return;
          }
          outputCommand = PowerCommandGateway::setStirrerSpeed(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST,
              static_cast<uint8_t>(speedPercent));

          char detail[128];
          snprintf(detail, sizeof(detail),
                   "Скорость мешалки изменена до %d%%.", speedPercent);
          recordEquipmentTestingAction("info", "Тест мешалки", detail);
          sendTestingStatusWithOutputCommand(request, outputCommand,
                                             &controlResult,
                                             controlCommandId,
                                             controlStatus);
        } else {
          uint32_t controlCommandId = 0;
          ControlCommandQueue::Status controlStatus =
              ControlCommandQueue::Status::Unknown;
          const auto controlResult = ControlCommandQueue::enqueueAction(
              ControlCommandQueue::Type::SetStirrerAutoMode,
              ControlCommandQueue::Source::Api, 0, 0.0f, 0,
              ControlCommandQueue::Reason::OperatorRequest,
              &controlCommandId, &controlStatus);
          outputCommand = PowerCommandGateway::stopStirrer(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST);
          recordEquipmentTestingAction(
              "warning", "Тест мешалки",
              "Мешалка остановлена из сервисного экрана.");
          sendTestingStatusWithOutputCommand(request, outputCommand,
                                             &controlResult,
                                             controlCommandId,
                                             controlStatus);
        }
      });

  server.on(
      "/api/testing/heater", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"message\":\"Invalid JSON\"}")) {
          return;
        }

        const String action = doc["action"] | "";
        if (action != "start" && action != "stop") {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Invalid heater action\"}");
          return;
        }

        PowerCommandGateway::Command outputCommand;
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
          outputCommand = PowerCommandGateway::setHeaterPowerWatts(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST, powerWatts);
          const unsigned power = powerWatts;
          char detail[128];
          snprintf(detail, sizeof(detail),
                   "ТЭН включен вручную на %u Вт после подтверждения.",
                   power);
          recordEquipmentTestingAction("danger", "Тест ТЭНа", detail);
        } else {
          outputCommand = PowerCommandGateway::setHeaterPowerWatts(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST, 0);
          recordEquipmentTestingAction("warning", "Тест ТЭНа",
                                       "ТЭН выключен из сервисного экрана.");
        }

        sendTestingStatusWithOutputCommand(request, outputCommand);
      });

  server.on(
      "/api/testing/valves", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"message\":\"Invalid JSON\"}")) {
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
        PowerCommandGateway::Command outputCommands[6];
        size_t outputCommandCount = 0;

        if (target == "all") {
          outputCommands[outputCommandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST,
              PowerCommandGateway::Valve::WATER, false);
          outputCommands[outputCommandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST,
              PowerCommandGateway::Valve::HEADS, false);
          if (Valves::hasBodyValve()) {
            outputCommands[outputCommandCount++] = PowerCommandGateway::setValve(
                PowerCommandGateway::Owner::SERVICE,
                PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                PowerCommandGateway::Valve::BODY, false);
          }
          if (Valves::hasTailsValve()) {
            outputCommands[outputCommandCount++] = PowerCommandGateway::setValve(
                PowerCommandGateway::Owner::SERVICE,
                PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                PowerCommandGateway::Valve::TAILS, false);
          }
          outputCommands[outputCommandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST,
              PowerCommandGateway::Valve::UNO, false);
          outputCommands[outputCommandCount++] = PowerCommandGateway::setCoolingPwm(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST, 0);
          recordEquipmentTestingAction("warning", "Клапаны",
                                       "Все клапаны закрыты одной сервисной командой.");
        } else if (target == "water") {
          if (action == "pulse") {
            if (durationMs < 100) durationMs = 100;
            if (durationMs > 10000) durationMs = 10000;
            outputCommands[outputCommandCount++] = PowerCommandGateway::pulseValve(
                PowerCommandGateway::Owner::SERVICE,
                PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                PowerCommandGateway::Valve::WATER, durationMs);
            char detail[128];
            snprintf(detail, sizeof(detail), "Клапан воды дал импульс %lu мс.",
                     (unsigned long)durationMs);
            recordEquipmentTestingAction("info", "Клапан воды", detail);
          } else {
            outputCommands[outputCommandCount++] = PowerCommandGateway::setValve(
                PowerCommandGateway::Owner::SERVICE,
                PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                PowerCommandGateway::Valve::WATER, open);
            recordEquipmentTestingAction("info", "Клапан воды",
                                         open ? "Клапан воды открыт вручную."
                                              : "Клапан воды закрыт вручную.");
          }
        } else if (target == "heads") {
          if (action == "pulse") {
            if (durationMs < 100) durationMs = 100;
            if (durationMs > 10000) durationMs = 10000;
            outputCommands[outputCommandCount++] = PowerCommandGateway::pulseValve(
                PowerCommandGateway::Owner::SERVICE,
                PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                PowerCommandGateway::Valve::HEADS, durationMs);
            char detail[128];
            snprintf(detail, sizeof(detail), "Клапан голов дал импульс %lu мс.",
                     (unsigned long)durationMs);
            recordEquipmentTestingAction("info", "Клапан голов", detail);
          } else {
            outputCommands[outputCommandCount++] = PowerCommandGateway::setValve(
                PowerCommandGateway::Owner::SERVICE,
                PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                PowerCommandGateway::Valve::HEADS, open);
            recordEquipmentTestingAction("info", "Клапан голов",
                                         open ? "Клапан голов открыт вручную."
                                              : "Клапан голов закрыт вручную.");
          }
        } else if (target == "body") {
          if (!Valves::hasBodyValve()) {
            request->send(
                409, "application/json",
                "{\"success\":false,\"message\":\"Body valve is not available on this board\"}");
            return;
          }
          if (action == "pulse") {
            if (durationMs < 100) durationMs = 100;
            if (durationMs > 10000) durationMs = 10000;
            outputCommands[outputCommandCount++] = PowerCommandGateway::pulseValve(
                PowerCommandGateway::Owner::SERVICE,
                PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                PowerCommandGateway::Valve::BODY, durationMs);
            recordEquipmentTestingAction("info", "РљР»Р°РїР°РЅ С‚РµР»Р°",
                                         "РљР»Р°РїР°РЅ С‚РµР»Р° РїРѕР»СѓС‡РёР» СЃРµСЂРІРёСЃРЅС‹Р№ РёРјРїСѓР»СЊСЃ.");
          } else {
            outputCommands[outputCommandCount++] = PowerCommandGateway::setValve(
                PowerCommandGateway::Owner::SERVICE,
                PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                PowerCommandGateway::Valve::BODY, open);
            recordEquipmentTestingAction("info", "РљР»Р°РїР°РЅ С‚РµР»Р°",
                                         open ? "РљР»Р°РїР°РЅ С‚РµР»Р° РѕС‚РєСЂС‹С‚ РІСЂСѓС‡РЅСѓСЋ."
                                              : "РљР»Р°РїР°РЅ С‚РµР»Р° Р·Р°РєСЂС‹С‚ РІСЂСѓС‡РЅСѓСЋ.");
          }
        } else if (target == "tails") {
          if (!Valves::hasTailsValve()) {
            request->send(
                409, "application/json",
                "{\"success\":false,\"message\":\"Tails valve is not available on this board\"}");
            return;
          }
          if (action == "pulse") {
            if (durationMs < 100) durationMs = 100;
            if (durationMs > 10000) durationMs = 10000;
            outputCommands[outputCommandCount++] = PowerCommandGateway::pulseValve(
                PowerCommandGateway::Owner::SERVICE,
                PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                PowerCommandGateway::Valve::TAILS, durationMs);
            recordEquipmentTestingAction("info", "РљР»Р°РїР°РЅ С…РІРѕСЃС‚РѕРІ",
                                         "РљР»Р°РїР°РЅ С…РІРѕСЃС‚РѕРІ РїРѕР»СѓС‡РёР» СЃРµСЂРІРёСЃРЅС‹Р№ РёРјРїСѓР»СЊСЃ.");
          } else {
            outputCommands[outputCommandCount++] = PowerCommandGateway::setValve(
                PowerCommandGateway::Owner::SERVICE,
                PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                PowerCommandGateway::Valve::TAILS, open);
            recordEquipmentTestingAction("info", "РљР»Р°РїР°РЅ С…РІРѕСЃС‚РѕРІ",
                                         open ? "РљР»Р°РїР°РЅ С…РІРѕСЃС‚РѕРІ РѕС‚РєСЂС‹С‚ РІСЂСѓС‡РЅСѓСЋ."
                                              : "РљР»Р°РїР°РЅ С…РІРѕСЃС‚РѕРІ Р·Р°РєСЂС‹С‚ РІСЂСѓС‡РЅСѓСЋ.");
          }
        } else if (target == "uno") {
          if (action == "pulse") {
            if (durationMs < 100) durationMs = 100;
            if (durationMs > 10000) durationMs = 10000;
            outputCommands[outputCommandCount++] = PowerCommandGateway::pulseValve(
                PowerCommandGateway::Owner::SERVICE,
                PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                PowerCommandGateway::Valve::UNO, durationMs);
            char detail[128];
            snprintf(detail, sizeof(detail), "УНО дало импульс %lu мс.",
                     (unsigned long)durationMs);
            recordEquipmentTestingAction("info", "УНО", detail);
          } else {
            outputCommands[outputCommandCount++] = PowerCommandGateway::setValve(
                PowerCommandGateway::Owner::SERVICE,
                PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                PowerCommandGateway::Valve::UNO, open);
            recordEquipmentTestingAction("info", "УНО",
                                         open ? "УНО открыто вручную."
                                              : "УНО закрыто вручную.");
          }
        } else if (target == "startStop") {
          outputCommands[outputCommandCount++] = PowerCommandGateway::setCoolingPwm(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST, duty);
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

        sendTestingStatusWithOutputCommands(request, outputCommands,
                                            outputCommandCount);
      });

  server.on(
      "/api/testing/servo", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"message\":\"Invalid JSON\"}")) {
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
          Valves::configureFractionator(g_settings.fractionator);
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

        PowerCommandGateway::Command outputCommand;
        if (action == "preset") {
          Fraction fraction = Fraction::UNKNOWN;
          if (!parseFractionPresetToken(doc["preset"] | "", fraction) ||
              fraction == Fraction::UNKNOWN) {
            request->send(
                400, "application/json",
                "{\"success\":false,\"message\":\"Unknown servo preset\"}");
            return;
          }
          outputCommand = PowerCommandGateway::setFraction(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST, fraction, true);
          char detail[128];
          snprintf(detail, sizeof(detail), "Сервопривод переведён в позицию %s.",
                   getFractionLabel(fraction));
          recordEquipmentTestingAction("success", "Сервопривод", detail);
        } else if (action == "angle") {
          int angle = doc["angle"] | 0;
          if (angle < 0) angle = 0;
          if (angle > 180) angle = 180;
          outputCommand = PowerCommandGateway::setFractionAngle(
              PowerCommandGateway::Owner::SERVICE,
              PowerCommandGateway::Reason::TFT_SERVICE_TEST,
              static_cast<uint8_t>(angle));
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

        sendTestingStatusWithOutputCommand(request, outputCommand);
      });
}
