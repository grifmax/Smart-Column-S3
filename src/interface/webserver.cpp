/**
 * Smart-Column S3 - Веб-сервер
 *
 * HTTP server + WebSocket для Web UI
 */

#include "webserver.h"
#include "../config.h"
#include "../fs_compat.h"
#include "../history.h"
#include "../types.h"
#include <AsyncTCP.h>
#include <WiFi.h>

// Определение HTTP методов для ESPAsyncWebServer
#ifndef HTTP_GET
typedef enum {
  HTTP_GET = 0b00000001,
  HTTP_POST = 0b00000010,
  HTTP_DELETE = 0b00000100,
  HTTP_PUT = 0b00001000,
  HTTP_PATCH = 0b00010000,
  HTTP_HEAD = 0b00100000,
  HTTP_OPTIONS = 0b01000000,
  HTTP_ANY = 0b01111111,
} WebRequestMethod;
#endif

#include "control/fsm.h"
#include "control/safety.h"
#include "control/v2/reason_codes.h"
#include "control/v2/status_adapter.h"
#include "control/watt_control.h"
#include "drivers/display.h"
#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/stirrer.h"
#include "drivers/valves.h"
#include "drivers/sensors.h"
#include "interface/mqtt.h"
#include "interface/security.h"
#include "interface/wifi_profiles.h"
#include "storage/logger.h"
#include "storage/nvs_manager.h"
#include "cloud_tunnel.h"
#include "../profiles.h"
#include <ArduinoJson.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <Update.h>


// Внешние переменные из main.cpp
extern SystemState g_state;
extern Settings g_settings;
extern EnergyHistory g_energyHistory;

static AsyncWebServer server(WEB_SERVER_PORT);
static AsyncWebSocket ws("/ws");

struct PumpCalibrationSession {
  bool active = false;
  uint32_t startSteps = 0;
  uint32_t stopSteps = 0;
  uint32_t startMs = 0;
  uint32_t stopMs = 0;
};

static PumpCalibrationSession g_pumpCalSession;

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

static bool hasConfiguredWiFi() {
  return WiFiProfiles::hasConfiguredProfiles(g_settings.wifi);
}

static void appendWiFiProfileJson(JsonObject obj, const WiFiProfile& profile, uint8_t index) {
  obj["index"] = index;
  obj["priority"] = index + 1;
  obj["enabled"] = profile.enabled;
  obj["ssid"] = profile.ssid;
  obj["hasPassword"] = (profile.password[0] != '\0');
  obj["useStaticIp"] = profile.useStaticIp;
  obj["ip"] = profile.ip;
  obj["gateway"] = profile.gateway;
  obj["subnet"] = profile.subnet;
  obj["dns1"] = profile.dns1;
  obj["dns2"] = profile.dns2;
  obj["connected"] =
      (WiFi.status() == WL_CONNECTED && String(WiFi.SSID()) == String(profile.ssid));
}

static void buildWiFiProfilesResponse(JsonDocument& doc) {
  WiFiProfiles::compactProfiles(g_settings.wifi);
  JsonArray profiles = doc["profiles"].to<JsonArray>();
  for (uint8_t i = 0; i < g_settings.wifi.profileCount && i < WIFI_MAX_PROFILES; ++i) {
    JsonObject item = profiles.add<JsonObject>();
    appendWiFiProfileJson(item, g_settings.wifi.profiles[i], i);
  }
  doc["count"] = g_settings.wifi.profileCount;
}

static bool isValidIpOrEmpty(const char* value) {
  if (!value || value[0] == '\0') return true;
  IPAddress ip;
  return ip.fromString(value);
}

// Вспомогательные функции для строковых представлений
static const char *getModeString(Mode mode) {
  switch (mode) {
  case Mode::IDLE:
    return "idle";
  case Mode::RECTIFICATION:
    return "rectification";
  case Mode::DISTILLATION:
    return "distillation";
  case Mode::MANUAL_RECT:
    return "manual";
  case Mode::MASHING:
    return "mashing";
  case Mode::HOLD:
    return "hold";
  case Mode::NBK:
    return "nbk";
  case Mode::FERMENTATION:
    return "fermentation";
  default:
    return "unknown";
  }
}

static const char *getPhaseString(RectPhase phase) {
  switch (phase) {
  case RectPhase::IDLE:
    return "idle";
  case RectPhase::HEATING:
    return "heating";
  case RectPhase::STABILIZATION:
    return "stabilization";
  case RectPhase::HEADS:
    return "heads";
  case RectPhase::POST_HEADS_STABILIZATION:
    return "post_heads";
  case RectPhase::BODY:
    return "body";
  case RectPhase::TAILS:
    return "tails";
  case RectPhase::PURGE:
    return "purge";
  case RectPhase::FINISH:
    return "finish";
  case RectPhase::COMPLETED:
    return "completed";
  default:
    return "unknown";
  }
}

static const char *getMashPhaseString(MashPhase phase) {
  switch (phase) {
  case MashPhase::IDLE:
    return "idle";
  case MashPhase::ACID_REST:
    return "acid_rest";
  case MashPhase::PROTEIN_REST:
    return "protein_rest";
  case MashPhase::BETA_AMYLASE:
    return "beta_amylase";
  case MashPhase::ALPHA_AMYLASE:
    return "alpha_amylase";
  case MashPhase::MASH_OUT:
    return "mash_out";
  case MashPhase::FINISH:
    return "finish";
  default:
    return "unknown";
  }
}

static const char *getNbkPhaseString(NbkPhase phase) {
  switch (phase) {
  case NbkPhase::IDLE:
    return "idle";
  case NbkPhase::HEATING:
    return "heating";
  case NbkPhase::STABILIZATION:
    return "stabilization";
  case NbkPhase::WORKING:
    return "working";
  case NbkPhase::FINISH:
    return "finish";
  case NbkPhase::COMPLETED:
    return "completed";
  default:
    return "unknown";
  }
}

static const char *getFermPhaseString(FermentationPhase phase) {
  switch (phase) {
  case FermentationPhase::IDLE:
    return "idle";
  case FermentationPhase::RUNNING:
    return "running";
  case FermentationPhase::COMPLETED:
    return "completed";
  default:
    return "unknown";
  }
}

static const char *getFractionToken(Fraction fraction) {
  switch (fraction) {
  case Fraction::HEADS:
    return "heads";
  case Fraction::SUBHEADS:
    return "subheads";
  case Fraction::BODY:
    return "body";
  case Fraction::PRETAILS:
    return "pretails";
  case Fraction::TAILS:
    return "tails";
  default:
    return "manual";
  }
}

static const char *getFractionLabel(Fraction fraction) {
  switch (fraction) {
  case Fraction::HEADS:
    return FRACTION_NAME_HEADS;
  case Fraction::SUBHEADS:
    return FRACTION_NAME_SUBHEADS;
  case Fraction::BODY:
    return FRACTION_NAME_BODY;
  case Fraction::PRETAILS:
    return FRACTION_NAME_PRETAILS;
  case Fraction::TAILS:
    return FRACTION_NAME_TAILS;
  default:
    return "Ручная позиция";
  }
}

