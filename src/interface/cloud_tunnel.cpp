/**
 * Smart-Column S3 - Cloud Tunnel (IoT)
 */

#include "cloud_tunnel.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_system.h>
#include <memory>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>

#include "../config.h"
#include "../history.h"
#include "../history_demo.h"
#include "../live_chart_history.h"
#include "../profiles.h"
#include "../types.h"
#include "../control/fsm.h"
#include "../control/safety.h"
#include "../control/v2/status_adapter.h"
#include "../control/watt_control.h"
#include "../drivers/heater.h"
#include "../drivers/pump.h"
#include "../drivers/sensors.h"
#include "../drivers/stirrer.h"
#include "../drivers/valves.h"
#include "../storage/logger.h"
#include "../storage/nvs_manager.h"
#include "mqtt.h"
#include "security.h"
#include "webserver_shared.h"

extern SystemState g_state;
extern Settings g_settings;

namespace CloudTunnel {

static WebSocketsClient ws;
static bool wsConnected = false;
static bool wsAuthenticated = false;
static bool wsStarted = false;

static char deviceId[13] = {0}; // 12 hex + '\0'

static char claimCode[9] = {0};
static char claimSalt[33] = {0}; // hex
static char claimHash[65] = {0}; // sha256 hex
static uint32_t claimExpiresAt = 0;

static uint32_t lastHelloAt = 0;
static uint32_t lastHeartbeatAt = 0;

static Preferences prefsCloud;

struct PumpCalibrationSessionCloud {
  bool active = false;
  uint32_t startSteps = 0;
  uint32_t stopSteps = 0;
  uint32_t startMs = 0;
  uint32_t stopMs = 0;
};

static PumpCalibrationSessionCloud pumpCalSession;

static void sendHttpResponse(const char* requestId, int status,
                             const String& bodyJson,
                             const char* error = nullptr,
                             const char* contentType = "application/json; charset=utf-8",
                             const char* contentDisposition = nullptr);

static void computeDeviceId() {
  if (deviceId[0]) return;
  uint64_t efuseMac = ESP.getEfuseMac();
  snprintf(deviceId, sizeof(deviceId), "%012llX",
           (unsigned long long)(efuseMac & 0xFFFFFFFFFFFFULL));
}

static bool parseWssUrl(const char* url, String& hostOut, uint16_t& portOut,
                        String& pathOut) {
  if (!url || !url[0]) return false;
  String s(url);
  s.trim();
  if (!s.startsWith("wss://")) return false;
  s.remove(0, 6); // strip wss://

  int slash = s.indexOf('/');
  String hostPort = (slash >= 0) ? s.substring(0, slash) : s;
  pathOut = (slash >= 0) ? s.substring(slash) : String("/");

  int colon = hostPort.indexOf(':');
  if (colon >= 0) {
    hostOut = hostPort.substring(0, colon);
    portOut = (uint16_t)hostPort.substring(colon + 1).toInt();
  } else {
    hostOut = hostPort;
    portOut = 443;
  }
  if (hostOut.length() == 0) return false;
  if (!pathOut.startsWith("/")) pathOut = "/" + pathOut;
  return true;
}

// Stable ASCII tokens to keep cloud status compatible with local Web API/WebSocket.
static const char* getModeToken(Mode mode) {
  switch (mode) {
    case Mode::IDLE: return "idle";
    case Mode::RECTIFICATION: return "rectification";
    case Mode::DISTILLATION: return "distillation";
    case Mode::MANUAL_RECT: return "manual";
    case Mode::MASHING: return "mashing";
    case Mode::HOLD: return "hold";
    case Mode::NBK: return "nbk";
    case Mode::FERMENTATION: return "fermentation";
    default: return "unknown";
  }
}

static const char* getPhaseToken(RectPhase phase) {
  switch (phase) {
    case RectPhase::IDLE: return "idle";
    case RectPhase::HEATING: return "heating";
    case RectPhase::STABILIZATION: return "stabilization";
    case RectPhase::HEADS: return "heads";
    case RectPhase::POST_HEADS_STABILIZATION: return "post_heads";
    case RectPhase::BODY: return "body";
    case RectPhase::TAILS: return "tails";
    case RectPhase::PURGE: return "purge";
    case RectPhase::FINISH: return "finish";
    case RectPhase::COMPLETED: return "completed";
    default: return "unknown";
  }
}

static const char* getMashPhaseToken(MashPhase phase) {
  switch (phase) {
    case MashPhase::IDLE: return "idle";
    case MashPhase::ACID_REST: return "acid_rest";
    case MashPhase::PROTEIN_REST: return "protein_rest";
    case MashPhase::BETA_AMYLASE: return "beta_amylase";
    case MashPhase::ALPHA_AMYLASE: return "alpha_amylase";
    case MashPhase::MASH_OUT: return "mash_out";
    case MashPhase::FINISH: return "finish";
    default: return "unknown";
  }
}

static const char* getNbkPhaseToken(NbkPhase phase) {
  switch (phase) {
    case NbkPhase::IDLE: return "idle";
    case NbkPhase::HEATING: return "heating";
    case NbkPhase::STABILIZATION: return "stabilization";
    case NbkPhase::WORKING: return "working";
    case NbkPhase::FINISH: return "finish";
    case NbkPhase::COMPLETED: return "completed";
    default: return "unknown";
  }
}

static const char* getFermPhaseToken(FermentationPhase phase) {
  switch (phase) {
    case FermentationPhase::IDLE: return "idle";
    case FermentationPhase::RUNNING: return "running";
    case FermentationPhase::COMPLETED: return "completed";
    default: return "unknown";
  }
}

static const char* getActivePhaseToken(const SystemState& state) {
  switch (state.mode) {
    case Mode::MASHING:
      return getMashPhaseToken(state.mashing.phase);
    case Mode::NBK:
      return getNbkPhaseToken(state.nbkPhase);
    case Mode::FERMENTATION:
      return getFermPhaseToken(state.fermPhase);
    default:
      return getPhaseToken(state.rectPhase);
  }
}

static const char* getTempSensorLabel(uint8_t index) {
  switch (index) {
    case TEMP_CUBE: return "Куб";
    case TEMP_COLUMN_BOTTOM: return "Царга низ";
    case TEMP_COLUMN_TOP: return "Царга верх";
    case TEMP_REFLUX: return "Дефлегматор";
    case TEMP_TSA: return "ТСА";
    case TEMP_WATER_IN: return "Вода вход";
    case TEMP_WATER_OUT: return "Вода выход";
    default: return "Датчик";
  }
}

static const char* getTempSensorRoleKey(uint8_t index) {
  switch (index) {
    case TEMP_CUBE: return "cube";
    case TEMP_COLUMN_BOTTOM: return "columnBottom";
    case TEMP_COLUMN_TOP: return "columnTop";
    case TEMP_REFLUX: return "reflux";
    case TEMP_TSA: return "tsa";
    case TEMP_WATER_IN: return "waterIn";
    case TEMP_WATER_OUT: return "waterOut";
    default: return "unknown";
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

static void formatTempAddress(const uint8_t address[8], char* buffer,
                              size_t bufferSize) {
  if (!buffer || bufferSize == 0) return;
  if (!address || isZeroTempAddress(address)) {
    buffer[0] = '\0';
    return;
  }

  snprintf(buffer, bufferSize,
           "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
           address[0], address[1], address[2], address[3], address[4],
           address[5], address[6], address[7]);
}

static float getCurrentTempByIndex(uint8_t index) {
  switch (index) {
    case TEMP_CUBE: return g_state.temps.cube;
    case TEMP_COLUMN_BOTTOM: return g_state.temps.columnBottom;
    case TEMP_COLUMN_TOP: return g_state.temps.columnTop;
    case TEMP_REFLUX: return g_state.temps.reflux;
    case TEMP_TSA: return g_state.temps.tsa;
    case TEMP_WATER_IN: return g_state.temps.waterIn;
    case TEMP_WATER_OUT: return g_state.temps.waterOut;
    default: return 0.0f;
  }
}

static void fillCalibrationJson(JsonObject root) {
  JsonObject pump = root["pump"].to<JsonObject>();
  pump["mlPerRev"] = g_settings.pumpCal.mlPerRevolution;
  pump["stepsPerRev"] = g_settings.pumpCal.stepsPerRevolution;
  pump["microsteps"] = g_settings.pumpCal.microsteps;

  JsonArray temps = root["temperatures"].to<JsonArray>();
  for (uint8_t i = 0; i < TEMP_COUNT; ++i) {
    JsonObject temp = temps.add<JsonObject>();
    temp["index"] = i;
    temp["name"] = getTempSensorLabel(i);
    temp["offset"] = g_settings.tempCal.offsets[i];

    char assignedAddrStr[24];
    formatTempAddress(g_settings.tempCal.addresses[i], assignedAddrStr,
                      sizeof(assignedAddrStr));

    uint8_t detectedAddress[8] = {0};
    char detectedAddrStr[24];
    if (Sensors::getDiscoveredTempAddress(i, detectedAddress)) {
      formatTempAddress(detectedAddress, detectedAddrStr, sizeof(detectedAddrStr));
    } else {
      detectedAddrStr[0] = '\0';
    }

    temp["address"] = detectedAddrStr[0] != '\0' ? detectedAddrStr : assignedAddrStr;
    temp["assignedAddress"] = assignedAddrStr;
    temp["detectedAddress"] = detectedAddrStr;
    temp["mappingMode"] = assignedAddrStr[0] != '\0' ? "manual" : "auto";
    temp["current"] = getCurrentTempByIndex(i);
    temp["valid"] = g_state.temps.valid[i];
  }

  JsonObject pressureSensor = root["pressureSensor"].to<JsonObject>();
  pressureSensor["pointCount"] = g_settings.pressureCal.pointCount;
  JsonArray pressureVoltages = pressureSensor["voltagePoints"].to<JsonArray>();
  JsonArray pressureMmHgPoints = pressureSensor["pressurePoints"].to<JsonArray>();
  for (uint8_t i = 0; i < g_settings.pressureCal.pointCount; ++i) {
    pressureVoltages.add(g_settings.pressureCal.voltagePoints[i]);
    pressureMmHgPoints.add(g_settings.pressureCal.pressurePoints[i]);
  }
  pressureSensor["zeroOffsetMmHg"] = g_settings.pressureCal.zeroOffsetMmHg;
  pressureSensor["ads1115Available"] = g_state.health.ads1115Ok;
  pressureSensor["source"] = "ADS1115 A1 @ 0x48";
  pressureSensor["currentVoltage"] = g_state.pressure.sensorVoltage;
  pressureSensor["currentAdc"] = g_state.pressure.sensorAdc;
  pressureSensor["currentPressure"] = g_state.pressure.cube;
  pressureSensor["valid"] = g_state.pressure.ok;
  pressureSensor["calibrated"] = g_settings.pressureCal.pointCount >= 2;

  JsonObject hydro = root["hydrometer"].to<JsonObject>();
  hydro["densityOffset"] = g_settings.hydroCal.densityOffset;
  hydro["pointCount"] = g_settings.hydroCal.pointCount;
  JsonArray abvPoints = hydro["abvPoints"].to<JsonArray>();
  JsonArray pressurePoints = hydro["pressurePoints"].to<JsonArray>();
  for (uint8_t i = 0; i < g_settings.hydroCal.pointCount; ++i) {
    abvPoints.add(g_settings.hydroCal.abvPoints[i]);
    pressurePoints.add(g_settings.hydroCal.pressurePoints[i]);
  }
  hydro["currentPressure"] = g_state.hydrometer.pressure;
  hydro["currentDensity"] = g_state.hydrometer.density;
  hydro["currentABV"] = g_state.hydrometer.abv;
  hydro["valid"] = g_state.hydrometer.valid;
}

static void fillCloudStirrerJson(JsonObject stirrer, const SystemState& state) {
  stirrer["running"] = state.stirrer.running;
  stirrer["speed"] = state.stirrer.speedPercent;
  stirrer["speedPercent"] = state.stirrer.speedPercent;
  stirrer["available"] = state.stirrer.available;
  stirrer["autoMode"] = state.stirrer.autoMode;
  stirrer["lastUpdate"] = state.stirrer.lastUpdate;
}

static void fillStirrerSettingsJson(JsonObject settings, const Settings& source) {
  settings["enabled"] = source.stirrer.enabled;
  settings["defaultSpeedPercent"] = source.stirrer.defaultSpeedPercent;
  settings["autoMashing"] = source.stirrer.autoMashing;
  settings["autoFermentation"] = source.stirrer.autoFermentation;
  settings["autoNbk"] = source.stirrer.autoNbk;
}

static void sendStirrerHttpResponse(const char* requestId, int statusCode,
                                    bool success, const char* message) {
  syncStirrerState();

  JsonDocument doc;
  doc["success"] = success;
  doc["message"] = message;
  JsonObject stirrer = doc["stirrer"].to<JsonObject>();
  fillCloudStirrerJson(stirrer, g_state);

  String out;
  serializeJson(doc, out);
  sendHttpResponse(requestId, statusCode, out,
                   success ? nullptr : "stirrer_unavailable");
}

static bool ensureStirrerReady(const char* requestId) {
  if (g_state.mode != Mode::IDLE) {
    char reason[128];
    snprintf(reason, sizeof(reason),
             g_state.paused
                 ? "Manual stirrer control is unavailable while %s is paused"
                 : "Manual stirrer control is unavailable while %s is active",
             getModeToken(g_state.mode));
    sendStirrerHttpResponse(requestId, 409, false, reason);
    return false;
  }

  if (!g_settings.stirrer.enabled) {
    sendStirrerHttpResponse(requestId, 409, false,
                            "Stirrer is disabled in settings");
    return false;
  }

  if (!Stirrer::isAvailable()) {
    sendStirrerHttpResponse(requestId, 503, false,
                            "Stirrer DAC is not available");
    return false;
  }

  if (!g_state.safetyOk || Safety::isLatched(g_state)) {
    sendStirrerHttpResponse(requestId, 409, false,
                            "Safety lockout is active");
    return false;
  }

  return true;
}

static void normalizeRectFractions(RectParams& params) {
  params.headsPercent = clampFloatRange(params.headsPercent, 0.0f, 40.0f);
  params.bodyPercent = clampFloatRange(params.bodyPercent, 0.0f, 100.0f);
  params.tailsPercent = clampFloatRange(params.tailsPercent, 0.0f, 100.0f);

  const float sum = params.headsPercent + params.bodyPercent + params.tailsPercent;
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

static void fillCloudStatusJson(JsonObject cloud) {
  cloud["enabled"] = g_settings.cloud.enabled;
  cloud["tunnelUrl"] = g_settings.cloud.tunnelUrl;
  cloud["connected"] = wsConnected;
  cloud["authenticated"] = wsAuthenticated;
  cloud["claimActive"] = hasActiveClaim();
  if (hasActiveClaim()) {
    cloud["claimCode"] = claimCode;
    cloud["claimExpiresAt"] = claimExpiresAt;
  }
}

static void fillEquipmentJson(JsonObject equipment) {
  equipment["heaterPowerW"] = g_settings.equipment.heaterPowerW;
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
}

static String base64Encode(const uint8_t* data, size_t len) {
  size_t outLen = 0;
  mbedtls_base64_encode(nullptr, 0, &outLen, data, len);
  String out;
  out.reserve(outLen + 1);
  std::unique_ptr<uint8_t[]> buf(new uint8_t[outLen + 1]);
  if (mbedtls_base64_encode(buf.get(), outLen + 1, &outLen, data, len) == 0) {
    buf[outLen] = 0;
    out = (char*)buf.get();
  }
  return out;
}

static bool base64DecodeToString(const String& b64, String& out) {
  if (b64.length() == 0) {
    out = "";
    return true;
  }
  size_t outLen = 0;
  const uint8_t* in = (const uint8_t*)b64.c_str();
  const size_t inLen = b64.length();
  std::unique_ptr<uint8_t[]> buf(new uint8_t[inLen + 1]);
  int rc = mbedtls_base64_decode(buf.get(), inLen + 1, &outLen, in, inLen);
  if (rc != 0) return false;
  buf[outLen] = 0;
  out = (char*)buf.get();
  return true;
}

static String sha256Hex(const String& input) {
  uint8_t out[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_setup(&ctx, info, 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char*)input.c_str(), input.length());
  mbedtls_md_finish(&ctx, out);
  mbedtls_md_free(&ctx);

  char hex[65];
  for (int i = 0; i < 32; i++) {
    sprintf(hex + i * 2, "%02x", out[i]);
  }
  hex[64] = 0;
  return String(hex);
}

static void sendHello() {
  if (!wsConnected) return;
  computeDeviceId();

  JsonDocument doc;
  doc["type"] = "hello";
  doc["deviceId"] = deviceId;
  doc["fwVersion"] = FW_VERSION;

  if (g_settings.cloud.token[0]) {
    doc["token"] = g_settings.cloud.token;
  }

  const uint32_t nowSec = (uint32_t)(millis() / 1000UL);
  if (claimCode[0] && claimExpiresAt > nowSec) {
    doc["claimSalt"] = claimSalt;
    doc["claimHash"] = claimHash;
    doc["claimExpiresAt"] = claimExpiresAt;
  }

  String json;
  serializeJson(doc, json);
  ws.sendTXT(json);
  lastHelloAt = millis();
}

static void sendHeartbeat() {
  if (!wsConnected) return;
  computeDeviceId();

  JsonDocument doc;
  doc["type"] = "heartbeat";
  doc["deviceId"] = deviceId;
  doc["uptime"] = g_state.uptime;
  doc["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  doc["ipInfo"] = WiFi.localIP().toString();

  String json;
  serializeJson(doc, json);
  ws.sendTXT(json);
  lastHeartbeatAt = millis();
}

static void sendHttpResponse(const char* requestId, int status,
                             const String& bodyJson,
                             const char* error,
                             const char* contentType,
                             const char* contentDisposition) {
  JsonDocument doc;
  doc["type"] = "http_response";
  doc["requestId"] = requestId;
  doc["status"] = status;
  if (error) doc["error"] = error;
  if (contentType && contentType[0]) {
    doc["contentType"] = contentType;
  }
  if (contentDisposition && contentDisposition[0]) {
    doc["contentDisposition"] = contentDisposition;
  }

  if (bodyJson.length() > 0) {
    const String b64 =
        base64Encode((const uint8_t*)bodyJson.c_str(), bodyJson.length());
    doc["bodyBase64"] = b64;
  }

  String json;
  serializeJson(doc, json);
  ws.sendTXT(json);
}

static void handleHttpRequest(JsonDocument& req) {
  const char* requestId = req["requestId"] | "";
  const char* method = req["method"] | "GET";
  const char* rawPath = req["path"] | "";
  const char* bodyBase64 = req["bodyBase64"] | "";

  if (!requestId[0] || !rawPath[0]) {
    return;
  }

  String pathStorage(rawPath);
  String queryString;
  const int queryIndex = pathStorage.indexOf('?');
  if (queryIndex >= 0) {
    queryString = pathStorage.substring(queryIndex + 1);
    pathStorage.remove(queryIndex);
  }
  const char* path = pathStorage.c_str();

  if (!pathStorage.startsWith("/api/")) {
    sendHttpResponse(requestId, 400, "", "path_not_allowed");
    return;
  }

  auto getQueryParam = [&](const char* key, String& valueOut) -> bool {
    if (!key || !key[0] || queryString.isEmpty()) return false;

    const String needle = String(key) + "=";
    int start = 0;
    while (start >= 0 && start < queryString.length()) {
      const int end = queryString.indexOf('&', start);
      const String part = end >= 0 ? queryString.substring(start, end)
                                   : queryString.substring(start);
      if (part.startsWith(needle)) {
        valueOut = part.substring(needle.length());
        return true;
      }
      if (end < 0) break;
      start = end + 1;
    }
    return false;
  };

  auto isDigitsOnly = [&](const String& value) -> bool {
    if (value.isEmpty()) return false;
    for (size_t i = 0; i < value.length(); ++i) {
      if (!isDigit(static_cast<unsigned char>(value[i]))) {
        return false;
      }
    }
    return true;
  };

  auto isProfileIdValid = [&](const String& value) -> bool {
    if (value.isEmpty()) return false;
    for (size_t i = 0; i < value.length(); ++i) {
      const char c = value[i];
      if (!isAlphaNumeric(static_cast<unsigned char>(c)) && c != '_') {
        return false;
      }
    }
    return true;
  };

  auto matchPathWithId = [&](const char* prefix, const char* suffix,
                             String& idOut, bool digitsOnly,
                             bool profileIdOnly) -> bool {
    if (!pathStorage.startsWith(prefix)) return false;
    String rest = pathStorage.substring(strlen(prefix));
    if (suffix && suffix[0]) {
      if (!rest.endsWith(suffix)) return false;
      rest.remove(rest.length() - strlen(suffix));
    }
    if (rest.isEmpty() || rest.indexOf('/') >= 0) return false;
    if (digitsOnly && !isDigitsOnly(rest)) return false;
    if (profileIdOnly && !isProfileIdValid(rest)) return false;
    idOut = rest;
    return true;
  };

  auto decodeBody = [&](JsonDocument& body) -> bool {
    String bodyJson;
    if (!base64DecodeToString(String(bodyBase64), bodyJson)) {
      sendHttpResponse(requestId, 400, "", "invalid_body_base64");
      return false;
    }
    if (deserializeJson(body, bodyJson)) {
      sendHttpResponse(requestId, 400, "", "invalid_json");
      return false;
    }
    return true;
  };

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/status") == 0) {
    ControlV2::updateRuntime(g_state, g_settings);

    JsonDocument doc;
    doc["mode"] = static_cast<int>(g_state.mode);
    doc["modeStr"] = getModeToken(g_state.mode);
    doc["phase"] = static_cast<int>(g_state.rectPhase);
    doc["phaseStr"] = getActivePhaseToken(g_state);
    doc["paused"] = g_state.paused;
    doc["safetyOk"] = g_state.safetyOk;
    doc["uptime"] = g_state.uptime;
    doc["deviceId"] = deviceId;
    fillAlarmJson(doc["alarm"].to<JsonObject>(), g_state, g_settings);
    fillV2StatusJson(doc["v2"].to<JsonObject>(), ControlV2::getLatestModeStatus(),
                     ControlV2::getLatestMetricsSnapshot());

    JsonObject temps = doc["temps"].to<JsonObject>();
    temps["cube"] = g_state.temps.cube;
    temps["columnBottom"] = g_state.temps.columnBottom;
    temps["columnTop"] = g_state.temps.columnTop;
    temps["reflux"] = g_state.temps.reflux;
    temps["tsa"] = g_state.temps.tsa;
    temps["waterIn"] = g_state.temps.waterIn;
    temps["waterOut"] = g_state.temps.waterOut;

    JsonObject pressure = doc["pressure"].to<JsonObject>();
    pressure["available"] = g_state.pressure.ok;
    pressure["cube"] = g_state.pressure.cube;
    pressure["atm"] = g_state.pressure.atmosphere;

    JsonObject power = doc["power"].to<JsonObject>();
    power["available"] = g_state.power.ok;
    power["voltage"] = g_state.power.voltage;
    power["current"] = g_state.power.current;
    power["power"] = g_state.power.power;
    power["energy"] = g_state.power.energy;
    power["frequency"] = g_state.power.frequency;
    power["pf"] = g_state.power.powerFactor;

    JsonObject pump = doc["pump"].to<JsonObject>();
    pump["speedMlH"] = g_state.pump.speedMlPerHour;
    pump["totalMl"] = g_state.pump.totalVolumeMl;

    JsonObject volumes = doc["volumes"].to<JsonObject>();
    volumes["heads"] = g_state.stats.headsVolume;
    volumes["body"] = g_state.stats.bodyVolume;
    volumes["tails"] = g_state.stats.tailsVolume;

    JsonObject valves = doc["valves"].to<JsonObject>();
    valves["water"] = Valves::getWater();
    valves["heads"] = Valves::getHeads();
    valves["uno"] = Valves::getUno();
    valves["startStopDuty"] = Valves::getStartStop();

    JsonObject hydrometer = doc["hydrometer"].to<JsonObject>();
    hydrometer["abv"] = g_state.hydrometer.abv;
    hydrometer["valid"] = g_state.hydrometer.valid;

    fillEquipmentJson(doc["equipment"].to<JsonObject>());
    fillCloudStatusJson(doc["cloud"].to<JsonObject>());

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/version") == 0) {
    JsonDocument doc;

    JsonObject firmware = doc["firmware"].to<JsonObject>();
    firmware["version"] = FIRMWARE_VERSION;
    firmware["buildDate"] = __DATE__;
    firmware["buildTime"] = __TIME__;
    firmware["compiler"] = "GCC " __VERSION__;

    JsonObject board = doc["board"].to<JsonObject>();
    board["chip"] = "ESP32-S3";
    board["flashSize"] = ESP.getFlashChipSize();
    board["psramSize"] = ESP.getPsramSize();
    board["cpuFreq"] = ESP.getCpuFreqMHz();
    board["mac"] = WiFi.macAddress();
    board["deviceId"] = deviceId;

    File versionFile = LittleFS.open("/version.json", "r");
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

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/charts/live") == 0) {
    JsonDocument doc;
    doc["success"] = true;

    JsonObject meta = doc["meta"].to<JsonObject>();
    JsonObject temperatureMeta = meta["temperatures"].to<JsonObject>();
    for (uint8_t i = 0; i < TEMP_COUNT; ++i) {
      JsonObject channel =
          temperatureMeta[getTempSensorRoleKey(i)].to<JsonObject>();
      channel["label"] = getTempSensorLabel(i);
      channel["installed"] = Safety::isTempSensorInstalled(g_settings.equipment, i);
      channel["assigned"] = !isZeroTempAddress(g_settings.tempCal.addresses[i]);
      uint8_t detectedAddress[8] = {0};
      channel["detected"] = Sensors::getDiscoveredTempAddress(i, detectedAddress);
      channel["valid"] = g_state.temps.valid[i];
    }

    JsonObject powerMeta = meta["power"].to<JsonObject>();
    powerMeta["available"] = g_state.health.pzemOk;

    LiveChartHistory::fillJson(doc.as<JsonObject>());

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/charts/live/reset") == 0) {
    LiveChartHistory::clear();
    sendHttpResponse(requestId, 200,
                     "{\"success\":true,\"message\":\"Live chart history cleared\"}");
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/logs/events") == 0) {
    uint16_t limit = 100;
    uint32_t since = 0;
    String paramValue;
    if (getQueryParam("limit", paramValue)) {
      const long parsed = paramValue.toInt();
      if (parsed > 0) {
        limit = static_cast<uint16_t>(parsed > 200 ? 200 : parsed);
      }
    }
    if (getQueryParam("since", paramValue)) {
      const long parsed = paramValue.toInt();
      if (parsed > 0) {
        since = static_cast<uint32_t>(parsed);
      }
    }

    sendHttpResponse(requestId, 200, Logger::getRecentEventsJson(limit, since));
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/logs/events/clear") == 0) {
    Logger::clearRecentEvents();
    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/export") == 0) {
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

    const String disposition =
        "attachment; filename=\"" + filename + "\"";
    sendHttpResponse(requestId, 200, body, nullptr,
                     "text/csv; charset=utf-8", disposition.c_str());
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/history") == 0) {
    const std::vector<ProcessListItem> processes = getProcessList();

    JsonDocument doc;
    doc["total"] = processes.size();
    JsonArray processArray = doc["processes"].to<JsonArray>();

    for (const auto& process : processes) {
      JsonObject item = processArray.add<JsonObject>();
      item["id"] = process.id;
      item["type"] = process.type;
      item["mode"] = process.mode;
      item["profileId"] = process.profileId;
      item["profile"] = process.profile;
      item["startTime"] = process.startTime;
      item["duration"] = process.duration;
      item["status"] = process.status;
      item["completedSuccessfully"] = process.completedSuccessfully;
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
      JsonObject indicatorsSummary = item["indicatorsSummary"].to<JsonObject>();
      indicatorsSummary["available"] = process.indicatorsAvailable;
      indicatorsSummary["samples"] = process.indicatorSamples;
      indicatorsSummary["avgProcessHealth"] = process.avgProcessHealth;
      indicatorsSummary["minProcessHealth"] = process.minProcessHealth;
      indicatorsSummary["avgStabilityIndex"] = process.avgStabilityIndex;
      indicatorsSummary["minCoolingMarginC"] = process.minCoolingMarginC;
      indicatorsSummary["maxFloodRisk"] = process.maxFloodRisk;
      indicatorsSummary["takeoffShare"] = process.takeoffShare;
      indicatorsSummary["freshnessShare"] = process.freshnessShare;
    }

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "DELETE") == 0 && strcmp(path, "/api/history") == 0) {
    if (clearHistory()) {
      sendHttpResponse(requestId, 200,
                       "{\"success\":true,\"message\":\"All history cleared\"}");
      return;
    }

    sendHttpResponse(requestId, 500, "{\"error\":\"Failed to clear history\"}");
    return;
  }

  if (strcmp(path, "/api/history/demo") == 0) {
    if (strcmp(method, "POST") == 0) {
      bool replaceExisting = false;
      if (bodyBase64 && bodyBase64[0]) {
        JsonDocument body;
        if (!decodeBody(body)) return;
        replaceExisting = body["replace"] | false;
      }

      DemoHistorySeedResult result;
      if (!seedPublicDemoDataset(result, replaceExisting)) {
        sendHttpResponse(
            requestId, 500,
            "{\"success\":false,\"error\":\"Failed to seed demo dataset\"}");
        return;
      }

      JsonDocument doc;
      doc["success"] = true;
      doc["imported"] = result.imported;
      doc["skipped"] = result.skipped;
      doc["removed"] = result.removed;
      doc["demoCount"] = countPublicDemoDatasetEntries();

      String out;
      serializeJson(doc, out);
      sendHttpResponse(requestId, 200, out);
      return;
    }

    if (strcmp(method, "DELETE") == 0) {
      DemoHistorySeedResult result;
      if (!clearPublicDemoDataset(result)) {
        sendHttpResponse(
            requestId, 500,
            "{\"success\":false,\"error\":\"Failed to clear demo dataset\"}");
        return;
      }

      JsonDocument doc;
      doc["success"] = true;
      doc["removed"] = result.removed;
      doc["demoCount"] = countPublicDemoDatasetEntries();

      String out;
      serializeJson(doc, out);
      sendHttpResponse(requestId, 200, out);
      return;
    }
  }

  String historyId;
  if (matchPathWithId("/api/history/", "/advisor", historyId, true, false)) {
    if (strcmp(method, "POST") != 0) {
      sendHttpResponse(requestId, 405, "", "method_not_allowed");
      return;
    }

    JsonDocument body;
    if (!decodeBody(body)) return;

    ProcessAdvisorSnapshot snapshot;
    snapshot.schemaVersion = body["schemaVersion"].as<String>();
    snapshot.createdAt = body["createdAt"] | 0;
    snapshot.baselineProcessId = body["baselineProcessId"].as<String>();
    snapshot.baselineProfile = body["baselineProfile"].as<String>();

    JsonArray items = body["items"];
    size_t itemCount = 0;
    for (JsonObject itemObj : items) {
      if (itemCount >= 16) {
        break;
      }

      ProcessAdvisorItem item;
      item.kind = itemObj["kind"].as<String>();
      item.code = itemObj["code"].as<String>();
      item.tone = itemObj["tone"].as<String>();
      item.title = itemObj["title"].as<String>();
      item.detail = itemObj["detail"].as<String>();
      item.action = itemObj["action"].as<String>();
      item.parameterKey = itemObj["parameterKey"].as<String>();
      item.previousValue = itemObj["previousValue"] | 0.0f;
      item.suggestedValue = itemObj["suggestedValue"] | 0.0f;

      if (item.title.isEmpty()) {
        continue;
      }

      snapshot.items.push_back(item);
      itemCount++;
    }

    if (!updateProcessAdvisorSnapshot(historyId, snapshot)) {
      sendHttpResponse(
          requestId, 500,
          "{\"success\":false,\"error\":\"Failed to update advisor snapshot\"}");
      return;
    }

    JsonDocument doc;
    doc["success"] = true;
    fillTemperatureTopologyJson(doc["temperatureTopology"].to<JsonObject>(),
                                g_settings.equipment);
    fillTemperatureModeSupportJson(doc["supportedModes"].to<JsonObject>(),
                                   g_settings);

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (matchPathWithId("/api/history/", "/export", historyId, true, false)) {
    if (strcmp(method, "GET") != 0) {
      sendHttpResponse(requestId, 405, "", "method_not_allowed");
      return;
    }

    ProcessHistory history;
    if (!loadProcessHistory(historyId, history)) {
      sendHttpResponse(requestId, 404, "{\"error\":\"Process not found\"}");
      return;
    }

    String format = "csv";
    String paramValue;
    if (getQueryParam("format", paramValue) && !paramValue.isEmpty()) {
      format = paramValue;
    }

    String body;
    const char* contentType = nullptr;
    String filename;
    if (format == "json") {
      body = exportProcessToJSON(history);
      contentType = "application/json; charset=utf-8";
      filename = "process_" + historyId + ".json";
    } else if (format == "csv") {
      body = exportProcessToCSV(history);
      contentType = "text/csv; charset=utf-8";
      filename = "process_" + historyId + ".csv";
    } else {
      sendHttpResponse(
          requestId, 400,
          "{\"error\":\"Invalid format. Use csv or json\"}");
      return;
    }

    const String disposition =
        "attachment; filename=\"" + filename + "\"";
    sendHttpResponse(requestId, 200, body, nullptr, contentType,
                     disposition.c_str());
    return;
  }

  if (matchPathWithId("/api/history/", "", historyId, true, false)) {
    if (strcmp(method, "GET") == 0) {
      ProcessHistory history;
      if (!loadProcessHistory(historyId, history)) {
        sendHttpResponse(requestId, 404, "{\"error\":\"Process not found\"}");
        return;
      }

      sendHttpResponse(requestId, 200, exportProcessToJSON(history));
      return;
    }

    if (strcmp(method, "DELETE") == 0) {
      if (deleteProcess(historyId)) {
        sendHttpResponse(requestId, 200,
                         "{\"success\":true,\"message\":\"Process deleted\"}");
        return;
      }

      sendHttpResponse(requestId, 404, "{\"error\":\"Process not found\"}");
      return;
    }
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/reboot/status") == 0) {
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
    const bool lastWasWdt = lastReason == ESP_RST_WDT ||
                            lastReason == ESP_RST_TASK_WDT ||
                            lastReason == ESP_RST_INT_WDT;
    const bool lastWasCrash = lastReason == ESP_RST_PANIC;
    const bool lastWasUser = lastReason == ESP_RST_SW || lastReason == ESP_RST_EXT;

    doc["lastReason"] = lastReason;
    doc["lastReasonStr"] = g_rebootTracker.lastReasonStr;
    doc["totalReboots"] = totalReboots;
    doc["wdtReboots"] = wdtReboots;
    doc["crashReboots"] = crashReboots;
    doc["userReboots"] = userReboots;
    doc["otherReboots"] = otherReboots;
    doc["uptimeSec"] = millis() / 1000UL;
    doc["healthOverall"] = g_state.health.overallHealth;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["lastReasonKind"] =
        lastWasWdt ? "wdt" : lastWasCrash ? "crash" : lastWasUser ? "user" : "other";

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/settings/demo") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    const bool enabled = body["enabled"] | false;
    g_settings.demoMode = enabled;

    Preferences prefs;
    prefs.begin("settings", false);
    prefs.putBool("demoMode", enabled);
    prefs.end();

    JsonDocument doc;
    doc["success"] = true;
    doc["demoMode"] = enabled;
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/profiles") == 0) {
    const std::vector<ProfileListItem> profiles = getProfileList();

    JsonDocument doc;
    doc["total"] = profiles.size();
    JsonArray profileArray = doc["profiles"].to<JsonArray>();
    for (const auto& prof : profiles) {
      JsonObject p = profileArray.add<JsonObject>();
      p["id"] = prof.id;
      p["name"] = prof.name;
      p["description"] = prof.description;
      p["category"] = prof.category;
      JsonArray tags = p["tags"].to<JsonArray>();
      for (const auto& tag : prof.tags) {
        tags.add(tag);
      }
      p["author"] = prof.author;
      p["useCount"] = prof.useCount;
      p["lastUsed"] = prof.lastUsed;
      p["updated"] = prof.updated;
      p["successRate"] = prof.successRate;
      p["successfulRuns"] = prof.successfulRuns;
      p["isBuiltin"] = prof.isBuiltin;
    }

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "DELETE") == 0 && strcmp(path, "/api/profiles") == 0) {
    const bool cleared = clearProfiles();
    sendHttpResponse(
        requestId, cleared ? 200 : 500,
        String("{\"success\":") + (cleared ? "true" : "false") + "}");
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/profiles") == 0) {
    String bodyJson;
    if (!base64DecodeToString(String(bodyBase64), bodyJson)) {
      sendHttpResponse(requestId, 400, "", "invalid_body_base64");
      return;
    }

    const String newId = importProfileFromJSON(bodyJson);
    if (newId.isEmpty()) {
      sendHttpResponse(requestId, 400,
                       "{\"error\":\"Failed to create profile\"}");
      return;
    }

    sendHttpResponse(requestId, 201,
                     "{\"success\":true,\"id\":\"" + newId + "\"}");
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/profiles/export") == 0) {
    bool includeBuiltin = false;
    String paramValue;
    if (getQueryParam("includeBuiltin", paramValue)) {
      paramValue.toLowerCase();
      includeBuiltin = paramValue == "1" || paramValue == "true" ||
                       paramValue == "yes" || paramValue == "on";
    }

    sendHttpResponse(requestId, 200, exportAllProfilesToJSON(includeBuiltin));
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/profiles/import") == 0) {
    String bodyJson;
    if (!base64DecodeToString(String(bodyBase64), bodyJson)) {
      sendHttpResponse(requestId, 400, "", "invalid_body_base64");
      return;
    }

    const uint16_t count = importProfilesFromJSON(bodyJson);
    sendHttpResponse(requestId, 200,
                     "{\"success\":true,\"count\":" + String(count) +
                         ",\"imported\":" + String(count) + "}");
    return;
  }

  String profileId;
  if (matchPathWithId("/api/profiles/", "/load", profileId, false, true)) {
    if (strcmp(method, "POST") != 0) {
      sendHttpResponse(requestId, 405, "", "method_not_allowed");
      return;
    }

    if (applyProfile(profileId)) {
      Logger::logf(0, "Profile loaded: %s", profileId.c_str());
      sendHttpResponse(requestId, 200,
                       "{\"success\":true,\"message\":\"Profile loaded\"}");
      return;
    }

    Logger::logf(1, "Profile load failed: %s not found", profileId.c_str());
    sendHttpResponse(requestId, 404, "{\"error\":\"Profile not found\"}");
    return;
  }

  if (matchPathWithId("/api/profiles/", "/export", profileId, false, true)) {
    if (strcmp(method, "GET") != 0) {
      sendHttpResponse(requestId, 405, "", "method_not_allowed");
      return;
    }

    const String json = exportProfileToJSON(profileId);
    if (json.isEmpty()) {
      sendHttpResponse(
          requestId, 404,
          "{\"success\":false,\"error\":\"Profile not found\"}");
      return;
    }

    sendHttpResponse(requestId, 200, json);
    return;
  }

  if (matchPathWithId("/api/profiles/", "", profileId, false, true)) {
    if (strcmp(method, "GET") == 0) {
      const String json = exportProfileToJSON(profileId);
      if (json.isEmpty()) {
        sendHttpResponse(requestId, 404, "{\"error\":\"Profile not found\"}");
        return;
      }

      sendHttpResponse(requestId, 200, json);
      return;
    }

    if (strcmp(method, "DELETE") == 0) {
      if (deleteProfile(profileId)) {
        sendHttpResponse(
            requestId, 200,
            "{\"success\":true,\"message\":\"Profile deleted\"}");
        return;
      }

      sendHttpResponse(requestId, 404,
                       "{\"error\":\"Profile not found or builtin\"}");
      return;
    }

    if (strcmp(method, "PUT") == 0) {
      Profile profile;
      if (!loadProfile(profileId, profile)) {
        sendHttpResponse(
            requestId, 404,
            "{\"success\":false,\"error\":\"Profile not found\"}");
        return;
      }
      if (profile.metadata.isBuiltin) {
        sendHttpResponse(
            requestId, 403,
            "{\"success\":false,\"error\":\"Builtin profile is read-only\"}");
        return;
      }

      JsonDocument body;
      if (!decodeBody(body)) return;

      JsonObject metadata = body["metadata"].is<JsonObject>()
                                ? body["metadata"].as<JsonObject>()
                                : JsonObject();
      JsonObject parameters = body["parameters"].is<JsonObject>()
                                  ? body["parameters"].as<JsonObject>()
                                  : JsonObject();

      if (!metadata.isNull()) {
        if (!metadata["name"].isNull()) {
          profile.metadata.name = metadata["name"].as<String>();
        }
        if (!metadata["description"].isNull()) {
          profile.metadata.description = metadata["description"].as<String>();
        }
        if (!metadata["category"].isNull()) {
          profile.metadata.category = metadata["category"].as<String>();
        }
        if (metadata["tags"].is<JsonArray>()) {
          profile.metadata.tags.clear();
          for (JsonVariant tag : metadata["tags"].as<JsonArray>()) {
            const String value = tag.as<String>();
            if (!value.isEmpty()) {
              profile.metadata.tags.push_back(value);
            }
          }
        }
      }

      if (!parameters.isNull()) {
        profile.parameters.mode = !parameters["mode"].isNull()
                                      ? parameters["mode"].as<String>()
                                      : profile.metadata.category;
        if (!parameters["model"].isNull()) {
          profile.parameters.model = parameters["model"].as<String>();
        }

        JsonObject heater = parameters["heater"].is<JsonObject>()
                                ? parameters["heater"].as<JsonObject>()
                                : JsonObject();
        if (!heater.isNull()) {
          if (!heater["maxPower"].isNull()) {
            profile.parameters.heater.maxPower =
                clampU16Range(heater["maxPower"].as<uint32_t>(), 300, 10000);
          }
          if (!heater["autoMode"].isNull()) {
            profile.parameters.heater.autoMode = heater["autoMode"].as<bool>();
          }
          if (!heater["pidKp"].isNull()) {
            profile.parameters.heater.pidKp =
                clampFloatRange(heater["pidKp"].as<float>(), 0.0f, 100.0f);
          }
          if (!heater["pidKi"].isNull()) {
            profile.parameters.heater.pidKi =
                clampFloatRange(heater["pidKi"].as<float>(), 0.0f, 100.0f);
          }
          if (!heater["pidKd"].isNull()) {
            profile.parameters.heater.pidKd =
                clampFloatRange(heater["pidKd"].as<float>(), 0.0f, 100.0f);
          }
          if (!heater["boosterEnabled"].isNull()) {
            profile.parameters.heater.boosterEnabled =
                heater["boosterEnabled"].as<bool>();
          }
          if (!heater["boosterStopCubeTempC"].isNull()) {
            profile.parameters.heater.boosterStopCubeTempC = clampFloatRange(
                heater["boosterStopCubeTempC"].as<float>(), 20.0f, 100.0f);
          }
        }

        JsonObject rectification = parameters["rectification"].is<JsonObject>()
                                       ? parameters["rectification"].as<JsonObject>()
                                       : JsonObject();
        if (!rectification.isNull()) {
          if (!rectification["stabilizationMin"].isNull()) {
            profile.parameters.rectification.stabilizationMin = clampU16Range(
                rectification["stabilizationMin"].as<uint32_t>(), 1, 180);
          }
          if (!rectification["headsVolume"].isNull()) {
            profile.parameters.rectification.headsVolume = clampU16Range(
                rectification["headsVolume"].as<uint32_t>(), 1, 10000);
          }
          if (!rectification["bodyVolume"].isNull()) {
            profile.parameters.rectification.bodyVolume = clampU16Range(
                rectification["bodyVolume"].as<uint32_t>(), 1, 50000);
          }
          if (!rectification["tailsVolume"].isNull()) {
            profile.parameters.rectification.tailsVolume = clampU16Range(
                rectification["tailsVolume"].as<uint32_t>(), 0, 20000);
          }
          if (!rectification["headsSpeed"].isNull()) {
            profile.parameters.rectification.headsSpeed = clampU16Range(
                rectification["headsSpeed"].as<uint32_t>(), 10, 2000);
          }
          if (!rectification["bodySpeed"].isNull()) {
            profile.parameters.rectification.bodySpeed = clampU16Range(
                rectification["bodySpeed"].as<uint32_t>(), 50, 3000);
          }
          if (!rectification["tailsSpeed"].isNull()) {
            profile.parameters.rectification.tailsSpeed = clampU16Range(
                rectification["tailsSpeed"].as<uint32_t>(), 0, 3000);
          }
          if (!rectification["purgeMin"].isNull()) {
            profile.parameters.rectification.purgeMin = clampU16Range(
                rectification["purgeMin"].as<uint32_t>(), 1, 120);
          }
        }

        JsonObject distillation = parameters["distillation"].is<JsonObject>()
                                      ? parameters["distillation"].as<JsonObject>()
                                      : JsonObject();
        if (!distillation.isNull()) {
          if (!distillation["headsVolume"].isNull()) {
            profile.parameters.distillation.headsVolume = clampU16Range(
                distillation["headsVolume"].as<uint32_t>(), 0, 10000);
          }
          if (!distillation["targetVolume"].isNull()) {
            profile.parameters.distillation.targetVolume = clampU16Range(
                distillation["targetVolume"].as<uint32_t>(), 1, 50000);
          }
          if (!distillation["speed"].isNull()) {
            profile.parameters.distillation.speed = clampU16Range(
                distillation["speed"].as<uint32_t>(), 50, 65000);
          }
          if (!distillation["endTemp"].isNull()) {
            profile.parameters.distillation.endTemp = clampFloatRange(
                distillation["endTemp"].as<float>(), 50.0f, 110.0f);
          }
        }

        JsonObject mashing = parameters["mashing"].is<JsonObject>()
                                 ? parameters["mashing"].as<JsonObject>()
                                 : JsonObject();
        if (!mashing.isNull() && mashing["steps"].is<JsonArray>()) {
          profile.parameters.mashing.steps.clear();
          uint8_t stepIndex = 0;
          for (JsonObject step : mashing["steps"].as<JsonArray>()) {
            if (stepIndex >= 10) break;

            MashingStepParams stepData;
            stepData.temperature = clampFloatRange(
                step["temperature"].as<float>(), 20.0f, 100.0f);
            stepData.duration =
                clampU16Range(step["duration"].as<uint32_t>(), 1, 240);
            String name = step["name"].as<String>();
            name.trim();
            if (name.length() > 31) {
              name = name.substring(0, 31);
            }
            if (name.isEmpty()) {
              name = "Step " + String(stepIndex + 1);
            }
            stepData.name = name;
            profile.parameters.mashing.steps.push_back(stepData);
            stepIndex++;
          }
        }

        JsonObject temperatures = parameters["temperatures"].is<JsonObject>()
                                      ? parameters["temperatures"].as<JsonObject>()
                                      : JsonObject();
        if (!temperatures.isNull()) {
          if (!temperatures["maxCube"].isNull()) {
            profile.parameters.temperatures.maxCube = clampFloatRange(
                temperatures["maxCube"].as<float>(), 50.0f, 120.0f);
          }
          if (!temperatures["maxColumn"].isNull()) {
            profile.parameters.temperatures.maxColumn = clampFloatRange(
                temperatures["maxColumn"].as<float>(), 50.0f, 110.0f);
          }
          if (!temperatures["headsEnd"].isNull()) {
            profile.parameters.temperatures.headsEnd = clampFloatRange(
                temperatures["headsEnd"].as<float>(), 50.0f, 110.0f);
          }
          if (!temperatures["bodyStart"].isNull()) {
            profile.parameters.temperatures.bodyStart = clampFloatRange(
                temperatures["bodyStart"].as<float>(), 50.0f, 110.0f);
          }
          if (!temperatures["bodyEnd"].isNull()) {
            profile.parameters.temperatures.bodyEnd = clampFloatRange(
                temperatures["bodyEnd"].as<float>(), 50.0f, 120.0f);
          }
        }

        JsonObject safety = parameters["safety"].is<JsonObject>()
                                ? parameters["safety"].as<JsonObject>()
                                : JsonObject();
        if (!safety.isNull()) {
          if (!safety["maxRuntime"].isNull()) {
            profile.parameters.safety.maxRuntime =
                clampU16Range(safety["maxRuntime"].as<uint32_t>(), 10, 5000);
          }
          if (!safety["waterFlowMin"].isNull()) {
            profile.parameters.safety.waterFlowMin = clampFloatRange(
                safety["waterFlowMin"].as<float>(), 0.0f, 20.0f);
          }
          if (!safety["pressureMax"].isNull()) {
            profile.parameters.safety.pressureMax =
                clampU16Range(safety["pressureMax"].as<uint32_t>(), 5, 200);
          }
        }
      }

      profile.metadata.updated = millis() / 1000UL;
      if (profile.parameters.mode.isEmpty()) {
        profile.parameters.mode = profile.metadata.category;
      }
      if (profile.parameters.model.isEmpty()) {
        profile.parameters.model = "classic";
      }

      if (!saveProfile(profile)) {
        sendHttpResponse(
            requestId, 400,
            "{\"success\":false,\"error\":\"Failed to validate or save profile\"}");
        return;
      }

      sendHttpResponse(requestId, 200,
                       "{\"success\":true,\"id\":\"" + profile.id + "\"}");
      return;
    }
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/settings/demo") == 0) {
    JsonDocument doc;
    doc["demoMode"] = g_settings.demoMode;
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/reboot") == 0) {
    sendHttpResponse(requestId, 200,
                     "{\"success\":true,\"message\":\"Rebooting...\"}");
    delay(500);
    ESP.restart();
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/settings/equipment") == 0) {
    JsonDocument doc;
    fillEquipmentJson(doc.to<JsonObject>());
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/settings/equipment") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    if (!body["heaterPowerW"].isNull()) {
      g_settings.equipment.heaterPowerW =
          clampU16Range(body["heaterPowerW"].as<uint32_t>(), 1000, 10000);
    }
    if (!body["cubeVolumeL"].isNull()) {
      g_settings.equipment.cubeVolumeL =
          clampFloatRange(body["cubeVolumeL"].as<float>(), 5.0f, 250.0f);
    }
    if (!body["minHeaterSubmergeL"].isNull()) {
      g_settings.equipment.minHeaterSubmergeL =
          clampFloatRange(body["minHeaterSubmergeL"].as<float>(), 0.5f, 100.0f);
    }
    if (!body["waterAutoStartCubeTempC"].isNull()) {
      g_settings.equipment.waterAutoStartCubeTempC = clampFloatRange(
          body["waterAutoStartCubeTempC"].as<float>(), 20.0f, 60.0f);
    }
    if (!body["boosterHeaterEnabled"].isNull()) {
      g_settings.equipment.boosterHeaterEnabled =
          body["boosterHeaterEnabled"].as<bool>();
    }
    if (!body["boosterHeaterPowerW"].isNull()) {
      g_settings.equipment.boosterHeaterPowerW =
          clampU16Range(body["boosterHeaterPowerW"].as<uint32_t>(), 1000, 10000);
    }
    if (!body["boosterHeaterStopCubeTempC"].isNull()) {
      g_settings.equipment.boosterHeaterStopCubeTempC = clampFloatRange(
          body["boosterHeaterStopCubeTempC"].as<float>(), 20.0f, 100.0f);
    }

    if (!NVSManager::saveSettings(g_settings)) {
      sendHttpResponse(requestId, 500, "", "save_failed");
      return;
    }

    JsonDocument doc;
    doc["success"] = true;
    fillEquipmentJson(doc["settings"].to<JsonObject>());
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/settings/safety") == 0) {
    JsonDocument doc;
    doc["pressureMaxMmHg"] = g_settings.safety.pressureMaxMmHg;
    doc["tsaMaxC"] = g_settings.safety.tsaMaxC;
    doc["waterOutMaxC"] = g_settings.safety.waterOutMaxC;
    doc["waterOutRiseRateCMin"] = g_settings.safety.waterOutRiseRateCMin;
    doc["pressureRiseRateMmHgMin"] = g_settings.safety.pressureRiseRateMmHgMin;
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/settings/safety") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    if (!body["pressureMaxMmHg"].isNull()) {
      g_settings.safety.pressureMaxMmHg =
          clampFloatRange(body["pressureMaxMmHg"].as<float>(), 5.0f, 200.0f);
    }
    if (!body["tsaMaxC"].isNull()) {
      g_settings.safety.tsaMaxC =
          clampFloatRange(body["tsaMaxC"].as<float>(), 35.0f, 120.0f);
    }
    if (!body["waterOutMaxC"].isNull()) {
      g_settings.safety.waterOutMaxC =
          clampFloatRange(body["waterOutMaxC"].as<float>(), 30.0f, 120.0f);
    }
    if (!body["waterOutRiseRateCMin"].isNull()) {
      g_settings.safety.waterOutRiseRateCMin = clampFloatRange(
          body["waterOutRiseRateCMin"].as<float>(), 0.5f, 60.0f);
    }
    if (!body["pressureRiseRateMmHgMin"].isNull()) {
      g_settings.safety.pressureRiseRateMmHgMin = clampFloatRange(
          body["pressureRiseRateMmHgMin"].as<float>(), 1.0f, 200.0f);
    }

    if (!NVSManager::saveSettings(g_settings)) {
      sendHttpResponse(requestId, 500, "", "save_failed");
      return;
    }

    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/settings/security") == 0) {
    JsonDocument doc;
    doc["authEnabled"] = g_settings.security.authEnabled;
    doc["rateLimitEnabled"] = g_settings.security.rateLimitEnabled;
    doc["username"] = g_settings.security.username;
    doc["passwordConfigured"] = (g_settings.security.password[0] != '\0');
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/settings/security") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    const bool authEnabled = !body["authEnabled"].isNull()
                                 ? body["authEnabled"].as<bool>()
                                 : g_settings.security.authEnabled;
    const bool rateLimitEnabled = !body["rateLimitEnabled"].isNull()
                                      ? body["rateLimitEnabled"].as<bool>()
                                      : g_settings.security.rateLimitEnabled;
    const bool hasUsernameField = !body["username"].isNull();
    const bool hasPasswordField = !body["password"].isNull();
    const char* username =
        hasUsernameField ? (body["username"] | "") : g_settings.security.username;
    const char* password = hasPasswordField ? (body["password"] | "") : nullptr;

    if (authEnabled && (!username || strlen(username) == 0)) {
      sendHttpResponse(requestId, 400, "", "username_required");
      return;
    }

    const bool hasStoredPassword = (g_settings.security.password[0] != '\0');
    if (authEnabled && !hasStoredPassword && (!password || strlen(password) == 0)) {
      sendHttpResponse(requestId, 400, "", "password_required");
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
      sendHttpResponse(requestId, 500, "", "save_failed");
      return;
    }

    applySecuritySettings();
    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/settings/mqtt") == 0) {
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
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/settings/mqtt") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    const bool enabled = body["enabled"] | g_settings.mqtt.enabled;
    const char* server =
        !body["server"].isNull() ? (body["server"] | "") : g_settings.mqtt.server;
    uint16_t port = !body["port"].isNull()
                        ? static_cast<uint16_t>(body["port"] | g_settings.mqtt.port)
                        : g_settings.mqtt.port;
    const char* username = !body["username"].isNull()
                               ? (body["username"] | "")
                               : g_settings.mqtt.username;
    const char* password = !body["password"].isNull()
                               ? (body["password"] | "")
                               : g_settings.mqtt.password;
    const char* baseTopic = !body["baseTopic"].isNull()
                                ? (body["baseTopic"] | "")
                                : g_settings.mqtt.baseTopic;
    const bool discovery = !body["discovery"].isNull()
                               ? static_cast<bool>(body["discovery"])
                               : g_settings.mqtt.discovery;
    uint32_t publishInterval = !body["publishInterval"].isNull()
                                   ? static_cast<uint32_t>(body["publishInterval"] |
                                                           g_settings.mqtt.publishInterval)
                                   : g_settings.mqtt.publishInterval;

    if (enabled && (!server || server[0] == '\0')) {
      sendHttpResponse(requestId, 400, "", "mqtt_server_required");
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
      sendHttpResponse(requestId, 500, "", "save_failed");
      return;
    }

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

    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/settings/mqtt/test") == 0) {
    if (!g_settings.mqtt.enabled) {
      sendHttpResponse(requestId, 400, "", "mqtt_disabled");
      return;
    }
    if (g_settings.mqtt.server[0] == '\0') {
      sendHttpResponse(requestId, 400, "", "mqtt_server_not_configured");
      return;
    }
    if (WiFi.status() != WL_CONNECTED) {
      sendHttpResponse(requestId, 503, "", "wifi_not_connected");
      return;
    }

    JsonDocument body;
    String message = "Smart-Column S3: MQTT test from Web UI";
    if (bodyBase64 && bodyBase64[0]) {
      if (!decodeBody(body)) return;
      message = body["message"] | message;
    }

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
      sendHttpResponse(requestId, 503, "", "mqtt_broker_unavailable");
      return;
    }

    MQTT::publishNotification("MQTT test", message.c_str(), "info");
    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/settings/stirrer") == 0) {
    syncStirrerState();

    JsonDocument doc;
    fillStirrerSettingsJson(doc.to<JsonObject>(), g_settings);
    doc["available"] = g_state.stirrer.available;
    doc["running"] = g_state.stirrer.running;
    doc["autoMode"] = g_state.stirrer.autoMode;
    doc["speed"] = g_state.stirrer.speedPercent;
    doc["speedPercent"] = g_state.stirrer.speedPercent;
    doc["lastUpdate"] = g_state.stirrer.lastUpdate;

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/settings/stirrer") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    if (!body["enabled"].isNull()) {
      g_settings.stirrer.enabled = body["enabled"].as<bool>();
    }
    if (!body["defaultSpeedPercent"].isNull()) {
      g_settings.stirrer.defaultSpeedPercent =
          clampU8Range(body["defaultSpeedPercent"].as<uint32_t>(), 1, 100);
    }
    if (!body["autoMashing"].isNull()) {
      g_settings.stirrer.autoMashing = body["autoMashing"].as<bool>();
    }
    if (!body["autoFermentation"].isNull()) {
      g_settings.stirrer.autoFermentation = body["autoFermentation"].as<bool>();
    }
    if (!body["autoNbk"].isNull()) {
      g_settings.stirrer.autoNbk = body["autoNbk"].as<bool>();
    }

