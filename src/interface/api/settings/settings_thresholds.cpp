#include "settings_modules.h"

#include <string.h>

#include "interface/security.h"
#include "interface/webserver_shared.h"
#include "storage/logger.h"
#include "storage/nvs_manager.h"

void registerThresholdSettingsRoutes(AsyncWebServer &server) {
  server.on("/api/settings/safety", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              JsonDocument doc;
              doc["pressureMaxMmHg"] = g_settings.safety.pressureMaxMmHg;
              doc["tsaMaxC"] = g_settings.safety.tsaMaxC;
              doc["waterOutMaxC"] = g_settings.safety.waterOutMaxC;
              doc["waterOutRiseRateCMin"] =
                  g_settings.safety.waterOutRiseRateCMin;
              doc["pressureRiseRateMmHgMin"] =
                  g_settings.safety.pressureRiseRateMmHgMin;

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "/api/settings/safety", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        if (!doc["pressureMaxMmHg"].isNull()) {
          g_settings.safety.pressureMaxMmHg = clampFloatRange(
              doc["pressureMaxMmHg"].as<float>(), 5.0f, 200.0f);
        }
        if (!doc["tsaMaxC"].isNull()) {
          g_settings.safety.tsaMaxC =
              clampFloatRange(doc["tsaMaxC"].as<float>(), 35.0f, 120.0f);
        }
        if (!doc["waterOutMaxC"].isNull()) {
          g_settings.safety.waterOutMaxC = clampFloatRange(
              doc["waterOutMaxC"].as<float>(), 30.0f, 120.0f);
        }
        if (!doc["waterOutRiseRateCMin"].isNull()) {
          g_settings.safety.waterOutRiseRateCMin = clampFloatRange(
              doc["waterOutRiseRateCMin"].as<float>(), 0.5f, 60.0f);
        }
        if (!doc["pressureRiseRateMmHgMin"].isNull()) {
          g_settings.safety.pressureRiseRateMmHgMin = clampFloatRange(
              doc["pressureRiseRateMmHgMin"].as<float>(), 1.0f, 200.0f);
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
              doc["passwordConfigured"] =
                  (g_settings.security.password[0] != '\0');

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "/api/settings/security", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
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
        const char *username = hasUsernameField
                                   ? (doc["username"] | "")
                                   : g_settings.security.username;
        const char *password =
            hasPasswordField ? (doc["password"] | "") : nullptr;

        if (authEnabled && (!username || strlen(username) == 0)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Username required\"}");
          return;
        }

        const bool hasStoredPassword =
            (g_settings.security.password[0] != '\0');
        if (authEnabled &&
            (!hasStoredPassword && (!password || strlen(password) == 0))) {
          request->send(
              400, "application/json",
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
        Logger::logf(
            0, "Security settings updated: auth=%s, rateLimit=%s, user=%s",
            authEnabled ? "enabled" : "disabled",
            rateLimitEnabled ? "enabled" : "disabled",
            g_settings.security.username);
        request->send(200, "application/json", "{\"success\":true}");
      });
}
