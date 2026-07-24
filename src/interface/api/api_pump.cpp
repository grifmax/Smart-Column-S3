#include "api_routes.h"

#include "config.h"
#include "control/pump_calibration.h"
#include "drivers/pump.h"
#include "interface/webserver_shared.h"
#include "storage/nvs_manager.h"

void registerPumpApiRoutes(AsyncWebServer &server) {
  server.on("/api/pump/calibrate/start", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (g_state.mode != Mode::IDLE) {
                request->send(
                    409, "application/json",
                    "{\"success\":false,\"message\":\"Process is running\"}");
                return;
              }
              float speed = Pump::getMaxSpeedMlH();
              if (request->hasParam("speed")) {
                float reqSpeed = request->getParam("speed")->value().toFloat();
                if (reqSpeed > 0) {
                  speed = reqSpeed;
                }
              }
              if (!PumpCalibrationSession::beginMeasurement(speed, true)) {
                request->send(
                    409, "application/json",
                    "{\"success\":false,\"message\":\"Calibration already active\"}");
                return;
              }
              LOG_I("Pump calibration started at %.1f ml/h", speed);

              request->send(200, "application/json",
                            "{\"success\":true,\"running\":true}");
            });

  server.on("/api/pump/calibrate/stop", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (!PumpCalibrationSession::stopMeasurement()) {
                request->send(
                    409, "application/json",
                    "{\"success\":false,\"message\":\"Calibration not active\"}");
                return;
              }

              LOG_I("Pump calibration measurement stopped");

              request->send(200, "application/json",
                            "{\"success\":true,\"running\":false}");
            });

  server.on("/api/pump/calibrate/cancel", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              PumpCalibrationSession::cancel();
              LOG_I("Pump calibration cancelled and reset");

              request->send(200, "application/json",
                            "{\"success\":true,\"active\":false}");
            });

  server.on(
      "/api/pump/calibrate/finish", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"message\":\"Invalid JSON\"}")) {
          return;
        }

        float volume = doc["volume"] | 0.0f;
        if (volume <= 0.0f) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Volume must be > 0\"}");
          return;
        }

        if (PumpCalibrationSession::outputActive()) {
          (void)PumpCalibrationSession::stopMeasurement();
        }
        if (!PumpCalibrationSession::enterMeasuredVolume(volume) ||
            !PumpCalibrationSession::mayCommit()) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"No valid measurement captured\"}");
          return;
        }

        const float mlPerRev = PumpCalibrationSession::getCandidateMlPerRev();

        g_settings.pumpCal.mlPerRevolution = mlPerRev;
        NVSManager::saveSettings(g_settings);
        Pump::setCalibration(mlPerRev);
        const PumpCalibrationSession::Snapshot snapshot =
            PumpCalibrationSession::getSnapshot();
        const uint32_t elapsedMs =
            (snapshot.stoppedAtMs > snapshot.startedAtMs)
                ? (snapshot.stoppedAtMs - snapshot.startedAtMs)
                : 0;
        const float elapsedSec = elapsedMs / 1000.0f;
        const float calibrationFactor =
            (elapsedSec > 0.0f) ? (volume / elapsedSec) : 0.0f;

        PumpCalibrationSession::complete();

        char response[160];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"mlPerRev\":%.4f,\"calibrationFactor\":%.3f}",
                 mlPerRev, calibrationFactor);
        request->send(200, "application/json", response);
      });

  server.on("/api/pump/calibrate/prime/start", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (g_state.mode != Mode::IDLE) {
                request->send(409, "application/json",
                              "{\"success\":false,\"message\":\"Process is running\"}");
                return;
              }
              float speed = Pump::getMaxSpeedMlH();
              if (request->hasParam("speed")) {
                const float requested = request->getParam("speed")->value().toFloat();
                if (requested > 0.0f) speed = requested;
              }
              if (!PumpCalibrationSession::beginPriming(speed)) {
                request->send(409, "application/json",
                              "{\"success\":false,\"message\":\"Calibration is not idle\"}");
                return;
              }
              request->send(200, "application/json",
                            "{\"success\":true,\"phase\":\"priming\"}");
            });

  server.on("/api/pump/calibrate/prime/stop", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (!PumpCalibrationSession::finishPriming()) {
                request->send(409, "application/json",
                              "{\"success\":false,\"message\":\"Priming is not active\"}");
                return;
              }
              request->send(200, "application/json",
                            "{\"success\":true,\"phase\":\"ready\"}");
            });

  server.on(
      "/api/pump/start", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        float speed = doc["speed"] | 0.0f;
        if (speed <= 0) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Speed must be > 0\"}");
          return;
        }
        if (PumpCalibrationSession::getSnapshot().phase !=
            PumpCalibrationSession::Phase::IDLE) {
          request->send(
              409, "application/json",
              "{\"success\":false,\"error\":\"Pump calibration session is active\"}");
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
    if (PumpCalibrationSession::getSnapshot().phase !=
        PumpCalibrationSession::Phase::IDLE) {
      PumpCalibrationSession::cancel();
    } else {
      Pump::stop();
    }
    LOG_I("Pump stopped via API");
    request->send(200, "application/json",
                  "{\"success\":true,\"message\":\"Pump stopped\"}");
  });

  server.on("/api/pump/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    const PumpCalibrationSession::Snapshot calibration =
        PumpCalibrationSession::getSnapshot();
    doc["running"] = Pump::isRunning();
    doc["speed"] = Pump::getSpeed();
    doc["totalVolume"] = Pump::getTotalVolume();
    doc["calibrationPhase"] = static_cast<uint8_t>(calibration.phase);
    doc["calibrationCandidateMlPerRev"] = calibration.candidateMlPerRev;

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