    if (!g_settings.stirrer.enabled) {
      g_state.stirrer.autoMode = false;
      Stirrer::stop();
    }

    if (!NVSManager::saveSettings(g_settings)) {
      sendHttpResponse(requestId, 500, "", "save_failed");
      return;
    }

    syncStirrerState();
    JsonDocument doc;
    doc["success"] = true;
    fillStirrerSettingsJson(doc["settings"].to<JsonObject>(), g_settings);
    fillCloudStirrerJson(doc["stirrer"].to<JsonObject>(), g_state);
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/stirrer/start") == 0) {
    JsonDocument body;
    bool hasBody = bodyBase64 && bodyBase64[0];
    if (hasBody && !decodeBody(body)) return;
    if (!ensureStirrerReady(requestId)) return;

    uint8_t speed = g_settings.stirrer.defaultSpeedPercent;
    if (hasBody) {
      const int requestedSpeed = body["speed"] | 0;
      if (requestedSpeed < 0 || requestedSpeed > 100) {
        sendHttpResponse(requestId, 400, "", "invalid_stirrer_speed");
        return;
      }
      if (requestedSpeed > 0) {
        speed = static_cast<uint8_t>(requestedSpeed);
      }
    }

    g_state.stirrer.autoMode = false;
    Stirrer::start(speed);
    sendStirrerHttpResponse(requestId, 200, true, "Stirrer started");
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/stirrer/stop") == 0) {
    if (!ensureStirrerReady(requestId)) return;
    g_state.stirrer.autoMode = false;
    Stirrer::stop();
    sendStirrerHttpResponse(requestId, 200, true, "Stirrer stopped");
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/stirrer/set") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;
    if (!ensureStirrerReady(requestId)) return;

