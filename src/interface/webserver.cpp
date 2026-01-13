/**
 * Smart-Column S3 - Веб-сервер
 *
 * HTTP server + WebSocket для Web UI
 */

#include "webserver.h"
#include "../config.h"
#include "../fs_compat.h"
#include "../types.h"
#include <AsyncTCP.h>
#include <WiFi.h>

// Определение HTTP методов для ESPAsyncWebServer
#ifndef HTTP_GET
typedef enum {
  HTTP_GET = 0b00000001,
  HTTP_POST = 0b00000010,
  HTTP_DELETE = 0b00000100,
  HTTP_PUT = 0b00001000,
  HTTP_PATCH = 0b00010000,
  HTTP_HEAD = 0b00100000,
  HTTP_OPTIONS = 0b01000000,
  HTTP_ANY = 0b01111111,
} WebRequestMethod;
#endif

#include "control/fsm.h"
#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/valves.h"
#include "drivers/sensors.h"
#include "storage/nvs_manager.h"
#include "cloud_tunnel.h"
#include "../profiles.h"
#include <ArduinoJson.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <Update.h>


// Внешние переменные из main.cpp
extern SystemState g_state;
extern Settings g_settings;
extern EnergyHistory g_energyHistory;

static AsyncWebServer server(WEB_SERVER_PORT);
static AsyncWebSocket ws("/ws");

// Вспомогательные функции для строковых представлений
static const char *getModeString(Mode mode) {
  switch (mode) {
  case Mode::IDLE:
    return "idle";
  case Mode::RECTIFICATION:
    return "rectification";
  case Mode::DISTILLATION:
    return "distillation";
  case Mode::MANUAL_RECT:
    return "manual";
  case Mode::MASHING:
    return "mashing";
  case Mode::HOLD:
    return "hold";
  default:
    return "unknown";
  }
}

static const char *getPhaseString(RectPhase phase) {
  switch (phase) {
  case RectPhase::IDLE:
    return "idle";
  case RectPhase::HEATING:
    return "heating";
  case RectPhase::STABILIZATION:
    return "stabilization";
  case RectPhase::HEADS:
    return "heads";
  case RectPhase::POST_HEADS_STABILIZATION:
    return "post_heads";
  case RectPhase::BODY:
    return "body";
  case RectPhase::TAILS:
    return "tails";
  case RectPhase::PURGE:
    return "purge";
  case RectPhase::FINISH:
    return "finish";
  case RectPhase::COMPLETED:
    return "completed";
  default:
    return "unknown";
  }
}

