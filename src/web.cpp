/**
 * @file web.cpp
 * @brief Реализация веб-интерфейса для управления системой
 */

#include "web.h"
#include "settings.h"
#include "temp_sensors.h"
#include "heater.h"
#include "pump.h"
#include "valve.h"
#include "rectification.h"
#include "distillation.h"
#include "safety.h"
#include "history.h"
#include "profiles.h"
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Объект веб-сервера на порту 80
AsyncWebServer server(80);

// Объекты для обработки WebSocket
AsyncWebSocket ws("/ws");
bool webSocketActive = false;
unsigned long lastWsUpdate = 0;
int wsUpdateInterval = 1000; // Обновление по WebSocket каждую секунду

// Указатель на функцию обработки сообщений WebSocket
void (*webSocketMessageHandler)(AsyncWebSocketClient*, String) = nullptr;

// Инициализация модуля веб-сервера
void initWebServer() {
    // Начинаем работу с файловой системой
    if (!LittleFS.begin(true)) {
        Serial.println("Ошибка монтирования файловой системы LittleFS");
        return;
    }
    
    // Настройка обработчика WebSocket
    ws.onEvent(onWebSocketEvent);
    server.addHandler(&ws);
    
    // Настройка маршрутов API
    setupApiRoutes();
    
    // Настройка маршрутов для статических файлов
    setupStaticRoutes();
    
    // Настройка обработчика для не найденных ресурсов (404)
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    
    // Запуск веб-сервера
    server.begin();
    
    Serial.println("Веб-сервер запущен");
}

// Обновление состояния WebSocket соединения
void updateWebSocket() {
    if (!webSocketActive) {
        return;
    }
    
    unsigned long currentTime = millis();
    if (currentTime - lastWsUpdate < wsUpdateInterval) {
        return;
    }
    
    lastWsUpdate = currentTime;
    
    // Создаем JSON объект для отправки статуса
    DynamicJsonDocument doc(1024);
    
    // Добавляем информацию о температуре
    JsonObject temps = doc.createNestedObject("temperatures");
    temps["cube"] = getTemperature(TEMP_CUBE);
    temps["column"] = getTemperature(TEMP_COLUMN);
    temps["reflux"] = getTemperature(TEMP_REFLUX);
    temps["tsa"] = getTemperature(TEMP_TSA);
    temps["waterOut"] = getTemperature(TEMP_WATER_OUT);
    
    // Добавляем информацию о нагревателе
    JsonObject heater = doc.createNestedObject("heater");
    heater["power"] = getHeaterPowerWatts();
    heater["percent"] = getHeaterPowerPercent();
    
    // Добавляем информацию о насосе
    JsonObject pump = doc.createNestedObject("pump");
    pump["running"] = isPumpRunning();
    pump["flowRate"] = getPumpFlowRate();
    
    // Добавляем информацию о клапане
    JsonObject valve = doc.createNestedObject("valve");
    valve["open"] = isValveOpen();
    
    // Добавляем информацию о системе
    JsonObject system = doc.createNestedObject("system");
    system["uptime"] = millis() / 1000;
    
    // Информация о текущем процессе
    if (isRectificationRunning()) {
        JsonObject process = doc.createNestedObject("rectification");
        process["running"] = true;
        process["paused"] = isRectificationPaused();
        process["phase"] = getRectificationPhaseName();
        process["uptime"] = getRectificationUptime();
        process["phaseTime"] = getRectificationPhaseTime();
        process["headsVolume"] = getRectificationHeadsVolume();
        process["bodyVolume"] = getRectificationBodyVolume();
        process["tailsVolume"] = getRectificationTailsVolume();
        process["totalVolume"] = getRectificationTotalVolume();
        process["refluxStatus"] = getRectificationRefluxStatus();
    }
    else if (isDistillationRunning()) {
        JsonObject process = doc.createNestedObject("distillation");
        process["running"] = true;
        process["paused"] = isDistillationPaused();
        process["phase"] = getDistillationPhaseName();
        process["uptime"] = getDistillationUptime();
        process["phaseTime"] = getDistillationPhaseTime();
        process["productVolume"] = getDistillationProductVolume();
        process["headsVolume"] = getDistillationHeadsVolume();
        process["headsMode"] = isDistillationHeadsMode();
    }
    
    // Добавляем информацию о безопасности
    JsonObject safety = doc.createNestedObject("safety");
    SafetyStatus safetyStatus = getSafetyStatus();
    safety["isSystemSafe"] = safetyStatus.isSystemSafe;
    safety["errorCode"] = safetyStatus.errorCode;
    safety["errorDescription"] = safetyStatus.errorDescription;
    
    if (!safetyStatus.isSystemSafe) {
        safety["errorTime"] = safetyStatus.errorTime;
        safety["isSensorError"] = safetyStatus.isSensorError;
        safety["isTemperatureError"] = safetyStatus.isTemperatureError;
        safety["isWaterFlowError"] = safetyStatus.isWaterFlowError;
        safety["isPressureError"] = safetyStatus.isPressureError;
        safety["isRuntimeError"] = safetyStatus.isRuntimeError;
        safety["isEmergencyStop"] = safetyStatus.isEmergencyStop;
    }
    
    // Сериализуем JSON в строку и отправляем
    String jsonString;
    serializeJson(doc, jsonString);
    ws.textAll(jsonString);
}