static const char *getTempSensorLabel(uint8_t index) {
  switch (index) {
  case TEMP_CUBE:
    return "Куб";
  case TEMP_COLUMN_BOTTOM:
    return "Царга низ";
  case TEMP_COLUMN_TOP:
    return "Царга верх";
  case TEMP_REFLUX:
    return "Дефлегматор";
  case TEMP_TSA:
    return "ТСА";
  case TEMP_WATER_IN:
    return "Вода вход";
  case TEMP_WATER_OUT:
    return "Вода выход";
  default:
    return "Датчик";
  }
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

static void fillEquipmentTestingStatus(JsonDocument &doc) {
  char reason[160] = "";
  const bool blocked = isEquipmentTestingBlocked(reason, sizeof(reason));
  const bool processActive = g_state.mode != Mode::IDLE;
  const bool latched = Safety::isLatched(g_state);
  const bool alarmActive = (g_state.currentAlarm.type != AlarmType::NONE);
  const bool heaterActive = Heater::getPower() > 0;
  const bool servoAvailable =
      g_settings.fractionator.enabled && Valves::isFractionatorEnabled();
  const Pump::Diagnostics pumpDiag = Pump::getDiagnostics();

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
  activeTests["heater"] = heaterActive;
  activeTests["waterValve"] = Valves::getWater();
  activeTests["headsValve"] = Valves::getHeads();
  activeTests["unoValve"] = Valves::getUno();
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

  JsonObject heater = doc["heater"].to<JsonObject>();
  heater["active"] = heaterActive;
  heater["powerPercent"] = Heater::getPower();
  heater["powerSetPercent"] = Heater::getPower();
  heater["minSubmergeLiters"] = g_settings.equipment.minHeaterSubmergeL;

  JsonObject valves = doc["valves"].to<JsonObject>();
  valves["water"] = Valves::getWater();
  valves["heads"] = Valves::getHeads();
  valves["uno"] = Valves::getUno();
  valves["startStopDuty"] = Valves::getStartStop();
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

  JsonObject pressure = doc["pressure"].to<JsonObject>();
  pressure["cubeMmHg"] = g_state.pressure.cube;
  pressure["atmosphere"] = g_state.pressure.atmosphere;
  pressure["ok"] = g_state.pressure.ok;
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
  power["voltage"] = g_state.power.voltage;
  power["current"] = g_state.power.current;
  power["power"] = g_state.power.power;
  power["energy"] = g_state.power.energy;
  power["frequency"] = g_state.power.frequency;
  power["powerFactor"] = g_state.power.powerFactor;

  JsonArray recentActions = doc["recentActions"].to<JsonArray>();
  for (uint8_t i = 0; i < g_equipmentTestingActionCount; ++i) {
    const int16_t rawIndex =
        static_cast<int16_t>(g_equipmentTestingActionNext) - 1 - i;
    const uint8_t index =
        static_cast<uint8_t>((rawIndex + EQUIPMENT_TESTING_ACTION_CAPACITY) %
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

static void fillAlarmJson(JsonObject alarm, const SystemState& state, const Settings& settings) {
  const bool active = (state.currentAlarm.type != AlarmType::NONE);
  const bool latched = Safety::isLatched(state);
  char resetBlockedReason[128] = "";
  const bool resetAvailable = latched
                                  ? Safety::canResetNow(state, settings, resetBlockedReason,
                                                        sizeof(resetBlockedReason))
                                  : !active;
  alarm["active"] = active;
  alarm["latched"] = latched;
  alarm["type"] = Safety::getAlarmTypeToken(state.currentAlarm.type);
  alarm["typeCode"] = static_cast<int>(state.currentAlarm.type);
  alarm["level"] = Safety::getAlarmLevelToken(state.currentAlarm.level);
  alarm["levelCode"] = static_cast<int>(state.currentAlarm.level);
  alarm["message"] = active ? state.currentAlarm.message : "";
  alarm["timestamp"] = state.currentAlarm.timestamp;
  alarm["acknowledged"] = state.currentAlarm.acknowledged;
  alarm["resetAvailable"] = resetAvailable;
  alarm["resetBlockedReason"] = latched && !resetAvailable ? resetBlockedReason : "";
}

static void fillV2StatusJson(JsonObject v2, const ControlV2::ModeStatusV2& status,
                             const ControlV2::MetricsSnapshotV2& metrics) {
  v2["available"] = true;
  v2["mode"] = static_cast<int>(status.mode);
  v2["lifecycle"] = ControlV2::modeLifecycleToString(status.lifecycle);
  v2["phaseId"] = status.phaseId;
  v2["phaseToken"] = status.phaseToken;
  v2["phaseStartMs"] = status.phaseStartMs;
  v2["phaseElapsedSec"] = status.phaseElapsedSec;
  v2["modeStartMs"] = status.modeStartMs;
  v2["modeElapsedSec"] = status.modeElapsedSec;
  v2["paused"] = status.paused;
  v2["safetyLatched"] = status.safetyLatched;
  v2["lastReasonCode"] = ControlV2::reasonCodeToString(status.lastReasonCode);
  v2["operatorMessage"] = status.operatorMessage;
  v2["timestampMs"] = metrics.timestampMs;

  JsonObject limits = v2["activeLimits"].to<JsonObject>();
  limits["powerCapped"] = status.activeLimits.powerCapped;
  limits["maxHeaterPowerPercent"] = status.activeLimits.maxHeaterPowerPercent;
  limits["pumpCapped"] = status.activeLimits.pumpCapped;
  limits["maxPumpSpeedMlH"] = status.activeLimits.maxPumpSpeedMlH;
  limits["takeoffBlocked"] = status.activeLimits.takeoffBlocked;
  limits["phaseAdvanceBlocked"] = status.activeLimits.phaseAdvanceBlocked;

  JsonObject targets = v2["commandTargets"].to<JsonObject>();
  targets["heaterPowerPercent"] = status.commandTargets.heaterPowerPercent;
  targets["pumpSpeedMlH"] = status.commandTargets.pumpSpeedMlH;
  targets["waterValveOpen"] = status.commandTargets.waterValveOpen;
  targets["headsValveOpen"] = status.commandTargets.headsValveOpen;
  targets["stopRequested"] = status.commandTargets.stopRequested;

  JsonObject indicators = v2["indicators"].to<JsonObject>();
  indicators["processHealth"] = status.indicators.processHealth;
  indicators["sensorFreshnessOk"] = status.indicators.sensorFreshnessOk;
  indicators["pressureStable"] = status.indicators.pressureStable;
  indicators["boilingDetected"] = status.indicators.boilingDetected;
  indicators["columnStable"] = status.indicators.columnStable;
  indicators["targetReached"] = status.indicators.targetReached;
  indicators["powerLimited"] = status.indicators.powerLimited;
  indicators["recoveryActive"] = status.indicators.recoveryActive;
  indicators["takeoffAllowed"] = status.indicators.takeoffAllowed;
  indicators["distHeatingComplete"] = status.indicators.distHeatingComplete;
  indicators["distHeadsOptionalComplete"] = status.indicators.distHeadsOptionalComplete;
  indicators["distBodyNearEnd"] = status.indicators.distBodyNearEnd;
  indicators["steamReady"] = status.indicators.steamReady;
  indicators["nbkWorkingStable"] = status.indicators.nbkWorkingStable;
  indicators["nbkFeedAllowed"] = status.indicators.nbkFeedAllowed;
  indicators["finishLikely"] = status.indicators.finishLikely;
  indicators["tempInBand"] = status.indicators.tempInBand;
  indicators["stepReady"] = status.indicators.stepReady;
  indicators["stepHoldStable"] = status.indicators.stepHoldStable;
  indicators["heatingTooSlow"] = status.indicators.heatingTooSlow;
  indicators["overshootRisk"] = status.indicators.overshootRisk;
  indicators["fermTempInBand"] = status.indicators.fermTempInBand;
  indicators["longDeviation"] = status.indicators.longDeviation;
  indicators["heatingDemand"] = status.indicators.heatingDemand;
  indicators["coolingDemand"] = status.indicators.coolingDemand;
  indicators["heatingRateCPerMin"] = status.indicators.heatingRateCPerMin;
  indicators["topTempRateCPerMin"] = status.indicators.topTempRateCPerMin;
  indicators["pressureRateMmHgPerMin"] = status.indicators.pressureRateMmHgPerMin;
  indicators["coolingMarginC"] = status.indicators.coolingMarginC;
  indicators["distPressureMargin"] = status.indicators.distPressureMargin;
  indicators["nbkPressureMargin"] = status.indicators.nbkPressureMargin;
  indicators["nbkColumnLoad"] = status.indicators.nbkColumnLoad;
  indicators["feedEnergyBalance"] = status.indicators.feedEnergyBalance;
  indicators["stabilityIndex"] = status.indicators.stabilityIndex;
  indicators["floodRisk"] = status.indicators.floodRisk;
  indicators["headsCompletionScore"] = status.indicators.headsCompletionScore;
  indicators["bodyEndScore"] = status.indicators.bodyEndScore;

  JsonObject safety = v2["safety"].to<JsonObject>();
  safety["severity"] = ControlV2::safetySeverityToString(metrics.safety.severity);
  safety["event"] = ControlV2::safetyEventTypeToString(metrics.safety.primaryEvent);
  safety["reasonCode"] = ControlV2::reasonCodeToString(metrics.safety.reasonCode);
  safety["requiresAcknowledge"] = metrics.safety.requiresAcknowledge;
  safety["message"] = metrics.safety.message;
  safety["resetAvailable"] = !status.safetyLatched ||
                             metrics.safety.severity == ControlV2::SafetySeverityV2::RECOVERY;
  safety["resetBlockedReason"] =
      status.safetyLatched &&
              metrics.safety.severity != ControlV2::SafetySeverityV2::RECOVERY
          ? metrics.safety.message
          : "";
}

static void fillSafetyActionV2Json(JsonObject v2, const ControlV2::ModeStatusV2& status,
                                   const ControlV2::MetricsSnapshotV2& metrics) {
  v2["available"] = true;
  v2["safetyLatched"] = status.safetyLatched;
  v2["lastReasonCode"] = ControlV2::reasonCodeToString(status.lastReasonCode);
  v2["operatorMessage"] = status.operatorMessage;

  JsonObject safety = v2["safety"].to<JsonObject>();
  safety["severity"] = ControlV2::safetySeverityToString(metrics.safety.severity);
  safety["event"] = ControlV2::safetyEventTypeToString(metrics.safety.primaryEvent);
  safety["reasonCode"] = ControlV2::reasonCodeToString(metrics.safety.reasonCode);
  safety["requiresAcknowledge"] = metrics.safety.requiresAcknowledge;
  safety["message"] = metrics.safety.message;
  safety["resetAvailable"] = !status.safetyLatched ||
                             metrics.safety.severity == ControlV2::SafetySeverityV2::RECOVERY;
  safety["resetBlockedReason"] =
      status.safetyLatched &&
              metrics.safety.severity != ControlV2::SafetySeverityV2::RECOVERY
          ? metrics.safety.message
          : "";
}

static bool isSecurityOnboardingMode() {
  return !hasConfiguredWiFi() && WiFi.status() != WL_CONNECTED;
}

static bool isSecurityExemptPath(const String& path) {
  if (!isSecurityOnboardingMode()) {
    return false;
  }

  return path == "/" || path == "/wifi.html" || path == "/generate_204" ||
         path == "/hotspot-detect.html" || path == "/connecttest.txt" ||
         path.startsWith("/api/wifi");
}

static bool handleSecurityGate(AsyncWebServerRequest* request) {
  if (!request || isSecurityExemptPath(request->url())) {
    return true;
  }

  if (!Security::checkRateLimit(request->client()->remoteIP())) {
    const bool isApi = request->url().startsWith("/api/");
    request->send(
        429,
        isApi ? "application/json" : "text/plain",
        isApi ? "{\"success\":false,\"message\":\"Too many requests\"}"
              : "Too many requests");
    return false;
  }

  if (!Security::checkAuth(request)) {
    Security::requestAuth(request);
    return false;
  }

  return true;
}

static void applySecuritySettings() {
  Security::init(g_settings.security.username, g_settings.security.password);
  Security::setAuthEnabled(g_settings.security.authEnabled);
  Security::setRateLimitEnabled(g_settings.security.rateLimitEnabled);
}

static float clampFloatRange(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

static uint16_t clampU16Range(uint32_t value, uint16_t minValue,
                              uint16_t maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return static_cast<uint16_t>(value);
}

static void getRectFeedstockDefaults(uint8_t feedstock, float &headsPct,
                                     float &bodyPct, float &tailsPct) {
  switch (feedstock) {
  case 0: // Sugar
    headsPct = 6.0f;
    bodyPct = 84.0f;
    tailsPct = 10.0f;
    break;
  case 1: // Flour / grain
    headsPct = 8.0f;
    bodyPct = 80.0f;
    tailsPct = 12.0f;
    break;
  case 2: // Malt
    headsPct = 7.0f;
    bodyPct = 81.0f;
    tailsPct = 12.0f;
    break;
  case 3: // Fruit
    headsPct = 5.0f;
    bodyPct = 75.0f;
    tailsPct = 20.0f;
    break;
  case 4: // Molasses
    headsPct = 8.0f;
    bodyPct = 74.0f;
    tailsPct = 18.0f;
    break;
  case 5: // Grape / wine
    headsPct = 6.0f;
    bodyPct = 78.0f;
    tailsPct = 16.0f;
    break;
  case 6: // Honey
    headsPct = 7.0f;
    bodyPct = 79.0f;
    tailsPct = 14.0f;
    break;
  default:
    headsPct = RECT_HEADS_PERCENT_DEFAULT;
    bodyPct = RECT_BODY_PERCENT_DEFAULT;
    tailsPct = RECT_TAILS_PERCENT_DEFAULT;
    break;
  }
}

static void normalizeRectFractions(RectParams &params) {
  params.headsPercent = clampFloatRange(params.headsPercent, 0.0f, 40.0f);
  params.bodyPercent = clampFloatRange(params.bodyPercent, 0.0f, 100.0f);
  params.tailsPercent = clampFloatRange(params.tailsPercent, 0.0f, 100.0f);

  float sum = params.headsPercent + params.bodyPercent + params.tailsPercent;
  if (sum <= 100.0f) return;

  float excess = sum - 100.0f;
  if (params.tailsPercent >= excess) {
    params.tailsPercent -= excess;
    return;
  }

  excess -= params.tailsPercent;
  params.tailsPercent = 0.0f;
  params.bodyPercent = clampFloatRange(params.bodyPercent - excess, 0.0f, 100.0f);
}

namespace WebServer {

void init() {
  LOG_I("WebServer: Initializing...");

  applySecuritySettings();

  server.addMiddleware([](AsyncWebServerRequest* request, ArMiddlewareNext next) {
    if (!handleSecurityGate(request)) {
      return;
    }
    next();
  });

  // WebSocket обработчик
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      LOG_I("WebSocket: Client connected #%u", client->id());
    } else if (type == WS_EVT_DISCONNECT) {
      LOG_I("WebSocket: Client disconnected #%u", client->id());
    } else if (type == WS_EVT_DATA) {
      // Обработка команд от клиента
      LOG_D("WebSocket: Data received");
    }
  });

  server.addHandler(&ws);

  // WiFi Setup Wizard — при первом запуске (нет сохранённого SSID)
  // редирект на лёгкую страницу wifi.html вместо тяжёлого index.html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!hasConfiguredWiFi() && WiFi.status() != WL_CONNECTED) {
      request->redirect("/wifi.html");
      return;
    }
    // Отдаём index.html (или .gz если есть)
    if (LittleFS.exists("/index.html.gz")) {
      AsyncWebServerResponse *response =
          request->beginResponse(LittleFS, "/index.html.gz", "text/html");
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
    } else {
      request->send(LittleFS, "/index.html", "text/html");
    }
  });

  // Captive portal: перехват всех неизвестных хостов → wifi.html
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!hasConfiguredWiFi()) {
      request->redirect("/wifi.html");
    } else {
      request->send(204);
    }
  });
  server.on("/hotspot-detect.html", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              request->redirect("/wifi.html");
            });
  server.on("/connecttest.txt", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              if (!hasConfiguredWiFi()) {
                request->redirect("/wifi.html");
              } else {
                request->send(200, "text/plain", "Microsoft Connect Test");
              }
            });

  // Статические файлы (Web UI)
  server.serveStatic("/", LittleFS, "/")
      .setDefaultFile("index.html")
      .setTemplateProcessor(nullptr);

  // API endpoints
  // GET /api/status - полное состояние системы
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    ControlV2::updateRuntime(g_state, g_settings);

    JsonDocument doc;

    // Режим и состояние процесса
    doc["mode"] = static_cast<int>(g_state.mode);
    doc["modeStr"] = getModeString(g_state.mode);
    int activePhase = static_cast<int>(g_state.rectPhase);
    const char *activePhaseStr = getPhaseString(g_state.rectPhase);
    if (g_state.mode == Mode::NBK) {
      activePhase = static_cast<int>(g_state.nbkPhase);
      activePhaseStr = getNbkPhaseString(g_state.nbkPhase);
    } else if (g_state.mode == Mode::FERMENTATION) {
      activePhase = static_cast<int>(g_state.fermPhase);
      activePhaseStr = getFermPhaseString(g_state.fermPhase);
    }
    doc["phase"] = activePhase;
    doc["phaseStr"] = activePhaseStr;
    doc["nbkPhase"] = static_cast<int>(g_state.nbkPhase);
    doc["nbkPhaseStr"] = getNbkPhaseString(g_state.nbkPhase);
    doc["fermPhase"] = static_cast<int>(g_state.fermPhase);
    doc["fermPhaseStr"] = getFermPhaseString(g_state.fermPhase);
    doc["paused"] = g_state.paused;
    doc["safetyOk"] = g_state.safetyOk;
    doc["uptime"] = g_state.uptime;
    doc["deviceId"] = CloudTunnel::getDeviceId();
    JsonObject alarm = doc["alarm"].to<JsonObject>();
    fillAlarmJson(alarm, g_state, g_settings);

    // Температуры
    JsonObject temps = doc["temps"].to<JsonObject>();
    temps["cube"] = g_state.temps.cube;
    temps["columnBottom"] = g_state.temps.columnBottom;
    temps["columnMiddle"] = g_state.temps.columnMiddle;
    temps["columnTop"] = g_state.temps.columnTop;
    temps["reflux"] = g_state.temps.reflux;
    temps["deflegmator"] = g_state.temps.deflegmator;
    temps["product"] = g_state.temps.product;
    temps["tsa"] = g_state.temps.tsa;
    temps["waterIn"] = g_state.temps.waterIn;
    temps["waterOut"] = g_state.temps.waterOut;

    // Давление
    JsonObject pressure = doc["pressure"].to<JsonObject>();
    pressure["cube"] = g_state.pressure.cube;
    pressure["atm"] = g_state.pressure.atmosphere;
    pressure["kpa"] = g_state.pressure.pressure;

    // Мощность (PZEM-004T)
    JsonObject power = doc["power"].to<JsonObject>();
    power["voltage"] = g_state.power.voltage;
    power["current"] = g_state.power.current;
    power["power"] = g_state.power.power;
    power["setPercent"] = Heater::getPower();
    power["energy"] = g_state.power.energy;
    power["frequency"] = g_state.power.frequency;
    power["pf"] = g_state.power.powerFactor;

    // Насос
    JsonObject pump = doc["pump"].to<JsonObject>();
    pump["speedMlH"] = g_state.pump.speedMlPerHour;
    pump["totalMl"] = g_state.pump.totalVolumeMl;
    pump["running"] = g_state.pump.running;

    JsonObject valves = doc["valves"].to<JsonObject>();
    valves["water"] = Valves::getWater();
    valves["heads"] = Valves::getHeads();
    valves["uno"] = Valves::getUno();
    valves["tails"] = false; // Отдельного канала хвостов в драйвере нет

    // Ареометр
    JsonObject hydro = doc["hydrometer"].to<JsonObject>();
    hydro["abv"] = g_state.hydrometer.abv;
    hydro["pressure"] = g_state.hydrometer.pressure;
    hydro["valid"] = g_state.hydrometer.valid;

    // Объёмы фракций
    JsonObject volumes = doc["volumes"].to<JsonObject>();
    volumes["heads"] = g_state.stats.headsVolume;
    volumes["body"] = g_state.stats.bodyVolume;
    volumes["tails"] = g_state.stats.tailsVolume;

    // Настройки оборудования (для UI)
    JsonObject equipment = doc["equipment"].to<JsonObject>();
    equipment["heaterPowerW"] = g_settings.equipment.heaterPowerW;
    equipment["columnHeightMm"] = g_settings.equipment.columnHeightMm;
    equipment["cubeVolumeL"] = g_settings.equipment.cubeVolumeL;
    equipment["minHeaterSubmergeL"] = g_settings.equipment.minHeaterSubmergeL;
    equipment["waterAutoStartCubeTempC"] = g_settings.equipment.waterAutoStartCubeTempC;

    JsonObject safetySettings = doc["safetySettings"].to<JsonObject>();
    safetySettings["pressureMaxMmHg"] = g_settings.safety.pressureMaxMmHg;
    safetySettings["tsaMaxC"] = g_settings.safety.tsaMaxC;
    safetySettings["waterOutMaxC"] = g_settings.safety.waterOutMaxC;
    safetySettings["waterOutRiseRateCMin"] = g_settings.safety.waterOutRiseRateCMin;
    safetySettings["pressureRiseRateMmHgMin"] = g_settings.safety.pressureRiseRateMmHgMin;
    doc["min_heater_submerge_l"] = g_settings.equipment.minHeaterSubmergeL;
    doc["water_auto_start_cube_temp_c"] = g_settings.equipment.waterAutoStartCubeTempC;
    doc["safety_pressure_max_mmhg"] = g_settings.safety.pressureMaxMmHg;
    doc["safety_tsa_max_c"] = g_settings.safety.tsaMaxC;
    doc["safety_water_out_max_c"] = g_settings.safety.waterOutMaxC;
    doc["safety_water_out_rise_rate_c_min"] = g_settings.safety.waterOutRiseRateCMin;
    doc["safety_pressure_rise_rate_mmhg_min"] = g_settings.safety.pressureRiseRateMmHgMin;

    // Runtime-параметры режимов (для экрана мониторинга)
    JsonObject rect = doc["rectification"].to<JsonObject>();
    rect["feedVolumeL"] = g_settings.rectParams.feedVolumeL;
    rect["feedAbvPercent"] = g_settings.rectParams.feedAbvPercent;
    rect["headsPercent"] = g_settings.rectParams.headsPercent;
    rect["bodyPercent"] = g_settings.rectParams.bodyPercent;
    rect["tailsPercent"] = g_settings.rectParams.tailsPercent;
    rect["headsSpeedMlHKw"] = g_settings.rectParams.headsSpeedMlHKw;
    rect["bodySpeedMlHKw"] = g_settings.rectParams.bodySpeedMlHKw;

    float rectHeadsTargetMl = 0.0f;
    float rectBodyTargetMl = 0.0f;
    float rectTailsTargetMl = 0.0f;
    FSM::getRectTargetsMl(rectHeadsTargetMl, rectBodyTargetMl, rectTailsTargetMl);
    rect["headsTargetMl"] = rectHeadsTargetMl;
    rect["bodyTargetMl"] = rectBodyTargetMl;
    rect["tailsTargetMl"] = rectTailsTargetMl;

    JsonObject distillation = doc["distillation"].to<JsonObject>();
    float distSpeedMlH = 0.0f;
    float distHeadsVolumeMl = 0.0f;
    float distTargetVolumeMl = 0.0f;
    float distEndTempC = 0.0f;
    uint8_t distPowerPercent = 0;
    FSM::getDistillationParams(distSpeedMlH, distHeadsVolumeMl, distTargetVolumeMl, distEndTempC,
                               distPowerPercent);
    distillation["speedMlH"] = distSpeedMlH;
    distillation["headsVolumeMl"] = distHeadsVolumeMl;
    distillation["targetVolumeMl"] = distTargetVolumeMl;
    distillation["endTempC"] = distEndTempC;
    distillation["powerPercent"] = distPowerPercent;

    JsonObject nbk = doc["nbk"].to<JsonObject>();
    nbk["powerW"] = g_settings.nbk.powerW;
    nbk["pumpSpeedMlH"] = g_settings.nbk.pumpSpeedMlH;
    nbk["columnBottomTempThresholdC"] = g_settings.nbk.columnBottomTempThresholdC;
    nbk["phase"] = static_cast<int>(g_state.nbkPhase);
    nbk["phaseStr"] = getNbkPhaseString(g_state.nbkPhase);

    JsonObject fermentation = doc["fermentation"].to<JsonObject>();
    fermentation["targetTempC"] = g_settings.fermentation.targetTempC;
    fermentation["hysteresisC"] = g_settings.fermentation.hysteresisC;
    fermentation["useHeater"] = g_settings.fermentation.useHeater;
    fermentation["phase"] = static_cast<int>(g_state.fermPhase);
    fermentation["phaseStr"] = getFermPhaseString(g_state.fermPhase);

    JsonObject progress = doc["progress"].to<JsonObject>();
    const uint32_t phaseElapsedSec = FSM::getPhaseElapsedSec();
    const uint32_t phaseTargetSec = FSM::getPhaseTargetSec(g_state, g_settings);
    progress["phaseElapsedSec"] = phaseElapsedSec;
    progress["phaseTargetSec"] = phaseTargetSec;
    progress["phaseRemainingSec"] =
        (phaseTargetSec > phaseElapsedSec) ? (phaseTargetSec - phaseElapsedSec) : 0;
    progress["phasePercent"] = FSM::getPhaseProgressPercent(g_state, g_settings);

    JsonObject v2 = doc["v2"].to<JsonObject>();
    fillV2StatusJson(v2, ControlV2::getLatestModeStatus(),
                     ControlV2::getLatestMetricsSnapshot());

    const auto displayStats = Display::getRuntimeStats();
    JsonObject display = doc["display"].to<JsonObject>();
    display["frames"] = displayStats.framesRendered;
    display["slowFrames"] = displayStats.slowFrames;
    display["recoveries"] = displayStats.watchdogRecoveries;
    display["hardRecoveries"] = displayStats.hardWatchdogRecoveries;
    display["hardFailures"] = displayStats.hardWatchdogFailures;
    display["lastFrameMs"] = displayStats.lastFrameMs;
    display["maxFrameMs"] = displayStats.maxFrameMs;
    display["lastFrameAt"] = displayStats.lastFrameAtMs;
    display["lastGapMs"] = displayStats.lastUpdateGapMs;
    display["maxGapMs"] = displayStats.maxUpdateGapMs;
    display["gapOverruns"] = displayStats.updateGapOverruns;

    // Cloud tunnel status (локально полезно для привязки)
    JsonObject cloud = doc["cloud"].to<JsonObject>();
    cloud["enabled"] = g_settings.cloud.enabled;
    cloud["tunnelUrl"] = g_settings.cloud.tunnelUrl;
    cloud["connected"] = CloudTunnel::isConnected();
    cloud["authenticated"] = CloudTunnel::isAuthenticated();
    cloud["claimActive"] = CloudTunnel::hasActiveClaim();
    if (CloudTunnel::hasActiveClaim()) {
      cloud["claimCode"] = CloudTunnel::getClaimCode();
      cloud["claimExpiresAt"] = CloudTunnel::getClaimExpiresAt();
    }

    // -----------------------------------------------------------------------
    // Режимы с температурными ступенями (mashing / hold)
    // -----------------------------------------------------------------------
    const uint32_t now = millis();

    JsonObject mashing = doc["mashing"].to<JsonObject>();
    mashing["active"] = g_state.mashing.active;
    mashing["phase"] = static_cast<int>(g_state.mashing.phase);
    mashing["phaseStr"] = getMashPhaseString(g_state.mashing.phase);
    mashing["stepCount"] = g_state.mashing.stepCount;
    mashing["currentStep"] = g_state.mashing.currentStep;
    mashing["targetTemp"] = g_state.mashing.targetTemp;
    mashing["stepDurationSec"] = g_state.mashing.stepDuration;
    mashing["tempInRange"] = g_state.mashing.tempInRange;
    mashing["stepName"] = g_state.mashing.stepName;

    uint32_t mashElapsedSec = 0;
    if (g_state.mashing.tempInRange && g_state.mashing.inRangeStartTime > 0 &&
        now >= g_state.mashing.inRangeStartTime) {
      mashElapsedSec = (now - g_state.mashing.inRangeStartTime) / 1000UL;
    }
    mashing["elapsedSec"] = mashElapsedSec;
    mashing["remainingSec"] =
        (g_state.mashing.stepDuration > mashElapsedSec)
            ? (g_state.mashing.stepDuration - mashElapsedSec)
            : 0;

    JsonObject hold = doc["hold"].to<JsonObject>();
    hold["active"] = g_state.hold.active;
    hold["stepCount"] = g_state.hold.stepCount;
    hold["currentStep"] = g_state.hold.currentStep;
    hold["targetTemp"] = g_state.hold.targetTemp;
    hold["tempInRange"] = g_state.hold.tempInRange;

    uint32_t holdStepDurationSec = 0;
    if (g_state.hold.stepCount > 0 && g_state.hold.currentStep < g_state.hold.stepCount) {
      holdStepDurationSec = (uint32_t)g_state.hold.steps[g_state.hold.currentStep].duration * 60UL;
    }
    hold["stepDurationSec"] = holdStepDurationSec;

    uint32_t holdElapsedSec = 0;
    if (g_state.hold.tempInRange && g_state.hold.inRangeStartTime > 0 &&
        now >= g_state.hold.inRangeStartTime) {
      holdElapsedSec = (now - g_state.hold.inRangeStartTime) / 1000UL;
    }
    hold["elapsedSec"] = holdElapsedSec;
    hold["remainingSec"] =
        (holdStepDurationSec > holdElapsedSec) ? (holdStepDurationSec - holdElapsedSec) : 0;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // GET /api/health - получить здоровье системы
  server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;

    // Датчики температуры
    JsonObject temps = doc["temperatures"].to<JsonObject>();
    temps["ok"] = g_state.health.tempSensorsOk;
    temps["total"] = g_state.health.tempSensorsTotal;

    // Другие датчики
    JsonObject sensors = doc["sensors"].to<JsonObject>();
    sensors["bmp280"] = g_state.health.bmp280Ok;
    sensors["ads1115"] = g_state.health.ads1115Ok;
    sensors["pzem"] = g_state.health.pzemOk;

    // WiFi
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = g_state.health.wifiConnected;
    wifi["rssi"] = g_state.health.wifiRSSI;

    // Система
    JsonObject system = doc["system"].to<JsonObject>();
    system["uptime"] = g_state.health.uptime;
    system["freeHeap"] = g_state.health.freeHeap;
    system["cpuTemp"] = g_state.health.cpuTemp;

    // Ошибки
    JsonObject errors = doc["errors"].to<JsonObject>();
    errors["pzemSpikes"] = g_state.health.pzemSpikeCount;
    errors["tempErrors"] = g_state.health.tempReadErrors;

    // Общая оценка
    doc["overallHealth"] = g_state.health.overallHealth;
    doc["lastUpdate"] = g_state.health.lastUpdate;
    
    // Детальные оценки
    JsonArray scores = doc["healthScores"].to<JsonArray>();
    for (int i = 0; i < 6; i++) {
        scores.add(g_state.health.healthScores[i]);
    }
    
    // Информация о перезагрузке
    JsonObject reboot = doc["reboot"].to<JsonObject>();
    reboot["reason"] = g_rebootTracker.lastReason;
    reboot["reasonStr"] = g_rebootTracker.lastReasonStr;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // GET /api/version - получить информацию о версиях прошивки и фронтенда
  server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;

    // Версия и дата компиляции прошивки
    JsonObject firmware = doc["firmware"].to<JsonObject>();
    firmware["version"] = FIRMWARE_VERSION;
    firmware["buildDate"] = __DATE__;
    firmware["buildTime"] = __TIME__;
    firmware["compiler"] = "GCC " __VERSION__;

    // Информация о плате
    JsonObject board = doc["board"].to<JsonObject>();
    board["chip"] = "ESP32-S3";
    board["flashSize"] = ESP.getFlashChipSize();
    board["psramSize"] = ESP.getPsramSize();
    board["cpuFreq"] = ESP.getCpuFreqMHz();
    board["mac"] = WiFi.macAddress();

    // Стабильный публичный идентификатор устройства (для облака/привязки).
    // Важно: это НЕ секрет и не должен использоваться как "токен".
    char deviceId[13] = {0}; // 12 hex + '\0'
    uint64_t efuseMac = ESP.getEfuseMac();
    snprintf(deviceId, sizeof(deviceId), "%012llX",
             (unsigned long long)(efuseMac & 0xFFFFFFFFFFFFULL));
    board["deviceId"] = deviceId;

