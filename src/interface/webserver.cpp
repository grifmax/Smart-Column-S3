/**
 * Smart-Column S3 - Веб-сервер
 *
 * HTTP server + WebSocket для Web UI
 */

#include "webserver.h"
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <ArduinoJson.h>
#include "storage/nvs_manager.h"
#include "drivers/sensors.h"

// Внешние переменные из main.cpp
extern SystemState g_state;
extern Settings g_settings;

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

    // GET /api/health - получить здоровье системы
    server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<512> doc;

        // Датчики температуры
        JsonObject temps = doc.createNestedObject("temperatures");
        temps["ok"] = g_state.health.tempSensorsOk;
        temps["total"] = g_state.health.tempSensorsTotal;

        // Другие датчики
        JsonObject sensors = doc.createNestedObject("sensors");
        sensors["bmp280"] = g_state.health.bmp280Ok;
        sensors["ads1115"] = g_state.health.ads1115Ok;
        sensors["pzem"] = g_state.health.pzemOk;

        // WiFi
        JsonObject wifi = doc.createNestedObject("wifi");
        wifi["connected"] = g_state.health.wifiConnected;
        wifi["rssi"] = g_state.health.wifiRSSI;

        // Система
        JsonObject system = doc.createNestedObject("system");
        system["uptime"] = g_state.health.uptime;
        system["freeHeap"] = g_state.health.freeHeap;
        system["cpuTemp"] = g_state.health.cpuTemp;

        // Ошибки
        JsonObject errors = doc.createNestedObject("errors");
        errors["pzemSpikes"] = g_state.health.pzemSpikeCount;
        errors["tempErrors"] = g_state.health.tempReadErrors;

        // Общая оценка
        doc["overallHealth"] = g_state.health.overallHealth;
        doc["lastUpdate"] = g_state.health.lastUpdate;

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server.on("/api/start", HTTP_POST, [](AsyncWebServerRequest *request) {
        // TODO: Запуск процесса
        request->send(200);
    });

    server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        // TODO: Остановка процесса
        request->send(200);
    });

    // ==========================================================================
    // КАЛИБРОВКА
    // ==========================================================================

    // GET /api/calibration - получить все данные калибровки
    server.on("/api/calibration", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<1024> doc;

        // Насос
        JsonObject pump = doc.createNestedObject("pump");
        pump["mlPerRev"] = g_settings.pumpCal.mlPerRevolution;
        pump["stepsPerRev"] = g_settings.pumpCal.stepsPerRevolution;
        pump["microsteps"] = g_settings.pumpCal.microsteps;

        // Термометры
        JsonArray temps = doc.createNestedArray("temperatures");
        for (uint8_t i = 0; i < TEMP_COUNT; i++) {
            JsonObject t = temps.createNestedObject();
            t["index"] = i;
            t["offset"] = g_settings.tempCal.offsets[i];

            // Адрес датчика (hex string)
            char addrStr[24];
            snprintf(addrStr, sizeof(addrStr), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                g_settings.tempCal.addresses[i][0], g_settings.tempCal.addresses[i][1],
                g_settings.tempCal.addresses[i][2], g_settings.tempCal.addresses[i][3],
                g_settings.tempCal.addresses[i][4], g_settings.tempCal.addresses[i][5],
                g_settings.tempCal.addresses[i][6], g_settings.tempCal.addresses[i][7]);
            t["address"] = addrStr;

            // Текущие показания
            float currentTemp = 0;
            switch(i) {
                case TEMP_CUBE: currentTemp = g_state.temps.cube; break;
                case TEMP_COLUMN_BOTTOM: currentTemp = g_state.temps.columnBottom; break;
                case TEMP_COLUMN_TOP: currentTemp = g_state.temps.columnTop; break;
                case TEMP_REFLUX: currentTemp = g_state.temps.reflux; break;
                case TEMP_TSA: currentTemp = g_state.temps.tsa; break;
                case TEMP_WATER_IN: currentTemp = g_state.temps.waterIn; break;
                case TEMP_WATER_OUT: currentTemp = g_state.temps.waterOut; break;
            }
            t["current"] = currentTemp;
            t["valid"] = g_state.temps.valid[i];
        }

        // Ареометр (гидрометр)
        JsonObject hydro = doc.createNestedObject("hydrometer");
        hydro["pointCount"] = g_settings.hydroCal.pointCount;
        JsonArray abvPoints = hydro.createNestedArray("abvPoints");
        JsonArray pressurePoints = hydro.createNestedArray("pressurePoints");
        for (uint8_t i = 0; i < g_settings.hydroCal.pointCount; i++) {
            abvPoints.add(g_settings.hydroCal.abvPoints[i]);
            pressurePoints.add(g_settings.hydroCal.pressurePoints[i]);
        }
        // Текущие показания
        hydro["currentPressure"] = g_state.hydrometer.density; // TODO: использовать реальное давление
        hydro["currentABV"] = g_state.hydrometer.abv;
        hydro["valid"] = g_state.hydrometer.valid;

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // POST /api/calibration/pump - калибровка насоса
    server.on("/api/calibration/pump", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<256> doc;
            DeserializationError error = deserializeJson(doc, data, len);

            if (error) {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            // Метод 1: Прямая калибровка (мл на оборот)
            if (doc.containsKey("mlPerRev")) {
                g_settings.pumpCal.mlPerRevolution = doc["mlPerRev"].as<float>();
                NVSManager::saveSettings(g_settings);
                LOG_I("Pump calibrated: %.3f ml/rev", g_settings.pumpCal.mlPerRevolution);
                request->send(200, "application/json", "{\"status\":\"ok\",\"method\":\"direct\"}");
                return;
            }

            // Метод 2: Калибровка по известному объёму
            if (doc.containsKey("knownVolume") && doc.containsKey("steps")) {
                float knownVolume = doc["knownVolume"].as<float>();  // мл
                uint32_t steps = doc["steps"].as<uint32_t>();        // шагов выполнено

                uint16_t stepsPerRev = g_settings.pumpCal.stepsPerRevolution * g_settings.pumpCal.microsteps;
                float revolutions = (float)steps / stepsPerRev;

                if (revolutions > 0) {
                    g_settings.pumpCal.mlPerRevolution = knownVolume / revolutions;
                    NVSManager::saveSettings(g_settings);

                    LOG_I("Pump calibrated: %.3f ml/rev (from %.1f ml in %u steps)",
                        g_settings.pumpCal.mlPerRevolution, knownVolume, steps);

                    StaticJsonDocument<128> resp;
                    resp["status"] = "ok";
                    resp["method"] = "measured";
                    resp["mlPerRev"] = g_settings.pumpCal.mlPerRevolution;

                    String json;
                    serializeJson(resp, json);
                    request->send(200, "application/json", json);
                } else {
                    request->send(400, "application/json", "{\"error\":\"Invalid steps\"}");
                }
                return;
            }

            request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
        }
    );

    // POST /api/calibration/temp - калибровка термометров
    server.on("/api/calibration/temp", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, data, len);

            if (error) {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            uint8_t sensorIndex = doc["index"].as<uint8_t>();

            if (sensorIndex >= TEMP_COUNT) {
                request->send(400, "application/json", "{\"error\":\"Invalid sensor index\"}");
                return;
            }

            // Метод 1: Прямое смещение
            if (doc.containsKey("offset")) {
                g_settings.tempCal.offsets[sensorIndex] = doc["offset"].as<float>();

                // Применить калибровку к драйверу
                Sensors::applyCalibration(g_settings.tempCal);
                NVSManager::saveSettings(g_settings);

                LOG_I("Temp[%d] calibrated: offset = %.2f°C",
                    sensorIndex, g_settings.tempCal.offsets[sensorIndex]);

                request->send(200, "application/json", "{\"status\":\"ok\",\"method\":\"offset\"}");
                return;
            }

            // Метод 2: Калибровка по эталону
            if (doc.containsKey("reference")) {
                float reference = doc["reference"].as<float>();  // Эталонная температура

                // Прочитать текущее значение
                float currentTemp = 0;
                switch(sensorIndex) {
                    case TEMP_CUBE: currentTemp = g_state.temps.cube; break;
                    case TEMP_COLUMN_BOTTOM: currentTemp = g_state.temps.columnBottom; break;
                    case TEMP_COLUMN_TOP: currentTemp = g_state.temps.columnTop; break;
                    case TEMP_REFLUX: currentTemp = g_state.temps.reflux; break;
                    case TEMP_TSA: currentTemp = g_state.temps.tsa; break;
                    case TEMP_WATER_IN: currentTemp = g_state.temps.waterIn; break;
                    case TEMP_WATER_OUT: currentTemp = g_state.temps.waterOut; break;
                }

                // Вычислить смещение (без учёта старого смещения)
                float rawTemp = currentTemp - g_settings.tempCal.offsets[sensorIndex];
                g_settings.tempCal.offsets[sensorIndex] = reference - rawTemp;

                Sensors::applyCalibration(g_settings.tempCal);
                NVSManager::saveSettings(g_settings);

                LOG_I("Temp[%d] calibrated to %.2f°C: offset = %.2f°C",
                    sensorIndex, reference, g_settings.tempCal.offsets[sensorIndex]);

                StaticJsonDocument<128> resp;
                resp["status"] = "ok";
                resp["method"] = "reference";
                resp["offset"] = g_settings.tempCal.offsets[sensorIndex];

                String json;
                serializeJson(resp, json);
                request->send(200, "application/json", json);
                return;
            }

            request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
        }
    );

    // POST /api/calibration/hydrometer - калибровка ареометра
    server.on("/api/calibration/hydrometer", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, data, len);

            if (error) {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            // Проверка наличия массивов калибровочных точек
            if (!doc.containsKey("abvPoints") || !doc.containsKey("pressurePoints")) {
                request->send(400, "application/json", "{\"error\":\"Missing abvPoints or pressurePoints\"}");
                return;
            }

            JsonArray abvArray = doc["abvPoints"].as<JsonArray>();
            JsonArray pressureArray = doc["pressurePoints"].as<JsonArray>();

            if (abvArray.size() != pressureArray.size() || abvArray.size() > 5) {
                request->send(400, "application/json", "{\"error\":\"Invalid point count (max 5, must match)\"}");
                return;
            }

            // Сохранение калибровочных точек
            g_settings.hydroCal.pointCount = abvArray.size();
            for (uint8_t i = 0; i < g_settings.hydroCal.pointCount; i++) {
                g_settings.hydroCal.abvPoints[i] = abvArray[i].as<float>();
                g_settings.hydroCal.pressurePoints[i] = pressureArray[i].as<float>();
            }

            // Сохранить в NVS
            NVSManager::saveSettings(g_settings);

            StaticJsonDocument<128> resp;
            resp["status"] = "ok";
            resp["pointCount"] = g_settings.hydroCal.pointCount;

            String json;
            serializeJson(resp, json);
            request->send(200, "application/json", json);
        }
    );

    // GET /api/calibration/scan - сканирование DS18B20
    server.on("/api/calibration/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        uint8_t addresses[TEMP_COUNT][8];
        uint8_t count = Sensors::scanDS18B20(addresses);

        StaticJsonDocument<768> doc;
        doc["count"] = count;

        JsonArray sensors = doc.createNestedArray("sensors");
        for (uint8_t i = 0; i < count; i++) {
            JsonObject s = sensors.createNestedObject();

            char addrStr[24];
            snprintf(addrStr, sizeof(addrStr), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                addresses[i][0], addresses[i][1], addresses[i][2], addresses[i][3],
                addresses[i][4], addresses[i][5], addresses[i][6], addresses[i][7]);

            s["index"] = i;
            s["address"] = addrStr;
            s["valid"] = Sensors::isTempSensorValid(i);
        }

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
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
    StaticJsonDocument<1536> doc;

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

    // Здоровье системы
    JsonObject health = doc.createNestedObject("health");
    health["overall"] = state.health.overallHealth;
    health["tempSensorsOk"] = state.health.tempSensorsOk;
    health["tempSensorsTotal"] = state.health.tempSensorsTotal;
    health["bmp280"] = state.health.bmp280Ok;
    health["ads1115"] = state.health.ads1115Ok;
    health["pzem"] = state.health.pzemOk;
    health["wifiRSSI"] = state.health.wifiRSSI;
    health["pzemSpikes"] = state.health.pzemSpikeCount;
    health["tempErrors"] = state.health.tempReadErrors;
    health["cpuTemp"] = state.health.cpuTemp;

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
