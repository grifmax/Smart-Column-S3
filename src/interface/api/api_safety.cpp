#include "api_routes.h"

#include "control/safety.h"
#include "control/v2/status_adapter.h"
#include "interface/cloud_tunnel.h"
#include "interface/webserver_shared.h"
#include "storage/nvs_manager.h"

void registerSafetyApiRoutes(AsyncWebServer &server) {
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
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        uint32_t ttl = 600;
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}", true)) {
          return;
        }
        if (!doc.isNull()) {
          ttl = doc["ttlSeconds"] | 600;
          if (ttl < 60) ttl = 60;
          if (ttl > 3600) ttl = 3600;
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

  server.on(
      "/api/cloud/config", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        bool enabled = doc["enabled"] | g_settings.cloud.enabled;
        const char *url = doc["tunnelUrl"] | g_settings.cloud.tunnelUrl;

        g_settings.cloud.enabled = enabled;
        strlcpy(g_settings.cloud.tunnelUrl, url,
                sizeof(g_settings.cloud.tunnelUrl));

        NVSManager::saveSettings(g_settings);

        request->send(200, "application/json", "{\"success\":true}");
      });
}
