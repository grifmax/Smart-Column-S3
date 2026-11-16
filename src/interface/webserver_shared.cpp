/**
 * Smart-Column S3 - Веб-сервер
 *
 * HTTP server + WebSocket для Web UI
 */

#include "webserver_shared.h"

#include "config.h"
#include "fs_compat.h"
#include <WiFi.h>

#include "control/fsm.h"
#include "control/fsm_utils.h"
#include "control/autoclave_hardware_profile.h"
#include "control/cooling_pwm_logic.h"
#include "control/power_command_gateway.h"
#include "control/rect_takeoff.h"
#include "control/v2/reason_codes.h"
#include "control/v2/safety_policy.h"
#include "drivers/sensors.h"
#include "drivers/stirrer.h"
#include "drivers/valves.h"
#include "interface/editable_field_descriptors.h"
#include "interface/security.h"
#include "interface/wifi_profiles.h"
#include "profiles.h"
#include "storage/logger.h"

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
  case Mode::AUTOCLAVE:
    return "autoclave";
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
  case NbkPhase::FEED_RAMP: return "feed_ramp";
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
  case FermentationPhase::PREPARE: return "prepare";
  case FermentationPhase::HEATING: return "heating";
  case FermentationPhase::COOLING: return "cooling";
  case FermentationPhase::FERMENTATION: return "fermentation";
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

void sendStirrerStateResponse(AsyncWebServerRequest *request, int statusCode,
                              bool success, const char *message,
                              const SystemState &state,
                              const PowerCommandGateway::Command *command) {
  JsonDocument doc;
  doc["success"] = command != nullptr ? command->accepted && command->applied
                                      : success;
  doc["message"] = message;
  if (command != nullptr) {
    doc["commandId"] = command->sequence;
    doc["commandType"] = "output";
    doc["accepted"] = command->accepted;
    doc["queued"] = command->queued;
    doc["applied"] = command->applied;
    doc["status"] = !command->accepted ? "rejected"
                     : command->applied ? "applied" : "queued";
    doc["rejectReason"] =
        PowerCommandGateway::rejectReasonString(command->rejectReason);
    doc["rejectReasonCode"] = static_cast<uint8_t>(command->rejectReason);
  }
  JsonObject stirrer = doc["stirrer"].to<JsonObject>();
  fillStirrerJson(stirrer, state);

  String json;
  serializeJson(doc, json);
  request->send(statusCode, "application/json", json);
}

void sendJsonSuccess(AsyncWebServerRequest *request, int statusCode,
                     const char *message) {
  JsonDocument doc;
  doc["success"] = true;
  if (message && message[0] != '\0') {
    doc["message"] = message;
  }

  String json;
  serializeJson(doc, json);
  request->send(statusCode, "application/json", json);
}

void sendJsonError(AsyncWebServerRequest *request, int statusCode,
                   const char *error, bool includeSuccess) {
  JsonDocument doc;
  if (includeSuccess) {
    doc["success"] = false;
  }
  doc["error"] = error ? error : "Unknown error";

  String json;
  serializeJson(doc, json);
  request->send(statusCode, "application/json", json);
}

