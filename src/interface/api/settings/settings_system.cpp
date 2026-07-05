#include "settings_modules.h"

#include <Preferences.h>
#include <esp_system.h>

#include "interface/webserver_shared.h"
#include "storage/logger.h"

void registerSystemSettingsApiRoutes(AsyncWebServer &server) {
  server.on(
      "/api/settings/demo", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"message\":\"Invalid JSON\"}")) {
          return;
        }

        const bool enabled = doc["enabled"] | false;
        g_settings.demoMode = enabled;

        LOG_I("Demo mode %s", enabled ? "ENABLED" : "DISABLED");
        Logger::logf(0, "Demo mode %s", enabled ? "enabled" : "disabled");

        Preferences prefs;
        prefs.begin("settings", false);
        prefs.putBool("demoMode", enabled);
        prefs.end();

        JsonDocument out;
        out["success"] = true;
        out["demoMode"] = enabled;
        String json;
        serializeJson(out, json);
        request->send(200, "application/json", json);
      });

  server.on("/api/settings/demo", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              char response[64];
              snprintf(response, sizeof(response), "{\"demoMode\":%s}",
                       g_settings.demoMode ? "true" : "false");
              request->send(200, "application/json", response);
            });

  server.on("/api/reboot/status", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              JsonDocument doc;
              const uint32_t totalReboots = g_rebootTracker.totalReboots;
              const uint32_t wdtReboots = g_rebootTracker.wdtReboots;
              const uint32_t crashReboots = g_rebootTracker.crashReboots;
              const uint32_t userReboots = g_rebootTracker.userReboots;
              const uint32_t otherReboots =
                  totalReboots > (wdtReboots + crashReboots + userReboots)
                      ? totalReboots -
                            (wdtReboots + crashReboots + userReboots)
                      : 0;
              const uint8_t lastReason = g_rebootTracker.lastReason;
              const bool lastWasWdt =
                  lastReason == ESP_RST_WDT || lastReason == ESP_RST_TASK_WDT ||
                  lastReason == ESP_RST_INT_WDT;
              const bool lastWasCrash = lastReason == ESP_RST_PANIC;
              const bool lastWasUser =
                  lastReason == ESP_RST_SW || lastReason == ESP_RST_EXT;

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
              doc["lastReasonKind"] =
                  lastWasWdt ? "wdt"
                             : lastWasCrash ? "crash"
                                            : lastWasUser ? "user" : "other";

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    LOG_W("Reboot requested via API");
    Logger::logf(1, "System reboot requested via API");
    sendJsonSuccess(request, 200, "Rebooting...");

    delay(500);
    ESP.restart();
  });
}