static const char *getMashPhaseString(MashPhase phase) {
  switch (phase) {
  case MashPhase::IDLE:
    return "idle";
  case MashPhase::ACID_REST:
    return "acid_rest";
  case MashPhase::PROTEIN_REST:
    return "protein_rest";
  case MashPhase::BETA_AMYLASE:
    return "beta_amylase";
  case MashPhase::ALPHA_AMYLASE:
    return "alpha_amylase";
  case MashPhase::MASH_OUT:
    return "mash_out";
  case MashPhase::FINISH:
    return "finish";
  default:
    return "unknown";
  }
}

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
  // GET /api/status - полное состояние системы
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<2048> doc;

    // Режим и состояние процесса
    doc["mode"] = static_cast<int>(g_state.mode);
    doc["modeStr"] = getModeString(g_state.mode);
    doc["phase"] = static_cast<int>(g_state.rectPhase);
    doc["phaseStr"] = getPhaseString(g_state.rectPhase);
    doc["paused"] = g_state.paused;
    doc["safetyOk"] = g_state.safetyOk;
    doc["uptime"] = g_state.uptime;
    doc["deviceId"] = CloudTunnel::getDeviceId();

    // Температуры
    JsonObject temps = doc.createNestedObject("temps");
    temps["cube"] = g_state.temps.cube;
    temps["columnBottom"] = g_state.temps.columnBottom;
    temps["columnTop"] = g_state.temps.columnTop;
    temps["reflux"] = g_state.temps.reflux;
    temps["deflegmator"] = g_state.temps.deflegmator;
    temps["product"] = g_state.temps.product;
    temps["tsa"] = g_state.temps.tsa;
    temps["waterIn"] = g_state.temps.waterIn;
    temps["waterOut"] = g_state.temps.waterOut;

    // Давление
    JsonObject pressure = doc.createNestedObject("pressure");
    pressure["cube"] = g_state.pressure.cube;
    pressure["atm"] = g_state.pressure.atmosphere;
    pressure["kpa"] = g_state.pressure.pressure;

    // Мощность (PZEM-004T)
    JsonObject power = doc.createNestedObject("power");
    power["voltage"] = g_state.power.voltage;
    power["current"] = g_state.power.current;
    power["power"] = g_state.power.power;
    power["energy"] = g_state.power.energy;
    power["frequency"] = g_state.power.frequency;
    power["pf"] = g_state.power.powerFactor;

    // Насос
    JsonObject pump = doc.createNestedObject("pump");
    pump["speedMlH"] = g_state.pump.speedMlPerHour;
    pump["totalMl"] = g_state.pump.totalVolumeMl;
    pump["running"] = g_state.pump.running;

    // Ареометр
    JsonObject hydro = doc.createNestedObject("hydrometer");
    hydro["abv"] = g_state.hydrometer.abv;
    hydro["density"] = g_state.hydrometer.density;
    hydro["valid"] = g_state.hydrometer.valid;

    // Объёмы фракций
    JsonObject volumes = doc.createNestedObject("volumes");
    volumes["heads"] = g_state.stats.headsVolume;
    volumes["body"] = g_state.stats.bodyVolume;
    volumes["tails"] = g_state.stats.tailsVolume;

    // Настройки оборудования (для UI)
    JsonObject equipment = doc.createNestedObject("equipment");
    equipment["heaterPowerW"] = g_settings.equipment.heaterPowerW;
    equipment["columnHeightMm"] = g_settings.equipment.columnHeightMm;

    // Cloud tunnel status (локально полезно для привязки)
    JsonObject cloud = doc.createNestedObject("cloud");
    cloud["enabled"] = g_settings.cloud.enabled;
    cloud["tunnelUrl"] = g_settings.cloud.tunnelUrl;
    cloud["connected"] = CloudTunnel::isConnected();
    cloud["authenticated"] = CloudTunnel::isAuthenticated();
    cloud["claimActive"] = CloudTunnel::hasActiveClaim();
    if (CloudTunnel::hasActiveClaim()) {
      cloud["claimCode"] = CloudTunnel::getClaimCode();
      cloud["claimExpiresAt"] = CloudTunnel::getClaimExpiresAt();
    }

    // -----------------------------------------------------------------------
    // Режимы с температурными ступенями (mashing / hold)
    // -----------------------------------------------------------------------
    const uint32_t now = millis();

    JsonObject mashing = doc.createNestedObject("mashing");
    mashing["active"] = g_state.mashing.active;
    mashing["phase"] = static_cast<int>(g_state.mashing.phase);
    mashing["phaseStr"] = getMashPhaseString(g_state.mashing.phase);
    mashing["stepCount"] = g_state.mashing.stepCount;
    mashing["currentStep"] = g_state.mashing.currentStep;
    mashing["targetTemp"] = g_state.mashing.targetTemp;
    mashing["stepDurationSec"] = g_state.mashing.stepDuration;
    mashing["tempInRange"] = g_state.mashing.tempInRange;
    mashing["stepName"] = g_state.mashing.stepName;

    uint32_t mashElapsedSec = 0;
    if (g_state.mashing.tempInRange && g_state.mashing.inRangeStartTime > 0 &&
        now >= g_state.mashing.inRangeStartTime) {
      mashElapsedSec = (now - g_state.mashing.inRangeStartTime) / 1000UL;
    }
    mashing["elapsedSec"] = mashElapsedSec;
    mashing["remainingSec"] =
        (g_state.mashing.stepDuration > mashElapsedSec)
            ? (g_state.mashing.stepDuration - mashElapsedSec)
            : 0;

    JsonObject hold = doc.createNestedObject("hold");
    hold["active"] = g_state.hold.active;
    hold["stepCount"] = g_state.hold.stepCount;
    hold["currentStep"] = g_state.hold.currentStep;
    hold["targetTemp"] = g_state.hold.targetTemp;
    hold["tempInRange"] = g_state.hold.tempInRange;

    uint32_t holdStepDurationSec = 0;
    if (g_state.hold.stepCount > 0 && g_state.hold.currentStep < g_state.hold.stepCount) {
      holdStepDurationSec = (uint32_t)g_state.hold.steps[g_state.hold.currentStep].duration * 60UL;
    }
    hold["stepDurationSec"] = holdStepDurationSec;

    uint32_t holdElapsedSec = 0;
    if (g_state.hold.tempInRange && g_state.hold.inRangeStartTime > 0 &&
        now >= g_state.hold.inRangeStartTime) {
      holdElapsedSec = (now - g_state.hold.inRangeStartTime) / 1000UL;
    }
    hold["elapsedSec"] = holdElapsedSec;
    hold["remainingSec"] =
        (holdStepDurationSec > holdElapsedSec) ? (holdStepDurationSec - holdElapsedSec) : 0;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
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

  // GET /api/version - получить информацию о версиях прошивки и фронтенда
  server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<768> doc;

    // Версия и дата компиляции прошивки
    JsonObject firmware = doc["firmware"].to<JsonObject>();
    firmware["version"] = FIRMWARE_VERSION;
    firmware["buildDate"] = __DATE__;
    firmware["buildTime"] = __TIME__;
    firmware["compiler"] = "GCC " __VERSION__;

    // Информация о плате
    JsonObject board = doc["board"].to<JsonObject>();
    board["chip"] = "ESP32-S3";
    board["flashSize"] = ESP.getFlashChipSize();
    board["psramSize"] = ESP.getPsramSize();
    board["cpuFreq"] = ESP.getCpuFreqMHz();
    board["mac"] = WiFi.macAddress();

    // Стабильный публичный идентификатор устройства (для облака/привязки).
    // Важно: это НЕ секрет и не должен использоваться как "токен".
    char deviceId[13] = {0}; // 12 hex + '\0'
    uint64_t efuseMac = ESP.getEfuseMac();
    snprintf(deviceId, sizeof(deviceId), "%012llX",
             (unsigned long long)(efuseMac & 0xFFFFFFFFFFFFULL));
    board["deviceId"] = deviceId;

// Попытка прочитать версию фронтенда из файла
#ifdef USE_LITTLEFS
    File versionFile = LittleFS.open("/version.json", "r");
#else
            File versionFile = SPIFFS.open("/version.json", "r");