// Попытка прочитать версию фронтенда из файла
#ifdef USE_LITTLEFS
    File versionFile = LittleFS.open("/version.json", "r");
#else
            File versionFile = LittleFS.open("/version.json", "r");
#endif

    if (versionFile) {
      JsonDocument frontendDoc;
      DeserializationError error = deserializeJson(frontendDoc, versionFile);
      versionFile.close();

      if (!error) {
        doc["frontend"] = frontendDoc.as<JsonObject>();
      } else {
        JsonObject frontend = doc["frontend"].to<JsonObject>();
        frontend["error"] = "Failed to parse version.json";
      }
    } else {
      JsonObject frontend = doc["frontend"].to<JsonObject>();
      frontend["buildDate"] = "Unknown";
      frontend["note"] = "version.json not found";
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/process/start - запуск процесса
  server.on("/api/logs/events", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint16_t limit = 100;
    uint32_t since = 0;

    if (request->hasParam("limit")) {
      limit = static_cast<uint16_t>(request->getParam("limit")->value().toInt());
      if (limit > 200) {
        limit = 200;
      }
    }

    if (request->hasParam("since")) {
      since = static_cast<uint32_t>(request->getParam("since")->value().toInt());
    }

    request->send(200, "application/json",
                  Logger::getRecentEventsJson(limit, since));
  });

  server.on("/api/logs/events/clear", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              Logger::clearRecentEvents();
              request->send(200, "application/json", "{\"success\":true}");
            });

  server.on("/api/export", HTTP_GET, [](AsyncWebServerRequest *request) {
    const char* currentLogFile = Logger::getCurrentLogFile();
    String body;
    String filename = "system-events.csv";

    if (currentLogFile && currentLogFile[0]) {
      body = Logger::readLog(currentLogFile);

      const String currentName = currentLogFile;
      const int slashIndex = currentName.lastIndexOf('/');
      if (slashIndex >= 0 && slashIndex + 1 < currentName.length()) {
        filename = currentName.substring(slashIndex + 1);
      } else {
        filename = currentName;
      }
    } else {
      body = Logger::exportRecentEventsCsv();
    }

    AsyncWebServerResponse* response =
        request->beginResponse(200, "text/csv; charset=utf-8", body);
    response->addHeader("Content-Disposition",
                        "attachment; filename=\"" + filename + "\"");
    request->send(response);
  });

  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *request) {
    const std::vector<ProcessListItem> processes = getProcessList();

    JsonDocument doc;
    doc["total"] = processes.size();
    JsonArray processArray = doc["processes"].to<JsonArray>();

    for (const auto& process : processes) {
      JsonObject item = processArray.add<JsonObject>();
      item["id"] = process.id;
      item["type"] = process.type;
      item["startTime"] = process.startTime;
      item["duration"] = process.duration;
      item["status"] = process.status;
      item["totalVolume"] = process.totalVolume;
      item["completionState"] = process.completionState;
      item["completionReasonCode"] = process.completionReasonCode;
      item["completionOperatorMessage"] = process.completionOperatorMessage;
      item["lastPhaseName"] = process.lastPhaseName;
      item["lastReasonCode"] = process.lastReasonCode;
      item["lastOperatorMessage"] = process.lastOperatorMessage;
      item["safetyTrip"] = process.safetyTrip;
      item["safetyAck"] = process.safetyAck;
      item["safetyReset"] = process.safetyReset;
      item["safetyRecovery"] = process.safetyRecovery;
      item["safetyLimited"] = process.safetyLimited;
      item["safetyState"] = process.safetyState;
      item["safetySummary"] = process.safetySummary;
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("^\\/api\\/history\\/([0-9]+)$", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              const String id = request->pathArg(0);
              ProcessHistory history;
              if (!loadProcessHistory(id, history)) {
                request->send(404, "application/json",
                              "{\"error\":\"Process not found\"}");
                return;
              }

              request->send(200, "application/json",
                            exportProcessToJSON(history));
            });

  server.on("^\\/api\\/history\\/([0-9]+)\\/export$", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              const String id = request->pathArg(0);
              const String format = request->hasParam("format")
                                        ? request->getParam("format")->value()
                                        : "csv";

              ProcessHistory history;
              if (!loadProcessHistory(id, history)) {
                request->send(404, "application/json",
                              "{\"error\":\"Process not found\"}");
                return;
              }

              String body;
              String contentType;
              String filename;
              if (format == "json") {
                body = exportProcessToJSON(history);
                contentType = "application/json; charset=utf-8";
                filename = "process_" + id + ".json";
              } else if (format == "csv") {
                body = exportProcessToCSV(history);
                contentType = "text/csv; charset=utf-8";
                filename = "process_" + id + ".csv";
              } else {
                request->send(400, "application/json",
                              "{\"error\":\"Invalid format. Use csv or json\"}");
                return;
              }

              AsyncWebServerResponse* response =
                  request->beginResponse(200, contentType, body);
              response->addHeader(
                  "Content-Disposition",
                  "attachment; filename=\"" + filename + "\"");
              request->send(response);
            });

  server.on("^\\/api\\/history\\/([0-9]+)$", HTTP_DELETE,
            [](AsyncWebServerRequest *request) {
              const String id = request->pathArg(0);
              if (deleteProcess(id)) {
                request->send(200, "application/json",
                              "{\"success\":true,\"message\":\"Process deleted\"}");
                return;
              }

              request->send(404, "application/json",
                            "{\"error\":\"Process not found\"}");
            });

  server.on("/api/history", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    if (clearHistory()) {
      request->send(200, "application/json",
                    "{\"success\":true,\"message\":\"All history cleared\"}");
      return;
    }

    request->send(500, "application/json",
                  "{\"error\":\"Failed to clear history\"}");
  });

  server.on(
      "/api/process/start", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        // Ждем получения всех данных
        if (index + len != total) {
          return;
        }

        // Важно: сюда могут приходить steps/profiles, поэтому нужен запас
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          LOG_E("Process start: JSON parse error");
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Invalid JSON\"}");
          return;
        }

        const char *modeStr = doc["mode"];
        if (!modeStr) {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Mode required\"}");
          return;
        }

        JsonObject params = doc["params"].as<JsonObject>();

        // Определяем режим
        Mode mode = Mode::IDLE;
        if (strcmp(modeStr, "rectification") == 0) {
          mode = Mode::RECTIFICATION;
        } else if (strcmp(modeStr, "distillation") == 0) {
          mode = Mode::DISTILLATION;
        } else if (strcmp(modeStr, "manual") == 0 ||
                   strcmp(modeStr, "manual_rect") == 0) {
          mode = Mode::MANUAL_RECT;
        } else if (strcmp(modeStr, "mashing") == 0) {
          mode = Mode::MASHING;
        } else if (strcmp(modeStr, "hold") == 0) {
          mode = Mode::HOLD;
        } else if (strcmp(modeStr, "nbk") == 0) {
          mode = Mode::NBK;
        } else if (strcmp(modeStr, "fermentation") == 0) {
          mode = Mode::FERMENTATION;
        } else {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Unknown mode\"}");
          return;
        }

        // Проверка термометров (только предупреждение, не блокируем запуск)
        const bool allowDemoSensorFailure =
            g_settings.demoMode &&
            g_state.currentAlarm.type == AlarmType::SENSOR_FAILURE;

        if (Safety::isLatched(g_state) && !allowDemoSensorFailure) {
          Logger::logf(1, "Process start rejected: safety alarm is latched (%s)",
                       g_state.currentAlarm.message[0]
                           ? g_state.currentAlarm.message
                           : Safety::getAlarmTypeToken(g_state.currentAlarm.type));
          JsonDocument errorDoc;
          errorDoc["success"] = false;
          errorDoc["message"] =
              "Safety alarm is latched. Reset the alarm before starting.";
          JsonObject alarm = errorDoc["alarm"].to<JsonObject>();
          fillAlarmJson(alarm, g_state, g_settings);

          String response;
          serializeJson(errorDoc, response);
          request->send(409, "application/json", response);
          return;
        }

        bool sensorsOk =
            g_state.health.tempSensorsTotal > 0 && g_state.health.tempSensorsOk;

        if (!sensorsOk) {
          LOG_W("Starting process without temperature sensors!");
        }

        // Если уже что-то запущено — сначала остановим
        if (g_state.mode != Mode::IDLE) {
          FSM::stopMode(g_state);
        }

        // Запуск через FSM + разбор params (для некоторых режимов)
        if (mode == Mode::DISTILLATION) {
          // params: speed (ml/h), headsVolume (ml), targetVolume (ml), endTemp (°C)
          float speed = params["speed"] | 500.0f;
          float headsVol = params["headsVolume"] | 0.0f;
          float targetVol = params["targetVolume"] | 0.0f;
          float endTemp = params["endTemp"] | 96.0f;
          uint8_t powerPercent = params["powerPercent"] | 60;
          if (powerPercent > 100) powerPercent = 100;
          FSM::Distillation::setParams(speed, headsVol, targetVol, endTemp);
          FSM::Distillation::setPowerPercent(powerPercent);
          FSM::startMode(g_state, g_settings, mode);
        } else if (mode == Mode::MASHING) {
          // params.profile: { name, steps:[{temperature,duration,name?}, ...] }
          static MashProfile runtimeProfile;
          memset(&runtimeProfile, 0, sizeof(runtimeProfile));

          bool hasProfile = false;
          if (!params.isNull() && !params["profile"].isNull()) {
            JsonObject profileObj = params["profile"].as<JsonObject>();
            JsonArray steps = profileObj["steps"].as<JsonArray>();
            if (!profileObj.isNull() && steps.size() > 0) {
              const char *pName = profileObj["name"] | "Mashing";
              strncpy(runtimeProfile.name, pName, sizeof(runtimeProfile.name) - 1);
              runtimeProfile.name[sizeof(runtimeProfile.name) - 1] = '\0';

              uint8_t count = 0;
              for (JsonObject s : steps) {
                if (count >= 10) break;
                runtimeProfile.steps[count].temperature = s["temperature"] | 0.0f;
                runtimeProfile.steps[count].duration = s["duration"] | 0;
                const char *sName = s["name"] | "";
                strncpy(runtimeProfile.steps[count].name, sName,
                        sizeof(runtimeProfile.steps[count].name) - 1);
                runtimeProfile.steps[count].name[sizeof(runtimeProfile.steps[count].name) - 1] = '\0';
                count++;
              }
              runtimeProfile.stepCount = count;
              hasProfile = (count > 0);
            }
          }

          // Если профиль не передан — используем разумный дефолт
          if (!hasProfile) {
            strncpy(runtimeProfile.name, "Default Mashing",
                    sizeof(runtimeProfile.name) - 1);
            runtimeProfile.stepCount = 5;
            // NB: у вложенной структуры нет operator= от initializer_list в GCC,
            // поэтому заполняем поля вручную
            runtimeProfile.steps[0].temperature = 38.0f;
            runtimeProfile.steps[0].duration = 20;
            strncpy(runtimeProfile.steps[0].name, "Кислотная пауза",
                    sizeof(runtimeProfile.steps[0].name) - 1);

            runtimeProfile.steps[1].temperature = 52.0f;
            runtimeProfile.steps[1].duration = 20;
            strncpy(runtimeProfile.steps[1].name, "Белковая пауза",
                    sizeof(runtimeProfile.steps[1].name) - 1);

            runtimeProfile.steps[2].temperature = 63.0f;
            runtimeProfile.steps[2].duration = 40;
            strncpy(runtimeProfile.steps[2].name, "Мальтозная пауза",
                    sizeof(runtimeProfile.steps[2].name) - 1);

            runtimeProfile.steps[3].temperature = 72.0f;
            runtimeProfile.steps[3].duration = 20;
            strncpy(runtimeProfile.steps[3].name, "Осахаривание",
                    sizeof(runtimeProfile.steps[3].name) - 1);

            runtimeProfile.steps[4].temperature = 78.0f;
            runtimeProfile.steps[4].duration = 10;
            strncpy(runtimeProfile.steps[4].name, "Мэш-аут",
                    sizeof(runtimeProfile.steps[4].name) - 1);

            // гарантируем '\0' для всех name
            for (uint8_t i = 0; i < runtimeProfile.stepCount; i++) {
              runtimeProfile.steps[i].name[sizeof(runtimeProfile.steps[i].name) - 1] = '\0';
            }
          }

          FSM::Mashing::start(g_state, &runtimeProfile);
        } else if (mode == Mode::HOLD) {
          // params.steps: [{temperature, duration}, ...] (duration в минутах)
          static TempStep runtimeSteps[10];
          uint8_t count = 0;
          if (!params.isNull() && !params["steps"].isNull()) {
            JsonArray steps = params["steps"].as<JsonArray>();
            for (JsonObject s : steps) {
              if (count >= 10) break;
              runtimeSteps[count].temperature = s["temperature"] | 0.0f;
              runtimeSteps[count].duration = s["duration"] | 0;
              runtimeSteps[count].useCooling = s["useCooling"] | false;
              count++;
            }
          }

          // Дефолт — одна ступень 65°C на 60 минут
          if (count == 0) {
            runtimeSteps[0].temperature = 65.0f;
            runtimeSteps[0].duration = 60;
            runtimeSteps[0].useCooling = false;
            count = 1;
          }

          FSM::Hold::start(g_state, runtimeSteps, count);
        } else if (mode == Mode::NBK || mode == Mode::FERMENTATION) {
          FSM::startMode(g_state, g_settings, mode);
        } else {
          FSM::startMode(g_state, g_settings, mode);
        }

        LOG_I("Process started: mode=%s, sensors=%s", modeStr,
              sensorsOk ? "OK" : "WARNING");

        // Формируем ответ
        String response = "{\"success\":true,\"message\":\"Process started\"";
        if (!sensorsOk) {
          response += ",\"warning\":\"No temperature sensors detected\"";
        }
        response += "}";

        request->send(200, "application/json", response);
      });

  // POST /api/process/stop - остановка процесса
  server.on("/api/process/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    FSM::stopMode(g_state);
    LOG_I("Process stopped via API");
    request->send(200, "application/json",
                  "{\"success\":true,\"message\":\"Process stopped\"}");
  });

  // POST /api/process/pause - пауза
  server.on(
      "/api/process/pause", HTTP_POST, [](AsyncWebServerRequest *request) {
        FSM::pause(g_state);
        ControlV2::updateRuntime(g_state, g_settings);
        LOG_I("Process paused via API");
        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"Process paused\"}");
      });

  // POST /api/process/resume - возобновление
  server.on(
      "/api/process/resume", HTTP_POST, [](AsyncWebServerRequest *request) {
        FSM::resume(g_state);
        ControlV2::updateRuntime(g_state, g_settings);
        LOG_I("Process resumed via API");
        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"Process resumed\"}");
      });

  // ==========================================================================
  // MANUAL CONTROL API (для manual.html)
  // ==========================================================================

  // --------------------------------------------------------------------------
  // CLOUD TUNNEL API (локально, для привязки устройства в облако)
  // --------------------------------------------------------------------------

  // POST /api/cloud/claim - сгенерировать новый PIN для привязки
  server.on("/api/safety/ack", HTTP_POST, [](AsyncWebServerRequest *request) {
    Safety::acknowledge(g_state);
    ControlV2::updateRuntime(g_state, g_settings);

    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Alarm acknowledged";
    JsonObject alarm = doc["alarm"].to<JsonObject>();
    fillAlarmJson(alarm, g_state, g_settings);
    JsonObject v2 = doc["v2"].to<JsonObject>();
    fillSafetyActionV2Json(v2, ControlV2::getLatestModeStatus(),
                           ControlV2::getLatestMetricsSnapshot());

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/api/safety/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    char reason[128] = "";
    const bool ok = Safety::reset(g_state, g_settings, reason, sizeof(reason));
    ControlV2::updateRuntime(g_state, g_settings);

    JsonDocument doc;
    doc["success"] = ok;
    doc["message"] = ok ? "Safety alarm reset" : "Safety reset rejected";
    if (!ok) {
      doc["reason"] = reason;
    }
    JsonObject alarm = doc["alarm"].to<JsonObject>();
    fillAlarmJson(alarm, g_state, g_settings);
    JsonObject v2 = doc["v2"].to<JsonObject>();
    fillSafetyActionV2Json(v2, ControlV2::getLatestModeStatus(),
                           ControlV2::getLatestMetricsSnapshot());

    String response;
    serializeJson(doc, response);
    request->send(ok ? 200 : 409, "application/json", response);
  });

  server.on(
      "/api/cloud/claim", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        uint32_t ttl = 600;
        if (len > 0) {
          JsonDocument doc;
          if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            ttl = doc["ttlSeconds"] | 600;
            if (ttl < 60) ttl = 60;
            if (ttl > 3600) ttl = 3600;
          }
        }

        CloudTunnel::generateClaim(ttl);

        JsonDocument out;
        out["success"] = true;
        out["deviceId"] = CloudTunnel::getDeviceId();
        out["claimCode"] = CloudTunnel::getClaimCode();
        out["claimExpiresAt"] = CloudTunnel::getClaimExpiresAt();

        String json;
        serializeJson(out, json);
        request->send(200, "application/json", json);
      });

  // POST /api/cloud/config - включить/выключить облако и задать URL туннеля
  server.on(
      "/api/cloud/config", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        bool enabled = doc["enabled"] | g_settings.cloud.enabled;
        const char* url = doc["tunnelUrl"] | g_settings.cloud.tunnelUrl;

        g_settings.cloud.enabled = enabled;
        strlcpy(g_settings.cloud.tunnelUrl, url, sizeof(g_settings.cloud.tunnelUrl));

        NVSManager::saveSettings(g_settings);

        request->send(200, "application/json", "{\"success\":true}");
      });

  // --------------------------------------------------------------------------
  // EQUIPMENT SETTINGS API
  // --------------------------------------------------------------------------

  // GET /api/settings/equipment - получить настройки оборудования
  server.on("/api/settings/equipment", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["heaterPowerW"] = g_settings.equipment.heaterPowerW;
    doc["columnHeightMm"] = g_settings.equipment.columnHeightMm;
    doc["cubeVolumeL"] = g_settings.equipment.cubeVolumeL;
    doc["minHeaterSubmergeL"] = g_settings.equipment.minHeaterSubmergeL;
    doc["waterAutoStartCubeTempC"] = g_settings.equipment.waterAutoStartCubeTempC;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/settings/equipment - сохранить настройки оборудования
  server.on(
      "/api/settings/equipment", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        if (!doc["heaterPowerW"].isNull()) {
          g_settings.equipment.heaterPowerW = clampU16Range(
              doc["heaterPowerW"].as<uint32_t>(), 1000, 10000
          );
        }
        if (!doc["columnHeightMm"].isNull()) {
          g_settings.equipment.columnHeightMm = clampU16Range(
              doc["columnHeightMm"].as<uint32_t>(), 500, 3000
          );
        }
        if (!doc["cubeVolumeL"].isNull()) {
          g_settings.equipment.cubeVolumeL = clampFloatRange(
              doc["cubeVolumeL"].as<float>(), 5.0f, 250.0f
          );
        }
        if (!doc["minHeaterSubmergeL"].isNull()) {
          g_settings.equipment.minHeaterSubmergeL = clampFloatRange(
              doc["minHeaterSubmergeL"].as<float>(), 0.5f, 100.0f
          );
        }
        if (!doc["waterAutoStartCubeTempC"].isNull()) {
          g_settings.equipment.waterAutoStartCubeTempC = clampFloatRange(
              doc["waterAutoStartCubeTempC"].as<float>(), 20.0f, 60.0f
          );
        }

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        request->send(200, "application/json", "{\"success\":true}");
      });

  // GET /api/settings/safety - получить пороги аварий
  server.on("/api/settings/safety", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["pressureMaxMmHg"] = g_settings.safety.pressureMaxMmHg;
    doc["tsaMaxC"] = g_settings.safety.tsaMaxC;
    doc["waterOutMaxC"] = g_settings.safety.waterOutMaxC;
    doc["waterOutRiseRateCMin"] = g_settings.safety.waterOutRiseRateCMin;
    doc["pressureRiseRateMmHgMin"] = g_settings.safety.pressureRiseRateMmHgMin;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/settings/safety - сохранить пороги аварий
  server.on(
      "/api/settings/safety", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        if (!doc["pressureMaxMmHg"].isNull()) {
          g_settings.safety.pressureMaxMmHg = clampFloatRange(
              doc["pressureMaxMmHg"].as<float>(), 5.0f, 200.0f
          );
        }
        if (!doc["tsaMaxC"].isNull()) {
          g_settings.safety.tsaMaxC = clampFloatRange(
              doc["tsaMaxC"].as<float>(), 35.0f, 120.0f
          );
        }
        if (!doc["waterOutMaxC"].isNull()) {
          g_settings.safety.waterOutMaxC = clampFloatRange(
              doc["waterOutMaxC"].as<float>(), 30.0f, 120.0f
          );
        }
        if (!doc["waterOutRiseRateCMin"].isNull()) {
          g_settings.safety.waterOutRiseRateCMin = clampFloatRange(
              doc["waterOutRiseRateCMin"].as<float>(), 0.5f, 60.0f
          );
        }
        if (!doc["pressureRiseRateMmHgMin"].isNull()) {
          g_settings.safety.pressureRiseRateMmHgMin = clampFloatRange(
              doc["pressureRiseRateMmHgMin"].as<float>(), 1.0f, 200.0f
          );
        }

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        request->send(200, "application/json", "{\"success\":true}");
      });

  server.on("/api/settings/security", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              JsonDocument doc;
              doc["authEnabled"] = g_settings.security.authEnabled;
              doc["rateLimitEnabled"] = g_settings.security.rateLimitEnabled;
              doc["username"] = g_settings.security.username;
              doc["passwordConfigured"] = (g_settings.security.password[0] != '\0');

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "/api/settings/security", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        const bool authEnabled = !doc["authEnabled"].isNull()
                                     ? doc["authEnabled"].as<bool>()
                                     : g_settings.security.authEnabled;
        const bool rateLimitEnabled = !doc["rateLimitEnabled"].isNull()
                                          ? doc["rateLimitEnabled"].as<bool>()
                                          : g_settings.security.rateLimitEnabled;
        const bool hasUsernameField = !doc["username"].isNull();
        const bool hasPasswordField = !doc["password"].isNull();
        const char *username =
            hasUsernameField ? (doc["username"] | "") : g_settings.security.username;
        const char *password =
            hasPasswordField ? (doc["password"] | "") : nullptr;

        if (authEnabled && (!username || strlen(username) == 0)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Username required\"}");
          return;
        }

        const bool hasStoredPassword = (g_settings.security.password[0] != '\0');
        if (authEnabled && ((!hasStoredPassword && (!password || strlen(password) == 0)))) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Password required to enable auth\"}");
          return;
        }

        g_settings.security.authEnabled = authEnabled;
        g_settings.security.rateLimitEnabled = rateLimitEnabled;
        if (hasUsernameField && username) {
          strlcpy(g_settings.security.username, username,
                  sizeof(g_settings.security.username));
        }
        if (hasPasswordField && password && strlen(password) > 0) {
          strlcpy(g_settings.security.password, password,
                  sizeof(g_settings.security.password));
        }

        if (!NVSManager::saveSettings(g_settings)) {
          Logger::logf(2, "Security settings save failed");
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        applySecuritySettings();
        Logger::logf(0, "Security settings updated: auth=%s, rateLimit=%s, user=%s",
                     authEnabled ? "enabled" : "disabled",
                     rateLimitEnabled ? "enabled" : "disabled",
                     g_settings.security.username);
        request->send(200, "application/json", "{\"success\":true}");
      });

  // GET /api/settings/nbk - ???????? ????????? ???
  server.on("/api/settings/nbk", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["powerW"] = g_settings.nbk.powerW;
    doc["pumpSpeedMlH"] = g_settings.nbk.pumpSpeedMlH;
    doc["columnBottomTempThresholdC"] = g_settings.nbk.columnBottomTempThresholdC;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/settings/nbk - ????????? ????????? ???
  server.on(
      "/api/settings/nbk", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        if (!doc["powerW"].isNull()) {
          g_settings.nbk.powerW = clampFloatRange(doc["powerW"].as<float>(), 500.0f, 5500.0f);
        }
        if (!doc["pumpSpeedMlH"].isNull()) {
          g_settings.nbk.pumpSpeedMlH =
              clampFloatRange(doc["pumpSpeedMlH"].as<float>(), 100.0f, 120000.0f);
        }
        if (!doc["columnBottomTempThresholdC"].isNull()) {
          g_settings.nbk.columnBottomTempThresholdC = clampFloatRange(
              doc["columnBottomTempThresholdC"].as<float>(), 50.0f, 110.0f);
        }

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        request->send(200, "application/json", "{\"success\":true}");
      });

  // GET /api/settings/fermentation - ???????? ????????? ???????????
  server.on("/api/settings/fermentation", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["targetTempC"] = g_settings.fermentation.targetTempC;
    doc["hysteresisC"] = g_settings.fermentation.hysteresisC;
    doc["useHeater"] = g_settings.fermentation.useHeater;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/settings/fermentation - ????????? ????????? ???????????
  server.on(
      "/api/settings/fermentation", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        if (!doc["targetTempC"].isNull()) {
          g_settings.fermentation.targetTempC =
              clampFloatRange(doc["targetTempC"].as<float>(), 5.0f, 45.0f);
        }
        if (!doc["hysteresisC"].isNull()) {
          g_settings.fermentation.hysteresisC =
              clampFloatRange(doc["hysteresisC"].as<float>(), 0.1f, 10.0f);
        }
        if (!doc["useHeater"].isNull()) {
          g_settings.fermentation.useHeater = doc["useHeater"].as<bool>();
        }

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        request->send(200, "application/json", "{\"success\":true}");
      });

  // --------------------------------------------------------------------------
  // MQTT SETTINGS API
  // --------------------------------------------------------------------------

  // GET /api/settings/mqtt - получить настройки MQTT
  server.on("/api/settings/mqtt", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["enabled"] = g_settings.mqtt.enabled;
    doc["server"] = g_settings.mqtt.server;
    doc["port"] = g_settings.mqtt.port;
    doc["username"] = g_settings.mqtt.username;
    doc["password"] = g_settings.mqtt.password;
    doc["baseTopic"] = g_settings.mqtt.baseTopic;
    doc["publishInterval"] = g_settings.mqtt.publishInterval;
    doc["discovery"] = g_settings.mqtt.discovery;
    doc["connected"] = MQTT::isConnected();

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/settings/mqtt - сохранить настройки MQTT
  server.on(
      "/api/settings/mqtt", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        const bool enabled = doc["enabled"] | g_settings.mqtt.enabled;
        const char* server = !doc["server"].isNull()
                                 ? (doc["server"] | "")
                                 : g_settings.mqtt.server;
        uint16_t port = !doc["port"].isNull()
                            ? static_cast<uint16_t>(doc["port"] | g_settings.mqtt.port)
                            : g_settings.mqtt.port;
        const char* username = !doc["username"].isNull()
                                   ? (doc["username"] | "")
                                   : g_settings.mqtt.username;
        const char* password = !doc["password"].isNull()
                                   ? (doc["password"] | "")
                                   : g_settings.mqtt.password;
        const char* baseTopic = !doc["baseTopic"].isNull()
                                    ? (doc["baseTopic"] | "")
                                    : g_settings.mqtt.baseTopic;
        const bool discovery = !doc["discovery"].isNull()
                                   ? static_cast<bool>(doc["discovery"])
                                   : g_settings.mqtt.discovery;
        uint32_t publishInterval = !doc["publishInterval"].isNull()
                                       ? static_cast<uint32_t>(doc["publishInterval"] |
                                                                g_settings.mqtt.publishInterval)
                                       : g_settings.mqtt.publishInterval;

        if (enabled && (!server || server[0] == '\0')) {
          Logger::logf(1, "MQTT settings rejected: server is required when enabled");
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"MQTT server is required when enabled\"}");
          return;
        }
        if (port == 0) port = 1883;
        if (!baseTopic || baseTopic[0] == '\0') baseTopic = "smart-column";
        if (publishInterval < 1000) publishInterval = 1000;
        if (publishInterval > 60000) publishInterval = 60000;

        g_settings.mqtt.enabled = enabled;
        strlcpy(g_settings.mqtt.server, server, sizeof(g_settings.mqtt.server));
        g_settings.mqtt.port = port;
        strlcpy(g_settings.mqtt.username, username, sizeof(g_settings.mqtt.username));
        strlcpy(g_settings.mqtt.password, password, sizeof(g_settings.mqtt.password));
        strlcpy(g_settings.mqtt.baseTopic, baseTopic, sizeof(g_settings.mqtt.baseTopic));
        g_settings.mqtt.publishInterval = publishInterval;
        g_settings.mqtt.discovery = discovery;

        if (!NVSManager::saveSettings(g_settings)) {
          Logger::logf(2, "MQTT settings save failed");
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        request->send(200, "application/json", "{\"success\":true}");
        Logger::logf(
            0,
            "MQTT settings updated: %s, server=%s:%u, topic=%s, interval=%lums",
            g_settings.mqtt.enabled ? "enabled" : "disabled",
            g_settings.mqtt.server[0] ? g_settings.mqtt.server : "-",
            g_settings.mqtt.port,
            g_settings.mqtt.baseTopic,
            static_cast<unsigned long>(g_settings.mqtt.publishInterval));

        // Применяем runtime-настройки после ответа
        MQTT::disconnect();
        if (g_settings.mqtt.enabled && g_settings.mqtt.server[0] != '\0') {
          MQTT::setBaseTopic(g_settings.mqtt.baseTopic);
          MQTT::init(g_settings.mqtt.server, g_settings.mqtt.port,
                     g_settings.mqtt.username[0] ? g_settings.mqtt.username : nullptr,
                     g_settings.mqtt.password[0] ? g_settings.mqtt.password : nullptr);
          if (WiFi.status() == WL_CONNECTED) {
            MQTT::handle();
          }
        }
      });

  // POST /api/settings/mqtt/test - отправить тестовое MQTT уведомление
  server.on(
      "/api/settings/mqtt/test", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        if (!g_settings.mqtt.enabled) {
          Logger::logf(1, "MQTT test rejected: MQTT is disabled");
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"MQTT disabled\"}");
          return;
        }
        if (g_settings.mqtt.server[0] == '\0') {
          Logger::logf(1, "MQTT test rejected: broker is not configured");
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"MQTT server is not configured\"}");
          return;
        }
        if (WiFi.status() != WL_CONNECTED) {
          Logger::logf(1, "MQTT test rejected: WiFi STA is not connected");
          request->send(503, "application/json",
                        "{\"success\":false,\"error\":\"WiFi STA not connected\"}");
          return;
        }

        JsonDocument doc;
        if (len > 0 && deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }
        const char* message = doc["message"] | "Smart-Column S3: MQTT test from Web UI";

        // Убеждаемся, что MQTT клиент инициализирован и подключен
        if (!MQTT::isConnected()) {
          MQTT::setBaseTopic(g_settings.mqtt.baseTopic);
          MQTT::init(g_settings.mqtt.server, g_settings.mqtt.port,
                     g_settings.mqtt.username[0] ? g_settings.mqtt.username : nullptr,
                     g_settings.mqtt.password[0] ? g_settings.mqtt.password : nullptr);
          for (uint8_t i = 0; i < 20 && !MQTT::isConnected(); ++i) {
            MQTT::handle();
            delay(50);
          }
        }

        if (!MQTT::isConnected()) {
          Logger::logf(1, "MQTT test failed: broker unavailable");
          request->send(503, "application/json",
                        "{\"success\":false,\"error\":\"MQTT broker unavailable\"}");
          return;
        }

        MQTT::publishNotification("MQTT test", message, "info");
        Logger::logf(0, "MQTT test notification sent");
        request->send(200, "application/json", "{\"success\":true}");
      });

  // GET /api/settings/rect - get auto-rectification startup parameters
  server.on("/api/settings/rect", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    const RectParams &params = g_settings.rectParams;

    doc["feedstock"] = params.feedstock;
    doc["feedVolumeL"] = params.feedVolumeL;
    doc["feedAbvPercent"] = params.feedAbvPercent;
    doc["headsPercent"] = params.headsPercent;
    doc["bodyPercent"] = params.bodyPercent;
    doc["tailsPercent"] = params.tailsPercent;
    doc["headsSpeedMlHKw"] = params.headsSpeedMlHKw;
    doc["bodySpeedMlHKw"] = params.bodySpeedMlHKw;
    doc["stabilizationMin"] = params.stabilizationMin;
    doc["purgeMin"] = params.purgeMin;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/settings/rect - save auto-rectification startup parameters
  server.on(
      "/api/settings/rect", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        JsonObject root = doc.as<JsonObject>();
        JsonObject params = root["params"].is<JsonObject>()
                                ? root["params"].as<JsonObject>()
                                : root;

        RectParams updated = g_settings.rectParams;
        bool fractionsUpdated = false;

        if (!params["feedstock"].isNull()) {
          int feedstock = params["feedstock"].as<int>();
          if (feedstock < 0) feedstock = 0;
          if (feedstock > 7) feedstock = 7;
          updated.feedstock = static_cast<uint8_t>(feedstock);
        }

        bool applyFeedstockDefaults = params["applyFeedstockDefaults"] | false;
        if (applyFeedstockDefaults) {
          getRectFeedstockDefaults(updated.feedstock, updated.headsPercent,
                                   updated.bodyPercent, updated.tailsPercent);
          fractionsUpdated = true;
        }

        if (!params["feedVolumeL"].isNull()) {
          updated.feedVolumeL =
              clampFloatRange(params["feedVolumeL"].as<float>(), 1.0f, 250.0f);
        }
        if (!params["feedAbvPercent"].isNull()) {
          updated.feedAbvPercent = clampFloatRange(
              params["feedAbvPercent"].as<float>(), 1.0f, 96.0f);
        }
        if (!params["headsPercent"].isNull()) {
          updated.headsPercent =
              clampFloatRange(params["headsPercent"].as<float>(), 0.0f, 40.0f);
          fractionsUpdated = true;
        }
        if (!params["bodyPercent"].isNull()) {
          updated.bodyPercent =
              clampFloatRange(params["bodyPercent"].as<float>(), 0.0f, 100.0f);
          fractionsUpdated = true;
        }
        if (!params["tailsPercent"].isNull()) {
          updated.tailsPercent =
              clampFloatRange(params["tailsPercent"].as<float>(), 0.0f, 100.0f);
          fractionsUpdated = true;
        }
        if (!params["headsSpeedMlHKw"].isNull()) {
          updated.headsSpeedMlHKw = clampFloatRange(
              params["headsSpeedMlHKw"].as<float>(), 10.0f, 2000.0f);
        }
        if (!params["bodySpeedMlHKw"].isNull()) {
          updated.bodySpeedMlHKw = clampFloatRange(
              params["bodySpeedMlHKw"].as<float>(), 50.0f, 3000.0f);
        }
        if (!params["stabilizationMin"].isNull()) {
          updated.stabilizationMin = clampU16Range(
              params["stabilizationMin"].as<uint32_t>(), 1, 180);
        }
        if (!params["purgeMin"].isNull()) {
          updated.purgeMin =
              clampU16Range(params["purgeMin"].as<uint32_t>(), 1, 120);
        }

        if (fractionsUpdated) {
          normalizeRectFractions(updated);
        }

        g_settings.rectParams = updated;
        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        JsonDocument out;
        out["success"] = true;
        out["feedstock"] = g_settings.rectParams.feedstock;
        out["feedVolumeL"] = g_settings.rectParams.feedVolumeL;
        out["feedAbvPercent"] = g_settings.rectParams.feedAbvPercent;
        out["headsPercent"] = g_settings.rectParams.headsPercent;
        out["bodyPercent"] = g_settings.rectParams.bodyPercent;
        out["tailsPercent"] = g_settings.rectParams.tailsPercent;
        out["headsSpeedMlHKw"] = g_settings.rectParams.headsSpeedMlHKw;
        out["bodySpeedMlHKw"] = g_settings.rectParams.bodySpeedMlHKw;
        out["stabilizationMin"] = g_settings.rectParams.stabilizationMin;
        out["purgeMin"] = g_settings.rectParams.purgeMin;

        String json;
        serializeJson(out, json);
        request->send(200, "application/json", json);
      });

  // POST /api/manual/heater - установить мощность ТЭНа (0-100%)
  server.on(
      "/api/manual/heater", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
          return;
        }

        int power = doc["power"] | 0;
        if (power < 0) power = 0;
        if (power > 100) power = 100;
        Heater::setPower((uint8_t)power);

        char resp[96];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"power\":%d}", power);
        request->send(200, "application/json", resp);
      });

  // POST /api/rect/heater - override мощности ТЭНа в авто-ректификации (0-100%)
  // power=-1 снимает override и возвращает управление WattControl
  server.on(
      "/api/rect/heater", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
          return;
        }

        int power = doc["power"] | -2;
        if (power < -1) power = -1;
        if (power > 100) power = 100;
        WattControl::setOverride((int8_t)power);

        char resp[96];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"power\":%d}", power);
        request->send(200, "application/json", resp);
      });

  // POST /api/manual/pump - установить скорость насоса (мл/ч; 0 = стоп)
  server.on(
      "/api/manual/pump", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
          return;
        }

        float speed = doc["speed"] | 0.0f;
        if (speed <= 0.0f) {
          Pump::stop();
          request->send(200, "application/json",
                        "{\"success\":true,\"running\":false}");
          return;
        }

        Pump::start(speed);
        char resp[128];
        snprintf(resp, sizeof(resp),
                 "{\"success\":true,\"running\":true,\"speed\":%.1f}", speed);
        request->send(200, "application/json", resp);
      });

  // POST /api/manual/valves - управление клапанами
  // body: { "water":true/false, "heads":true/false, "uno":true/false, "allOff":true }
  server.on(
      "/api/manual/valves", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
          return;
        }

        bool allOff = doc["allOff"] | false;
        if (allOff) {
          Valves::closeAll();
          request->send(200, "application/json", "{\"success\":true}");
          return;
        }

        if (!doc["water"].isNull()) {
          Valves::setWater(doc["water"].as<bool>());
        }
        if (!doc["heads"].isNull()) {
          Valves::setHeads(doc["heads"].as<bool>());
        }
        if (!doc["uno"].isNull()) {
          Valves::setUno(doc["uno"].as<bool>());
        }

        request->send(200, "application/json", "{\"success\":true}");
      });

  // POST /api/manual/phase - переключение фазы в ручном режиме
  // body: { "phase": 3 } (где 3 = HEADS, 5 = BODY, 6 = TAILS)
  server.on(
      "/api/manual/phase", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
          return;
        }

        if (g_state.mode != Mode::MANUAL_RECT) {
          request->send(400, "application/json", "{\"error\":\"Not in MANUAL_RECT mode\"}");
          return;
        }

        if (!!doc["phase"].isNull()) {
          request->send(400, "application/json", "{\"error\":\"Missing phase\"}");
          return;
        }

        uint8_t phaseId = doc["phase"].as<uint8_t>();
        
        // Маппим фазу и вызываем FSM
        FSM::ManualRect::setPhase(g_state, static_cast<RectPhase>(phaseId));

        char resp[128];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"phase\":%d}", (int)phaseId);
        request->send(200, "application/json", resp);
      });

  // POST /api/manual/volumes - ручная корректировка отображаемых объёмов фракций
  // body: { "heads": 100, "body": 2500, "tails": 120, "syncTotal": true }
  server.on(
      "/api/manual/volumes", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
          return;
        }

        bool changed = false;
        if (!doc["heads"].isNull()) {
          const float v = doc["heads"].as<float>();
          g_state.stats.headsVolume = (v < 0.0f) ? 0.0f : v;
          changed = true;
        }
        if (!doc["body"].isNull()) {
          const float v = doc["body"].as<float>();
          g_state.stats.bodyVolume = (v < 0.0f) ? 0.0f : v;
          changed = true;
        }
        if (!doc["tails"].isNull()) {
          const float v = doc["tails"].as<float>();
          g_state.stats.tailsVolume = (v < 0.0f) ? 0.0f : v;
          changed = true;
        }

        if (!changed) {
          request->send(400, "application/json",
                        "{\"error\":\"At least one field is required\"}");
          return;
        }

        const bool syncTotal = doc["syncTotal"] | true;
        if (syncTotal) {
          g_state.pump.totalVolumeMl =
              g_state.stats.headsVolume + g_state.stats.bodyVolume + g_state.stats.tailsVolume;
        }

        JsonDocument out;
        out["success"] = true;
        out["heads"] = g_state.stats.headsVolume;
        out["body"] = g_state.stats.bodyVolume;
        out["tails"] = g_state.stats.tailsVolume;
        out["totalMl"] = g_state.pump.totalVolumeMl;
        String json;
        serializeJson(out, json);
        request->send(200, "application/json", json);
      });

  // ==========================================================================
  // ДЕМО-РЕЖИМ
  // ==========================================================================

  // POST /api/settings/demo - включить/выключить демо-режим
  server.on(
      "/api/settings/demo", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Invalid JSON\"}");
          return;
        }

        bool enabled = doc["enabled"] | false;
        g_settings.demoMode = enabled;

        LOG_I("Demo mode %s", enabled ? "ENABLED" : "DISABLED");
        Logger::logf(0, "Demo mode %s", enabled ? "enabled" : "disabled");

        // Сохраняем в NVS
        Preferences prefs;
        prefs.begin("settings", false);
        prefs.putBool("demoMode", enabled);
        prefs.end();

        char response[128];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"demoMode\":%s}",
                 enabled ? "true" : "false");
        request->send(200, "application/json", response);
      });

  // GET /api/settings/demo - получить состояние демо-режима
  server.on("/api/settings/demo", HTTP_GET, [](AsyncWebServerRequest *request) {
    char response[64];
    snprintf(response, sizeof(response), "{\"demoMode\":%s}",
             g_settings.demoMode ? "true" : "false");
    request->send(200, "application/json", response);
  });

  // ==========================================================================
  // ПЕРЕЗАГРУЗКА
  // ==========================================================================

  // GET /api/reboot/status - получить информацию о последней перезагрузке
  server.on("/api/reboot/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["lastReason"] = g_rebootTracker.lastReason;
    doc["lastReasonStr"] = g_rebootTracker.lastReasonStr;
    doc["totalReboots"] = g_rebootTracker.totalReboots;
    doc["wdtReboots"] = g_rebootTracker.wdtReboots;
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/reboot - перезагрузка контроллера
  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    LOG_W("Reboot requested via API");
    Logger::logf(1, "System reboot requested via API");
    request->send(200, "application/json",
                  "{\"success\":true,\"message\":\"Rebooting...\"}");

    // Небольшая задержка чтобы успеть отправить ответ
    delay(500);
    ESP.restart();
  });

  // ==========================================================================
  // КАЛИБРОВКА
  // ==========================================================================

  // GET /api/calibration - получить все данные калибровки
  server.on("/api/calibration", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;

    // Насос
    JsonObject pump = doc["pump"].to<JsonObject>();
    pump["mlPerRev"] = g_settings.pumpCal.mlPerRevolution;
    pump["stepsPerRev"] = g_settings.pumpCal.stepsPerRevolution;
    pump["microsteps"] = g_settings.pumpCal.microsteps;

    // Термометры
    JsonArray temps = doc["temperatures"].to<JsonArray>();
    for (uint8_t i = 0; i < TEMP_COUNT; i++) {
      JsonObject t = temps.add<JsonObject>();
      t["index"] = i;
      t["offset"] = g_settings.tempCal.offsets[i];

      // Адрес датчика (hex string)
      char addrStr[24];
      snprintf(addrStr, sizeof(addrStr),
               "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
               g_settings.tempCal.addresses[i][0],
               g_settings.tempCal.addresses[i][1],
               g_settings.tempCal.addresses[i][2],
               g_settings.tempCal.addresses[i][3],
               g_settings.tempCal.addresses[i][4],
               g_settings.tempCal.addresses[i][5],
               g_settings.tempCal.addresses[i][6],
               g_settings.tempCal.addresses[i][7]);
      t["address"] = addrStr;

      // Текущие показания
      float currentTemp = 0;
      switch (i) {
      case TEMP_CUBE:
        currentTemp = g_state.temps.cube;
        break;
      case TEMP_COLUMN_BOTTOM:
        currentTemp = g_state.temps.columnBottom;
        break;
      case TEMP_COLUMN_TOP:
        currentTemp = g_state.temps.columnTop;
        break;
      case TEMP_REFLUX:
        currentTemp = g_state.temps.reflux;
        break;
      case TEMP_TSA:
        currentTemp = g_state.temps.tsa;
        break;
      case TEMP_WATER_IN:
        currentTemp = g_state.temps.waterIn;
        break;
      case TEMP_WATER_OUT:
        currentTemp = g_state.temps.waterOut;
        break;
      }
      t["current"] = currentTemp;
      t["valid"] = g_state.temps.valid[i];
    }

    // Ареометр (гидрометр)
    JsonObject hydro = doc["hydrometer"].to<JsonObject>();
    hydro["pointCount"] = g_settings.hydroCal.pointCount;
    JsonArray abvPoints = hydro["abvPoints"].to<JsonArray>();
    JsonArray pressurePoints = hydro["pressurePoints"].to<JsonArray>();
    for (uint8_t i = 0; i < g_settings.hydroCal.pointCount; i++) {
      abvPoints.add(g_settings.hydroCal.abvPoints[i]);
      pressurePoints.add(g_settings.hydroCal.pressurePoints[i]);
    }
    // Текущие показания
    hydro["currentPressure"] = g_state.hydrometer.pressure;
    hydro["currentABV"] = g_state.hydrometer.abv;
    hydro["valid"] = g_state.hydrometer.valid;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/calibration/pump - калибровка насоса
  server.on(
      "/api/calibration/pump", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid JSON\"}");
          return;
        }

        // Метод 1: Прямая калибровка (мл на оборот и шаги)
        if (!doc["mlPerRev"].isNull() || !doc["stepsPerRev"].isNull()) {
          if (!doc["mlPerRev"].isNull()) {
            g_settings.pumpCal.mlPerRevolution = doc["mlPerRev"].as<float>();
            LOG_I("Pump mlPerRev: %.3f", g_settings.pumpCal.mlPerRevolution);
          }
          if (!doc["stepsPerRev"].isNull()) {
            g_settings.pumpCal.stepsPerRevolution =
                doc["stepsPerRev"].as<uint16_t>();
            LOG_I("Pump stepsPerRev: %u",
                  g_settings.pumpCal.stepsPerRevolution);
          }
          NVSManager::saveSettings(g_settings);
          request->send(200, "application/json",
                        "{\"status\":\"ok\",\"method\":\"direct\"}");
          return;
        }

        // Метод 2: Калибровка по известному объёму
        if (!doc["knownVolume"].isNull() && !doc["steps"].isNull()) {
          float knownVolume = doc["knownVolume"].as<float>(); // мл
          uint32_t steps = doc["steps"].as<uint32_t>();       // шагов выполнено

          uint16_t stepsPerRev = g_settings.pumpCal.stepsPerRevolution *
                                 g_settings.pumpCal.microsteps;
          float revolutions = (float)steps / stepsPerRev;

          if (revolutions > 0) {
            g_settings.pumpCal.mlPerRevolution = knownVolume / revolutions;
            NVSManager::saveSettings(g_settings);

            LOG_I("Pump calibrated: %.3f ml/rev (from %.1f ml in %u steps)",
                  g_settings.pumpCal.mlPerRevolution, knownVolume, steps);

            JsonDocument resp;
            resp["status"] = "ok";
            resp["method"] = "measured";
            resp["mlPerRev"] = g_settings.pumpCal.mlPerRevolution;

            String json;
            serializeJson(resp, json);
            request->send(200, "application/json", json);
          } else {
            request->send(400, "application/json",
                          "{\"error\":\"Invalid steps\"}");
          }
          return;
        }

        request->send(400, "application/json",
                      "{\"error\":\"Missing parameters\"}");
      });

  // POST /api/calibration/temp - калибровка термометров
  server.on(
      "/api/calibration/temp", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid JSON\"}");
          return;
        }

        uint8_t sensorIndex = doc["index"].as<uint8_t>();

        if (sensorIndex >= TEMP_COUNT) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid sensor index\"}");
          return;
        }

        // Метод 1: Прямое смещение
        if (!doc["offset"].isNull()) {
          g_settings.tempCal.offsets[sensorIndex] = doc["offset"].as<float>();

          // Применить калибровку к драйверу
          Sensors::applyCalibration(g_settings.tempCal);
          NVSManager::saveSettings(g_settings);

          LOG_I("Temp[%d] calibrated: offset = %.2f°C", sensorIndex,
                g_settings.tempCal.offsets[sensorIndex]);

          request->send(200, "application/json",
                        "{\"status\":\"ok\",\"method\":\"offset\"}");
          return;
        }

        // Метод 2: Калибровка по эталону
        if (!doc["reference"].isNull()) {
          float reference =
              doc["reference"].as<float>(); // Эталонная температура

          // Прочитать текущее значение
          float currentTemp = 0;
          switch (sensorIndex) {
          case TEMP_CUBE:
            currentTemp = g_state.temps.cube;
            break;
          case TEMP_COLUMN_BOTTOM:
            currentTemp = g_state.temps.columnBottom;
            break;
          case TEMP_COLUMN_TOP:
            currentTemp = g_state.temps.columnTop;
            break;
          case TEMP_REFLUX:
            currentTemp = g_state.temps.reflux;
            break;
          case TEMP_TSA:
            currentTemp = g_state.temps.tsa;
            break;
          case TEMP_WATER_IN:
            currentTemp = g_state.temps.waterIn;
            break;
          case TEMP_WATER_OUT:
            currentTemp = g_state.temps.waterOut;
            break;
          }

          // Вычислить смещение (без учёта старого смещения)
          float rawTemp = currentTemp - g_settings.tempCal.offsets[sensorIndex];
          g_settings.tempCal.offsets[sensorIndex] = reference - rawTemp;

          Sensors::applyCalibration(g_settings.tempCal);
          NVSManager::saveSettings(g_settings);

          LOG_I("Temp[%d] calibrated to %.2f°C: offset = %.2f°C", sensorIndex,
                reference, g_settings.tempCal.offsets[sensorIndex]);

          JsonDocument resp;
          resp["status"] = "ok";
          resp["method"] = "reference";
          resp["offset"] = g_settings.tempCal.offsets[sensorIndex];

          String json;
          serializeJson(resp, json);
          request->send(200, "application/json", json);
          return;
        }

        request->send(400, "application/json",
                      "{\"error\":\"Missing parameters\"}");
      });

  // POST /api/calibration/hydrometer - калибровка ареометра
  server.on(
      "/api/calibration/hydrometer", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid JSON\"}");
          return;
        }

        // Проверка наличия массивов калибровочных точек
        if (!!doc["abvPoints"].isNull() ||
            !!doc["pressurePoints"].isNull()) {
          request->send(400, "application/json",
                        "{\"error\":\"Missing abvPoints or pressurePoints\"}");
          return;
        }

        JsonArray abvArray = doc["abvPoints"].as<JsonArray>();
        JsonArray pressureArray = doc["pressurePoints"].as<JsonArray>();

        if (abvArray.size() != pressureArray.size() || abvArray.size() > 5) {
          request->send(
              400, "application/json",
              "{\"error\":\"Invalid point count (max 5, must match)\"}");
          return;
        }

        // Сохранение калибровочных точек
        g_settings.hydroCal.pointCount = abvArray.size();
        for (uint8_t i = 0; i < g_settings.hydroCal.pointCount; i++) {
          g_settings.hydroCal.abvPoints[i] = abvArray[i].as<float>();
          g_settings.hydroCal.pressurePoints[i] = pressureArray[i].as<float>();
        }

        // Сохранить в NVS
        NVSManager::saveSettings(g_settings);

        JsonDocument resp;
        resp["status"] = "ok";
        resp["pointCount"] = g_settings.hydroCal.pointCount;

        String json;
        serializeJson(resp, json);
        request->send(200, "application/json", json);
      });

  // GET /api/calibration/scan - сканирование DS18B20
  server.on("/api/calibration/scan", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              uint8_t addresses[TEMP_COUNT][8];
              uint8_t count = Sensors::scanDS18B20(addresses);

              JsonDocument doc;
              doc["count"] = count;

              JsonArray sensors = doc["sensors"].to<JsonArray>();
              for (uint8_t i = 0; i < count; i++) {
                JsonObject s = sensors.add<JsonObject>();

                char addrStr[24];
                snprintf(addrStr, sizeof(addrStr),
                         "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                         addresses[i][0], addresses[i][1], addresses[i][2],
                         addresses[i][3], addresses[i][4], addresses[i][5],
                         addresses[i][6], addresses[i][7]);

                s["index"] = i;
                s["address"] = addrStr;
                s["valid"] = Sensors::isTempSensorValid(i);
              }

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  // ==========================================================================
  // PUMP CONTROL (для калибровки)
  // ==========================================================================

  // POST /api/pump/calibrate/start - запуск калибровки насоса
  server.on("/api/pump/calibrate/start", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (g_state.mode != Mode::IDLE) {
                request->send(
                    409, "application/json",
                    "{\"success\":false,\"message\":\"Process is running\"}");
                return;
              }
              if (g_pumpCalSession.active) {
                request->send(
                    409, "application/json",
                    "{\"success\":false,\"message\":\"Calibration already active\"}");
                return;
              }

              Pump::resetVolume();
              g_pumpCalSession.active = true;
              g_pumpCalSession.startSteps = Pump::getTotalSteps();
              g_pumpCalSession.stopSteps = 0;
              g_pumpCalSession.startMs = millis();
              g_pumpCalSession.stopMs = 0;

              float speed = Pump::getMaxSpeedMlH();
              if (request->hasParam("speed")) {
                  float reqSpeed = request->getParam("speed")->value().toFloat();
                  if (reqSpeed > 0) {
                      speed = reqSpeed;
                  }
              }
              Pump::start(speed);
              LOG_I("Pump calibration started at %.1f ml/h", speed);

              request->send(200, "application/json",
                            "{\"success\":true,\"running\":true}");
            });

  // POST /api/pump/calibrate/stop - остановка калибровки насоса
  server.on("/api/pump/calibrate/stop", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (!g_pumpCalSession.active) {
                request->send(
                    409, "application/json",
                    "{\"success\":false,\"message\":\"Calibration not active\"}");
                return;
              }

              Pump::stop();
              g_pumpCalSession.stopSteps = Pump::getTotalSteps();
              g_pumpCalSession.stopMs = millis();
              LOG_I("Pump calibration stopped: %u steps",
                    g_pumpCalSession.stopSteps);

              request->send(200, "application/json",
                            "{\"success\":true,\"running\":false}");
            });

  // POST /api/pump/calibrate/cancel - отмена калибровки (сброс флага active)
  server.on("/api/pump/calibrate/cancel", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              Pump::stop();
              g_pumpCalSession.active = false;
              LOG_I("Pump calibration cancelled and reset");

              request->send(200, "application/json",
                            "{\"success\":true,\"active\":false}");
            });

  // POST /api/pump/calibrate/finish - завершение калибровки насоса
  server.on(
      "/api/pump/calibrate/finish", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) {
          return;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);
        if (error) {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Invalid JSON\"}");
          return;
        }

        if (!g_pumpCalSession.active) {
          request->send(
              409, "application/json",
              "{\"success\":false,\"message\":\"Calibration not active\"}");
          return;
        }

        float volume = doc["volume"] | 0.0f;
        if (volume <= 0.0f) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Volume must be > 0\"}");
          return;
        }

        if (Pump::isRunning()) {
          Pump::stop();
        }
        if (g_pumpCalSession.stopSteps == 0) {
          g_pumpCalSession.stopSteps = Pump::getTotalSteps();
          g_pumpCalSession.stopMs = millis();
        }

        const uint32_t steps = g_pumpCalSession.stopSteps -
                               g_pumpCalSession.startSteps;
        const uint32_t stepsPerRev =
            (uint32_t)PUMP_STEPS_PER_REV * (uint32_t)PUMP_MICROSTEPS;

        if (steps == 0 || stepsPerRev == 0) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"No steps captured\"}");
          return;
        }

        const float revolutions = (float)steps / (float)stepsPerRev;
        const float mlPerRev = volume / revolutions;

        if (mlPerRev <= 0.0f) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Invalid calibration result\"}");
          return;
        }

        g_settings.pumpCal.mlPerRevolution = mlPerRev;
        NVSManager::saveSettings(g_settings);
        Pump::setCalibration(mlPerRev);

        const uint32_t elapsedMs =
            (g_pumpCalSession.stopMs > g_pumpCalSession.startMs)
                ? (g_pumpCalSession.stopMs - g_pumpCalSession.startMs)
                : 30000;
        const float elapsedSec = elapsedMs / 1000.0f;
        const float calibrationFactor =
            (elapsedSec > 0.0f) ? (volume / elapsedSec) : 0.0f;

        g_pumpCalSession.active = false;

        char response[160];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"mlPerRev\":%.4f,\"calibrationFactor\":%.3f}",
                 mlPerRev, calibrationFactor);
        request->send(200, "application/json", response);
      });

  // POST /api/pump/start - запуск насоса
  server.on(
      "/api/pump/start", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) {
          return;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        float speed = doc["speed"] | 0.0f;

        if (speed <= 0) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Speed must be > 0\"}");
          return;
        }

        Pump::start(speed);
        LOG_I("Pump started via API at %.1f ml/h", speed);

        char response[128];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"speed\":%.1f}", speed);
        request->send(200, "application/json", response);
      });

  // POST /api/pump/stop - остановка насоса
  server.on("/api/pump/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    Pump::stop();
    LOG_I("Pump stopped via API");
    request->send(200, "application/json",
                  "{\"success\":true,\"message\":\"Pump stopped\"}");
  });

  // GET /api/pump/status - статус насоса
  server.on("/api/pump/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["running"] = Pump::isRunning();
    doc["speed"] = Pump::getSpeed();
    doc["totalVolume"] = Pump::getTotalVolume();

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // GET /api/pump/diag - расширенная диагностика worker-task насоса
  server.on("/api/pump/diag", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    Pump::Diagnostics diag = Pump::getDiagnostics();
    doc["running"] = Pump::isRunning();
    doc["taskAlive"] = diag.taskAlive;
    doc["mutexReady"] = diag.mutexReady;
    doc["speedMlH"] = diag.speedMlH;
    doc["appliedSpeedMlH"] = diag.appliedSpeedMlH;
    doc["totalSteps"] = diag.totalSteps;
    doc["totalVolumeMl"] = diag.totalVolumeMl;
    doc["taskLoopCount"] = diag.taskLoopCount;
    doc["counterUpdateCount"] = diag.counterUpdateCount;
    doc["cooperativeSleepCount"] = diag.cooperativeSleepCount;
    doc["fastYieldCount"] = diag.fastYieldCount;
    doc["lockTimeoutCount"] = diag.lockTimeoutCount;
    doc["lastLoopAtMs"] = diag.lastLoopAtMs;
    doc["lastYieldAtMs"] = diag.lastYieldAtMs;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // ==========================================================================
  // EQUIPMENT TESTING
  // ==========================================================================

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
            snprintf(detail, sizeof(detail), "Насос запущен вручную со скоростью %.1f мл/ч.", speed);
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

            int power = doc["powerPercent"] | 0;
            if (power < 1) power = 1;
            if (power > 100) power = 100;
            Heater::setPower((uint8_t)power);
            char detail[128];
            snprintf(detail, sizeof(detail),
                     "ТЭН включен вручную на %d%% мощности после подтверждения.", power);
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
               snprintf(detail, sizeof(detail),
                        "Клапан воды дал импульс %lu мс.",
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
               snprintf(detail, sizeof(detail),
                        "Клапан голов дал импульс %lu мс.",
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
          if (angles.isNull() || enabled.isNull() || angles.size() != FRACTION_COUNT ||
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
              g_settings.fractionator.angles[i] = (uint16_t)angle;
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
            Valves::setFractionAngle((uint8_t)angle);
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

  // ==========================================================================
  // ENERGY CONSUMPTION GRAPH
  // ==========================================================================

  // GET /api/energy - получить историю энергопотребления
  server.on("/api/energy", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Размер JSON зависит от количества точек
    size_t docSize = 2048 + g_energyHistory.count * 128;
    JsonDocument doc;

    doc["count"] = g_energyHistory.count;
    doc["maxPoints"] = EnergyHistory::MAX_POINTS;
    doc["lastUpdate"] = g_energyHistory.lastUpdate;

    JsonArray dataArray = doc["data"].to<JsonArray>();

    // Прочитать данные из циклического буфера в правильном порядке
    for (uint16_t i = 0; i < g_energyHistory.count; i++) {
      // Индекс: начинаем с самой старой записи
      uint16_t index;
      if (g_energyHistory.count < EnergyHistory::MAX_POINTS) {
        // Буфер ещё не заполнен - читаем с начала
        index = i;
      } else {
        // Буфер заполнен - читаем с позиции writeIndex (самая старая)
        index = (g_energyHistory.writeIndex + i) % EnergyHistory::MAX_POINTS;
      }

      const EnergyDataPoint &point = g_energyHistory.points[index];

      JsonObject obj = dataArray.add<JsonObject>();
      obj["t"] = point.timestamp;
      obj["p"] = round(point.power * 10) / 10;      // 1 знак после запятой
      obj["e"] = round(point.energy * 1000) / 1000; // 3 знака
      obj["v"] = round(point.voltage * 10) / 10;
      obj["i"] = round(point.current * 100) / 100; // 2 знака
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // ==========================================================================
  // WIFI MANAGEMENT
  // ==========================================================================

  // GET /api/wifi/scan - сканирование доступных сетей
  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    LOG_I("WiFi: Scanning networks...");

    int networksFound = WiFi.scanNetworks();

    JsonDocument doc;
    doc["count"] = networksFound;

    JsonArray networks = doc["networks"].to<JsonArray>();
    for (int i = 0; i < networksFound; i++) {
      JsonObject net = networks.add<JsonObject>();
      net["ssid"] = WiFi.SSID(i);
      net["rssi"] = WiFi.RSSI(i);
      net["encryption"] =
          (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "secured";
      net["channel"] = WiFi.channel(i);
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);

    WiFi.scanDelete(); // Очистить результаты сканирования
  });

  // GET /api/wifi/status - текущий статус WiFi
  server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;

    doc["connected"] = (WiFi.status() == WL_CONNECTED);
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["apMode"] = g_settings.wifi.apMode;
    doc["wifiConfigured"] = hasConfiguredWiFi();
    doc["savedProfiles"] = g_settings.wifi.profileCount;

    if (g_settings.wifi.apMode) {
      doc["apSSID"] = WIFI_AP_SSID;
      doc["apIP"] = WiFi.softAPIP().toString();
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // GET /api/wifi/profiles - сохраненные профили WiFi
  server.on("/api/wifi/profiles", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    buildWiFiProfilesResponse(doc);

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/wifi/profile - сохранить или обновить профиль WiFi
  server.on(
      "/api/wifi/profile", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        const char* ssid = doc["ssid"] | "";
        if (!ssid[0]) {
          request->send(400, "application/json", "{\"success\":false,\"error\":\"SSID required\"}");
          return;
        }

        WiFiProfile profile{};
        profile.enabled = doc["enabled"] | true;
        strlcpy(profile.ssid, ssid, sizeof(profile.ssid));
        strlcpy(profile.password, doc["password"] | "", sizeof(profile.password));
        profile.useStaticIp = doc["useStaticIp"] | false;
        strlcpy(profile.ip, doc["ip"] | "", sizeof(profile.ip));
        strlcpy(profile.gateway, doc["gateway"] | "", sizeof(profile.gateway));
        strlcpy(profile.subnet, doc["subnet"] | "255.255.255.0", sizeof(profile.subnet));
        strlcpy(profile.dns1, doc["dns1"] | "", sizeof(profile.dns1));
        strlcpy(profile.dns2, doc["dns2"] | "", sizeof(profile.dns2));

        if (profile.useStaticIp &&
            (!profile.ip[0] || !profile.gateway[0] || !profile.subnet[0])) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Static IP requires IP, gateway and subnet\"}");
          return;
        }
        if (!isValidIpOrEmpty(profile.ip) || !isValidIpOrEmpty(profile.gateway) ||
            !isValidIpOrEmpty(profile.subnet) || !isValidIpOrEmpty(profile.dns1) ||
            !isValidIpOrEmpty(profile.dns2)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid IP address format\"}");
          return;
        }

        const bool makePreferred = doc["makePreferred"] | false;
        if (!WiFiProfiles::upsertProfile(g_settings.wifi, profile, makePreferred)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save WiFi profile (limit reached or invalid SSID)\"}");
          return;
        }

        if (!NVSManager::saveSettings(g_settings)) {
          Logger::logf(2, "WiFi profile save failed: %s", ssid);
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        JsonDocument out;
        out["success"] = true;
        buildWiFiProfilesResponse(out);

        String json;
        serializeJson(out, json);
        Logger::logf(0, "WiFi profile saved: %s (%s%s)",
                     ssid,
                     profile.enabled ? "enabled" : "disabled",
                     makePreferred ? ", preferred" : "");
        request->send(200, "application/json", json);
      });

  // POST /api/wifi/profile/reorder - изменить приоритет профиля
  server.on(
      "/api/wifi/profile/reorder", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        const char* ssid = doc["ssid"] | "";
        const char* direction = doc["direction"] | "up";
        if (!ssid[0]) {
          request->send(400, "application/json", "{\"success\":false,\"error\":\"SSID required\"}");
          return;
        }

        const int shift = (strcmp(direction, "down") == 0) ? 1 : -1;
        if (!WiFiProfiles::moveProfile(g_settings.wifi, ssid, shift)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Cannot change profile priority\"}");
          return;
        }

        if (!NVSManager::saveSettings(g_settings)) {
          Logger::logf(2, "WiFi profile reorder save failed: %s", ssid);
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        JsonDocument out;
        out["success"] = true;
        buildWiFiProfilesResponse(out);

        String json;
        serializeJson(out, json);
        Logger::logf(0, "WiFi profile reordered: %s moved %s", ssid,
                     shift > 0 ? "down" : "up");
        request->send(200, "application/json", json);
      });

  // POST /api/wifi/profile/delete - удалить профиль WiFi
  server.on(
      "/api/wifi/profile/delete", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        const char* ssid = doc["ssid"] | "";
        if (!ssid[0]) {
          request->send(400, "application/json", "{\"success\":false,\"error\":\"SSID required\"}");
          return;
        }

        if (!WiFiProfiles::deleteProfile(g_settings.wifi, ssid)) {
          request->send(404, "application/json", "{\"success\":false,\"error\":\"Profile not found\"}");
          return;
        }

        if (!NVSManager::saveSettings(g_settings)) {
          Logger::logf(2, "WiFi profile delete save failed: %s", ssid);
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        JsonDocument out;
        out["success"] = true;
        buildWiFiProfilesResponse(out);

        String json;
        serializeJson(out, json);
        Logger::logf(0, "WiFi profile deleted: %s", ssid);
        request->send(200, "application/json", json);
      });

  // POST /api/wifi/connect - подключение к сети
  server.on(
      "/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        // Обрабатываем только когда получены все данные
        if (index + len != total) {
          return; // Ждем остальные chunks
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          LOG_E("WiFi: JSON parse error: %s", error.c_str());
          Logger::logf(1, "WiFi connect rejected: invalid JSON");
          request->send(400, "application/json",
                        "{\"error\":\"Invalid JSON\"}");
          return;
        }

        const char *ssid = doc["ssid"];
        const bool hasPasswordField = !doc["password"].isNull();
        const char *password = hasPasswordField ? (doc["password"] | "") : nullptr;
        const bool saveProfile = doc["saveProfile"] | true;
        const bool makePreferred = doc["makePreferred"] | false;

        if (!ssid || strlen(ssid) == 0) {
          request->send(400, "application/json",
                        "{\"error\":\"SSID required\"}");
          return;
        }

        LOG_I("WiFi: Connect request for SSID: %s", ssid);
        Logger::logf(0, "WiFi connect requested: %s%s",
                     ssid, saveProfile ? " (save profile)" : "");

        WiFiProfile profileToConnect{};
        profileToConnect.enabled = true;
        strlcpy(profileToConnect.ssid, ssid, sizeof(profileToConnect.ssid));
        strlcpy(profileToConnect.subnet, "255.255.255.0", sizeof(profileToConnect.subnet));

        WiFiProfile savedProfile{};
        const bool hasSavedProfile = WiFiProfiles::getProfileBySsid(g_settings.wifi, ssid, savedProfile);
        if (hasSavedProfile) {
          profileToConnect = savedProfile;
        }

        if (hasPasswordField && password && strlen(password) > 0) {
          strlcpy(profileToConnect.password, password, sizeof(profileToConnect.password));
        } else if (!hasSavedProfile && (!password || strlen(password) == 0)) {
          profileToConnect.password[0] = '\0';
        }

        if (!doc["useStaticIp"].isNull()) {
          profileToConnect.useStaticIp = doc["useStaticIp"] | false;
        }
        if (!doc["ip"].isNull()) {
          strlcpy(profileToConnect.ip, doc["ip"] | "", sizeof(profileToConnect.ip));
        }
        if (!doc["gateway"].isNull()) {
          strlcpy(profileToConnect.gateway, doc["gateway"] | "", sizeof(profileToConnect.gateway));
        }
        if (!doc["subnet"].isNull()) {
          strlcpy(profileToConnect.subnet, doc["subnet"] | "255.255.255.0",
                  sizeof(profileToConnect.subnet));
        }
        if (!doc["dns1"].isNull()) {
          strlcpy(profileToConnect.dns1, doc["dns1"] | "", sizeof(profileToConnect.dns1));
        }
        if (!doc["dns2"].isNull()) {
          strlcpy(profileToConnect.dns2, doc["dns2"] | "", sizeof(profileToConnect.dns2));
        }

        if (profileToConnect.useStaticIp &&
            (!profileToConnect.ip[0] || !profileToConnect.gateway[0] ||
             !profileToConnect.subnet[0])) {
          request->send(400, "application/json",
                        "{\"error\":\"Static IP requires IP, gateway and subnet\"}");
          return;
        }
        if (!isValidIpOrEmpty(profileToConnect.ip) ||
            !isValidIpOrEmpty(profileToConnect.gateway) ||
            !isValidIpOrEmpty(profileToConnect.subnet) ||
            !isValidIpOrEmpty(profileToConnect.dns1) ||
            !isValidIpOrEmpty(profileToConnect.dns2)) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid IP address format\"}");
          return;
        }

        if (saveProfile) {
          if (!WiFiProfiles::upsertProfile(g_settings.wifi, profileToConnect, makePreferred)) {
            request->send(400, "application/json",
                          "{\"error\":\"Failed to save WiFi profile\"}");
            return;
          }
          WiFiProfiles::getProfileBySsid(g_settings.wifi, ssid, profileToConnect);
        }

        WiFiProfiles::syncLegacyFields(g_settings.wifi, &profileToConnect);

        // AP must stay available while STA reconnects, so local UI at 192.168.4.1
        // does not disappear during/after WiFi credential updates.
        g_settings.wifi.apMode = true;

        // Сохранить в NVS
        if (NVSManager::saveSettings(g_settings)) {
          LOG_I("WiFi: Settings saved, connecting to %s", ssid);

          // Отправить ответ перед переподключением
          request->send(200, "application/json",
                        "{\"status\":\"connecting\",\"message\":\"Connecting "
                        "to WiFi, please wait...\"}");

          // Попытка подключения через небольшую задержку
          // чтобы ответ успел уйти клиенту
          delay(100);

          WiFiProfiles::beginConnection(profileToConnect);
        } else {
          LOG_E("WiFi: Failed to save settings to NVS");
          Logger::logf(2, "WiFi connect save failed: %s", ssid);
          request->send(500, "application/json",
                        "{\"error\":\"Failed to save settings\"}");
        }
      });

  // ==========================================================================
  // OTA UPDATE (Web UI)
  // ==========================================================================

  // GET /update - страница загрузки прошивки
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = F(
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'><meta name='viewport' "
        "content='width=device-width,initial-scale=1'>"
        "<title>Smart-Column S3 - OTA Update</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;max-width:600px;margin:50px "
        "auto;padding:20px;background:#f5f5f5}"
        ".container{background:white;padding:30px;border-radius:10px;box-"
        "shadow:0 2px 10px rgba(0,0,0,0.1)}"
        "h1{color:#333;margin-bottom:20px}"
        ".info{background:#e3f2fd;padding:15px;border-radius:5px;margin-bottom:"
        "20px}"
        "input[type=file]{width:100%;padding:10px;margin:10px 0;border:2px "
        "dashed #ccc;border-radius:5px;cursor:pointer}"
        "input[type=submit]{background:#4CAF50;color:white;padding:15px "
        "30px;border:none;border-radius:5px;cursor:pointer;font-size:16px;"
        "width:100%}"
        "input[type=submit]:hover{background:#45a049}"
        ".progress{display:none;margin-top:20px}"
        ".progress-bar{width:100%;height:30px;background:#ddd;border-radius:"
        "15px;overflow:hidden}"
        ".progress-fill{height:100%;background:#4CAF50;transition:width 0.3s}"
        ".status{margin-top:15px;padding:10px;border-radius:5px;text-align:"
        "center}"
        ".success{background:#d4edda;color:#155724}"
        ".error{background:#f8d7da;color:#721c24}"
        "</style>"
        "</head><body>"
        "<div class='container'>"
        "<h1>🔧 Firmware Update</h1>"
        "<div class='info'>"
        "<strong>Current version:</strong> " FW_VERSION "<br>"
        "<strong>Build date:</strong> " __DATE__ " " __TIME__ "<br>"
        "<strong>Platform:</strong> ESP32-S3"
        "</div>"
        "<form method='POST' action='/update' enctype='multipart/form-data' "
        "id='upload_form'>"
        "<input type='file' name='update' accept='.bin' required>"
        "<input type='submit' value='Upload Firmware'>"
        "</form>"
        "<div class='progress' id='progress'>"
        "<div class='progress-bar'><div class='progress-fill' "
        "id='progress-fill'></div></div>"
        "<div id='status'></div>"
        "</div>"
        "</div>"
        "<script>"
        "document.getElementById('upload_form').addEventListener('submit',"
        "function(e){"
        "e.preventDefault();"
        "var formData=new FormData(this);"
        "var xhr=new XMLHttpRequest();"
        "document.getElementById('progress').style.display='block';"
        "xhr.upload.addEventListener('progress',function(e){"
        "if(e.lengthComputable){"
        "var percent=(e.loaded/e.total)*100;"
        "document.getElementById('progress-fill').style.width=percent+'%';"
        "document.getElementById('status').textContent=Math.round(percent)+'%';"
        "}"
        "});"
        "xhr.addEventListener('load',function(){"
        "if(xhr.status===200){"
        "document.getElementById('status').className='status success';"
        "document.getElementById('status').textContent='✓ Update successful! "
        "Rebooting...';"
        "setTimeout(function(){location.href='/';},5000);"
        "}else{"
        "document.getElementById('status').className='status error';"
        "document.getElementById('status').textContent='✗ Update failed: "
        "'+xhr.responseText;"
        "}"
        "});"
        "xhr.open('POST','/update');"
        "xhr.send(formData);"
        "});"
        "</script>"
        "</body></html>");
    request->send(200, "text/html", html);
  });

  // POST /update - загрузка и установка прошивки
  server.on(
      "/update", HTTP_POST,
      // Обработчик завершения загрузки
      [](AsyncWebServerRequest *request) {
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(
            200, "text/plain", shouldReboot ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);

        if (shouldReboot) {
          LOG_I("OTA: Update successful, rebooting...");
          delay(1000);
          ESP.restart();
        } else {
          LOG_E("OTA: Update failed!");
        }
      },
      // Обработчик загрузки данных
      [](AsyncWebServerRequest *request, String filename, size_t index,
         uint8_t *data, size_t len, bool final) {
        if (!index) {
          LOG_I("OTA: Update start: %s", filename.c_str());

          // Начало обновления
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
          }
        }

        // Запись данных
        if (Update.write(data, len) != len) {
          Update.printError(Serial);
        }

        if (final) {
          if (Update.end(true)) {
            LOG_I("OTA: Update success: %u bytes", index + len);
          } else {
            Update.printError(Serial);
          }
        }
      });

  // ==========================================================================
  // PROFILES API
  // ==========================================================================

  // GET /api/profiles - Получить список всех профилей
  server.on("/api/profiles", HTTP_GET, [](AsyncWebServerRequest *request) {
    std::vector<ProfileListItem> profiles = getProfileList();
    
    JsonDocument doc;
    doc["total"] = profiles.size();
    
    JsonArray profileArray = doc["profiles"].to<JsonArray>();
    for (const auto& prof : profiles) {
      JsonObject p = profileArray.add<JsonObject>();
      p["id"] = prof.id;
      p["name"] = prof.name;
      p["category"] = prof.category;
      p["useCount"] = prof.useCount;
      p["lastUsed"] = prof.lastUsed;
      p["isBuiltin"] = prof.isBuiltin;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // GET /api/profiles/{id} - Получить полные данные профиля
  server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)$", HTTP_GET, [](AsyncWebServerRequest *request) {
    String id = request->pathArg(0);
    
    Profile profile;
    if (loadProfile(id, profile)) {
      JsonDocument doc;
      
      doc["id"] = profile.id;
      
      JsonObject metadata = doc["metadata"].to<JsonObject>();
      metadata["name"] = profile.metadata.name;
      metadata["description"] = profile.metadata.description;
      metadata["category"] = profile.metadata.category;
      
      JsonArray tags = metadata["tags"].to<JsonArray>();
      for (const auto& tag : profile.metadata.tags) {
        tags.add(tag);
      }
      
      metadata["created"] = profile.metadata.created;
      metadata["updated"] = profile.metadata.updated;
      metadata["author"] = profile.metadata.author;
      metadata["isBuiltin"] = profile.metadata.isBuiltin;
      
      JsonObject parameters = doc["parameters"].to<JsonObject>();
      parameters["mode"] = profile.parameters.mode;
      parameters["model"] = profile.parameters.model;
      
      JsonObject heater = parameters["heater"].to<JsonObject>();
      heater["maxPower"] = profile.parameters.heater.maxPower;
      heater["autoMode"] = profile.parameters.heater.autoMode;
      heater["pidKp"] = profile.parameters.heater.pidKp;
      heater["pidKi"] = profile.parameters.heater.pidKi;
      heater["pidKd"] = profile.parameters.heater.pidKd;
      
      JsonObject rectification = parameters["rectification"].to<JsonObject>();
      rectification["stabilizationMin"] = profile.parameters.rectification.stabilizationMin;
      rectification["headsVolume"] = profile.parameters.rectification.headsVolume;
      rectification["bodyVolume"] = profile.parameters.rectification.bodyVolume;
      rectification["tailsVolume"] = profile.parameters.rectification.tailsVolume;
      rectification["headsSpeed"] = profile.parameters.rectification.headsSpeed;
      rectification["bodySpeed"] = profile.parameters.rectification.bodySpeed;
      rectification["tailsSpeed"] = profile.parameters.rectification.tailsSpeed;
      rectification["purgeMin"] = profile.parameters.rectification.purgeMin;
      
      JsonObject distillation = parameters["distillation"].to<JsonObject>();
      distillation["headsVolume"] = profile.parameters.distillation.headsVolume;
      distillation["targetVolume"] = profile.parameters.distillation.targetVolume;
      distillation["speed"] = profile.parameters.distillation.speed;
      distillation["endTemp"] = profile.parameters.distillation.endTemp;
      
      JsonObject temperatures = parameters["temperatures"].to<JsonObject>();
      temperatures["maxCube"] = profile.parameters.temperatures.maxCube;
      temperatures["maxColumn"] = profile.parameters.temperatures.maxColumn;
      temperatures["headsEnd"] = profile.parameters.temperatures.headsEnd;
      temperatures["bodyStart"] = profile.parameters.temperatures.bodyStart;
      temperatures["bodyEnd"] = profile.parameters.temperatures.bodyEnd;
      
      JsonObject safety = parameters["safety"].to<JsonObject>();
      safety["maxRuntime"] = profile.parameters.safety.maxRuntime;
      safety["waterFlowMin"] = profile.parameters.safety.waterFlowMin;
      safety["pressureMax"] = profile.parameters.safety.pressureMax;
      
      JsonObject statistics = doc["statistics"].to<JsonObject>();
      statistics["useCount"] = profile.statistics.useCount;
      statistics["lastUsed"] = profile.statistics.lastUsed;
      statistics["avgDuration"] = profile.statistics.avgDuration;
      statistics["avgYield"] = profile.statistics.avgYield;
      statistics["successRate"] = profile.statistics.successRate;
      
      String response;
      serializeJson(doc, response);
      request->send(200, "application/json", response);
    } else {
      request->send(404, "application/json", "{\"error\":\"Profile not found\"}");
    }
  });

  // POST /api/profiles/{id}/load - Загрузить профиль в текущие настройки
  server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)\\/load$", HTTP_POST, [](AsyncWebServerRequest *request) {
    String id = request->pathArg(0);
    
    if (applyProfile(id)) {
      Logger::logf(0, "Profile loaded: %s", id.c_str());
      request->send(200, "application/json", "{\"success\":true,\"message\":\"Profile loaded\"}");
    } else {
      Logger::logf(1, "Profile load failed: %s not found", id.c_str());
      request->send(404, "application/json", "{\"error\":\"Profile not found\"}");
    }
  });

  // DELETE /api/profiles/{id} - Удалить профиль
  server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)$", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    String id = request->pathArg(0);
    
    if (deleteProfile(id)) {
      request->send(200, "application/json", "{\"success\":true,\"message\":\"Profile deleted\"}");
    } else {
      request->send(404, "application/json", "{\"error\":\"Profile not found or builtin\"}");
    }
  });

  // POST /api/profiles - Создать новый профиль
  server.on("/api/profiles", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        String jsonStr = "";
        for (size_t i = 0; i < len; i++) jsonStr += (char)data[i];
        
        String newId = importProfileFromJSON(jsonStr);
        if (!newId.isEmpty()) {
          request->send(201, "application/json", "{\"success\":true,\"id\":\"" + newId + "\"}");
        } else {
          request->send(400, "application/json", "{\"error\":\"Failed to create profile\"}");
        }
      }
    }
  );

  // GET /api/profiles/export - Экспорт всех профилей
  server.on("/api/profiles/export", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool includeBuiltin = request->hasParam("includeBuiltin");
    String json = exportAllProfilesToJSON(includeBuiltin);
    request->send(200, "application/json", json);
  });

  // POST /api/profiles/import - Импорт профилей
  server.on("/api/profiles/import", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        String jsonStr = "";
        for (size_t i = 0; i < len; i++) jsonStr += (char)data[i];
        
        uint16_t count = importProfilesFromJSON(jsonStr);
        request->send(200, "application/json", "{\"success\":true,\"count\":" + String(count) + "}");
      }
    }
  );

  // 404
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not Found");
  });

  server.begin();
  LOG_I("WebServer: Started on port %d", WEB_SERVER_PORT);
}

