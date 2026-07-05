#include "api_routes.h"

#include "../../live_chart_history.h"
#include "control/safety.h"
#include "interface/webserver_shared.h"

void registerChartsApiRoutes(AsyncWebServer &server) {
  server.on("/api/charts/live", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["success"] = true;

    JsonObject meta = doc["meta"].to<JsonObject>();
    JsonObject temperatureMeta = meta["temperatures"].to<JsonObject>();
    for (uint8_t i = 0; i < TEMP_COUNT; ++i) {
      JsonObject channel =
          temperatureMeta[getTempSensorRoleKey(i)].to<JsonObject>();
      channel["label"] = getTempSensorLabel(i);
      channel["installed"] =
          Safety::isTempSensorInstalled(g_settings.equipment, i);
      appendTempSensorMeta(channel, i);
    }

    JsonObject powerMeta = meta["power"].to<JsonObject>();
    powerMeta["available"] = g_state.health.pzemOk;

    LiveChartHistory::fillJson(doc.as<JsonObject>());

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/charts/live/reset", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              LiveChartHistory::clear();
              request->send(
                  200, "application/json",
                  "{\"success\":true,\"message\":\"Live chart history cleared\"}");
            });
}