#endif

    if (versionFile) {
      StaticJsonDocument<256> frontendDoc;
      DeserializationError error = deserializeJson(frontendDoc, versionFile);
      versionFile.close();

      if (!error) {
        doc["frontend"] = frontendDoc.as<JsonObject>();
      } else {
        JsonObject frontend = doc["frontend"].to<JsonObject>();
        frontend["error"] = "Failed to parse version.json";
      }
    } else {
      JsonObject frontend = doc["frontend"].to<JsonObject>();
      frontend["buildDate"] = "Unknown";
      frontend["note"] = "version.json not found";
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/process/start - запуск процесса
  server.on(
      "/api/process/start", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        // Ждем получения всех данных
        if (index + len != total) {
          return;
        }

        // Важно: сюда могут приходить steps/profiles, поэтому нужен запас
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          LOG_E("Process start: JSON parse error");
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Invalid JSON\"}");
          return;
        }

        const char *modeStr = doc["mode"];
        if (!modeStr) {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Mode required\"}");
          return;
        }

        JsonObject params = doc["params"].as<JsonObject>();

        // Определяем режим
        Mode mode = Mode::IDLE;
        if (strcmp(modeStr, "rectification") == 0) {
          mode = Mode::RECTIFICATION;
        } else if (strcmp(modeStr, "distillation") == 0) {
          mode = Mode::DISTILLATION;
        } else if (strcmp(modeStr, "manual") == 0 ||
                   strcmp(modeStr, "manual_rect") == 0) {
          mode = Mode::MANUAL_RECT;
        } else if (strcmp(modeStr, "mashing") == 0) {
          mode = Mode::MASHING;
        } else if (strcmp(modeStr, "hold") == 0) {
          mode = Mode::HOLD;
        } else {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Unknown mode\"}");
          return;
        }

        // Проверка термометров (только предупреждение, не блокируем запуск)
        bool sensorsOk =
            g_state.health.tempSensorsTotal > 0 && g_state.health.tempSensorsOk;

        if (!sensorsOk) {
          LOG_W("Starting process without temperature sensors!");
        }

        // Если уже что-то запущено — сначала остановим
        if (g_state.mode != Mode::IDLE) {
          FSM::stopMode(g_state);
        }

        // Запуск через FSM + разбор params (для некоторых режимов)
        if (mode == Mode::DISTILLATION) {
          // params: speed (ml/h), headsVolume (ml), targetVolume (ml), endTemp (°C)
          float speed = params["speed"] | 500.0f;
          float headsVol = params["headsVolume"] | 0.0f;
          float targetVol = params["targetVolume"] | 0.0f;
          float endTemp = params["endTemp"] | 96.0f;
          FSM::Distillation::setParams(speed, headsVol, targetVol, endTemp);
          FSM::startMode(g_state, g_settings, mode);
        } else if (mode == Mode::MASHING) {
          // params.profile: { name, steps:[{temperature,duration,name?}, ...] }
          static MashProfile runtimeProfile;
          memset(&runtimeProfile, 0, sizeof(runtimeProfile));

          bool hasProfile = false;
          if (!params.isNull() && params.containsKey("profile")) {
            JsonObject profileObj = params["profile"].as<JsonObject>();
            JsonArray steps = profileObj["steps"].as<JsonArray>();
            if (!profileObj.isNull() && steps.size() > 0) {
              const char *pName = profileObj["name"] | "Mashing";
              strncpy(runtimeProfile.name, pName, sizeof(runtimeProfile.name) - 1);
              runtimeProfile.name[sizeof(runtimeProfile.name) - 1] = '\0';

              uint8_t count = 0;
              for (JsonObject s : steps) {
                if (count >= 10) break;
                runtimeProfile.steps[count].temperature = s["temperature"] | 0.0f;
                runtimeProfile.steps[count].duration = s["duration"] | 0;
                const char *sName = s["name"] | "";
                strncpy(runtimeProfile.steps[count].name, sName,
                        sizeof(runtimeProfile.steps[count].name) - 1);
                runtimeProfile.steps[count].name[sizeof(runtimeProfile.steps[count].name) - 1] = '\0';
                count++;
              }
              runtimeProfile.stepCount = count;
              hasProfile = (count > 0);
            }
          }

          // Если профиль не передан — используем разумный дефолт
          if (!hasProfile) {
            strncpy(runtimeProfile.name, "Default Mashing",
                    sizeof(runtimeProfile.name) - 1);
            runtimeProfile.stepCount = 5;
            // NB: у вложенной структуры нет operator= от initializer_list в GCC,
            // поэтому заполняем поля вручную
            runtimeProfile.steps[0].temperature = 38.0f;
            runtimeProfile.steps[0].duration = 20;
            strncpy(runtimeProfile.steps[0].name, "Кислотная пауза",
                    sizeof(runtimeProfile.steps[0].name) - 1);

            runtimeProfile.steps[1].temperature = 52.0f;
            runtimeProfile.steps[1].duration = 20;
            strncpy(runtimeProfile.steps[1].name, "Белковая пауза",
                    sizeof(runtimeProfile.steps[1].name) - 1);

            runtimeProfile.steps[2].temperature = 63.0f;
            runtimeProfile.steps[2].duration = 40;
            strncpy(runtimeProfile.steps[2].name, "Мальтозная пауза",
                    sizeof(runtimeProfile.steps[2].name) - 1);

            runtimeProfile.steps[3].temperature = 72.0f;
            runtimeProfile.steps[3].duration = 20;
            strncpy(runtimeProfile.steps[3].name, "Осахаривание",
                    sizeof(runtimeProfile.steps[3].name) - 1);

            runtimeProfile.steps[4].temperature = 78.0f;
            runtimeProfile.steps[4].duration = 10;
            strncpy(runtimeProfile.steps[4].name, "Мэш-аут",
                    sizeof(runtimeProfile.steps[4].name) - 1);

            // гарантируем '\0' для всех name
            for (uint8_t i = 0; i < runtimeProfile.stepCount; i++) {
              runtimeProfile.steps[i].name[sizeof(runtimeProfile.steps[i].name) - 1] = '\0';
            }
          }

          FSM::Mashing::start(g_state, &runtimeProfile);
        } else if (mode == Mode::HOLD) {
          // params.steps: [{temperature, duration}, ...] (duration в минутах)
          static TempStep runtimeSteps[10];
          uint8_t count = 0;
          if (!params.isNull() && params.containsKey("steps")) {
            JsonArray steps = params["steps"].as<JsonArray>();
            for (JsonObject s : steps) {
              if (count >= 10) break;
              runtimeSteps[count].temperature = s["temperature"] | 0.0f;
              runtimeSteps[count].duration = s["duration"] | 0;
              count++;
            }
          }

          // Дефолт — одна ступень 65°C на 60 минут
          if (count == 0) {
            runtimeSteps[0].temperature = 65.0f;
            runtimeSteps[0].duration = 60;
            count = 1;
          }

          FSM::Hold::start(g_state, runtimeSteps, count);
        } else {
          FSM::startMode(g_state, g_settings, mode);
        }

        LOG_I("Process started: mode=%s, sensors=%s", modeStr,
              sensorsOk ? "OK" : "WARNING");

        // Формируем ответ
        String response = "{\"success\":true,\"message\":\"Process started\"";
        if (!sensorsOk) {
          response += ",\"warning\":\"No temperature sensors detected\"";
        }
        response += "}";

        request->send(200, "application/json", response);
      });

  // POST /api/process/stop - остановка процесса
  server.on("/api/process/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    FSM::stopMode(g_state);
    LOG_I("Process stopped via API");
    request->send(200, "application/json",
                  "{\"success\":true,\"message\":\"Process stopped\"}");
  });

  // POST /api/process/pause - пауза
  server.on(
      "/api/process/pause", HTTP_POST, [](AsyncWebServerRequest *request) {
        FSM::pause(g_state);
        LOG_I("Process paused via API");
        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"Process paused\"}");
      });

  // POST /api/process/resume - возобновление
  server.on(
      "/api/process/resume", HTTP_POST, [](AsyncWebServerRequest *request) {
        FSM::resume(g_state);
        LOG_I("Process resumed via API");
        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"Process resumed\"}");
      });

  // ==========================================================================
  // MANUAL CONTROL API (для manual.html)
  // ==========================================================================

  // --------------------------------------------------------------------------
  // CLOUD TUNNEL API (локально, для привязки устройства в облако)
  // --------------------------------------------------------------------------

  // POST /api/cloud/claim - сгенерировать новый PIN для привязки
  server.on(
      "/api/cloud/claim", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        uint32_t ttl = 600;
        if (len > 0) {
          StaticJsonDocument<256> doc;
          if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            ttl = doc["ttlSeconds"] | 600;
            if (ttl < 60) ttl = 60;
            if (ttl > 3600) ttl = 3600;
          }
        }

        CloudTunnel::generateClaim(ttl);

        StaticJsonDocument<256> out;
        out["success"] = true;
        out["deviceId"] = CloudTunnel::getDeviceId();
        out["claimCode"] = CloudTunnel::getClaimCode();
        out["claimExpiresAt"] = CloudTunnel::getClaimExpiresAt();

        String json;
        serializeJson(out, json);
        request->send(200, "application/json", json);
      });

  // POST /api/cloud/config - включить/выключить облако и задать URL туннеля
  server.on(
      "/api/cloud/config", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
         size_t total) {
        if (index + len != total) return;

        StaticJsonDocument<384> doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        bool enabled = doc["enabled"] | g_settings.cloud.enabled;
        const char* url = doc["tunnelUrl"] | g_settings.cloud.tunnelUrl;

        g_settings.cloud.enabled = enabled;
        strlcpy(g_settings.cloud.tunnelUrl, url, sizeof(g_settings.cloud.tunnelUrl));

        NVSManager::saveSettings(g_settings);

        request->send(200, "application/json", "{\"success\":true}");
      });

  // POST /api/manual/heater - установить мощность ТЭНа (0-100%)
  server.on(
      "/api/manual/heater", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
          return;
        }

        int power = doc["power"] | 0;
        if (power < 0) power = 0;
        if (power > 100) power = 100;
        Heater::setPower((uint8_t)power);

        char resp[96];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"power\":%d}", power);
        request->send(200, "application/json", resp);
      });

  // POST /api/manual/pump - установить скорость насоса (мл/ч; 0 = стоп)
  server.on(
      "/api/manual/pump", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
          return;
        }

        float speed = doc["speed"] | 0.0f;
        if (speed <= 0.0f) {
          Pump::stop();
          request->send(200, "application/json",
                        "{\"success\":true,\"running\":false}");
          return;
        }

        Pump::start(speed);
        char resp[128];
        snprintf(resp, sizeof(resp),
                 "{\"success\":true,\"running\":true,\"speed\":%.1f}", speed);
        request->send(200, "application/json", resp);
      });

  // POST /api/manual/valves - управление клапанами
  // body: { "water":true/false, "heads":true/false, "uno":true/false, "allOff":true }
  server.on(
      "/api/manual/valves", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        StaticJsonDocument<192> doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
          return;
        }

        bool allOff = doc["allOff"] | false;
        if (allOff) {
          Valves::closeAll();
          request->send(200, "application/json", "{\"success\":true}");
          return;
        }

        if (doc.containsKey("water")) {
          Valves::setWater(doc["water"].as<bool>());
        }
        if (doc.containsKey("heads")) {
          Valves::setHeads(doc["heads"].as<bool>());
        }
        if (doc.containsKey("uno")) {
          Valves::setUno(doc["uno"].as<bool>());
        }

        request->send(200, "application/json", "{\"success\":true}");
      });

  // ==========================================================================
  // ДЕМО-РЕЖИМ
  // ==========================================================================

  // POST /api/settings/demo - включить/выключить демо-режим
  server.on(
      "/api/settings/demo", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        StaticJsonDocument<128> doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Invalid JSON\"}");
          return;
        }

        bool enabled = doc["enabled"] | false;
        g_settings.demoMode = enabled;

        LOG_I("Demo mode %s", enabled ? "ENABLED" : "DISABLED");

        // Сохраняем в NVS
        Preferences prefs;
        prefs.begin("settings", false);
        prefs.putBool("demoMode", enabled);
        prefs.end();

        char response[128];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"demoMode\":%s}",
                 enabled ? "true" : "false");
        request->send(200, "application/json", response);
      });

  // GET /api/settings/demo - получить состояние демо-режима
  server.on("/api/settings/demo", HTTP_GET, [](AsyncWebServerRequest *request) {
    char response[64];
    snprintf(response, sizeof(response), "{\"demoMode\":%s}",
             g_settings.demoMode ? "true" : "false");
    request->send(200, "application/json", response);
  });

  // ==========================================================================
  // ПЕРЕЗАГРУЗКА
  // ==========================================================================

  // POST /api/reboot - перезагрузка контроллера
  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    LOG_W("Reboot requested via API");
    request->send(200, "application/json",
                  "{\"success\":true,\"message\":\"Rebooting...\"}");

    // Небольшая задержка чтобы успеть отправить ответ
    delay(500);
    ESP.restart();
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
      snprintf(addrStr, sizeof(addrStr),
               "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
               g_settings.tempCal.addresses[i][0],
               g_settings.tempCal.addresses[i][1],
               g_settings.tempCal.addresses[i][2],
               g_settings.tempCal.addresses[i][3],
               g_settings.tempCal.addresses[i][4],
               g_settings.tempCal.addresses[i][5],
               g_settings.tempCal.addresses[i][6],
               g_settings.tempCal.addresses[i][7]);
      t["address"] = addrStr;

      // Текущие показания
      float currentTemp = 0;
      switch (i) {
      case TEMP_CUBE:
        currentTemp = g_state.temps.cube;
        break;
      case TEMP_COLUMN_BOTTOM:
        currentTemp = g_state.temps.columnBottom;
        break;
      case TEMP_COLUMN_TOP:
        currentTemp = g_state.temps.columnTop;
        break;
      case TEMP_REFLUX:
        currentTemp = g_state.temps.reflux;
        break;
      case TEMP_TSA:
        currentTemp = g_state.temps.tsa;
        break;
      case TEMP_WATER_IN:
        currentTemp = g_state.temps.waterIn;
        break;
      case TEMP_WATER_OUT:
        currentTemp = g_state.temps.waterOut;
        break;
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
    hydro["currentPressure"] =
        g_state.hydrometer.density; // TODO: использовать реальное давление
    hydro["currentABV"] = g_state.hydrometer.abv;
    hydro["valid"] = g_state.hydrometer.valid;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/calibration/pump - калибровка насоса
  server.on(
      "/api/calibration/pump", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid JSON\"}");
          return;
        }

        // Метод 1: Прямая калибровка (мл на оборот и шаги)
        if (doc.containsKey("mlPerRev") || doc.containsKey("stepsPerRev")) {
          if (doc.containsKey("mlPerRev")) {
            g_settings.pumpCal.mlPerRevolution = doc["mlPerRev"].as<float>();
            LOG_I("Pump mlPerRev: %.3f", g_settings.pumpCal.mlPerRevolution);
          }
          if (doc.containsKey("stepsPerRev")) {
            g_settings.pumpCal.stepsPerRevolution =
                doc["stepsPerRev"].as<uint16_t>();
            LOG_I("Pump stepsPerRev: %u",
                  g_settings.pumpCal.stepsPerRevolution);
          }
          NVSManager::saveSettings(g_settings);
          request->send(200, "application/json",
                        "{\"status\":\"ok\",\"method\":\"direct\"}");
          return;
        }

        // Метод 2: Калибровка по известному объёму
        if (doc.containsKey("knownVolume") && doc.containsKey("steps")) {
          float knownVolume = doc["knownVolume"].as<float>(); // мл
          uint32_t steps = doc["steps"].as<uint32_t>();       // шагов выполнено

          uint16_t stepsPerRev = g_settings.pumpCal.stepsPerRevolution *
                                 g_settings.pumpCal.microsteps;
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
            request->send(400, "application/json",
                          "{\"error\":\"Invalid steps\"}");
          }
          return;
        }

        request->send(400, "application/json",
                      "{\"error\":\"Missing parameters\"}");
      });

  // POST /api/calibration/temp - калибровка термометров
  server.on(
      "/api/calibration/temp", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid JSON\"}");
          return;
        }

        uint8_t sensorIndex = doc["index"].as<uint8_t>();

        if (sensorIndex >= TEMP_COUNT) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid sensor index\"}");
          return;
        }

        // Метод 1: Прямое смещение
        if (doc.containsKey("offset")) {
          g_settings.tempCal.offsets[sensorIndex] = doc["offset"].as<float>();

          // Применить калибровку к драйверу
          Sensors::applyCalibration(g_settings.tempCal);
          NVSManager::saveSettings(g_settings);

          LOG_I("Temp[%d] calibrated: offset = %.2f°C", sensorIndex,
                g_settings.tempCal.offsets[sensorIndex]);

          request->send(200, "application/json",
                        "{\"status\":\"ok\",\"method\":\"offset\"}");
          return;
        }

        // Метод 2: Калибровка по эталону
        if (doc.containsKey("reference")) {
          float reference =
              doc["reference"].as<float>(); // Эталонная температура

          // Прочитать текущее значение
          float currentTemp = 0;
          switch (sensorIndex) {
          case TEMP_CUBE:
            currentTemp = g_state.temps.cube;
            break;
          case TEMP_COLUMN_BOTTOM:
            currentTemp = g_state.temps.columnBottom;
            break;
          case TEMP_COLUMN_TOP:
            currentTemp = g_state.temps.columnTop;
            break;
          case TEMP_REFLUX:
            currentTemp = g_state.temps.reflux;
            break;
          case TEMP_TSA:
            currentTemp = g_state.temps.tsa;
            break;
          case TEMP_WATER_IN:
            currentTemp = g_state.temps.waterIn;
            break;
          case TEMP_WATER_OUT:
            currentTemp = g_state.temps.waterOut;
            break;
          }

          // Вычислить смещение (без учёта старого смещения)
          float rawTemp = currentTemp - g_settings.tempCal.offsets[sensorIndex];
          g_settings.tempCal.offsets[sensorIndex] = reference - rawTemp;

          Sensors::applyCalibration(g_settings.tempCal);
          NVSManager::saveSettings(g_settings);

          LOG_I("Temp[%d] calibrated to %.2f°C: offset = %.2f°C", sensorIndex,
                reference, g_settings.tempCal.offsets[sensorIndex]);

          StaticJsonDocument<128> resp;
          resp["status"] = "ok";
          resp["method"] = "reference";
          resp["offset"] = g_settings.tempCal.offsets[sensorIndex];

          String json;
          serializeJson(resp, json);
          request->send(200, "application/json", json);
          return;
        }

        request->send(400, "application/json",
                      "{\"error\":\"Missing parameters\"}");
      });

  // POST /api/calibration/hydrometer - калибровка ареометра
  server.on(
      "/api/calibration/hydrometer", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid JSON\"}");
          return;
        }

        // Проверка наличия массивов калибровочных точек
        if (!doc.containsKey("abvPoints") ||
            !doc.containsKey("pressurePoints")) {
          request->send(400, "application/json",
                        "{\"error\":\"Missing abvPoints or pressurePoints\"}");
          return;
        }

        JsonArray abvArray = doc["abvPoints"].as<JsonArray>();
        JsonArray pressureArray = doc["pressurePoints"].as<JsonArray>();

        if (abvArray.size() != pressureArray.size() || abvArray.size() > 5) {
          request->send(
              400, "application/json",
              "{\"error\":\"Invalid point count (max 5, must match)\"}");
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
      });

  // GET /api/calibration/scan - сканирование DS18B20
  server.on("/api/calibration/scan", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              uint8_t addresses[TEMP_COUNT][8];
              uint8_t count = Sensors::scanDS18B20(addresses);

              StaticJsonDocument<768> doc;
              doc["count"] = count;

              JsonArray sensors = doc.createNestedArray("sensors");
              for (uint8_t i = 0; i < count; i++) {
                JsonObject s = sensors.createNestedObject();

                char addrStr[24];
                snprintf(addrStr, sizeof(addrStr),
                         "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                         addresses[i][0], addresses[i][1], addresses[i][2],
                         addresses[i][3], addresses[i][4], addresses[i][5],
                         addresses[i][6], addresses[i][7]);

                s["index"] = i;
                s["address"] = addrStr;
                s["valid"] = Sensors::isTempSensorValid(i);
              }

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  // ==========================================================================
  // PUMP CONTROL (для калибровки)
  // ==========================================================================

  // POST /api/pump/start - запуск насоса
  server.on(
      "/api/pump/start", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) {
          return;
        }

        StaticJsonDocument<128> doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        float speed = doc["speed"] | 0.0f;

        if (speed <= 0) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Speed must be > 0\"}");
          return;
        }

        Pump::start(speed);
        LOG_I("Pump started via API at %.1f ml/h", speed);

        char response[128];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"speed\":%.1f}", speed);
        request->send(200, "application/json", response);
      });

  // POST /api/pump/stop - остановка насоса
  server.on("/api/pump/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    Pump::stop();
    LOG_I("Pump stopped via API");
    request->send(200, "application/json",
                  "{\"success\":true,\"message\":\"Pump stopped\"}");
  });

  // GET /api/pump/status - статус насоса
  server.on("/api/pump/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<128> doc;
    doc["running"] = Pump::isRunning();
    doc["speed"] = Pump::getSpeed();
    doc["totalVolume"] = Pump::getTotalVolume();

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

      const EnergyDataPoint &point = g_energyHistory.points[index];

      JsonObject obj = dataArray.createNestedObject();
      obj["t"] = point.timestamp;
      obj["p"] = round(point.power * 10) / 10;      // 1 знак после запятой
      obj["e"] = round(point.energy * 1000) / 1000; // 3 знака
      obj["v"] = round(point.voltage * 10) / 10;
      obj["i"] = round(point.current * 100) / 100; // 2 знака
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
      net["encryption"] =
          (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "secured";
      net["channel"] = WiFi.channel(i);
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);

    WiFi.scanDelete(); // Очистить результаты сканирования
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
  server.on(
      "/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        // Обрабатываем только когда получены все данные
        if (index + len != total) {
          return; // Ждем остальные chunks
        }

        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          LOG_E("WiFi: JSON parse error: %s", error.c_str());
          request->send(400, "application/json",
                        "{\"error\":\"Invalid JSON\"}");
          return;
        }

        const char *ssid = doc["ssid"];
        const char *password = doc["password"];

        if (!ssid || strlen(ssid) == 0) {
          request->send(400, "application/json",
                        "{\"error\":\"SSID required\"}");
          return;
        }

        LOG_I("WiFi: Connect request for SSID: %s", ssid);

        // Сохранить в настройки
        strncpy(g_settings.wifi.ssid, ssid, sizeof(g_settings.wifi.ssid) - 1);
        g_settings.wifi.ssid[sizeof(g_settings.wifi.ssid) - 1] = '\0';

        strncpy(g_settings.wifi.password, password ? password : "",
                sizeof(g_settings.wifi.password) - 1);
        g_settings.wifi.password[sizeof(g_settings.wifi.password) - 1] = '\0';

        g_settings.wifi.apMode = false;

        // Сохранить в NVS
        if (NVSManager::saveSettings(g_settings)) {
          LOG_I("WiFi: Settings saved, connecting to %s", ssid);

          // Отправить ответ перед переподключением
          request->send(200, "application/json",
                        "{\"status\":\"connecting\",\"message\":\"Connecting "
                        "to WiFi, please wait...\"}");

          // Попытка подключения через небольшую задержку
          // чтобы ответ успел уйти клиенту
          delay(100);

          WiFi.disconnect();
          WiFi.mode(WIFI_STA);
          WiFi.begin(g_settings.wifi.ssid, g_settings.wifi.password);
        } else {
          LOG_E("WiFi: Failed to save settings to NVS");
          request->send(500, "application/json",
                        "{\"error\":\"Failed to save settings\"}");
        }
      });

  // ==========================================================================
  // OTA UPDATE (Web UI)
  // ==========================================================================

  // GET /update - страница загрузки прошивки
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = F(
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'><meta name='viewport' "
        "content='width=device-width,initial-scale=1'>"
        "<title>Smart-Column S3 - OTA Update</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;max-width:600px;margin:50px "
        "auto;padding:20px;background:#f5f5f5}"
        ".container{background:white;padding:30px;border-radius:10px;box-"
        "shadow:0 2px 10px rgba(0,0,0,0.1)}"
        "h1{color:#333;margin-bottom:20px}"
        ".info{background:#e3f2fd;padding:15px;border-radius:5px;margin-bottom:"
        "20px}"
        "input[type=file]{width:100%;padding:10px;margin:10px 0;border:2px "
        "dashed #ccc;border-radius:5px;cursor:pointer}"
        "input[type=submit]{background:#4CAF50;color:white;padding:15px "
        "30px;border:none;border-radius:5px;cursor:pointer;font-size:16px;"
        "width:100%}"
        "input[type=submit]:hover{background:#45a049}"
        ".progress{display:none;margin-top:20px}"
        ".progress-bar{width:100%;height:30px;background:#ddd;border-radius:"
        "15px;overflow:hidden}"
        ".progress-fill{height:100%;background:#4CAF50;transition:width 0.3s}"
        ".status{margin-top:15px;padding:10px;border-radius:5px;text-align:"
        "center}"
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
        "<form method='POST' action='/update' enctype='multipart/form-data' "
        "id='upload_form'>"
        "<input type='file' name='update' accept='.bin' required>"
        "<input type='submit' value='Upload Firmware'>"
        "</form>"
        "<div class='progress' id='progress'>"
        "<div class='progress-bar'><div class='progress-fill' "
        "id='progress-fill'></div></div>"
        "<div id='status'></div>"
        "</div>"
        "</div>"
        "<script>"
        "document.getElementById('upload_form').addEventListener('submit',"
        "function(e){"
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
        "document.getElementById('status').textContent='✓ Update successful! "
        "Rebooting...';"
        "setTimeout(function(){location.href='/';},5000);"
        "}else{"
        "document.getElementById('status').className='status error';"
        "document.getElementById('status').textContent='✗ Update failed: "
        "'+xhr.responseText;"
        "}"
        "});"
        "xhr.open('POST','/update');"
        "xhr.send(formData);"
        "});"
        "</script>"
        "</body></html>");
    request->send(200, "text/html", html);
  });

  // POST /update - загрузка и установка прошивки
  server.on(
      "/update", HTTP_POST,
      // Обработчик завершения загрузки
      [](AsyncWebServerRequest *request) {
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(
            200, "text/plain", shouldReboot ? "OK" : "FAIL");
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
      [](AsyncWebServerRequest *request, String filename, size_t index,
         uint8_t *data, size_t len, bool final) {
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
      });

  // ==========================================================================
  // PROFILES API
  // ==========================================================================

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

  // POST /api/profiles/{id}/load - Загрузить профиль в текущие настройки
  server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)\\/load$", HTTP_POST, [](AsyncWebServerRequest *request) {
    String id = request->pathArg(0);
    
    if (applyProfile(id)) {
      request->send(200, "application/json", "{\"success\":true,\"message\":\"Profile loaded\"}");
    } else {
      request->send(404, "application/json", "{\"error\":\"Profile not found\"}");
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

  // 404
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not Found");
  });

  server.begin();
  LOG_I("WebServer: Started on port %d", WEB_SERVER_PORT);
}

