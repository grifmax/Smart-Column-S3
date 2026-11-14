#include "settings_modules.h"

#include "config.h"
#include "control/control_command_queue.h"
#include "control/fsm.h"
#include "control/power_command_gateway.h"
#include "control/rect_takeoff.h"
#include "control/state_snapshot.h"
#include "control/watt_control.h"
#include "interface/editable_field_descriptors.h"
#include "interface/webserver_shared.h"

namespace {

const char *outputStatus(const PowerCommandGateway::Command &command) {
  if (!command.accepted) return "rejected";
  return command.applied ? "applied" : "queued";
}

void appendOutputCommand(JsonObject response,
                         const PowerCommandGateway::Command &command) {
  response["commandId"] = command.sequence;
  response["commandType"] = "output";
  response["accepted"] = command.accepted;
  response["queued"] = command.queued;
  response["applied"] = command.applied;
  response["success"] = command.accepted && command.applied;
  response["status"] = outputStatus(command);
  response["rejectReason"] =
      PowerCommandGateway::rejectReasonString(command.rejectReason);
}

void sendOutputCommand(AsyncWebServerRequest *request,
                       const PowerCommandGateway::Command &command,
                       JsonDocument &response) {
  appendOutputCommand(response.as<JsonObject>(), command);
  String body;
  serializeJson(response, body);
  const int status = !command.accepted ? 409 : command.queued ? 202 : 200;
  request->send(status, "application/json", body);
}

void sendOutputCommandBatch(AsyncWebServerRequest *request,
                            const PowerCommandGateway::Command *commands,
                            uint8_t count) {
  bool accepted = count > 0;
  bool applied = count > 0;
  bool queued = false;
  JsonDocument response;
  response["commandType"] = "output";
  JsonArray results = response["commands"].to<JsonArray>();
  for (uint8_t i = 0; i < count; ++i) {
    const auto &command = commands[i];
    accepted = accepted && command.accepted;
    applied = applied && command.applied;
    queued = queued || command.queued;
    JsonObject result = results.add<JsonObject>();
    result["target"] = static_cast<uint8_t>(command.target);
    appendOutputCommand(result, command);
  }
  response["accepted"] = accepted;
  response["queued"] = queued;
  response["applied"] = applied;
  response["success"] = accepted && applied;
  response["status"] = !accepted ? "rejected" : applied ? "applied" : "queued";
  if (count == 1) response["commandId"] = commands[0].sequence;
  String body;
  serializeJson(response, body);
  request->send(!accepted ? 409 : queued ? 202 : 200,
                "application/json", body);
}

void sendQueuedRuntimeCommand(AsyncWebServerRequest *request,
                              ControlCommandQueue::EnqueueResult result,
                              uint32_t commandId,
                              ControlCommandQueue::Status status) {
  const bool accepted = result == ControlCommandQueue::EnqueueResult::Accepted ||
                        result == ControlCommandQueue::EnqueueResult::Duplicate;
  const bool queued = status == ControlCommandQueue::Status::Queued;
  const bool applied = status == ControlCommandQueue::Status::Applied;
  JsonDocument response;
  response["success"] = accepted && applied;
  response["accepted"] = accepted;
  response["applied"] = applied;
  response["queued"] = queued;
  response["commandId"] = commandId;
  response["commandType"] = "control";
  response["status"] = ControlCommandQueue::statusToken(status);
  String body;
  serializeJson(response, body);
  const int httpStatus = (accepted && queued) ? 202
                         : (accepted && applied) ? 200
                         : (result == ControlCommandQueue::EnqueueResult::Full)
                             ? 429
                             : (result == ControlCommandQueue::EnqueueResult::NotInitialized)
                                 ? 503
                                 : 409;
  request->send(httpStatus, "application/json", body);
}

} // namespace

