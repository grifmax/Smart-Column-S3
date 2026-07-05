#include "settings_modules.h"

#include <Preferences.h>
#include <WiFi.h>

#include "interface/mqtt.h"
#include "interface/webserver_shared.h"
#include "storage/logger.h"
#include "storage/nvs_manager.h"

void registerMqttSettingsApiRoutes(AsyncWebServer &server) {
  server.on("/api/settings/mqtt", HTTP_GET,
            [](AsyncWebServerRequest *request) {
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

  server.on(
      "/api/settings/mqtt", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        const bool enabled = doc["enabled"] | g_settings.mqtt.enabled;
        const char *serverValue = !doc["server"].isNull()
                                      ? (doc["server"] | "")
                                      : g_settings.mqtt.server;
        uint16_t port = !doc["port"].isNull()
                            ? static_cast<uint16_t>(doc["port"] |
                                                    g_settings.mqtt.port)
                            : g_settings.mqtt.port;
        const char *username = !doc["username"].isNull()
                                   ? (doc["username"] | "")
                                   : g_settings.mqtt.username;
        const char *password = !doc["password"].isNull()
                                   ? (doc["password"] | "")
                                   : g_settings.mqtt.password;
        const char *baseTopic = !doc["baseTopic"].isNull()
                                    ? (doc["baseTopic"] | "")
                                    : g_settings.mqtt.baseTopic;
        const bool discovery = !doc["discovery"].isNull()
                                   ? static_cast<bool>(doc["discovery"])
                                   : g_settings.mqtt.discovery;
        uint32_t publishInterval = !doc["publishInterval"].isNull()
                                       ? static_cast<uint32_t>(
                                             doc["publishInterval"] |
                                             g_settings.mqtt.publishInterval)
                                       : g_settings.mqtt.publishInterval;

        if (enabled && (!serverValue || serverValue[0] == '\0')) {
          Logger::logf(1,
                       "MQTT settings rejected: server is required when enabled");
          sendJsonError(request, 400, "MQTT server is required when enabled");
          return;
        }
        if (port == 0) {
          port = 1883;
        }
        if (!baseTopic || baseTopic[0] == '\0') {
          baseTopic = "smart-column";
        }
        if (publishInterval < 1000) {
          publishInterval = 1000;
        }
        if (publishInterval > 60000) {
          publishInterval = 60000;
        }

        g_settings.mqtt.enabled = enabled;
        strlcpy(g_settings.mqtt.server, serverValue,
                sizeof(g_settings.mqtt.server));
        g_settings.mqtt.port = port;
        strlcpy(g_settings.mqtt.username, username,
                sizeof(g_settings.mqtt.username));
        strlcpy(g_settings.mqtt.password, password,
                sizeof(g_settings.mqtt.password));
        strlcpy(g_settings.mqtt.baseTopic, baseTopic,
                sizeof(g_settings.mqtt.baseTopic));
        g_settings.mqtt.publishInterval = publishInterval;
        g_settings.mqtt.discovery = discovery;

        if (!NVSManager::saveSettings(g_settings)) {
          Logger::logf(2, "MQTT settings save failed");
          sendJsonError(request, 500, "Failed to save settings");
          return;
        }

        sendJsonSuccess(request);
        Logger::logf(
            0,
            "MQTT settings updated: %s, server=%s:%u, topic=%s, interval=%lums",
            g_settings.mqtt.enabled ? "enabled" : "disabled",
            g_settings.mqtt.server[0] ? g_settings.mqtt.server : "-",
            g_settings.mqtt.port, g_settings.mqtt.baseTopic,
            static_cast<unsigned long>(g_settings.mqtt.publishInterval));

        MQTT::disconnect();
        if (g_settings.mqtt.enabled && g_settings.mqtt.server[0] != '\0') {
          MQTT::setBaseTopic(g_settings.mqtt.baseTopic);
          MQTT::init(g_settings.mqtt.server, g_settings.mqtt.port,
                     g_settings.mqtt.username[0]
                         ? g_settings.mqtt.username
                         : nullptr,
                     g_settings.mqtt.password[0]
                         ? g_settings.mqtt.password
                         : nullptr);
          if (WiFi.status() == WL_CONNECTED) {
            MQTT::handle();
          }
        }
      });

  server.on(
      "/api/settings/mqtt/test", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (!g_settings.mqtt.enabled) {
          Logger::logf(1, "MQTT test rejected: MQTT is disabled");
          sendJsonError(request, 400, "MQTT disabled");
          return;
        }
        if (g_settings.mqtt.server[0] == '\0') {
          Logger::logf(1, "MQTT test rejected: broker is not configured");
          sendJsonError(request, 400, "MQTT server is not configured");
          return;
        }
        if (WiFi.status() != WL_CONNECTED) {
          Logger::logf(1, "MQTT test rejected: WiFi STA is not connected");
          sendJsonError(request, 503, "WiFi STA not connected");
          return;
        }

        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}", true)) {
          return;
        }
        const char *message =
            doc["message"] | "Smart-Column S3: MQTT test from Web UI";

        if (!MQTT::isConnected()) {
          MQTT::setBaseTopic(g_settings.mqtt.baseTopic);
          MQTT::init(g_settings.mqtt.server, g_settings.mqtt.port,
                     g_settings.mqtt.username[0]
                         ? g_settings.mqtt.username
                         : nullptr,
                     g_settings.mqtt.password[0]
                         ? g_settings.mqtt.password
                         : nullptr);
          for (uint8_t i = 0; i < 20 && !MQTT::isConnected(); ++i) {
            MQTT::handle();
            delay(50);
          }
        }

        if (!MQTT::isConnected()) {
          Logger::logf(1, "MQTT test failed: broker unavailable");
          sendJsonError(request, 503, "MQTT broker unavailable");
          return;
        }

        MQTT::publishNotification("MQTT test", message, "info");
        Logger::logf(0, "MQTT test notification sent");
        sendJsonSuccess(request);
      });
}