// Настройка маршрутов API
void setupApiRoutes() {
    // Получение статуса системы
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(1024);
        
        // Информация о температуре
        JsonObject temps = doc.createNestedObject("temperatures");
        temps["cube"] = getTemperature(TEMP_CUBE);
        temps["column"] = getTemperature(TEMP_COLUMN);
        temps["reflux"] = getTemperature(TEMP_REFLUX);
        temps["tsa"] = getTemperature(TEMP_TSA);
        temps["waterOut"] = getTemperature(TEMP_WATER_OUT);
        
        // Информация о подключенных датчиках
        JsonObject sensors = doc.createNestedObject("sensors");
        sensors["cube"] = isSensorConnected(TEMP_CUBE);
        sensors["column"] = isSensorConnected(TEMP_COLUMN);
        sensors["reflux"] = isSensorConnected(TEMP_REFLUX);
        sensors["tsa"] = isSensorConnected(TEMP_TSA);
        sensors["waterOut"] = isSensorConnected(TEMP_WATER_OUT);
        
        // Информация о нагревателе
        JsonObject heater = doc.createNestedObject("heater");
        heater["power"] = getHeaterPowerWatts();
        heater["percent"] = getHeaterPowerPercent();
        
        // Информация о насосе
        JsonObject pump = doc.createNestedObject("pump");
        pump["running"] = isPumpRunning();
        pump["flowRate"] = getPumpFlowRate();
        
        // Информация о клапане
        JsonObject valve = doc.createNestedObject("valve");
        valve["open"] = isValveOpen();
        
        // Информация о текущем процессе
        if (isRectificationRunning()) {
            JsonObject process = doc.createNestedObject("rectification");
            process["running"] = true;
            process["paused"] = isRectificationPaused();
            process["phase"] = getRectificationPhaseName();
            process["uptime"] = getRectificationUptime();
            process["phaseTime"] = getRectificationPhaseTime();
            process["headsVolume"] = getRectificationHeadsVolume();
            process["bodyVolume"] = getRectificationBodyVolume();
            process["tailsVolume"] = getRectificationTailsVolume();
            process["totalVolume"] = getRectificationTotalVolume();
            process["refluxStatus"] = getRectificationRefluxStatus();
        }
        else if (isDistillationRunning()) {
            JsonObject process = doc.createNestedObject("distillation");
            process["running"] = true;
            process["paused"] = isDistillationPaused();
            process["phase"] = getDistillationPhaseName();
            process["uptime"] = getDistillationUptime();
            process["phaseTime"] = getDistillationPhaseTime();
            process["productVolume"] = getDistillationProductVolume();
            process["headsVolume"] = getDistillationHeadsVolume();
            process["headsMode"] = isDistillationHeadsMode();
        }
        else {
            doc["process"] = "idle";
        }
        
        // Информация о безопасности
        JsonObject safety = doc.createNestedObject("safety");
        SafetyStatus status = getSafetyStatus();
        safety["isSystemSafe"] = status.isSystemSafe;
        safety["errorCode"] = status.errorCode;
        safety["errorDescription"] = status.errorDescription;
        
        if (!status.isSystemSafe) {
            safety["errorTime"] = status.errorTime;
            safety["isSensorError"] = status.isSensorError;
            safety["isTemperatureError"] = status.isTemperatureError;
            safety["isWaterFlowError"] = status.isWaterFlowError;
            safety["isPressureError"] = status.isPressureError;
            safety["isRuntimeError"] = status.isRuntimeError;
            safety["isEmergencyStop"] = status.isEmergencyStop;
            safety["isWatchdogReset"] = status.isWatchdogReset;
        }
        
        // Отправляем ответ
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Получение настроек
    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(2048);
        
        // Настройки нагревателя
        JsonObject heater = doc.createNestedObject("heater");
        heater["maxPowerWatts"] = sysSettings.heaterSettings.maxPowerWatts;
        
        // Настройки датчиков
        JsonObject sensors = doc.createNestedObject("sensors");
        
        for (int i = 0; i < MAX_TEMP_SENSORS; i++) {
            JsonObject sensor = sensors.createNestedObject(String(i));
            sensor["name"] = getTempSensorName(i);
            sensor["enabled"] = sysSettings.tempSensorEnabled[i];
            sensor["calibration"] = sysSettings.tempSensorCalibration[i];
            
            String address = "";
            for (int j = 0; j < 8; j++) {
                char hex[3];
                sprintf(hex, "%02X", sysSettings.tempSensorAddresses[i][j]);
                address += hex;
                if (j < 7) address += ":";
            }
            sensor["address"] = address;
        }
        
        // Настройки насоса
        JsonObject pump = doc.createNestedObject("pump");
        pump["headsFlowRate"] = sysSettings.pumpSettings.headsFlowRate;
        pump["bodyFlowRate"] = sysSettings.pumpSettings.bodyFlowRate;
        pump["tailsFlowRate"] = sysSettings.pumpSettings.tailsFlowRate;
        
        // Настройки ректификации
        JsonObject rect = doc.createNestedObject("rectification");
        rect["model"] = sysSettings.rectificationSettings.model;
        rect["heatingPowerWatts"] = sysSettings.rectificationSettings.heatingPowerWatts;
        rect["stabilizationPowerWatts"] = sysSettings.rectificationSettings.stabilizationPowerWatts;
        rect["bodyPowerWatts"] = sysSettings.rectificationSettings.bodyPowerWatts;
        rect["tailsPowerWatts"] = sysSettings.rectificationSettings.tailsPowerWatts;
        rect["headsTemp"] = sysSettings.rectificationSettings.headsTemp;
        rect["bodyTemp"] = sysSettings.rectificationSettings.bodyTemp;
        rect["tailsTemp"] = sysSettings.rectificationSettings.tailsTemp;
        rect["endTemp"] = sysSettings.rectificationSettings.endTemp;
        rect["maxCubeTemp"] = sysSettings.rectificationSettings.maxCubeTemp;
        rect["stabilizationTime"] = sysSettings.rectificationSettings.stabilizationTime;
        rect["postHeadsStabilizationTime"] = sysSettings.rectificationSettings.postHeadsStabilizationTime;
        rect["headsVolume"] = sysSettings.rectificationSettings.headsVolume;
        rect["bodyVolume"] = sysSettings.rectificationSettings.bodyVolume;
        rect["refluxRatio"] = sysSettings.rectificationSettings.refluxRatio;
        rect["refluxPeriod"] = sysSettings.rectificationSettings.refluxPeriod;
        
        // Настройки дистилляции
        JsonObject dist = doc.createNestedObject("distillation");
        dist["heatingPowerWatts"] = sysSettings.distillationSettings.heatingPowerWatts;
        dist["distillationPowerWatts"] = sysSettings.distillationSettings.distillationPowerWatts;
        dist["startCollectingTemp"] = sysSettings.distillationSettings.startCollectingTemp;
        dist["endTemp"] = sysSettings.distillationSettings.endTemp;
        dist["maxCubeTemp"] = sysSettings.distillationSettings.maxCubeTemp;
        dist["separateHeads"] = sysSettings.distillationSettings.separateHeads;
        dist["headsVolume"] = sysSettings.distillationSettings.headsVolume;
        dist["flowRate"] = sysSettings.distillationSettings.flowRate;
        dist["headsFlowRate"] = sysSettings.distillationSettings.headsFlowRate;
        
        // Настройки безопасности
        JsonObject safety = doc.createNestedObject("safety");
        safety["maxRuntimeHours"] = sysSettings.safetySettings.maxRuntimeHours;
        safety["maxCubeTemp"] = sysSettings.safetySettings.maxCubeTemp;
        safety["maxTempRiseRate"] = sysSettings.safetySettings.maxTempRiseRate;
        safety["minWaterOutTemp"] = sysSettings.safetySettings.minWaterOutTemp;
        safety["maxWaterOutTemp"] = sysSettings.safetySettings.maxWaterOutTemp;
        safety["emergencyStopEnabled"] = sysSettings.safetySettings.emergencyStopEnabled;
        safety["watchdogEnabled"] = sysSettings.safetySettings.watchdogEnabled;
        
        // Отправляем ответ
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Обновление настроек
    server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200);
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        DynamicJsonDocument doc(2048);
        deserializeJson(doc, data, len);
        
        // Обновляем настройки нагревателя
        if (doc.containsKey("heater")) {
            JsonObject heater = doc["heater"];
            if (heater.containsKey("maxPowerWatts")) {
                sysSettings.heaterSettings.maxPowerWatts = heater["maxPowerWatts"];
            }
        }
        
        // Обновляем настройки насоса
        if (doc.containsKey("pump")) {
            JsonObject pump = doc["pump"];
            if (pump.containsKey("headsFlowRate")) {
                sysSettings.pumpSettings.headsFlowRate = pump["headsFlowRate"];
            }
            if (pump.containsKey("bodyFlowRate")) {
                sysSettings.pumpSettings.bodyFlowRate = pump["bodyFlowRate"];
            }
            if (pump.containsKey("tailsFlowRate")) {
                sysSettings.pumpSettings.tailsFlowRate = pump["tailsFlowRate"];
            }
        }
        
        // Обновляем настройки датчиков
        if (doc.containsKey("sensors")) {
            JsonObject sensors = doc["sensors"];
            for (JsonPair kv : sensors) {
                int sensorIndex = atoi(kv.key().c_str());
                if (sensorIndex >= 0 && sensorIndex < MAX_TEMP_SENSORS) {
                    JsonObject sensor = kv.value().as<JsonObject>();
                    
                    if (sensor.containsKey("calibration")) {
                        float calibration = sensor["calibration"];
                        calibrateTempSensor(sensorIndex, calibration);
                    }
                }
            }
        }
        
        // Обновляем настройки ректификации
        if (doc.containsKey("rectification")) {
            JsonObject rect = doc["rectification"];
            
            if (rect.containsKey("model")) {
                sysSettings.rectificationSettings.model = rect["model"];
            }
            if (rect.containsKey("heatingPowerWatts")) {
                sysSettings.rectificationSettings.heatingPowerWatts = rect["heatingPowerWatts"];
            }
            if (rect.containsKey("stabilizationPowerWatts")) {
                sysSettings.rectificationSettings.stabilizationPowerWatts = rect["stabilizationPowerWatts"];
            }
            if (rect.containsKey("bodyPowerWatts")) {
                sysSettings.rectificationSettings.bodyPowerWatts = rect["bodyPowerWatts"];
            }
            if (rect.containsKey("tailsPowerWatts")) {
                sysSettings.rectificationSettings.tailsPowerWatts = rect["tailsPowerWatts"];
            }
            if (rect.containsKey("headsTemp")) {
                sysSettings.rectificationSettings.headsTemp = rect["headsTemp"];
            }
            if (rect.containsKey("bodyTemp")) {
                sysSettings.rectificationSettings.bodyTemp = rect["bodyTemp"];
            }
            if (rect.containsKey("tailsTemp")) {
                sysSettings.rectificationSettings.tailsTemp = rect["tailsTemp"];
            }
            if (rect.containsKey("endTemp")) {
                sysSettings.rectificationSettings.endTemp = rect["endTemp"];
            }
            if (rect.containsKey("maxCubeTemp")) {
                sysSettings.rectificationSettings.maxCubeTemp = rect["maxCubeTemp"];
            }
            if (rect.containsKey("stabilizationTime")) {
                sysSettings.rectificationSettings.stabilizationTime = rect["stabilizationTime"];
            }
            if (rect.containsKey("postHeadsStabilizationTime")) {
                sysSettings.rectificationSettings.postHeadsStabilizationTime = rect["postHeadsStabilizationTime"];
            }
            if (rect.containsKey("headsVolume")) {
                sysSettings.rectificationSettings.headsVolume = rect["headsVolume"];
            }
            if (rect.containsKey("bodyVolume")) {
                sysSettings.rectificationSettings.bodyVolume = rect["bodyVolume"];
            }
            if (rect.containsKey("refluxRatio")) {
                sysSettings.rectificationSettings.refluxRatio = rect["refluxRatio"];
            }
            if (rect.containsKey("refluxPeriod")) {
                sysSettings.rectificationSettings.refluxPeriod = rect["refluxPeriod"];
            }
        }
        
        // Обновляем настройки дистилляции
        if (doc.containsKey("distillation")) {
            JsonObject dist = doc["distillation"];
            
            if (dist.containsKey("heatingPowerWatts")) {
                sysSettings.distillationSettings.heatingPowerWatts = dist["heatingPowerWatts"];
            }
            if (dist.containsKey("distillationPowerWatts")) {
                sysSettings.distillationSettings.distillationPowerWatts = dist["distillationPowerWatts"];
            }
            if (dist.containsKey("startCollectingTemp")) {
                sysSettings.distillationSettings.startCollectingTemp = dist["startCollectingTemp"];
            }
            if (dist.containsKey("endTemp")) {
                sysSettings.distillationSettings.endTemp = dist["endTemp"];
            }
            if (dist.containsKey("maxCubeTemp")) {
                sysSettings.distillationSettings.maxCubeTemp = dist["maxCubeTemp"];
            }
            if (dist.containsKey("separateHeads")) {
                sysSettings.distillationSettings.separateHeads = dist["separateHeads"];
            }
            if (dist.containsKey("headsVolume")) {
                sysSettings.distillationSettings.headsVolume = dist["headsVolume"];
            }
            if (dist.containsKey("flowRate")) {
                sysSettings.distillationSettings.flowRate = dist["flowRate"];
            }
            if (dist.containsKey("headsFlowRate")) {
                sysSettings.distillationSettings.headsFlowRate = dist["headsFlowRate"];
            }
        }
        
        // Обновляем настройки безопасности
        if (doc.containsKey("safety")) {
            JsonObject safety = doc["safety"];
            
            if (safety.containsKey("maxRuntimeHours")) {
                sysSettings.safetySettings.maxRuntimeHours = safety["maxRuntimeHours"];
                setSafetyMaxRuntime(sysSettings.safetySettings.maxRuntimeHours);
            }
            
            if (safety.containsKey("maxCubeTemp")) {
                sysSettings.safetySettings.maxCubeTemp = safety["maxCubeTemp"];
                setSafetyMaxCubeTemp(sysSettings.safetySettings.maxCubeTemp);
            }
            
            if (safety.containsKey("maxTempRiseRate")) {
                sysSettings.safetySettings.maxTempRiseRate = safety["maxTempRiseRate"];
                setSafetyMaxTempRiseRate(sysSettings.safetySettings.maxTempRiseRate);
            }
            
            if (safety.containsKey("minWaterOutTemp")) {
                sysSettings.safetySettings.minWaterOutTemp = safety["minWaterOutTemp"];
                setSafetyMinWaterOutTemp(sysSettings.safetySettings.minWaterOutTemp);
            }
            
            if (safety.containsKey("maxWaterOutTemp")) {
                sysSettings.safetySettings.maxWaterOutTemp = safety["maxWaterOutTemp"];
                setSafetyMaxWaterOutTemp(sysSettings.safetySettings.maxWaterOutTemp);
            }
            
            if (safety.containsKey("emergencyStopEnabled")) {
                sysSettings.safetySettings.emergencyStopEnabled = safety["emergencyStopEnabled"];
            }
            
            if (safety.containsKey("watchdogEnabled")) {
                sysSettings.safetySettings.watchdogEnabled = safety["watchdogEnabled"];
            }
        }
        
        // Сохраняем обновленные настройки
        saveSystemSettings();
        
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // API для управления нагревателем
    server.on("/api/heater", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200);
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        DynamicJsonDocument doc(256);
        deserializeJson(doc, data, len);
        
        if (doc.containsKey("power")) {
            int power = doc["power"];
            setHeaterPower(power);
        } else if (doc.containsKey("percent")) {
            int percent = doc["percent"];
            setHeaterPowerPercent(percent);
        }
        
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // API для управления насосом
    server.on("/api/pump", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200);
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        DynamicJsonDocument doc(256);
        deserializeJson(doc, data, len);
        
        if (doc.containsKey("start")) {
            bool start = doc["start"];
            if (start) {
                if (doc.containsKey("flowRate")) {
                    float flowRate = doc["flowRate"];
                    pumpStart(flowRate);
                } else {
                    pumpStart(sysSettings.pumpSettings.bodyFlowRate);
                }
            } else {
                pumpStop();
            }
        } else if (doc.containsKey("flowRate")) {
            float flowRate = doc["flowRate"];
            setPumpFlowRate(flowRate);
        }
        
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // API для управления клапаном
    server.on("/api/valve", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200);
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        DynamicJsonDocument doc(256);
        deserializeJson(doc, data, len);
        
        if (doc.containsKey("open")) {
            bool open = doc["open"];
            if (open) {
                valveOpen();
            } else {
                valveClose();
            }
        } else if (doc.containsKey("toggle")) {
            toggleValve();
        }
        
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // API для запуска процесса ректификации
    server.on("/api/rectification/start", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool success = startRectification();
        
        if (success) {
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            request->send(400, "application/json", 
                "{\"status\":\"error\",\"message\":\"Не удалось запустить процесс ректификации\"}");
        }
    });
    
    // API для остановки процесса ректификации
    server.on("/api/rectification/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        stopRectification();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // API для паузы процесса ректификации
    server.on("/api/rectification/pause", HTTP_POST, [](AsyncWebServerRequest *request) {
        pauseRectification();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // API для возобновления процесса ректификации
    server.on("/api/rectification/resume", HTTP_POST, [](AsyncWebServerRequest *request) {
        resumeRectification();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // API для управления фазами ректификации
    server.on("/api/rectification/phase", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200);
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        DynamicJsonDocument doc(256);
        deserializeJson(doc, data, len);
        
        if (doc.containsKey("phase")) {
            String phase = doc["phase"].as<String>();
            bool success = false;
            
            if (phase == "heads") {
                success = startHeadsCollection();
            } else if (phase == "body") {
                success = startBodyCollection();
            } else if (phase == "tails") {
                success = startTailsCollection();
            } else if (phase == "next") {
                moveToNextRectificationPhase();
                success = true;
            }
            
            if (success) {
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(400, "application/json", 
                    "{\"status\":\"error\",\"message\":\"Не удалось изменить фазу ректификации\"}");
            }
        } else {
            request->send(400, "application/json", 
                "{\"status\":\"error\",\"message\":\"Не указана фаза\"}");
        }
    });
    
    // API для запуска процесса дистилляции
    server.on("/api/distillation/start", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool success = startDistillation();
        
        if (success) {
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            request->send(400, "application/json", 
                "{\"status\":\"error\",\"message\":\"Не удалось запустить процесс дистилляции\"}");
        }
    });
    
    // API для остановки процесса дистилляции
    server.on("/api/distillation/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        stopDistillation();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // API для паузы процесса дистилляции
    server.on("/api/distillation/pause", HTTP_POST, [](AsyncWebServerRequest *request) {
        pauseDistillation();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // API для возобновления процесса дистилляции
    server.on("/api/distillation/resume", HTTP_POST, [](AsyncWebServerRequest *request) {
        resumeDistillation();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // API для управления фазами дистилляции
    server.on("/api/distillation/phase", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200);
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        DynamicJsonDocument doc(256);
        deserializeJson(doc, data, len);
        
        if (doc.containsKey("phase")) {
            String phase = doc["phase"].as<String>();
            bool success = false;
            
            if (phase == "heads") {
                success = startDistillationHeadsCollection();
            } else if (phase == "main") {
                success = startMainDistillation();
            } else if (phase == "next") {
                moveToNextDistillationPhase();
                success = true;
            }
            
            if (success) {
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(400, "application/json", 
                    "{\"status\":\"error\",\"message\":\"Не удалось изменить фазу дистилляции\"}");
            }
        } else {
            request->send(400, "application/json", 
                "{\"status\":\"error\",\"message\":\"Не указана фаза\"}");
        }
    });
    
    // API для получения статуса безопасности
    server.on("/api/safety/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        SafetyStatus status = getSafetyStatus();
        
        doc["isSystemSafe"] = status.isSystemSafe;
        doc["errorCode"] = status.errorCode;
        doc["errorTime"] = status.errorTime;
        doc["errorDescription"] = status.errorDescription;
        doc["isSensorError"] = status.isSensorError;
        doc["isTemperatureError"] = status.isTemperatureError;
        doc["isWaterFlowError"] = status.isWaterFlowError;
        doc["isPressureError"] = status.isPressureError;
        doc["isRuntimeError"] = status.isRuntimeError;
        doc["isEmergencyStop"] = status.isEmergencyStop;
        doc["isWatchdogReset"] = status.isWatchdogReset;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // API для сброса ошибок безопасности
    server.on("/api/safety/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool success = resetSafetyErrors();
        
        if (success) {
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            request->send(400, "application/json", 
                "{\"status\":\"error\",\"message\":\"Не удалось сбросить ошибки безопасности\"}");
        }
    });
    
    // API для сканирования датчиков температуры
    server.on("/api/sensors/scan", HTTP_POST, [](AsyncWebServerRequest *request) {
        int count = scanForTempSensors();
        
        DynamicJsonDocument doc(256);
        doc["status"] = "ok";
        doc["count"] = count;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // API для сброса настроек к значениям по умолчанию
    server.on("/api/settings/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        resetSystemSettings();
        saveSystemSettings();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // API для перезагрузки системы
    server.on("/api/system/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        delay(1000);
        ESP.restart();
    });

    // ========================================================================
    // API для работы с историей процессов
    // ========================================================================

    // GET /api/history - Получить список всех процессов
    server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *request) {
        std::vector<ProcessListItem> processes = getProcessList();

        DynamicJsonDocument doc(4096);
        doc["total"] = processes.size();

        JsonArray procArray = doc.createNestedArray("processes");
        for (const auto& proc : processes) {
            JsonObject p = procArray.createNestedObject();
            p["id"] = proc.id;
            p["type"] = proc.type;
            p["startTime"] = proc.startTime;
            p["duration"] = proc.duration;
            p["status"] = proc.status;
            p["totalVolume"] = proc.totalVolume;
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // GET /api/history/{id} - Получить полные данные процесса
    server.on("^\\/api\\/history\\/([0-9]+)$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String id = request->pathArg(0);

        ProcessHistory history;
        if (loadProcessHistory(id, history)) {
            // Создать полный JSON ответ
            DynamicJsonDocument doc(32768);

            doc["id"] = history.id;
            doc["version"] = history.version;

            JsonObject metadata = doc.createNestedObject("metadata");
            metadata["startTime"] = history.metadata.startTime;
            metadata["endTime"] = history.metadata.endTime;
            metadata["duration"] = history.metadata.duration;
            metadata["completedSuccessfully"] = history.metadata.completedSuccessfully;
            metadata["deviceId"] = history.metadata.deviceId;

            JsonObject process = doc.createNestedObject("process");
            process["type"] = history.process.type;
            process["mode"] = history.process.mode;
            process["profile"] = history.process.profile;

            JsonObject parameters = doc.createNestedObject("parameters");
            parameters["targetPower"] = history.parameters.targetPower;
            parameters["headVolume"] = history.parameters.headVolume;
            parameters["bodyVolume"] = history.parameters.bodyVolume;
            parameters["tailVolume"] = history.parameters.tailVolume;
            parameters["pumpSpeedHead"] = history.parameters.pumpSpeedHead;
            parameters["pumpSpeedBody"] = history.parameters.pumpSpeedBody;
            parameters["stabilizationTime"] = history.parameters.stabilizationTime;
            parameters["wattControlEnabled"] = history.parameters.wattControlEnabled;
            parameters["smartDecrementEnabled"] = history.parameters.smartDecrementEnabled;

            JsonObject metrics = doc.createNestedObject("metrics");
            JsonObject temps = metrics.createNestedObject("temperatures");
            JsonObject cube = temps.createNestedObject("cube");
            cube["min"] = history.metrics.cube.min;
            cube["max"] = history.metrics.cube.max;
            cube["avg"] = history.metrics.cube.avg;
            cube["final"] = history.metrics.cube.final;

            JsonObject power = metrics.createNestedObject("power");
            power["energyUsed"] = history.metrics.energyUsed;
            power["avgPower"] = history.metrics.avgPower;
            power["peakPower"] = history.metrics.peakPower;

            JsonObject pump = metrics.createNestedObject("pump");
            pump["totalVolume"] = history.metrics.totalVolume;
            pump["avgSpeed"] = history.metrics.avgSpeed;

            JsonArray phases = doc.createNestedArray("phases");
            for (const auto& phase : history.phases) {
                JsonObject p = phases.createNestedObject();
                p["name"] = phase.name;
                p["startTime"] = phase.startTime;
                p["endTime"] = phase.endTime;
                p["duration"] = phase.duration;
                p["startTemp"] = phase.startTemp;
                p["endTemp"] = phase.endTemp;
                p["volume"] = phase.volume;
                p["avgSpeed"] = phase.avgSpeed;
            }

            JsonObject timeseries = doc.createNestedObject("timeseries");
            timeseries["interval"] = TIMESERIES_INTERVAL;
            JsonArray data = timeseries.createNestedArray("data");
            for (const auto& point : history.timeseries) {
                JsonObject p = data.createNestedObject();
                p["time"] = point.time;
                p["cube"] = point.cube;
                p["columnTop"] = point.columnTop;
                p["power"] = point.power;
                p["pumpSpeed"] = point.pumpSpeed;
            }

            JsonObject results = doc.createNestedObject("results");
            results["headsCollected"] = history.results.headsCollected;
            results["bodyCollected"] = history.results.bodyCollected;
            results["tailsCollected"] = history.results.tailsCollected;
            results["totalCollected"] = history.results.totalCollected;
            results["status"] = history.results.status;

            doc["notes"] = history.notes;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        } else {
            request->send(404, "application/json", "{\"error\":\"Process not found\"}");
        }
    });

    // GET /api/history/{id}/export - Экспорт процесса в CSV или JSON
    server.on("^\\/api\\/history\\/([0-9]+)\\/export$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String id = request->pathArg(0);
        String format = request->hasParam("format") ? request->getParam("format")->value() : "csv";

        ProcessHistory history;
        if (loadProcessHistory(id, history)) {
            if (format == "csv") {
                String csv = exportProcessToCSV(history);
                request->send(200, "text/csv", csv);
            } else if (format == "json") {
                String json = exportProcessToJSON(history);
                request->send(200, "application/json", json);
            } else {
                request->send(400, "application/json", "{\"error\":\"Invalid format. Use csv or json\"}");
            }
        } else {
            request->send(404, "application/json", "{\"error\":\"Process not found\"}");
        }
    });

    // DELETE /api/history/{id} - Удалить процесс из истории
    server.on("^\\/api\\/history\\/([0-9]+)$", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        String id = request->pathArg(0);

        if (deleteProcess(id)) {
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Process deleted\"}");
        } else {
            request->send(404, "application/json", "{\"error\":\"Process not found\"}");
        }
    });

    // DELETE /api/history - Очистить всю историю
    server.on("/api/history", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (clearHistory()) {
            request->send(200, "application/json", "{\"success\":true,\"message\":\"All history cleared\"}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to clear history\"}");
        }
    });

    // POST /api/history/{id}/compare - Сравнение процессов
    server.on("^\\/api\\/history\\/([0-9]+)\\/compare$", HTTP_POST, [](AsyncWebServerRequest *request) {
        // TODO: Реализовать сравнение процессов в следующей версии
        request->send(501, "application/json", "{\"error\":\"Not implemented yet\"}");
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        // Body handler для получения списка ID процессов для сравнения
    });

    // =========================================================================
    // PROFILES API - Управление профилями процессов
    // =========================================================================

    // GET /api/profiles - Получить список всех профилей
    server.on("/api/profiles", HTTP_GET, [](AsyncWebServerRequest *request) {
        std::vector<ProfileListItem> profiles = getProfileList();

        DynamicJsonDocument doc(4096);
        doc["total"] = profiles.size();

        JsonArray profileArray = doc.createNestedArray("profiles");
        for (const auto& prof : profiles) {
            JsonObject p = profileArray.createNestedObject();
            p["id"] = prof.id;
            p["name"] = prof.name;
            p["category"] = prof.category;
            p["useCount"] = prof.useCount;
            p["lastUsed"] = prof.lastUsed;
            p["isBuiltin"] = prof.isBuiltin;
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // GET /api/profiles/{id} - Получить полные данные профиля
    server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String id = request->pathArg(0);

        Profile profile;
        if (loadProfile(id, profile)) {
            // Создать полный JSON ответ
            DynamicJsonDocument doc(8192);

            doc["id"] = profile.id;

            JsonObject metadata = doc.createNestedObject("metadata");
            metadata["name"] = profile.metadata.name;
            metadata["description"] = profile.metadata.description;
            metadata["category"] = profile.metadata.category;

            JsonArray tags = metadata.createNestedArray("tags");
            for (const auto& tag : profile.metadata.tags) {
                tags.add(tag);
            }

            metadata["created"] = profile.metadata.created;
            metadata["updated"] = profile.metadata.updated;
            metadata["author"] = profile.metadata.author;
            metadata["isBuiltin"] = profile.metadata.isBuiltin;

            JsonObject parameters = doc.createNestedObject("parameters");
            parameters["mode"] = profile.parameters.mode;
            parameters["model"] = profile.parameters.model;

            JsonObject heater = parameters.createNestedObject("heater");
            heater["maxPower"] = profile.parameters.heater.maxPower;
            heater["autoMode"] = profile.parameters.heater.autoMode;
            heater["pidKp"] = profile.parameters.heater.pidKp;
            heater["pidKi"] = profile.parameters.heater.pidKi;
            heater["pidKd"] = profile.parameters.heater.pidKd;

            JsonObject rectification = parameters.createNestedObject("rectification");
            rectification["stabilizationMin"] = profile.parameters.rectification.stabilizationMin;
            rectification["headsVolume"] = profile.parameters.rectification.headsVolume;
            rectification["bodyVolume"] = profile.parameters.rectification.bodyVolume;
            rectification["tailsVolume"] = profile.parameters.rectification.tailsVolume;
            rectification["headsSpeed"] = profile.parameters.rectification.headsSpeed;
            rectification["bodySpeed"] = profile.parameters.rectification.bodySpeed;
            rectification["tailsSpeed"] = profile.parameters.rectification.tailsSpeed;
            rectification["purgeMin"] = profile.parameters.rectification.purgeMin;

            JsonObject distillation = parameters.createNestedObject("distillation");
            distillation["headsVolume"] = profile.parameters.distillation.headsVolume;
            distillation["targetVolume"] = profile.parameters.distillation.targetVolume;
            distillation["speed"] = profile.parameters.distillation.speed;
            distillation["endTemp"] = profile.parameters.distillation.endTemp;

            JsonObject temperatures = parameters.createNestedObject("temperatures");
            temperatures["maxCube"] = profile.parameters.temperatures.maxCube;
            temperatures["maxColumn"] = profile.parameters.temperatures.maxColumn;
            temperatures["headsEnd"] = profile.parameters.temperatures.headsEnd;
            temperatures["bodyStart"] = profile.parameters.temperatures.bodyStart;
            temperatures["bodyEnd"] = profile.parameters.temperatures.bodyEnd;

            JsonObject safety = parameters.createNestedObject("safety");
            safety["maxRuntime"] = profile.parameters.safety.maxRuntime;
            safety["waterFlowMin"] = profile.parameters.safety.waterFlowMin;
            safety["pressureMax"] = profile.parameters.safety.pressureMax;

            JsonObject statistics = doc.createNestedObject("statistics");
            statistics["useCount"] = profile.statistics.useCount;
            statistics["lastUsed"] = profile.statistics.lastUsed;
            statistics["avgDuration"] = profile.statistics.avgDuration;
            statistics["avgYield"] = profile.statistics.avgYield;
            statistics["successRate"] = profile.statistics.successRate;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        } else {
            request->send(404, "application/json", "{\"error\":\"Profile not found\"}");
        }
    });

    // POST /api/profiles - Создать новый профиль
    server.on("/api/profiles", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"success\":true}");
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        // Body handler
        static String body;

        if (index == 0) {
            body = "";
        }

        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }

        if (index + len == total) {
            // Парсим JSON
            DynamicJsonDocument doc(8192);
            DeserializationError error = deserializeJson(doc, body);

            if (error) {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            Profile profile;
            uint32_t now = millis() / 1000;
            profile.id = String(now);

            // Метаданные
            profile.metadata.name = doc["metadata"]["name"].as<String>();
            profile.metadata.description = doc["metadata"]["description"].as<String>();
            profile.metadata.category = doc["metadata"]["category"].as<String>();

            JsonArray tags = doc["metadata"]["tags"];
            for (JsonVariant tag : tags) {
                profile.metadata.tags.push_back(tag.as<String>());
            }

            profile.metadata.created = now;
            profile.metadata.updated = now;
            profile.metadata.author = doc["metadata"]["author"] | "user";
            profile.metadata.isBuiltin = false;

            // Параметры
            profile.parameters.mode = doc["parameters"]["mode"].as<String>();
            profile.parameters.model = doc["parameters"]["model"] | "classic";

            profile.parameters.heater.maxPower = doc["parameters"]["heater"]["maxPower"];
            profile.parameters.heater.autoMode = doc["parameters"]["heater"]["autoMode"];
            profile.parameters.heater.pidKp = doc["parameters"]["heater"]["pidKp"];
            profile.parameters.heater.pidKi = doc["parameters"]["heater"]["pidKi"];
            profile.parameters.heater.pidKd = doc["parameters"]["heater"]["pidKd"];

            profile.parameters.rectification.stabilizationMin = doc["parameters"]["rectification"]["stabilizationMin"];
            profile.parameters.rectification.headsVolume = doc["parameters"]["rectification"]["headsVolume"];
            profile.parameters.rectification.bodyVolume = doc["parameters"]["rectification"]["bodyVolume"];
            profile.parameters.rectification.tailsVolume = doc["parameters"]["rectification"]["tailsVolume"];
            profile.parameters.rectification.headsSpeed = doc["parameters"]["rectification"]["headsSpeed"];
            profile.parameters.rectification.bodySpeed = doc["parameters"]["rectification"]["bodySpeed"];
            profile.parameters.rectification.tailsSpeed = doc["parameters"]["rectification"]["tailsSpeed"];
            profile.parameters.rectification.purgeMin = doc["parameters"]["rectification"]["purgeMin"];

            profile.parameters.distillation.headsVolume = doc["parameters"]["distillation"]["headsVolume"];
            profile.parameters.distillation.targetVolume = doc["parameters"]["distillation"]["targetVolume"];
            profile.parameters.distillation.speed = doc["parameters"]["distillation"]["speed"];
            profile.parameters.distillation.endTemp = doc["parameters"]["distillation"]["endTemp"];

            profile.parameters.temperatures.maxCube = doc["parameters"]["temperatures"]["maxCube"];
            profile.parameters.temperatures.maxColumn = doc["parameters"]["temperatures"]["maxColumn"];
            profile.parameters.temperatures.headsEnd = doc["parameters"]["temperatures"]["headsEnd"];
            profile.parameters.temperatures.bodyStart = doc["parameters"]["temperatures"]["bodyStart"];
            profile.parameters.temperatures.bodyEnd = doc["parameters"]["temperatures"]["bodyEnd"];

            profile.parameters.safety.maxRuntime = doc["parameters"]["safety"]["maxRuntime"];
            profile.parameters.safety.waterFlowMin = doc["parameters"]["safety"]["waterFlowMin"];
            profile.parameters.safety.pressureMax = doc["parameters"]["safety"]["pressureMax"];

            // Статистика
            profile.statistics.useCount = 0;
            profile.statistics.lastUsed = 0;
            profile.statistics.avgDuration = 0;
            profile.statistics.avgYield = 0;
            profile.statistics.successRate = 0;

            if (saveProfile(profile)) {
                String response = "{\"success\":true,\"id\":\"" + profile.id + "\"}";
                request->send(200, "application/json", response);
            } else {
                request->send(500, "application/json", "{\"error\":\"Failed to save profile\"}");
            }
        }
    });

    // PUT /api/profiles/{id} - Обновить профиль
    server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)$", HTTP_PUT, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"success\":true}");
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        static String body;
        static String profileId;

        if (index == 0) {
            body = "";
            profileId = request->pathArg(0);
        }

        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }

        if (index + len == total) {
            Profile profile;
            if (!loadProfile(profileId, profile)) {
                request->send(404, "application/json", "{\"error\":\"Profile not found\"}");
                return;
            }

            // Парсим JSON для обновления
            DynamicJsonDocument doc(8192);
            DeserializationError error = deserializeJson(doc, body);

            if (error) {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            // Обновляем поля (аналогично POST, но сохраняем id и created)
            profile.metadata.name = doc["metadata"]["name"].as<String>();
            profile.metadata.description = doc["metadata"]["description"].as<String>();
            profile.metadata.updated = millis() / 1000;

            // ... остальные поля как в POST

            if (saveProfile(profile)) {
                request->send(200, "application/json", "{\"success\":true}");
            } else {
                request->send(500, "application/json", "{\"error\":\"Failed to update profile\"}");
            }
        }
    });

    // DELETE /api/profiles/{id} - Удалить профиль
    server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)$", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        String id = request->pathArg(0);

        if (deleteProfile(id)) {
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Profile deleted\"}");
        } else {
            request->send(404, "application/json", "{\"error\":\"Profile not found or builtin\"}");
        }
    });

    // POST /api/profiles/{id}/load - Загрузить профиль в текущие настройки
    server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)\\/load$", HTTP_POST, [](AsyncWebServerRequest *request) {
        String id = request->pathArg(0);

        if (applyProfile(id)) {
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Profile loaded\"}");
        } else {
            request->send(404, "application/json", "{\"error\":\"Profile not found\"}");
        }
    });

    // DELETE /api/profiles - Очистить все профили (кроме встроенных)
    server.on("/api/profiles", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (clearProfiles()) {
            request->send(200, "application/json", "{\"success\":true,\"message\":\"All user profiles cleared\"}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to clear profiles\"}");
        }
    });

    // GET /api/profiles/{id}/export - Экспорт одного профиля в JSON
    server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)\\/export$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String id = request->pathArg(0);

        String json = exportProfileToJSON(id);
        if (json.length() > 0) {
            request->send(200, "application/json", json);
        } else {
            request->send(404, "application/json", "{\"error\":\"Profile not found\"}");
        }
    });

    // GET /api/profiles/export - Экспорт всех профилей в JSON
    server.on("/api/profiles/export", HTTP_GET, [](AsyncWebServerRequest *request) {
        bool includeBuiltin = request->hasParam("includeBuiltin") &&
                              request->getParam("includeBuiltin")->value() == "true";

        String json = exportAllProfilesToJSON(includeBuiltin);
        if (json.length() > 0) {
            request->send(200, "application/json", json);
        } else {
            request->send(500, "application/json", "{\"error\":\"Export failed\"}");
        }
    });

    // POST /api/profiles/import - Импорт профилей из JSON
    server.on("/api/profiles/import", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"success\":true}");
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        static String body;

        if (index == 0) {
            body = "";
        }

        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }

        if (index + len == total) {
            // Парсим JSON
            DynamicJsonDocument doc(32768);
            DeserializationError error = deserializeJson(doc, body);

            if (error) {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            uint16_t imported = 0;

            // Проверяем, массив это или один объект
            if (doc.is<JsonArray>()) {
                // Импорт нескольких профилей
                imported = importProfilesFromJSON(body);
            } else if (doc.is<JsonObject>()) {
                // Импорт одного профиля
                String id = importProfileFromJSON(body);
                if (!id.isEmpty()) {
                    imported = 1;
                }
            } else {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON format\"}");
                return;
            }

            String response = "{\"success\":true,\"imported\":" + String(imported) + "}";
            request->send(200, "application/json", response);
        }
    });
}