void registerRuntimeSettingsApiRoutes(AsyncWebServer &server) {
  server.on(
      "/api/manual/heater", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(request, data, len, index, total, doc,
                                        "{\"error\":\"Invalid JSON\"}")) {
          return;
        }

        const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
                                        ? g_settings.equipment.heaterPowerW
                                        : DEFAULT_HEATER_POWER_W;
        uint16_t powerWatts = 0;
        if (!doc["powerW"].isNull()) {
          powerWatts =
              clampU16Range(doc["powerW"].as<uint32_t>(), 0, heaterMaxW);
        } else {
          const float normalizedPower = EditableFields::normalize(
              EditableFields::Id::MANUAL_HEATER_POWER_PERCENT,
              doc["power"] | 0.0f);
          const uint8_t powerPercent = static_cast<uint8_t>(normalizedPower);
          powerWatts = static_cast<uint16_t>(
              (static_cast<uint32_t>(heaterMaxW) * powerPercent) / 100U);
        }
        const PowerCommandGateway::Command command =
            PowerCommandGateway::setHeaterPowerWatts(
            PowerCommandGateway::Owner::MANUAL,
            PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT, powerWatts);

        const uint8_t powerPercent =
            heaterMaxW > 0
                ? static_cast<uint8_t>(
                      (static_cast<uint32_t>(powerWatts) * 100U +
                       heaterMaxW / 2U) /
                      heaterMaxW)
                : 0;
        JsonDocument response;
        response["powerW"] = powerWatts;
        response["powerPercent"] = powerPercent;
        sendOutputCommand(request, command, response);
      });

  server.on(
      "/api/rect/heater", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(request, data, len, index, total, doc,
                                        "{\"error\":\"Invalid JSON\"}")) {
          return;
        }

        const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
                                        ? g_settings.equipment.heaterPowerW
                                        : DEFAULT_HEATER_POWER_W;
        int powerWatts = -2;
        if (!doc["powerW"].isNull()) {
          powerWatts = doc["powerW"].as<int>();
        } else {
          int powerPercent = doc["power"] | -2;
          if (powerPercent >= 0) {
            if (powerPercent > 100) {
              powerPercent = 100;
            }
            powerWatts = static_cast<int>(
                (static_cast<uint32_t>(heaterMaxW) * powerPercent) / 100U);
          } else {
            powerWatts = -1;
          }
        }
        if (powerWatts < -1) {
          powerWatts = -1;
        }
        if (powerWatts > heaterMaxW) {
          powerWatts = heaterMaxW;
        }
        uint32_t commandId = 0;
        ControlCommandQueue::Status status =
            ControlCommandQueue::Status::Unknown;
        const auto result = ControlCommandQueue::enqueueAction(
            ControlCommandQueue::Type::SetHeaterOverride,
            ControlCommandQueue::Source::Api, 0,
            static_cast<float>(powerWatts), 0,
            ControlCommandQueue::Reason::OperatorRequest, &commandId,
            &status);
        sendQueuedRuntimeCommand(request, result, commandId, status);
      });

  server.on(
      "/api/manual/pump", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(request, data, len, index, total, doc,
                                        "{\"error\":\"Invalid JSON\"}")) {
          return;
        }

        const float speed = EditableFields::normalize(
            EditableFields::Id::MANUAL_PUMP_SPEED_ML_H,
            doc["speed"] | 0.0f);
        SystemState stateSnapshot;
        if (!StateSnapshot::read(stateSnapshot)) {
          sendJsonError(request, 503, "State snapshot unavailable", false);
          return;
        }
        if (stateSnapshot.mode == Mode::MANUAL_RECT) {
          uint32_t commandId = doc["commandId"] | 0U;
          ControlCommandQueue::Status status =
              ControlCommandQueue::Status::Unknown;
          const auto result = ControlCommandQueue::enqueueAction(
              ControlCommandQueue::Type::SetManualRectRate,
              ControlCommandQueue::Source::Api, 0, speed, commandId,
              ControlCommandQueue::Reason::OperatorRequest, &commandId,
              &status);
          sendQueuedRuntimeCommand(request, result, commandId, status);
          return;
        }

        if (speed <= 0.0f) {
          const PowerCommandGateway::Command command =
              PowerCommandGateway::setPumpSpeed(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT, 0.0f);
          JsonDocument response;
          response["requestedRunning"] = false;
          if (command.applied) response["running"] = false;
          sendOutputCommand(request, command, response);
          return;
        }

        const PowerCommandGateway::Command command =
            PowerCommandGateway::setPumpSpeed(
            PowerCommandGateway::Owner::MANUAL,
            PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT, speed);
        JsonDocument response;
        response["requestedRunning"] = speed > 0.0f;
        if (command.applied) response["running"] = speed > 0.0f;
        response["speed"] = speed;
        sendOutputCommand(request, command, response);
      });

  server.on(
      "/api/manual/valves", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(request, data, len, index, total, doc,
                                        "{\"error\":\"Invalid JSON\"}")) {
          return;
        }

        const bool allOff = doc["allOff"] | false;
        PowerCommandGateway::Command commands[6];
        uint8_t commandCount = 0;
        if (allOff) {
          commands[commandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
              PowerCommandGateway::Valve::WATER, false);
          commands[commandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
              PowerCommandGateway::Valve::HEADS, false);
          commands[commandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
              PowerCommandGateway::Valve::BODY, false);
          commands[commandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
              PowerCommandGateway::Valve::TAILS, false);
          commands[commandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
              PowerCommandGateway::Valve::UNO, false);
          commands[commandCount++] = PowerCommandGateway::setCoolingPwm(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT, 0);
          sendOutputCommandBatch(request, commands, commandCount);
          return;
        }

        if (!doc["water"].isNull()) {
          commands[commandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
              PowerCommandGateway::Valve::WATER, doc["water"].as<bool>());
        }
        if (!doc["heads"].isNull()) {
          commands[commandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
              PowerCommandGateway::Valve::HEADS, doc["heads"].as<bool>());
        }
        if (!doc["body"].isNull()) {
          commands[commandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
              PowerCommandGateway::Valve::BODY, doc["body"].as<bool>());
        }
        if (!doc["tails"].isNull()) {
          commands[commandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
              PowerCommandGateway::Valve::TAILS, doc["tails"].as<bool>());
        }
        if (!doc["uno"].isNull()) {
          commands[commandCount++] = PowerCommandGateway::setValve(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
              PowerCommandGateway::Valve::UNO, doc["uno"].as<bool>());
        }
        if (!doc["startStopDuty"].isNull()) {
          const uint8_t duty =
              clampU8Range(doc["startStopDuty"].as<uint32_t>(), 0, 255);
          commands[commandCount++] = PowerCommandGateway::setCoolingPwm(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT, duty);
        }

        if (commandCount == 0) {
          sendJsonError(request, 400, "At least one output field is required");
          return;
        }
        sendOutputCommandBatch(request, commands, commandCount);
      });

  server.on(
      "/api/manual/phase", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(request, data, len, index, total, doc,
                                        "{\"error\":\"Invalid JSON\"}")) {
          return;
        }

        SystemState stateSnapshot;
        if (!StateSnapshot::read(stateSnapshot)) {
          sendJsonError(request, 503, "State snapshot unavailable", false);
          return;
        }
        if (stateSnapshot.mode != Mode::MANUAL_RECT) {
          sendJsonError(request, 400, "Not in MANUAL_RECT mode", false);
          return;
        }

        if (doc["phase"].isNull()) {
          sendJsonError(request, 400, "Missing phase", false);
          return;
        }

        const uint8_t phaseId = doc["phase"].as<uint8_t>();
        if (phaseId == static_cast<uint8_t>(RectPhase::IDLE) ||
            phaseId > static_cast<uint8_t>(RectPhase::COMPLETED)) {
          sendJsonError(request, 400, "Invalid phase", false);
          return;
        }
        uint32_t commandId = doc["commandId"] | 0U;
        ControlCommandQueue::Status status =
            ControlCommandQueue::Status::Unknown;
        const auto result = ControlCommandQueue::enqueueAction(
            ControlCommandQueue::Type::SetManualRectPhase,
            ControlCommandQueue::Source::Api, phaseId, 0.0f, commandId,
            ControlCommandQueue::Reason::OperatorRequest, &commandId,
            &status);
        sendQueuedRuntimeCommand(request, result, commandId, status);
      });

  server.on(
      "/api/manual/volumes", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(request, data, len, index, total, doc,
                                        "{\"error\":\"Invalid JSON\"}")) {
          return;
        }

        uint8_t fieldMask = 0;
        float headsMl = 0.0f;
        float bodyMl = 0.0f;
        float tailsMl = 0.0f;
        if (!doc["heads"].isNull()) {
          headsMl = doc["heads"].as<float>();
          fieldMask |= 0x01U;
        }
        if (!doc["body"].isNull()) {
          bodyMl = doc["body"].as<float>();
          fieldMask |= 0x02U;
        }
        if (!doc["tails"].isNull()) {
          tailsMl = doc["tails"].as<float>();
          fieldMask |= 0x04U;
        }

        if (fieldMask == 0) {
          sendJsonError(request, 400, "At least one field is required", false);
          return;
        }

        const bool syncTotal = doc["syncTotal"] | true;
        uint32_t commandId = doc["commandId"] | 0U;
        ControlCommandQueue::Status status =
            ControlCommandQueue::Status::Unknown;
        const auto result = ControlCommandQueue::enqueueVolumes(
            ControlCommandQueue::Source::Api, fieldMask, headsMl, bodyMl,
            tailsMl, syncTotal, commandId, &commandId, &status);
        sendQueuedRuntimeCommand(request, result, commandId, status);
      });
}
