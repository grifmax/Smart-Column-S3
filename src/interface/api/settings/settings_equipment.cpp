#include "settings_modules.h"

#include <string.h>

#include "config.h"
#include "control/cooling_pwm_logic.h"
#include "control/control_command_queue.h"
#include "control/power_command_gateway.h"
#include "control/state_snapshot.h"
#include "drivers/sensors.h"
#include "drivers/stirrer.h"
#include "drivers/valves.h"
#include "interface/editable_field_descriptors.h"
#include "interface/webserver_shared.h"
#include "storage/nvs_manager.h"

namespace {

const char *getPackingTypeKey(PackingType type) {
  switch (type) {
  case PackingType::SPN_3_5:
    return "spn_3_5";
  case PackingType::SPN_4_0:
    return "spn_4_0";
  case PackingType::RASCHIG:
    return "raschig";
  case PackingType::CUSTOM:
    return "custom";
  default:
    return "unknown";
  }
}

PackingType parsePackingTypeKey(const char *value, PackingType fallback) {
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  if (strcmp(value, "spn_3_5") == 0) {
    return PackingType::SPN_3_5;
  }
  if (strcmp(value, "spn_4_0") == 0) {
    return PackingType::SPN_4_0;
  }
  if (strcmp(value, "raschig") == 0) {
    return PackingType::RASCHIG;
  }
  if (strcmp(value, "custom") == 0) {
    return PackingType::CUSTOM;
  }
  return fallback;
}

void fillStirrerSettingsJson(JsonObject settings, const Settings &source) {
  settings["enabled"] = source.stirrer.enabled;
  settings["defaultSpeedPercent"] = source.stirrer.defaultSpeedPercent;
  settings["autoMashing"] = source.stirrer.autoMashing;
  settings["autoFermentation"] = source.stirrer.autoFermentation;
  settings["autoNbk"] = source.stirrer.autoNbk;
}

} // namespace

