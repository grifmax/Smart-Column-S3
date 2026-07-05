#include "api_routes.h"

#include "../../history.h"
#include "../../profiles.h"
#include "interface/webserver_shared.h"
#include "storage/logger.h"

void registerProfilesRoutes(AsyncWebServer &server) {
  server.on("/api/profiles/export", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              bool includeBuiltin = request->hasParam("includeBuiltin");
              String json = exportAllProfilesToJSON(includeBuiltin);
              request->send(200, "application/json", json);
            });

  server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)$", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              String id = request->pathArg(0);

              Profile profile;
              if (loadProfile(id, profile)) {
                JsonDocument doc;

                doc["id"] = profile.id;

                JsonObject metadata = doc["metadata"].to<JsonObject>();
                metadata["name"] = profile.metadata.name;
                metadata["description"] = profile.metadata.description;
                metadata["category"] = profile.metadata.category;

                JsonArray tags = metadata["tags"].to<JsonArray>();
                for (const auto &tag : profile.metadata.tags) {
                  tags.add(tag);
                }

                metadata["created"] = profile.metadata.created;
                metadata["updated"] = profile.metadata.updated;
                metadata["author"] = profile.metadata.author;
                metadata["isBuiltin"] = profile.metadata.isBuiltin;

                JsonObject parameters = doc["parameters"].to<JsonObject>();
                parameters["mode"] = profile.parameters.mode;
                parameters["model"] = profile.parameters.model;

                JsonObject heater = parameters["heater"].to<JsonObject>();
                heater["maxPower"] = profile.parameters.heater.maxPower;
                heater["autoMode"] = profile.parameters.heater.autoMode;
                heater["pidKp"] = profile.parameters.heater.pidKp;
                heater["pidKi"] = profile.parameters.heater.pidKi;
                heater["pidKd"] = profile.parameters.heater.pidKd;
                heater["boosterEnabled"] = profile.parameters.heater.boosterEnabled;
                heater["boosterStopCubeTempC"] =
                    profile.parameters.heater.boosterStopCubeTempC;

                JsonObject rectification =
                    parameters["rectification"].to<JsonObject>();
                rectification["stabilizationMin"] =
                    profile.parameters.rectification.stabilizationMin;
                rectification["headsVolume"] =
                    profile.parameters.rectification.headsVolume;
                rectification["bodyVolume"] =
                    profile.parameters.rectification.bodyVolume;
                rectification["tailsVolume"] =
                    profile.parameters.rectification.tailsVolume;
                rectification["headsSpeed"] =
                    profile.parameters.rectification.headsSpeed;
                rectification["bodySpeed"] =
                    profile.parameters.rectification.bodySpeed;
                rectification["tailsSpeed"] =
                    profile.parameters.rectification.tailsSpeed;
                rectification["purgeMin"] =
                    profile.parameters.rectification.purgeMin;

                JsonObject distillation =
                    parameters["distillation"].to<JsonObject>();
                distillation["headsVolume"] =
                    profile.parameters.distillation.headsVolume;
                distillation["targetVolume"] =
                    profile.parameters.distillation.targetVolume;
                distillation["speed"] = profile.parameters.distillation.speed;
                distillation["endTemp"] = profile.parameters.distillation.endTemp;

                JsonObject mashing = parameters["mashing"].to<JsonObject>();
                JsonArray mashingSteps = mashing["steps"].to<JsonArray>();
                for (const auto &stepData : profile.parameters.mashing.steps) {
                  JsonObject step = mashingSteps.add<JsonObject>();
                  step["temperature"] = stepData.temperature;
                  step["duration"] = stepData.duration;
                  step["name"] = stepData.name;
                }

                JsonObject temperatures = parameters["temperatures"].to<JsonObject>();
                temperatures["maxCube"] = profile.parameters.temperatures.maxCube;
                temperatures["maxColumn"] =
                    profile.parameters.temperatures.maxColumn;
                temperatures["headsEnd"] = profile.parameters.temperatures.headsEnd;
                temperatures["bodyStart"] =
                    profile.parameters.temperatures.bodyStart;
                temperatures["bodyEnd"] = profile.parameters.temperatures.bodyEnd;

                ProfileBaroCorrectionSummary baroSummary;
                TemperatureParams effectiveTemps =
                    getEffectiveProfileTemperatures(profile, &baroSummary);

                JsonObject effectiveTemperatures =
                    doc["effectiveTemperatures"].to<JsonObject>();
                effectiveTemperatures["maxCube"] = effectiveTemps.maxCube;
                effectiveTemperatures["maxColumn"] = effectiveTemps.maxColumn;
                effectiveTemperatures["headsEnd"] = effectiveTemps.headsEnd;
                effectiveTemperatures["bodyStart"] = effectiveTemps.bodyStart;
                effectiveTemperatures["bodyEnd"] = effectiveTemps.bodyEnd;

                JsonObject baroCorrection = doc["baroCorrection"].to<JsonObject>();
                baroCorrection["enabled"] = baroSummary.enabled;
                baroCorrection["applicable"] = baroSummary.applicable;
                baroCorrection["applied"] = baroSummary.applied;
                baroCorrection["baselinePressureMmHg"] =
                    baroSummary.baselinePressureMmHg;
                baroCorrection["currentPressureMmHg"] =
                    baroSummary.currentPressureMmHg;
                baroCorrection["pressureDeltaMmHg"] =
                    baroSummary.pressureDeltaMmHg;
                baroCorrection["boilingShiftC"] = baroSummary.boilingShiftC;
                baroCorrection["appliedShiftC"] = baroSummary.appliedShiftC;
                baroCorrection["strength"] = baroSummary.strength;
                baroCorrection["maxShiftC"] = baroSummary.maxShiftC;
                baroCorrection["note"] = baroSummary.note;

                JsonObject safety = parameters["safety"].to<JsonObject>();
                safety["maxRuntime"] = profile.parameters.safety.maxRuntime;
                safety["waterFlowMin"] = profile.parameters.safety.waterFlowMin;
                safety["pressureMax"] = profile.parameters.safety.pressureMax;

                JsonObject statistics = doc["statistics"].to<JsonObject>();
                statistics["useCount"] = profile.statistics.useCount;
                statistics["lastUsed"] = profile.statistics.lastUsed;
                statistics["avgDuration"] = profile.statistics.avgDuration;
                statistics["avgYield"] = profile.statistics.avgYield;
                statistics["successRate"] = profile.statistics.successRate;

                JsonObject learning = doc["learning"].to<JsonObject>();
                learning["successfulRuns"] = profile.learning.successfulRuns;
                learning["failedRuns"] = profile.learning.failedRuns;
                learning["avgEnergyUsed"] = profile.learning.avgEnergyUsed;
                learning["avgEnergyPerLiter"] = profile.learning.avgEnergyPerLiter;
                learning["avgProcessHealth"] = profile.learning.avgProcessHealth;
                learning["avgStabilityIndex"] =
                    profile.learning.avgStabilityIndex;
                learning["typicalCubeFinalTemp"] =
                    profile.learning.typicalCubeFinalTemp;
                learning["typicalColumnTopFinalTemp"] =
                    profile.learning.typicalColumnTopFinalTemp;
                learning["lastProcessId"] = profile.learning.lastProcessId;
                learning["lastSuccessfulProcessId"] =
                    profile.learning.lastSuccessfulProcessId;

                if (!profile.learning.lastSuccessfulProcessId.isEmpty()) {
                  ProcessHistory lastSuccessfulHistory;
                  if (loadProcessHistory(profile.learning.lastSuccessfulProcessId,
                                         lastSuccessfulHistory)) {
                    JsonObject baseline =
                        learning["lastSuccessfulRun"].to<JsonObject>();
                    baseline["id"] = lastSuccessfulHistory.id;
                    baseline["startTime"] =
                        lastSuccessfulHistory.metadata.startTime;
                    baseline["duration"] =
                        lastSuccessfulHistory.metadata.duration;
                    baseline["totalCollected"] =
                        lastSuccessfulHistory.results.totalCollected;
                    baseline["energyUsed"] =
                        lastSuccessfulHistory.metrics.energyUsed;
                    baseline["avgProcessHealth"] =
                        lastSuccessfulHistory.metrics.avgProcessHealth;
                    baseline["avgStabilityIndex"] =
                        lastSuccessfulHistory.metrics.avgStabilityIndex;
                    baseline["cubeFinal"] =
                        lastSuccessfulHistory.metrics.cube.final;
                    baseline["columnTopFinal"] =
                        lastSuccessfulHistory.metrics.columnTop.final;

                    if (!lastSuccessfulHistory.advisorSnapshot.schemaVersion
                             .isEmpty() ||
                        !lastSuccessfulHistory.advisorSnapshot.items.empty()) {
                      JsonObject advisor =
                          learning["lastAdvisorSnapshot"].to<JsonObject>();
                      advisor["schemaVersion"] =
                          lastSuccessfulHistory.advisorSnapshot.schemaVersion;
                      advisor["createdAt"] =
                          lastSuccessfulHistory.advisorSnapshot.createdAt;
                      advisor["baselineProcessId"] =
                          lastSuccessfulHistory.advisorSnapshot
                              .baselineProcessId;
                      advisor["baselineProfile"] =
                          lastSuccessfulHistory.advisorSnapshot.baselineProfile;
                      JsonArray advisorItems = advisor["items"].to<JsonArray>();
                      for (const auto &item :
                           lastSuccessfulHistory.advisorSnapshot.items) {
                        JsonObject advisorItem =
                            advisorItems.add<JsonObject>();
                        advisorItem["kind"] = item.kind;
                        advisorItem["code"] = item.code;
                        advisorItem["tone"] = item.tone;
                        advisorItem["title"] = item.title;
                        advisorItem["detail"] = item.detail;
                        advisorItem["action"] = item.action;
                        advisorItem["parameterKey"] = item.parameterKey;
                        advisorItem["previousValue"] = item.previousValue;
                        advisorItem["suggestedValue"] = item.suggestedValue;
                      }
                    }
                  }
                }

                JsonObject validation = doc["validation"].to<JsonObject>();
                validation["validatedAt"] = profile.validation.validatedAt;
                validation["sourceProcessId"] =
                    profile.validation.sourceProcessId;
                validation["atmosphereHpa"] = profile.validation.atmosphereHpa;
                validation["atmosphereMmHg"] = profile.validation.atmosphereMmHg;
                validation["columnHeightMm"] =
                    profile.validation.columnHeightMm;
                validation["packingType"] = profile.validation.packingType;
                validation["packingCoeff"] = profile.validation.packingCoeff;
                validation["heaterPowerW"] = profile.validation.heaterPowerW;
                validation["targetPowerW"] = profile.validation.targetPowerW;
                validation["feedVolumeL"] = profile.validation.feedVolumeL;
                validation["feedAbvPercent"] =
                    profile.validation.feedAbvPercent;
                validation["cubeChargePercent"] =
                    profile.validation.cubeChargePercent;
                validation["headsActualMl"] = profile.validation.headsActualMl;
                validation["bodyActualMl"] = profile.validation.bodyActualMl;
                validation["tailsActualMl"] = profile.validation.tailsActualMl;
                validation["headsCutColumnTopC"] =
                    profile.validation.headsCutColumnTopC;
                validation["bodyCutColumnTopC"] =
                    profile.validation.bodyCutColumnTopC;
                validation["tailsCutColumnTopC"] =
                    profile.validation.tailsCutColumnTopC;
                validation["cubeFinalC"] = profile.validation.cubeFinalC;
                validation["columnTopFinalC"] =
                    profile.validation.columnTopFinalC;
                validation["avgStabilityIndex"] =
                    profile.validation.avgStabilityIndex;
                validation["avgProcessHealth"] =
                    profile.validation.avgProcessHealth;

                String response;
                serializeJson(doc, response);
                request->send(200, "application/json", response);
              } else {
                request->send(404, "application/json",
                              "{\"error\":\"Profile not found\"}");
              }
            });

  server.on(
      "^\\/api\\/profiles\\/([a-zA-Z0-9_]+)$", HTTP_PUT,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        String body;
        if (!collectRequestBody(request, data, len, index, total, body)) {
          return;
        }

        String id = request->pathArg(0);
        Profile profile;
        if (!loadProfile(id, profile)) {
          request->send(404, "application/json",
                        "{\"success\":false,\"error\":\"Profile not found\"}");
          return;
        }
        if (profile.metadata.isBuiltin) {
          request->send(
              403, "application/json",
              "{\"success\":false,\"error\":\"Builtin profile is read-only\"}");
          return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, body)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Invalid JSON\"}");
          return;
        }

        JsonObject metadata = doc["metadata"].is<JsonObject>()
                                  ? doc["metadata"].as<JsonObject>()
                                  : JsonObject();
        JsonObject parameters = doc["parameters"].is<JsonObject>()
                                    ? doc["parameters"].as<JsonObject>()
                                    : JsonObject();

        if (!metadata.isNull()) {
          if (!metadata["name"].isNull())
            profile.metadata.name = metadata["name"].as<String>();
          if (!metadata["description"].isNull())
            profile.metadata.description = metadata["description"].as<String>();
          if (!metadata["category"].isNull())
            profile.metadata.category = metadata["category"].as<String>();
          if (metadata["tags"].is<JsonArray>()) {
            profile.metadata.tags.clear();
            for (JsonVariant tag : metadata["tags"].as<JsonArray>()) {
              const String value = tag.as<String>();
              if (!value.isEmpty()) profile.metadata.tags.push_back(value);
            }
          }
        }

        if (!parameters.isNull()) {
          profile.parameters.mode = !parameters["mode"].isNull()
                                        ? parameters["mode"].as<String>()
                                        : profile.metadata.category;
          if (!parameters["model"].isNull())
            profile.parameters.model = parameters["model"].as<String>();

          JsonObject heater = parameters["heater"].is<JsonObject>()
                                  ? parameters["heater"].as<JsonObject>()
                                  : JsonObject();
          if (!heater.isNull()) {
            if (!heater["maxPower"].isNull())
              profile.parameters.heater.maxPower =
                  clampU16Range(heater["maxPower"].as<uint32_t>(), 300, 10000);
            if (!heater["autoMode"].isNull())
              profile.parameters.heater.autoMode = heater["autoMode"].as<bool>();
            if (!heater["pidKp"].isNull())
              profile.parameters.heater.pidKp =
                  clampFloatRange(heater["pidKp"].as<float>(), 0.0f, 100.0f);
            if (!heater["pidKi"].isNull())
              profile.parameters.heater.pidKi =
                  clampFloatRange(heater["pidKi"].as<float>(), 0.0f, 100.0f);
            if (!heater["pidKd"].isNull())
              profile.parameters.heater.pidKd =
                  clampFloatRange(heater["pidKd"].as<float>(), 0.0f, 100.0f);
            if (!heater["boosterEnabled"].isNull())
              profile.parameters.heater.boosterEnabled =
                  heater["boosterEnabled"].as<bool>();
            if (!heater["boosterStopCubeTempC"].isNull())
              profile.parameters.heater.boosterStopCubeTempC = clampFloatRange(
                  heater["boosterStopCubeTempC"].as<float>(), 20.0f, 100.0f);
          }

          JsonObject rectification = parameters["rectification"].is<JsonObject>()
                                         ? parameters["rectification"].as<JsonObject>()
                                         : JsonObject();
          if (!rectification.isNull()) {
            if (!rectification["stabilizationMin"].isNull())
              profile.parameters.rectification.stabilizationMin = clampU16Range(
                  rectification["stabilizationMin"].as<uint32_t>(), 1, 180);
            if (!rectification["headsVolume"].isNull())
              profile.parameters.rectification.headsVolume = clampU16Range(
                  rectification["headsVolume"].as<uint32_t>(), 1, 10000);
            if (!rectification["bodyVolume"].isNull())
              profile.parameters.rectification.bodyVolume = clampU16Range(
                  rectification["bodyVolume"].as<uint32_t>(), 1, 50000);
            if (!rectification["tailsVolume"].isNull())
              profile.parameters.rectification.tailsVolume = clampU16Range(
                  rectification["tailsVolume"].as<uint32_t>(), 0, 20000);
            if (!rectification["headsSpeed"].isNull())
              profile.parameters.rectification.headsSpeed = clampU16Range(
                  rectification["headsSpeed"].as<uint32_t>(), 10, 2000);
            if (!rectification["bodySpeed"].isNull())
              profile.parameters.rectification.bodySpeed = clampU16Range(
                  rectification["bodySpeed"].as<uint32_t>(), 50, 3000);
            if (!rectification["tailsSpeed"].isNull())
              profile.parameters.rectification.tailsSpeed = clampU16Range(
                  rectification["tailsSpeed"].as<uint32_t>(), 0, 3000);
            if (!rectification["purgeMin"].isNull())
              profile.parameters.rectification.purgeMin = clampU16Range(
                  rectification["purgeMin"].as<uint32_t>(), 1, 120);
          }

          JsonObject distillation = parameters["distillation"].is<JsonObject>()
                                        ? parameters["distillation"].as<JsonObject>()
                                        : JsonObject();
          if (!distillation.isNull()) {
            if (!distillation["headsVolume"].isNull())
              profile.parameters.distillation.headsVolume = clampU16Range(
                  distillation["headsVolume"].as<uint32_t>(), 0, 10000);
            if (!distillation["targetVolume"].isNull())
              profile.parameters.distillation.targetVolume = clampU16Range(
                  distillation["targetVolume"].as<uint32_t>(), 1, 50000);
            if (!distillation["speed"].isNull())
              profile.parameters.distillation.speed = clampU16Range(
                  distillation["speed"].as<uint32_t>(), 50, 65000);
            if (!distillation["endTemp"].isNull())
              profile.parameters.distillation.endTemp = clampFloatRange(
                  distillation["endTemp"].as<float>(), 50.0f, 110.0f);
          }

          JsonObject mashing = parameters["mashing"].is<JsonObject>()
                                   ? parameters["mashing"].as<JsonObject>()
                                   : JsonObject();
          if (!mashing.isNull() && mashing["steps"].is<JsonArray>()) {
            profile.parameters.mashing.steps.clear();
            uint8_t stepIndex = 0;
            for (JsonObject step : mashing["steps"].as<JsonArray>()) {
              if (stepIndex >= 10) break;
              const float temperature = clampFloatRange(
                  step["temperature"].as<float>(), 20.0f, 100.0f);
              const uint16_t duration =
                  clampU16Range(step["duration"].as<uint32_t>(), 1, 240);
              String name = step["name"].as<String>();
              name.trim();
              if (name.length() > 31) {
                name = name.substring(0, 31);
              }
              if (name.isEmpty()) {
                name = "Шаг " + String(stepIndex + 1);
              }

              MashingStepParams stepData;
              stepData.temperature = temperature;
              stepData.duration = duration;
              stepData.name = name;
              profile.parameters.mashing.steps.push_back(stepData);
              stepIndex++;
            }
          }

          JsonObject temperatures = parameters["temperatures"].is<JsonObject>()
                                        ? parameters["temperatures"].as<JsonObject>()
                                        : JsonObject();
          if (!temperatures.isNull()) {
            if (!temperatures["maxCube"].isNull())
              profile.parameters.temperatures.maxCube = clampFloatRange(
                  temperatures["maxCube"].as<float>(), 50.0f, 120.0f);
            if (!temperatures["maxColumn"].isNull())
              profile.parameters.temperatures.maxColumn = clampFloatRange(
                  temperatures["maxColumn"].as<float>(), 50.0f, 110.0f);
            if (!temperatures["headsEnd"].isNull())
              profile.parameters.temperatures.headsEnd = clampFloatRange(
                  temperatures["headsEnd"].as<float>(), 50.0f, 110.0f);
            if (!temperatures["bodyStart"].isNull())
              profile.parameters.temperatures.bodyStart = clampFloatRange(
                  temperatures["bodyStart"].as<float>(), 50.0f, 110.0f);
            if (!temperatures["bodyEnd"].isNull())
              profile.parameters.temperatures.bodyEnd = clampFloatRange(
                  temperatures["bodyEnd"].as<float>(), 50.0f, 120.0f);
          }

          JsonObject safety = parameters["safety"].is<JsonObject>()
                                  ? parameters["safety"].as<JsonObject>()
                                  : JsonObject();
          if (!safety.isNull()) {
            if (!safety["maxRuntime"].isNull())
              profile.parameters.safety.maxRuntime =
                  clampU16Range(safety["maxRuntime"].as<uint32_t>(), 10, 5000);
            if (!safety["waterFlowMin"].isNull())
              profile.parameters.safety.waterFlowMin = clampFloatRange(
                  safety["waterFlowMin"].as<float>(), 0.0f, 20.0f);
            if (!safety["pressureMax"].isNull())
              profile.parameters.safety.pressureMax =
                  clampU16Range(safety["pressureMax"].as<uint32_t>(), 5, 200);
          }
        }

        profile.metadata.updated = millis() / 1000;
        if (profile.parameters.mode.isEmpty())
          profile.parameters.mode = profile.metadata.category;
        if (profile.parameters.model.isEmpty())
          profile.parameters.model = "classic";

        if (!saveProfile(profile)) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"error\":\"Failed to validate or save profile\"}");
          return;
        }

        request->send(200, "application/json",
                      "{\"success\":true,\"id\":\"" + profile.id + "\"}");
      });

  server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)\\/load$", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              String id = request->pathArg(0);

              if (applyProfile(id)) {
                Logger::logf(0, "Profile loaded: %s", id.c_str());
                request->send(200, "application/json",
                              "{\"success\":true,\"message\":\"Profile loaded\"}");
              } else {
                Logger::logf(1, "Profile load failed: %s not found", id.c_str());
                request->send(404, "application/json",
                              "{\"error\":\"Profile not found\"}");
              }
            });

  server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)\\/export$", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              String id = request->pathArg(0);
              String json = exportProfileToJSON(id);
              if (json.isEmpty()) {
                request->send(
                    404, "application/json",
                    "{\"success\":false,\"error\":\"Profile not found\"}");
                return;
              }
              request->send(200, "application/json", json);
            });

  server.on("^\\/api\\/profiles\\/([a-zA-Z0-9_]+)$", HTTP_DELETE,
            [](AsyncWebServerRequest *request) {
              String id = request->pathArg(0);

              if (deleteProfile(id)) {
                request->send(
                    200, "application/json",
                    "{\"success\":true,\"message\":\"Profile deleted\"}");
              } else {
                request->send(
                    404, "application/json",
                    "{\"error\":\"Profile not found or builtin\"}");
              }
            });

  server.on("/api/profiles", HTTP_DELETE,
            [](AsyncWebServerRequest *request) {
              bool cleared = clearProfiles();
              request->send(cleared ? 200 : 500, "application/json",
                            String("{\"success\":") +
                                (cleared ? "true" : "false") + "}");
            });

  server.on(
      "/api/profiles/import", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        String body;
        if (!collectRequestBody(request, data, len, index, total, body)) {
          return;
        }

        uint16_t count = importProfilesFromJSON(body);
        request->send(200, "application/json",
                      "{\"success\":true,\"count\":" + String(count) +
                          ",\"imported\":" + String(count) + "}");
      });

  server.on(
      "/api/profiles", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        String body;
        if (!collectRequestBody(request, data, len, index, total, body)) {
          return;
        }

        String newId = importProfileFromJSON(body);
        if (!newId.isEmpty()) {
          request->send(201, "application/json",
                        "{\"success\":true,\"id\":\"" + newId + "\"}");
        } else {
          request->send(400, "application/json",
                        "{\"error\":\"Failed to create profile\"}");
        }
      });

  server.on("/api/profiles", HTTP_GET, [](AsyncWebServerRequest *request) {
    std::vector<ProfileListItem> profiles = getProfileList();

    JsonDocument doc;
    doc["total"] = profiles.size();

    JsonArray profileArray = doc["profiles"].to<JsonArray>();
    for (const auto &prof : profiles) {
      JsonObject p = profileArray.add<JsonObject>();
      p["id"] = prof.id;
      p["name"] = prof.name;
      p["description"] = prof.description;
      p["category"] = prof.category;
      JsonArray tags = p["tags"].to<JsonArray>();
      for (const auto &tag : prof.tags) {
        tags.add(tag);
      }
      p["author"] = prof.author;
      p["useCount"] = prof.useCount;
      p["lastUsed"] = prof.lastUsed;
      p["updated"] = prof.updated;
      p["successRate"] = prof.successRate;
      p["successfulRuns"] = prof.successfulRuns;
      p["isBuiltin"] = prof.isBuiltin;
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
}
