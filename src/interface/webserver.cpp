/**
 * Smart-Column S3 - Веб-сервер
 *
 * HTTP server + WebSocket для Web UI
 */

#include "webserver.h"

#include "api/api_routes.h"
#include "web_live.h"
#include "webserver_shared.h"

#include "fs_compat.h"

#include <WiFi.h>

#include "storage/logger.h"

namespace {

AsyncWebServer server(WEB_SERVER_PORT);
AsyncWebSocket ws("/ws");

} // namespace

namespace WebServer {

void init() {
  LOG_I("WebServer: Initializing...");

  applySecuritySettings();

  server.addMiddleware([](AsyncWebServerRequest *request,
                          ArMiddlewareNext next) {
    if (!handleSecurityGate(request)) {
      return;
    }
    next();
  });

  ws.onEvent([](AsyncWebSocket *, AsyncWebSocketClient *client,
                AwsEventType type, void *, uint8_t *, size_t) {
    if (type == WS_EVT_CONNECT) {
      LOG_I("WebSocket: Client connected #%u", client->id());
    } else if (type == WS_EVT_DISCONNECT) {
      LOG_I("WebSocket: Client disconnected #%u", client->id());
    } else if (type == WS_EVT_DATA) {
      LOG_D("WebSocket: Data received");
    }
  });

  server.addHandler(&ws);
  WebServerLive::bindWebSocket(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!hasConfiguredWiFi() && WiFi.status() != WL_CONNECTED) {
      request->redirect("/wifi.html");
      return;
    }

    if (LittleFS.exists("/index.html.gz")) {
      AsyncWebServerResponse *response =
          request->beginResponse(LittleFS, "/index.html.gz", "text/html");
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }

    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!hasConfiguredWiFi()) {
      request->redirect("/wifi.html");
    } else {
      request->send(204);
    }
  });

  server.on("/hotspot-detect.html", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              request->redirect("/wifi.html");
            });

  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!hasConfiguredWiFi()) {
      request->redirect("/wifi.html");
    } else {
      request->send(200, "text/plain", "Microsoft Connect Test");
    }
  });

  server.serveStatic("/", LittleFS, "/")
      .setDefaultFile("index.html")
      .setTemplateProcessor(nullptr);

  registerStatusApiRoutes(server);
  registerChartsApiRoutes(server);
  registerHealthApiRoutes(server);
  registerLogsApiRoutes(server);

  registerHistoryApiRoutes(server);
  registerProcessApiRoutes(server);
  registerSafetyApiRoutes(server);
  registerSettingsApiRoutes(server);

  registerCalibrationApiRoutes(server);
  registerPumpApiRoutes(server);
  registerTestingApiRoutes(server);
  registerEnergyApiRoutes(server);
  registerWifiApiRoutes(server);
  registerOtaApiRoutes(server);
  registerProfilesApiRoutes(server);

  server.onNotFound(
      [](AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not Found"); });

  server.begin();
  LOG_I("WebServer: Started on port %d", WEB_SERVER_PORT);
}

void broadcastState(const SystemState &state) {
  WebServerLive::broadcastState(state);
}

void broadcastEvent(const char *event, const char *message) {
  WebServerLive::broadcastEvent(event, message);
}

} // namespace WebServer