void broadcastState(const SystemState &state) {
  ws.cleanupClients();
  if (ws.count() == 0)
    return;

  const uint32_t now = millis();
  const auto displayStats = Display::getRuntimeStats();

  // Минимальный пакет для "лайв" (часто)
  JsonDocument fastDoc;
  fastDoc["mode"] = static_cast<int>(state.mode);
  fastDoc["modeStr"] = getModeString(state.mode);
  fastDoc["phase"] = static_cast<int>(state.rectPhase);
  fastDoc["phaseStr"] = getPhaseString(state.rectPhase);
  fastDoc["paused"] = state.paused;
  fastDoc["safetyOk"] = state.safetyOk;
  fastDoc["uptime"] = state.uptime;
  JsonObject fastAlarm = fastDoc["alarm"].to<JsonObject>();
  fillAlarmJson(fastAlarm, state, g_settings);
  JsonObject fastV2 = fastDoc["v2"].to<JsonObject>();
  fillSafetyActionV2Json(fastV2, ControlV2::getLatestModeStatus(),
                         ControlV2::getLatestMetricsSnapshot());

  fastDoc["t_cube"] = state.temps.cube;
  fastDoc["t_column_bottom"] = state.temps.columnBottom;
  fastDoc["t_column_top"] = state.temps.columnTop;
  fastDoc["t_reflux"] = state.temps.reflux;
  fastDoc["t_tsa"] = state.temps.tsa;
  fastDoc["t_water_in"] = state.temps.waterIn;
  fastDoc["t_water_out"] = state.temps.waterOut;

  fastDoc["p_cube"] = state.pressure.cube;
  fastDoc["p_atm"] = state.pressure.atmosphere;

  fastDoc["voltage"] = state.power.voltage;
  fastDoc["current"] = state.power.current;
  fastDoc["power"] = state.power.power;
  fastDoc["energy"] = state.power.energy;
  fastDoc["frequency"] = state.power.frequency;
  fastDoc["pf"] = state.power.powerFactor;

  fastDoc["pump_speed"] = state.pump.speedMlPerHour;
  fastDoc["pump_volume"] = state.pump.totalVolumeMl;
  fastDoc["speed"] = state.pump.speedMlPerHour;
  fastDoc["volume"] = state.pump.totalVolumeMl;
  fastDoc["volume_heads"] = state.stats.headsVolume;
  fastDoc["volume_body"] = state.stats.bodyVolume;
  fastDoc["volume_tails"] = state.stats.tailsVolume;
  JsonObject fastEquipment = fastDoc["equipment"].to<JsonObject>();
  fastEquipment["heaterPowerW"] = g_settings.equipment.heaterPowerW;
  fastEquipment["columnHeightMm"] = g_settings.equipment.columnHeightMm;
  fastEquipment["cubeVolumeL"] = g_settings.equipment.cubeVolumeL;
  fastEquipment["minHeaterSubmergeL"] = g_settings.equipment.minHeaterSubmergeL;
  fastEquipment["waterAutoStartCubeTempC"] = g_settings.equipment.waterAutoStartCubeTempC;
  JsonObject fastSafetySettings = fastDoc["safetySettings"].to<JsonObject>();
  fastSafetySettings["pressureMaxMmHg"] = g_settings.safety.pressureMaxMmHg;
  fastSafetySettings["tsaMaxC"] = g_settings.safety.tsaMaxC;
  fastSafetySettings["waterOutMaxC"] = g_settings.safety.waterOutMaxC;
  fastSafetySettings["waterOutRiseRateCMin"] = g_settings.safety.waterOutRiseRateCMin;
  fastSafetySettings["pressureRiseRateMmHgMin"] = g_settings.safety.pressureRiseRateMmHgMin;
  fastDoc["min_heater_submerge_l"] = g_settings.equipment.minHeaterSubmergeL;
  fastDoc["water_auto_start_cube_temp_c"] = g_settings.equipment.waterAutoStartCubeTempC;
  fastDoc["safety_pressure_max_mmhg"] = g_settings.safety.pressureMaxMmHg;
  fastDoc["safety_tsa_max_c"] = g_settings.safety.tsaMaxC;
  fastDoc["safety_water_out_max_c"] = g_settings.safety.waterOutMaxC;
  fastDoc["safety_water_out_rise_rate_c_min"] = g_settings.safety.waterOutRiseRateCMin;
  fastDoc["safety_pressure_rise_rate_mmhg_min"] = g_settings.safety.pressureRiseRateMmHgMin;
  JsonObject fastValves = fastDoc["valves"].to<JsonObject>();
  fastValves["water"] = Valves::getWater();
  fastValves["heads"] = Valves::getHeads();
  fastValves["uno"] = Valves::getUno();
  fastValves["tails"] = false;

  fastDoc["abv"] = state.hydrometer.abv;
  fastDoc["abv_valid"] = state.hydrometer.valid;
  const uint32_t phaseElapsedSec = FSM::getPhaseElapsedSec();
  const uint32_t phaseTargetSec = FSM::getPhaseTargetSec(state, g_settings);
  fastDoc["phase_elapsed_sec"] = phaseElapsedSec;
  fastDoc["phase_target_sec"] = phaseTargetSec;
  fastDoc["phase_remaining_sec"] =
      (phaseTargetSec > phaseElapsedSec) ? (phaseTargetSec - phaseElapsedSec) : 0;
  fastDoc["phase_percent"] = FSM::getPhaseProgressPercent(state, g_settings);
  fastDoc["display_last_ms"] = displayStats.lastFrameMs;
  fastDoc["display_slow"] = displayStats.slowFrames;
  fastDoc["display_hard"] = displayStats.hardWatchdogRecoveries;
  fastDoc["display_gap_ms"] = displayStats.lastUpdateGapMs;

  String fastJson;
  serializeJson(fastDoc, fastJson);
  ws.textAll(fastJson);

  // Полный пакет (редко)
  static uint32_t lastFullBroadcast = 0;
  if (now - lastFullBroadcast < INTERVAL_WEB_BROADCAST_FULL) {
    return;
  }
  lastFullBroadcast = now;

  JsonDocument doc;
  doc["mode"] = static_cast<int>(state.mode);
  doc["modeStr"] = getModeString(state.mode);
  doc["phase"] = static_cast<int>(state.rectPhase);
  doc["phaseStr"] = getPhaseString(state.rectPhase);
  doc["paused"] = state.paused;
  doc["safetyOk"] = state.safetyOk;
  doc["uptime"] = state.uptime;
  JsonObject alarm = doc["alarm"].to<JsonObject>();
  fillAlarmJson(alarm, state, g_settings);
  JsonObject v2 = doc["v2"].to<JsonObject>();
  fillV2StatusJson(v2, ControlV2::getLatestModeStatus(),
                   ControlV2::getLatestMetricsSnapshot());

  doc["t_cube"] = state.temps.cube;
  doc["t_column_bottom"] = state.temps.columnBottom;
  doc["t_column_top"] = state.temps.columnTop;
  doc["t_reflux"] = state.temps.reflux;
  doc["t_tsa"] = state.temps.tsa;
  doc["t_water_in"] = state.temps.waterIn;
  doc["t_water_out"] = state.temps.waterOut;

  doc["p_cube"] = state.pressure.cube;
  doc["p_atm"] = state.pressure.atmosphere;

  doc["voltage"] = state.power.voltage;
  doc["current"] = state.power.current;
  doc["power"] = state.power.power;
  doc["energy"] = state.power.energy;
  doc["frequency"] = state.power.frequency;
  doc["pf"] = state.power.powerFactor;

  doc["pump_speed"] = state.pump.speedMlPerHour;
  doc["pump_volume"] = state.pump.totalVolumeMl;
  doc["speed"] = state.pump.speedMlPerHour;
  doc["volume"] = state.pump.totalVolumeMl;
  doc["volume_heads"] = state.stats.headsVolume;
  doc["volume_body"] = state.stats.bodyVolume;
  doc["volume_tails"] = state.stats.tailsVolume;
  JsonObject valves = doc["valves"].to<JsonObject>();
  valves["water"] = Valves::getWater();
  valves["heads"] = Valves::getHeads();
  valves["uno"] = Valves::getUno();
  valves["tails"] = false;

  doc["abv"] = state.hydrometer.abv;
  doc["abv_valid"] = state.hydrometer.valid;

  JsonObject progress = doc["progress"].to<JsonObject>();
  progress["phaseElapsedSec"] = phaseElapsedSec;
  progress["phaseTargetSec"] = phaseTargetSec;
  progress["phaseRemainingSec"] =
      (phaseTargetSec > phaseElapsedSec) ? (phaseTargetSec - phaseElapsedSec) : 0;
  progress["phasePercent"] = FSM::getPhaseProgressPercent(state, g_settings);

  JsonObject equipment = doc["equipment"].to<JsonObject>();
  equipment["heaterPowerW"] = g_settings.equipment.heaterPowerW;
  equipment["columnHeightMm"] = g_settings.equipment.columnHeightMm;
  equipment["cubeVolumeL"] = g_settings.equipment.cubeVolumeL;
  equipment["minHeaterSubmergeL"] = g_settings.equipment.minHeaterSubmergeL;
  equipment["waterAutoStartCubeTempC"] = g_settings.equipment.waterAutoStartCubeTempC;
  JsonObject safetySettings = doc["safetySettings"].to<JsonObject>();
  safetySettings["pressureMaxMmHg"] = g_settings.safety.pressureMaxMmHg;
  safetySettings["tsaMaxC"] = g_settings.safety.tsaMaxC;
  safetySettings["waterOutMaxC"] = g_settings.safety.waterOutMaxC;
  safetySettings["waterOutRiseRateCMin"] = g_settings.safety.waterOutRiseRateCMin;
  safetySettings["pressureRiseRateMmHgMin"] = g_settings.safety.pressureRiseRateMmHgMin;
  doc["min_heater_submerge_l"] = g_settings.equipment.minHeaterSubmergeL;
  doc["water_auto_start_cube_temp_c"] = g_settings.equipment.waterAutoStartCubeTempC;
  doc["safety_pressure_max_mmhg"] = g_settings.safety.pressureMaxMmHg;
  doc["safety_tsa_max_c"] = g_settings.safety.tsaMaxC;
  doc["safety_water_out_max_c"] = g_settings.safety.waterOutMaxC;
  doc["safety_water_out_rise_rate_c_min"] = g_settings.safety.waterOutRiseRateCMin;
  doc["safety_pressure_rise_rate_mmhg_min"] = g_settings.safety.pressureRiseRateMmHgMin;

  JsonObject rect = doc["rectification"].to<JsonObject>();
  rect["feedVolumeL"] = g_settings.rectParams.feedVolumeL;
  rect["feedAbvPercent"] = g_settings.rectParams.feedAbvPercent;
  rect["headsPercent"] = g_settings.rectParams.headsPercent;
  rect["bodyPercent"] = g_settings.rectParams.bodyPercent;
  rect["tailsPercent"] = g_settings.rectParams.tailsPercent;
  rect["headsSpeedMlHKw"] = g_settings.rectParams.headsSpeedMlHKw;
  rect["bodySpeedMlHKw"] = g_settings.rectParams.bodySpeedMlHKw;

  float rectHeadsTargetMl = 0.0f;
  float rectBodyTargetMl = 0.0f;
  float rectTailsTargetMl = 0.0f;
  FSM::getRectTargetsMl(rectHeadsTargetMl, rectBodyTargetMl, rectTailsTargetMl);
  rect["headsTargetMl"] = rectHeadsTargetMl;
  rect["bodyTargetMl"] = rectBodyTargetMl;
  rect["tailsTargetMl"] = rectTailsTargetMl;

  JsonObject distillation = doc["distillation"].to<JsonObject>();
  float distSpeedMlH = 0.0f;
  float distHeadsVolumeMl = 0.0f;
  float distTargetVolumeMl = 0.0f;
  float distEndTempC = 0.0f;
  uint8_t distPowerPercent = 0;
  FSM::getDistillationParams(distSpeedMlH, distHeadsVolumeMl, distTargetVolumeMl, distEndTempC,
                             distPowerPercent);
  distillation["speedMlH"] = distSpeedMlH;
  distillation["headsVolumeMl"] = distHeadsVolumeMl;
  distillation["targetVolumeMl"] = distTargetVolumeMl;
  distillation["endTempC"] = distEndTempC;
  distillation["powerPercent"] = distPowerPercent;

  JsonObject display = doc["display"].to<JsonObject>();
  display["frames"] = displayStats.framesRendered;
  display["slowFrames"] = displayStats.slowFrames;
  display["recoveries"] = displayStats.watchdogRecoveries;
  display["hardRecoveries"] = displayStats.hardWatchdogRecoveries;
  display["hardFailures"] = displayStats.hardWatchdogFailures;
  display["lastFrameMs"] = displayStats.lastFrameMs;
  display["maxFrameMs"] = displayStats.maxFrameMs;
  display["lastFrameAt"] = displayStats.lastFrameAtMs;
  display["lastGapMs"] = displayStats.lastUpdateGapMs;
  display["maxGapMs"] = displayStats.maxUpdateGapMs;
  display["gapOverruns"] = displayStats.updateGapOverruns;

  JsonObject mashing = doc["mashing"].to<JsonObject>();
  mashing["active"] = state.mashing.active;
  mashing["phase"] = static_cast<int>(state.mashing.phase);
  mashing["phaseStr"] = getMashPhaseString(state.mashing.phase);
  mashing["stepCount"] = state.mashing.stepCount;
  mashing["currentStep"] = state.mashing.currentStep;
  mashing["targetTemp"] = state.mashing.targetTemp;
  mashing["stepDurationSec"] = state.mashing.stepDuration;
  mashing["tempInRange"] = state.mashing.tempInRange;
  mashing["stepName"] = state.mashing.stepName;

  uint32_t mashElapsedSec = 0;
  if (state.mashing.tempInRange && state.mashing.inRangeStartTime > 0 &&
      now >= state.mashing.inRangeStartTime) {
    mashElapsedSec = (now - state.mashing.inRangeStartTime) / 1000UL;
  }
  mashing["elapsedSec"] = mashElapsedSec;
  mashing["remainingSec"] =
      (state.mashing.stepDuration > mashElapsedSec)
          ? (state.mashing.stepDuration - mashElapsedSec)
          : 0;

  JsonObject hold = doc["hold"].to<JsonObject>();
  hold["active"] = state.hold.active;
  hold["stepCount"] = state.hold.stepCount;
  hold["currentStep"] = state.hold.currentStep;
  hold["targetTemp"] = state.hold.targetTemp;
  hold["tempInRange"] = state.hold.tempInRange;

  uint32_t holdStepDurationSec = 0;
  if (state.hold.stepCount > 0 && state.hold.currentStep < state.hold.stepCount) {
    holdStepDurationSec =
        (uint32_t)state.hold.steps[state.hold.currentStep].duration * 60UL;
  }
  hold["stepDurationSec"] = holdStepDurationSec;

  uint32_t holdElapsedSec = 0;
  if (state.hold.tempInRange && state.hold.inRangeStartTime > 0 &&
      now >= state.hold.inRangeStartTime) {
    holdElapsedSec = (now - state.hold.inRangeStartTime) / 1000UL;
  }
  hold["elapsedSec"] = holdElapsedSec;
  hold["remainingSec"] =
      (holdStepDurationSec > holdElapsedSec) ? (holdStepDurationSec - holdElapsedSec) : 0;

  JsonObject mem = doc["memory"].to<JsonObject>();
  mem["heap_free"] = ESP.getFreeHeap();
  mem["heap_total"] = ESP.getHeapSize();
  mem["heap_used_pct"] =
      (ESP.getHeapSize() - ESP.getFreeHeap()) * 100 / ESP.getHeapSize();
  mem["psram_free"] = ESP.getFreePsram();
  mem["psram_total"] = ESP.getPsramSize();
  mem["flash_used"] = ESP.getSketchSize();
  mem["flash_total"] = ESP.getFlashChipSize();
  mem["flash_used_pct"] = ESP.getSketchSize() * 100 / ESP.getFlashChipSize();

  JsonObject health = doc["health"].to<JsonObject>();
  health["overall"] = state.health.overallHealth;
  health["tempSensorsOk"] = state.health.tempSensorsOk;
  health["tempSensorsTotal"] = state.health.tempSensorsTotal;
  health["bmp280"] = state.health.bmp280Ok;
  health["ads1115"] = state.health.ads1115Ok;
  health["pzem"] = state.health.pzemOk;
  health["wifiRSSI"] = state.health.wifiRSSI;
  health["pzemSpikes"] = state.health.pzemSpikeCount;
  health["tempErrors"] = state.health.tempReadErrors;
  health["cpuTemp"] = state.health.cpuTemp;

  // Мешалка
  JsonObject stirrer = doc["stirrer"].to<JsonObject>();
  stirrer["running"]   = state.stirrer.running;
  stirrer["speed"]     = state.stirrer.speedPercent;
  stirrer["available"] = state.stirrer.available;
  stirrer["autoMode"]  = state.stirrer.autoMode;

  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}

void broadcastEvent(const char *event, const char *message) {
  JsonDocument doc;
  doc["type"] = "event";
  doc["event"] = event;
  doc["message"] = message;

  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}

} // namespace WebServer
