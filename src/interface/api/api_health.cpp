#include "api_routes.h"

#include "../../config.h"
#include "../../fs_compat.h"
#include <WiFi.h>

#include "interface/webserver_shared.h"

void registerHealthApiRoutes(AsyncWebServer &server) {
  server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;

    JsonObject temps = doc["temperatures"].to<JsonObject>();
    temps["ok"] = g_state.health.tempSensorsOk;
    temps["total"] = g_state.health.tempSensorsTotal;

    JsonObject sensors = doc["sensors"].to<JsonObject>();
    sensors["bmp280"] = g_state.health.bmp280Ok;
    sensors["ads1115"] = g_state.health.ads1115Ok;
    sensors["pzem"] = g_state.health.pzemOk;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = g_state.health.wifiConnected;
    wifi["rssi"] = g_state.health.wifiRSSI;

    JsonObject system = doc["system"].to<JsonObject>();
    system["uptime"] = g_state.health.uptime;
    system["freeHeap"] = g_state.health.freeHeap;
    system["cpuTemp"] = g_state.health.cpuTemp;

    JsonObject errors = doc["errors"].to<JsonObject>();
    errors["pzemSpikes"] = g_state.health.pzemSpikeCount;
    errors["tempErrors"] = g_state.health.tempReadErrors;

    doc["overallHealth"] = g_state.health.overallHealth;
    doc["lastUpdate"] = g_state.health.lastUpdate;

    JsonArray scores = doc["healthScores"].to<JsonArray>();
    for (int i = 0; i < 6; i++) {
      scores.add(g_state.health.healthScores[i]);
    }

    JsonObject reboot = doc["reboot"].to<JsonObject>();
    reboot["reason"] = g_rebootTracker.lastReason;
    reboot["reasonStr"] = g_rebootTracker.lastReasonStr;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request) {
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

    char deviceId[13] = {0};
    uint64_t efuseMac = ESP.getEfuseMac();
    snprintf(deviceId, sizeof(deviceId), "%012llX",
             (unsigned long long)(efuseMac & 0xFFFFFFFFFFFFULL));
    board["deviceId"] = deviceId;

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
}
