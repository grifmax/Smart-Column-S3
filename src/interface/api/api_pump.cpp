#include "api_routes.h"

#include "config.h"
#include "control/pump_calibration.h"
#include "control/power_command_gateway.h"
#include "control/state_snapshot.h"
#include "drivers/pump.h"
#include "interface/webserver_shared.h"
#include "storage/logger.h"
#include "storage/nvs_manager.h"

namespace {

const char *outputCommandStatus(const PowerCommandGateway::Command &command) {
  if (!command.accepted) return "rejected";
  return command.applied ? "applied" : "queued";
}

void appendOutputCommand(JsonObject object,
                         const PowerCommandGateway::Command &command) {
  object["commandId"] = command.sequence;
  object["commandType"] = "output";
  object["accepted"] = command.accepted;
  object["queued"] = command.queued;
  object["applied"] = command.applied;
  object["success"] = command.accepted && command.applied;
  object["status"] = outputCommandStatus(command);
  object["rejectReason"] =
      PowerCommandGateway::rejectReasonString(command.rejectReason);
  object["rejectReasonCode"] =
      static_cast<uint8_t>(command.rejectReason);
}

}  // namespace

void registerPumpApiRoutes(AsyncWebServer &server) {
  server.on("/api/pump/calibrate/start", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              SystemState stateSnapshot;
              if (!StateSnapshot::read(stateSnapshot)) {
                request->send(503, "application/json",
                              "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
                return;
              }
              if (stateSnapshot.mode != Mode::IDLE) {
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
              if (!Pump::isRunning()) {
                request->send(202, "application/json",
                              "{\"accepted\":true,\"status\":\"starting\"}");
                return;
              }
              request->send(200, "application/json",
                            "{\"success\":true,\"status\":\"running\"}");
            });

  server.on("/api/pump/calibrate/stop", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (!PumpCalibrationSession::stopMeasurement()) {
                request->send(
                    409, "application/json",
                    "{\"success\":false,\"message\":\"Calibration not active\"}");
                return;
              }
              if (PumpCalibrationSession::isStopPending()) {
                request->send(202, "application/json",
                              "{\"accepted\":true,\"status\":\"stopping\"}");
                return;
              }
              LOG_I("Pump calibration measurement stopped");
              request->send(200, "application/json",
                            "{\"success\":true,\"status\":\"stopped\"}");
            });

  server.on("/api/pump/calibrate/cancel", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              PumpCalibrationSession::cancel();
              LOG_I("Pump calibration cancelled and reset");
              if (PumpCalibrationSession::isStopPending()) {
                request->send(202, "application/json",
                              "{\"accepted\":true,\"status\":\"stopping\"}");
                return;
              }
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

        const float volume = doc["volume"] | 0.0f;
        if (volume <= 0.0f) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"message\":\"Volume must be > 0\"}");
          return;
        }

        const char *requestedMethod = doc["method"] | "steps";
        if (strcmp(requestedMethod, "steps") != 0 &&
            strcmp(requestedMethod, "time") != 0) {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Method must be steps or time\"}");
          return;
        }

        PumpCalibrationSession::Snapshot captured =
            PumpCalibrationSession::getSnapshot();
        if (captured.phase == PumpCalibrationSession::Phase::PRIMING) {
          PumpCalibrationSession::cancel();
          request->send(
              409, "application/json",
              "{\"success\":false,\"code\":\"PRIMING_NOT_MEASUREMENT\",\"message\":\"Прокачка остановлена; выполните отдельный замер перед калибровкой\"}");
          return;
        }
        if (captured.phase == PumpCalibrationSession::Phase::MEASURING) {
          if (!PumpCalibrationSession::stopMeasurement()) {
            request->send(409, "application/json",
                          "{\"success\":false,\"code\":\"STOP_REJECTED\",\"message\":\"Останов насоса не принят\"}");
            return;
          }
          captured = PumpCalibrationSession::getSnapshot();
          if (PumpCalibrationSession::isStopPending() ||
              captured.phase == PumpCalibrationSession::Phase::MEASURING) {
            request->send(202, "application/json",
                          "{\"accepted\":true,\"status\":\"stopping\"}");
            return;
          }
        }
        if ((captured.phase !=
                 PumpCalibrationSession::Phase::READY_TO_REVIEW &&
             captured.phase != PumpCalibrationSession::Phase::REVIEW) ||
            !captured.measurementCompleted) {
          request->send(
              409, "application/json",
              "{\"success\":false,\"code\":\"MEASUREMENT_NOT_COMPLETED\",\"message\":\"Завершённый замер не найден; начните и остановите калибровочный замер\"}");
          return;
        }

        const float oldMlPerRev = g_settings.pumpCal.mlPerRevolution;
        const bool confirmLargeChange = doc["confirmLargeChange"] | false;
        float mlPerRev = 0.0f;
        uint32_t elapsedMs = captured.stoppedAtMs - captured.startedAtMs;
        const char *method = requestedMethod;

        if (strcmp(requestedMethod, "time") == 0) {
          const PumpCalibrationLogic::TimeFallbackAssessment assessment =
              PumpCalibrationLogic::assessTimeFallback(
                  captured, volume, oldMlPerRev, confirmLargeChange);
          if (assessment.status ==
              PumpCalibrationLogic::ChangeStatus::ConfirmationRequired) {
            JsonDocument responseDoc;
            responseDoc["success"] = false;
            responseDoc["code"] = "CONFIRM_LARGE_CALIBRATION_CHANGE";
            responseDoc["message"] =
                "Изменение коэффициента больше 2 раз; подтвердите повторно";
            responseDoc["candidateMlPerRev"] = assessment.candidateMlPerRev;
            responseDoc["changeRatio"] = assessment.changeRatio;
            responseDoc["durationMs"] = assessment.durationMs;
            responseDoc["expectedVolumeMl"] = assessment.expectedVolumeMl;
            String response;
            serializeJson(responseDoc, response);
            request->send(409, "application/json", response);
            return;
          }
          if (assessment.status !=
              PumpCalibrationLogic::ChangeStatus::Accepted) {
            JsonDocument responseDoc;
            responseDoc["success"] = false;
            responseDoc["code"] = "TIME_FALLBACK_UNAVAILABLE";
            responseDoc["message"] =
                "Расчёт по времени доступен только для завершённого замера без учтённых шагов, длительностью от 30 секунд и ожидаемым объёмом от 5 мл";
            responseDoc["durationMs"] = elapsedMs;
            responseDoc["speedMlH"] = captured.speedMlH;
            responseDoc["expectedVolumeMl"] =
                captured.speedMlH * static_cast<float>(elapsedMs) / 3600000.0f;
            responseDoc["startSteps"] = captured.startSteps;
            responseDoc["stopSteps"] = captured.stopSteps;
            String response;
            serializeJson(responseDoc, response);
            request->send(409, "application/json", response);
            return;
          }
          mlPerRev = assessment.candidateMlPerRev;
        } else {
          if (captured.phase == PumpCalibrationSession::Phase::REVIEW &&
              PumpCalibrationSession::mayCommit()) {
            if (fabsf(captured.measuredVolumeMl - volume) > 0.01f) {
              request->send(
                  409, "application/json",
                  "{\"success\":false,\"code\":\"MEASURED_VOLUME_CHANGED\",\"message\":\"Объём отличается от уже рассчитанного результата; повторите замер\"}");
              return;
            }
            mlPerRev = captured.candidateMlPerRev;
          } else if (PumpCalibrationSession::enterMeasuredVolume(volume) &&
                     PumpCalibrationSession::mayCommit()) {
            captured = PumpCalibrationSession::getSnapshot();
            mlPerRev = captured.candidateMlPerRev;
          } else {
            captured = PumpCalibrationSession::getSnapshot();
            JsonDocument responseDoc;
            responseDoc["success"] = false;
            responseDoc["code"] = captured.stepCounterValid
                                       ? "TIME_FALLBACK_AVAILABLE"
                                       : "INVALID_STEP_MEASUREMENT";
            responseDoc["fallbackAvailable"] =
                captured.stepCounterValid &&
                captured.stopSteps == captured.startSteps;
            responseDoc["message"] =
                "Шаговая калибровка невозможна. Введите method=time только если подтверждаете, что насос подавал жидкость, но счётчик не учёл шаги";
            responseDoc["phase"] = static_cast<uint8_t>(captured.phase);
            responseDoc["startSteps"] = captured.startSteps;
            responseDoc["stopSteps"] = captured.stopSteps;
            responseDoc["durationMs"] = elapsedMs;
            String response;
            serializeJson(responseDoc, response);
            request->send(409, "application/json", response);
            return;
          }

          const PumpCalibrationLogic::ChangeStatus changeStatus =
              PumpCalibrationLogic::assessCalibrationChange(
                  oldMlPerRev, mlPerRev, confirmLargeChange);
          if (changeStatus ==
              PumpCalibrationLogic::ChangeStatus::ConfirmationRequired) {
            JsonDocument responseDoc;
            responseDoc["success"] = false;
            responseDoc["code"] = "CONFIRM_LARGE_CALIBRATION_CHANGE";
            responseDoc["message"] =
                "Изменение коэффициента больше 2 раз; подтвердите повторно";
            responseDoc["candidateMlPerRev"] = mlPerRev;
            responseDoc["changeRatio"] = mlPerRev / oldMlPerRev;
            responseDoc["durationMs"] = elapsedMs;
            String response;
            serializeJson(responseDoc, response);
            request->send(409, "application/json", response);
            return;
          }
          if (changeStatus != PumpCalibrationLogic::ChangeStatus::Accepted) {
            request->send(
                422, "application/json",
                "{\"success\":false,\"code\":\"CALIBRATION_CHANGE_OUT_OF_RANGE\",\"message\":\"Новый коэффициент отличается от текущего более чем в 4 раза; проверьте замер и оборудование\"}");
            return;
          }
        }

        const uint8_t phase = static_cast<uint8_t>(captured.phase);
        Logger::logf(
            0, "pump cal committed: method=%s phase=%u durationMs=%lu speed=%.1f volume=%.1f old=%.4f new=%.4f steps %lu->%lu",
            method, static_cast<unsigned>(phase),
            static_cast<unsigned long>(elapsedMs),
            static_cast<double>(captured.speedMlH), static_cast<double>(volume),
            static_cast<double>(oldMlPerRev), static_cast<double>(mlPerRev),
            static_cast<unsigned long>(captured.startSteps),
            static_cast<unsigned long>(captured.stopSteps));

        g_settings.pumpCal.mlPerRevolution = mlPerRev;
        if (!NVSManager::saveSettings(g_settings)) {
          g_settings.pumpCal.mlPerRevolution = oldMlPerRev;
          request->send(
              500, "application/json",
              "{\"success\":false,\"message\":\"Не удалось сохранить калибровку\"}");
          return;
        }
        Pump::setCalibration(mlPerRev);
        const float elapsedSec = elapsedMs / 1000.0f;
        const float calibrationFactor =
            (elapsedSec > 0.0f) ? (volume / elapsedSec) : 0.0f;

        PumpCalibrationSession::complete();

        char response[320];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"mlPerRev\":%.4f,"
                 "\"oldMlPerRev\":%.4f,\"calibrationFactor\":%.3f,"
                 "\"method\":\"%s\",\"phase\":%u,\"durationMs\":%lu,"
                 "\"speedMlH\":%.2f,\"measuredVolumeMl\":%.2f,"
                 "\"startSteps\":%lu,\"stopSteps\":%lu}",
                 mlPerRev, oldMlPerRev, calibrationFactor, method,
                 static_cast<unsigned>(phase),
                 static_cast<unsigned long>(elapsedMs),
                 static_cast<double>(captured.speedMlH),
                 static_cast<double>(volume),
                 static_cast<unsigned long>(captured.startSteps),
                 static_cast<unsigned long>(captured.stopSteps));
        request->send(200, "application/json", response);
      });

  server.on("/api/pump/calibrate/prime/start", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              SystemState stateSnapshot;
              if (!StateSnapshot::read(stateSnapshot)) {
                request->send(503, "application/json",
                              "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
                return;
              }
              if (stateSnapshot.mode != Mode::IDLE) {
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
              if (!Pump::isRunning()) {
                request->send(202, "application/json",
                              "{\"accepted\":true,\"phase\":\"starting\"}");
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
              if (PumpCalibrationSession::isStopPending()) {
                request->send(202, "application/json",
                              "{\"accepted\":true,\"phase\":\"stopping\"}");
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

        const PowerCommandGateway::Command command =
            PowerCommandGateway::setPumpSpeed(
            PowerCommandGateway::Owner::MANUAL,
            PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT, speed);
        if (!command.accepted) {
          JsonDocument response;
          appendOutputCommand(response.to<JsonObject>(), command);
          response["message"] = "Pump command rejected";
          String json;
          serializeJson(response, json);
          request->send(409, "application/json", json);
          return;
        }
        if (command.applied) LOG_I("Pump started via API at %.1f ml/h", speed);

        JsonDocument response;
        appendOutputCommand(response.to<JsonObject>(), command);
        response["speed"] = speed;
        String json;
        serializeJson(response, json);
        request->send(command.queued ? 202 : 200, "application/json", json);
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
      if (PumpCalibrationSession::isStopPending() || Pump::isRunning()) {
        request->send(202, "application/json",
                      "{\"accepted\":true,\"status\":\"stopping\"}");
        return;
      }
    } else {
      const PowerCommandGateway::Command command =
          PowerCommandGateway::setPumpSpeed(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT, 0.0f);
      if (!command.accepted) {
        JsonDocument response;
        appendOutputCommand(response.to<JsonObject>(), command);
        response["message"] = "Pump stop rejected";
        String json;
        serializeJson(response, json);
        request->send(409, "application/json", json);
        return;
      }
      if (command.queued) {
        JsonDocument response;
        appendOutputCommand(response.to<JsonObject>(), command);
        response["message"] = "Pump stop queued";
        String json;
        serializeJson(response, json);
        request->send(202, "application/json", json);
        return;
      }
      JsonDocument response;
      appendOutputCommand(response.to<JsonObject>(), command);
      response["message"] = "Pump stopped";
      String json;
      serializeJson(response, json);
      request->send(200, "application/json", json);
      return;
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
    doc["calibrationStopPending"] =
        PumpCalibrationSession::isStopPending();
    doc["calibrationCandidateMlPerRev"] = calibration.candidateMlPerRev;
    doc["calibrationStartSteps"] = calibration.startSteps;
    doc["calibrationStopSteps"] = calibration.stopSteps;
    doc["calibrationPumpSteps"] = Pump::getTotalSteps();
    const PowerCommandGateway::Command lastCommand =
        PowerCommandGateway::getLastCommand();
    if (lastCommand.target == PowerCommandGateway::Target::PUMP) {
      JsonObject outputCommand = doc["lastOutputCommand"].to<JsonObject>();
      appendOutputCommand(outputCommand, lastCommand);
      outputCommand["requestedValue"] = lastCommand.requestedValue;
      outputCommand["appliedValue"] = lastCommand.appliedValue;
      outputCommand["timestampMs"] = lastCommand.timestampMs;
    }

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
    doc["requestedStepHz"] = diag.requestedStepHz;
    doc["outputStepHz"] = diag.outputStepHz;
    doc["stepsPerRevolution"] = diag.stepsPerRevolution;
    doc["stepOutputOk"] = diag.stepOutputOk;
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
