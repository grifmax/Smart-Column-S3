#include "api_routes.h"

#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "interface/wifi_profiles.h"
#include "interface/webserver_shared.h"
#include "storage/logger.h"
#include "storage/nvs_manager.h"

static void appendWiFiProfileJson(JsonObject obj, const WiFiProfile &profile,
                                  uint8_t index) {
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

static void buildWiFiProfilesResponse(JsonDocument &doc) {
  WiFiProfiles::compactProfiles(g_settings.wifi);
  JsonArray profiles = doc["profiles"].to<JsonArray>();
  for (uint8_t i = 0;
       i < g_settings.wifi.profileCount && i < WIFI_MAX_PROFILES; ++i) {
    JsonObject item = profiles.add<JsonObject>();
    appendWiFiProfileJson(item, g_settings.wifi.profiles[i], i);
  }
  doc["count"] = g_settings.wifi.profileCount;
}

static bool isValidIpOrEmpty(const char *value) {
  if (!value || value[0] == '\0') {
    return true;
  }
  IPAddress ip;
  return ip.fromString(value);
}

static void beginWiFiConnectionTask(void *param) {
  WiFiProfile *profile = static_cast<WiFiProfile *>(param);
  if (profile != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(250));
    WiFiProfiles::beginConnection(*profile);
    delete profile;
  }
  vTaskDelete(nullptr);
}

