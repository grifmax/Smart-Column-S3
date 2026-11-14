#include "api_routes.h"

#include "control/control_command_queue.h"
#include "control/safety.h"
#include "interface/cloud_tunnel.h"
#include "interface/webserver_shared.h"
#include "storage/nvs_manager.h"

namespace {

void sendSafetyCommandResponse(AsyncWebServerRequest *request,
                               ControlCommandQueue::EnqueueResult result,
                               uint32_t commandId,
                               ControlCommandQueue::Status status) {
  const bool accepted = result == ControlCommandQueue::EnqueueResult::Accepted ||
                        result == ControlCommandQueue::EnqueueResult::Duplicate;
  const bool queued = status == ControlCommandQueue::Status::Queued;
  const bool applied = status == ControlCommandQueue::Status::Applied;
  JsonDocument doc;
  doc["success"] = accepted && applied;
  doc["accepted"] = accepted;
  doc["applied"] = applied;
  doc["queued"] = queued;
  doc["commandId"] = commandId;
  doc["status"] = ControlCommandQueue::statusToken(status);
  String response;
  serializeJson(doc, response);
  const int httpStatus = (accepted && queued) ? 202
                         : (accepted && applied) ? 200
                         : result == ControlCommandQueue::EnqueueResult::Full
                             ? 429
                             : result == ControlCommandQueue::EnqueueResult::NotInitialized
                                 ? 503
                                 : 409;
  request->send(httpStatus, "application/json", response);
}

} // namespace

void registerSafetyApiRoutes(AsyncWebServer &server) {
  server.on("/api/safety/ack", HTTP_POST, [](AsyncWebServerRequest *request) {
    const long rawId = request->hasParam("commandId")
                           ? request->getParam("commandId")->value().toInt()
                           : 0;
    uint32_t commandId = rawId > 0 ? static_cast<uint32_t>(rawId) : 0;
    ControlCommandQueue::Status status =
        ControlCommandQueue::Status::Unknown;
    const auto result = ControlCommandQueue::enqueueAction(
        ControlCommandQueue::Type::AcknowledgeSafety,
        ControlCommandQueue::Source::Api, 0, 0.0f, commandId,
        ControlCommandQueue::Reason::SafetyRequest, &commandId, &status);
    sendSafetyCommandResponse(request, result, commandId, status);
  });

  server.on("/api/safety/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    const long rawId = request->hasParam("commandId")
                           ? request->getParam("commandId")->value().toInt()
                           : 0;
    uint32_t commandId = rawId > 0 ? static_cast<uint32_t>(rawId) : 0;
    ControlCommandQueue::Status status =
        ControlCommandQueue::Status::Unknown;
    const auto result = ControlCommandQueue::enqueueAction(
        ControlCommandQueue::Type::ResetSafety,
        ControlCommandQueue::Source::Api, 0, 0.0f, commandId,
        ControlCommandQueue::Reason::SafetyRequest, &commandId, &status);
    sendSafetyCommandResponse(request, result, commandId, status);
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
