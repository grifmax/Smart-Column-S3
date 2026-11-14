#include "api_routes.h"

#include <string.h>
#include <math.h>

#include "config.h"
#include "control/control_command_queue.h"
#include "control/fsm.h"
#include "control/process_resume.h"
#include "control/process_resume_logic.h"
#include "control/power_command_gateway.h"
#include "control/service_test_session.h"
#include "control/safety.h"
#include "control/state_snapshot.h"
#include "control/v2/status_adapter.h"
#include "drivers/stirrer.h"
#include "interface/editable_field_descriptors.h"
#include "interface/webserver_shared.h"
#include "storage/logger.h"
#include "storage/nvs_manager.h"

namespace {

uint32_t requestedCommandId(AsyncWebServerRequest *request) {
  if (!request->hasParam("commandId")) return 0;
  const long value = request->getParam("commandId")->value().toInt();
  return value > 0 ? static_cast<uint32_t>(value) : 0;
}

bool controlCommandAccepted(ControlCommandQueue::EnqueueResult result) {
  return result == ControlCommandQueue::EnqueueResult::Accepted ||
         result == ControlCommandQueue::EnqueueResult::Duplicate;
}

void appendControlStatus(JsonObject item,
                         ControlCommandQueue::EnqueueResult result,
                         uint32_t commandId,
                         ControlCommandQueue::Status status) {
  const bool accepted = controlCommandAccepted(result);
  const bool queued = status == ControlCommandQueue::Status::Queued ||
                      status == ControlCommandQueue::Status::Processing;
  const bool applied = status == ControlCommandQueue::Status::Applied;
  item["commandType"] = "control";
  item["commandId"] = commandId;
  item["accepted"] = accepted;
  item["queued"] = queued;
  item["applied"] = applied;
  item["success"] = accepted && applied;
  item["status"] = ControlCommandQueue::statusToken(status);
}

void appendOutputStatus(JsonObject item,
                        const PowerCommandGateway::Command &command) {
  item["commandType"] = "output";
  item["commandId"] = command.sequence;
  item["accepted"] = command.accepted;
  item["queued"] = command.queued;
  item["applied"] = command.applied;
  item["success"] = command.accepted && command.applied;
  item["status"] = !command.accepted ? "rejected"
                    : command.applied ? "applied" : "queued";
  item["rejectReason"] =
      PowerCommandGateway::rejectReasonString(command.rejectReason);
}

void sendManualStirrerCommandResponse(
    AsyncWebServerRequest *request, const SystemState &state,
    ControlCommandQueue::EnqueueResult controlResult,
    uint32_t controlCommandId, ControlCommandQueue::Status controlStatus,
    const PowerCommandGateway::Command *outputCommand,
    const char *message) {
  const bool controlAccepted = controlCommandAccepted(controlResult);
  const bool controlQueued = controlStatus == ControlCommandQueue::Status::Queued ||
                             controlStatus == ControlCommandQueue::Status::Processing;
  const bool controlApplied = controlStatus == ControlCommandQueue::Status::Applied;
  const bool outputAccepted = outputCommand == nullptr || outputCommand->accepted;
  const bool outputQueued = outputCommand != nullptr && outputCommand->queued;
  const bool outputApplied = outputCommand == nullptr || outputCommand->applied;
  const bool accepted = controlAccepted && outputAccepted;
  const bool queued = controlQueued || outputQueued;
  const bool applied = controlApplied && outputApplied;

  JsonDocument response;
  response["commandType"] = "composite";
  response["success"] = accepted && applied;
  response["accepted"] = accepted;
  response["queued"] = queued;
  response["applied"] = applied;
  response["status"] = !accepted ? "rejected" : applied ? "applied" : "queued";
  response["message"] = message;
  JsonArray commands = response["commands"].to<JsonArray>();
  JsonObject control = commands.add<JsonObject>();
  appendControlStatus(control, controlResult, controlCommandId, controlStatus);
  if (outputCommand != nullptr) {
    JsonObject output = commands.add<JsonObject>();
    appendOutputStatus(output, *outputCommand);
  }
  JsonObject stirrer = response["stirrer"].to<JsonObject>();
  fillStirrerJson(stirrer, state);

  String json;
  serializeJson(response, json);
  int httpStatus = 200;
  if (!accepted) {
    httpStatus = controlResult == ControlCommandQueue::EnqueueResult::Full
                     ? 429
                     : controlResult == ControlCommandQueue::EnqueueResult::NotInitialized
                           ? 503
                           : 409;
  } else if (queued) {
    httpStatus = 202;
  }
  request->send(httpStatus, "application/json", json);
}

void sendControlCommandResponse(AsyncWebServerRequest *request,
                                ControlCommandQueue::EnqueueResult result,
                                uint32_t commandId,
                                ControlCommandQueue::Status status,
                                bool sensorsOk = true) {
  const bool queued = status == ControlCommandQueue::Status::Queued;
  const bool applied = status == ControlCommandQueue::Status::Applied;
  const bool accepted = result == ControlCommandQueue::EnqueueResult::Accepted ||
                        result == ControlCommandQueue::EnqueueResult::Duplicate;
  const bool success = accepted && applied;

  JsonDocument responseDoc;
  responseDoc["success"] = success;
  responseDoc["accepted"] = accepted;
  responseDoc["applied"] = applied;
  responseDoc["queued"] = queued;
  responseDoc["commandId"] = commandId;
  responseDoc["status"] = ControlCommandQueue::statusToken(status);
  if (success && !sensorsOk) {
    responseDoc["warning"] = "No temperature sensors detected";
  }
  if (!success) {
    responseDoc["message"] =
        result == ControlCommandQueue::EnqueueResult::Full
            ? "Control command queue is full"
            : "Control command was rejected or superseded";
  }

  String response;
  serializeJson(responseDoc, response);
  const int httpStatus = success ? (queued ? 202 : 200)
                                 : (result == ControlCommandQueue::EnqueueResult::Full
                                        ? 429
                                        : result == ControlCommandQueue::EnqueueResult::NotInitialized
                                              ? 503
                                              : 409);
  request->send(httpStatus, "application/json", response);
}

} // namespace

