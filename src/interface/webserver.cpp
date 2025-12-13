/**
 * Smart-Column S3 - Веб-сервер
 *
 * HTTP server + WebSocket для Web UI
 */

#include "webserver.h"
#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>

// Определение HTTP методов для ESPAsyncWebServer
#ifndef HTTP_GET
typedef enum {
  HTTP_GET     = 0b00000001,
  HTTP_POST    = 0b00000010,
  HTTP_DELETE  = 0b00000100,
  HTTP_PUT     = 0b00001000,
  HTTP_PATCH   = 0b00010000,
  HTTP_HEAD    = 0b00100000,
  HTTP_OPTIONS = 0b01000000,
  HTTP_ANY     = 0b01111111,
} WebRequestMethod;
#endif

#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "storage/nvs_manager.h"
#include "drivers/sensors.h"
#include "control/fsm.h"

// Внешние переменные из main.cpp
extern SystemState g_state;
extern Settings g_settings;
extern EnergyHistory g_energyHistory;

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

    // POST /api/process/start - запуск процесса
    server.on("/api/process/start", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            // Ждем получения всех данных
            if (index + len != total) {
                return;
            }

            StaticJsonDocument<256> doc;
            DeserializationError error = deserializeJson(doc, data, len);

            if (error) {
                LOG_E("Process start: JSON parse error");
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
                return;
            }

            const char* modeStr = doc["mode"];
            if (!modeStr) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Mode required\"}");
                return;
            }

            // Определяем режим
            Mode mode = Mode::IDLE;
            if (strcmp(modeStr, "rectification") == 0) {
                mode = Mode::RECTIFICATION;
            } else if (strcmp(modeStr, "distillation") == 0) {
                mode = Mode::DISTILLATION;
            } else if (strcmp(modeStr, "manual") == 0) {
                mode = Mode::MANUAL_RECT;
            } else {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Unknown mode\"}");
                return;
            }

            // Проверка термометров (только предупреждение, не блокируем запуск)
            bool sensorsOk = g_state.health.tempSensorsTotal > 0 && g_state.health.tempSensorsOk;

            if (!sensorsOk) {
                LOG_W("Starting process without temperature sensors!");
            }

            // Запуск через FSM
            FSM::startMode(g_state, g_settings, mode);

            LOG_I("Process started: mode=%s, sensors=%s", modeStr, sensorsOk ? "OK" : "WARNING");

            // Формируем ответ
            String response = "{\"success\":true,\"message\":\"Process started\"";
            if (!sensorsOk) {
                response += ",\"warning\":\"No temperature sensors detected\"";
            }
            response += "}";

            request->send(200, "application/json", response);
        }
    );

    // POST /api/process/stop - остановка процесса
    server.on("/api/process/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        FSM::stopMode(g_state);
        LOG_I("Process stopped via API");
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Process stopped\"}");
    });

    // POST /api/process/pause - пауза
    server.on("/api/process/pause", HTTP_POST, [](AsyncWebServerRequest *request) {
        FSM::pause(g_state);
        LOG_I("Process paused via API");
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Process paused\"}");
    });

    // POST /api/process/resume - возобновление
    server.on("/api/process/resume", HTTP_POST, [](AsyncWebServerRequest *request) {
        FSM::resume(g_state);
        LOG_I("Process resumed via API");
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Process resumed\"}");
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

    // ==========================================================================
    // ENERGY CONSUMPTION GRAPH
    // ==========================================================================

    // GET /api/energy - получить историю энергопотребления
    server.on("/api/energy", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Размер JSON зависит от количества точек
        size_t docSize = 2048 + g_energyHistory.count * 128;
        DynamicJsonDocument doc(docSize);

        doc["count"] = g_energyHistory.count;
        doc["maxPoints"] = EnergyHistory::MAX_POINTS;
        doc["lastUpdate"] = g_energyHistory.lastUpdate;

        JsonArray dataArray = doc.createNestedArray("data");

        // Прочитать данные из циклического буфера в правильном порядке
        for (uint16_t i = 0; i < g_energyHistory.count; i++) {
            // Индекс: начинаем с самой старой записи
            uint16_t index;
            if (g_energyHistory.count < EnergyHistory::MAX_POINTS) {
                // Буфер ещё не заполнен - читаем с начала
                index = i;
            } else {
                // Буфер заполнен - читаем с позиции writeIndex (самая старая)
                index = (g_energyHistory.writeIndex + i) % EnergyHistory::MAX_POINTS;
            }

            const EnergyDataPoint& point = g_energyHistory.points[index];

            JsonObject obj = dataArray.createNestedObject();
            obj["t"] = point.timestamp;
            obj["p"] = round(point.power * 10) / 10;        // 1 знак после запятой
            obj["e"] = round(point.energy * 1000) / 1000;   // 3 знака
            obj["v"] = round(point.voltage * 10) / 10;
            obj["i"] = round(point.current * 100) / 100;    // 2 знака
        }

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // ==========================================================================
    // WIFI MANAGEMENT
    // ==========================================================================

    // GET /api/wifi/scan - сканирование доступных сетей
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        LOG_I("WiFi: Scanning networks...");

        int networksFound = WiFi.scanNetworks();

        StaticJsonDocument<2048> doc;
        doc["count"] = networksFound;

        JsonArray networks = doc.createNestedArray("networks");
        for (int i = 0; i < networksFound; i++) {
            JsonObject net = networks.createNestedObject();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
            net["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "secured";
            net["channel"] = WiFi.channel(i);
        }

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);

        WiFi.scanDelete();  // Очистить результаты сканирования
    });

    // GET /api/wifi/status - текущий статус WiFi
    server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<512> doc;

        doc["connected"] = (WiFi.status() == WL_CONNECTED);
        doc["ssid"] = WiFi.SSID();
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
        doc["apMode"] = g_settings.wifi.apMode;

        if (g_settings.wifi.apMode) {
            doc["apSSID"] = WIFI_AP_SSID;
            doc["apIP"] = WiFi.softAPIP().toString();
        }

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // POST /api/wifi/connect - подключение к сети
    server.on("/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            // Обрабатываем только когда получены все данные
            if (index + len != total) {
                return; // Ждем остальные chunks
            }

            StaticJsonDocument<256> doc;
            DeserializationError error = deserializeJson(doc, data, len);

            if (error) {
                LOG_E("WiFi: JSON parse error: %s", error.c_str());
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            const char* ssid = doc["ssid"];
            const char* password = doc["password"];

            if (!ssid || strlen(ssid) == 0) {
                request->send(400, "application/json", "{\"error\":\"SSID required\"}");
                return;
            }

            LOG_I("WiFi: Connect request for SSID: %s", ssid);

            // Сохранить в настройки
            strncpy(g_settings.wifi.ssid, ssid, sizeof(g_settings.wifi.ssid) - 1);
            g_settings.wifi.ssid[sizeof(g_settings.wifi.ssid) - 1] = '\0';

            strncpy(g_settings.wifi.password, password ? password : "", sizeof(g_settings.wifi.password) - 1);
            g_settings.wifi.password[sizeof(g_settings.wifi.password) - 1] = '\0';

            g_settings.wifi.apMode = false;

            // Сохранить в NVS
            if (NVSManager::saveSettings(g_settings)) {
                LOG_I("WiFi: Settings saved, connecting to %s", ssid);

                // Отправить ответ перед переподключением
                request->send(200, "application/json", "{\"status\":\"connecting\",\"message\":\"Connecting to WiFi, please wait...\"}");

                // Попытка подключения через небольшую задержку
                // чтобы ответ успел уйти клиенту
                delay(100);

                WiFi.disconnect();
                WiFi.mode(WIFI_STA);
                WiFi.begin(g_settings.wifi.ssid, g_settings.wifi.password);
            } else {
                LOG_E("WiFi: Failed to save settings to NVS");
                request->send(500, "application/json", "{\"error\":\"Failed to save settings\"}");
            }
        }
    );

    // ==========================================================================
    // OTA UPDATE (Web UI)
    // ==========================================================================

    // GET /update - страница загрузки прошивки
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = F(
            "<!DOCTYPE html><html><head>"
            "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Smart-Column S3 - OTA Update</title>"
            "<style>"
            "body{font-family:Arial,sans-serif;max-width:600px;margin:50px auto;padding:20px;background:#f5f5f5}"
            ".container{background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
            "h1{color:#333;margin-bottom:20px}"
            ".info{background:#e3f2fd;padding:15px;border-radius:5px;margin-bottom:20px}"
            "input[type=file]{width:100%;padding:10px;margin:10px 0;border:2px dashed #ccc;border-radius:5px;cursor:pointer}"
            "input[type=submit]{background:#4CAF50;color:white;padding:15px 30px;border:none;border-radius:5px;cursor:pointer;font-size:16px;width:100%}"
            "input[type=submit]:hover{background:#45a049}"
            ".progress{display:none;margin-top:20px}"
            ".progress-bar{width:100%;height:30px;background:#ddd;border-radius:15px;overflow:hidden}"
            ".progress-fill{height:100%;background:#4CAF50;transition:width 0.3s}"
            ".status{margin-top:15px;padding:10px;border-radius:5px;text-align:center}"
            ".success{background:#d4edda;color:#155724}"
            ".error{background:#f8d7da;color:#721c24}"
            "</style>"
            "</head><body>"
            "<div class='container'>"
            "<h1>🔧 Firmware Update</h1>"
            "<div class='info'>"
            "<strong>Current version:</strong> " FW_VERSION "<br>"
            "<strong>Build date:</strong> " __DATE__ " " __TIME__ "<br>"
            "<strong>Platform:</strong> ESP32-S3"
            "</div>"
            "<form method='POST' action='/update' enctype='multipart/form-data' id='upload_form'>"
            "<input type='file' name='update' accept='.bin' required>"
            "<input type='submit' value='Upload Firmware'>"
            "</form>"
            "<div class='progress' id='progress'>"
            "<div class='progress-bar'><div class='progress-fill' id='progress-fill'></div></div>"
            "<div id='status'></div>"
            "</div>"
            "</div>"
            "<script>"
            "document.getElementById('upload_form').addEventListener('submit',function(e){"
            "e.preventDefault();"
            "var formData=new FormData(this);"
            "var xhr=new XMLHttpRequest();"
            "document.getElementById('progress').style.display='block';"
            "xhr.upload.addEventListener('progress',function(e){"
            "if(e.lengthComputable){"
            "var percent=(e.loaded/e.total)*100;"
            "document.getElementById('progress-fill').style.width=percent+'%';"
            "document.getElementById('status').textContent=Math.round(percent)+'%';"
            "}"
            "});"
            "xhr.addEventListener('load',function(){"
            "if(xhr.status===200){"
            "document.getElementById('status').className='status success';"
            "document.getElementById('status').textContent='✓ Update successful! Rebooting...';"
            "setTimeout(function(){location.href='/';},5000);"
            "}else{"
            "document.getElementById('status').className='status error';"
            "document.getElementById('status').textContent='✗ Update failed: '+xhr.responseText;"
            "}"
            "});"
            "xhr.open('POST','/update');"
            "xhr.send(formData);"
            "});"
            "</script>"
            "</body></html>"
        );
        request->send(200, "text/html", html);
    });

    // POST /update - загрузка и установка прошивки
    server.on("/update", HTTP_POST,
        // Обработчик завершения загрузки
        [](AsyncWebServerRequest *request) {
            bool shouldReboot = !Update.hasError();
            AsyncWebServerResponse *response = request->beginResponse(
                200, "text/plain",
                shouldReboot ? "OK" : "FAIL"
            );
            response->addHeader("Connection", "close");
            request->send(response);

            if (shouldReboot) {
                LOG_I("OTA: Update successful, rebooting...");
                delay(1000);
                ESP.restart();
            } else {
                LOG_E("OTA: Update failed!");
            }
        },
        // Обработчик загрузки данных
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                LOG_I("OTA: Update start: %s", filename.c_str());

                // Начало обновления
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            }

            // Запись данных
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }

            if (final) {
                if (Update.end(true)) {
                    LOG_I("OTA: Update success: %u bytes", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );

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