void registerEquipmentSettingsApiRoutes(AsyncWebServer &server) {
  server.on("/api/settings/equipment", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              SystemState stateSnapshot;
              if (!StateSnapshot::read(stateSnapshot)) {
                request->send(503, "application/json",
                              "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
                return;
              }
              JsonDocument doc;
              doc["heaterPowerW"] = g_settings.equipment.heaterPowerW;
              doc["columnHeightMm"] = g_settings.equipment.columnHeightMm;
              doc["cubeVolumeL"] = g_settings.equipment.cubeVolumeL;
              doc["minHeaterSubmergeL"] =
                  g_settings.equipment.minHeaterSubmergeL;
              doc["waterAutoStartCubeTempC"] =
                  g_settings.equipment.waterAutoStartCubeTempC;
              doc["boosterHeaterEnabled"] =
                  g_settings.equipment.boosterHeaterEnabled;
              doc["boosterHeaterPowerW"] =
                  g_settings.equipment.boosterHeaterPowerW;
              doc["boosterHeaterStopCubeTempC"] =
                  g_settings.equipment.boosterHeaterStopCubeTempC;
              doc["coolingPwmEnabled"] =
                  g_settings.equipment.coolingPwmEnabled;
              doc["coolingPwmHoldPercent"] =
                  g_settings.equipment.coolingPwmHoldPercent;
              doc["coolingPwmPullMs"] =
                  g_settings.equipment.coolingPwmPullMs;
              doc["coolingPwmCurrentDuty"] = Valves::getStartStop();
              doc["useDs2482ForTemps"] =
                  g_settings.equipment.useDs2482ForTemps;
              doc["ds2482Address"] = g_settings.equipment.ds2482Address;
              doc["tempBusGpioPin"] = PIN_ONEWIRE;
              doc["temperatureBusSource"] =
                  Sensors::getTemperatureBusSourceKey();
              doc["temperatureBusSourceLabel"] =
                  Sensors::getTemperatureBusSourceLabel();
              doc["bodyLevelSensorEnabled"] =
                  g_settings.equipment.bodyLevelSensorEnabled;
              doc["bodyLevelThresholdV"] =
                  g_settings.equipment.bodyLevelThresholdV;
              doc["bodyLevelTriggerAbove"] =
                  g_settings.equipment.bodyLevelTriggerAbove;
              doc["leakSensorEnabled"] =
                  g_settings.equipment.leakSensorEnabled;
              doc["leakThresholdV"] = g_settings.equipment.leakThresholdV;
              doc["leakTriggerAbove"] =
                  g_settings.equipment.leakTriggerAbove;
              doc["valveNormallyOpenMask"] = g_settings.valves.normallyOpenMask;
              JsonObject valveTypes = doc["valveTypes"].to<JsonObject>();
              static const char *const kValveKeys[6] = {"water", "heads", "body",
                                                        "tails", "uno", "startStop"};
              for (uint8_t bit = 0; bit < 6; ++bit) {
                valveTypes[kValveKeys[bit]] =
                    (g_settings.valves.normallyOpenMask & (1U << bit)) != 0 ? "no"
                                                                            : "nc";
              }
              doc["packingType"] =
                  getPackingTypeKey(g_settings.equipment.packingType);
              doc["packingCoeff"] = g_settings.equipment.packingCoeff;

              JsonObject temperatureTopology =
                  doc["temperatureTopology"].to<JsonObject>();
              fillTemperatureTopologyJson(temperatureTopology,
                                          g_settings.equipment);
              JsonObject supportedModes =
                  doc["supportedModes"].to<JsonObject>();
              fillTemperatureModeSupportJson(supportedModes, g_settings);

              JsonObject boardProfile = doc["boardProfile"].to<JsonObject>();
              boardProfile["rev"] = BOARD_REV_LABEL;
              boardProfile["name"] = BOARD_PROFILE_NAME;
              boardProfile["code"] = BOARD_REV;
              boardProfile["hasTft"] = BOARD_HAS_TFT;
              boardProfile["hasTouch"] = BOARD_HAS_TOUCH;
              boardProfile["hasTriac"] = BOARD_HAS_TRIAC;
              boardProfile["hasZeroCross"] = BOARD_HAS_ZERO_CROSS;
              boardProfile["hasFractionatorServo"] =
                  BOARD_HAS_FRACTIONATOR_SERVO;
              boardProfile["hasStartStopPwm"] = BOARD_HAS_STARTSTOP_PWM;

              JsonObject pzem = doc["pzem"].to<JsonObject>();
              pzem["available"] = stateSnapshot.health.pzemOk;
              pzem["uartNum"] = PZEM_UART_NUM;
              pzem["baudRate"] = PZEM_BAUD_RATE;
              pzem["rxPin"] = PIN_PZEM_RX;
              pzem["txPin"] = PIN_PZEM_TX;
              pzem["voltage"] = stateSnapshot.power.voltage;
              pzem["current"] = stateSnapshot.power.current;
              pzem["power"] = stateSnapshot.power.power;
              pzem["energy"] = stateSnapshot.power.energy;
              pzem["frequency"] = stateSnapshot.power.frequency;
              pzem["powerFactor"] = stateSnapshot.power.powerFactor;
              pzem["lastUpdate"] = stateSnapshot.power.lastUpdate;

              JsonObject bootGpio = doc["bootGpio"].to<JsonObject>();
              bootGpio["completed"] = g_bootGpioSelfTest.completed;
              bootGpio["overallOk"] = g_bootGpioSelfTest.overallOk;
              bootGpio["readbackType"] = "gpio_latch";
              bootGpio["physicalFeedback"] = false;
              bootGpio["checkedCount"] = g_bootGpioSelfTest.checkedCount;
              bootGpio["timestampMs"] = g_bootGpioSelfTest.timestampMs;
              bootGpio["boardRev"] = g_bootGpioSelfTest.boardRev;

              JsonArray bootItems = bootGpio["items"].to<JsonArray>();
              for (uint8_t i = 0;
                   i < g_bootGpioSelfTest.checkedCount &&
                   i < BOOT_GPIO_CHECK_MAX; ++i) {
                const BootGpioCheckItem &src = g_bootGpioSelfTest.items[i];
                JsonObject item = bootItems.add<JsonObject>();
                item["label"] = src.label;
                item["pin"] = src.pin;
                item["expectedLevel"] = src.expectedLevel;
                item["actualLevel"] = src.actualLevel;
                item["ok"] = src.ok;
                switch (src.mode) {
                case 0:
                  item["mode"] = "output_low";
                  break;
                case 1:
                  item["mode"] = "output_high";
                  break;
                case 2:
                  item["mode"] = "input";
                  break;
                case 3:
                  item["mode"] = "input_pullup";
                  break;
                default:
                  item["mode"] = "unknown";
                  break;
                }
              }

              JsonObject modules = doc["modules"].to<JsonObject>();
              fillEquipmentModulesJson(modules);

              JsonObject safetyChannels =
                  doc["safetyChannels"].to<JsonObject>();
              fillSafetyChannelsJson(safetyChannels, stateSnapshot);

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "/api/settings/equipment", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        if (!doc["heaterPowerW"].isNull()) {
          g_settings.equipment.heaterPowerW =
              EditableFields::normalizeU16(
                  EditableFields::Id::EQUIPMENT_HEATER_POWER_W,
                  doc["heaterPowerW"].as<uint32_t>());
        }
        if (!doc["columnHeightMm"].isNull()) {
          g_settings.equipment.columnHeightMm =
              EditableFields::normalizeU16(
                  EditableFields::Id::EQUIPMENT_COLUMN_HEIGHT_MM,
                  doc["columnHeightMm"].as<uint32_t>());
        }
        if (!doc["packingType"].isNull()) {
          g_settings.equipment.packingType = parsePackingTypeKey(
              doc["packingType"].as<const char *>(),
              g_settings.equipment.packingType);
        }
        if (!doc["packingCoeff"].isNull()) {
          g_settings.equipment.packingCoeff =
              EditableFields::normalize(
                  EditableFields::Id::EQUIPMENT_PACKING_COEFFICIENT,
                  doc["packingCoeff"].as<float>());
        }
        if (!doc["cubeVolumeL"].isNull()) {
          g_settings.equipment.cubeVolumeL =
              EditableFields::normalize(
                  EditableFields::Id::EQUIPMENT_CUBE_VOLUME_L,
                  doc["cubeVolumeL"].as<float>());
        }
        if (!doc["minHeaterSubmergeL"].isNull()) {
          g_settings.equipment.minHeaterSubmergeL =
              EditableFields::normalize(
                  EditableFields::Id::EQUIPMENT_MIN_HEATER_SUBMERGE_L,
                  doc["minHeaterSubmergeL"].as<float>());
        }
        if (!doc["waterAutoStartCubeTempC"].isNull()) {
          g_settings.equipment.waterAutoStartCubeTempC = clampFloatRange(
              doc["waterAutoStartCubeTempC"].as<float>(), 20.0f, 60.0f);
        }
        if (!doc["boosterHeaterEnabled"].isNull()) {
          g_settings.equipment.boosterHeaterEnabled =
              doc["boosterHeaterEnabled"].as<bool>();
        }
        if (!doc["boosterHeaterPowerW"].isNull()) {
          g_settings.equipment.boosterHeaterPowerW =
              EditableFields::normalizeU16(
                  EditableFields::Id::EQUIPMENT_BOOSTER_HEATER_POWER_W,
                  doc["boosterHeaterPowerW"].as<uint32_t>());
        }
        if (!doc["boosterHeaterStopCubeTempC"].isNull()) {
          g_settings.equipment.boosterHeaterStopCubeTempC = clampFloatRange(
              doc["boosterHeaterStopCubeTempC"].as<float>(), 20.0f, 100.0f);
        }
        if (!doc["coolingPwmEnabled"].isNull()) {
          g_settings.equipment.coolingPwmEnabled =
              doc["coolingPwmEnabled"].as<bool>();
        }
        if (!doc["coolingPwmHoldPercent"].isNull()) {
          g_settings.equipment.coolingPwmHoldPercent = static_cast<uint8_t>(
              EditableFields::normalize(
                  EditableFields::Id::COOLING_PWM_HOLD_PERCENT,
                  doc["coolingPwmHoldPercent"].as<float>()));
        } else if (!doc["coolingPwmStartupDuty"].isNull()) {
          g_settings.equipment.coolingPwmHoldPercent =
              CoolingPwmLogic::clampHoldPercent(CoolingPwmLogic::dutyToPercent(
                  doc["coolingPwmStartupDuty"].as<uint32_t>()));
        }
        if (!doc["coolingPwmPullMs"].isNull()) {
          g_settings.equipment.coolingPwmPullMs =
              EditableFields::normalizeU16(
                  EditableFields::Id::COOLING_PWM_PULL_MS,
                  doc["coolingPwmPullMs"].as<float>());
        }

        bool temperatureBusChanged = false;
        if (!doc["useDs2482ForTemps"].isNull()) {
          const bool nextUseDs2482 = doc["useDs2482ForTemps"].as<bool>();
          temperatureBusChanged |=
              g_settings.equipment.useDs2482ForTemps != nextUseDs2482;
          g_settings.equipment.useDs2482ForTemps = nextUseDs2482;
        }
        if (!doc["ds2482Address"].isNull()) {
          const uint8_t requestedAddress = doc["ds2482Address"].as<uint32_t>();
          const uint8_t safeAddress =
              requestedAddress < I2C_ADDR_DS2482_0 ||
                      requestedAddress > I2C_ADDR_DS2482_3
                  ? I2C_ADDR_DS2482_DEFAULT
                  : requestedAddress;
          temperatureBusChanged |=
              g_settings.equipment.ds2482Address != safeAddress;
          g_settings.equipment.ds2482Address = safeAddress;
        }
        if (!doc["bodyLevelSensorEnabled"].isNull()) {
          g_settings.equipment.bodyLevelSensorEnabled =
              doc["bodyLevelSensorEnabled"].as<bool>();
        }
        if (!doc["bodyLevelThresholdV"].isNull()) {
          g_settings.equipment.bodyLevelThresholdV = EditableFields::normalize(
              EditableFields::Id::SENSOR_BODY_LEVEL_THRESHOLD_V,
              doc["bodyLevelThresholdV"].as<float>());
        }
        if (!doc["bodyLevelTriggerAbove"].isNull()) {
          g_settings.equipment.bodyLevelTriggerAbove =
              doc["bodyLevelTriggerAbove"].as<bool>();
        }
        if (!doc["leakSensorEnabled"].isNull()) {
          g_settings.equipment.leakSensorEnabled =
              doc["leakSensorEnabled"].as<bool>();
        }
        if (!doc["leakThresholdV"].isNull()) {
          g_settings.equipment.leakThresholdV = EditableFields::normalize(
              EditableFields::Id::SENSOR_LEAK_THRESHOLD_V,
              doc["leakThresholdV"].as<float>());
        }
        if (!doc["leakTriggerAbove"].isNull()) {
          g_settings.equipment.leakTriggerAbove =
              doc["leakTriggerAbove"].as<bool>();
        }
        if (doc["temperatureTopology"].is<JsonObject>()) {
          JsonObject topology = doc["temperatureTopology"].as<JsonObject>();
          if (!topology["cube"].isNull()) {
            g_settings.equipment.temperatureTopology.cube =
                topology["cube"].as<bool>();
          }
          if (!topology["columnBottom"].isNull()) {
            g_settings.equipment.temperatureTopology.columnBottom =
                topology["columnBottom"].as<bool>();
          }
          if (!topology["columnTop"].isNull()) {
            g_settings.equipment.temperatureTopology.columnTop =
                topology["columnTop"].as<bool>();
          }
          if (!topology["reflux"].isNull()) {
            g_settings.equipment.temperatureTopology.reflux =
                topology["reflux"].as<bool>();
          }
          if (!topology["tsa"].isNull()) {
            g_settings.equipment.temperatureTopology.tsa =
                topology["tsa"].as<bool>();
          }
          if (!topology["waterIn"].isNull()) {
            g_settings.equipment.temperatureTopology.waterIn =
                topology["waterIn"].as<bool>();
          }
          if (!topology["waterOut"].isNull()) {
            g_settings.equipment.temperatureTopology.waterOut =
                topology["waterOut"].as<bool>();
          }
        }

        bool valveTypesChanged = false;
        uint8_t valveMask = g_settings.valves.normallyOpenMask;
        if (!doc["valveNormallyOpenMask"].isNull()) {
          valveMask = static_cast<uint8_t>(
              doc["valveNormallyOpenMask"].as<uint32_t>() & 0x3FU);
        }
        if (doc["valveTypes"].is<JsonObject>()) {
          JsonObject types = doc["valveTypes"].as<JsonObject>();
          static const char *const kKeys[6] = {"water", "heads", "body",
                                               "tails", "uno", "startStop"};
          for (uint8_t bit = 0; bit < 6; ++bit) {
            if (types[kKeys[bit]].isNull()) {
              continue;
            }
            const char *value = types[kKeys[bit]].as<const char *>();
            const bool normallyOpen =
                value != nullptr && (strcmp(value, "no") == 0 || strcmp(value, "НО") == 0);
            if (normallyOpen) {
              valveMask = static_cast<uint8_t>(valveMask | (1U << bit));
            } else {
              valveMask = static_cast<uint8_t>(valveMask & ~(1U << bit));
            }
          }
        }
        if (valveMask != g_settings.valves.normallyOpenMask) {
          g_settings.valves.normallyOpenMask = valveMask;
          valveTypesChanged = true;
        }

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        if (temperatureBusChanged) {
          Sensors::refreshTemperatureInventory();
        }
        if (valveTypesChanged) {
          Valves::refreshOutputs();
        }

        JsonDocument resp;
        resp["success"] = true;
        resp["valveNormallyOpenMask"] = g_settings.valves.normallyOpenMask;
        resp["packingType"] =
            getPackingTypeKey(g_settings.equipment.packingType);
        resp["packingCoeff"] = g_settings.equipment.packingCoeff;
        resp["useDs2482ForTemps"] = g_settings.equipment.useDs2482ForTemps;
        resp["ds2482Address"] = g_settings.equipment.ds2482Address;
        JsonObject temperatureTopology =
            resp["temperatureTopology"].to<JsonObject>();
        fillTemperatureTopologyJson(temperatureTopology, g_settings.equipment);
        JsonObject supportedModes = resp["supportedModes"].to<JsonObject>();
        fillTemperatureModeSupportJson(supportedModes, g_settings);

        String json;
        serializeJson(resp, json);
        request->send(200, "application/json", json);
      });

  server.on("/api/settings/stirrer", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              SystemState stateSnapshot;
              if (!StateSnapshot::read(stateSnapshot)) {
                request->send(503, "application/json",
                              "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
                return;
              }

              JsonDocument doc;
              fillStirrerSettingsJson(doc.to<JsonObject>(), g_settings);
              doc["available"] = stateSnapshot.stirrer.available;
              doc["running"] = stateSnapshot.stirrer.running;
              doc["autoMode"] = stateSnapshot.stirrer.autoMode;

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "/api/settings/stirrer", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        if (request->contentLength() == 0) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"error\":\"Request body is required\"}");
        }
      },
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}",
                false,
                "{\"success\":false,\"error\":\"Request body is required\"}")) {
          return;
        }

        SystemState stateSnapshot;
        if (!StateSnapshot::read(stateSnapshot)) {
          request->send(503, "application/json",
                        "{\"success\":false,\"message\":\"State snapshot unavailable\"}");
          return;
        }

        if (!doc["enabled"].isNull()) {
          g_settings.stirrer.enabled = doc["enabled"].as<bool>();
        }
        if (!doc["defaultSpeedPercent"].isNull()) {
          g_settings.stirrer.defaultSpeedPercent = static_cast<uint8_t>(
              EditableFields::normalize(
                  EditableFields::Id::STIRRER_DEFAULT_SPEED_PERCENT,
                  doc["defaultSpeedPercent"].as<float>()));
        }
        if (!doc["autoMashing"].isNull()) {
          g_settings.stirrer.autoMashing = doc["autoMashing"].as<bool>();
        }
        if (!doc["autoFermentation"].isNull()) {
          g_settings.stirrer.autoFermentation =
              doc["autoFermentation"].as<bool>();
        }
        if (!doc["autoNbk"].isNull()) {
          g_settings.stirrer.autoNbk = doc["autoNbk"].as<bool>();
        }

        const bool stopStirrerAfterSave = !g_settings.stirrer.enabled;

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        PowerCommandGateway::Command stopCommand;
        ControlCommandQueue::EnqueueResult controlResult =
            ControlCommandQueue::EnqueueResult::Accepted;
        uint32_t controlCommandId = 0;
        ControlCommandQueue::Status controlStatus =
            ControlCommandQueue::Status::Applied;
        if (stopStirrerAfterSave) {
          controlStatus = ControlCommandQueue::Status::Unknown;
          controlResult = ControlCommandQueue::enqueueAction(
              ControlCommandQueue::Type::SetStirrerAutoMode,
              ControlCommandQueue::Source::Api, 0, 0.0f, 0,
              ControlCommandQueue::Reason::OperatorRequest,
              &controlCommandId, &controlStatus);
          stopCommand = PowerCommandGateway::stopStirrer(
              PowerCommandGateway::Owner::MANUAL,
              PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT);
        }

        JsonDocument responseDoc;
        responseDoc["settingsSaved"] = true;
        JsonObject settingsJson = responseDoc["settings"].to<JsonObject>();
        fillStirrerSettingsJson(settingsJson, g_settings);
        JsonObject stirrer = responseDoc["stirrer"].to<JsonObject>();
        fillStirrerJson(stirrer, stateSnapshot);

        int httpStatus = 200;
        if (stopStirrerAfterSave) {
          const bool controlAccepted =
              controlResult == ControlCommandQueue::EnqueueResult::Accepted ||
              controlResult == ControlCommandQueue::EnqueueResult::Duplicate;
          const bool controlQueued =
              controlStatus == ControlCommandQueue::Status::Queued ||
              controlStatus == ControlCommandQueue::Status::Processing;
          const bool controlApplied =
              controlStatus == ControlCommandQueue::Status::Applied;
          const bool accepted = controlAccepted && stopCommand.accepted;
          const bool queued = controlQueued || stopCommand.queued;
          const bool applied = controlApplied && stopCommand.applied;
          responseDoc["commandType"] = "composite";
          responseDoc["success"] = accepted && applied;
          responseDoc["accepted"] = accepted;
          responseDoc["queued"] = queued;
          responseDoc["applied"] = applied;
          responseDoc["status"] = !accepted ? "rejected"
                                   : applied ? "applied" : "queued";
          JsonArray commands = responseDoc["commands"].to<JsonArray>();
          JsonObject control = commands.add<JsonObject>();
          control["commandType"] = "control";
          control["commandId"] = controlCommandId;
          control["accepted"] = controlAccepted;
          control["queued"] = controlQueued;
          control["applied"] = controlApplied;
          control["success"] = controlAccepted && controlApplied;
          control["status"] = ControlCommandQueue::statusToken(controlStatus);
          JsonObject output = commands.add<JsonObject>();
          output["commandType"] = "output";
          output["commandId"] = stopCommand.sequence;
          output["accepted"] = stopCommand.accepted;
          output["queued"] = stopCommand.queued;
          output["applied"] = stopCommand.applied;
          output["success"] = stopCommand.accepted && stopCommand.applied;
          output["status"] = !stopCommand.accepted ? "rejected"
                              : stopCommand.applied ? "applied" : "queued";
          output["rejectReason"] = PowerCommandGateway::rejectReasonString(
              stopCommand.rejectReason);
          httpStatus = !accepted
                           ? controlResult == ControlCommandQueue::EnqueueResult::Full
                                 ? 429
                                 : controlResult == ControlCommandQueue::EnqueueResult::NotInitialized
                                       ? 503
                                       : 409
                       : queued ? 202 : 200;
        } else {
          responseDoc["success"] = true;
        }

        String json;
        serializeJson(responseDoc, json);
        request->send(httpStatus, "application/json", json);
      });
}