void registerWifiApiRoutes(AsyncWebServer &server) {
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

    WiFi.scanDelete();
  });

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

  server.on("/api/wifi/profiles", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              JsonDocument doc;
              buildWiFiProfilesResponse(doc);

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "^\\/api\\/wifi\\/profile\\/reorder$", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        const char *ssid = doc["ssid"] | "";
        const char *direction = doc["direction"] | "up";
        if (!ssid[0]) {
          sendJsonError(request, 400, "SSID required");
          return;
        }

        const int shift = (strcmp(direction, "down") == 0) ? 1 : -1;
        if (!WiFiProfiles::moveProfile(g_settings.wifi, ssid, shift)) {
          sendJsonError(request, 400, "Cannot change profile priority");
          return;
        }

        if (!NVSManager::saveSettings(g_settings)) {
          Logger::logf(2, "WiFi profile reorder save failed: %s", ssid);
          sendJsonError(request, 500, "Failed to save settings");
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

  server.on(
      "^\\/api\\/wifi\\/profile\\/delete$", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        const char *ssid = doc["ssid"] | "";
        if (!ssid[0]) {
          sendJsonError(request, 400, "SSID required");
          return;
        }

        if (!WiFiProfiles::deleteProfile(g_settings.wifi, ssid)) {
          sendJsonError(request, 404, "Profile not found");
          return;
        }

        if (!NVSManager::saveSettings(g_settings)) {
          Logger::logf(2, "WiFi profile delete save failed: %s", ssid);
          sendJsonError(request, 500, "Failed to save settings");
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

  server.on(
      "^\\/api\\/wifi\\/profile$", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        const char *ssid = doc["ssid"] | "";
        if (!ssid[0]) {
          sendJsonError(request, 400, "SSID required");
          return;
        }

        WiFiProfile profile{};
        profile.enabled = doc["enabled"] | true;
        strlcpy(profile.ssid, ssid, sizeof(profile.ssid));
        strlcpy(profile.password, doc["password"] | "", sizeof(profile.password));
        profile.useStaticIp = doc["useStaticIp"] | false;
        strlcpy(profile.ip, doc["ip"] | "", sizeof(profile.ip));
        strlcpy(profile.gateway, doc["gateway"] | "", sizeof(profile.gateway));
        strlcpy(profile.subnet, doc["subnet"] | "255.255.255.0",
                sizeof(profile.subnet));
        strlcpy(profile.dns1, doc["dns1"] | "", sizeof(profile.dns1));
        strlcpy(profile.dns2, doc["dns2"] | "", sizeof(profile.dns2));

        if (profile.useStaticIp &&
            (!profile.ip[0] || !profile.gateway[0] || !profile.subnet[0])) {
          sendJsonError(request, 400,
                        "Static IP requires IP, gateway and subnet");
          return;
        }
        if (!isValidIpOrEmpty(profile.ip) ||
            !isValidIpOrEmpty(profile.gateway) ||
            !isValidIpOrEmpty(profile.subnet) ||
            !isValidIpOrEmpty(profile.dns1) ||
            !isValidIpOrEmpty(profile.dns2)) {
          sendJsonError(request, 400, "Invalid IP address format");
          return;
        }

        const bool makePreferred = doc["makePreferred"] | false;
        if (!WiFiProfiles::upsertProfile(g_settings.wifi, profile,
                                         makePreferred)) {
          sendJsonError(
              request, 400,
              "Failed to save WiFi profile (limit reached or invalid SSID)");
          return;
        }

        if (!NVSManager::saveSettings(g_settings)) {
          Logger::logf(2, "WiFi profile save failed: %s", ssid);
          sendJsonError(request, 500, "Failed to save settings");
          return;
        }

        JsonDocument out;
        out["success"] = true;
        buildWiFiProfilesResponse(out);

        String json;
        serializeJson(out, json);
        Logger::logf(0, "WiFi profile saved: %s (%s%s)", ssid,
                     profile.enabled ? "enabled" : "disabled",
                     makePreferred ? ", preferred" : "");
        request->send(200, "application/json", json);
      });

  server.on(
      "^\\/api\\/wifi\\/connect$", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"error\":\"Invalid JSON\"}")) {
          LOG_E("WiFi: JSON parse error");
          Logger::logf(1, "WiFi connect rejected: invalid JSON");
          return;
        }

        const char *ssid = doc["ssid"];
        const bool hasPasswordField = !doc["password"].isNull();
        const char *password =
            hasPasswordField ? (doc["password"] | "") : nullptr;
        const bool saveProfile = doc["saveProfile"] | true;
        const bool makePreferred = doc["makePreferred"] | false;

        if (!ssid || strlen(ssid) == 0) {
          sendJsonError(request, 400, "SSID required", false);
          return;
        }

        LOG_I("WiFi: Connect request for SSID: %s", ssid);
        Logger::logf(0, "WiFi connect requested: %s%s", ssid,
                     saveProfile ? " (save profile)" : "");

        WiFiProfile profileToConnect{};
        profileToConnect.enabled = true;
        strlcpy(profileToConnect.ssid, ssid, sizeof(profileToConnect.ssid));
        strlcpy(profileToConnect.subnet, "255.255.255.0",
                sizeof(profileToConnect.subnet));

        WiFiProfile savedProfile{};
        const bool hasSavedProfile =
            WiFiProfiles::getProfileBySsid(g_settings.wifi, ssid, savedProfile);
        if (hasSavedProfile) {
          profileToConnect = savedProfile;
        }

        if (hasPasswordField && password && strlen(password) > 0) {
          strlcpy(profileToConnect.password, password,
                  sizeof(profileToConnect.password));
        } else if (!hasSavedProfile && (!password || strlen(password) == 0)) {
          profileToConnect.password[0] = '\0';
        }

        if (!doc["useStaticIp"].isNull()) {
          profileToConnect.useStaticIp = doc["useStaticIp"] | false;
        }
        if (!doc["ip"].isNull()) {
          strlcpy(profileToConnect.ip, doc["ip"] | "",
                  sizeof(profileToConnect.ip));
        }
        if (!doc["gateway"].isNull()) {
          strlcpy(profileToConnect.gateway, doc["gateway"] | "",
                  sizeof(profileToConnect.gateway));
        }
        if (!doc["subnet"].isNull()) {
          strlcpy(profileToConnect.subnet, doc["subnet"] | "255.255.255.0",
                  sizeof(profileToConnect.subnet));
        }
        if (!doc["dns1"].isNull()) {
          strlcpy(profileToConnect.dns1, doc["dns1"] | "",
                  sizeof(profileToConnect.dns1));
        }
        if (!doc["dns2"].isNull()) {
          strlcpy(profileToConnect.dns2, doc["dns2"] | "",
                  sizeof(profileToConnect.dns2));
        }

        if (profileToConnect.useStaticIp &&
            (!profileToConnect.ip[0] || !profileToConnect.gateway[0] ||
             !profileToConnect.subnet[0])) {
          sendJsonError(request, 400,
                        "Static IP requires IP, gateway and subnet", false);
          return;
        }
        if (!isValidIpOrEmpty(profileToConnect.ip) ||
            !isValidIpOrEmpty(profileToConnect.gateway) ||
            !isValidIpOrEmpty(profileToConnect.subnet) ||
            !isValidIpOrEmpty(profileToConnect.dns1) ||
            !isValidIpOrEmpty(profileToConnect.dns2)) {
          sendJsonError(request, 400, "Invalid IP address format", false);
          return;
        }

        if (saveProfile) {
          if (!WiFiProfiles::upsertProfile(g_settings.wifi, profileToConnect,
                                           makePreferred)) {
            sendJsonError(request, 400, "Failed to save WiFi profile", false);
            return;
          }
          WiFiProfiles::getProfileBySsid(g_settings.wifi, ssid, profileToConnect);
        }

        WiFiProfiles::syncLegacyFields(g_settings.wifi, &profileToConnect);
        g_settings.wifi.apMode = true;

        if (NVSManager::saveSettings(g_settings)) {
          LOG_I("WiFi: Settings saved, connecting to %s", ssid);
          WiFiProfile *profileCopy = new WiFiProfile(profileToConnect);
          if (profileCopy == nullptr) {
            LOG_E("WiFi: Failed to allocate connect task profile");
            Logger::logf(2, "WiFi connect allocation failed: %s", ssid);
            sendJsonError(request, 500, "Failed to schedule WiFi connect",
                          false);
            return;
          }

          BaseType_t taskCreated = xTaskCreate(
              beginWiFiConnectionTask, "wifi-connect", 4096, profileCopy, 1, nullptr);
          if (taskCreated != pdPASS) {
            delete profileCopy;
            LOG_E("WiFi: Failed to create connect task");
            Logger::logf(2, "WiFi connect task create failed: %s", ssid);
            sendJsonError(request, 500, "Failed to schedule WiFi connect",
                          false);
            return;
          }

          request->send(
              200, "application/json",
              "{\"status\":\"connecting\",\"message\":\"Connecting to WiFi, please wait...\"}");
        } else {
          LOG_E("WiFi: Failed to save settings to NVS");
          Logger::logf(2, "WiFi connect save failed: %s", ssid);
          sendJsonError(request, 500, "Failed to save settings", false);
        }
      });
}
