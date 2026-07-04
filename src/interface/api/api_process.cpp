#include "api_routes.h"

#include <string.h>

#include "config.h"
#include "control/fsm.h"
#include "control/safety.h"
#include "control/v2/status_adapter.h"
#include "drivers/stirrer.h"
#include "interface/webserver_shared.h"
#include "storage/logger.h"

void registerProcessRoutes(AsyncWebServer &server) {
  server.on(
      "/api/process/preflight", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) {
          return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
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

        Mode mode = Mode::IDLE;
        if (!parseRequestedMode(modeStr, mode)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Unknown mode\"}");
          return;
        }

        JsonObject params = doc["params"].as<JsonObject>();
        JsonDocument responseDoc;
        buildProcessPreflight(responseDoc, mode, modeStr, params);

        String response;
        serializeJson(responseDoc, response);
        request->send(200, "application/json", response);
      });

  server.on(
      "/api/process/start", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) {
          return;
        }

        JsonDocument doc;
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

        Mode mode = Mode::IDLE;
        if (parseRequestedMode(modeStr, mode)) {
          JsonDocument startCheckDoc;
          if (!buildProcessPreflight(startCheckDoc, mode, modeStr, params)) {
            String response;
            serializeJson(startCheckDoc, response);
            request->send(409, "application/json", response);
            return;
          }
        } else if (strcmp(modeStr, "rectification") == 0) {
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
        } else if (strcmp(modeStr, "nbk") == 0) {
          mode = Mode::NBK;
        } else if (strcmp(modeStr, "fermentation") == 0) {
          mode = Mode::FERMENTATION;
        } else {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Unknown mode\"}");
          return;
        }

        const bool allowDemoSensorFailure =
            g_settings.demoMode &&
            g_state.currentAlarm.type == AlarmType::SENSOR_FAILURE;

        if (Safety::isLatched(g_state) && !allowDemoSensorFailure) {
          Logger::logf(1, "Process start rejected: safety alarm is latched (%s)",
                       g_state.currentAlarm.message[0]
                           ? g_state.currentAlarm.message
                           : Safety::getAlarmTypeToken(g_state.currentAlarm.type));
          JsonDocument errorDoc;
          errorDoc["success"] = false;
          errorDoc["message"] =
              "Safety alarm is latched. Reset the alarm before starting.";
          JsonObject alarm = errorDoc["alarm"].to<JsonObject>();
          fillAlarmJson(alarm, g_state, g_settings);

          String response;
          serializeJson(errorDoc, response);
          request->send(409, "application/json", response);
          return;
        }

        bool sensorsOk =
            g_state.health.tempSensorsTotal > 0 && g_state.health.tempSensorsOk;

        if (!sensorsOk) {
          LOG_W("Starting process without temperature sensors!");
        }

        if (g_state.mode != Mode::IDLE) {
          request->send(409, "application/json",
                        "{\"success\":false,\"message\":\"Process already active\"}");
          return;
        }

        if (g_state.mode != Mode::IDLE) {
          FSM::stopMode(g_state);
        }

        if (mode == Mode::RECTIFICATION || mode == Mode::DISTILLATION ||
            mode == Mode::NBK) {
          applyBoosterStartOverride(params, g_settings);
        }

        if (mode == Mode::DISTILLATION) {
          float speed = params["speed"] | 500.0f;
          float headsVol = params["headsVolume"] | 0.0f;
          float targetVol = params["targetVolume"] | 0.0f;
          float endTemp = params["endTemp"] | 96.0f;
          const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
                                          ? g_settings.equipment.heaterPowerW
                                          : DEFAULT_HEATER_POWER_W;
          uint16_t powerWatts = 0;
          if (!params["powerW"].isNull()) {
            powerWatts =
                clampU16Range(params["powerW"].as<uint32_t>(), 0, heaterMaxW);
          } else {
            uint8_t powerPercent = params["powerPercent"] | 60;
            if (powerPercent > 100) powerPercent = 100;
            powerWatts = static_cast<uint16_t>(
                (static_cast<uint32_t>(heaterMaxW) * powerPercent) / 100U);
          }
          FSM::Distillation::setParams(speed, headsVol, targetVol, endTemp);
          FSM::Distillation::setPowerWatts(powerWatts);
          FSM::startMode(g_state, g_settings, mode);
        } else if (mode == Mode::MASHING) {
          static MashProfile runtimeProfile;
          memset(&runtimeProfile, 0, sizeof(runtimeProfile));

          bool hasProfile = false;
          if (!params.isNull() && !params["profile"].isNull()) {
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
                runtimeProfile.steps[count]
                    .name[sizeof(runtimeProfile.steps[count].name) - 1] = '\0';
                count++;
              }
              runtimeProfile.stepCount = count;
              hasProfile = (count > 0);
            }
          }

          if (!hasProfile) {
            strncpy(runtimeProfile.name, "Default Mashing",
                    sizeof(runtimeProfile.name) - 1);
            runtimeProfile.stepCount = 5;
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

            for (uint8_t i = 0; i < runtimeProfile.stepCount; i++) {
              runtimeProfile.steps[i]
                  .name[sizeof(runtimeProfile.steps[i].name) - 1] = '\0';
            }
          }

          FSM::Mashing::start(g_state, &runtimeProfile);
        } else if (mode == Mode::HOLD) {
          static TempStep runtimeSteps[10];
          uint8_t count = 0;
          if (!params.isNull() && !params["steps"].isNull()) {
            JsonArray steps = params["steps"].as<JsonArray>();
            for (JsonObject s : steps) {
              if (count >= 10) break;
              runtimeSteps[count].temperature = s["temperature"] | 0.0f;
              runtimeSteps[count].duration = s["duration"] | 0;
              runtimeSteps[count].useCooling = s["useCooling"] | false;
              count++;
            }
          }

          if (count == 0) {
            runtimeSteps[0].temperature = 65.0f;
            runtimeSteps[0].duration = 60;
            runtimeSteps[0].useCooling = false;
            count = 1;
          }

          FSM::Hold::start(g_state, runtimeSteps, count);
        } else if (mode == Mode::NBK || mode == Mode::FERMENTATION) {
          FSM::startMode(g_state, g_settings, mode);
        } else {
          FSM::startMode(g_state, g_settings, mode);
        }

        LOG_I("Process started: mode=%s, sensors=%s", modeStr,
              sensorsOk ? "OK" : "WARNING");

        String response = "{\"success\":true,\"message\":\"Process started\"";
        if (!sensorsOk) {
          response += ",\"warning\":\"No temperature sensors detected\"";
        }
        response += "}";

        request->send(200, "application/json", response);
      });

  server.on("/api/process/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    FSM::stopMode(g_state);
    LOG_I("Process stopped via API");
    request->send(200, "application/json",
                  "{\"success\":true,\"message\":\"Process stopped\"}");
  });

  server.on(
      "/api/process/pause", HTTP_POST, [](AsyncWebServerRequest *request) {
        FSM::pause(g_state);
        ControlV2::updateRuntime(g_state, g_settings);
        LOG_I("Process paused via API");
        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"Process paused\"}");
      });

  server.on(
      "/api/process/resume", HTTP_POST, [](AsyncWebServerRequest *request) {
        FSM::resume(g_state);
        ControlV2::updateRuntime(g_state, g_settings);
        LOG_I("Process resumed via API");
        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"Process resumed\"}");
      });

  server.on(
      "/api/stirrer/start", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        if (request->contentLength() != 0) {
          return;
        }

        if (!ensureStirrerReady(request)) {
          return;
        }

        const uint8_t speed = g_settings.stirrer.defaultSpeedPercent;
        g_state.stirrer.autoMode = false;
        Stirrer::start(speed);
        LOG_I("Stirrer started via API at %u%%", speed);
        sendStirrerStateResponse(request, 200, true, "Stirrer started");
      },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        if (total == 0) {
          return;
        }

        if (!ensureStirrerReady(request)) {
          return;
        }

        uint8_t speed = g_settings.stirrer.defaultSpeedPercent;
        if (len > 0) {
          JsonDocument doc;
          if (deserializeJson(doc, data, len)) {
            request->send(400, "application/json",
                          "{\"success\":false,\"error\":\"Invalid JSON\"}");
            return;
          }

          const int requestedSpeed = doc["speed"] | 0;
          if (requestedSpeed < 0 || requestedSpeed > 100) {
            request->send(400, "application/json",
                          "{\"success\":false,\"error\":\"Speed must be between 0 and 100\"}");
            return;
          }

          if (requestedSpeed > 0) {
            speed = static_cast<uint8_t>(requestedSpeed);
          }
        }

        g_state.stirrer.autoMode = false;
        Stirrer::start(speed);
        LOG_I("Stirrer started via API at %u%%", speed);
        sendStirrerStateResponse(request, 200, true, "Stirrer started");
      });

  server.on("/api/stirrer/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!ensureStirrerReady(request)) {
      return;
    }
    g_state.stirrer.autoMode = false;
    Stirrer::stop();
    LOG_I("Stirrer stopped via API");
    sendStirrerStateResponse(request, 200, true, "Stirrer stopped");
  });

  server.on(
      "/api/stirrer/set", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        if (request->contentLength() == 0) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Speed is required\"}");
        }
      },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) return;

        if (total == 0) {
          return;
        }

        if (!ensureStirrerReady(request)) {
          return;
        }

        if (!Stirrer::isRunning()) {
          sendStirrerStateResponse(request, 409, false,
                                   "Stirrer is not running");
          return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        if (doc["speed"].isNull()) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Speed is required\"}");
          return;
        }

        const int requestedSpeed = doc["speed"].as<int>();
        if (requestedSpeed < 1 || requestedSpeed > 100) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Speed must be between 1 and 100\"}");
          return;
        }

        g_state.stirrer.autoMode = false;
        Stirrer::setSpeed(static_cast<uint8_t>(requestedSpeed));
        LOG_I("Stirrer speed changed via API to %d%%", requestedSpeed);
        sendStirrerStateResponse(request, 200, true, "Stirrer speed updated");
      });
}
