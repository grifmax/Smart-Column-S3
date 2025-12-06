/**
 * Smart-Column S3 - Веб-сервер
 *
 * HTTP server + WebSocket для Web UI
 */

#include "webserver.h"
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <ArduinoJson.h>

static AsyncWebServer server(WEB_SERVER_PORT);
static AsyncWebSocket ws("/ws");

namespace WebServer {

void init() {
    LOG_I("WebServer: Initializing...");

    // WebSocket обработчик
    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                  AwsEventType type, void *arg, uint8_t *data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            LOG_I("WebSocket: Client connected #%u", client->id());
        } else if (type == WS_EVT_DISCONNECT) {
            LOG_I("WebSocket: Client disconnected #%u", client->id());
        } else if (type == WS_EVT_DATA) {
            // Обработка команд от клиента
            LOG_D("WebSocket: Data received");
        }
    });

    server.addHandler(&ws);

    // Статические файлы (Web UI)
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    // API endpoints
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        // TODO: Отправить состояние системы
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/start", HTTP_POST, [](AsyncWebServerRequest *request) {
        // TODO: Запуск процесса
        request->send(200);
    });

    server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        // TODO: Остановка процесса
        request->send(200);
    });

    // 404
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not Found");
    });

    server.begin();
    LOG_I("WebServer: Started on port %d", WEB_SERVER_PORT);
}

void broadcastState(const SystemState& state) {
    if (ws.count() == 0) return;

    // Сформировать JSON со состоянием
    StaticJsonDocument<1024> doc;

    doc["mode"] = static_cast<int>(state.mode);
    doc["phase"] = static_cast<int>(state.rectPhase);
    doc["t_cube"] = state.temps.cube;
    doc["t_column"] = state.temps.columnTop;
    doc["power"] = state.power.power;
    doc["speed"] = state.pump.speedMlPerHour;
    doc["volume"] = state.pump.totalVolumeMl;

    // Статистика памяти
    JsonObject mem = doc.createNestedObject("memory");
    mem["heap_free"] = ESP.getFreeHeap();
    mem["heap_total"] = ESP.getHeapSize();
    mem["heap_used_pct"] = (ESP.getHeapSize() - ESP.getFreeHeap()) * 100 / ESP.getHeapSize();
    mem["psram_free"] = ESP.getFreePsram();
    mem["psram_total"] = ESP.getPsramSize();
    mem["flash_used"] = ESP.getSketchSize();
    mem["flash_total"] = ESP.getFlashChipSize();
    mem["flash_used_pct"] = ESP.getSketchSize() * 100 / ESP.getFlashChipSize();

    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void sendEvent(const char* event, const char* message) {
    StaticJsonDocument<256> doc;
    doc["type"] = "event";
    doc["event"] = event;
    doc["message"] = message;

    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

} // namespace WebServer
