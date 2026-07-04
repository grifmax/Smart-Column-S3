#include "api_routes.h"

#include "storage/logger.h"

void registerLogsRoutes(AsyncWebServer &server) {
  server.on("/api/logs/events", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint16_t limit = 100;
    uint32_t since = 0;

    if (request->hasParam("limit")) {
      limit = static_cast<uint16_t>(request->getParam("limit")->value().toInt());
      if (limit > 200) {
        limit = 200;
      }
    }

    if (request->hasParam("since")) {
      since = static_cast<uint32_t>(request->getParam("since")->value().toInt());
    }

    request->send(200, "application/json",
                  Logger::getRecentEventsJson(limit, since));
  });

  server.on("/api/logs/events/clear", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              Logger::clearRecentEvents();
              request->send(200, "application/json", "{\"success\":true}");
            });

  server.on("/api/export", HTTP_GET, [](AsyncWebServerRequest *request) {
    const char *currentLogFile = Logger::getCurrentLogFile();
    String body;
    String filename = "system-events.csv";

    if (currentLogFile && currentLogFile[0]) {
      body = Logger::readLog(currentLogFile);

      const String currentName = currentLogFile;
      const int slashIndex = currentName.lastIndexOf('/');
      if (slashIndex >= 0 && slashIndex + 1 < currentName.length()) {
        filename = currentName.substring(slashIndex + 1);
      } else {
        filename = currentName;
      }
    } else {
      body = Logger::exportRecentEventsCsv();
    }

    AsyncWebServerResponse *response =
        request->beginResponse(200, "text/csv; charset=utf-8", body);
    response->addHeader("Content-Disposition",
                        "attachment; filename=\"" + filename + "\"");
    request->send(response);
  });
}