void broadcastState(const SystemState &state) {
  if (ws.count() == 0)
    return;

  // Сформировать JSON со состоянием
  StaticJsonDocument<3072> doc;

  doc["mode"] = static_cast<int>(state.mode);
  doc["phase"] = static_cast<int>(state.rectPhase);
  doc["uptime"] = state.uptime;

  // Все температуры
  doc["t_cube"] = state.temps.cube;
  doc["t_column_bottom"] = state.temps.columnBottom;
  doc["t_column_top"] = state.temps.columnTop;
  doc["t_reflux"] = state.temps.reflux;
  doc["t_tsa"] = state.temps.tsa;
  doc["t_water_in"] = state.temps.waterIn;
  doc["t_water_out"] = state.temps.waterOut;

  // Давление
  doc["p_cube"] = state.pressure.cube;
  doc["p_atm"] = state.pressure.atmosphere;

  // Мощность (PZEM-004T)
  doc["voltage"] = state.power.voltage;
  doc["current"] = state.power.current;
  doc["power"] = state.power.power;
  doc["energy"] = state.power.energy;
  doc["frequency"] = state.power.frequency;
  doc["pf"] = state.power.powerFactor;

  // Насос
  doc["pump_speed"] = state.pump.speedMlPerHour;
  doc["pump_volume"] = state.pump.totalVolumeMl;
  doc["speed"] = state.pump.speedMlPerHour;
  doc["volume"] = state.pump.totalVolumeMl;

  // Ареометр
  doc["abv"] = state.hydrometer.abv;

  // -----------------------------------------------------------------------
  // Режимы с температурными ступенями (mashing / hold)
  // -----------------------------------------------------------------------
  const uint32_t now = millis();

  JsonObject mashing = doc.createNestedObject("mashing");
  mashing["active"] = state.mashing.active;
  mashing["phase"] = static_cast<int>(state.mashing.phase);
  mashing["phaseStr"] = getMashPhaseString(state.mashing.phase);
  mashing["stepCount"] = state.mashing.stepCount;
  mashing["currentStep"] = state.mashing.currentStep;
  mashing["targetTemp"] = state.mashing.targetTemp;
  mashing["stepDurationSec"] = state.mashing.stepDuration;
  mashing["tempInRange"] = state.mashing.tempInRange;
  mashing["stepName"] = state.mashing.stepName;

  uint32_t mashElapsedSec = 0;
  if (state.mashing.tempInRange && state.mashing.inRangeStartTime > 0 &&
      now >= state.mashing.inRangeStartTime) {
    mashElapsedSec = (now - state.mashing.inRangeStartTime) / 1000UL;
  }
  mashing["elapsedSec"] = mashElapsedSec;
  mashing["remainingSec"] =
      (state.mashing.stepDuration > mashElapsedSec)
          ? (state.mashing.stepDuration - mashElapsedSec)
          : 0;

  JsonObject hold = doc.createNestedObject("hold");
  hold["active"] = state.hold.active;
  hold["stepCount"] = state.hold.stepCount;
  hold["currentStep"] = state.hold.currentStep;
  hold["targetTemp"] = state.hold.targetTemp;
  hold["tempInRange"] = state.hold.tempInRange;

  uint32_t holdStepDurationSec = 0;
  if (state.hold.stepCount > 0 && state.hold.currentStep < state.hold.stepCount) {
    holdStepDurationSec =
        (uint32_t)state.hold.steps[state.hold.currentStep].duration * 60UL;
  }
  hold["stepDurationSec"] = holdStepDurationSec;

  uint32_t holdElapsedSec = 0;
  if (state.hold.tempInRange && state.hold.inRangeStartTime > 0 &&
      now >= state.hold.inRangeStartTime) {
    holdElapsedSec = (now - state.hold.inRangeStartTime) / 1000UL;
  }
  hold["elapsedSec"] = holdElapsedSec;
  hold["remainingSec"] =
      (holdStepDurationSec > holdElapsedSec) ? (holdStepDurationSec - holdElapsedSec) : 0;

  // Статистика памяти
  JsonObject mem = doc.createNestedObject("memory");
  mem["heap_free"] = ESP.getFreeHeap();
  mem["heap_total"] = ESP.getHeapSize();
  mem["heap_used_pct"] =
      (ESP.getHeapSize() - ESP.getFreeHeap()) * 100 / ESP.getHeapSize();
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

void sendEvent(const char *event, const char *message) {
  StaticJsonDocument<256> doc;
  doc["type"] = "event";
  doc["event"] = event;
  doc["message"] = message;

  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}

} // namespace WebServer
