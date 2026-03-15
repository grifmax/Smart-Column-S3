/**
 * Smart-Column S3 - Cloud Tunnel (IoT)
 */

#include "cloud_tunnel.h"

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_system.h>
#include <memory>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>

#include "../config.h"
#include "../types.h"
#include "../control/fsm.h"
#include "../control/safety.h"
#include "../storage/nvs_manager.h"

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

static void computeDeviceId() {
  if (deviceId[0]) return;
  uint64_t efuseMac = ESP.getEfuseMac();
  snprintf(deviceId, sizeof(deviceId), "%012llX",
           (unsigned long long)(efuseMac & 0xFFFFFFFFFFFFULL));
}

static bool parseWssUrl(const char* url, String& hostOut, uint16_t& portOut, String& pathOut) {
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
    case Mode::NBK: return "nbk";               // ARCH-2 fix: был "unknown"
    case Mode::FERMENTATION: return "fermentation"; // ARCH-2 fix: был "unknown"
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

static String base64Encode(const uint8_t* data, size_t len) {
  size_t outLen = 0;
  // calculate size
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
  // upper bound
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

  // Если есть активный claim — прикладываем hash/salt/expiresAt
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

static void sendHttpResponse(const char* requestId, int status, const String& bodyJson, const char* error = nullptr) {
  JsonDocument doc;
  doc["type"] = "http_response";
  doc["requestId"] = requestId;
  doc["status"] = status;
  if (error) doc["error"] = error;

  if (bodyJson.length() > 0) {
    const String b64 = base64Encode((const uint8_t*)bodyJson.c_str(), bodyJson.length());
    doc["bodyBase64"] = b64;
  }

  String json;
  serializeJson(doc, json);
  ws.sendTXT(json);
}

static void handleHttpRequest(JsonDocument& req) {
  const char* requestId = req["requestId"] | "";
  const char* method = req["method"] | "GET";
  const char* path = req["path"] | "";
  const char* bodyBase64 = req["bodyBase64"] | "";

  if (!requestId[0] || !path[0]) {
    return;
  }

  // Белый список: только /api/*
  if (strncmp(path, "/api/", 5) != 0) {
    sendHttpResponse(requestId, 400, "", "path_not_allowed");
    return;
  }

  // GET /api/status
  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/status") == 0) {
    // Соберём JSON как в WebServer::/api/status (минимально достаточно для UI)
    JsonDocument doc;
    doc["mode"] = static_cast<int>(g_state.mode);
    doc["modeStr"] = getModeToken(g_state.mode);
    doc["phase"] = static_cast<int>(g_state.rectPhase);
    doc["phaseStr"] = getPhaseToken(g_state.rectPhase);
    doc["paused"] = g_state.paused;
    doc["safetyOk"] = g_state.safetyOk;
    doc["uptime"] = g_state.uptime;
    doc["deviceId"] = deviceId;
    JsonObject alarm = doc["alarm"].to<JsonObject>();
    fillAlarmJson(alarm, g_state, g_settings);

    JsonObject temps = doc["temps"].to<JsonObject>();
    temps["cube"] = g_state.temps.cube;
    temps["columnBottom"] = g_state.temps.columnBottom;
    temps["columnTop"] = g_state.temps.columnTop;
    temps["reflux"] = g_state.temps.reflux;
    temps["tsa"] = g_state.temps.tsa;

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }

  // Управление процессом — базовая поддержка
  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/process/stop") == 0) {
    FSM::stopMode(g_state);
    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }
  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/process/pause") == 0) {
    FSM::pause(g_state);
    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }
  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/process/resume") == 0) {
    FSM::resume(g_state);
    sendHttpResponse(requestId, 200, "{\"success\":true}");
    return;
  }
  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/safety/ack") == 0) {
    Safety::acknowledge(g_state);

    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Alarm acknowledged";
    JsonObject alarm = doc["alarm"].to<JsonObject>();
    fillAlarmJson(alarm, g_state, g_settings);

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, 200, out);
    return;
  }
  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/safety/reset") == 0) {
    char reason[128] = "";
    const bool ok = Safety::reset(g_state, g_settings, reason, sizeof(reason));

    JsonDocument doc;
    doc["success"] = ok;
    doc["message"] = ok ? "Safety alarm reset" : "Safety reset rejected";
    if (!ok) {
      doc["reason"] = reason;
    }
    JsonObject alarm = doc["alarm"].to<JsonObject>();
    fillAlarmJson(alarm, g_state, g_settings);

    String out;
    serializeJson(doc, out);
    sendHttpResponse(requestId, ok ? 200 : 409, out, ok ? nullptr : "unsafe_state");
    return;
  }
  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/process/start") == 0) {
    String bodyJson;
    if (!base64DecodeToString(String(bodyBase64), bodyJson)) {
      sendHttpResponse(requestId, 400, "", "invalid_body_base64");
      return;
    }

    JsonDocument body;
    DeserializationError err = deserializeJson(body, bodyJson);
    if (err) {
      sendHttpResponse(requestId, 400, "", "invalid_json");
      return;
    }

    const char* modeStr = body["mode"];
    if (!modeStr) {
      sendHttpResponse(requestId, 400, "", "mode_required");
      return;
    }

    // Остановить текущий режим
    const bool allowDemoSensorFailure =
        g_settings.demoMode &&
        g_state.currentAlarm.type == AlarmType::SENSOR_FAILURE;

    if (Safety::isLatched(g_state) && !allowDemoSensorFailure) {
      JsonDocument doc;
      doc["success"] = false;
      doc["message"] =
          "Safety alarm is latched. Reset the alarm before starting.";
      JsonObject alarm = doc["alarm"].to<JsonObject>();
      fillAlarmJson(alarm, g_state, g_settings);

      String out;
      serializeJson(doc, out);
      sendHttpResponse(requestId, 409, out, "safety_latched");
      return;
    }

    if (g_state.mode != Mode::IDLE) {
      FSM::stopMode(g_state);
    }

    // Поддерживаем те же mode strings, что и webserver.cpp
    Mode mode = Mode::IDLE;
    if (strcmp(modeStr, "rectification") == 0) mode = Mode::RECTIFICATION;
    else if (strcmp(modeStr, "distillation") == 0) mode = Mode::DISTILLATION;
    else if (strcmp(modeStr, "manual") == 0 || strcmp(modeStr, "manual_rect") == 0) mode = Mode::MANUAL_RECT;
    else if (strcmp(modeStr, "mashing") == 0) mode = Mode::MASHING;
    else if (strcmp(modeStr, "hold") == 0) mode = Mode::HOLD;
    else {
      sendHttpResponse(requestId, 400, "", "unknown_mode");
      return;
    }

    // Минимально: без params (backend имеет дефолты)
    FSM::startMode(g_state, g_settings, mode);

    sendHttpResponse(requestId, 200, "{\"success\":true,\"message\":\"Process started\"}");
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
          // persist
          NVSManager::saveSettings(g_settings);
          LOG_I("CloudTunnel: token stored (id=%s)", tid);
          // после получения токена можно отправить hello заново, чтобы server отметил auth_ok
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
  // Не активируем туннель, если выключен в настройках или нет WiFi
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
    // lazy connect
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

  // Heartbeat каждые 10 сек
  if (wsConnected && millis() - lastHeartbeatAt > 10000) {
    sendHeartbeat();
  }

  // Переотправить hello раз в минуту (на случай reconnect)
  if (wsConnected && millis() - lastHelloAt > 60000) {
    sendHello();
  }
}

void generateClaim(uint32_t ttlSeconds) {
  computeDeviceId();
  // 6 digits
  uint32_t code = esp_random() % 1000000;
  snprintf(claimCode, sizeof(claimCode), "%06lu", (unsigned long)code);

  // salt 8 bytes hex
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  snprintf(claimSalt, sizeof(claimSalt), "%08lx%08lx", (unsigned long)r1, (unsigned long)r2);

  String toHash = String(claimSalt) + String(claimCode);
  String h = sha256Hex(toHash);
  strlcpy(claimHash, h.c_str(), sizeof(claimHash));

  claimExpiresAt = (uint32_t)(millis() / 1000UL) + ttlSeconds;

  LOG_I("CloudTunnel: claim generated (expires in %lus)", (unsigned long)ttlSeconds);

  // Если уже есть соединение — отправим hello немедленно, чтобы облако увидело claim
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