    if (!Stirrer::isRunning()) {
      sendStirrerHttpResponse(requestId, 409, false, "Stirrer is not running");
      return;
    }
    if (body["speed"].isNull()) {
      sendHttpResponse(requestId, 400, "", "stirrer_speed_required");
      return;
    }

    const int requestedSpeed = body["speed"].as<int>();
    if (requestedSpeed < 1 || requestedSpeed > 100) {
      sendHttpResponse(requestId, 400, "", "invalid_stirrer_speed");
      return;
    }

    g_state.stirrer.autoMode = false;
    Stirrer::setSpeed(static_cast<uint8_t>(requestedSpeed));
    sendStirrerHttpResponse(requestId, 200, true, "Stirrer speed updated");
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/settings/rect") == 0) {
    JsonDocument doc;
    const RectParams& params = g_settings.rectParams;
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
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/settings/rect") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    JsonObject params = body["params"].is<JsonObject>() ? body["params"].as<JsonObject>()
                                                        : body.as<JsonObject>();
    RectParams updated = g_settings.rectParams;
    bool fractionsUpdated = false;

    if (!params["feedstock"].isNull()) {
      updated.feedstock =
          static_cast<uint8_t>(clampU16Range(params["feedstock"].as<uint32_t>(), 0, 7));
    }
    if (!params["feedVolumeL"].isNull()) {
      updated.feedVolumeL =
          clampFloatRange(params["feedVolumeL"].as<float>(), 1.0f, 250.0f);
    }
    if (!params["feedAbvPercent"].isNull()) {
      updated.feedAbvPercent =
          clampFloatRange(params["feedAbvPercent"].as<float>(), 1.0f, 96.0f);
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
      updated.headsSpeedMlHKw =
          clampFloatRange(params["headsSpeedMlHKw"].as<float>(), 10.0f, 2000.0f);
    }
    if (!params["bodySpeedMlHKw"].isNull()) {
      updated.bodySpeedMlHKw =
          clampFloatRange(params["bodySpeedMlHKw"].as<float>(), 50.0f, 3000.0f);
    }
    if (!params["stabilizationMin"].isNull()) {
      updated.stabilizationMin =
          clampU16Range(params["stabilizationMin"].as<uint32_t>(), 1, 180);
    }
    if (!params["purgeMin"].isNull()) {
      updated.purgeMin = clampU16Range(params["purgeMin"].as<uint32_t>(), 1, 120);
    }
    if (!params["baroCorrectionEnabled"].isNull()) {
      updated.baroCorrectionEnabled = params["baroCorrectionEnabled"].as<bool>();
    }