// Настройка маршрутов для статических файлов
void setupStaticRoutes() {
    // Проверяем, существует ли файловая система и есть ли в ней файлы
    if (!LittleFS.begin(false)) {
        Serial.println("Ошибка монтирования файловой системы LittleFS");
        return;
    }
    
    // Маршрут для главной страницы
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/index.html", "text/html");
    });
    
    // Маршруты для статических файлов
    server.on("/css/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/css/style.css", "text/css");
    });
    
    server.on("/js/main.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/js/main.js", "text/javascript");
    });
    
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/favicon.ico", "image/x-icon");
    });
    
    // Маршрут для статических файлов в папке 'img'
    server.serveStatic("/img/", LittleFS, "/img/");
}

// Обработчик событий WebSocket
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        // Клиент подключился
        Serial.printf("WebSocket клиент #%u подключен с %s\n", client->id(), client->remoteIP().toString().c_str());
        webSocketActive = true;
        
        // Отправляем приветственное сообщение
        client->text("{\"type\":\"welcome\",\"message\":\"Соединение WebSocket установлено\"}");
    } else if (type == WS_EVT_DISCONNECT) {
        // Клиент отключился
        Serial.printf("WebSocket клиент #%u отключен\n", client->id());
        
        // Проверяем, есть ли еще подключенные клиенты
        webSocketActive = (ws.count() > 0);
    } else if (type == WS_EVT_DATA) {
        // Получены данные от клиента
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len) {
            // Сообщение получено полностью
            if (info->opcode == WS_TEXT) {
                // Текстовое сообщение
                data[len] = 0; // Добавляем нулевой символ для безопасного преобразования в строку
                String message = String((char*)data);
                
                // Если установлен обработчик сообщений, вызываем его
                if (webSocketMessageHandler != nullptr) {
                    webSocketMessageHandler(client, message);
                } else {
                    // Базовая обработка команд
                    DynamicJsonDocument doc(512);
                    DeserializationError error = deserializeJson(doc, message);
                    
                    if (!error) {
                        if (doc.containsKey("command")) {
                            String command = doc["command"];
                            
                            if (command == "getStatus") {
                                // Принудительно обновляем статус
                                lastWsUpdate = 0;
                                updateWebSocket();
                            } else if (command == "ping") {
                                // Отвечаем на пинг
                                client->text("{\"type\":\"pong\"}");
                            }
                        }
                    }
                }
            }
        }
    }
}

// Установка интервала обновления WebSocket
void setWebSocketUpdateInterval(int intervalMs) {
    wsUpdateInterval = intervalMs;
}

// Установка обработчика сообщений от клиента
void setWebSocketMessageHandler(void (*callback)(AsyncWebSocketClient*, String)) {
    webSocketMessageHandler = callback;
}

// Отправка сообщения конкретному клиенту WebSocket
void sendWebSocketMessage(AsyncWebSocketClient *client, const String &message) {
    if (client && client->status() == WS_CONNECTED) {
        client->text(message);
    }
}

// Отправка сообщения всем клиентам WebSocket
void broadcastWebSocketMessage(const String &message) {
    ws.textAll(message);
}

// Получение текущего количества подключенных клиентов
int getConnectedClientsCount() {
    return ws.count();
}

// Проверка активности WebSocket соединения
bool isWebSocketActive() {
    return webSocketActive;
}
