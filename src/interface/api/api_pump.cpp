#include "api_routes.h"

#include "config.h"
#include "drivers/pump.h"
#include "interface/webserver_shared.h"
#include "storage/nvs_manager.h"

struct PumpCalibrationSession {
  bool active = false;
  uint32_t startSteps = 0;
  uint32_t stopSteps = 0;
  uint32_t startMs = 0;
  uint32_t stopMs = 0;
};

static PumpCalibrationSession g_pumpCalSession;

void registerPumpRoutes(AsyncWebServer &server) {
  server.on("/api/pump/calibrate/start", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (g_state.mode != Mode::IDLE) {
                request->send(
                    409, "application/json",
                    "{\"success\":false,\"message\":\"Process is running\"}");
                return;
              }
              if (g_pumpCalSession.active) {
                request->send(
                    409, "application/json",
                    "{\"success\":false,\"message\":\"Calibration already active\"}");
                return;
              }

              Pump::resetVolume();
              g_pumpCalSession.active = true;
              g_pumpCalSession.startSteps = Pump::getTotalSteps();
              g_pumpCalSession.stopSteps = 0;
              g_pumpCalSession.startMs = millis();
              g_pumpCalSession.stopMs = 0;

              float speed = Pump::getMaxSpeedMlH();
              if (request->hasParam("speed")) {
                float reqSpeed = request->getParam("speed")->value().toFloat();
                if (reqSpeed > 0) {
                  speed = reqSpeed;
                }
              }
              Pump::start(speed);
              LOG_I("Pump calibration started at %.1f ml/h", speed);

              request->send(200, "application/json",
                            "{\"success\":true,\"running\":true}");
            });

  server.on("/api/pump/calibrate/stop", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (!g_pumpCalSession.active) {
                request->send(
                    409, "application/json",
                    "{\"success\":false,\"message\":\"Calibration not active\"}");
                return;
              }

              Pump::stop();
              g_pumpCalSession.stopSteps = Pump::getTotalSteps();
              g_pumpCalSession.stopMs = millis();
              LOG_I("Pump calibration stopped: %u", g_pumpCalSession.stopSteps);

              request->send(200, "application/json",
                            "{\"success\":true,\"running\":false}");
            });

  server.on("/api/pump/calibrate/cancel", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              Pump::stop();
              g_pumpCalSession.active = false;
              LOG_I("Pump calibration cancelled and reset");

              request->send(200, "application/json",
                            "{\"success\":true,\"active\":false}");
            });

  server.on(
      "/api/pump/calibrate/finish", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) {
          return;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);
        if (error) {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Invalid JSON\"}");
          return;
        }

        if (!g_pumpCalSession.active) {
          request->send(
              409, "application/json",
              "{\"success\":false,\"message\":\"Calibration not active\"}");
          return;
        }

        float volume = doc["volume"] | 0.0f;
        if (volume <= 0.0f) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Volume must be > 0\"}");
          return;
        }

        if (Pump::isRunning()) {
          Pump::stop();
        }
        if (g_pumpCalSession.stopSteps == 0) {
          g_pumpCalSession.stopSteps = Pump::getTotalSteps();
          g_pumpCalSession.stopMs = millis();
        }

        const uint32_t steps =
            g_pumpCalSession.stopSteps - g_pumpCalSession.startSteps;
        const uint32_t stepsPerRev =
            static_cast<uint32_t>(PUMP_STEPS_PER_REV) *
            static_cast<uint32_t>(PUMP_MICROSTEPS);

        if (steps == 0 || stepsPerRev == 0) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"No steps captured\"}");
          return;
        }

        const float revolutions = static_cast<float>(steps) /
                                  static_cast<float>(stepsPerRev);
        const float mlPerRev = volume / revolutions;

        if (mlPerRev <= 0.0f) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Invalid calibration result\"}");
          return;
        }

        g_settings.pumpCal.mlPerRevolution = mlPerRev;
        NVSManager::saveSettings(g_settings);
        Pump::setCalibration(mlPerRev);

        const uint32_t elapsedMs =
            (g_pumpCalSession.stopMs > g_pumpCalSession.startMs)
                ? (g_pumpCalSession.stopMs - g_pumpCalSession.startMs)
                : 30000;
        const float elapsedSec = elapsedMs / 1000.0f;
        const float calibrationFactor =
            (elapsedSec > 0.0f) ? (volume / elapsedSec) : 0.0f;

        g_pumpCalSession.active = false;

        char response[160];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"mlPerRev\":%.4f,\"calibrationFactor\":%.3f}",
                 mlPerRev, calibrationFactor);
        request->send(200, "application/json", response);
      });

  server.on(
      "/api/pump/start", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index + len != total) {
          return;
        }

        JsonDocument doc;
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
        snprintf(response, sizeof(response), "{\"success\":true,\"speed\":%.1f}",
                 speed);
        request->send(200, "application/json", response);
      });

  server.on("/api/pump/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    Pump::stop();
    LOG_I("Pump stopped via API");
    request->send(200, "application/json",
                  "{\"success\":true,\"message\":\"Pump stopped\"}");
  });

  server.on("/api/pump/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["running"] = Pump::isRunning();
    doc["speed"] = Pump::getSpeed();
    doc["totalVolume"] = Pump::getTotalVolume();

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/pump/diag", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    Pump::Diagnostics diag = Pump::getDiagnostics();
    doc["running"] = Pump::isRunning();
    doc["taskAlive"] = diag.taskAlive;
    doc["mutexReady"] = diag.mutexReady;
    doc["speedMlH"] = diag.speedMlH;
    doc["appliedSpeedMlH"] = diag.appliedSpeedMlH;
    doc["totalSteps"] = diag.totalSteps;
    doc["totalVolumeMl"] = diag.totalVolumeMl;
    doc["taskLoopCount"] = diag.taskLoopCount;
    doc["counterUpdateCount"] = diag.counterUpdateCount;
    doc["cooperativeSleepCount"] = diag.cooperativeSleepCount;
    doc["fastYieldCount"] = diag.fastYieldCount;
    doc["lockTimeoutCount"] = diag.lockTimeoutCount;
    doc["lastLoopAtMs"] = diag.lastLoopAtMs;
    doc["lastYieldAtMs"] = diag.lastYieldAtMs;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
}
