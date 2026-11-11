#include "api_routes.h"

#include "interface/webserver_shared.h"
#include "storage/nvs_manager.h"

namespace {

void sendAiConfig(AsyncWebServerRequest *request, int code = 200) {
  JsonDocument doc;
  doc["success"] = code == 200;
  doc["configured"] = g_settings.ai.configured;
  doc["baseUrl"] = g_settings.ai.baseUrl;
  doc["apiKey"] = g_settings.ai.apiKey;
  doc["model"] = g_settings.ai.model;
  doc["temperature"] = g_settings.ai.temperature;
  doc["maxTokens"] = g_settings.ai.maxTokens;
  String json;
  serializeJson(doc, json);
  request->send(code, "application/json", json);
}

void copyBounded(char *target, size_t size, const String &value) {
  if (size == 0) return;
  const size_t maxLen = size - 1;
  const size_t len = value.length() < maxLen ? value.length() : maxLen;
  memcpy(target, value.c_str(), len);
  target[len] = '\0';
}

} // namespace

void registerAiApiRoutes(AsyncWebServer &server) {
  server.on("/api/ai/config", HTTP_GET,
            [](AsyncWebServerRequest *request) { sendAiConfig(request); });

  server.on(
      "/api/ai/config", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        if (!doc["baseUrl"].isNull()) {
          const String baseUrl = doc["baseUrl"].as<String>();
          if (baseUrl.length() > 0 &&
              !baseUrl.startsWith("http://") && !baseUrl.startsWith("https://")) {
            request->send(
                400, "application/json",
                "{\"success\":false,\"error\":\"baseUrl must start with http:// or https://\"}");
            return;
          }
          copyBounded(g_settings.ai.baseUrl, sizeof(g_settings.ai.baseUrl), baseUrl);
        }
        if (!doc["apiKey"].isNull()) {
          copyBounded(g_settings.ai.apiKey, sizeof(g_settings.ai.apiKey),
                      doc["apiKey"].as<String>());
        }
        if (!doc["model"].isNull()) {
          copyBounded(g_settings.ai.model, sizeof(g_settings.ai.model),
                      doc["model"].as<String>());
        }
        if (!doc["temperature"].isNull()) {
          float temperature = doc["temperature"].as<float>();
          if (temperature < 0.0f) temperature = 0.0f;
          if (temperature > 2.0f) temperature = 2.0f;
          g_settings.ai.temperature = temperature;
        }
        if (!doc["maxTokens"].isNull()) {
          uint32_t tokens = doc["maxTokens"].as<uint32_t>();
          if (tokens < 256) tokens = 256;
          if (tokens > 32000) tokens = 32000;
          g_settings.ai.maxTokens = static_cast<uint16_t>(tokens);
        }

        g_settings.ai.configured =
            g_settings.ai.baseUrl[0] != '\0' && g_settings.ai.model[0] != '\0';
        NVSManager::saveSettings(g_settings);
        sendAiConfig(request);
      });
}