    if (fractionsUpdated) {
      normalizeRectFractions(updated);
    }

    g_settings.rectParams = updated;
    if (!NVSManager::saveSettings(g_settings)) {
      sendHttpResponse(requestId, 500, "", "save_failed");
      return;
    }

    JsonDocument doc;
    doc["success"] = true;
    doc["feedstock"] = g_settings.rectParams.feedstock;
    doc["feedVolumeL"] = g_settings.rectParams.feedVolumeL;
    doc["feedAbvPercent"] = g_settings.rectParams.feedAbvPercent;
    doc["headsPercent"] = g_settings.rectParams.headsPercent;
    doc["bodyPercent"] = g_settings.rectParams.bodyPercent;
    doc["tailsPercent"] = g_settings.rectParams.tailsPercent;
    doc["headsSpeedMlHKw"] = g_settings.rectParams.headsSpeedMlHKw;
    doc["bodySpeedMlHKw"] = g_settings.rectParams.bodySpeedMlHKw;
    doc["stabilizationMin"] = g_settings.rectParams.stabilizationMin;
    doc["purgeMin"] = g_settings.rectParams.purgeMin;
    doc["baroCorrectionEnabled"] = g_settings.rectParams.baroCorrectionEnabled;
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/settings/nbk") == 0) {
    JsonDocument doc;
    doc["powerW"] = g_settings.nbk.powerW;
    doc["pumpSpeedMlH"] = g_settings.nbk.pumpSpeedMlH;
    doc["columnBottomTempThresholdC"] = g_settings.nbk.columnBottomTempThresholdC;
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/settings/nbk") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    if (!body["powerW"].isNull()) {
      g_settings.nbk.powerW =
          clampFloatRange(body["powerW"].as<float>(), 500.0f, 5500.0f);
    }
    if (!body["pumpSpeedMlH"].isNull()) {
      g_settings.nbk.pumpSpeedMlH =
          clampFloatRange(body["pumpSpeedMlH"].as<float>(), 100.0f, 120000.0f);
    }
    if (!body["columnBottomTempThresholdC"].isNull()) {
      g_settings.nbk.columnBottomTempThresholdC = clampFloatRange(
          body["columnBottomTempThresholdC"].as<float>(), 50.0f, 110.0f);
    }

    if (!NVSManager::saveSettings(g_settings)) {
      sendHttpResponse(requestId, 500, "", "save_failed");
      return;
    }

    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/settings/fermentation") == 0) {
    JsonDocument doc;
    doc["targetTempC"] = g_settings.fermentation.targetTempC;
    doc["hysteresisC"] = g_settings.fermentation.hysteresisC;
    doc["useHeater"] = g_settings.fermentation.useHeater;
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/settings/fermentation") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    if (!body["targetTempC"].isNull()) {
      g_settings.fermentation.targetTempC =
          clampFloatRange(body["targetTempC"].as<float>(), 5.0f, 45.0f);
    }
    if (!body["hysteresisC"].isNull()) {
      g_settings.fermentation.hysteresisC =
          clampFloatRange(body["hysteresisC"].as<float>(), 0.1f, 10.0f);
    }
    if (!body["useHeater"].isNull()) {
      g_settings.fermentation.useHeater = body["useHeater"].as<bool>();
    }

    if (!NVSManager::saveSettings(g_settings)) {
      sendHttpResponse(requestId, 500, "", "save_failed");
      return;
    }

    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/calibration") == 0) {
    JsonDocument doc;
    fillCalibrationJson(doc.to<JsonObject>());
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/calibration/pump") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    if (!body["mlPerRev"].isNull() || !body["stepsPerRev"].isNull()) {
      if (!body["mlPerRev"].isNull()) {
        g_settings.pumpCal.mlPerRevolution = body["mlPerRev"].as<float>();
      }
      if (!body["stepsPerRev"].isNull()) {
        g_settings.pumpCal.stepsPerRevolution = body["stepsPerRev"].as<uint16_t>();
      }

      if (!NVSManager::saveSettings(g_settings)) {
        sendHttpResponse(requestId, 500, "", "save_failed");
        return;
      }

      sendHttpResponse(requestId, 200,
                       "{\"status\":\"ok\",\"method\":\"direct\"}");
      return;
    }

    if (!body["knownVolume"].isNull() && !body["steps"].isNull()) {
      const float knownVolume = body["knownVolume"].as<float>();
      const uint32_t steps = body["steps"].as<uint32_t>();
      const uint16_t stepsPerRev =
          g_settings.pumpCal.stepsPerRevolution * g_settings.pumpCal.microsteps;
      const float revolutions =
          stepsPerRev > 0 ? static_cast<float>(steps) / stepsPerRev : 0.0f;

      if (revolutions > 0.0f) {
        g_settings.pumpCal.mlPerRevolution = knownVolume / revolutions;
        if (!NVSManager::saveSettings(g_settings)) {
          sendHttpResponse(requestId, 500, "", "save_failed");
          return;
        }

        JsonDocument doc;
        doc["success"] = true;
        doc["mlPerRev"] = g_settings.pumpCal.mlPerRevolution;
        doc["steps"] = steps;
        String out;
        serializeJson(doc, out);
        sendHttpResponse(requestId, 200, out);
        return;
      }
    }

    sendHttpResponse(requestId, 400, "", "unsupported_calibration_payload");
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/pump/calibrate/start") == 0) {
    if (g_state.mode != Mode::IDLE) {
      sendHttpResponse(requestId, 409, "",
                       "process_is_running");
      return;
    }
    if (pumpCalSession.active) {
      sendHttpResponse(requestId, 409, "",
                       "calibration_already_active");
      return;
    }

    float speed = Pump::getMaxSpeedMlH();
    String paramValue;
    if (getQueryParam("speed", paramValue)) {
      const float requested = paramValue.toFloat();
      if (requested > 0.0f) {
        speed = requested;
      }
    } else if (bodyBase64 && bodyBase64[0]) {
      JsonDocument body;
      if (!decodeBody(body)) return;
      const float requested = body["speed"] | 0.0f;
      if (requested > 0.0f) {
        speed = requested;
      }
    }

    Pump::resetVolume();
    pumpCalSession.active = true;
    pumpCalSession.startSteps = Pump::getTotalSteps();
    pumpCalSession.stopSteps = 0;
    pumpCalSession.startMs = millis();
    pumpCalSession.stopMs = 0;

    Pump::start(speed);
    sendHttpResponse(requestId, 200,
                     "{\"success\":true,\"running\":true}");
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/pump/calibrate/stop") == 0) {
    if (!pumpCalSession.active) {
      sendHttpResponse(requestId, 409, "", "calibration_not_active");
      return;
    }

    Pump::stop();
    pumpCalSession.stopSteps = Pump::getTotalSteps();
    pumpCalSession.stopMs = millis();

    sendHttpResponse(requestId, 200,
                     "{\"success\":true,\"running\":false}");
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/pump/calibrate/cancel") == 0) {
    Pump::stop();
    pumpCalSession.active = false;
    pumpCalSession.startSteps = 0;
    pumpCalSession.stopSteps = 0;
    pumpCalSession.startMs = 0;
    pumpCalSession.stopMs = 0;
    sendHttpResponse(requestId, 200,
                     "{\"success\":true,\"active\":false}");
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/pump/calibrate/finish") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    if (!pumpCalSession.active) {
      sendHttpResponse(requestId, 409, "", "calibration_not_active");
      return;
    }

    const float volume = body["volume"] | 0.0f;
    if (volume <= 0.0f) {
      sendHttpResponse(requestId, 400, "", "volume_must_be_positive");
      return;
    }

    if (Pump::isRunning()) {
      Pump::stop();
    }
    if (pumpCalSession.stopSteps == 0) {
      pumpCalSession.stopSteps = Pump::getTotalSteps();
      pumpCalSession.stopMs = millis();
    }

    const uint32_t steps = pumpCalSession.stopSteps - pumpCalSession.startSteps;
    const uint32_t stepsPerRev =
        static_cast<uint32_t>(PUMP_STEPS_PER_REV) *
        static_cast<uint32_t>(PUMP_MICROSTEPS);
    if (steps == 0 || stepsPerRev == 0) {
      sendHttpResponse(requestId, 400, "", "no_steps_captured");
      return;
    }

    const float revolutions = static_cast<float>(steps) / static_cast<float>(stepsPerRev);
    const float mlPerRev = volume / revolutions;
    if (mlPerRev <= 0.0f) {
      sendHttpResponse(requestId, 400, "", "invalid_calibration_result");
      return;
    }

    g_settings.pumpCal.mlPerRevolution = mlPerRev;
    if (!NVSManager::saveSettings(g_settings)) {
      sendHttpResponse(requestId, 500, "", "save_failed");
      return;
    }
    Pump::setCalibration(mlPerRev);

    const uint32_t elapsedMs =
        (pumpCalSession.stopMs > pumpCalSession.startMs)
            ? (pumpCalSession.stopMs - pumpCalSession.startMs)
            : 30000;
    const float elapsedSec = elapsedMs / 1000.0f;
    const float calibrationFactor =
        elapsedSec > 0.0f ? (volume / elapsedSec) : 0.0f;

    pumpCalSession.active = false;

    JsonDocument doc;
    doc["success"] = true;
    doc["mlPerRev"] = mlPerRev;
    doc["calibrationFactor"] = calibrationFactor;
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/pump/stop") == 0) {
    Pump::stop();
    JsonDocument doc;
    doc["success"] = true;
    doc["running"] = Pump::isRunning();
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/process/stop") == 0) {
    FSM::stopMode(g_state);
    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }
  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/process/pause") == 0) {
    FSM::pause(g_state);
    ControlV2::updateRuntime(g_state, g_settings);
    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }
  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/process/resume") == 0) {
    FSM::resume(g_state);
    ControlV2::updateRuntime(g_state, g_settings);
    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }
  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/safety/ack") == 0) {
    Safety::acknowledge(g_state);
    ControlV2::updateRuntime(g_state, g_settings);

    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Alarm acknowledged";
    fillAlarmJson(doc["alarm"].to<JsonObject>(), g_state, g_settings);
    fillSafetyActionV2Json(doc["v2"].to<JsonObject>(),
                           ControlV2::getLatestModeStatus(),
                           ControlV2::getLatestMetricsSnapshot());

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }
  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/safety/reset") == 0) {
    char reason[128] = "";
    const bool ok = Safety::reset(g_state, g_settings, reason, sizeof(reason));
    ControlV2::updateRuntime(g_state, g_settings);

    JsonDocument doc;
    doc["success"] = ok;
    doc["message"] = ok ? "Safety alarm reset" : "Safety reset rejected";
    if (!ok) {
      doc["reason"] = reason;
    }
    fillAlarmJson(doc["alarm"].to<JsonObject>(), g_state, g_settings);
    fillSafetyActionV2Json(doc["v2"].to<JsonObject>(),
                           ControlV2::getLatestModeStatus(),
                           ControlV2::getLatestMetricsSnapshot());

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, ok ? 200 : 409, out,
                     ok ? nullptr : "unsafe_state");
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/process/preflight") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    const char* modeStr = body["mode"] | "";
    Mode requestedMode = Mode::IDLE;
    if (!modeStr[0] || !parseRequestedMode(modeStr, requestedMode)) {
      sendHttpResponse(requestId, 400, "", "unknown_mode");
      return;
    }

    const bool allowDemoSensorFailure =
        g_settings.demoMode &&
        g_state.currentAlarm.type == AlarmType::SENSOR_FAILURE;
    const bool safetyBlocked = Safety::isLatched(g_state) && !allowDemoSensorFailure;
    const bool modeBlocked = g_state.mode != Mode::IDLE;

    JsonDocument doc;
    doc["success"] = true;
    doc["mode"] = modeStr;
    doc["ready"] = !safetyBlocked && !modeBlocked;
    doc["blockingCount"] = (safetyBlocked ? 1 : 0) + (modeBlocked ? 1 : 0);
    doc["warningCount"] = 0;

    if (safetyBlocked) {
      doc["tone"] = "danger";
      doc["title"] = "Запуск заблокирован";
      doc["detail"] = "Сначала снимите защёлкнутую аварию безопасности.";
    } else if (modeBlocked) {
      doc["tone"] = "danger";
      doc["title"] = "Запуск заблокирован";
      doc["detail"] = "Новый запуск недоступен, пока текущий процесс не остановлен.";
    } else {
      doc["tone"] = "good";
      doc["title"] = "Можно запускать";
      doc["detail"] = "Минимальный backend preflight не видит критичных блокировок.";
    }

    JsonArray items = doc["items"].to<JsonArray>();
    JsonObject safetyItem = items.add<JsonObject>();
    safetyItem["id"] = "safety";
    safetyItem["tone"] = safetyBlocked ? "danger" : "good";
    safetyItem["title"] = "Безопасность";
    safetyItem["detail"] = safetyBlocked
                               ? "Есть защёлкнутая авария безопасности."
                               : "Защёлкнутых аварий нет.";
    safetyItem["blocking"] = safetyBlocked;

    JsonObject modeItem = items.add<JsonObject>();
    modeItem["id"] = "mode";
    modeItem["tone"] = modeBlocked ? "danger" : "good";
    modeItem["title"] = "Режим";
    modeItem["detail"] = modeBlocked
                             ? "Контроллер уже занят активным процессом."
                             : "Контроллер находится в idle.";
    modeItem["blocking"] = modeBlocked;

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/process/start") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    const char* modeStr = body["mode"] | "";
    if (!modeStr[0]) {
      sendHttpResponse(requestId, 400, "", "mode_required");
      return;
    }

    JsonObject params = body["params"].as<JsonObject>();
    Mode mode = Mode::IDLE;
    if (!parseRequestedMode(modeStr, mode)) {
      sendHttpResponse(requestId, 400, "", "unknown_mode");
      return;
    }

    const bool allowDemoSensorFailure =
        g_settings.demoMode &&
        g_state.currentAlarm.type == AlarmType::SENSOR_FAILURE;
    if (Safety::isLatched(g_state) && !allowDemoSensorFailure) {
      JsonDocument doc;
      doc["success"] = false;
      doc["message"] =
          "Safety alarm is latched. Reset the alarm before starting.";
      fillAlarmJson(doc["alarm"].to<JsonObject>(), g_state, g_settings);
      String out;
      serializeJson(doc, out);
      sendHttpResponse(requestId, 409, out, "safety_latched");
      return;
    }

    const bool sensorsOk =
        g_state.health.tempSensorsTotal > 0 && g_state.health.tempSensorsOk;
    if (g_state.mode != Mode::IDLE) {
      sendHttpResponse(requestId, 409, "", "process_already_active");
      return;
    }

    if (mode == Mode::RECTIFICATION || mode == Mode::DISTILLATION ||
        mode == Mode::NBK) {
      applyBoosterStartOverride(params, g_settings);
    }

    if (mode == Mode::RECTIFICATION) {
      if (!params.isNull()) {
        if (!params["feedstock"].isNull()) {
          g_settings.rectParams.feedstock = static_cast<uint8_t>(
              clampU16Range(params["feedstock"].as<uint32_t>(), 0, 7));
        }
        if (!params["feedVolumeL"].isNull()) {
          g_settings.rectParams.feedVolumeL =
              clampFloatRange(params["feedVolumeL"].as<float>(), 1.0f, 250.0f);
        }
        if (!params["feedAbvPercent"].isNull()) {
          g_settings.rectParams.feedAbvPercent = clampFloatRange(
              params["feedAbvPercent"].as<float>(), 1.0f, 96.0f);
        }
        if (!params["headsPercent"].isNull()) {
          g_settings.rectParams.headsPercent =
              clampFloatRange(params["headsPercent"].as<float>(), 0.0f, 40.0f);
        }
        if (!params["bodyPercent"].isNull()) {
          g_settings.rectParams.bodyPercent =
              clampFloatRange(params["bodyPercent"].as<float>(), 0.0f, 100.0f);
        }
        if (!params["tailsPercent"].isNull()) {
          g_settings.rectParams.tailsPercent =
              clampFloatRange(params["tailsPercent"].as<float>(), 0.0f, 100.0f);
        }
        if (!params["headsSpeedMlHKw"].isNull()) {
          g_settings.rectParams.headsSpeedMlHKw = clampFloatRange(
              params["headsSpeedMlHKw"].as<float>(), 10.0f, 2000.0f);
        }
        if (!params["bodySpeedMlHKw"].isNull()) {
          g_settings.rectParams.bodySpeedMlHKw = clampFloatRange(
              params["bodySpeedMlHKw"].as<float>(), 50.0f, 3000.0f);
        }
        if (!params["stabilizationMin"].isNull()) {
          g_settings.rectParams.stabilizationMin =
              clampU16Range(params["stabilizationMin"].as<uint32_t>(), 1, 180);
        }
        if (!params["purgeMin"].isNull()) {
          g_settings.rectParams.purgeMin =
              clampU16Range(params["purgeMin"].as<uint32_t>(), 1, 120);
        }
        if (!params["baroCorrectionEnabled"].isNull()) {
          g_settings.rectParams.baroCorrectionEnabled =
              params["baroCorrectionEnabled"].as<bool>();
        }
        normalizeRectFractions(g_settings.rectParams);
      }
      FSM::startMode(g_state, g_settings, mode);
    } else if (mode == Mode::DISTILLATION) {
      const float speed = params["speed"] | 500.0f;
      const float headsVol = params["headsVolume"] | 0.0f;
      const float targetVol = params["targetVolume"] | 0.0f;
      const float endTemp = params["endTemp"] | 96.0f;
      const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
                                      ? g_settings.equipment.heaterPowerW
                                      : DEFAULT_HEATER_POWER_W;
      uint16_t powerWatts = 0;
      if (!params["powerW"].isNull()) {
        powerWatts = clampU16Range(params["powerW"].as<uint32_t>(), 0, heaterMaxW);
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
      static MashProfile runtimeProfile;
      memset(&runtimeProfile, 0, sizeof(runtimeProfile));
      bool hasProfile = false;
      if (!params.isNull() && !params["profile"].isNull()) {
        JsonObject profileObj = params["profile"].as<JsonObject>();
        JsonArray steps = profileObj["steps"].as<JsonArray>();
        if (!profileObj.isNull() && steps.size() > 0) {
          const char* profileName = profileObj["name"] | "Mashing";
          strncpy(runtimeProfile.name, profileName, sizeof(runtimeProfile.name) - 1);
          runtimeProfile.name[sizeof(runtimeProfile.name) - 1] = '\0';
          uint8_t count = 0;
          for (JsonObject step : steps) {
            if (count >= 10) break;
            runtimeProfile.steps[count].temperature = step["temperature"] | 0.0f;
            runtimeProfile.steps[count].duration = step["duration"] | 0;
            const char* stepName = step["name"] | "";
            strncpy(runtimeProfile.steps[count].name, stepName,
                    sizeof(runtimeProfile.steps[count].name) - 1);
            runtimeProfile.steps[count].name[sizeof(runtimeProfile.steps[count].name) - 1] =
                '\0';
            count++;
          }
          runtimeProfile.stepCount = count;
          hasProfile = count > 0;
        }
      }
      if (!hasProfile) {
        strncpy(runtimeProfile.name, "Default Mashing",
                sizeof(runtimeProfile.name) - 1);
        runtimeProfile.stepCount = 5;
        runtimeProfile.steps[0].temperature = 38.0f;
        runtimeProfile.steps[0].duration = 20;
        strncpy(runtimeProfile.steps[0].name, "Acid rest",
                sizeof(runtimeProfile.steps[0].name) - 1);
        runtimeProfile.steps[1].temperature = 52.0f;
        runtimeProfile.steps[1].duration = 20;
        strncpy(runtimeProfile.steps[1].name, "Protein rest",
                sizeof(runtimeProfile.steps[1].name) - 1);
        runtimeProfile.steps[2].temperature = 63.0f;
        runtimeProfile.steps[2].duration = 40;
        strncpy(runtimeProfile.steps[2].name, "Beta amylase",
                sizeof(runtimeProfile.steps[2].name) - 1);
        runtimeProfile.steps[3].temperature = 72.0f;
        runtimeProfile.steps[3].duration = 20;
        strncpy(runtimeProfile.steps[3].name, "Saccharification",
                sizeof(runtimeProfile.steps[3].name) - 1);
        runtimeProfile.steps[4].temperature = 78.0f;
        runtimeProfile.steps[4].duration = 10;
        strncpy(runtimeProfile.steps[4].name, "Mash out",
                sizeof(runtimeProfile.steps[4].name) - 1);
      }
      FSM::Mashing::start(g_state, &runtimeProfile);
    } else if (mode == Mode::HOLD) {
      static TempStep runtimeSteps[10];
      uint8_t count = 0;
      if (!params.isNull() && !params["steps"].isNull()) {
        JsonArray steps = params["steps"].as<JsonArray>();
        for (JsonObject step : steps) {
          if (count >= 10) break;
          runtimeSteps[count].temperature = step["temperature"] | 0.0f;
          runtimeSteps[count].duration = step["duration"] | 0;
          runtimeSteps[count].useCooling = step["useCooling"] | false;
          count++;
        }
      }
      if (count == 0) {
        runtimeSteps[0].temperature = 65.0f;
        runtimeSteps[0].duration = 60;
        runtimeSteps[0].useCooling = false;
        count = 1;
      }
      FSM::Hold::start(g_state, runtimeSteps, count);
    } else {
      if (mode == Mode::NBK && !params.isNull()) {
        if (!params["powerW"].isNull()) {
          g_settings.nbk.powerW =
              clampFloatRange(params["powerW"].as<float>(), 500.0f, 5500.0f);
        }
        if (!params["pumpSpeedMlH"].isNull()) {
          g_settings.nbk.pumpSpeedMlH = clampFloatRange(
              params["pumpSpeedMlH"].as<float>(), 100.0f, 120000.0f);
        }
        if (!params["columnBottomTempThresholdC"].isNull()) {
          g_settings.nbk.columnBottomTempThresholdC = clampFloatRange(
              params["columnBottomTempThresholdC"].as<float>(), 50.0f, 110.0f);
        }
      } else if (mode == Mode::FERMENTATION && !params.isNull()) {
        if (!params["targetTempC"].isNull()) {
          g_settings.fermentation.targetTempC = clampFloatRange(
              params["targetTempC"].as<float>(), 5.0f, 45.0f);
        }
        if (!params["hysteresisC"].isNull()) {
          g_settings.fermentation.hysteresisC = clampFloatRange(
              params["hysteresisC"].as<float>(), 0.1f, 10.0f);
        }
        if (!params["useHeater"].isNull()) {
          g_settings.fermentation.useHeater = params["useHeater"].as<bool>();
        }
      }
      FSM::startMode(g_state, g_settings, mode);
    }

    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Process started";
    if (!sensorsOk) {
      doc["warning"] = "No temperature sensors detected";
    }
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/manual/heater") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
                                    ? g_settings.equipment.heaterPowerW
                                    : DEFAULT_HEATER_POWER_W;
    uint16_t powerWatts = 0;
    if (!body["powerW"].isNull()) {
      powerWatts = clampU16Range(body["powerW"].as<uint32_t>(), 0, heaterMaxW);
    } else {
      int powerPercent = body["power"] | 0;
      if (powerPercent < 0) powerPercent = 0;
      if (powerPercent > 100) powerPercent = 100;
      powerWatts = static_cast<uint16_t>(
          (static_cast<uint32_t>(heaterMaxW) * powerPercent) / 100U);
    }
    Heater::setPowerWatts(powerWatts);

    JsonDocument doc;
    doc["success"] = true;
    doc["powerW"] = powerWatts;
    doc["powerPercent"] = heaterMaxW > 0
                              ? static_cast<uint8_t>(
                                    (static_cast<uint32_t>(powerWatts) * 100U +
                                     heaterMaxW / 2U) /
                                    heaterMaxW)
                              : 0;
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/rect/heater") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
                                    ? g_settings.equipment.heaterPowerW
                                    : DEFAULT_HEATER_POWER_W;
    int powerWatts = -2;
    if (!body["powerW"].isNull()) {
      powerWatts = body["powerW"].as<int>();
    } else {
      int powerPercent = body["power"] | -2;
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

    JsonDocument doc;
    doc["success"] = true;
    doc["powerW"] = powerWatts;
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/manual/pump") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    const float speed = body["speed"] | 0.0f;
    if (speed <= 0.0f) {
      Pump::stop();
      sendHttpResponse(requestId, 200, "{\"success\":true,\"running\":false}");
      return;
    }

    Pump::start(speed);
    JsonDocument doc;
    doc["success"] = true;
    doc["running"] = true;
    doc["speed"] = speed;
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/manual/valves") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    if (body["allOff"] | false) {
      Valves::closeAll();
      sendHttpResponse(requestId, 200, "{\"success\":true}");
      return;
    }
    if (!body["water"].isNull()) {
      Valves::setWater(body["water"].as<bool>());
    }
    if (!body["heads"].isNull()) {
      Valves::setHeads(body["heads"].as<bool>());
    }
    if (!body["uno"].isNull()) {
      Valves::setUno(body["uno"].as<bool>());
    }
    if (!body["startStopDuty"].isNull()) {
      Valves::setStartStop(
          clampU8Range(body["startStopDuty"].as<uint32_t>(), 0, 255));
    }
    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/manual/volumes") == 0) {
    JsonDocument body;
    if (!decodeBody(body)) return;

    bool changed = false;
    if (!body["heads"].isNull()) {
      const float v = body["heads"].as<float>();
      g_state.stats.headsVolume = v < 0.0f ? 0.0f : v;
      changed = true;
    }
    if (!body["body"].isNull()) {
      const float v = body["body"].as<float>();
      g_state.stats.bodyVolume = v < 0.0f ? 0.0f : v;
      changed = true;
    }
    if (!body["tails"].isNull()) {
      const float v = body["tails"].as<float>();
      g_state.stats.tailsVolume = v < 0.0f ? 0.0f : v;
      changed = true;
    }
    if (!changed) {
      sendHttpResponse(requestId, 400, "", "at_least_one_field_required");
      return;
    }

    if (body["syncTotal"] | true) {
      g_state.pump.totalVolumeMl = g_state.stats.headsVolume +
                                   g_state.stats.bodyVolume +
                                   g_state.stats.tailsVolume;
    }

    JsonDocument doc;
    doc["success"] = true;
    doc["heads"] = g_state.stats.headsVolume;
    doc["body"] = g_state.stats.bodyVolume;
    doc["tails"] = g_state.stats.tailsVolume;
    doc["totalMl"] = g_state.pump.totalVolumeMl;
    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  sendHttpResponse(requestId, 404, "", "not_implemented");
}