bool ensureStirrerReady(AsyncWebServerRequest *request,
                        const SystemState &state) {
  if (state.mode != Mode::IDLE) {
    char reason[128];
    snprintf(reason, sizeof(reason),
             state.paused
                 ? "Manual stirrer control is unavailable while %s is paused"
                 : "Manual stirrer control is unavailable while %s is active",
             getModeString(state.mode));
    sendStirrerStateResponse(request, 409, false, reason, state);
    return false;
  }

  if (!g_settings.stirrer.enabled) {
    sendStirrerStateResponse(request, 409, false,
                             "Stirrer is disabled in settings", state);
    return false;
  }

  if (!Stirrer::isAvailable()) {
    sendStirrerStateResponse(request, 503, false,
                             "Stirrer DAC is not available", state);
    return false;
  }

  if (!state.safetyOk || Safety::isLatched(state)) {
    sendStirrerStateResponse(request, 409, false,
                             "Safety lockout is active", state);
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

bool handleSecurityGate(AsyncWebServerRequest* request) {
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

bool collectRequestBody(AsyncWebServerRequest *request, const uint8_t *data,
                        size_t len, size_t index, size_t total, String &body) {
  if (index == 0) {
    delete static_cast<String *>(request->_tempObject);
    String *buffer = new String();
    buffer->reserve(total);
    request->_tempObject = buffer;
  }

  String *buffer = static_cast<String *>(request->_tempObject);
  if (buffer == nullptr) {
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    *buffer += static_cast<char>(data[i]);
  }

  if (index + len != total) {
    return false;
  }

  body = *buffer;
  delete buffer;
  request->_tempObject = nullptr;
  return true;
}

bool deserializeRequestJsonBody(AsyncWebServerRequest *request,
                                const uint8_t *data, size_t len, size_t index,
                                size_t total, JsonDocument &doc,
                                const char *invalidJsonPayload,
                                bool allowEmpty,
                                const char *emptyBodyPayload) {
  String body;
  if (!collectRequestBody(request, data, len, index, total, body)) {
    return false;
  }

  if (body.isEmpty()) {
    if (allowEmpty) {
      doc.clear();
      return true;
    }

    request->send(400, "application/json",
                  emptyBodyPayload ? emptyBodyPayload : invalidJsonPayload);
    return false;
  }

  if (deserializeJson(doc, body)) {
    request->send(400, "application/json", invalidJsonPayload);
    return false;
  }

  return true;
}

uint16_t clampU16Range(uint32_t value, uint16_t minValue,
                       uint16_t maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return static_cast<uint16_t>(value);
}

RectTakeoffBackendType clampRectTakeoffBackendType(uint32_t rawValue) {
  return static_cast<RectTakeoffBackendType>(clampU16Range(
      rawValue, 0,
      static_cast<uint16_t>(RectTakeoffBackendType::VALVE_SINGLE_SWITCHED)));
}

FractionProgram parseFractionProgramJson(JsonObject json,
                                         const FractionProgram &fallback) {
  FractionProgram program = fallback;
  if (json.isNull()) {
    return program;
  }

  program.stepCount = 0;
  program.schemaVersion = clampU8Range(json["schemaVersion"] | fallback.schemaVersion,
                                       0, 255);
  program.enabled = json["enabled"] | fallback.enabled;
  program.heatingTemperatureSensorIndex = clampU8Range(
      json["heatingTemperatureSensorIndex"] |
          fallback.heatingTemperatureSensorIndex,
      0, TEMP_COUNT - 1);
  program.heatingTargetTemperatureC = clampFloatRange(
      json["heatingTargetTemperatureC"] | fallback.heatingTargetTemperatureC,
      0.0f, 110.0f);

  JsonArray steps = json["steps"].as<JsonArray>();
  if (steps.isNull()) {
    return program;
  }

  for (JsonObject stepObj : steps) {
    if (program.stepCount >= FRACTION_PROGRAM_MAX_STEPS) {
      break;
    }
    FractionProgramStep &step = program.steps[program.stepCount++];
    memset(&step, 0, sizeof(step));
    strlcpy(step.name, stepObj["name"] | "", sizeof(step.name));
    step.routeIndex = clampU8Range(stepObj["routeIndex"] | 0, 0, 4);
    step.pumpRateMlH =
        clampFloatRange(stepObj["pumpRateMlH"] | 0.0f, 0.0f, 65000.0f);
    step.heaterPowerW = clampFloatRange(stepObj["heaterPowerW"] | 0.0f, 0.0f,
                                        10000.0f);
    step.requireOperatorConfirmation =
        stepObj["requireOperatorConfirmation"] | false;
    strlcpy(step.confirmationPrompt, stepObj["confirmationPrompt"] | "",
            sizeof(step.confirmationPrompt));
    step.endConditions =
        clampU8Range(stepObj["endConditions"] | 0, 0, 255);
    step.endVolumeMl =
        clampFloatRange(stepObj["endVolumeMl"] | 0.0f, 0.0f, 50000.0f);
    step.endDurationSec =
        stepObj["endDurationSec"].isNull()
            ? 0
            : static_cast<uint32_t>(min<uint64_t>(
                  stepObj["endDurationSec"].as<uint64_t>(), 864000ULL));
    step.temperatureSensorIndex = clampU8Range(
        stepObj["temperatureSensorIndex"] | TEMP_CUBE, 0, TEMP_COUNT - 1);
    step.endTemperatureC =
        clampFloatRange(stepObj["endTemperatureC"] | 0.0f, 0.0f, 110.0f);
    step.allowManualAdvance = stepObj["allowManualAdvance"] | false;
  }

  return program;
}

void fillFractionProgramJson(JsonObject json, const FractionProgram &program) {
  json["schemaVersion"] = program.schemaVersion;
  json["enabled"] = program.enabled;
  json["stepCount"] = program.stepCount;
  json["heatingTemperatureSensorIndex"] =
      program.heatingTemperatureSensorIndex;
  json["heatingTargetTemperatureC"] = program.heatingTargetTemperatureC;

  JsonArray steps = json["steps"].to<JsonArray>();
  for (uint8_t i = 0; i < program.stepCount && i < FRACTION_PROGRAM_MAX_STEPS;
       ++i) {
    const FractionProgramStep &step = program.steps[i];
    JsonObject stepJson = steps.add<JsonObject>();
    stepJson["name"] = step.name;
    stepJson["routeIndex"] = step.routeIndex;
    stepJson["pumpRateMlH"] = step.pumpRateMlH;
    stepJson["heaterPowerW"] = step.heaterPowerW;
    stepJson["requireOperatorConfirmation"] =
        step.requireOperatorConfirmation;
    stepJson["confirmationPrompt"] = step.confirmationPrompt;
    stepJson["endConditions"] = step.endConditions;
    stepJson["endVolumeMl"] = step.endVolumeMl;
    stepJson["endDurationSec"] = step.endDurationSec;
    stepJson["temperatureSensorIndex"] = step.temperatureSensorIndex;
    stepJson["endTemperatureC"] = step.endTemperatureC;
    stepJson["allowManualAdvance"] = step.allowManualAdvance;
  }
}

void fillFractionProgramRuntimeJson(
    JsonObject json, const SystemState &state, const Settings &settings,
    const RectTakeoffFeedback &takeoffFeedback) {
  json["enabled"] = settings.fractionProgram.enabled;
  json["active"] = state.fractionProgram.active;
  json["currentStep"] = state.fractionProgram.currentStep;
  json["waitingForConfirmation"] = state.fractionProgram.waitingForConfirmation;
  json["routing"] = state.fractionProgram.routing;
  json["lastEndReason"] = state.fractionProgram.lastEndReason;
  json["requestedRoute"] = static_cast<uint8_t>(takeoffFeedback.requestedFraction);
  json["routedRoute"] = static_cast<uint8_t>(takeoffFeedback.routedFraction);
  json["actualRateMlH"] = takeoffFeedback.actualEquivalentRateMlH;

  float collectedMl = settings.distillationUi.takeoffBackendType ==
                              RectTakeoffBackendType::PUMP
                          ? state.pump.totalVolumeMl -
                                state.fractionProgram.stepStartVolumeMl
                          : takeoffFeedback.sessionVolumeMl -
                                state.fractionProgram.stepStartVolumeMl;
  if (collectedMl < 0.0f) {
    collectedMl = 0.0f;
  }
  json["collectedMl"] = collectedMl;
  json["confirmationPrompt"] = state.fractionProgram.confirmationPrompt;

  if (state.fractionProgram.currentStep < settings.fractionProgram.stepCount) {
    const FractionProgramStep &step =
        settings.fractionProgram.steps[state.fractionProgram.currentStep];
    json["stepName"] = step.name;
    json["targetRoute"] = step.routeIndex;
    json["targetRateMlH"] = step.pumpRateMlH;
    json["allowManualAdvance"] = step.allowManualAdvance;
    json["endConditions"] = step.endConditions;
    json["endVolumeMl"] = step.endVolumeMl;
    json["endDurationSec"] = step.endDurationSec;
    json["temperatureSensorIndex"] = step.temperatureSensorIndex;
    json["endTemperatureC"] = step.endTemperatureC;
  } else {
    json["stepName"] = "";
    json["targetRoute"] = 0;
    json["targetRateMlH"] = 0.0f;
    json["allowManualAdvance"] = false;
    json["endConditions"] = FRACTION_PROGRAM_END_NONE;
    json["endVolumeMl"] = 0.0f;
    json["endDurationSec"] = 0;
    json["temperatureSensorIndex"] = 0;
    json["endTemperatureC"] = 0.0f;
  }
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
  } else if (strcmp(modeStr, "autoclave") == 0) {
    mode = Mode::AUTOCLAVE;
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

static bool isPressureStartupBlockingForMode(Mode mode,
                                             const Settings &settings) {
  return Safety::getRequiredSensorsForMode(mode, settings).pressure;
}


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

  const float heaterPowerKw = FSM::getConfiguredHeaterPowerKw(g_settings);
  float rectDuty = 1.0f;
  if (rect.refluxMode == RectRefluxMode::SR_RATIO) {
    rectDuty = rect.srRatio <= 0.0f ? 0.0f : (1.0f / (rect.srRatio + 1.0f));
  } else if (rect.refluxMode == RectRefluxMode::AUTONOMOUS) {
    const float cycle = rect.autonomousCycleSec > 0
        ? static_cast<float>(rect.autonomousCycleSec)
        : 1.0f;
    const float pause = rect.autonomousPauseSec < rect.autonomousCycleSec
        ? static_cast<float>(rect.autonomousPauseSec)
        : (cycle - 1.0f);
    rectDuty = fmaxf(0.0f, (cycle - pause) / cycle);
  }
  forecast.headsSpeedMlH = FSM::getRectificationTakeoffRateMlH(
      g_settings, clampFloatRange(rect.headsSpeedMlHKw, 10.0f, 2000.0f)) * rectDuty;
  forecast.bodySpeedMlH = FSM::getRectificationTakeoffRateMlH(
      g_settings, clampFloatRange(rect.bodySpeedMlHKw, 50.0f, 3000.0f)) * rectDuty;
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
    const float baselineBodySpeedMlH = FSM::getRectificationTakeoffRateMlH(
        activeProfile->validation.heaterPowerW,
        activeProfile->validation.columnHeightMm,
        activeProfile->validation.packingCoeff,
        baselineBodySpeedMlHKw);
    const float currentBodySpeedMlH = FSM::getRectificationTakeoffRateMlH(
        g_settings.equipment.heaterPowerW,
        g_settings.equipment.columnHeightMm,
        g_settings.equipment.packingCoeff,
        currentBodySpeedMlHKw);
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
              safeRatio(baselineBodySpeedMlH, currentBodySpeedMlH, 1.0f),
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
  } else if (rect.refluxMode == RectRefluxMode::ML_H &&
             rect.bodySpeedMlHKw <= rect.headsSpeedMlHKw) {
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
                           JsonObject params, const SystemState &state) {
  // Keep the legacy local name so the builder body stays readable while
  // ensuring this read-only path never aliases the live global state.
  const SystemState &g_state = state;

  const auto &status = ControlV2::getLatestModeStatus();
  const auto &metrics = ControlV2::getLatestMetricsSnapshot();
  const bool lifecycleIdle = status.lifecycle == ControlV2::ModeLifecycleV2::IDLE;
  const bool v2SnapshotReady = metrics.timestampMs > 0;
  const Safety::RequiredSensorsMask requiredSensors =
      Safety::getRequiredSensorsForMode(mode, g_settings);
  const bool pressureStartupBlocking =
      isPressureStartupBlockingForMode(mode, g_settings);
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
  const bool autoclaveHardwareReady =
      mode != Mode::AUTOCLAVE || AutoclaveHardwareProfile::isSteamAvailable();
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
  const String requestedProfileId = params["profileId"] | "";
  const String activeProfileId = requestedProfileId.isEmpty()
                                     ? getActiveProfileId()
                                     : requestedProfileId;
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
  static constexpr uint8_t PREFLIGHT_SUMMARY_COUNT = 5;
  static constexpr uint8_t SUMMARY_SEEN = 1U << 0;
  static constexpr uint8_t SUMMARY_WARNING = 1U << 1;
  static constexpr uint8_t SUMMARY_BLOCKING = 1U << 2;
  uint8_t summaryFlags[PREFLIGHT_SUMMARY_COUNT] = {};
  char summaryDetails[PREFLIGHT_SUMMARY_COUNT][144] = {{0}};
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

  const char *autonomyLevel = autonomyLevelToString(g_settings.autonomyLevel);
  setProcessPreflightCheck(
      checks, "autonomy", autonomyLevel,
      g_settings.autonomyLevel == AutonomyLevel::MANUAL ? "warn" : "good");

  setProcessPreflightCheck(
      checks, "profile",
      mode == Mode::AUTOCLAVE
          ? (autoclaveHardwareReady ? "OK" : "Не подтверждён")
          : recipeProfileRelevant
          ? (profileCategoryMatches ? "OK"
                                    : (activeProfileLoaded ? "Mismatch"
                                                           : "Не указан"))
          : "Опц.",
      mode == Mode::AUTOCLAVE
          ? (autoclaveHardwareReady ? "good" : "danger")
          : (recipeProfileRelevant
                 ? (profileCategoryMatches ? "good" : "warn")
                 : "muted"));
  setProcessPreflightCheck(checks, "water",
                           coolingRelevant
                               ? (waterTelemetryReady ? "OK"
                                                      : "Проверьте")
                               : "Не нужно",
                           coolingRelevant
                               ? (waterTelemetryReady ? "good" : "warn")
                               : "muted");

  setProcessPreflightCheck(checks, "takeoff",
                           (mode == Mode::RECTIFICATION ||
                            mode == Mode::MANUAL_RECT)
                               ? "РџСЂРѕРІРµСЂСЊС‚Рµ"
                               : "РќРµ РЅСѓР¶РЅРѕ",
                           (mode == Mode::RECTIFICATION ||
                            mode == Mode::MANUAL_RECT)
                               ? "warn"
                               : "muted");

  auto addItem = [&](const char *id, const char *tone, const char *title,
                     const String &detail, bool blocking) {
    appendProcessPreflightItem(items, id, tone, title, detail, blocking,
                               blockingCount, warningCount);
    if (tone != nullptr && strcmp(tone, "muted") != 0) {
      uint8_t group = 3;
      if (strcmp(id, "process-active") == 0 || strcmp(id, "v2") == 0 ||
          strcmp(id, "logging") == 0) {
        group = 0;
      } else if (strcmp(id, "sensors") == 0 || strcmp(id, "pressure") == 0) {
        group = 1;
      } else if (strcmp(id, "safety") == 0 || strcmp(id, "alarm") == 0) {
        group = 2;
      } else if (strcmp(id, "water") == 0 || strcmp(id, "cooling") == 0 ||
                 strcmp(id, "valves") == 0 || strcmp(id, "pump") == 0 ||
                 strcmp(id, "stirrer") == 0 || strcmp(id, "takeoff") == 0 ||
                 strcmp(id, "rect-takeoff") == 0 ||
                 strcmp(id, "manual-takeoff") == 0 ||
                 strcmp(id, "dist-backend") == 0 ||
                 strcmp(id, "dist-safe-vent") == 0 ||
                 strcmp(id, "pressure-control") == 0) {
        group = 4;
      }

      uint8_t &flags = summaryFlags[group];
      const bool first = (flags & SUMMARY_SEEN) == 0;
      const bool firstBlocking = blocking && (flags & SUMMARY_BLOCKING) == 0;
      const bool firstWarning = !blocking && strcmp(tone, "warn") == 0 &&
                                (flags & (SUMMARY_BLOCKING | SUMMARY_WARNING)) == 0;
      if ((first || firstBlocking || firstWarning) && !detail.isEmpty()) {
        strncpy(summaryDetails[group], detail.c_str(),
                sizeof(summaryDetails[group]) - 1);
        summaryDetails[group][sizeof(summaryDetails[group]) - 1] = '\0';
      }
      flags |= SUMMARY_SEEN;
      if (blocking) {
        flags |= SUMMARY_BLOCKING;
      } else if (strcmp(tone, "warn") == 0) {
        flags |= SUMMARY_WARNING;
      }
    }
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

  switch (g_settings.autonomyLevel) {
    case AutonomyLevel::MANUAL:
      addItem("autonomy", "warn", "Autonomy: manual",
              "Adaptive corrections are disabled; the base process FSM and all safety cut-offs remain active.",
              false);
      break;
    case AutonomyLevel::GUIDED:
      addItem("autonomy", "good", "Autonomy: guided",
              "Automation is available with operator confirmations; safety and preflight remain mandatory.",
              false);
      break;
    case AutonomyLevel::ADAPTIVE:
      addItem("autonomy", "good", "Autonomy: adaptive",
              "Adaptive corrections are enabled when telemetry and decision trust pass their safety gates.",
              false);
      break;
    case AutonomyLevel::FULL_AUTO:
    default:
      addItem("autonomy", "good", "Autonomy: full-auto",
              "Maximum adaptive autonomy is enabled; safety, preflight and actuator guards remain mandatory.",
              false);
      break;
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

  const bool takeoffValvesOpen = Valves::isAnyTakeoffValveOpen();
  if (takeoffValvesOpen &&
      (mode == Mode::RECTIFICATION || mode == Mode::MANUAL_RECT)) {
    setProcessPreflightCheck(checks, "takeoff", "Р‘Р»РѕРє", "danger");
  }
  if (takeoffValvesOpen) {
    addItem("valves", "danger", "Клапаны отбора",
            "Перед стартом уже открыта одна из линий отбора. Сначала закройте "
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
      if (!params["takeoffBackendType"].isNull()) {
        rect.takeoffBackendType = static_cast<RectTakeoffBackendType>(clampU16Range(
            params["takeoffBackendType"].as<uint32_t>(), 0,
            static_cast<uint32_t>(RectTakeoffBackendType::VALVE_SINGLE_SWITCHED)));
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
      if (!params["refluxMode"].isNull()) {
        rect.refluxMode = static_cast<RectRefluxMode>(clampU16Range(
            params["refluxMode"].as<uint32_t>(), 0,
            static_cast<uint32_t>(RectRefluxMode::AUTONOMOUS)));
      }
      if (!params["srRatio"].isNull()) {
        rect.srRatio =
            clampFloatRange(params["srRatio"].as<float>(), 0.0f, 20.0f);
      }
      if (!params["autonomousCycleSec"].isNull()) {
        rect.autonomousCycleSec = clampU16Range(
            params["autonomousCycleSec"].as<uint32_t>(), 1, 7200);
      }
      if (!params["autonomousPauseSec"].isNull()) {
        rect.autonomousPauseSec = clampU16Range(
            params["autonomousPauseSec"].as<uint32_t>(), 0, 7199);
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
    } else if (fractionsSum > 100.0f ||
               (rect.refluxMode == RectRefluxMode::ML_H &&
                rect.bodySpeedMlHKw <= rect.headsSpeedMlHKw)) {
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

    String takeoffBackendDetail;
    if (!RectTakeoff::validateBackendConfiguration(rect.takeoffBackendType,
                                                   &takeoffBackendDetail)) {
      addItem("rect-takeoff", "danger", "Исполнитель отбора",
              takeoffBackendDetail, true);
      setProcessPreflightCheck(checks, "takeoff", "Р‘Р»РѕРє", "danger");
      setProcessPreflightCheck(checks, "takeoff", "Р‘Р»РѕРє", "danger");
    } else {
      addItem("rect-takeoff", "good", "Исполнитель отбора",
              takeoffBackendDetail, false);
      if (!takeoffValvesOpen) {
        setProcessPreflightCheck(checks, "takeoff", "OK", "good");
      }
      if (!takeoffValvesOpen) {
        setProcessPreflightCheck(checks, "takeoff", "OK", "good");
      }
    }

    if (rect.pressureControlEnabled) {
      if (!ControlV2::SafetyPolicyV2::isPressureCalibrationValid(
              g_settings.pressureCal)) {
        addItem("pressure-control", "warn", "Контур мощности по давлению",
                "Калибровка давления не подтверждена (нужно минимум две монотонные точки). Контур запустится в безопасном fallback без автоматической коррекции.",
                false);
      } else if (!g_state.pressure.ok) {
        addItem("pressure-control", "warn", "Контур мощности по давлению",
                "Сигнал давления сейчас недоступен. До восстановления свежего устойчивого сигнала будет использована фиксированная мощность профиля.",
                false);
      } else {
        addItem("pressure-control", "good", "Контур мощности по давлению",
                "Контур включён; мощность плавно ограничивается выше рабочего коридора и отключается на safety-пороге.",
                false);
      }
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

    String takeoffBackendDetail;
    if (!RectTakeoff::validateBackendConfiguration(
            g_settings.rectParams.takeoffBackendType, &takeoffBackendDetail)) {
      addItem("manual-takeoff", "danger", "Исполнитель отбора",
              takeoffBackendDetail, true);
    } else {
      addItem("manual-takeoff", "good", "Исполнитель отбора",
              takeoffBackendDetail, false);
    }
  } else if (mode == Mode::DISTILLATION) {
    const float endTemp =
        !params["endTemp"].isNull() ? clampFloatRange(params["endTemp"].as<float>(), 70.0f, 110.0f)
                                     : g_settings.distillationUi.endTempC;
    const RectTakeoffBackendType backendType =
        !params["takeoffBackendType"].isNull()
            ? clampRectTakeoffBackendType(params["takeoffBackendType"].as<uint32_t>())
            : g_settings.distillationUi.takeoffBackendType;
    const bool valveSafeVentConfirmed =
        !params["valveSafeVentConfirmed"].isNull()
            ? params["valveSafeVentConfirmed"].as<bool>()
            : g_settings.distillationUi.valveSafeVentConfirmed;
    const FractionProgram fractionProgram =
        !params["fractionProgram"].isNull()
            ? parseFractionProgramJson(params["fractionProgram"].as<JsonObject>(),
                                       g_settings.fractionProgram)
            : g_settings.fractionProgram;
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

    String takeoffBackendDetail;
    if (!RectTakeoff::validateBackendConfiguration(backendType, &takeoffBackendDetail)) {
      addItem("dist-backend", "danger", "Takeoff backend",
              String("Backend is not configured: ") + takeoffBackendDetail, true);
    } else {
      addItem("dist-backend", "good", "Takeoff backend",
              String("Backend is configured: ") + takeoffBackendDetail, false);
    }

    if (RectTakeoff::requiresSafeVent(backendType)) {
      if (!valveSafeVentConfirmed) {
        addItem("dist-safe-vent", "danger", "Safe vent",
                "Confirm a safe vent path before valve takeoff start.", true);
      } else {
        addItem("dist-safe-vent", "good", "Safe vent",
                "Safe vent path is confirmed for valve takeoff.", false);
      }
    }

    if (fractionProgram.enabled) {
      bool fractionsValid = true;
      const uint8_t count = fractionProgram.stepCount;
      if (count == 0 || count > FRACTION_PROGRAM_MAX_STEPS) {
        addItem("dist-fractions", "danger", "Fraction program",
                "Fraction program step count is invalid.", true);
        fractionsValid = false;
      } else {
        for (uint8_t i = 0; i < count; ++i) {
          const FractionProgramStep &step = fractionProgram.steps[i];
          if (step.pumpRateMlH <= 0.0f) {
            addItem("dist-fractions", "danger", "Fraction program",
                    String("Step ") + String(i + 1) + " has zero takeoff rate.", true);
            fractionsValid = false;
            break;
          }

          String routeDetail;
          if (!RectTakeoff::isFractionRouteSupported(backendType, step.routeIndex, &routeDetail)) {
            addItem("dist-fractions", "danger", "Fraction program",
                    String("Step ") + String(i + 1) + " is not supported: " + routeDetail, true);
            fractionsValid = false;
            break;
          }
        }
      }

      if (fractionsValid) {
        addItem("dist-fractions", "good", "Fraction program",
                "Fraction routes and rates match the selected backend.", false);
      }
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
  } else if (mode == Mode::AUTOCLAVE) {
    if (!autoclaveHardwareReady) {
      const char *reason = AutoclaveHardwareProfileLogic::validationToken(
          AutoclaveHardwareProfile::validate());
      addItem("autoclave-profile", "danger", "Аппаратный профиль автоклава",
              String("Запуск заблокирован: ") +
                  (reason != nullptr ? reason : "профиль не подтверждён"),
              true);
    } else {
      addItem("autoclave-profile", "good", "Аппаратный профиль автоклава",
              "Аппаратный и механический профили подтверждены для работы с паром.",
              false);
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

  static const char *const summaryIds[PREFLIGHT_SUMMARY_COUNT] = {
      "process", "sensors", "safety", "settings", "equipment"};
  static const char *const summaryTitles[PREFLIGHT_SUMMARY_COUNT] = {
      "Состояние процесса", "Датчики режима", "Безопасность и аварии",
      "Профиль и уставки", "Контуры и исполнители"};
  JsonArray summaryChecks = doc["summaryChecks"].to<JsonArray>();
  uint8_t summaryPassedCount = 0;
  for (uint8_t i = 0; i < PREFLIGHT_SUMMARY_COUNT; ++i) {
    const uint8_t flags = summaryFlags[i];
    const bool seen = (flags & SUMMARY_SEEN) != 0;
    const bool blocking = (flags & SUMMARY_BLOCKING) != 0;
    const bool warning = (flags & SUMMARY_WARNING) != 0;
    const char *tone = !seen ? "muted" : (blocking ? "danger"
                                         : (warning ? "warn" : "good"));
    const char *status = !seen ? "Не требуется"
                               : (blocking ? "БЛОК"
                                           : (warning ? "ПРОВЕРИТЬ" : "OK"));
    JsonObject summary = summaryChecks.add<JsonObject>();
    summary["id"] = summaryIds[i];
    summary["title"] = summaryTitles[i];
    summary["status"] = status;
    summary["tone"] = tone;
    summary["blocking"] = blocking;
    if (seen && summaryDetails[i][0] != '\0') {
      summary["detail"] = summaryDetails[i];
    } else {
      summary["detail"] = !seen ? "Для этого режима отдельная проверка не требуется."
                                 : "Проверка пройдена.";
    }
    if (!seen || (!blocking && !warning)) {
      ++summaryPassedCount;
    }
  }

  const bool ready = blockingCount == 0;
  doc["success"] = true;
  doc["mode"] = modeStr;
  doc["autonomyLevel"] = autonomyLevel;
  doc["autonomyLevelCode"] = static_cast<uint8_t>(g_settings.autonomyLevel);
  doc["ready"] = ready;
  doc["blockingCount"] = blockingCount;
  doc["warningCount"] = warningCount;
  doc["summaryPassedCount"] = summaryPassedCount;
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
