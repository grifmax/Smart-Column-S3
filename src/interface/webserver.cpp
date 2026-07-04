/**
 * Smart-Column S3 - Веб-сервер
 *
 * HTTP server + WebSocket для Web UI
 */

#include "webserver.h"
#include "api/api_routes.h"
#include "web_live.h"
#include "webserver_shared.h"
#include "../config.h"
#include "../fs_compat.h"
#include "../history.h"
#include "../history_demo.h"
#include "../live_chart_history.h"
#include "../types.h"
#include <AsyncTCP.h>
#include <WiFi.h>

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
void fillTemperatureTopologyJson(JsonObject topology,
                                 const EquipmentSettings &equipment);
void fillTemperatureModeSupportJson(JsonObject modes,
                                    const Settings &settings);

bool hasConfiguredWiFi() {
  return WiFiProfiles::hasConfiguredProfiles(g_settings.wifi);
}

// Вспомогательные функции для строковых представлений
const char *getModeString(Mode mode) {
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

const char *getPhaseString(RectPhase phase) {
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

const char *getMashPhaseString(MashPhase phase) {
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

const char *getNbkPhaseString(NbkPhase phase) {
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

const char *getFermPhaseString(FermentationPhase phase) {
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

const char *getFractionToken(Fraction fraction) {
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

const char *getFractionLabel(Fraction fraction) {
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

const char *getTempSensorLabel(uint8_t index) {
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

const char *getTempSensorRoleKey(uint8_t index) {
  switch (index) {
  case TEMP_CUBE:
    return "cube";
  case TEMP_COLUMN_BOTTOM:
    return "columnBottom";
  case TEMP_COLUMN_TOP:
    return "columnTop";
  case TEMP_REFLUX:
    return "reflux";
  case TEMP_TSA:
    return "tsa";
  case TEMP_WATER_IN:
    return "waterIn";
  case TEMP_WATER_OUT:
    return "waterOut";
  default:
    return "unknown";
  }
}

static bool isZeroTempAddress(const uint8_t address[8]) {
  if (!address) return true;
  for (uint8_t i = 0; i < 8; ++i) {
    if (address[i] != 0) {
      return false;
    }
  }
  return true;
}

void formatTempAddress(const uint8_t address[8], char *buffer,
                       size_t bufferSize) {
  if (!buffer || bufferSize == 0) return;
  if (!address || isZeroTempAddress(address)) {
    snprintf(buffer, bufferSize, "");
    return;
  }
  snprintf(buffer, bufferSize, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
           address[0], address[1], address[2], address[3], address[4],
           address[5], address[6], address[7]);
}

bool parseTempAddressString(const char *value, uint8_t address[8]) {
  if (!address) return false;
  memset(address, 0, 8);
  if (!value) return false;

  unsigned int bytes[8] = {0};
  const int parsed = sscanf(
      value, "%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x", &bytes[0], &bytes[1], &bytes[2],
      &bytes[3], &bytes[4], &bytes[5], &bytes[6], &bytes[7]);
  if (parsed != 8) {
    return false;
  }

  for (uint8_t i = 0; i < 8; ++i) {
    address[i] = static_cast<uint8_t>(bytes[i] & 0xFF);
  }
  return true;
}

void appendTempSensorMeta(JsonObject obj, uint8_t index) {
  if (index >= TEMP_COUNT) {
    return;
  }

  char assignedAddrStr[24];
  formatTempAddress(g_settings.tempCal.addresses[index], assignedAddrStr,
                    sizeof(assignedAddrStr));
  uint8_t detectedAddress[8] = {0};
  char detectedAddrStr[24];
  if (Sensors::getDiscoveredTempAddress(index, detectedAddress)) {
    formatTempAddress(detectedAddress, detectedAddrStr,
                      sizeof(detectedAddrStr));
  } else {
    detectedAddrStr[0] = '\0';
  }

  obj["roleKey"] = getTempSensorRoleKey(index);
  obj["assigned"] = assignedAddrStr[0] != '\0';
  obj["detected"] = detectedAddrStr[0] != '\0';
  obj["assignedAddress"] = assignedAddrStr;
  obj["detectedAddress"] = detectedAddrStr;
}

void syncStirrerState();
void fillStirrerJson(JsonObject stirrer, const SystemState &state);

void fillAlarmJson(JsonObject alarm, const SystemState& state, const Settings& settings) {
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

void fillV2StatusJson(JsonObject v2, const ControlV2::ModeStatusV2& status,
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

  JsonObject guidance = v2["guidance"].to<JsonObject>();
  guidance["tone"] = status.guidance.tone;
  guidance["title"] = status.guidance.title;
  guidance["detail"] = status.guidance.detail;
  guidance["action"] = status.guidance.action;

  JsonObject reasonInsight = v2["reasonInsight"].to<JsonObject>();
  reasonInsight["tone"] = status.reasonInsight.tone;
  reasonInsight["title"] = status.reasonInsight.title;
  reasonInsight["detail"] = status.reasonInsight.detail;
  reasonInsight["action"] = status.reasonInsight.action;

  JsonObject limits = v2["activeLimits"].to<JsonObject>();
  limits["powerCapped"] = status.activeLimits.powerCapped;
  limits["maxHeaterPowerPercent"] = status.activeLimits.maxHeaterPowerPercent;
  limits["pumpCapped"] = status.activeLimits.pumpCapped;
  limits["maxPumpSpeedMlH"] = status.activeLimits.maxPumpSpeedMlH;
  limits["takeoffBlocked"] = status.activeLimits.takeoffBlocked;
  limits["phaseAdvanceBlocked"] = status.activeLimits.phaseAdvanceBlocked;
  limits["antiOscillationActive"] = status.activeLimits.antiOscillationActive;
  limits["antiOscillationHoldSec"] = status.activeLimits.antiOscillationHoldSec;

  JsonObject targets = v2["commandTargets"].to<JsonObject>();
  targets["heaterPowerPercent"] = status.commandTargets.heaterPowerPercent;
  targets["pumpSpeedMlH"] = status.commandTargets.pumpSpeedMlH;
  targets["waterValveOpen"] = status.commandTargets.waterValveOpen;
  targets["headsValveOpen"] = status.commandTargets.headsValveOpen;
  targets["stopRequested"] = status.commandTargets.stopRequested;

  JsonObject indicators = v2["indicators"].to<JsonObject>();
  indicators["processHealth"] = status.indicators.processHealth;
  indicators["telemetryCoverage"] = status.indicators.telemetryCoverage;
  indicators["decisionTrust"] = status.indicators.decisionTrust;
  indicators["sensorFreshnessOk"] = status.indicators.sensorFreshnessOk;
  indicators["pressureStable"] = status.indicators.pressureStable;
  indicators["pressureSensorAvailable"] = status.indicators.pressureSensorAvailable;
  indicators["columnSensorsAvailable"] = status.indicators.columnSensorsAvailable;
  indicators["coolingSensorAvailable"] = status.indicators.coolingSensorAvailable;
  indicators["boilingDetected"] = status.indicators.boilingDetected;
  indicators["columnStable"] = status.indicators.columnStable;
  indicators["targetReached"] = status.indicators.targetReached;
  indicators["powerLimited"] = status.indicators.powerLimited;
  indicators["recoveryActive"] = status.indicators.recoveryActive;
  indicators["takeoffAllowed"] = status.indicators.takeoffAllowed;
  indicators["degradedModeActive"] = status.indicators.degradedModeActive;
  indicators["adaptiveControlAllowed"] = status.indicators.adaptiveControlAllowed;
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
  indicators["takeoffConfidence"] = status.indicators.takeoffConfidence;
  indicators["headsEndConfidence"] = status.indicators.headsEndConfidence;
  indicators["bodyEndConfidence"] = status.indicators.bodyEndConfidence;
  indicators["tailsTransitionConfidence"] = status.indicators.tailsTransitionConfidence;
  indicators["powerLimitConfidence"] = status.indicators.powerLimitConfidence;

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

void fillSafetyActionV2Json(JsonObject v2, const ControlV2::ModeStatusV2& status,
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

uint8_t clampU8Range(uint32_t value, uint8_t minValue, uint8_t maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return static_cast<uint8_t>(value);
}

void syncStirrerState() {
  Stirrer::syncState(g_state);
}

void fillStirrerJson(JsonObject stirrer, const SystemState &state) {
  stirrer["running"] = state.stirrer.running;
  stirrer["speed"] = state.stirrer.speedPercent;
  stirrer["available"] = state.stirrer.available;
  stirrer["autoMode"] = state.stirrer.autoMode;
  stirrer["lastUpdate"] = state.stirrer.lastUpdate;
}

static void fillStirrerSettingsJson(JsonObject settings,
                                    const Settings &source) {
  settings["enabled"] = source.stirrer.enabled;
  settings["defaultSpeedPercent"] = source.stirrer.defaultSpeedPercent;
  settings["autoMashing"] = source.stirrer.autoMashing;
  settings["autoFermentation"] = source.stirrer.autoFermentation;
  settings["autoNbk"] = source.stirrer.autoNbk;
}

void sendStirrerStateResponse(AsyncWebServerRequest *request, int statusCode,
                              bool success, const char *message) {
  syncStirrerState();

  JsonDocument doc;
  doc["success"] = success;
  doc["message"] = message;
  JsonObject stirrer = doc["stirrer"].to<JsonObject>();
  fillStirrerJson(stirrer, g_state);

  String json;
  serializeJson(doc, json);
  request->send(statusCode, "application/json", json);
}

bool ensureStirrerReady(AsyncWebServerRequest *request) {
  if (g_state.mode != Mode::IDLE) {
    char reason[128];
    snprintf(reason, sizeof(reason),
             g_state.paused
                 ? "Manual stirrer control is unavailable while %s is paused"
                 : "Manual stirrer control is unavailable while %s is active",
             getModeString(g_state.mode));
    sendStirrerStateResponse(request, 409, false, reason);
    return false;
  }

  if (!g_settings.stirrer.enabled) {
    sendStirrerStateResponse(request, 409, false,
                             "Stirrer is disabled in settings");
    return false;
  }

  if (!Stirrer::isAvailable()) {
    sendStirrerStateResponse(request, 503, false,
                             "Stirrer DAC is not available");
    return false;
  }

  if (!g_state.safetyOk || Safety::isLatched(g_state)) {
    sendStirrerStateResponse(request, 409, false,
                             "Safety lockout is active");
    return false;
  }

  return true;
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

void applySecuritySettings() {
  Security::init(g_settings.security.username, g_settings.security.password);
  Security::setAuthEnabled(g_settings.security.authEnabled);
  Security::setRateLimitEnabled(g_settings.security.rateLimitEnabled);
}

float clampFloatRange(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

uint16_t clampU16Range(uint32_t value, uint16_t minValue,
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

bool parseRequestedMode(const char *modeStr, Mode &mode) {
  if (!modeStr) {
    return false;
  }

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
    return false;
  }

  return true;
}

static bool isCoolingRelevantForMode(Mode mode) {
  return mode == Mode::RECTIFICATION || mode == Mode::DISTILLATION ||
         mode == Mode::MANUAL_RECT || mode == Mode::NBK;
}

static bool requiresAdaptiveColumnTopForMode(Mode mode) {
  return mode == Mode::RECTIFICATION || mode == Mode::MANUAL_RECT;
}

static bool isPressureStartupBlockingForMode(Mode mode) {
  (void)mode;
  return false;
}

#if 0
static String buildBlockingRequiredSensorsList(
    const Safety::RequiredSensorsMask &required, const SystemState &state,
    bool includePressure) {
  String missing;

  auto appendMissing = [&](bool condition, const char *label) {
    if (!condition) {
      return;
    }
    if (!missing.isEmpty()) {
      missing += ", ";
    }
    missing += label;
  };

  appendMissing(required.cubeTemp && !state.temps.valid[TEMP_CUBE], "куб");
  appendMissing(required.columnBottomTemp && !state.temps.valid[TEMP_COLUMN_BOTTOM],
                "низ колонны");
  appendMissing(required.tsaTemp && !state.temps.valid[TEMP_TSA], "TSA");
  appendMissing(required.waterOutTemp && !state.temps.valid[TEMP_WATER_OUT],
                "выход воды");
  appendMissing(includePressure && required.pressure && !state.pressure.ok,
                "давление куба");

  return missing;
}

static String buildMissingRequiredSensorsList(Mode mode, const Settings &settings,
                                              const SystemState &state) {
  const Safety::RequiredSensorsMask required =
      Safety::getRequiredSensorsForMode(mode, settings);
  String missing;

  auto appendMissing = [&](bool condition, const char *label) {
    if (!condition) {
      return;
    }
    if (!missing.isEmpty()) {
      missing += ", ";
    }
    missing += label;
  };

  appendMissing(required.cubeTemp && !state.temps.valid[TEMP_CUBE], "куб");
  appendMissing(required.columnBottomTemp && !state.temps.valid[TEMP_COLUMN_BOTTOM],
                "низ колонны");
  appendMissing(required.tsaTemp && !state.temps.valid[TEMP_TSA], "TSA");
  appendMissing(required.waterOutTemp && !state.temps.valid[TEMP_WATER_OUT],
                "выход воды");
  appendMissing(required.pressure && !state.pressure.ok, "давление куба");

  return missing;
}

static String buildStartupMissingSensorsList(
    const Safety::RequiredSensorsMask &required, const SystemState &state,
    bool includePressure) {
  String missing;

  auto appendMissing = [&](bool condition, const char *label) {
    if (!condition) {
      return;
    }
    if (!missing.isEmpty()) {
      missing += ", ";
    }
    missing += label;
  };

  appendMissing(required.cubeTemp && !state.temps.valid[TEMP_CUBE], "куб");
  appendMissing(required.columnBottomTemp && !state.temps.valid[TEMP_COLUMN_BOTTOM],
                "низ колонны");
  appendMissing(required.tsaTemp && !state.temps.valid[TEMP_TSA], "TSA");
  appendMissing(required.waterOutTemp && !state.temps.valid[TEMP_WATER_OUT],
                "выход воды");
  appendMissing(includePressure && required.pressure && !state.pressure.ok,
                "давление куба");

  return missing;
}

void fillTemperatureTopologyJson(JsonObject topology,
                                 const EquipmentSettings &equipment) {
  topology["cube"] = Safety::isTempSensorInstalled(equipment, TEMP_CUBE);
  topology["columnBottom"] =
      Safety::isTempSensorInstalled(equipment, TEMP_COLUMN_BOTTOM);
  topology["columnTop"] =
      Safety::isTempSensorInstalled(equipment, TEMP_COLUMN_TOP);
  topology["reflux"] = Safety::isTempSensorInstalled(equipment, TEMP_REFLUX);
  topology["tsa"] = Safety::isTempSensorInstalled(equipment, TEMP_TSA);
  topology["waterIn"] = Safety::isTempSensorInstalled(equipment, TEMP_WATER_IN);
  topology["waterOut"] =
      Safety::isTempSensorInstalled(equipment, TEMP_WATER_OUT);
  topology["installedCount"] = Safety::getInstalledTempSensorCount(equipment);
}

void fillTemperatureModeSupportJson(JsonObject modes,
                                    const Settings &settings) {
  struct ModeItem {
    const char *key;
    Mode mode;
  };
  static const ModeItem kModes[] = {
      {"rectification", Mode::RECTIFICATION},
      {"manualRect", Mode::MANUAL_RECT},
      {"distillation", Mode::DISTILLATION},
      {"nbk", Mode::NBK},
      {"mashing", Mode::MASHING},
      {"hold", Mode::HOLD},
      {"fermentation", Mode::FERMENTATION},
  };

  for (const ModeItem &item : kModes) {
    char reason[160] = "";
    JsonObject modeJson = modes[item.key].to<JsonObject>();
    const bool supported = Safety::isModeTemperatureTopologySupported(
        item.mode, settings.equipment, reason, sizeof(reason));
    modeJson["supported"] = supported;
    modeJson["reason"] = supported ? "" : reason;
  }
}
#endif

static bool expectsAutoStirrerForMode(Mode mode, const Settings &settings) {
  if (!settings.stirrer.enabled) {
    return false;
  }

  switch (mode) {
  case Mode::MASHING:
    return settings.stirrer.autoMashing;
  case Mode::NBK:
    return settings.stirrer.autoNbk;
  case Mode::FERMENTATION:
    return settings.stirrer.autoFermentation;
  default:
    return false;
  }
}

static void appendProcessPreflightItem(JsonArray items, const char *id,
                                       const char *tone, const char *title,
                                       const String &detail, bool blocking,
                                       uint8_t &blockingCount,
                                       uint8_t &warningCount) {
  JsonObject item = items.add<JsonObject>();
  item["id"] = id;
  item["tone"] = tone;
  item["title"] = title;
  item["detail"] = detail;
  item["blocking"] = blocking;

  if (blocking) {
    blockingCount++;
  } else if (strcmp(tone, "warn") == 0) {
    warningCount++;
  }
}

static void setProcessPreflightCheck(JsonObject checks, const char *key,
                                     const char *text, const char *tone) {
  JsonObject check = checks[key].to<JsonObject>();
  check["text"] = text;
  check["tone"] = tone;
}

struct ProcessDryRunForecast {
  bool supported = false;
  bool usesLearning = false;
  bool profileAligned = false;
  float feedVolumeL = 0.0f;
  float feedAbvPercent = 0.0f;
  float absoluteAlcoholMl = 0.0f;
  float headsMl = 0.0f;
  float bodyMl = 0.0f;
  float tailsMl = 0.0f;
  float headsSpeedMlH = 0.0f;
  float bodySpeedMlH = 0.0f;
  float tailsSpeedMlH = 0.0f;
  float heatingMin = 0.0f;
  float preparationMin = 0.0f;
  float takeoffMin = 0.0f;
  float totalMin = 0.0f;
  float energyKwh = 0.0f;
  float baselineDurationMin = 0.0f;
  float baselineEnergyKwh = 0.0f;
  float baselineAbsoluteAlcoholMl = 0.0f;
  String durationSource;
  String energySource;
  String summary;
  String riskTone = "muted";
  String riskTitle;
  String riskDetail;
  String baselineProcessId;
};

static float safeRatio(float numerator, float denominator, float fallback = 1.0f) {
  if (denominator <= 0.0f) {
    return fallback;
  }
  return numerator / denominator;
}

static float clampForecastRatio(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

static String formatForecastDuration(float totalMinutes) {
  if (!(totalMinutes > 0.0f)) {
    return "—";
  }

  const uint32_t rounded = static_cast<uint32_t>(totalMinutes + 0.5f);
  const uint32_t hours = rounded / 60;
  const uint32_t minutes = rounded % 60;
  if (hours == 0) {
    return String(minutes) + " мин";
  }
  if (minutes == 0) {
    return String(hours) + " ч";
  }
  return String(hours) + " ч " + String(minutes) + " мин";
}

static ProcessDryRunForecast buildRectificationDryRunForecast(
    const RectParams& rect,
    const ControlV2::ModeStatusV2& status,
    const Profile* activeProfile,
    const ProfileBaroCorrectionSummary* baroPreview,
    bool profileCategoryMatches,
    float cubeVolumeLimitL) {
  ProcessDryRunForecast forecast;
  forecast.supported = true;
  forecast.profileAligned = profileCategoryMatches && activeProfile != nullptr;
  forecast.feedVolumeL = clampFloatRange(rect.feedVolumeL, 1.0f, 250.0f);

  float feedAbvPercent = rect.feedAbvPercent;
  if ((feedAbvPercent <= 0.0f || feedAbvPercent >= 100.0f) && g_state.hydrometer.valid) {
    feedAbvPercent = g_state.hydrometer.abv;
  }
  forecast.feedAbvPercent = clampFloatRange(feedAbvPercent, 1.0f, 96.0f);
  forecast.absoluteAlcoholMl =
      forecast.feedVolumeL * 1000.0f * (forecast.feedAbvPercent / 100.0f);

  forecast.headsMl =
      forecast.absoluteAlcoholMl * (clampFloatRange(rect.headsPercent, 0.0f, 40.0f) / 100.0f);
  forecast.bodyMl =
      forecast.absoluteAlcoholMl * (clampFloatRange(rect.bodyPercent, 0.0f, 100.0f) / 100.0f);
  forecast.tailsMl =
      forecast.absoluteAlcoholMl * (clampFloatRange(rect.tailsPercent, 0.0f, 100.0f) / 100.0f);

  const float heaterPowerKw =
      fmaxf(0.1f, static_cast<float>(g_settings.equipment.heaterPowerW) / 1000.0f);
  forecast.headsSpeedMlH =
      clampFloatRange(rect.headsSpeedMlHKw, 10.0f, 2000.0f) * heaterPowerKw;
  forecast.bodySpeedMlH =
      clampFloatRange(rect.bodySpeedMlHKw, 50.0f, 3000.0f) * heaterPowerKw;
  forecast.tailsSpeedMlH = fmaxf(50.0f, forecast.bodySpeedMlH * 0.60f);

  const float headsMin =
      forecast.headsSpeedMlH > 0.0f ? (forecast.headsMl / forecast.headsSpeedMlH) * 60.0f : 0.0f;
  const float bodyMin =
      forecast.bodySpeedMlH > 0.0f ? (forecast.bodyMl / forecast.bodySpeedMlH) * 60.0f : 0.0f;
  const float tailsMin =
      forecast.tailsSpeedMlH > 0.0f ? (forecast.tailsMl / forecast.tailsSpeedMlH) * 60.0f : 0.0f;
  forecast.takeoffMin = headsMin + bodyMin + tailsMin;
  forecast.preparationMin = static_cast<float>(rect.stabilizationMin + rect.purgeMin);
  forecast.heatingMin =
      14.0f + safeRatio(forecast.feedVolumeL * 14.0f, heaterPowerKw, 42.0f);

  const float modelDurationMin =
      forecast.heatingMin + forecast.preparationMin + forecast.takeoffMin;
  float blendedDurationMin = modelDurationMin;
  float blendedEnergyKwh = heaterPowerKw * (modelDurationMin / 60.0f) * 0.72f;
  forecast.durationSource = "model";
  forecast.energySource = "model";

  if (activeProfile != nullptr) {
    forecast.baselineProcessId = activeProfile->learning.lastSuccessfulProcessId;
    const float baselineFeedVolumeL = activeProfile->validation.feedVolumeL;
    const float baselineFeedAbvPercent = activeProfile->validation.feedAbvPercent;
    if (baselineFeedVolumeL > 0.0f && baselineFeedAbvPercent > 0.0f) {
      forecast.baselineAbsoluteAlcoholMl =
          baselineFeedVolumeL * 1000.0f * (baselineFeedAbvPercent / 100.0f);
    }

    const float baselineBodySpeedMlHKw =
        activeProfile->parameters.rectification.bodySpeed > 0
            ? static_cast<float>(activeProfile->parameters.rectification.bodySpeed)
            : clampFloatRange(rect.bodySpeedMlHKw, 50.0f, 3000.0f);
    const float currentBodySpeedMlHKw =
        clampFloatRange(rect.bodySpeedMlHKw, 50.0f, 3000.0f);
    const float baselineDurationMin =
        activeProfile->statistics.avgDuration > 0
            ? static_cast<float>(activeProfile->statistics.avgDuration) / 60.0f
            : 0.0f;

    if (activeProfile->learning.successfulRuns > 0 && baselineDurationMin > 0.0f) {
      const float aaRatio =
          clampForecastRatio(
              safeRatio(forecast.absoluteAlcoholMl, forecast.baselineAbsoluteAlcoholMl, 1.0f),
              0.55f, 1.90f);
      const float chargeRatio =
          clampForecastRatio(safeRatio(forecast.feedVolumeL, baselineFeedVolumeL, 1.0f),
                             0.55f, 1.90f);
      const float speedRatio =
          clampForecastRatio(
              safeRatio(baselineBodySpeedMlHKw, currentBodySpeedMlHKw, 1.0f),
              0.70f, 1.35f);
      const float baselineScale =
          clampForecastRatio(aaRatio * 0.45f + chargeRatio * 0.35f + speedRatio * 0.20f,
                             0.60f, 1.85f);

      forecast.baselineDurationMin = baselineDurationMin * baselineScale;
      blendedDurationMin =
          (forecast.baselineDurationMin * 0.60f) + (modelDurationMin * 0.40f);
      forecast.durationSource = "learning+model";
      forecast.usesLearning = true;
    }

    if (activeProfile->learning.avgEnergyUsed > 0.0f) {
      const float aaRatio =
          clampForecastRatio(
              safeRatio(forecast.absoluteAlcoholMl, forecast.baselineAbsoluteAlcoholMl, 1.0f),
              0.55f, 1.90f);
      const float chargeRatio =
          clampForecastRatio(safeRatio(forecast.feedVolumeL, baselineFeedVolumeL, 1.0f),
                             0.55f, 1.90f);
      forecast.baselineEnergyKwh =
          activeProfile->learning.avgEnergyUsed *
          clampForecastRatio((aaRatio * 0.65f) + (chargeRatio * 0.35f), 0.60f, 1.90f);
      blendedEnergyKwh =
          (forecast.baselineEnergyKwh * 0.65f) + (blendedEnergyKwh * 0.35f);
      forecast.energySource =
          forecast.usesLearning ? "learning+model" : "learning";
      forecast.usesLearning = true;
    } else if (activeProfile->learning.avgEnergyPerLiter > 0.0f) {
      const float collectedLiters =
          (forecast.headsMl + forecast.bodyMl + forecast.tailsMl) / 1000.0f;
      if (collectedLiters > 0.0f) {
        forecast.baselineEnergyKwh =
            activeProfile->learning.avgEnergyPerLiter * collectedLiters;
        blendedEnergyKwh =
            (forecast.baselineEnergyKwh * 0.55f) + (blendedEnergyKwh * 0.45f);
        forecast.energySource =
            forecast.usesLearning ? "learning+model" : "learning";
        forecast.usesLearning = true;
      }
    }
  }

  forecast.totalMin = blendedDurationMin;
  forecast.energyKwh = blendedEnergyKwh;
  forecast.summary =
      String("Ожидаемо ") + formatForecastDuration(forecast.totalMin) +
      ", отбор около " + String(forecast.bodyMl, 0) + " мл тела и суммарно " +
      String(forecast.energyKwh, 1) + " кВт·ч.";

  forecast.riskTone = "good";
  forecast.riskTitle = "Основной риск dry-run";
  forecast.riskDetail =
      "Параметры запуска выглядят согласованными, явных инженерных рисков сверх штатных ограничений не видно.";

  const float fillPct =
      cubeVolumeLimitL > 0.0f ? (forecast.feedVolumeL / cubeVolumeLimitL) * 100.0f : 0.0f;
  if (forecast.feedVolumeL > cubeVolumeLimitL || rect.feedVolumeL < g_settings.equipment.minHeaterSubmergeL) {
    forecast.riskTone = "danger";
    forecast.riskDetail =
        "Объём загрузки выходит за безопасный диапазон куба, поэтому прогноз полезен только как черновик, а не как разрешение к старту.";
  } else if (status.indicators.floodRisk >= 0.65f) {
    forecast.riskTone = "danger";
    forecast.riskDetail =
        "Уже до старта виден повышенный риск захлёба. Лучше сначала стабилизировать колонну и охлаждение, иначе dry-run может оказаться слишком оптимистичным.";
  } else if (status.indicators.coolingMarginC <= 0.0f) {
    forecast.riskTone = "warn";
    forecast.riskDetail =
        "Cooling margin уже на нуле или ниже. Прогноз по длительности ещё полезен, но выход в стабильное тело может затянуться.";
  } else if (fillPct >= 85.0f) {
    forecast.riskTone = "warn";
    forecast.riskDetail =
        "Куб загружен почти под рабочий предел. Прогрев и стабилизация могут занять дольше среднего baseline.";
  } else if (rect.bodySpeedMlHKw <= rect.headsSpeedMlHKw) {
    forecast.riskTone = "warn";
    forecast.riskDetail =
        "Скорость тела не выше скорости голов. Такой сценарий безопасен, но обычно даёт слишком консервативный и длинный прогон.";
  } else if (!forecast.profileAligned) {
    forecast.riskTone = "warn";
    forecast.riskDetail =
        "Прогноз построен без корректного baseline профиля, поэтому длительность и энергия опираются в основном на модель текущих уставок.";
  } else if (baroPreview != nullptr && baroPreview->applicable &&
             !baroPreview->enabled &&
             fabsf(baroPreview->pressureDeltaMmHg) >= 8.0f) {
    forecast.riskTone = "warn";
    forecast.riskDetail =
        "Давление заметно ушло от baseline профиля, а барокоррекция на этот запуск отключена. Реальные cut points могут сместиться сильнее прогноза.";
  }

  return forecast;
}

static void resolveBoosterStartOverride(JsonObject params, bool &enabled,
                                        float &stopCubeTempC) {
  enabled = g_settings.equipment.boosterHeaterEnabled;
  stopCubeTempC = g_settings.equipment.boosterHeaterStopCubeTempC;

  if (params.isNull()) {
    return;
  }

  if (!params["boosterEnabled"].isNull()) {
    enabled = params["boosterEnabled"].as<bool>();
  }

  if (!params["boosterStopCubeTempC"].isNull()) {
    stopCubeTempC =
        clampFloatRange(params["boosterStopCubeTempC"].as<float>(), 20.0f, 100.0f);
  }
}

void applyBoosterStartOverride(JsonObject params, Settings &settings) {
  bool boosterEnabled = settings.equipment.boosterHeaterEnabled;
  float boosterStopCubeTempC = settings.equipment.boosterHeaterStopCubeTempC;
  resolveBoosterStartOverride(params, boosterEnabled, boosterStopCubeTempC);
  settings.equipment.boosterHeaterEnabled = boosterEnabled;
  settings.equipment.boosterHeaterStopCubeTempC = boosterStopCubeTempC;
}

bool buildProcessPreflight(JsonDocument &doc, Mode mode, const char *modeStr,
                           JsonObject params) {
  ControlV2::updateRuntime(g_state, g_settings);
  syncStirrerState();

  const auto &status = ControlV2::getLatestModeStatus();
  const auto &metrics = ControlV2::getLatestMetricsSnapshot();
  const bool lifecycleIdle = status.lifecycle == ControlV2::ModeLifecycleV2::IDLE;
  const bool v2SnapshotReady = metrics.timestampMs > 0;
  const Safety::RequiredSensorsMask requiredSensors =
      Safety::getRequiredSensorsForMode(mode, g_settings);
  const bool pressureStartupBlocking = isPressureStartupBlockingForMode(mode);
  char topologyReason[160] = "";
  const bool topologySupported = Safety::isModeTemperatureTopologySupported(
      mode, g_settings.equipment, topologyReason, sizeof(topologyReason));
  const bool tempSensorsPresent =
      g_state.health.tempSensorsTotal > 0 && g_state.health.tempSensorsOk;
  const bool sensorsFresh = tempSensorsPresent && status.indicators.sensorFreshnessOk;
  const String missingRequiredSensors =
      buildBlockingRequiredSensorsList(requiredSensors, g_state,
                                       pressureStartupBlocking);
  const bool requiredSensorsReady = missingRequiredSensors.isEmpty();
  const bool pressureSensorMissing =
      requiredSensors.pressure && !g_state.pressure.ok;
  const bool hasNonBlockingSensorGap =
      pressureSensorMissing && !pressureStartupBlocking;
  const bool allowDemoSensorFailure =
      g_settings.demoMode &&
      g_state.currentAlarm.type == AlarmType::SENSOR_FAILURE;
  const bool safetyLatched =
      (Safety::isLatched(g_state) || status.safetyLatched) &&
      !allowDemoSensorFailure;
  const bool alarmActive =
      g_state.currentAlarm.type != AlarmType::NONE && !allowDemoSensorFailure;
  const bool loggingReady =
      Logger::isSessionActive() || LittleFS.exists(LOG_FILE_PREFIX);
  const bool coolingRelevant = isCoolingRelevantForMode(mode);
  const bool recipeProfileRelevant =
      mode == Mode::RECTIFICATION || mode == Mode::MANUAL_RECT ||
      mode == Mode::DISTILLATION || mode == Mode::MASHING;
  const bool expectsAutoStirrer = expectsAutoStirrerForMode(mode, g_settings);
  const bool waterTelemetryReady =
      g_state.temps.valid[TEMP_WATER_IN] && g_state.temps.valid[TEMP_WATER_OUT];
  const bool adaptiveColumnTopRecommended = requiresAdaptiveColumnTopForMode(mode);
  const bool adaptiveColumnTopReady =
      !adaptiveColumnTopRecommended || g_state.temps.valid[TEMP_COLUMN_TOP];
  const float cubeVolumeLimitL = g_settings.equipment.cubeVolumeL;
  const float minSubmergeL = g_settings.equipment.minHeaterSubmergeL;
  const float absCubePressure =
      g_state.pressure.cube < 0.0f ? -g_state.pressure.cube : g_state.pressure.cube;
  const String activeProfileId = getActiveProfileId();
  Profile activeProfile;
  const bool activeProfileLoaded =
      !activeProfileId.isEmpty() && loadProfile(activeProfileId, activeProfile);
  const bool rectProfile =
      activeProfileLoaded &&
      (activeProfile.metadata.category == "rectification" ||
       activeProfile.parameters.mode == "rectification");
  const bool distProfile =
      activeProfileLoaded &&
      (activeProfile.metadata.category == "distillation" ||
       activeProfile.parameters.mode == "distillation");
  const bool mashProfile =
      activeProfileLoaded &&
      (activeProfile.metadata.category == "mashing" ||
       activeProfile.parameters.mode == "mashing");
  const bool profileCategoryMatches =
      ((mode == Mode::RECTIFICATION || mode == Mode::MANUAL_RECT) && rectProfile) ||
      (mode == Mode::DISTILLATION && distProfile) ||
      (mode == Mode::MASHING && mashProfile);
  bool hasBaroPreview = false;
  bool hasDryRunForecast = false;
  ProfileBaroCorrectionSummary baroPreview;
  TemperatureParams baroEffectiveTemps;
  ProcessDryRunForecast dryRunForecast;

  uint8_t blockingCount = 0;
  uint8_t warningCount = 0;
  String firstBlockingDetail;
  String firstWarningDetail;

  JsonArray items = doc["items"].to<JsonArray>();
  JsonObject checks = doc["checks"].to<JsonObject>();
  setProcessPreflightCheck(checks, "v2", v2SnapshotReady ? "OK" : "Нет пакета",
                           v2SnapshotReady ? "good" : "danger");
  setProcessPreflightCheck(checks, "sensors",
                           (topologySupported && requiredSensorsReady &&
                            !hasNonBlockingSensorGap && sensorsFresh &&
                            adaptiveColumnTopReady)
                               ? "OK"
                               : "Проверьте",
                           (!topologySupported || !requiredSensorsReady || !sensorsFresh)
                               ? "danger"
                               : ((requiredSensorsReady && adaptiveColumnTopReady &&
                                   !hasNonBlockingSensorGap)
                                      ? "good"
                                                                                   : "warn"));
  setProcessPreflightCheck(checks, "safety",
                           safetyLatched ? "Latch" : "Норма",
                           safetyLatched ? "danger" : "good");
  setProcessPreflightCheck(checks, "alarm", alarmActive ? "Активна" : "Нет",
                           alarmActive ? "danger" : "good");

  setProcessPreflightCheck(
      checks, "profile",
      recipeProfileRelevant
          ? (profileCategoryMatches ? "OK"
                                    : (activeProfileLoaded ? "Mismatch"
                                                           : "Не указан"))
          : "Опц.",
      recipeProfileRelevant ? (profileCategoryMatches ? "good" : "warn")
                            : "muted");
  setProcessPreflightCheck(checks, "water",
                           coolingRelevant
                               ? (waterTelemetryReady ? "OK"
                                                      : "Проверьте")
                               : "Не нужно",
                           coolingRelevant
                               ? (waterTelemetryReady ? "good" : "warn")
                               : "muted");

  auto addItem = [&](const char *id, const char *tone, const char *title,
                     const String &detail, bool blocking) {
    appendProcessPreflightItem(items, id, tone, title, detail, blocking,
                               blockingCount, warningCount);
    if (blocking && firstBlockingDetail.isEmpty()) {
      firstBlockingDetail = detail;
    } else if (strcmp(tone, "warn") == 0 && firstWarningDetail.isEmpty()) {
      firstWarningDetail = detail;
    }
  };

  if (!lifecycleIdle || g_state.mode != Mode::IDLE) {
    addItem("process-active", "danger", "Активный процесс",
            "Автоматика ещё не вернулась в idle. Новый режим сначала нужно "
            "запускать только после явной остановки текущего процесса.",
            true);
  } else {
    addItem("process-active", "good", "Активный процесс",
            "Система находится в idle и готова принять новый сценарий.",
            false);
  }

  if (!v2SnapshotReady) {
    addItem("v2", "danger", "Контур indicators v2",
            "Backend ещё не собрал осмысленный v2 snapshot. Перед стартом "
            "нужен хотя бы один живой пакет статуса.",
            true);
  } else {
    addItem("v2", "good", "Контур indicators v2",
            "Контур indicators v2 уже отдал рабочий snapshot для проверки старта.",
            false);
  }

  if (!topologySupported) {
    addItem("sensors", "danger", "Топология термодатчиков",
            topologyReason[0] != '\0'
                ? String(topologyReason)
                : String("Текущая конфигурация оборудования не поддерживает этот режим."),
            true);
  } else if (!requiredSensorsReady) {
    addItem("sensors", "danger", "Обязательные датчики",
            String("Для выбранного режима не хватает обязательных датчиков: ") +
                missingRequiredSensors + ".",
            true);
  } else if (hasNonBlockingSensorGap) {
    addItem("sensors", "warn", "Обязательные датчики",
            "Датчик давления куба сейчас не подтверждён. Старт для этого режима не блокируется, "
            "но процесс нужно вести с усиленным операторским контролем.",
            false);
  } else if (!sensorsFresh) {
    addItem("sensors", "danger", "Свежесть телеметрии",
            "Телеметрия устарела. Перед стартом дождитесь свежих данных от датчиков.",
            true);
  } else if (!adaptiveColumnTopReady) {
    addItem("sensors", "warn", "Телеметрия процесса",
            "Верх колонны сейчас не читается. Жёсткого блока старта нет, но "
            "adaptive-логика ректификации будет работать консервативно.",
            false);
  } else {
    addItem("sensors", "good", "Обязательные датчики",
            "Для выбранного режима обязательные датчики на связи, телеметрия выглядит свежей.",
            false);
  }

  if (safetyLatched) {
    addItem("safety", "danger", "Safety latch",
            g_state.currentAlarm.message[0] != '\0'
                ? String(g_state.currentAlarm.message)
                : String("Есть активный safety latch. Сначала снимите блокировку и "
                         "разберитесь с причиной trip."),
            true);
  } else {
    addItem("safety", "good", "Safety latch",
            "Safety latch сейчас не активен.", false);
  }

  if (alarmActive) {
    addItem("alarm", "danger", "Активная авария",
            g_state.currentAlarm.message[0] != '\0'
                ? String(g_state.currentAlarm.message)
                : String("В системе есть активная авария. Перед стартом её нужно "
                         "подтвердить и сбросить."),
            true);
  } else {
    addItem("alarm", "good", "Активная авария",
            "Активных аварий сейчас нет.", false);
  }

  if (!loggingReady) {
    addItem("logging", "warn", "Журналирование",
            "Лог-файл сейчас не выглядит готовым. Старт возможен, но post-mortem "
            "и run report могут оказаться неполными.",
            false);
  } else {
    addItem("logging", "good", "Журналирование",
            "Лог-файл активен, запуск будет записан в историю и системный журнал.",
            false);
  }

  if (recipeProfileRelevant) {
    if (!activeProfileLoaded) {
      addItem("profile", "warn", "Активный профиль",
              "Режим запустится без привязанного профиля. Сравнивать прогоны "
              "и учиться на успешных рецептах будет сложнее.",
              false);
    } else if (!profileCategoryMatches) {
      addItem(
          "profile", "warn", "Активный профиль",
          String("Сейчас активен профиль '") +
              activeProfile.metadata.name +
              "', но его категория не совпадает с выбранным режимом.",
          false);
    } else {
      addItem("profile", "good", "Активный профиль",
              String("Для запуска привязан профиль '") +
                  activeProfile.metadata.name + "'.",
              false);
    }
  }

  if (coolingRelevant) {
    const bool coolingCritical =
        g_state.temps.tsa >= g_settings.safety.tsaMaxC ||
        g_state.temps.waterOut >= g_settings.safety.waterOutMaxC;
    const bool coolingWarmStart =
        g_state.temps.cube >= g_settings.equipment.waterAutoStartCubeTempC &&
        !Valves::getWater();
    const bool coolingMarginLow = status.indicators.coolingMarginC <= 0.0f;

    if (!waterTelemetryReady) {
      addItem("water", "warn", "Вода и телеметрия",
              "Для охлаждаемого режима не видна полная телеметрия по воде. "
              "Запуск возможен, но операторский контроль нужно усилить.",
              false);
    } else if (coolingWarmStart) {
      addItem("water", "warn", "Вода и телеметрия",
              "Куб уже прогрет, а вода ещё не открыта. Лучше подготовить охлаждение "
              "до финального старта.",
              false);
    } else {
      addItem("water", "good", "Вода и телеметрия",
              "Датчики воды на связи, контур готов к запуску.",
              false);
    }

    if (coolingCritical) {
      addItem("cooling", "danger", "Контур охлаждения",
              "Охлаждение уже у опасной границы по TSA или температуре воды. "
              "Запуск нужно отложить до нормализации контура.",
              true);
    } else if (coolingWarmStart || coolingMarginLow) {
      addItem("cooling", "warn", "Контур охлаждения",
              coolingWarmStart
                  ? String("Куб уже тёплый, а вода не открыта. Перед стартом лучше "
                           "подготовить охлаждение заранее.")
                  : String("Cooling margin уже на нуле или ниже. Старт возможен, "
                           "но колонне будет тяжело выйти в стабильный режим."),
              false);
    } else {
      addItem("cooling", "good", "Контур охлаждения",
              "Охлаждение перед стартом выглядит в рабочем диапазоне.", false);
    }
  }

  if (!requiredSensors.pressure) {
    addItem("pressure", "muted", "Давление",
            "Для выбранного режима датчик давления не является обязательным условием старта.",
            false);
  } else if (!g_state.pressure.ok) {
    addItem("pressure", pressureStartupBlocking ? "danger" : "warn",
            "Давление",
            pressureStartupBlocking
                ? String("Датчик давления куба сейчас не подтверждён. Для этого режима старт лучше не продолжать без манометра.")
                : String("Датчик давления куба сейчас не подтверждён. Старт возможен, но манометр остаётся рекомендованным, а контроль процесса нужно вести внимательнее."),
            pressureStartupBlocking);
  } else if (absCubePressure >= g_settings.safety.pressureMaxMmHg) {
    addItem("pressure", "danger", "Давление",
            "Давление в кубе уже выше безопасного порога. Сначала устраните "
            "причину, затем возвращайтесь к старту.",
            true);
  } else if (!status.indicators.pressureStable ||
             absCubePressure >= g_settings.safety.pressureMaxMmHg * 0.7f) {
    addItem("pressure", "warn", "Давление",
            !status.indicators.pressureStable
                ? String("Давление ещё не выглядит стабильным. Для спокойного старта "
                         "лучше дождаться ровной линии.")
                : String("Давление уже заметно выше обычного фона. Проверьте колонну, "
                         "чтобы не стартовать в напряжённом режиме."),
            false);
  } else {
    addItem("pressure", "good", "Давление",
            "Давление перед стартом выглядит нормальным и стабильным.", false);
  }

  const bool takeoffValvesOpen = Valves::getHeads() || Valves::getUno();
  if (takeoffValvesOpen) {
    addItem("valves", "danger", "Клапаны отбора",
            "Перед стартом уже открыт один из клапанов отбора. Сначала закройте "
            "сервисные линии, чтобы процесс не стартовал в некорректной конфигурации.",
            true);
  } else {
    addItem("valves", "good", "Клапаны отбора",
            "Линии отбора перед стартом закрыты.", false);
  }

  if (g_state.pump.running) {
    addItem("pump", "danger", "Насос",
            "Насос уже работает в idle. Похоже на сервисный хвост или ручной запуск, "
            "который нужно сначала остановить.",
            true);
  } else if (mode == Mode::NBK && status.activeLimits.pumpCapped) {
    addItem("pump", "warn", "Насос",
            "Для НБК уже активен pump cap. Старт возможен, но подача будет ограничена "
            "автоматикой.",
            false);
  } else {
    addItem("pump", "good", "Насос",
            "Насос перед стартом в спокойном состоянии.", false);
  }

  if (expectsAutoStirrer) {
    if (!g_settings.stirrer.enabled) {
      addItem("stirrer", "danger", "Мешалка",
              "Для выбранного режима ожидается auto-start мешалки, но она отключена "
              "в настройках оборудования.",
              true);
    } else if (!g_state.stirrer.available) {
      addItem("stirrer", "danger", "Мешалка",
              "Для выбранного режима нужна мешалка, но DAC/драйвер мешалки сейчас "
              "не виден системе.",
              true);
    } else {
      addItem("stirrer", "good", "Мешалка",
              "Авто-режим мешалки доступен и готов к запуску вместе со сценарием.",
              false);
    }
  } else if (g_state.stirrer.running) {
    addItem("stirrer", "warn", "Мешалка",
            "Мешалка уже крутится вручную. Проверьте, точно ли это нужно для "
            "выбранного режима.",
            false);
  }

  if (mode == Mode::RECTIFICATION) {
    RectParams rect = g_settings.rectParams;
    if (!params.isNull()) {
      if (!params["feedstock"].isNull()) {
        rect.feedstock =
            static_cast<uint8_t>(clampU16Range(params["feedstock"].as<uint32_t>(), 0, 7));
      }
      if (!params["feedVolumeL"].isNull()) {
        rect.feedVolumeL =
            clampFloatRange(params["feedVolumeL"].as<float>(), 1.0f, 250.0f);
      }
      if (!params["feedAbvPercent"].isNull()) {
        rect.feedAbvPercent =
            clampFloatRange(params["feedAbvPercent"].as<float>(), 1.0f, 96.0f);
      }
      if (!params["headsPercent"].isNull()) {
        rect.headsPercent =
            clampFloatRange(params["headsPercent"].as<float>(), 0.0f, 40.0f);
      }
      if (!params["bodyPercent"].isNull()) {
        rect.bodyPercent =
            clampFloatRange(params["bodyPercent"].as<float>(), 0.0f, 100.0f);
      }
      if (!params["tailsPercent"].isNull()) {
        rect.tailsPercent =
            clampFloatRange(params["tailsPercent"].as<float>(), 0.0f, 100.0f);
      }
      if (!params["headsSpeedMlHKw"].isNull()) {
        rect.headsSpeedMlHKw =
            clampFloatRange(params["headsSpeedMlHKw"].as<float>(), 10.0f, 2000.0f);
      }
      if (!params["bodySpeedMlHKw"].isNull()) {
        rect.bodySpeedMlHKw =
            clampFloatRange(params["bodySpeedMlHKw"].as<float>(), 50.0f, 3000.0f);
      }
      if (!params["stabilizationMin"].isNull()) {
        rect.stabilizationMin =
            clampU16Range(params["stabilizationMin"].as<uint32_t>(), 1, 180);
      }
      if (!params["purgeMin"].isNull()) {
        rect.purgeMin = clampU16Range(params["purgeMin"].as<uint32_t>(), 1, 120);
      }
      if (!params["baroCorrectionEnabled"].isNull()) {
        rect.baroCorrectionEnabled = params["baroCorrectionEnabled"].as<bool>();
      }
    }

    const float fractionsSum =
        rect.headsPercent + rect.bodyPercent + rect.tailsPercent;
    if (rect.feedVolumeL > cubeVolumeLimitL) {
      addItem("rect-profile", "danger", "Параметры запуска режима",
              "Объём сырца превышает полезный объём куба. Исправьте загрузку перед стартом.",
              true);
    } else if (rect.feedVolumeL < minSubmergeL) {
      addItem("rect-profile", "danger", "Параметры запуска режима",
              "Объём сырца ниже минимального уровня для безопасного погружения ТЭНа.",
              true);
    } else if (fractionsSum > 100.0f || rect.bodySpeedMlHKw <= rect.headsSpeedMlHKw) {
      addItem("rect-profile", "warn", "Параметры запуска режима",
              fractionsSum > 100.0f
                  ? String("Сумма фракций выше 100%. Проверьте профиль перед реальным запуском.")
                  : String("Скорость тела не выше скорости голов. Для ректификации это обычно "
                           "слишком консервативный профиль."),
              false);
    } else {
      addItem("rect-profile", "good", "Параметры запуска режима",
              "Объём, фракции и скорости ректификации выглядят согласованно.", false);
    }

    if (activeProfileLoaded && rectProfile) {
      baroEffectiveTemps = getEffectiveProfileTemperatures(
          activeProfile, &baroPreview, rect.baroCorrectionEnabled ? 1 : 0);
      hasBaroPreview = true;

      if (!baroPreview.enabled) {
        addItem("baro", "muted", "Барокоррекция профиля",
                "Барокоррекция отключена для этого запуска. Пороги останутся как в сохранённом профиле.",
                false);
      } else if (!baroPreview.applicable) {
        addItem("baro", "warn", "Барокоррекция профиля",
                baroPreview.note.isEmpty()
                    ? String("Для мягкой барокоррекции пока не хватает baseline по давлению или текущих данных BMP280.")
                    : baroPreview.note,
                false);
      } else if (baroPreview.applied) {
        const String signedShift =
            String(baroPreview.appliedShiftC >= 0.0f ? "+" : "") +
            String(baroPreview.appliedShiftC, 2);
        addItem(
            "baro", "warn", "Барокоррекция профиля",
            String("Профиль валидирован при ") +
                String(baroPreview.baselinePressureMmHg, 1) +
                " мм рт.ст., сейчас " +
                String(baroPreview.currentPressureMmHg, 1) +
                " мм рт.ст. Перед стартом пороги будут мягко сдвинуты на " +
                signedShift + "°C: головы " +
                String(activeProfile.parameters.temperatures.headsEnd, 2) + " → " +
                String(baroEffectiveTemps.headsEnd, 2) + "°C, тело " +
                String(activeProfile.parameters.temperatures.bodyStart, 2) + " → " +
                String(baroEffectiveTemps.bodyStart, 2) + "°C.",
            false);
      } else {
        addItem("baro", "good", "Барокоррекция профиля",
                "Барокоррекция включена, но текущее отклонение давления слишком мало и заметный сдвиг порогов не требуется.",
                false);
      }
    }
    dryRunForecast = buildRectificationDryRunForecast(
        rect, status, rectProfile ? &activeProfile : nullptr,
        hasBaroPreview ? &baroPreview : nullptr, profileCategoryMatches,
        cubeVolumeLimitL);
    hasDryRunForecast = dryRunForecast.supported;
  } else if (mode == Mode::MANUAL_RECT) {
    const JsonObject feed = params["feed"].as<JsonObject>();
    const JsonObject heads = params["heads"].as<JsonObject>();
    const JsonObject body = params["body"].as<JsonObject>();
    const JsonObject tails = params["tails"].as<JsonObject>();
    const float volumeL = !feed.isNull() ? clampFloatRange(feed["volumeL"] | 20.0f, 1.0f, 250.0f)
                                         : g_settings.rectParams.feedVolumeL;
    const float headsVolume = !heads.isNull() ? clampFloatRange(heads["volume"] | 50.0f, 0.0f, 5000.0f) : 50.0f;
    const float spikeThreshold = !body.isNull() ? clampFloatRange(body["spikeThreshold"] | 0.2f, 0.01f, 10.0f) : 0.2f;
    const bool tailsEnabled = !tails.isNull() ? (tails["enabled"] | false) : false;

    if (volumeL > cubeVolumeLimitL) {
      addItem("manual-profile", "danger", "Параметры ручной ректификации",
              "Объём сырца превышает полезный объём куба.", true);
    } else if (volumeL < minSubmergeL) {
      addItem("manual-profile", "danger", "Параметры ручной ректификации",
              "Объём ниже минимального уровня безопасного погружения ТЭНа.", true);
    } else if (headsVolume <= 0.0f || spikeThreshold <= 0.0f || !tailsEnabled) {
      addItem("manual-profile", "warn", "Параметры ручной ректификации",
              !tailsEnabled
                  ? String("Хвостовая часть отключена. Это допустимо, но убедитесь, что такой сценарий осознан.")
                  : String("Проверьте объём голов и критерий перехода в хвосты перед запуском."),
              false);
    } else {
      addItem("manual-profile", "good", "Параметры ручной ректификации",
              "Ручной профиль выглядит полным и согласованным.", false);
    }
  } else if (mode == Mode::DISTILLATION) {
    const float endTemp =
        !params["endTemp"].isNull() ? clampFloatRange(params["endTemp"].as<float>(), 70.0f, 110.0f)
                                     : g_settings.distillationUi.endTempC;
    const float heaterMaxW =
        g_settings.equipment.heaterPowerW > 0 ? g_settings.equipment.heaterPowerW
                                              : DEFAULT_HEATER_POWER_W;
    const float powerWatts =
        !params["powerW"].isNull()
            ? clampFloatRange(params["powerW"].as<float>(), 0.0f, heaterMaxW)
            : (!params["powerPercent"].isNull()
                   ? heaterMaxW *
                         clampFloatRange(params["powerPercent"].as<float>(), 0.0f, 100.0f) /
                         100.0f
                   : g_settings.distillationUi.powerW);

    if (powerWatts <= 0.0f) {
      addItem("dist-profile", "danger", "Параметры дистилляции",
              "Нулевая мощность нагрева не имеет смысла для старта дистилляции.", true);
    } else if (endTemp < 88.0f || endTemp > 100.0f) {
      addItem("dist-profile", "warn", "Параметры дистилляции",
              "Стоп-температура выглядит нетипично. Проверьте, точно ли это нужный сценарий.",
              false);
    } else {
      addItem("dist-profile", "good", "Параметры дистилляции",
              "Порог окончания и мощность дистилляции выглядят рабочими.", false);
    }
  } else if (mode == Mode::MASHING) {
    JsonArray steps = params["profile"]["steps"].as<JsonArray>();
    uint8_t validSteps = 0;
    for (JsonObject step : steps) {
      const float temperature = step["temperature"] | 0.0f;
      const uint16_t duration = step["duration"] | 0;
      if (temperature > 0.0f && duration > 0) {
        validSteps++;
      }
    }

    if (validSteps == 0) {
      addItem("mash-profile", "danger", "Профиль затирки",
              "Для старта затирки нужен хотя бы один корректный шаг с температурой и длительностью.",
              true);
    } else {
      addItem("mash-profile", "good", "Профиль затирки",
              String("Подготовлено шагов затирки: ") + validSteps + ".", false);
    }
  } else if (mode == Mode::HOLD) {
    JsonArray steps = params["steps"].as<JsonArray>();
    uint8_t validSteps = 0;
    for (JsonObject step : steps) {
      const uint16_t duration = step["duration"] | 0;
      if (duration > 0) {
        validSteps++;
      }
    }

    if (validSteps == 0) {
      addItem("hold-profile", "danger", "Профиль пастеризации",
              "Для старта нужен хотя бы один шаг или пауза с длительностью.", true);
    } else {
      addItem("hold-profile", "good", "Профиль пастеризации",
              String("Подготовлено шагов: ") + validSteps + ".", false);
    }
  } else if (mode == Mode::NBK) {
    const float powerW = !params["powerW"].isNull()
                             ? clampFloatRange(params["powerW"].as<float>(), 500.0f, 5500.0f)
                             : g_settings.nbk.powerW;
    const float pumpSpeedMlH =
        !params["pumpSpeedMlH"].isNull()
            ? clampFloatRange(params["pumpSpeedMlH"].as<float>(), 100.0f, 120000.0f)
            : g_settings.nbk.pumpSpeedMlH;
    const float thresholdC =
        !params["columnBottomTempThresholdC"].isNull()
            ? clampFloatRange(params["columnBottomTempThresholdC"].as<float>(), 50.0f, 110.0f)
            : g_settings.nbk.columnBottomTempThresholdC;

    if (powerW < 1000.0f || pumpSpeedMlH < 500.0f) {
      addItem("nbk-profile", "warn", "Параметры НБК",
              "Мощность или подача заданы очень низко. НБК может долго не войти в рабочий режим.",
              false);
    } else if (thresholdC < 85.0f || thresholdC > 100.0f) {
      addItem("nbk-profile", "warn", "Параметры НБК",
              "Порог температуры низа колонны выглядит нетипично. Проверьте уставку перед стартом.",
              false);
    } else {
      addItem("nbk-profile", "good", "Параметры НБК",
              "Мощность, подача и защитный порог НБК выглядят согласованно.", false);
    }
  } else if (mode == Mode::FERMENTATION) {
    const float targetTempC =
        !params["targetTempC"].isNull()
            ? clampFloatRange(params["targetTempC"].as<float>(), 5.0f, 45.0f)
            : g_settings.fermentation.targetTempC;
    const float hysteresisC =
        !params["hysteresisC"].isNull()
            ? clampFloatRange(params["hysteresisC"].as<float>(), 0.1f, 10.0f)
            : g_settings.fermentation.hysteresisC;

    if (targetTempC < 18.0f || targetTempC > 32.0f) {
      addItem("fermentation-profile", "warn", "Параметры ферментации",
              "Целевая температура брожения выглядит нестандартно. Проверьте рецепт и культуру.",
              false);
    } else if (hysteresisC < 0.2f || hysteresisC > 2.0f) {
      addItem("fermentation-profile", "warn", "Параметры ферментации",
              "Гистерезис для брожения выглядит нетипично и может дать лишние качели по температуре.",
              false);
    } else {
      addItem("fermentation-profile", "good", "Параметры ферментации",
              "Цель и гистерезис ферментации выглядят рабочими.", false);
    }
  }

  if (mode == Mode::RECTIFICATION || mode == Mode::DISTILLATION ||
      mode == Mode::NBK) {
    bool boosterEnabled = false;
    float boosterStopCubeTempC = 78.0f;
    resolveBoosterStartOverride(params, boosterEnabled, boosterStopCubeTempC);

    if (!boosterEnabled) {
      addItem("booster", "muted", "Разгонный ТЭН",
              "Booster SSR для этого запуска отключён. Разогрев пойдёт только через основной TRIAC.",
              false);
    } else if (g_state.temps.cube > 0.0f &&
               g_state.temps.cube >= boosterStopCubeTempC) {
      addItem("booster", "warn", "Разгонный ТЭН",
              String("Booster SSR включён, но куб уже ") +
                  String(g_state.temps.cube, 1) + " °C и выше порога " +
                  String(boosterStopCubeTempC, 1) +
                  " °C. На старте он может почти сразу не понадобиться.",
              false);
    } else {
      addItem("booster", "good", "Разгонный ТЭН",
              String("Booster SSR будет работать только на фазе разогрева и отключится при ") +
                  String(boosterStopCubeTempC, 1) + " °C по кубу.",
              false);
    }
  }

  const bool ready = blockingCount == 0;
  doc["success"] = true;
  doc["mode"] = modeStr;
  doc["ready"] = ready;
  doc["blockingCount"] = blockingCount;
  doc["warningCount"] = warningCount;
  const float processHealthPct =
      clampFloatRange(status.indicators.processHealth * 100.0f, 0.0f, 100.0f);
  const float stabilityPct =
      clampFloatRange(status.indicators.stabilityIndex * 100.0f, 0.0f, 100.0f);
  const float decisionConfidencePct =
      clampFloatRange((processHealthPct * 0.55f) + (stabilityPct * 0.45f),
                      0.0f, 100.0f);
  const float startupConfidencePct =
      clampFloatRange(decisionConfidencePct - (blockingCount * 34.0f) -
                          (warningCount * 9.0f),
                      ready ? 18.0f : 0.0f, 100.0f);

  if (!ready) {
    doc["tone"] = "danger";
    doc["title"] = "Запуск заблокирован";
    doc["detail"] = firstBlockingDetail.isEmpty()
                        ? "Есть блокирующие условия, которые нужно снять до старта."
                        : firstBlockingDetail;
  } else if (warningCount > 0) {
    doc["tone"] = "warn";
    doc["title"] = "Старт возможен с оговорками";
    doc["detail"] = firstWarningDetail.isEmpty()
                        ? "Критичных блокировок нет, но часть условий требует внимания."
                        : firstWarningDetail;
  } else {
    doc["tone"] = "good";
    doc["title"] = "Можно запускать";
    doc["detail"] =
        "Основные проверки backend pre-flight пройдены, режим готов к запуску.";
  }

  JsonObject advisor = doc["advisor"].to<JsonObject>();
  advisor["tone"] = ready ? (warningCount > 0 ? "warn" : status.guidance.tone)
                          : "danger";
  advisor["title"] =
      ready ? ((status.guidance.title[0] != '\0') ? status.guidance.title
                                                  : doc["title"].as<const char*>())
            : doc["title"].as<const char*>();
  advisor["detail"] = ready
                          ? ((status.guidance.detail[0] != '\0')
                                 ? status.guidance.detail
                                 : doc["detail"].as<const char*>())
                          : doc["detail"].as<const char*>();
  advisor["action"] = ready
                          ? ((status.guidance.action[0] != '\0')
                                 ? status.guidance.action
                                 : "Перед стартом ещё раз проверьте ключевые уставки и готовность охлаждения.")
                          : "Снимите критичные блокировки в чек-листе, после чего повторите старт.";

  JsonObject advisorProfile = advisor["profile"].to<JsonObject>();
  advisorProfile["relevant"] = recipeProfileRelevant;
  advisorProfile["loaded"] = activeProfileLoaded;
  advisorProfile["matchesMode"] = profileCategoryMatches;
  advisorProfile["id"] = activeProfileLoaded ? activeProfile.id : activeProfileId;
  advisorProfile["name"] = activeProfileLoaded ? activeProfile.metadata.name : "";
  advisorProfile["category"] =
      activeProfileLoaded ? activeProfile.metadata.category : "";

  if (hasBaroPreview) {
    JsonObject advisorBaro = advisor["baroCorrection"].to<JsonObject>();
    advisorBaro["enabled"] = baroPreview.enabled;
    advisorBaro["applicable"] = baroPreview.applicable;
    advisorBaro["applied"] = baroPreview.applied;
    advisorBaro["baselinePressureMmHg"] = baroPreview.baselinePressureMmHg;
    advisorBaro["currentPressureMmHg"] = baroPreview.currentPressureMmHg;
    advisorBaro["pressureDeltaMmHg"] = baroPreview.pressureDeltaMmHg;
    advisorBaro["appliedShiftC"] = baroPreview.appliedShiftC;
    advisorBaro["note"] = baroPreview.note;
    JsonObject effectiveTemps = advisorBaro["effectiveTemperatures"].to<JsonObject>();
    effectiveTemps["headsEnd"] = baroEffectiveTemps.headsEnd;
    effectiveTemps["bodyStart"] = baroEffectiveTemps.bodyStart;
    effectiveTemps["bodyEnd"] = baroEffectiveTemps.bodyEnd;
  }

  if (hasDryRunForecast) {
    JsonObject advisorDryRun = advisor["dryRun"].to<JsonObject>();
    advisorDryRun["supported"] = dryRunForecast.supported;
    advisorDryRun["usesLearning"] = dryRunForecast.usesLearning;
    advisorDryRun["profileAligned"] = dryRunForecast.profileAligned;
    advisorDryRun["durationSource"] = dryRunForecast.durationSource;
    advisorDryRun["energySource"] = dryRunForecast.energySource;
    advisorDryRun["summary"] = dryRunForecast.summary;
    advisorDryRun["riskTone"] = dryRunForecast.riskTone;
    advisorDryRun["riskTitle"] = dryRunForecast.riskTitle;
    advisorDryRun["riskDetail"] = dryRunForecast.riskDetail;
    advisorDryRun["baselineProcessId"] = dryRunForecast.baselineProcessId;
    advisorDryRun["totalMin"] = dryRunForecast.totalMin;
    advisorDryRun["heatingMin"] = dryRunForecast.heatingMin;
    advisorDryRun["preparationMin"] = dryRunForecast.preparationMin;
    advisorDryRun["takeoffMin"] = dryRunForecast.takeoffMin;
    advisorDryRun["energyKwh"] = dryRunForecast.energyKwh;
    advisorDryRun["baselineDurationMin"] = dryRunForecast.baselineDurationMin;
    advisorDryRun["baselineEnergyKwh"] = dryRunForecast.baselineEnergyKwh;

    JsonObject forecastCharge = advisorDryRun["charge"].to<JsonObject>();
    forecastCharge["feedVolumeL"] = dryRunForecast.feedVolumeL;
    forecastCharge["feedAbvPercent"] = dryRunForecast.feedAbvPercent;
    forecastCharge["absoluteAlcoholMl"] = dryRunForecast.absoluteAlcoholMl;

    JsonObject forecastVolumes = advisorDryRun["volumes"].to<JsonObject>();
    forecastVolumes["headsMl"] = dryRunForecast.headsMl;
    forecastVolumes["bodyMl"] = dryRunForecast.bodyMl;
    forecastVolumes["tailsMl"] = dryRunForecast.tailsMl;

    JsonObject forecastSpeeds = advisorDryRun["speeds"].to<JsonObject>();
    forecastSpeeds["headsMlH"] = dryRunForecast.headsSpeedMlH;
    forecastSpeeds["bodyMlH"] = dryRunForecast.bodySpeedMlH;
    forecastSpeeds["tailsMlH"] = dryRunForecast.tailsSpeedMlH;
  }

  JsonObject confidence = advisor["confidence"].to<JsonObject>();
  confidence["startup"] = startupConfidencePct;
  confidence["decision"] = decisionConfidencePct;
  confidence["processHealth"] = processHealthPct;
  confidence["stability"] = stabilityPct;
  if (status.indicators.takeoffConfidence >= 0.0f) {
    confidence["takeoff"] =
        clampFloatRange(status.indicators.takeoffConfidence * 100.0f, 0.0f, 100.0f);
  }
  if (status.indicators.headsEndConfidence >= 0.0f) {
    confidence["headsEnd"] = clampFloatRange(
        status.indicators.headsEndConfidence * 100.0f, 0.0f, 100.0f);
  }
  if (status.indicators.bodyEndConfidence >= 0.0f) {
    confidence["bodyEnd"] = clampFloatRange(
        status.indicators.bodyEndConfidence * 100.0f, 0.0f, 100.0f);
  }
  if (status.indicators.tailsTransitionConfidence >= 0.0f) {
    confidence["tails"] = clampFloatRange(
        status.indicators.tailsTransitionConfidence * 100.0f, 0.0f, 100.0f);
  }
  confidence["powerLimit"] = clampFloatRange(
      status.indicators.powerLimitConfidence * 100.0f, 0.0f, 100.0f);

  return ready;
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
  WebServerLive::bindWebSocket(&ws);

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
#if 0
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    ControlV2::updateRuntime(g_state, g_settings);
    syncStirrerState();

    JsonDocument doc;
    const String activeProfileId = getActiveProfileId();
    Profile activeProfile;
    const bool activeProfileLoaded =
        !activeProfileId.isEmpty() && loadProfile(activeProfileId, activeProfile);

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
    JsonObject activeProfileJson = doc["activeProfile"].to<JsonObject>();
    activeProfileJson["id"] =
        activeProfileLoaded ? activeProfile.id : activeProfileId;
    activeProfileJson["loaded"] = activeProfileLoaded;
    activeProfileJson["name"] =
        activeProfileLoaded ? activeProfile.metadata.name : "";
    activeProfileJson["category"] =
        activeProfileLoaded ? activeProfile.metadata.category : "";

    if (activeProfileLoaded) {
      JsonObject validation = activeProfileJson["validation"].to<JsonObject>();
      validation["validatedAt"] = activeProfile.validation.validatedAt;
      validation["sourceProcessId"] = activeProfile.validation.sourceProcessId;
      validation["atmosphereMmHg"] = activeProfile.validation.atmosphereMmHg;
      validation["columnHeightMm"] = activeProfile.validation.columnHeightMm;
      validation["packingType"] = activeProfile.validation.packingType;
      validation["packingCoeff"] = activeProfile.validation.packingCoeff;
      validation["heaterPowerW"] = activeProfile.validation.heaterPowerW;
      validation["targetPowerW"] = activeProfile.validation.targetPowerW;
      validation["feedVolumeL"] = activeProfile.validation.feedVolumeL;
      validation["feedAbvPercent"] = activeProfile.validation.feedAbvPercent;

      JsonObject baseTemperatures =
          activeProfileJson["baseTemperatures"].to<JsonObject>();
      baseTemperatures["maxCube"] = activeProfile.parameters.temperatures.maxCube;
      baseTemperatures["maxColumn"] =
          activeProfile.parameters.temperatures.maxColumn;
      baseTemperatures["headsEnd"] =
          activeProfile.parameters.temperatures.headsEnd;
      baseTemperatures["bodyStart"] =
          activeProfile.parameters.temperatures.bodyStart;
      baseTemperatures["bodyEnd"] =
          activeProfile.parameters.temperatures.bodyEnd;

      ProfileBaroCorrectionSummary baroPreview =
          evaluateProfileBaroCorrection(activeProfile, 1);
      TemperatureParams previewTemps =
          getEffectiveProfileTemperatures(activeProfile, nullptr, 1);

      JsonObject baroPreviewJson =
          activeProfileJson["baroPreview"].to<JsonObject>();
      baroPreviewJson["enabled"] = true;
      baroPreviewJson["applicable"] = baroPreview.applicable;
      baroPreviewJson["applied"] = baroPreview.applied;
      baroPreviewJson["baselinePressureMmHg"] =
          baroPreview.baselinePressureMmHg;
      baroPreviewJson["currentPressureMmHg"] =
          baroPreview.currentPressureMmHg;
      baroPreviewJson["pressureDeltaMmHg"] = baroPreview.pressureDeltaMmHg;
      baroPreviewJson["boilingShiftC"] = baroPreview.boilingShiftC;
      baroPreviewJson["appliedShiftC"] = baroPreview.appliedShiftC;
      baroPreviewJson["strength"] = baroPreview.strength;
      baroPreviewJson["maxShiftC"] = baroPreview.maxShiftC;
      baroPreviewJson["note"] = baroPreview.note;

      JsonObject previewEffectiveTemps =
          activeProfileJson["effectiveTemperaturesPreview"].to<JsonObject>();
      previewEffectiveTemps["maxCube"] = previewTemps.maxCube;
      previewEffectiveTemps["maxColumn"] = previewTemps.maxColumn;
      previewEffectiveTemps["headsEnd"] = previewTemps.headsEnd;
      previewEffectiveTemps["bodyStart"] = previewTemps.bodyStart;
      previewEffectiveTemps["bodyEnd"] = previewTemps.bodyEnd;

      if (activeProfile.metadata.category == "mashing" ||
          activeProfile.parameters.mode == "mashing") {
        JsonObject activeMashing = activeProfileJson["mashing"].to<JsonObject>();
        JsonArray activeMashingSteps = activeMashing["steps"].to<JsonArray>();
        for (const auto& stepData : activeProfile.parameters.mashing.steps) {
          JsonObject step = activeMashingSteps.add<JsonObject>();
          step["temperature"] = stepData.temperature;
          step["duration"] = stepData.duration;
          step["name"] = stepData.name;
        }
      }
    }

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
    const Heater::Diagnostics heaterDiag = Heater::getDiagnostics();
    power["voltage"] = g_state.power.voltage;
    power["current"] = g_state.power.current;
    power["power"] = g_state.power.power;
    power["available"] = g_state.health.pzemOk;
    power["setPercent"] = heaterDiag.powerSetPercent;
    power["setW"] = heaterDiag.targetPowerWatts;
    power["errorW"] = heaterDiag.powerErrorWatts;
    power["backend"] = heaterDiag.triacMode ? "triac" : "ssr";
    power["boosterEnabled"] = heaterDiag.boosterEnabled;
    power["closedLoopActive"] = heaterDiag.closedLoopActive;
    power["zeroCrossSeen"] = heaterDiag.zeroCrossSeen;
    power["zeroCrossCount"] = heaterDiag.zeroCrossCount;
    power["triacFireCount"] = heaterDiag.triacFireCount;
    power["triacDelayUs"] = heaterDiag.triacDelayUs;
    power["energy"] = g_state.power.energy;
    power["frequency"] = g_state.power.frequency;
    power["pf"] = g_state.power.powerFactor;

    // Насос
    JsonObject pump = doc["pump"].to<JsonObject>();
    pump["speedMlH"] = g_state.pump.speedMlPerHour;
    pump["totalMl"] = g_state.pump.totalVolumeMl;
    pump["running"] = g_state.pump.running;

    JsonObject stirrer = doc["stirrer"].to<JsonObject>();
    fillStirrerJson(stirrer, g_state);

    JsonObject valves = doc["valves"].to<JsonObject>();
    valves["water"] = Valves::getWater();
    valves["heads"] = Valves::getHeads();
    valves["uno"] = Valves::getUno();
    valves["startStopDuty"] = Valves::getStartStop();
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
    equipment["boosterHeaterEnabled"] = g_settings.equipment.boosterHeaterEnabled;
    equipment["boosterHeaterPowerW"] = g_settings.equipment.boosterHeaterPowerW;
    equipment["boosterHeaterStopCubeTempC"] =
        g_settings.equipment.boosterHeaterStopCubeTempC;
    equipment["coolingPwmEnabled"] = g_settings.equipment.coolingPwmEnabled;
    equipment["coolingPwmMinDuty"] = g_settings.equipment.coolingPwmMinDuty;
    equipment["coolingPwmMaxDuty"] = g_settings.equipment.coolingPwmMaxDuty;
    equipment["coolingPwmStartupDuty"] = g_settings.equipment.coolingPwmStartupDuty;
    equipment["coolingPwmCurrentDuty"] = Valves::getStartStop();
    equipment["useDs2482ForTemps"] = g_settings.equipment.useDs2482ForTemps;
    equipment["ds2482Address"] = g_settings.equipment.ds2482Address;
    equipment["tempBusGpioPin"] = PIN_ONEWIRE;
    equipment["temperatureBusSource"] = Sensors::getTemperatureBusSourceKey();
    equipment["temperatureBusSourceLabel"] = Sensors::getTemperatureBusSourceLabel();

    JsonObject safetySettings = doc["safetySettings"].to<JsonObject>();
    safetySettings["pressureMaxMmHg"] = g_settings.safety.pressureMaxMmHg;
    safetySettings["tsaMaxC"] = g_settings.safety.tsaMaxC;
    safetySettings["waterOutMaxC"] = g_settings.safety.waterOutMaxC;
    safetySettings["waterOutRiseRateCMin"] = g_settings.safety.waterOutRiseRateCMin;
    safetySettings["pressureRiseRateMmHgMin"] = g_settings.safety.pressureRiseRateMmHgMin;
    doc["min_heater_submerge_l"] = g_settings.equipment.minHeaterSubmergeL;
    doc["water_auto_start_cube_temp_c"] = g_settings.equipment.waterAutoStartCubeTempC;
    doc["booster_heater_enabled"] = g_settings.equipment.boosterHeaterEnabled;
    doc["booster_heater_power_w"] = g_settings.equipment.boosterHeaterPowerW;
    doc["booster_heater_stop_cube_temp_c"] =
        g_settings.equipment.boosterHeaterStopCubeTempC;
    doc["cooling_pwm_enabled"] = g_settings.equipment.coolingPwmEnabled;
    doc["cooling_pwm_min_duty"] = g_settings.equipment.coolingPwmMinDuty;
    doc["cooling_pwm_max_duty"] = g_settings.equipment.coolingPwmMaxDuty;
    doc["cooling_pwm_startup_duty"] = g_settings.equipment.coolingPwmStartupDuty;
    doc["cooling_pwm_current_duty"] = Valves::getStartStop();
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
    uint16_t distPowerWatts = 0;
    FSM::getDistillationParams(distSpeedMlH, distHeadsVolumeMl, distTargetVolumeMl, distEndTempC,
                               distPowerWatts);
    distillation["speedMlH"] = distSpeedMlH;
    distillation["headsVolumeMl"] = distHeadsVolumeMl;
    distillation["targetVolumeMl"] = distTargetVolumeMl;
    distillation["endTempC"] = distEndTempC;
    distillation["powerW"] = distPowerWatts;
    if (g_settings.equipment.heaterPowerW > 0) {
      distillation["powerPercent"] =
          static_cast<uint8_t>((static_cast<uint32_t>(distPowerWatts) * 100U +
                                g_settings.equipment.heaterPowerW / 2U) /
                               g_settings.equipment.heaterPowerW);
    }

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
    display["fullRedraws"] = displayStats.fullRedraws;
    display["partialRedraws"] = displayStats.partialRedraws;
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
    display["lastReason"] = displayStats.lastRedrawReason;
    JsonObject redraw = display["reasons"].to<JsonObject>();
    redraw["screenEnter"] = displayStats.redrawReasonScreenEnter;
    redraw["tapAction"] = displayStats.redrawReasonTapAction;
    redraw["liveDataChanged"] = displayStats.redrawReasonLiveDataChanged;
    redraw["timerKeepalive"] = displayStats.redrawReasonTimerKeepalive;
    redraw["sparklineRefresh"] = displayStats.redrawReasonSparklineRefresh;
    redraw["themeChanged"] = displayStats.redrawReasonThemeChanged;
    redraw["languageChanged"] = displayStats.redrawReasonLanguageChanged;
    redraw["layoutChanged"] = displayStats.redrawReasonLayoutChanged;
    redraw["recovery"] = displayStats.redrawReasonRecovery;

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
#endif

  registerStatusRoutes(server);

  registerChartsRoutes(server);
  registerHealthRoutes(server);
  registerLogsRoutes(server);

  registerHistoryRoutes(server);
  registerProcessRoutes(server);
  registerSafetyRoutes(server);

#if 0
  server.on(
      "/api/process/preflight", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) {
          return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
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

        Mode mode = Mode::IDLE;
        if (!parseRequestedMode(modeStr, mode)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Unknown mode\"}");
          return;
        }

        JsonObject params = doc["params"].as<JsonObject>();
        JsonDocument responseDoc;
        buildProcessPreflight(responseDoc, mode, modeStr, params);

        String response;
        serializeJson(responseDoc, response);
        request->send(200, "application/json", response);
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
        if (parseRequestedMode(modeStr, mode)) {
          JsonDocument startCheckDoc;
          if (!buildProcessPreflight(startCheckDoc, mode, modeStr, params)) {
            String response;
            serializeJson(startCheckDoc, response);
            request->send(409, "application/json", response);
            return;
          }
        } else if (strcmp(modeStr, "rectification") == 0) {
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

        if (g_state.mode != Mode::IDLE) {
          request->send(409, "application/json",
                        "{\"success\":false,\"message\":\"Process already active\"}");
          return;
        }

        // Если уже что-то запущено — сначала остановим
        if (g_state.mode != Mode::IDLE) {
          FSM::stopMode(g_state);
        }

        // Запуск через FSM + разбор params (для некоторых режимов)
        if (mode == Mode::RECTIFICATION || mode == Mode::DISTILLATION ||
            mode == Mode::NBK) {
          applyBoosterStartOverride(params, g_settings);
        }

        if (mode == Mode::DISTILLATION) {
          // params: speed (ml/h), headsVolume (ml), targetVolume (ml), endTemp (°C)
          float speed = params["speed"] | 500.0f;
          float headsVol = params["headsVolume"] | 0.0f;
          float targetVol = params["targetVolume"] | 0.0f;
          float endTemp = params["endTemp"] | 96.0f;
          const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
                                          ? g_settings.equipment.heaterPowerW
                                          : DEFAULT_HEATER_POWER_W;
          uint16_t powerWatts = 0;
          if (!params["powerW"].isNull()) {
            powerWatts =
                clampU16Range(params["powerW"].as<uint32_t>(), 0, heaterMaxW);
          } else {
            uint8_t powerPercent = params["powerPercent"] | 60;
            if (powerPercent > 100) powerPercent = 100;
            powerWatts = static_cast<uint16_t>(
                (static_cast<uint32_t>(heaterMaxW) * powerPercent) / 100U);
          }
          FSM::Distillation::setParams(speed, headsVol, targetVol, endTemp);
          FSM::Distillation::setPowerWatts(powerWatts);
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

  // POST /api/stirrer/start - запуск мешалки
  server.on(
      "/api/stirrer/start", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        if (request->contentLength() != 0) {
          return;
        }

        if (!ensureStirrerReady(request)) {
          return;
        }

        const uint8_t speed = g_settings.stirrer.defaultSpeedPercent;
        g_state.stirrer.autoMode = false;
        Stirrer::start(speed);
        LOG_I("Stirrer started via API at %u%%", speed);
        sendStirrerStateResponse(request, 200, true, "Stirrer started");
      },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        if (total == 0) {
          return;
        }

        if (!ensureStirrerReady(request)) {
          return;
        }

        uint8_t speed = g_settings.stirrer.defaultSpeedPercent;
        if (len > 0) {
          JsonDocument doc;
          if (deserializeJson(doc, data, len)) {
            request->send(400, "application/json",
                          "{\"success\":false,\"error\":\"Invalid JSON\"}");
            return;
          }

          const int requestedSpeed = doc["speed"] | 0;
          if (requestedSpeed < 0 || requestedSpeed > 100) {
            request->send(400, "application/json",
                          "{\"success\":false,\"error\":\"Speed must be between 0 and 100\"}");
            return;
          }

          if (requestedSpeed > 0) {
            speed = static_cast<uint8_t>(requestedSpeed);
          }
        }

        g_state.stirrer.autoMode = false;
        Stirrer::start(speed);
        LOG_I("Stirrer started via API at %u%%", speed);
        sendStirrerStateResponse(request, 200, true, "Stirrer started");
      });

  // POST /api/stirrer/stop - остановка мешалки
  server.on("/api/stirrer/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!ensureStirrerReady(request)) {
      return;
    }
    g_state.stirrer.autoMode = false;
    Stirrer::stop();
    LOG_I("Stirrer stopped via API");
    sendStirrerStateResponse(request, 200, true, "Stirrer stopped");
  });

  // POST /api/stirrer/set - изменить скорость мешалки
  server.on(
      "/api/stirrer/set", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        if (request->contentLength() == 0) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Speed is required\"}");
        }
      },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        if (total == 0) {
          return;
        }

        if (!ensureStirrerReady(request)) {
          return;
        }

        if (!Stirrer::isRunning()) {
          sendStirrerStateResponse(request, 409, false,
                                   "Stirrer is not running");
          return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        if (doc["speed"].isNull()) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Speed is required\"}");
          return;
        }

        const int requestedSpeed = doc["speed"].as<int>();
        if (requestedSpeed < 1 || requestedSpeed > 100) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Speed must be between 1 and 100\"}");
          return;
        }

        g_state.stirrer.autoMode = false;
        Stirrer::setSpeed(static_cast<uint8_t>(requestedSpeed));
        LOG_I("Stirrer speed changed via API to %d%%", requestedSpeed);
        sendStirrerStateResponse(request, 200, true, "Stirrer speed updated");
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
#endif

  // --------------------------------------------------------------------------
  // EQUIPMENT SETTINGS API
  // --------------------------------------------------------------------------

  registerSettingsRoutes(server);

#if 0
  // GET /api/settings/equipment - получить настройки оборудования
  server.on("/api/settings/equipment", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    const char* packingType = "unknown";
    switch (g_settings.equipment.packingType) {
      case PackingType::SPN_3_5:
        packingType = "spn_3_5";
        break;
      case PackingType::SPN_4_0:
        packingType = "spn_4_0";
        break;
      case PackingType::RASCHIG:
        packingType = "raschig";
        break;
      case PackingType::CUSTOM:
        packingType = "custom";
        break;
      default:
        break;
    }
    doc["heaterPowerW"] = g_settings.equipment.heaterPowerW;
    doc["columnHeightMm"] = g_settings.equipment.columnHeightMm;
    doc["cubeVolumeL"] = g_settings.equipment.cubeVolumeL;
    doc["minHeaterSubmergeL"] = g_settings.equipment.minHeaterSubmergeL;
    doc["waterAutoStartCubeTempC"] = g_settings.equipment.waterAutoStartCubeTempC;
    doc["boosterHeaterEnabled"] = g_settings.equipment.boosterHeaterEnabled;
    doc["boosterHeaterPowerW"] = g_settings.equipment.boosterHeaterPowerW;
    doc["boosterHeaterStopCubeTempC"] =
        g_settings.equipment.boosterHeaterStopCubeTempC;
    doc["coolingPwmEnabled"] = g_settings.equipment.coolingPwmEnabled;
    doc["coolingPwmMinDuty"] = g_settings.equipment.coolingPwmMinDuty;
    doc["coolingPwmMaxDuty"] = g_settings.equipment.coolingPwmMaxDuty;
    doc["coolingPwmStartupDuty"] = g_settings.equipment.coolingPwmStartupDuty;
    doc["coolingPwmCurrentDuty"] = Valves::getStartStop();
    doc["useDs2482ForTemps"] = g_settings.equipment.useDs2482ForTemps;
    doc["ds2482Address"] = g_settings.equipment.ds2482Address;
    doc["tempBusGpioPin"] = PIN_ONEWIRE;
    doc["temperatureBusSource"] = Sensors::getTemperatureBusSourceKey();
    doc["temperatureBusSourceLabel"] = Sensors::getTemperatureBusSourceLabel();
    doc["bodyLevelSensorEnabled"] = g_settings.equipment.bodyLevelSensorEnabled;
    doc["bodyLevelThresholdV"] = g_settings.equipment.bodyLevelThresholdV;
    doc["bodyLevelTriggerAbove"] = g_settings.equipment.bodyLevelTriggerAbove;
    doc["leakSensorEnabled"] = g_settings.equipment.leakSensorEnabled;
    doc["leakThresholdV"] = g_settings.equipment.leakThresholdV;
    doc["leakTriggerAbove"] = g_settings.equipment.leakTriggerAbove;
    JsonObject temperatureTopology = doc["temperatureTopology"].to<JsonObject>();
    fillTemperatureTopologyJson(temperatureTopology, g_settings.equipment);
    JsonObject supportedModes = doc["supportedModes"].to<JsonObject>();
    fillTemperatureModeSupportJson(supportedModes, g_settings);
    doc["packingType"] = packingType;
    doc["packingCoeff"] = g_settings.equipment.packingCoeff;
    JsonObject boardProfile = doc["boardProfile"].to<JsonObject>();
    boardProfile["rev"] = BOARD_REV_LABEL;
    boardProfile["name"] = BOARD_PROFILE_NAME;
    boardProfile["code"] = BOARD_REV;
    boardProfile["hasTft"] = BOARD_HAS_TFT;
    boardProfile["hasTouch"] = BOARD_HAS_TOUCH;
    boardProfile["hasTriac"] = BOARD_HAS_TRIAC;
    boardProfile["hasZeroCross"] = BOARD_HAS_ZERO_CROSS;
    boardProfile["hasFractionatorServo"] = BOARD_HAS_FRACTIONATOR_SERVO;
    boardProfile["hasStartStopPwm"] = BOARD_HAS_STARTSTOP_PWM;
    JsonObject pzem = doc["pzem"].to<JsonObject>();
    pzem["available"] = g_state.health.pzemOk;
    pzem["uartNum"] = PZEM_UART_NUM;
    pzem["baudRate"] = PZEM_BAUD_RATE;
    pzem["rxPin"] = PIN_PZEM_RX;
    pzem["txPin"] = PIN_PZEM_TX;
    pzem["voltage"] = g_state.power.voltage;
    pzem["current"] = g_state.power.current;
    pzem["power"] = g_state.power.power;
    pzem["energy"] = g_state.power.energy;
    pzem["frequency"] = g_state.power.frequency;
    pzem["powerFactor"] = g_state.power.powerFactor;
    pzem["lastUpdate"] = g_state.power.lastUpdate;

    JsonObject bootGpio = doc["bootGpio"].to<JsonObject>();
    bootGpio["completed"] = g_bootGpioSelfTest.completed;
    bootGpio["overallOk"] = g_bootGpioSelfTest.overallOk;
    bootGpio["checkedCount"] = g_bootGpioSelfTest.checkedCount;
    bootGpio["timestampMs"] = g_bootGpioSelfTest.timestampMs;
    bootGpio["boardRev"] = g_bootGpioSelfTest.boardRev;

    JsonArray bootItems = bootGpio["items"].to<JsonArray>();
    for (uint8_t i = 0; i < g_bootGpioSelfTest.checkedCount &&
                        i < BOOT_GPIO_CHECK_MAX; ++i) {
      const BootGpioCheckItem& src = g_bootGpioSelfTest.items[i];
      JsonObject item = bootItems.add<JsonObject>();
      item["label"] = src.label;
      item["pin"] = src.pin;
      item["expectedLevel"] = src.expectedLevel;
      item["actualLevel"] = src.actualLevel;
      item["ok"] = src.ok;
      switch (src.mode) {
        case 0:
          item["mode"] = "output_low";
          break;
        case 1:
          item["mode"] = "output_high";
          break;
        case 2:
          item["mode"] = "input";
          break;
        case 3:
          item["mode"] = "input_pullup";
          break;
        default:
          item["mode"] = "unknown";
          break;
      }
    }

    JsonObject modules = doc["modules"].to<JsonObject>();
    fillEquipmentModulesJson(modules);

    JsonObject safetyChannels = doc["safetyChannels"].to<JsonObject>();
    fillSafetyChannelsJson(safetyChannels);

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
        if (!doc["boosterHeaterEnabled"].isNull()) {
          g_settings.equipment.boosterHeaterEnabled =
              doc["boosterHeaterEnabled"].as<bool>();
        }
        if (!doc["boosterHeaterPowerW"].isNull()) {
          g_settings.equipment.boosterHeaterPowerW = clampU16Range(
              doc["boosterHeaterPowerW"].as<uint32_t>(), 1000, 10000);
        }
        if (!doc["boosterHeaterStopCubeTempC"].isNull()) {
          g_settings.equipment.boosterHeaterStopCubeTempC = clampFloatRange(
              doc["boosterHeaterStopCubeTempC"].as<float>(), 20.0f, 100.0f);
        }
        if (!doc["coolingPwmEnabled"].isNull()) {
          g_settings.equipment.coolingPwmEnabled =
              doc["coolingPwmEnabled"].as<bool>();
        }
        if (!doc["coolingPwmMinDuty"].isNull()) {
          g_settings.equipment.coolingPwmMinDuty = clampU8Range(
              doc["coolingPwmMinDuty"].as<uint32_t>(), 0, 255);
        }
        if (!doc["coolingPwmMaxDuty"].isNull()) {
          g_settings.equipment.coolingPwmMaxDuty = clampU8Range(
              doc["coolingPwmMaxDuty"].as<uint32_t>(), 0, 255);
        }
        if (g_settings.equipment.coolingPwmMinDuty >
            g_settings.equipment.coolingPwmMaxDuty) {
          g_settings.equipment.coolingPwmMinDuty =
              g_settings.equipment.coolingPwmMaxDuty;
        }
        if (!doc["coolingPwmStartupDuty"].isNull()) {
          g_settings.equipment.coolingPwmStartupDuty = clampU8Range(
              doc["coolingPwmStartupDuty"].as<uint32_t>(),
              g_settings.equipment.coolingPwmMinDuty,
              g_settings.equipment.coolingPwmMaxDuty);
        } else {
          g_settings.equipment.coolingPwmStartupDuty = clampU8Range(
              g_settings.equipment.coolingPwmStartupDuty,
              g_settings.equipment.coolingPwmMinDuty,
              g_settings.equipment.coolingPwmMaxDuty);
        }
        bool temperatureBusChanged = false;
        if (!doc["useDs2482ForTemps"].isNull()) {
          const bool nextUseDs2482 = doc["useDs2482ForTemps"].as<bool>();
          temperatureBusChanged |=
              g_settings.equipment.useDs2482ForTemps != nextUseDs2482;
          g_settings.equipment.useDs2482ForTemps = nextUseDs2482;
        }
        if (!doc["ds2482Address"].isNull()) {
          const uint8_t requestedAddress = doc["ds2482Address"].as<uint32_t>();
          const uint8_t safeAddress =
              requestedAddress < I2C_ADDR_DS2482_0 ||
                      requestedAddress > I2C_ADDR_DS2482_3
                  ? I2C_ADDR_DS2482_DEFAULT
                  : requestedAddress;
          temperatureBusChanged |=
              g_settings.equipment.ds2482Address != safeAddress;
          g_settings.equipment.ds2482Address = safeAddress;
        }
        if (!doc["bodyLevelSensorEnabled"].isNull()) {
          g_settings.equipment.bodyLevelSensorEnabled =
              doc["bodyLevelSensorEnabled"].as<bool>();
        }
        if (!doc["bodyLevelThresholdV"].isNull()) {
          g_settings.equipment.bodyLevelThresholdV = clampFloatRange(
              doc["bodyLevelThresholdV"].as<float>(), 0.0f, 4.096f);
        }
        if (!doc["bodyLevelTriggerAbove"].isNull()) {
          g_settings.equipment.bodyLevelTriggerAbove =
              doc["bodyLevelTriggerAbove"].as<bool>();
        }
        if (!doc["leakSensorEnabled"].isNull()) {
          g_settings.equipment.leakSensorEnabled =
              doc["leakSensorEnabled"].as<bool>();
        }
        if (!doc["leakThresholdV"].isNull()) {
          g_settings.equipment.leakThresholdV = clampFloatRange(
              doc["leakThresholdV"].as<float>(), 0.0f, 4.096f);
        }
        if (!doc["leakTriggerAbove"].isNull()) {
          g_settings.equipment.leakTriggerAbove =
              doc["leakTriggerAbove"].as<bool>();
        }
        if (doc["temperatureTopology"].is<JsonObject>()) {
          JsonObject topology = doc["temperatureTopology"].as<JsonObject>();
          if (!topology["cube"].isNull()) {
            g_settings.equipment.temperatureTopology.cube =
                topology["cube"].as<bool>();
          }
          if (!topology["columnBottom"].isNull()) {
            g_settings.equipment.temperatureTopology.columnBottom =
                topology["columnBottom"].as<bool>();
          }
          if (!topology["columnTop"].isNull()) {
            g_settings.equipment.temperatureTopology.columnTop =
                topology["columnTop"].as<bool>();
          }
          if (!topology["reflux"].isNull()) {
            g_settings.equipment.temperatureTopology.reflux =
                topology["reflux"].as<bool>();
          }
          if (!topology["tsa"].isNull()) {
            g_settings.equipment.temperatureTopology.tsa =
                topology["tsa"].as<bool>();
          }
          if (!topology["waterIn"].isNull()) {
            g_settings.equipment.temperatureTopology.waterIn =
                topology["waterIn"].as<bool>();
          }
          if (!topology["waterOut"].isNull()) {
            g_settings.equipment.temperatureTopology.waterOut =
                topology["waterOut"].as<bool>();
          }
        }

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        if (temperatureBusChanged) {
          Sensors::refreshTemperatureInventory();
        }

        JsonDocument resp;
        resp["success"] = true;
        resp["useDs2482ForTemps"] = g_settings.equipment.useDs2482ForTemps;
        resp["ds2482Address"] = g_settings.equipment.ds2482Address;
        JsonObject temperatureTopology = resp["temperatureTopology"].to<JsonObject>();
        fillTemperatureTopologyJson(temperatureTopology, g_settings.equipment);
        JsonObject supportedModes = resp["supportedModes"].to<JsonObject>();
        fillTemperatureModeSupportJson(supportedModes, g_settings);
        String json;
        serializeJson(resp, json);
        request->send(200, "application/json", json);
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

  // GET /api/settings/nbk - получить настройки НБК
  server.on("/api/settings/nbk", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["powerW"] = g_settings.nbk.powerW;
    doc["pumpSpeedMlH"] = g_settings.nbk.pumpSpeedMlH;
    doc["columnBottomTempThresholdC"] = g_settings.nbk.columnBottomTempThresholdC;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/settings/nbk - сохранить настройки НБК
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

  // GET /api/settings/fermentation - получить настройки брожения
  server.on("/api/settings/fermentation", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["targetTempC"] = g_settings.fermentation.targetTempC;
    doc["hysteresisC"] = g_settings.fermentation.hysteresisC;
    doc["useHeater"] = g_settings.fermentation.useHeater;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/settings/fermentation - сохранить настройки брожения
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

  // GET /api/settings/stirrer - получить настройки мешалки
  server.on("/api/settings/stirrer", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              syncStirrerState();

              JsonDocument doc;
              fillStirrerSettingsJson(doc.to<JsonObject>(), g_settings);
              doc["available"] = g_state.stirrer.available;
              doc["running"] = g_state.stirrer.running;
              doc["autoMode"] = g_state.stirrer.autoMode;

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  // POST /api/settings/stirrer - сохранить настройки мешалки
  server.on(
      "/api/settings/stirrer", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        if (request->contentLength() == 0) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Request body is required\"}");
        }
      },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        if (total == 0) {
          return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        if (!doc["enabled"].isNull()) {
          g_settings.stirrer.enabled = doc["enabled"].as<bool>();
        }
        if (!doc["defaultSpeedPercent"].isNull()) {
          g_settings.stirrer.defaultSpeedPercent = clampU8Range(
              doc["defaultSpeedPercent"].as<uint32_t>(), 1, 100);
        }
        if (!doc["autoMashing"].isNull()) {
          g_settings.stirrer.autoMashing = doc["autoMashing"].as<bool>();
        }
        if (!doc["autoFermentation"].isNull()) {
          g_settings.stirrer.autoFermentation =
              doc["autoFermentation"].as<bool>();
        }
        if (!doc["autoNbk"].isNull()) {
          g_settings.stirrer.autoNbk = doc["autoNbk"].as<bool>();
        }

        if (!g_settings.stirrer.enabled) {
          g_state.stirrer.autoMode = false;
          Stirrer::stop();
        }

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        syncStirrerState();

        JsonDocument responseDoc;
        responseDoc["success"] = true;
        JsonObject settingsJson = responseDoc["settings"].to<JsonObject>();
        fillStirrerSettingsJson(settingsJson, g_settings);
        JsonObject stirrer = responseDoc["stirrer"].to<JsonObject>();
        fillStirrerJson(stirrer, g_state);

        String json;
        serializeJson(responseDoc, json);
        request->send(200, "application/json", json);
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
    doc["baroCorrectionEnabled"] = params.baroCorrectionEnabled;

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
        if (!params["baroCorrectionEnabled"].isNull()) {
          updated.baroCorrectionEnabled = params["baroCorrectionEnabled"].as<bool>();
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
        out["baroCorrectionEnabled"] = g_settings.rectParams.baroCorrectionEnabled;

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

        const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
                                        ? g_settings.equipment.heaterPowerW
                                        : DEFAULT_HEATER_POWER_W;
        uint16_t powerWatts = 0;
        if (!doc["powerW"].isNull()) {
          powerWatts =
              clampU16Range(doc["powerW"].as<uint32_t>(), 0, heaterMaxW);
        } else {
          int powerPercent = doc["power"] | 0;
          if (powerPercent < 0) powerPercent = 0;
          if (powerPercent > 100) powerPercent = 100;
          powerWatts = static_cast<uint16_t>(
              (static_cast<uint32_t>(heaterMaxW) * powerPercent) / 100U);
        }
        Heater::setPowerWatts(powerWatts);

        const uint8_t powerPercent = heaterMaxW > 0
                                         ? static_cast<uint8_t>(
                                               (static_cast<uint32_t>(powerWatts) *
                                                    100U +
                                                heaterMaxW / 2U) /
                                               heaterMaxW)
                                         : 0;
        char resp[160];
        snprintf(resp, sizeof(resp),
                 "{\"success\":true,\"powerW\":%u,\"powerPercent\":%u}",
                 powerWatts, powerPercent);
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

        const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
                                        ? g_settings.equipment.heaterPowerW
                                        : DEFAULT_HEATER_POWER_W;
        int powerWatts = -2;
        if (!doc["powerW"].isNull()) {
          powerWatts = doc["powerW"].as<int>();
        } else {
          int powerPercent = doc["power"] | -2;
          if (powerPercent >= 0) {
            if (powerPercent > 100) powerPercent = 100;
            powerWatts = static_cast<int>(
                (static_cast<uint32_t>(heaterMaxW) * powerPercent) / 100U);
          } else {
            powerWatts = -1;
          }
        }
        if (powerWatts < -1) powerWatts = -1;
        if (powerWatts > heaterMaxW) powerWatts = heaterMaxW;
        WattControl::setOverrideWatts(static_cast<int16_t>(powerWatts));

        char resp[128];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"powerW\":%d}",
                 powerWatts);
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
        if (!doc["startStopDuty"].isNull()) {
          const uint8_t duty =
              clampU8Range(doc["startStopDuty"].as<uint32_t>(), 0, 255);
          Valves::setStartStop(duty);
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
    const uint32_t totalReboots = g_rebootTracker.totalReboots;
    const uint32_t wdtReboots = g_rebootTracker.wdtReboots;
    const uint32_t crashReboots = g_rebootTracker.crashReboots;
    const uint32_t userReboots = g_rebootTracker.userReboots;
    const uint32_t otherReboots =
        totalReboots > (wdtReboots + crashReboots + userReboots)
            ? totalReboots - (wdtReboots + crashReboots + userReboots)
            : 0;
    const uint8_t lastReason = g_rebootTracker.lastReason;
    const bool lastWasWdt = lastReason == ESP_RST_WDT || lastReason == ESP_RST_TASK_WDT ||
                            lastReason == ESP_RST_INT_WDT;
    const bool lastWasCrash = lastReason == ESP_RST_PANIC;
    const bool lastWasUser = lastReason == ESP_RST_SW || lastReason == ESP_RST_EXT;

    doc["lastReason"] = g_rebootTracker.lastReason;
    doc["lastReasonStr"] = g_rebootTracker.lastReasonStr;
    doc["totalReboots"] = totalReboots;
    doc["wdtReboots"] = wdtReboots;
    doc["crashReboots"] = crashReboots;
    doc["userReboots"] = userReboots;
    doc["otherReboots"] = otherReboots;
    doc["uptimeSec"] = millis() / 1000UL;
    doc["healthOverall"] = g_state.health.overallHealth;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["lastReasonKind"] = lastWasWdt ? "wdt" : lastWasCrash ? "crash" : lastWasUser ? "user" : "other";
    
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
#endif

  registerCalibrationRoutes(server);

  registerPumpRoutes(server);
  registerTestingRoutes(server);
  registerEnergyRoutes(server);

  registerWifiRoutes(server);
  registerOtaRoutes(server);

  // ==========================================================================
  // PROFILES API
  // ==========================================================================

  registerProfilesRoutes(server);

  // 404
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not Found");
  });

  server.begin();
  LOG_I("WebServer: Started on port %d", WEB_SERVER_PORT);
}

void broadcastState(const SystemState &state) {
  WebServerLive::broadcastState(state);
}

void broadcastEvent(const char *event, const char *message) {
  WebServerLive::broadcastEvent(event, message);
}

#if 0
void broadcastState(const SystemState &state) {
  LiveChartHistory::recordState(state, millis());
  ws.cleanupClients();
  if (ws.count() == 0)
    return;

  syncStirrerState();

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
  fillV2StatusJson(fastV2, ControlV2::getLatestModeStatus(),
                   ControlV2::getLatestMetricsSnapshot());

  fastDoc["t_cube"] = state.temps.cube;
  fastDoc["t_column_bottom"] = state.temps.columnBottom;
  fastDoc["t_column_top"] = state.temps.columnTop;
  fastDoc["t_reflux"] = state.temps.reflux;
  fastDoc["t_tsa"] = state.temps.tsa;
  fastDoc["t_water_in"] = state.temps.waterIn;
  fastDoc["t_water_out"] = state.temps.waterOut;
  JsonObject fastTempValid = fastDoc["tempValid"].to<JsonObject>();
  fastTempValid["cube"] = state.temps.valid[TEMP_CUBE];
  fastTempValid["columnBottom"] = state.temps.valid[TEMP_COLUMN_BOTTOM];
  fastTempValid["columnTop"] = state.temps.valid[TEMP_COLUMN_TOP];
  fastTempValid["reflux"] = state.temps.valid[TEMP_REFLUX];
  fastTempValid["tsa"] = state.temps.valid[TEMP_TSA];
  fastTempValid["waterIn"] = state.temps.valid[TEMP_WATER_IN];
  fastTempValid["waterOut"] = state.temps.valid[TEMP_WATER_OUT];

  fastDoc["p_cube"] = state.pressure.cube;
  fastDoc["p_atm"] = state.pressure.atmosphere;

  fastDoc["voltage"] = state.power.voltage;
  fastDoc["current"] = state.power.current;
  fastDoc["power"] = state.power.power;
  fastDoc["energy"] = state.power.energy;
  fastDoc["frequency"] = state.power.frequency;
  fastDoc["pf"] = state.power.powerFactor;
  fastDoc["pzem_ok"] = state.health.pzemOk;

  fastDoc["pump_speed"] = state.pump.speedMlPerHour;
  fastDoc["pump_volume"] = state.pump.totalVolumeMl;
  fastDoc["speed"] = state.pump.speedMlPerHour;
  fastDoc["volume"] = state.pump.totalVolumeMl;
  fastDoc["volume_heads"] = state.stats.headsVolume;
  fastDoc["volume_body"] = state.stats.bodyVolume;
  fastDoc["volume_tails"] = state.stats.tailsVolume;
  JsonObject fastStirrer = fastDoc["stirrer"].to<JsonObject>();
  fillStirrerJson(fastStirrer, g_state);
  JsonObject fastEquipment = fastDoc["equipment"].to<JsonObject>();
  fastEquipment["heaterPowerW"] = g_settings.equipment.heaterPowerW;
  fastEquipment["columnHeightMm"] = g_settings.equipment.columnHeightMm;
  fastEquipment["cubeVolumeL"] = g_settings.equipment.cubeVolumeL;
  fastEquipment["minHeaterSubmergeL"] = g_settings.equipment.minHeaterSubmergeL;
  fastEquipment["waterAutoStartCubeTempC"] = g_settings.equipment.waterAutoStartCubeTempC;
  fastEquipment["boosterHeaterEnabled"] = g_settings.equipment.boosterHeaterEnabled;
  fastEquipment["boosterHeaterPowerW"] = g_settings.equipment.boosterHeaterPowerW;
  fastEquipment["boosterHeaterStopCubeTempC"] =
      g_settings.equipment.boosterHeaterStopCubeTempC;
  fastEquipment["coolingPwmEnabled"] = g_settings.equipment.coolingPwmEnabled;
  fastEquipment["coolingPwmMinDuty"] = g_settings.equipment.coolingPwmMinDuty;
  fastEquipment["coolingPwmMaxDuty"] = g_settings.equipment.coolingPwmMaxDuty;
  fastEquipment["coolingPwmStartupDuty"] = g_settings.equipment.coolingPwmStartupDuty;
  fastEquipment["coolingPwmCurrentDuty"] = Valves::getStartStop();
  fastEquipment["useDs2482ForTemps"] = g_settings.equipment.useDs2482ForTemps;
  fastEquipment["ds2482Address"] = g_settings.equipment.ds2482Address;
  fastEquipment["tempBusGpioPin"] = PIN_ONEWIRE;
  fastEquipment["temperatureBusSource"] = Sensors::getTemperatureBusSourceKey();
  fastEquipment["temperatureBusSourceLabel"] = Sensors::getTemperatureBusSourceLabel();
  JsonObject fastSafetySettings = fastDoc["safetySettings"].to<JsonObject>();
  fastSafetySettings["pressureMaxMmHg"] = g_settings.safety.pressureMaxMmHg;
  fastSafetySettings["tsaMaxC"] = g_settings.safety.tsaMaxC;
  fastSafetySettings["waterOutMaxC"] = g_settings.safety.waterOutMaxC;
  fastSafetySettings["waterOutRiseRateCMin"] = g_settings.safety.waterOutRiseRateCMin;
  fastSafetySettings["pressureRiseRateMmHgMin"] = g_settings.safety.pressureRiseRateMmHgMin;
  fastDoc["min_heater_submerge_l"] = g_settings.equipment.minHeaterSubmergeL;
  fastDoc["water_auto_start_cube_temp_c"] = g_settings.equipment.waterAutoStartCubeTempC;
  fastDoc["booster_heater_enabled"] = g_settings.equipment.boosterHeaterEnabled;
  fastDoc["booster_heater_power_w"] = g_settings.equipment.boosterHeaterPowerW;
  fastDoc["booster_heater_stop_cube_temp_c"] =
      g_settings.equipment.boosterHeaterStopCubeTempC;
  fastDoc["cooling_pwm_enabled"] = g_settings.equipment.coolingPwmEnabled;
  fastDoc["cooling_pwm_min_duty"] = g_settings.equipment.coolingPwmMinDuty;
  fastDoc["cooling_pwm_max_duty"] = g_settings.equipment.coolingPwmMaxDuty;
  fastDoc["cooling_pwm_startup_duty"] = g_settings.equipment.coolingPwmStartupDuty;
  fastDoc["cooling_pwm_current_duty"] = Valves::getStartStop();
  fastDoc["safety_pressure_max_mmhg"] = g_settings.safety.pressureMaxMmHg;
  fastDoc["safety_tsa_max_c"] = g_settings.safety.tsaMaxC;
  fastDoc["safety_water_out_max_c"] = g_settings.safety.waterOutMaxC;
  fastDoc["safety_water_out_rise_rate_c_min"] = g_settings.safety.waterOutRiseRateCMin;
  fastDoc["safety_pressure_rise_rate_mmhg_min"] = g_settings.safety.pressureRiseRateMmHgMin;
  JsonObject fastValves = fastDoc["valves"].to<JsonObject>();
  fastValves["water"] = Valves::getWater();
  fastValves["heads"] = Valves::getHeads();
  fastValves["uno"] = Valves::getUno();
  fastValves["startStopDuty"] = Valves::getStartStop();
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
  fastDoc["display_full"] = displayStats.fullRedraws;
  fastDoc["display_partial"] = displayStats.partialRedraws;
  fastDoc["display_last_reason"] = displayStats.lastRedrawReason;
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
  JsonObject tempValid = doc["tempValid"].to<JsonObject>();
  tempValid["cube"] = state.temps.valid[TEMP_CUBE];
  tempValid["columnBottom"] = state.temps.valid[TEMP_COLUMN_BOTTOM];
  tempValid["columnTop"] = state.temps.valid[TEMP_COLUMN_TOP];
  tempValid["reflux"] = state.temps.valid[TEMP_REFLUX];
  tempValid["tsa"] = state.temps.valid[TEMP_TSA];
  tempValid["waterIn"] = state.temps.valid[TEMP_WATER_IN];
  tempValid["waterOut"] = state.temps.valid[TEMP_WATER_OUT];

  doc["p_cube"] = state.pressure.cube;
  doc["p_atm"] = state.pressure.atmosphere;

  doc["voltage"] = state.power.voltage;
  doc["current"] = state.power.current;
  doc["power"] = state.power.power;
  doc["energy"] = state.power.energy;
  doc["frequency"] = state.power.frequency;
  doc["pf"] = state.power.powerFactor;
  doc["pzem_ok"] = state.health.pzemOk;

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
  valves["startStopDuty"] = Valves::getStartStop();
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
  equipment["waterAutoStartCubeTempC"] =
      g_settings.equipment.waterAutoStartCubeTempC;
  equipment["boosterHeaterEnabled"] = g_settings.equipment.boosterHeaterEnabled;
  equipment["boosterHeaterPowerW"] = g_settings.equipment.boosterHeaterPowerW;
  equipment["boosterHeaterStopCubeTempC"] =
      g_settings.equipment.boosterHeaterStopCubeTempC;
  equipment["coolingPwmEnabled"] = g_settings.equipment.coolingPwmEnabled;
  equipment["coolingPwmMinDuty"] = g_settings.equipment.coolingPwmMinDuty;
  equipment["coolingPwmMaxDuty"] = g_settings.equipment.coolingPwmMaxDuty;
  equipment["coolingPwmStartupDuty"] =
      g_settings.equipment.coolingPwmStartupDuty;
  equipment["coolingPwmCurrentDuty"] = Valves::getStartStop();
  equipment["useDs2482ForTemps"] = g_settings.equipment.useDs2482ForTemps;
  equipment["ds2482Address"] = g_settings.equipment.ds2482Address;
  equipment["tempBusGpioPin"] = PIN_ONEWIRE;
  equipment["temperatureBusSource"] = Sensors::getTemperatureBusSourceKey();
  equipment["temperatureBusSourceLabel"] = Sensors::getTemperatureBusSourceLabel();
  JsonObject safetySettings = doc["safetySettings"].to<JsonObject>();
  safetySettings["pressureMaxMmHg"] = g_settings.safety.pressureMaxMmHg;
  safetySettings["tsaMaxC"] = g_settings.safety.tsaMaxC;
  safetySettings["waterOutMaxC"] = g_settings.safety.waterOutMaxC;
  safetySettings["waterOutRiseRateCMin"] = g_settings.safety.waterOutRiseRateCMin;
  safetySettings["pressureRiseRateMmHgMin"] =
      g_settings.safety.pressureRiseRateMmHgMin;
  doc["min_heater_submerge_l"] = g_settings.equipment.minHeaterSubmergeL;
  doc["water_auto_start_cube_temp_c"] =
      g_settings.equipment.waterAutoStartCubeTempC;
  doc["booster_heater_enabled"] = g_settings.equipment.boosterHeaterEnabled;
  doc["booster_heater_power_w"] = g_settings.equipment.boosterHeaterPowerW;
  doc["booster_heater_stop_cube_temp_c"] =
      g_settings.equipment.boosterHeaterStopCubeTempC;
  doc["cooling_pwm_enabled"] = g_settings.equipment.coolingPwmEnabled;
  doc["cooling_pwm_min_duty"] = g_settings.equipment.coolingPwmMinDuty;
  doc["cooling_pwm_max_duty"] = g_settings.equipment.coolingPwmMaxDuty;
  doc["cooling_pwm_startup_duty"] =
      g_settings.equipment.coolingPwmStartupDuty;
  doc["cooling_pwm_current_duty"] = Valves::getStartStop();
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
  uint16_t distPowerWatts = 0;
  FSM::getDistillationParams(distSpeedMlH, distHeadsVolumeMl, distTargetVolumeMl, distEndTempC,
                             distPowerWatts);
  distillation["speedMlH"] = distSpeedMlH;
  distillation["headsVolumeMl"] = distHeadsVolumeMl;
  distillation["targetVolumeMl"] = distTargetVolumeMl;
  distillation["endTempC"] = distEndTempC;
  distillation["powerW"] = distPowerWatts;
  if (g_settings.equipment.heaterPowerW > 0) {
    distillation["powerPercent"] =
        static_cast<uint8_t>((static_cast<uint32_t>(distPowerWatts) * 100U +
                              g_settings.equipment.heaterPowerW / 2U) /
                             g_settings.equipment.heaterPowerW);
  }

  JsonObject display = doc["display"].to<JsonObject>();
  display["frames"] = displayStats.framesRendered;
  display["fullRedraws"] = displayStats.fullRedraws;
  display["partialRedraws"] = displayStats.partialRedraws;
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
  display["lastReason"] = displayStats.lastRedrawReason;
  JsonObject redraw = display["reasons"].to<JsonObject>();
  redraw["screenEnter"] = displayStats.redrawReasonScreenEnter;
  redraw["tapAction"] = displayStats.redrawReasonTapAction;
  redraw["liveDataChanged"] = displayStats.redrawReasonLiveDataChanged;
  redraw["timerKeepalive"] = displayStats.redrawReasonTimerKeepalive;
  redraw["sparklineRefresh"] = displayStats.redrawReasonSparklineRefresh;
  redraw["themeChanged"] = displayStats.redrawReasonThemeChanged;
  redraw["languageChanged"] = displayStats.redrawReasonLanguageChanged;
  redraw["layoutChanged"] = displayStats.redrawReasonLayoutChanged;
  redraw["recovery"] = displayStats.redrawReasonRecovery;

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
  fillStirrerJson(stirrer, g_state);

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
#endif

} // namespace WebServer
