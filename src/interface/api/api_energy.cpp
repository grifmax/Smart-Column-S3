#include "api_routes.h"

#include <math.h>

#include "interface/webserver_shared.h"

void registerEnergyApiRoutes(AsyncWebServer &server) {
  server.on("/api/energy", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;

    doc["count"] = g_energyHistory.count;
    doc["maxPoints"] = EnergyHistory::MAX_POINTS;
    doc["lastUpdate"] = g_energyHistory.lastUpdate;

    JsonArray dataArray = doc["data"].to<JsonArray>();
    for (uint16_t i = 0; i < g_energyHistory.count; i++) {
      uint16_t index;
      if (g_energyHistory.count < EnergyHistory::MAX_POINTS) {
        index = i;
      } else {
        index = (g_energyHistory.writeIndex + i) % EnergyHistory::MAX_POINTS;
      }

      const EnergyDataPoint &point = g_energyHistory.points[index];

      JsonObject obj = dataArray.add<JsonObject>();
      obj["t"] = point.timestamp;
      obj["p"] = round(point.power * 10) / 10;
      obj["e"] = round(point.energy * 1000) / 1000;
      obj["v"] = round(point.voltage * 10) / 10;
      obj["i"] = round(point.current * 100) / 100;
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
}
