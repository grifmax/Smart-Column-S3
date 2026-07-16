#include "api_routes.h"

#include "../../history.h"
#include "../../profiles.h"
#include "interface/webserver_shared.h"
#include "storage/logger.h"

void registerProfilesApiRoutes(AsyncWebServer &server) {
  server.on("/api/profiles/export", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              bool includeBuiltin = request->hasParam("includeBuiltin");
              String json = exportAllProfilesToJSON(includeBuiltin);
              request->send(200, "application/json", json);
            });

  server.on(
      "^\\/api\\/profiles\\/([a-zA-Z0-9_]+)$", HTTP_GET,
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
          rectification["purgeMin"] = profile.parameters.rectification.purgeMin;
          rectification["baroCorrectionEnabled"] =
              profile.parameters.rectification.baroCorrectionEnabled;
          rectification["takeoffBackendType"] = static_cast<uint8_t>(
              profile.parameters.rectification.takeoffBackendType);
          rectification["refluxMode"] =
              static_cast<uint8_t>(profile.parameters.rectification.refluxMode);
          rectification["srTarget"] = profile.parameters.rectification.srTarget;
          rectification["srRatio"] = profile.parameters.rectification.srTarget;
          rectification["autonomousCycleSec"] =
              profile.parameters.rectification.autonomousCycleSec;
          rectification["autonomousPauseSec"] =
              profile.parameters.rectification.autonomousPauseSec;
          rectification["chimAutoPercent"] =
              profile.parameters.rectification.chimAutoPercent;
          rectification["chimTimePerH"] =
              profile.parameters.rectification.chimTimePerH;
          rectification["chimBegPercent"] =
              profile.parameters.rectification.chimBegPercent;
          rectification["chimMinPercent"] =
              profile.parameters.rectification.chimMinPercent;
          rectification["usePbMode"] =
              profile.parameters.rectification.usePbMode;
          rectification["timpPbMs"] = profile.parameters.rectification.timpPbMs;
          rectification["routingSettlingMs"] =
              profile.parameters.rectification.routingSettlingMs;
          rectification["routingRetargetMinMs"] =
              profile.parameters.rectification.routingRetargetMinMs;
          rectification["valvePulsePeriodMs"] =
              profile.parameters.rectification.valvePulsePeriodMs;
          rectification["valvePulseMinOpenMs"] =
              profile.parameters.rectification.valvePulseMinOpenMs;
          rectification["valvePulseMaxOpenMs"] =
              profile.parameters.rectification.valvePulseMaxOpenMs;
          JsonArray phasePowerPercent =
              rectification["phasePowerPercent"].to<JsonArray>();
          for (uint8_t index = 0; index < RECT_POWER_COUNT; ++index) {
            phasePowerPercent.add(
                profile.parameters.rectification.phasePowerPercent[index]);
          }
          rectification["phasePowerStabilization"] =
              profile.parameters.rectification
                  .phasePowerPercent[RECT_POWER_STABILIZATION];
          rectification["phasePowerHeads"] =
              profile.parameters.rectification.phasePowerPercent[RECT_POWER_HEADS];
          rectification["phasePowerBody"] =
              profile.parameters.rectification.phasePowerPercent[RECT_POWER_BODY];
          rectification["phasePowerTails"] =
              profile.parameters.rectification.phasePowerPercent[RECT_POWER_TAILS];

          JsonObject distillation =
              parameters["distillation"].to<JsonObject>();
          distillation["headsVolume"] =
              profile.parameters.distillation.headsVolume;
          distillation["targetVolume"] =
              profile.parameters.distillation.targetVolume;
          distillation["tailsVolume"] =
              profile.parameters.distillation.tailsVolume;
          distillation["speed"] = profile.parameters.distillation.speed;
          distillation["endTemp"] = profile.parameters.distillation.endTemp;
          distillation["takeoffBackendType"] = static_cast<uint8_t>(
              profile.parameters.distillation.takeoffBackendType);
          distillation["valveSafeVentConfirmed"] =
              profile.parameters.distillation.valveSafeVentConfirmed;
          JsonObject fractionProgram =
              distillation["fractionProgram"].to<JsonObject>();
          fractionProgram["schemaVersion"] =
              profile.parameters.distillation.fractionProgram.schemaVersion;
          fractionProgram["enabled"] =
              profile.parameters.distillation.fractionProgram.enabled;
          fractionProgram["stepCount"] =
              profile.parameters.distillation.fractionProgram.stepCount;
          fractionProgram["heatingTemperatureSensorIndex"] =
              profile.parameters.distillation.fractionProgram
                  .heatingTemperatureSensorIndex;
          fractionProgram["heatingTargetTemperatureC"] =
              profile.parameters.distillation.fractionProgram
                  .heatingTargetTemperatureC;
          JsonArray fractionSteps = fractionProgram["steps"].to<JsonArray>();
          for (uint8_t index = 0;
               index < profile.parameters.distillation.fractionProgram.stepCount &&
               index < FRACTION_PROGRAM_MAX_STEPS;
               ++index) {
            const FractionProgramStep &step =
                profile.parameters.distillation.fractionProgram.steps[index];
            JsonObject item = fractionSteps.add<JsonObject>();
            item["name"] = step.name;
            item["routeIndex"] = step.routeIndex;
            item["pumpRateMlH"] = step.pumpRateMlH;
            item["heaterPowerW"] = step.heaterPowerW;
            item["requireOperatorConfirmation"] =
                step.requireOperatorConfirmation;
            item["confirmationPrompt"] = step.confirmationPrompt;
            item["endConditions"] = step.endConditions;
            item["endVolumeMl"] = step.endVolumeMl;
            item["endDurationSec"] = step.endDurationSec;
            item["temperatureSensorIndex"] = step.temperatureSensorIndex;
            item["endTemperatureC"] = step.endTemperatureC;
            item["allowManualAdvance"] = step.allowManualAdvance;
          }

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
          temperatures["maxColumn"] = profile.parameters.temperatures.maxColumn;
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
          baroCorrection["pressureDeltaMmHg"] = baroSummary.pressureDeltaMmHg;
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
          learning["avgStabilityIndex"] = profile.learning.avgStabilityIndex;
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
              baseline["startTime"] = lastSuccessfulHistory.metadata.startTime;
              baseline["duration"] = lastSuccessfulHistory.metadata.duration;
              baseline["totalCollected"] =
                  lastSuccessfulHistory.results.totalCollected;
              baseline["energyUsed"] = lastSuccessfulHistory.metrics.energyUsed;
              baseline["avgProcessHealth"] =
                  lastSuccessfulHistory.metrics.avgProcessHealth;
              baseline["avgStabilityIndex"] =
                  lastSuccessfulHistory.metrics.avgStabilityIndex;
              baseline["cubeFinal"] = lastSuccessfulHistory.metrics.cube.final;
              baseline["columnTopFinal"] =
                  lastSuccessfulHistory.metrics.columnTop.final;

              if (!lastSuccessfulHistory.advisorSnapshot.schemaVersion.isEmpty() ||
                  !lastSuccessfulHistory.advisorSnapshot.items.empty()) {
                JsonObject advisor =
                    learning["lastAdvisorSnapshot"].to<JsonObject>();
                advisor["schemaVersion"] =
                    lastSuccessfulHistory.advisorSnapshot.schemaVersion;
                advisor["createdAt"] =
                    lastSuccessfulHistory.advisorSnapshot.createdAt;
                advisor["baselineProcessId"] =
                    lastSuccessfulHistory.advisorSnapshot.baselineProcessId;
                advisor["baselineProfile"] =
                    lastSuccessfulHistory.advisorSnapshot.baselineProfile;
                JsonArray advisorItems = advisor["items"].to<JsonArray>();
                for (const auto &item :
                     lastSuccessfulHistory.advisorSnapshot.items) {
                  JsonObject advisorItem = advisorItems.add<JsonObject>();
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

          appendProfileValidationJson(doc["validation"].to<JsonObject>(),
                                      profile);

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
            if (!rectification["baroCorrectionEnabled"].isNull())
              profile.parameters.rectification.baroCorrectionEnabled =
                  rectification["baroCorrectionEnabled"].as<bool>();
            if (!rectification["takeoffBackendType"].isNull())
              profile.parameters.rectification.takeoffBackendType =
                  static_cast<RectTakeoffBackendType>(clampU16Range(
                      rectification["takeoffBackendType"].as<uint32_t>(), 0,
                      static_cast<uint32_t>(
                          RectTakeoffBackendType::VALVE_SINGLE_SWITCHED)));
            if (!rectification["refluxMode"].isNull())
              profile.parameters.rectification.refluxMode =
                  static_cast<RectRefluxMode>(clampU16Range(
                      rectification["refluxMode"].as<uint32_t>(), 0,
                      static_cast<uint32_t>(RectRefluxMode::AUTONOMOUS)));
            if (!rectification["srTarget"].isNull())
              profile.parameters.rectification.srTarget = clampFloatRange(
                  rectification["srTarget"].as<float>(), 0.0f, 20.0f);
            else if (!rectification["srRatio"].isNull())
              profile.parameters.rectification.srTarget = clampFloatRange(
                  rectification["srRatio"].as<float>(), 0.0f, 20.0f);
            if (!rectification["autonomousCycleSec"].isNull())
              profile.parameters.rectification.autonomousCycleSec =
                  clampU16Range(
                      rectification["autonomousCycleSec"].as<uint32_t>(), 1,
                      7200);
            if (!rectification["autonomousPauseSec"].isNull())
              profile.parameters.rectification.autonomousPauseSec =
                  clampU16Range(
                      rectification["autonomousPauseSec"].as<uint32_t>(), 0,
                      7199);
            if (!rectification["chimAutoPercent"].isNull())
              profile.parameters.rectification.chimAutoPercent =
                  clampFloatRange(rectification["chimAutoPercent"].as<float>(),
                                  0.0f, 200.0f);
            if (!rectification["chimTimePerH"].isNull())
              profile.parameters.rectification.chimTimePerH =
                  clampFloatRange(rectification["chimTimePerH"].as<float>(),
                                  -2000.0f, 2000.0f);
            if (!rectification["chimBegPercent"].isNull())
              profile.parameters.rectification.chimBegPercent =
                  clampFloatRange(rectification["chimBegPercent"].as<float>(),
                                  -100.0f, 200.0f);
            if (!rectification["chimMinPercent"].isNull())
              profile.parameters.rectification.chimMinPercent =
                  clampFloatRange(rectification["chimMinPercent"].as<float>(),
                                  0.0f, 100.0f);
            if (!rectification["usePbMode"].isNull())
              profile.parameters.rectification.usePbMode = clampU8Range(
                  rectification["usePbMode"].as<uint32_t>(), 0, 3);
            if (!rectification["timpPbMs"].isNull()) {
              const uint32_t timpPbMs = rectification["timpPbMs"].as<uint32_t>();
              profile.parameters.rectification.timpPbMs =
                  timpPbMs > 600000UL ? 600000UL : timpPbMs;
            }
            if (!rectification["routingSettlingMs"].isNull())
              profile.parameters.rectification.routingSettlingMs =
                  clampU16Range(
                      rectification["routingSettlingMs"].as<uint32_t>(), 0,
                      10000);
            if (!rectification["routingRetargetMinMs"].isNull())
              profile.parameters.rectification.routingRetargetMinMs =
                  clampU16Range(
                      rectification["routingRetargetMinMs"].as<uint32_t>(), 0,
                      30000);
            if (!rectification["valvePulsePeriodMs"].isNull())
              profile.parameters.rectification.valvePulsePeriodMs =
                  clampU16Range(
                      rectification["valvePulsePeriodMs"].as<uint32_t>(), 100,
                      5000);
            if (!rectification["valvePulseMinOpenMs"].isNull())
              profile.parameters.rectification.valvePulseMinOpenMs =
                  clampU16Range(
                      rectification["valvePulseMinOpenMs"].as<uint32_t>(), 0,
                      profile.parameters.rectification.valvePulsePeriodMs);
            if (!rectification["valvePulseMaxOpenMs"].isNull())
              profile.parameters.rectification.valvePulseMaxOpenMs =
                  clampU16Range(
                      rectification["valvePulseMaxOpenMs"].as<uint32_t>(),
                      profile.parameters.rectification.valvePulseMinOpenMs,
                      profile.parameters.rectification.valvePulsePeriodMs);
            if (rectification["phasePowerPercent"].is<JsonArray>()) {
              JsonArray phasePower = rectification["phasePowerPercent"].as<JsonArray>();
              for (uint8_t index = 0;
                   index < RECT_POWER_COUNT && index < phasePower.size();
                   ++index) {
                profile.parameters.rectification.phasePowerPercent[index] =
                    clampU8Range(phasePower[index].as<uint32_t>(), 1, 100);
              }
            }
            if (!rectification["phasePowerStabilization"].isNull())
              profile.parameters.rectification
                  .phasePowerPercent[RECT_POWER_STABILIZATION] = clampU8Range(
                  rectification["phasePowerStabilization"].as<uint32_t>(), 1,
                  100);
            if (!rectification["phasePowerHeads"].isNull())
              profile.parameters.rectification.phasePowerPercent[RECT_POWER_HEADS] =
                  clampU8Range(rectification["phasePowerHeads"].as<uint32_t>(), 1,
                               100);
            if (!rectification["phasePowerBody"].isNull())
              profile.parameters.rectification.phasePowerPercent[RECT_POWER_BODY] =
                  clampU8Range(rectification["phasePowerBody"].as<uint32_t>(), 1,
                               100);
            if (!rectification["phasePowerTails"].isNull())
              profile.parameters.rectification.phasePowerPercent[RECT_POWER_TAILS] =
                  clampU8Range(rectification["phasePowerTails"].as<uint32_t>(), 1,
                               100);
            if (profile.parameters.rectification.autonomousPauseSec >=
                profile.parameters.rectification.autonomousCycleSec) {
              profile.parameters.rectification.autonomousPauseSec =
                  profile.parameters.rectification.autonomousCycleSec - 1;
            }
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
            if (!distillation["tailsVolume"].isNull())
              profile.parameters.distillation.tailsVolume = clampU16Range(
                  distillation["tailsVolume"].as<uint32_t>(), 0, 50000);
            if (!distillation["speed"].isNull())
              profile.parameters.distillation.speed = clampU16Range(
                  distillation["speed"].as<uint32_t>(), 50, 65000);
            if (!distillation["endTemp"].isNull())
              profile.parameters.distillation.endTemp = clampFloatRange(
                  distillation["endTemp"].as<float>(), 50.0f, 110.0f);
            if (!distillation["takeoffBackendType"].isNull())
              profile.parameters.distillation.takeoffBackendType =
                  static_cast<RectTakeoffBackendType>(clampU8Range(
                      distillation["takeoffBackendType"].as<uint32_t>(), 0,
                      static_cast<uint8_t>(
                          RectTakeoffBackendType::VALVE_SINGLE_SWITCHED)));
            if (!distillation["valveSafeVentConfirmed"].isNull())
              profile.parameters.distillation.valveSafeVentConfirmed =
                  distillation["valveSafeVentConfirmed"].as<bool>();
            if (distillation["fractionProgram"].is<JsonObject>()) {
              JsonObject programJson =
                  distillation["fractionProgram"].as<JsonObject>();
              FractionProgram program =
                  profile.parameters.distillation.fractionProgram;
              program.stepCount = 0;
              program.schemaVersion = clampU16Range(
                  programJson["schemaVersion"] | program.schemaVersion, 0,
                  65535);
              program.enabled = programJson["enabled"] | program.enabled;
              program.heatingTemperatureSensorIndex = clampU8Range(
                  programJson["heatingTemperatureSensorIndex"] |
                      program.heatingTemperatureSensorIndex,
                  0, TEMP_COUNT - 1);
              program.heatingTargetTemperatureC = clampFloatRange(
                  programJson["heatingTargetTemperatureC"] |
                      program.heatingTargetTemperatureC,
                  0.0f, 110.0f);
              if (programJson["steps"].is<JsonArray>()) {
                for (JsonObject item : programJson["steps"].as<JsonArray>()) {
                  if (program.stepCount >= FRACTION_PROGRAM_MAX_STEPS)
                    break;
                  FractionProgramStep &step =
                      program.steps[program.stepCount++];
                  strlcpy(step.name, item["name"] | "", sizeof(step.name));
                  step.routeIndex =
                      clampU8Range(item["routeIndex"] | 0, 0, 4);
                  step.pumpRateMlH = clampFloatRange(
                      item["pumpRateMlH"] | 0.0f, 0.0f, 65000.0f);
                  step.heaterPowerW = clampFloatRange(
                      item["heaterPowerW"] | 0.0f, 0.0f, 10000.0f);
                  step.requireOperatorConfirmation =
                      item["requireOperatorConfirmation"] | false;
                  strlcpy(step.confirmationPrompt,
                          item["confirmationPrompt"] | "",
                          sizeof(step.confirmationPrompt));
                  step.endConditions =
                      static_cast<uint8_t>(clampU8Range(
                          item["endConditions"] | 0, 0, 255));
                  step.endVolumeMl = clampFloatRange(
                      item["endVolumeMl"] | 0.0f, 0.0f, 50000.0f);
                  step.endDurationSec =
                      static_cast<uint32_t>(min<uint64_t>(
                          item["endDurationSec"] | 0ULL, 864000ULL));
                  step.temperatureSensorIndex = clampU8Range(
                      item["temperatureSensorIndex"] | 0, 0, TEMP_COUNT - 1);
                  step.endTemperatureC = clampFloatRange(
                      item["endTemperatureC"] | 0.0f, 0.0f, 110.0f);
                  step.allowManualAdvance =
                      item["allowManualAdvance"] | false;
                }
              }
              profile.parameters.distillation.fractionProgram = program;
            }
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

  server.on(
      "^\\/api\\/profiles\\/([a-zA-Z0-9_]+)\\/load$", HTTP_POST,
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

  server.on(
      "^\\/api\\/profiles\\/([a-zA-Z0-9_]+)\\/export$", HTTP_GET,
      [](AsyncWebServerRequest *request) {
        String id = request->pathArg(0);
        String json = exportProfileToJSON(id);
        if (json.isEmpty()) {
          request->send(404, "application/json",
                        "{\"success\":false,\"error\":\"Profile not found\"}");
          return;
        }
        request->send(200, "application/json", json);
      });

  server.on(
      "^\\/api\\/profiles\\/([a-zA-Z0-9_]+)$", HTTP_DELETE,
      [](AsyncWebServerRequest *request) {
        String id = request->pathArg(0);

        if (deleteProfile(id)) {
          request->send(200, "application/json",
                        "{\"success\":true,\"message\":\"Profile deleted\"}");
        } else {
          request->send(404, "application/json",
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