static void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsConnected = true;
      wsAuthenticated = false;
      LOG_I("CloudTunnel: WS connected");
      sendHello();
      break;
    case WStype_DISCONNECTED:
      wsConnected = false;
      wsAuthenticated = false;
      LOG_W("CloudTunnel: WS disconnected");
      break;
    case WStype_TEXT: {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload, length);
      if (err) return;

      const char* msgType = doc["type"] | "";
      if (strcmp(msgType, "auth_ok") == 0) {
        wsAuthenticated = true;
        LOG_I("CloudTunnel: authenticated");
      } else if (strcmp(msgType, "auth_required") == 0) {
        wsAuthenticated = false;
        LOG_W("CloudTunnel: auth required");
      } else if (strcmp(msgType, "deliver_token") == 0) {
        const char* tok = doc["token"] | "";
        const char* tid = doc["tokenId"] | "";
        if (tok[0]) {
          strlcpy(g_settings.cloud.token, tok, sizeof(g_settings.cloud.token));
          strlcpy(g_settings.cloud.tokenId, tid, sizeof(g_settings.cloud.tokenId));
          NVSManager::saveSettings(g_settings);
          LOG_I("CloudTunnel: token stored (id=%s)", tid);
          sendHello();
        }
      } else if (strcmp(msgType, "http_request") == 0) {
        handleHttpRequest(doc);
      }
      break;
    }
    default:
      break;
  }
}

