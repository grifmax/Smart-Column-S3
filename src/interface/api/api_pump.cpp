#include "api_routes.h"

#include "config.h"
#include "control/pump_calibration.h"
#include "control/power_command_gateway.h"
#include "drivers/pump.h"
#include "interface/webserver_shared.h"
#include "storage/logger.h"
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

        // Замер мог не зафиксироваться (лимит времени, safety, повторный вход) —
        // восстанавливаем его по накопленным шагам, иначе объём не применить.
        if (PumpCalibrationSession::outputActive()) {
          (void)PumpCalibrationSession::stopMeasurement();
        }
        (void)PumpCalibrationSession::ensureMeasurementCaptured();

        const PumpCalibrationSession::Snapshot captured =
            PumpCalibrationSession::getSnapshot();
        float mlPerRev = 0.0f;
        const char *method = "steps";
        if (PumpCalibrationSession::enterMeasuredVolume(volume) &&
            PumpCalibrationSession::mayCommit()) {
          mlPerRev = PumpCalibrationSession::getCandidateMlPerRev();
        } else {
          // Счётчик шагов мог не набежать (сбой учёта или движение без
          // счётчика). Тогда считаем по времени и заданной подаче: во сколько
          // раз реальный объём отличается от ожидаемого.
          const uint32_t now = millis();
          const uint32_t elapsedMs =
              (captured.stoppedAtMs > captured.startedAtMs)
                  ? (captured.stoppedAtMs - captured.startedAtMs)
                  : (now - captured.startedAtMs);
          const float hours = static_cast<float>(elapsedMs) / 3600000.0f;
          const float expectedMl = captured.speedMlH * hours;
          if (hours > 0.0f && expectedMl > 0.0f &&
              g_settings.pumpCal.mlPerRevolution > 0.0f) {
            mlPerRev =
                g_settings.pumpCal.mlPerRevolution * (volume / expectedMl);
            method = "time";
          }
        }

        if (mlPerRev <= 0.0f) {
          char diag[224];
          snprintf(diag, sizeof(diag),
                   "{\"success\":false,\"message\":\"Замер не захвачен: шагов "
                   "%lu→%lu, фаза %u\",\"phase\":%u,\"startSteps\":%lu,"
                   "\"stopSteps\":%lu}",
                   static_cast<unsigned long>(captured.startSteps),
                   static_cast<unsigned long>(captured.stopSteps),
                   static_cast<unsigned>(captured.phase),
                   static_cast<unsigned>(captured.phase),
                   static_cast<unsigned long>(captured.startSteps),
                   static_cast<unsigned long>(captured.stopSteps));
          Logger::logf(
              1, "pump cal finish failed: phase=%u steps %lu->%lu vol=%.1f",
              static_cast<unsigned>(captured.phase),
              static_cast<unsigned long>(captured.startSteps),
              static_cast<unsigned long>(captured.stopSteps),
              static_cast<double>(volume));
          request->send(400, "application/json", diag);
          return;
        }

        Logger::logf(
            0, "pump cal committed: method=%s mlPerRev=%.4f vol=%.1f steps %lu->%lu",
            method, static_cast<double>(mlPerRev), static_cast<double>(volume),
            static_cast<unsigned long>(captured.startSteps),
            static_cast<unsigned long>(captured.stopSteps));

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

        char response[192];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"mlPerRev\":%.4f,"
                 "\"calibrationFactor\":%.3f,\"method\":\"%s\"}",
                 mlPerRev, calibrationFactor, method);
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

        PowerCommandGateway::setPumpSpeed(
            PowerCommandGateway::Owner::MANUAL,
            PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT, speed);
        LOG_I("Pump started via API at %.1f ml/h", speed);

        char response[128];
        snprintf(response, sizeof(response), "{\"success\":true,\"speed\":%.1f}",
                 speed);
        request->send(200, "application/json", response);
      });

  server.on("/api/pump/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Стоп насоса НЕ должен выбрасывать калибровочный замер: иначе связка
    // /calibrate/stop + /pump/stop (её делает веб-страница) убивает сессию, и
    // ввод объёма отвечает «No valid measurement captured».
    const PumpCalibrationSession::Phase phase =
        PumpCalibrationSession::getSnapshot().phase;
    if (phase == PumpCalibrationSession::Phase::PRIMING ||
        phase == PumpCalibrationSession::Phase::MEASURING) {
      (void)PumpCalibrationSession::ensureMeasurementCaptured();
    } else {
      PowerCommandGateway::setPumpSpeed(PowerCommandGateway::Owner::MANUAL,
                                        PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
                                        0.0f);
    }
    LOG_I("Pump stopped via API (phase=%u)",
          static_cast<unsigned>(phase));
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
    doc["calibrationStartSteps"] = calibration.startSteps;
    doc["calibrationStopSteps"] = calibration.stopSteps;
    doc["calibrationPumpSteps"] = Pump::getTotalSteps();

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
