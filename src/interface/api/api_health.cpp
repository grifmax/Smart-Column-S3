#include "api_routes.h"

#include "../../config.h"
#include "../../fs_compat.h"
#include <WiFi.h>

#include "control/control_command_queue.h"
#include "control/power_command_gateway.h"
#include "control/runtime_metrics.h"
#include "control/state_snapshot.h"
#include "interface/webserver_shared.h"

void registerHealthApiRoutes(AsyncWebServer &server) {
  server.on("/api/outputs/command", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              const char *parameter = request->hasParam("commandId")
                                          ? "commandId"
                                          : "id";
              if (!request->hasParam(parameter)) {
                request->send(400, "application/json",
                              "{\"success\":false,\"error\":\"commandId is required\"}");
                return;
              }
              const uint32_t commandId =
                  static_cast<uint32_t>(request->getParam(parameter)->value().toInt());
              PowerCommandGateway::Command command;
              if (!PowerCommandGateway::getCommandStatus(commandId, command)) {
                request->send(404, "application/json",
                              "{\"success\":false,\"error\":\"Command status not found\"}");
                return;
              }
              JsonDocument response;
              response["commandId"] = command.sequence;
              response["accepted"] = command.accepted;
              response["queued"] = command.queued;
              response["applied"] = command.applied;
              response["success"] = command.accepted && command.applied;
              response["status"] = !command.accepted ? "rejected"
                                    : command.applied ? "applied" : "queued";
              response["owner"] = static_cast<uint8_t>(command.owner);
              response["target"] = static_cast<uint8_t>(command.target);
              response["reason"] = static_cast<uint8_t>(command.reason);
              response["requestedValue"] = command.requestedValue;
              response["appliedValue"] = command.appliedValue;
              response["timestampMs"] = command.timestampMs;
              response["rejectReason"] =
                  PowerCommandGateway::rejectReasonString(command.rejectReason);
              response["rejectReasonCode"] =
                  static_cast<uint8_t>(command.rejectReason);
              String json;
              serializeJson(response, json);
              request->send(200, "application/json", json);
            });

  server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest *request) {
    SystemState snapshot;
    if (!StateSnapshot::read(snapshot)) {
      request->send(503, "application/json",
                    "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
      return;
    }
    const SystemState &st = snapshot;

    JsonDocument doc;

    JsonObject temps = doc["temperatures"].to<JsonObject>();
    temps["ok"] = st.health.tempSensorsOk;
    temps["total"] = st.health.tempSensorsTotal;

    JsonObject sensors = doc["sensors"].to<JsonObject>();
    sensors["bmp280"] = st.health.bmp280Ok;
    sensors["ads1115"] = st.health.ads1115Ok;
    sensors["pzem"] = st.health.pzemOk;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = st.health.wifiConnected;
    wifi["rssi"] = st.health.wifiRSSI;

    JsonObject system = doc["system"].to<JsonObject>();
    system["uptime"] = st.health.uptime;
    system["freeHeap"] = st.health.freeHeap;
    system["cpuTemp"] = st.health.cpuTemp;

    JsonObject errors = doc["errors"].to<JsonObject>();
    errors["pzemSpikes"] = st.health.pzemSpikeCount;
    errors["tempErrors"] = st.health.tempReadErrors;

    doc["overallHealth"] = st.health.overallHealth;
    doc["lastUpdate"] = st.health.lastUpdate;

    JsonArray scores = doc["healthScores"].to<JsonArray>();
    for (int i = 0; i < 6; i++) {
      scores.add(st.health.healthScores[i]);
    }

    JsonObject reboot = doc["reboot"].to<JsonObject>();
    reboot["reason"] = g_rebootTracker.lastReason;
    reboot["reasonStr"] = g_rebootTracker.lastReasonStr;

    JsonObject control = doc["control"].to<JsonObject>();
    const ControlRuntimeMetrics::Snapshot runtime =
        ControlRuntimeMetrics::getSnapshot();
    JsonObject commands = control["commands"].to<JsonObject>();
    commands["pending"] = ControlCommandQueue::pending();
    commands["dropped"] = ControlCommandQueue::droppedCount();
    commands["cancelled"] = ControlCommandQueue::cancelledCount();
    commands["coalescedStops"] = ControlCommandQueue::coalescedStopCount();
    commands["applied"] = ControlCommandQueue::appliedCount();
    commands["processCount"] = runtime.controlCommandBatchCount;
    commands["processed"] = runtime.controlCommandsProcessed;
    commands["lastProcessUs"] = runtime.lastControlCommandBatchUs;
    commands["maxProcessUs"] = runtime.maxControlCommandBatchUs;

    JsonObject outputs = control["outputs"].to<JsonObject>();
    outputs["pending"] = PowerCommandGateway::pendingCommandCount();
    outputs["dropped"] = PowerCommandGateway::droppedCommandCount();
    outputs["cancelled"] = PowerCommandGateway::cancelledCommandCount();
    outputs["coalesced"] = PowerCommandGateway::coalescedCommandCount();
    outputs["ownerRejected"] =
        PowerCommandGateway::ownerRejectedCommandCount();
    outputs["safetyRejected"] =
        PowerCommandGateway::safetyRejectedCommandCount();
    outputs["applied"] = PowerCommandGateway::appliedCommandCount();
    outputs["processCount"] = runtime.outputCommandBatchCount;
    outputs["processed"] = runtime.outputCommandsProcessed;
    outputs["lastProcessUs"] = runtime.lastOutputCommandBatchUs;
    outputs["maxProcessUs"] = runtime.maxOutputCommandBatchUs;
    outputs["safetyTripLatched"] =
        PowerCommandGateway::isSafetyTripLatched();
    JsonArray outputStates = outputs["targets"].to<JsonArray>();
    for (uint8_t targetIndex = 0;
         targetIndex <= static_cast<uint8_t>(PowerCommandGateway::Target::ALL_OUTPUTS);
         ++targetIndex) {
      const auto target = static_cast<PowerCommandGateway::Target>(targetIndex);
      PowerCommandGateway::OutputState outputState;
      if (!PowerCommandGateway::getOutputState(target, outputState)) continue;
      JsonObject targetState = outputStates.add<JsonObject>();
      targetState["target"] = targetIndex;
      targetState["requestedCommandId"] = outputState.requestedCommandId;
      targetState["requestedAtMs"] = outputState.requestedAtMs;
      targetState["requestedValue"] = outputState.requestedValue;
      targetState["requestAccepted"] = outputState.requestAccepted;
      targetState["requestQueued"] = outputState.requestQueued;
      targetState["rejectReason"] = PowerCommandGateway::rejectReasonString(
          outputState.rejectReason);
      targetState["appliedCommandId"] = outputState.appliedCommandId;
      targetState["appliedAtMs"] = outputState.appliedAtMs;
      targetState["appliedValue"] = outputState.appliedValue;
      targetState["hasAppliedValue"] = outputState.hasAppliedValue;
      targetState["appliedSource"] = "gateway_driver_call";
    }
    const PowerCommandGateway::Command lastOutputCommand =
        PowerCommandGateway::getLastCommand();
    JsonObject lastOutput = outputs["lastCommand"].to<JsonObject>();
    lastOutput["sequence"] = lastOutputCommand.sequence;
    lastOutput["timestampMs"] = lastOutputCommand.timestampMs;
    lastOutput["owner"] = static_cast<uint8_t>(lastOutputCommand.owner);
    lastOutput["target"] = static_cast<uint8_t>(lastOutputCommand.target);
    lastOutput["reason"] = static_cast<uint8_t>(lastOutputCommand.reason);
    lastOutput["requestedValue"] = lastOutputCommand.requestedValue;
    lastOutput["accepted"] = lastOutputCommand.accepted;
    lastOutput["queued"] = lastOutputCommand.queued;
    lastOutput["applied"] = lastOutputCommand.applied;
    lastOutput["appliedValue"] = lastOutputCommand.appliedValue;
    lastOutput["rejectReason"] =
        static_cast<uint8_t>(lastOutputCommand.rejectReason);

    JsonObject stateSnapshot = control["stateSnapshot"].to<JsonObject>();
    stateSnapshot["sequence"] = StateSnapshot::sequence();
    stateSnapshot["readRetries"] = StateSnapshot::retryCount();
    stateSnapshot["publishSkips"] = StateSnapshot::publishSkipCount();
    stateSnapshot["ageMs"] = StateSnapshot::ageMs(millis());

    JsonObject cycle = control["cycle"].to<JsonObject>();
    cycle["count"] = runtime.cycleCount;
    cycle["lastUs"] = runtime.lastCycleUs;
    cycle["maxUs"] = runtime.maxCycleUs;
    cycle["maxGapUs"] = runtime.maxCycleGapUs;
    cycle["le1Ms"] = runtime.cyclesLe1Ms;
    cycle["le5Ms"] = runtime.cyclesLe5Ms;
    cycle["le10Ms"] = runtime.cyclesLe10Ms;
    cycle["gt10Ms"] = runtime.cyclesGt10Ms;
    cycle["controlBatchCount"] = runtime.controlBatchCount;
    cycle["lastControlBatchUs"] = runtime.lastControlBatchUs;
    cycle["maxControlBatchUs"] = runtime.maxControlBatchUs;

    JsonObject safety = control["safetyTiming"].to<JsonObject>();
    safety["tripCount"] = runtime.safetyTripCount;
    safety["sampleAgeKnownCount"] = runtime.safetySampleAgeKnownCount;
    safety["lastSampleAgeMs"] = runtime.lastSafetySampleAgeMs;
    safety["maxSampleAgeMs"] = runtime.maxSafetySampleAgeMs;
    safety["lastOutputStopUs"] = runtime.lastOutputStopUs;
    safety["maxOutputStopUs"] = runtime.maxOutputStopUs;
    safety["repeatedTrips"] = runtime.repeatedSafetyTripCount;
    safety["staleSensorChecks"] = runtime.staleSensorCheckCount;

    JsonObject snapshotCopy = control["snapshotCopy"].to<JsonObject>();
    snapshotCopy["count"] = runtime.snapshotCopyCount;
    snapshotCopy["lastUs"] = runtime.lastSnapshotCopyUs;
    snapshotCopy["maxUs"] = runtime.maxSnapshotCopyUs;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;

    JsonObject firmware = doc["firmware"].to<JsonObject>();
    firmware["version"] = FIRMWARE_VERSION;
    firmware["buildDate"] = __DATE__;
    firmware["buildTime"] = __TIME__;
    firmware["compiler"] = "GCC " __VERSION__;

    JsonObject board = doc["board"].to<JsonObject>();
    board["chip"] = "ESP32-S3";
    board["flashSize"] = ESP.getFlashChipSize();
    board["psramSize"] = ESP.getPsramSize();
    board["cpuFreq"] = ESP.getCpuFreqMHz();
    board["mac"] = WiFi.macAddress();

    char deviceId[13] = {0};
    uint64_t efuseMac = ESP.getEfuseMac();
    snprintf(deviceId, sizeof(deviceId), "%012llX",
             (unsigned long long)(efuseMac & 0xFFFFFFFFFFFFULL));
    board["deviceId"] = deviceId;

#ifdef USE_LITTLEFS
    File versionFile = LittleFS.open("/version.json", "r");
#else
    File versionFile = LittleFS.open("/version.json", "r");
#endif

    if (versionFile) {
      JsonDocument frontendDoc;
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
}