void registerProcessApiRoutes(AsyncWebServer &server) {
  server.on("/api/process/resume-available", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              JsonDocument response;
              ProcessResume::PersistedSnapshot saved;
              const bool available = ProcessResume::getPending(saved);
              response["success"] = true;
              response["available"] = available;
              if (available) {
                response["mode"] = "rectification";
                response["phase"] = getPhaseString(
                    static_cast<RectPhase>(saved.phase));
                response["phaseId"] = saved.phase;
                response["paused"] = saved.paused != 0;
                JsonObject volumes = response["volumes"].to<JsonObject>();
                volumes["heads"] = saved.headsVolumeMl;
                volumes["body"] = saved.bodyVolumeMl;
                volumes["tails"] = saved.tailsVolumeMl;
                volumes["total"] = saved.totalVolumeMl;
                JsonObject recoveryHeads =
                    response["recoveryHeads"].to<JsonObject>();
                const bool offered =
                    ProcessResume::shouldOfferRecoveryHeads(saved);
                recoveryHeads["offered"] = offered;
                recoveryHeads["suggestedMl"] = offered
                    ? ProcessResume::suggestedRecoveryHeadsMl(saved)
                    : 0.0f;
                recoveryHeads["selectedTargetMl"] =
                    saved.recoveryActive != 0
                        ? saved.recoveryHeadsTargetMl
                        : offered
                              ? ProcessResume::suggestedRecoveryHeadsMl(saved)
                              : 0.0f;
                recoveryHeads["collectedMl"] =
                    saved.recoveryActive != 0
                        ? saved.recoveryHeadsCollectedMl
                        : 0.0f;
              }
              String json;
              serializeJson(response, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "/api/process/recover", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (index != 0 || len != total) return;
        SystemState stateSnapshot;
        if (!StateSnapshot::read(stateSnapshot)) {
          request->send(503, "application/json",
                        "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
          return;
        }
        if (stateSnapshot.mode != Mode::IDLE || !stateSnapshot.safetyOk ||
            Safety::isLatched(stateSnapshot) ||
            ServiceTestSession::blocksProcessResume()) {
          request->send(409, "application/json",
                        "{\"success\":false,\"message\":\"Recovery requires an idle controller with safety clear\"}");
          return;
        }

        ProcessResume::PersistedSnapshot saved;
        if (!ProcessResume::getPending(saved)) {
          request->send(404, "application/json",
                        "{\"success\":false,\"message\":\"No process snapshot available\"}");
          return;
        }
        JsonDocument doc;
        const DeserializationError error = deserializeJson(doc, data, len);
        if (error) {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Invalid JSON body\"}");
          return;
        }
        const bool offered = ProcessResume::shouldOfferRecoveryHeads(saved);
        const float defaultTarget = offered
            ? (saved.recoveryActive != 0
                   ? saved.recoveryHeadsTargetMl
                   : ProcessResume::suggestedRecoveryHeadsMl(saved))
            : 0.0f;
        const float targetMl = doc["additionalHeadsMl"].isNull()
            ? defaultTarget
            : doc["additionalHeadsMl"].as<float>();
        if (!isfinite(targetMl) || targetMl < 0.0f || targetMl > 100.0f ||
            (!offered && targetMl > 0.0f) ||
            (saved.recoveryActive != 0 &&
             targetMl < saved.recoveryHeadsCollectedMl)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Additional heads must be 0-100 ml and cannot be below the amount already collected\"}");
          return;
        }

        uint32_t commandId = requestedCommandId(request);
        if (doc["commandId"].is<uint32_t>() && commandId == 0) {
          commandId = doc["commandId"].as<uint32_t>();
        }
        ControlCommandQueue::Status status =
            ControlCommandQueue::Status::Unknown;
        const auto result = ControlCommandQueue::enqueueAction(
            ControlCommandQueue::Type::RestoreRectification,
            ControlCommandQueue::Source::Api, 0, targetMl, commandId,
            ControlCommandQueue::Reason::OperatorRequest, &commandId, &status);
        sendControlCommandResponse(request, result, commandId, status);
      });

  server.on(
      "/api/process/resume/discard", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        SystemState stateSnapshot;
        if (!StateSnapshot::read(stateSnapshot)) {
          request->send(503, "application/json",
                        "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
          return;
        }
        if (stateSnapshot.mode != Mode::IDLE) {
          request->send(409, "application/json",
                        "{\"success\":false,\"message\":\"Stop the active process before discarding its snapshot\"}");
          return;
        }
        uint32_t commandId = requestedCommandId(request);
        ControlCommandQueue::Status status =
            ControlCommandQueue::Status::Unknown;
        const auto result = ControlCommandQueue::enqueueAction(
            ControlCommandQueue::Type::DiscardRectificationResume,
            ControlCommandQueue::Source::Api, 0, 0.0f, commandId,
            ControlCommandQueue::Reason::OperatorRequest, &commandId, &status);
        sendControlCommandResponse(request, result, commandId, status);
      });

  server.on(
      "/api/process/preflight", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"message\":\"Invalid JSON\"}")) {
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
        SystemState stateSnapshot;
        if (!StateSnapshot::read(stateSnapshot)) {
          request->send(503, "application/json",
                        "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
          return;
        }
        JsonDocument responseDoc;
        buildProcessPreflight(responseDoc, mode, modeStr, params,
                              stateSnapshot);

        String response;
        serializeJson(responseDoc, response);
        request->send(200, "application/json", response);
      });

  server.on(
      "/api/process/start", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"message\":\"Invalid JSON\"}")) {
          LOG_E("Process start: JSON parse error");
          return;
        }

        const char *modeStr = doc["mode"];
        if (!modeStr) {
          request->send(400, "application/json",
                        "{\"success\":false,\"message\":\"Mode required\"}");
          return;
        }

        JsonObject params = doc["params"].as<JsonObject>();

        SystemState stateSnapshot;
        if (!StateSnapshot::read(stateSnapshot)) {
          request->send(503, "application/json",
                        "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
          return;
        }

        Mode mode = Mode::IDLE;
        if (parseRequestedMode(modeStr, mode)) {
          JsonDocument startCheckDoc;
          if (!buildProcessPreflight(startCheckDoc, mode, modeStr, params,
                                     stateSnapshot)) {
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
            stateSnapshot.currentAlarm.type == AlarmType::SENSOR_FAILURE;

        if (Safety::isLatched(stateSnapshot) && !allowDemoSensorFailure) {
          Logger::logf(1, "Process start rejected: safety alarm is latched (%s)",
                       stateSnapshot.currentAlarm.message[0]
                           ? stateSnapshot.currentAlarm.message
                           : Safety::getAlarmTypeToken(stateSnapshot.currentAlarm.type));
          JsonDocument errorDoc;
          errorDoc["success"] = false;
          errorDoc["message"] =
              "Safety alarm is latched. Reset the alarm before starting.";
          JsonObject alarm = errorDoc["alarm"].to<JsonObject>();
          fillAlarmJson(alarm, stateSnapshot, g_settings);

          String response;
          serializeJson(errorDoc, response);
          request->send(409, "application/json", response);
          return;
        }

        bool sensorsOk =
            stateSnapshot.health.tempSensorsTotal > 0 &&
            stateSnapshot.health.tempSensorsOk;

        if (!sensorsOk) {
          LOG_W("Starting process without temperature sensors!");
        }

        if (stateSnapshot.mode != Mode::IDLE) {
          request->send(409, "application/json",
                        "{\"success\":false,\"message\":\"Process already active\"}");
          return;
        }

        if (mode == Mode::RECTIFICATION || mode == Mode::DISTILLATION ||
            mode == Mode::NBK) {
          applyBoosterStartOverride(params, g_settings);
        }

        ControlCommandQueue::StartRequest startRequest;
        startRequest.mode = mode;
        if (mode == Mode::DISTILLATION) {
          const float speed = EditableFields::normalize(
              EditableFields::Id::DIST_SPEED_ML_H,
              params["speed"] | 500.0f);
          const float headsVol = EditableFields::normalize(
              EditableFields::Id::DIST_HEADS_VOLUME_ML,
              params["headsVolume"] | 0.0f);
          const float targetVol = EditableFields::normalize(
              EditableFields::Id::DIST_TARGET_VOLUME_ML,
              params["targetVolume"] | 0.0f);
          const float tailsVol = EditableFields::normalize(
              EditableFields::Id::DIST_TAILS_VOLUME_ML,
              params["tailsVolume"] | 0.0f);
          const float endTemp = EditableFields::normalize(
              EditableFields::Id::DIST_END_TEMPERATURE_C,
              params["endTemp"] | 96.0f);
          RectTakeoffBackendType backendType =
              !params["takeoffBackendType"].isNull()
                  ? clampRectTakeoffBackendType(
                        params["takeoffBackendType"].as<uint32_t>())
                  : g_settings.distillationUi.takeoffBackendType;
          bool valveSafeVentConfirmed =
              !params["valveSafeVentConfirmed"].isNull()
                  ? params["valveSafeVentConfirmed"].as<bool>()
                  : g_settings.distillationUi.valveSafeVentConfirmed;
          const bool vaporTempControlEnabled =
              !params["vaporTempControlEnabled"].isNull()
                  ? params["vaporTempControlEnabled"].as<bool>()
                  : g_settings.distillationUi.vaporTempControlEnabled;
          const float vaporTempTargetC = !params["vaporTempTargetC"].isNull()
              ? clampFloatRange(params["vaporTempTargetC"].as<float>(), 20.0f, 110.0f)
              : g_settings.distillationUi.vaporTempTargetC;
          uint8_t vaporTempMinPowerPercent = !params["vaporTempMinPowerPercent"].isNull()
              ? clampU8Range(params["vaporTempMinPowerPercent"].as<uint32_t>(), 0, 100)
              : g_settings.distillationUi.vaporTempMinPowerPercent;
          uint8_t vaporTempMaxPowerPercent = !params["vaporTempMaxPowerPercent"].isNull()
              ? clampU8Range(params["vaporTempMaxPowerPercent"].as<uint32_t>(), vaporTempMinPowerPercent, 100)
              : g_settings.distillationUi.vaporTempMaxPowerPercent;
          if (vaporTempMaxPowerPercent < vaporTempMinPowerPercent) {
            vaporTempMaxPowerPercent = vaporTempMinPowerPercent;
          }
          const uint16_t vaporTempTimeoutMin = !params["vaporTempTimeoutMin"].isNull()
              ? clampU16Range(params["vaporTempTimeoutMin"].as<uint32_t>(), 0, 720)
              : g_settings.distillationUi.vaporTempTimeoutMin;
          FractionProgram fractionProgram =
              params["fractionProgram"].is<JsonObject>()
                  ? parseFractionProgramJson(
                        params["fractionProgram"].as<JsonObject>(),
                        g_settings.fractionProgram)
                  : g_settings.fractionProgram;
          const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
                                          ? g_settings.equipment.heaterPowerW
                                          : DEFAULT_HEATER_POWER_W;
          uint16_t powerWatts = 0;
          if (!params["powerW"].isNull()) {
            powerWatts =
                clampU16Range(params["powerW"].as<uint32_t>(), 0, heaterMaxW);
          } else {
            const uint8_t powerPercent = static_cast<uint8_t>(
                EditableFields::normalize(
                    EditableFields::Id::DIST_POWER_PERCENT,
                    params["powerPercent"] | 60.0f));
            powerWatts = static_cast<uint16_t>(
                (static_cast<uint32_t>(heaterMaxW) * powerPercent) / 100U);
          }
          g_settings.distillationUi.speedMlH = speed;
          g_settings.distillationUi.headsVolumeMl = headsVol;
          g_settings.distillationUi.targetVolumeMl = targetVol;
          g_settings.distillationUi.tailsVolumeMl = tailsVol;
          g_settings.distillationUi.endTempC = endTemp;
          g_settings.distillationUi.powerW = powerWatts;
          g_settings.distillationUi.powerPercent =
              heaterMaxW > 0
                  ? (100.0f * static_cast<float>(powerWatts)) /
                        static_cast<float>(heaterMaxW)
                  : 0.0f;
          g_settings.distillationUi.takeoffBackendType = backendType;
          g_settings.distillationUi.valveSafeVentConfirmed =
              valveSafeVentConfirmed;
          g_settings.distillationUi.vaporTempControlEnabled = vaporTempControlEnabled;
          g_settings.distillationUi.vaporTempTargetC = vaporTempTargetC;
          g_settings.distillationUi.vaporTempMinPowerPercent = vaporTempMinPowerPercent;
          g_settings.distillationUi.vaporTempMaxPowerPercent = vaporTempMaxPowerPercent;
          g_settings.distillationUi.vaporTempTimeoutMin = vaporTempTimeoutMin;
          g_settings.fractionProgram = fractionProgram;
          if (!NVSManager::saveSettings(g_settings)) {
            request->send(500, "application/json",
                          "{\"success\":false,\"message\":\"Failed to save settings\"}");
            return;
          }
          startRequest.payloadKind = ControlCommandQueue::PayloadKind::Distillation;
          startRequest.distillationUi = g_settings.distillationUi;
          startRequest.fractionProgram = g_settings.fractionProgram;
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
                runtimeProfile.steps[count].type = mashStepTypeFromString(
                    s["type"] | "heat_hold");
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

          startRequest.payloadKind = ControlCommandQueue::PayloadKind::Mashing;
          startRequest.mashProfile = runtimeProfile;
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

          startRequest.payloadKind = ControlCommandQueue::PayloadKind::Hold;
          startRequest.holdStepCount = count;
          memcpy(startRequest.holdSteps, runtimeSteps,
                 sizeof(TempStep) * count);
        }

        uint32_t commandId = requestedCommandId(request);
        if (doc["commandId"].is<uint32_t>() && commandId == 0) {
          commandId = doc["commandId"].as<uint32_t>();
        }
        ControlCommandQueue::Status status =
            ControlCommandQueue::Status::Unknown;
        const auto result = ControlCommandQueue::enqueueStart(
            ControlCommandQueue::Source::Api, startRequest, commandId,
            ControlCommandQueue::Reason::OperatorRequest, &commandId, &status);
        if (result == ControlCommandQueue::EnqueueResult::Accepted ||
            result == ControlCommandQueue::EnqueueResult::Duplicate) {
          LOG_I("Process start queued: mode=%s, sensors=%s, command=%lu",
                modeStr, sensorsOk ? "OK" : "WARNING",
                static_cast<unsigned long>(commandId));
        }
        sendControlCommandResponse(request, result, commandId, status,
                                   sensorsOk);
      });

  server.on("/api/process/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    uint32_t commandId = requestedCommandId(request);
    ControlCommandQueue::Status status = ControlCommandQueue::Status::Unknown;
    const auto result = ControlCommandQueue::enqueue(
        ControlCommandQueue::Type::Stop, ControlCommandQueue::Source::Api,
        Mode::IDLE, commandId, ControlCommandQueue::Reason::OperatorRequest,
        &commandId, &status);
    sendControlCommandResponse(request, result, commandId, status);
  });

  server.on(
      "/api/process/pause", HTTP_POST, [](AsyncWebServerRequest *request) {
        uint32_t commandId = requestedCommandId(request);
        ControlCommandQueue::Status status = ControlCommandQueue::Status::Unknown;
        const auto result = ControlCommandQueue::enqueue(
            ControlCommandQueue::Type::Pause,
            ControlCommandQueue::Source::Api, Mode::IDLE, commandId,
            ControlCommandQueue::Reason::OperatorRequest, &commandId, &status);
        sendControlCommandResponse(request, result, commandId, status);
      });

  server.on(
      "/api/process/resume", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (ServiceTestSession::blocksProcessResume()) {
          request->send(
              409, "application/json",
              "{\"success\":false,\"message\":\"Service test session must be closed before resume\"}");
          return;
        }
        uint32_t commandId = requestedCommandId(request);
        ControlCommandQueue::Status status = ControlCommandQueue::Status::Unknown;
        const auto result = ControlCommandQueue::enqueue(
            ControlCommandQueue::Type::Resume,
            ControlCommandQueue::Source::Api, Mode::IDLE, commandId,
            ControlCommandQueue::Reason::OperatorRequest, &commandId, &status);
        sendControlCommandResponse(request, result, commandId, status);
      });

  auto registerProcessAction = [&](const char *path,
                                   ControlCommandQueue::Type type) {
    server.on(path, HTTP_POST, [type](AsyncWebServerRequest *request) {
      uint32_t commandId = requestedCommandId(request);
      ControlCommandQueue::Status status =
          ControlCommandQueue::Status::Unknown;
      const auto result = ControlCommandQueue::enqueueAction(
          type, ControlCommandQueue::Source::Api, 0, 0.0f, commandId,
          ControlCommandQueue::Reason::ProcessAction, &commandId, &status);
      sendControlCommandResponse(request, result, commandId, status);
    });
  };
  registerProcessAction("/api/fraction-program/next",
                        ControlCommandQueue::Type::AdvanceFractionProgram);
  registerProcessAction("/api/fraction-program/confirm",
                        ControlCommandQueue::Type::ConfirmFractionProgram);
  registerProcessAction("/api/mashing/next",
                        ControlCommandQueue::Type::MashingManualAdvance);

  server.on("/api/process/command-status", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              if (!request->hasParam("commandId")) {
                request->send(400, "application/json",
                              "{\"success\":false,\"message\":\"commandId required\"}");
                return;
              }
              const long rawId =
                  request->getParam("commandId")->value().toInt();
              if (rawId <= 0) {
                request->send(400, "application/json",
                              "{\"success\":false,\"message\":\"Invalid commandId\"}");
                return;
              }
              ControlCommandQueue::Status status =
                  ControlCommandQueue::Status::Unknown;
              if (!ControlCommandQueue::getStatus(
                      ControlCommandQueue::Source::Api,
                      static_cast<uint32_t>(rawId), status)) {
                request->send(404, "application/json",
                              "{\"success\":false,\"message\":\"Command result not found\"}");
                return;
              }
              JsonDocument responseDoc;
              responseDoc["success"] = true;
              responseDoc["accepted"] =
                  status == ControlCommandQueue::Status::Queued ||
                  status == ControlCommandQueue::Status::Processing ||
                  status == ControlCommandQueue::Status::Applied;
              responseDoc["applied"] =
                  status == ControlCommandQueue::Status::Applied;
              responseDoc["queued"] =
                  status == ControlCommandQueue::Status::Queued;
              responseDoc["commandId"] = static_cast<uint32_t>(rawId);
              responseDoc["status"] =
                  ControlCommandQueue::statusToken(status);
              String response;
              serializeJson(responseDoc, response);
              request->send(200, "application/json", response);
            });

  server.on(
      "/api/stirrer/start", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        if (request->contentLength() != 0) {
          return;
        }

        SystemState stateSnapshot;
        if (!StateSnapshot::read(stateSnapshot)) {
          request->send(503, "application/json",
                        "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
          return;
        }
        if (!ensureStirrerReady(request, stateSnapshot)) {
          return;
        }

        const uint8_t speed = g_settings.stirrer.defaultSpeedPercent;
        uint32_t controlCommandId = 0;
        ControlCommandQueue::Status controlStatus =
            ControlCommandQueue::Status::Unknown;
        const auto controlResult = ControlCommandQueue::enqueueAction(
            ControlCommandQueue::Type::SetStirrerAutoMode,
            ControlCommandQueue::Source::Api, 0, 0.0f, 0,
            ControlCommandQueue::Reason::OperatorRequest, &controlCommandId,
            &controlStatus);
        if (!controlCommandAccepted(controlResult)) {
          sendManualStirrerCommandResponse(request, stateSnapshot,
                                           controlResult, controlCommandId,
                                           controlStatus, nullptr,
                                           "Stirrer start rejected");
          return;
        }
        const PowerCommandGateway::Command outputCommand =
            PowerCommandGateway::setStirrerSpeed(
            PowerCommandGateway::Owner::MANUAL,
            PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT, speed);
        if (outputCommand.applied) LOG_I("Stirrer started via API at %u%%", speed);
        sendManualStirrerCommandResponse(request, stateSnapshot, controlResult,
                                         controlCommandId, controlStatus,
                                         &outputCommand,
                                         "Stirrer start command");
      },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        SystemState stateSnapshot;
        if (!StateSnapshot::read(stateSnapshot)) {
          request->send(503, "application/json",
                        "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
          return;
        }
        if (!ensureStirrerReady(request, stateSnapshot)) {
          return;
        }

        uint8_t speed = g_settings.stirrer.defaultSpeedPercent;
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}", true)) {
          return;
        }

        if (!doc.isNull()) {
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

        uint32_t controlCommandId = 0;
        ControlCommandQueue::Status controlStatus =
            ControlCommandQueue::Status::Unknown;
        const auto controlResult = ControlCommandQueue::enqueueAction(
            ControlCommandQueue::Type::SetStirrerAutoMode,
            ControlCommandQueue::Source::Api, 0, 0.0f, 0,
            ControlCommandQueue::Reason::OperatorRequest, &controlCommandId,
            &controlStatus);
        if (!controlCommandAccepted(controlResult)) {
          sendManualStirrerCommandResponse(request, stateSnapshot,
                                           controlResult, controlCommandId,
                                           controlStatus, nullptr,
                                           "Stirrer start rejected");
          return;
        }
        const PowerCommandGateway::Command outputCommand =
            PowerCommandGateway::setStirrerSpeed(
            PowerCommandGateway::Owner::MANUAL,
            PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT, speed);
        if (outputCommand.applied) LOG_I("Stirrer started via API at %u%%", speed);
        sendManualStirrerCommandResponse(request, stateSnapshot, controlResult,
                                         controlCommandId, controlStatus,
                                         &outputCommand,
                                         "Stirrer start command");
      });

  server.on("/api/stirrer/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    SystemState stateSnapshot;
    if (!StateSnapshot::read(stateSnapshot)) {
      request->send(503, "application/json",
                    "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
      return;
    }
    if (!ensureStirrerReady(request, stateSnapshot)) {
      return;
    }
    uint32_t controlCommandId = 0;
    ControlCommandQueue::Status controlStatus =
        ControlCommandQueue::Status::Unknown;
    const auto controlResult = ControlCommandQueue::enqueueAction(
        ControlCommandQueue::Type::SetStirrerAutoMode,
        ControlCommandQueue::Source::Api, 0, 0.0f, 0,
        ControlCommandQueue::Reason::OperatorRequest, &controlCommandId,
        &controlStatus);
    const PowerCommandGateway::Command outputCommand =
        PowerCommandGateway::stopStirrer(
        PowerCommandGateway::Owner::MANUAL,
        PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT);
    if (outputCommand.applied) LOG_I("Stirrer stopped via API");
    sendManualStirrerCommandResponse(request, stateSnapshot, controlResult,
                                     controlCommandId, controlStatus,
                                     &outputCommand, "Stirrer stop command");
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
        SystemState stateSnapshot;
        if (!StateSnapshot::read(stateSnapshot)) {
          request->send(503, "application/json",
                        "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
          return;
        }
        if (!ensureStirrerReady(request, stateSnapshot)) {
          return;
        }

        if (!Stirrer::isRunning()) {
          sendStirrerStateResponse(request, 409, false,
                                   "Stirrer is not running", stateSnapshot);
          return;
        }

        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}",
                false,
                "{\"success\":false,\"error\":\"Speed is required\"}")) {
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

        uint32_t controlCommandId = 0;
        ControlCommandQueue::Status controlStatus =
            ControlCommandQueue::Status::Unknown;
        const auto controlResult = ControlCommandQueue::enqueueAction(
            ControlCommandQueue::Type::SetStirrerAutoMode,
            ControlCommandQueue::Source::Api, 0, 0.0f, 0,
            ControlCommandQueue::Reason::OperatorRequest, &controlCommandId,
            &controlStatus);
        if (!controlCommandAccepted(controlResult)) {
          sendManualStirrerCommandResponse(request, stateSnapshot,
                                           controlResult, controlCommandId,
                                           controlStatus, nullptr,
                                           "Stirrer speed change rejected");
          return;
        }
        const PowerCommandGateway::Command outputCommand =
            PowerCommandGateway::setStirrerSpeed(
            PowerCommandGateway::Owner::MANUAL,
            PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
            static_cast<uint8_t>(requestedSpeed));
        if (outputCommand.applied) LOG_I("Stirrer speed changed via API to %d%%", requestedSpeed);
        sendManualStirrerCommandResponse(request, stateSnapshot, controlResult,
                                         controlCommandId, controlStatus,
                                         &outputCommand,
                                         "Stirrer speed command");
      });

  // Подтверждение смены тары в ручной ректификации
  server.on("/api/process/manual-rect/confirm-container", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (g_state.mode != Mode::MANUAL_RECT) {
                sendJsonError(request, 400, "Not in manual rectification mode");
                return;
              }

              if (!g_state.manualRectContainerChangePending) {
                sendJsonError(request, 400, "No container change pending");
                return;
              }

              const bool confirmed = FSM::ManualRect::confirmContainerChange(
                  g_state, g_settings);

              if (!confirmed) {
                sendJsonError(request, 500, "Failed to confirm container change");
                return;
              }

              JsonDocument response;
              response["success"] = true;
              response["message"] = "Container change confirmed";
              response["newPhase"] = static_cast<uint8_t>(g_state.rectPhase);
              response["containerChangePending"] = g_state.manualRectContainerChangePending;

              String json;
              serializeJson(response, json);
              request->send(200, "application/json", json);
            });
}