void init() {
  computeDeviceId();
  prefsCloud.begin("cloud", false);
}

void loop() {
  if (!g_settings.cloud.enabled) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!g_settings.cloud.tunnelUrl[0]) {
    return;
  }

  if (!wsStarted) {
    String host, path;
    uint16_t port = 0;
    if (!parseWssUrl(g_settings.cloud.tunnelUrl, host, port, path)) {
      return;
    }

    ws.beginSSL(host.c_str(), port, path.c_str());
    ws.setReconnectInterval(5000);
    ws.onEvent(onWsEvent);
    wsStarted = true;
  }

  ws.loop();

  if (wsConnected && millis() - lastHeartbeatAt > 10000) {
    sendHeartbeat();
  }

  if (wsConnected && millis() - lastHelloAt > 60000) {
    sendHello();
  }
}

void generateClaim(uint32_t ttlSeconds) {
  computeDeviceId();
  uint32_t code = esp_random() % 1000000;
  snprintf(claimCode, sizeof(claimCode), "%06lu", (unsigned long)code);

  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  snprintf(claimSalt, sizeof(claimSalt), "%08lx%08lx", (unsigned long)r1,
           (unsigned long)r2);

  String toHash = String(claimSalt) + String(claimCode);
  String h = sha256Hex(toHash);
  strlcpy(claimHash, h.c_str(), sizeof(claimHash));

  claimExpiresAt = (uint32_t)(millis() / 1000UL) + ttlSeconds;

  LOG_I("CloudTunnel: claim generated (expires in %lus)",
        (unsigned long)ttlSeconds);

  if (wsConnected) {
    sendHello();
  }
}

bool hasActiveClaim() {
  const uint32_t nowSec = (uint32_t)(millis() / 1000UL);
  return claimCode[0] && claimExpiresAt > nowSec;
}

const char* getClaimCode() {
  return claimCode;
}

uint32_t getClaimExpiresAt() {
  return claimExpiresAt;
}

bool isConnected() {
  return wsConnected;
}

bool isAuthenticated() {
  return wsAuthenticated;
}

const char* getDeviceId() {
  computeDeviceId();
  return deviceId;
}

} // namespace CloudTunnel
