#include "api_routes.h"

#include "../../history.h"
#include "../../history_demo.h"
#include "interface/webserver_shared.h"

void registerHistoryApiRoutes(AsyncWebServer &server) {
  server.on(
      "^\\/api\\/history\\/([0-9]+)$", HTTP_GET,
      [](AsyncWebServerRequest *request) {
        const String id = request->pathArg(0);
        ProcessHistory history;
        if (!loadProcessHistory(id, history)) {
          request->send(404, "application/json",
                        "{\"error\":\"Process not found\"}");
          return;
        }

        request->send(200, "application/json", exportProcessToJSON(history));
      });

  server.on(
      "^\\/api\\/history\\/([0-9]+)\\/advisor$", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        String body;
        if (!collectRequestBody(request, data, len, index, total, body)) {
          return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, body)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        const String id = request->pathArg(0);
        ProcessAdvisorSnapshot snapshot;
        snapshot.schemaVersion = doc["schemaVersion"].as<String>();
        snapshot.createdAt = doc["createdAt"] | 0;
        snapshot.baselineProcessId = doc["baselineProcessId"].as<String>();
        snapshot.baselineProfile = doc["baselineProfile"].as<String>();

        JsonArray items = doc["items"];
        size_t itemCount = 0;
        for (JsonObject itemObj : items) {
          if (itemCount >= 16) {
            break;
          }

          ProcessAdvisorItem item;
          item.kind = itemObj["kind"].as<String>();
          item.code = itemObj["code"].as<String>();
          item.tone = itemObj["tone"].as<String>();
          item.title = itemObj["title"].as<String>();
          item.detail = itemObj["detail"].as<String>();
          item.action = itemObj["action"].as<String>();
          item.parameterKey = itemObj["parameterKey"].as<String>();
          item.previousValue = itemObj["previousValue"] | 0.0f;
          item.suggestedValue = itemObj["suggestedValue"] | 0.0f;

          if (item.title.isEmpty()) {
            continue;
          }

          snapshot.items.push_back(item);
          itemCount++;
        }

        if (!updateProcessAdvisorSnapshot(id, snapshot)) {
          request->send(
              500, "application/json",
              "{\"success\":false,\"error\":\"Failed to update advisor snapshot\"}");
          return;
        }

        JsonDocument resp;
        resp["success"] = true;
        JsonObject temperatureTopology =
            resp["temperatureTopology"].to<JsonObject>();
        fillTemperatureTopologyJson(temperatureTopology, g_settings.equipment);
        JsonObject supportedModes = resp["supportedModes"].to<JsonObject>();
        fillTemperatureModeSupportJson(supportedModes, g_settings);

        String json;
        serializeJson(resp, json);
        request->send(200, "application/json", json);
      });

  server.on(
      "^\\/api\\/history\\/([0-9]+)\\/export$", HTTP_GET,
      [](AsyncWebServerRequest *request) {
        const String id = request->pathArg(0);
        const String format = request->hasParam("format")
                                  ? request->getParam("format")->value()
                                  : "csv";

        ProcessHistory history;
        if (!loadProcessHistory(id, history)) {
          request->send(404, "application/json",
                        "{\"error\":\"Process not found\"}");
          return;
        }

        String body;
        String contentType;
        String filename;
        if (format == "json") {
          body = exportProcessToJSON(history);
          contentType = "application/json; charset=utf-8";
          filename = "process_" + id + ".json";
        } else if (format == "csv") {
          body = exportProcessToCSV(history);
          contentType = "text/csv; charset=utf-8";
          filename = "process_" + id + ".csv";
        } else {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid format. Use csv or json\"}");
          return;
        }

        AsyncWebServerResponse *response =
            request->beginResponse(200, contentType, body);
        response->addHeader("Content-Disposition",
                            "attachment; filename=\"" + filename + "\"");
        request->send(response);
      });

  server.on(
      "^\\/api\\/history\\/([0-9]+)$", HTTP_DELETE,
      [](AsyncWebServerRequest *request) {
        const String id = request->pathArg(0);
        if (deleteProcess(id)) {
          request->send(200, "application/json",
                        "{\"success\":true,\"message\":\"Process deleted\"}");
          return;
        }

        request->send(404, "application/json",
                      "{\"error\":\"Process not found\"}");
      });

  server.on(
      "/api/history/demo", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        String body;
        if (!collectRequestBody(request, data, len, index, total, body)) {
          return;
        }

        bool replaceExisting = false;
        if (!body.isEmpty()) {
          JsonDocument doc;
          DeserializationError error = deserializeJson(doc, body);
          if (error) {
            request->send(400, "application/json",
                          "{\"success\":false,\"error\":\"Invalid JSON\"}");
            return;
          }
          replaceExisting = doc["replace"] | false;
        }

        DemoHistorySeedResult result;
        if (!seedPublicDemoDataset(result, replaceExisting)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to seed demo dataset\"}");
          return;
        }

        JsonDocument responseDoc;
        responseDoc["success"] = true;
        responseDoc["imported"] = result.imported;
        responseDoc["skipped"] = result.skipped;
        responseDoc["removed"] = result.removed;
        responseDoc["demoCount"] = countPublicDemoDatasetEntries();

        String response;
        serializeJson(responseDoc, response);
        request->send(200, "application/json", response);
      });

  server.on("/api/history/demo", HTTP_DELETE,
            [](AsyncWebServerRequest *request) {
              DemoHistorySeedResult result;
              if (!clearPublicDemoDataset(result)) {
                request->send(
                    500, "application/json",
                    "{\"success\":false,\"error\":\"Failed to clear demo dataset\"}");
                return;
              }

              JsonDocument responseDoc;
              responseDoc["success"] = true;
              responseDoc["removed"] = result.removed;
              responseDoc["demoCount"] = countPublicDemoDatasetEntries();

              String response;
              serializeJson(responseDoc, response);
              request->send(200, "application/json", response);
            });

  server.on("/api/history", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    if (clearHistory()) {
      request->send(200, "application/json",
                    "{\"success\":true,\"message\":\"All history cleared\"}");
      return;
    }

    request->send(500, "application/json",
                  "{\"error\":\"Failed to clear history\"}");
  });

  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *request) {
    const std::vector<ProcessListItem> processes = getProcessList();

    JsonDocument doc;
    doc["total"] = processes.size();
    JsonArray processArray = doc["processes"].to<JsonArray>();

    for (const auto &process : processes) {
      JsonObject item = processArray.add<JsonObject>();
      item["id"] = process.id;
      item["type"] = process.type;
      item["mode"] = process.mode;
      item["profileId"] = process.profileId;
      item["profile"] = process.profile;
      item["startTime"] = process.startTime;
      item["duration"] = process.duration;
      item["status"] = process.status;
      item["completedSuccessfully"] = process.completedSuccessfully;
      item["totalVolume"] = process.totalVolume;
      item["completionState"] = process.completionState;
      item["completionReasonCode"] = process.completionReasonCode;
      item["completionOperatorMessage"] = process.completionOperatorMessage;
      item["lastPhaseName"] = process.lastPhaseName;
      item["lastReasonCode"] = process.lastReasonCode;
      item["lastOperatorMessage"] = process.lastOperatorMessage;
      item["safetyTrip"] = process.safetyTrip;
      item["safetyAck"] = process.safetyAck;
      item["safetyReset"] = process.safetyReset;
      item["safetyRecovery"] = process.safetyRecovery;
      item["safetyLimited"] = process.safetyLimited;
      item["safetyState"] = process.safetyState;
      item["safetySummary"] = process.safetySummary;
      JsonObject indicatorsSummary = item["indicatorsSummary"].to<JsonObject>();
      indicatorsSummary["available"] = process.indicatorsAvailable;
      indicatorsSummary["samples"] = process.indicatorSamples;
      indicatorsSummary["avgProcessHealth"] = process.avgProcessHealth;
      indicatorsSummary["minProcessHealth"] = process.minProcessHealth;
      indicatorsSummary["avgStabilityIndex"] = process.avgStabilityIndex;
      indicatorsSummary["minCoolingMarginC"] = process.minCoolingMarginC;
      indicatorsSummary["maxFloodRisk"] = process.maxFloodRisk;
      indicatorsSummary["takeoffShare"] = process.takeoffShare;
      indicatorsSummary["freshnessShare"] = process.freshnessShare;
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
}
