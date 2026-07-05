#include "settings_modules.h"

#include <string.h>

#include "config.h"
#include "drivers/sensors.h"
#include "drivers/stirrer.h"
#include "drivers/valves.h"
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
              doc["coolingPwmMinDuty"] =
                  g_settings.equipment.coolingPwmMinDuty;
              doc["coolingPwmMaxDuty"] =
                  g_settings.equipment.coolingPwmMaxDuty;
              doc["coolingPwmStartupDuty"] =
                  g_settings.equipment.coolingPwmStartupDuty;
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
              pzem["available"] = g_state.health.pzemOk;
              pzem["uartNum"] = PZEM_UART_NUM;
              pzem["baudRate"] = PZEM_BAUD_RATE;
              pzem["rxPin"] = PIN_PZEM_RX;
              pzem["txPin"] = PIN_PZEM_TX;
              pzem["voltage"] = g_state.power.voltage;
              pzem["current"] = g_state.power.current;
              pzem["power"] = g_state.power.power;
              pzem["energy"] = g_state.power.energy;
              pzem["frequency"] = g_state.power.frequency;
              pzem["powerFactor"] = g_state.power.powerFactor;
              pzem["lastUpdate"] = g_state.power.lastUpdate;

              JsonObject bootGpio = doc["bootGpio"].to<JsonObject>();
              bootGpio["completed"] = g_bootGpioSelfTest.completed;
              bootGpio["overallOk"] = g_bootGpioSelfTest.overallOk;
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
              fillSafetyChannelsJson(safetyChannels);

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
              clampU16Range(doc["heaterPowerW"].as<uint32_t>(), 1000, 10000);
        }
        if (!doc["columnHeightMm"].isNull()) {
          g_settings.equipment.columnHeightMm =
              clampU16Range(doc["columnHeightMm"].as<uint32_t>(), 500, 3000);
        }
        if (!doc["cubeVolumeL"].isNull()) {
          g_settings.equipment.cubeVolumeL =
              clampFloatRange(doc["cubeVolumeL"].as<float>(), 5.0f, 250.0f);
        }
        if (!doc["minHeaterSubmergeL"].isNull()) {
          g_settings.equipment.minHeaterSubmergeL = clampFloatRange(
              doc["minHeaterSubmergeL"].as<float>(), 0.5f, 100.0f);
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
          g_settings.equipment.boosterHeaterPowerW = clampU16Range(
              doc["boosterHeaterPowerW"].as<uint32_t>(), 1000, 10000);
        }
        if (!doc["boosterHeaterStopCubeTempC"].isNull()) {
          g_settings.equipment.boosterHeaterStopCubeTempC = clampFloatRange(
              doc["boosterHeaterStopCubeTempC"].as<float>(), 20.0f, 100.0f);
        }
        if (!doc["coolingPwmEnabled"].isNull()) {
          g_settings.equipment.coolingPwmEnabled =
              doc["coolingPwmEnabled"].as<bool>();
        }
        if (!doc["coolingPwmMinDuty"].isNull()) {
          g_settings.equipment.coolingPwmMinDuty =
              clampU8Range(doc["coolingPwmMinDuty"].as<uint32_t>(), 0, 255);
        }
        if (!doc["coolingPwmMaxDuty"].isNull()) {
          g_settings.equipment.coolingPwmMaxDuty =
              clampU8Range(doc["coolingPwmMaxDuty"].as<uint32_t>(), 0, 255);
        }
        if (g_settings.equipment.coolingPwmMinDuty >
            g_settings.equipment.coolingPwmMaxDuty) {
          g_settings.equipment.coolingPwmMinDuty =
              g_settings.equipment.coolingPwmMaxDuty;
        }
        if (!doc["coolingPwmStartupDuty"].isNull()) {
          g_settings.equipment.coolingPwmStartupDuty = clampU8Range(
              doc["coolingPwmStartupDuty"].as<uint32_t>(),
              g_settings.equipment.coolingPwmMinDuty,
              g_settings.equipment.coolingPwmMaxDuty);
        } else {
          g_settings.equipment.coolingPwmStartupDuty = clampU8Range(
              g_settings.equipment.coolingPwmStartupDuty,
              g_settings.equipment.coolingPwmMinDuty,
              g_settings.equipment.coolingPwmMaxDuty);
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
          g_settings.equipment.bodyLevelThresholdV = clampFloatRange(
              doc["bodyLevelThresholdV"].as<float>(), 0.0f, 4.096f);
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
          g_settings.equipment.leakThresholdV = clampFloatRange(
              doc["leakThresholdV"].as<float>(), 0.0f, 4.096f);
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

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        if (temperatureBusChanged) {
          Sensors::refreshTemperatureInventory();
        }

        JsonDocument resp;
        resp["success"] = true;
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
              syncStirrerState();

              JsonDocument doc;
              fillStirrerSettingsJson(doc.to<JsonObject>(), g_settings);
              doc["available"] = g_state.stirrer.available;
              doc["running"] = g_state.stirrer.running;
              doc["autoMode"] = g_state.stirrer.autoMode;

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

        if (!doc["enabled"].isNull()) {
          g_settings.stirrer.enabled = doc["enabled"].as<bool>();
        }
        if (!doc["defaultSpeedPercent"].isNull()) {
          g_settings.stirrer.defaultSpeedPercent = clampU8Range(
              doc["defaultSpeedPercent"].as<uint32_t>(), 1, 100);
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

        if (!g_settings.stirrer.enabled) {
          g_state.stirrer.autoMode = false;
          Stirrer::stop();
        }

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        syncStirrerState();

        JsonDocument responseDoc;
        responseDoc["success"] = true;
        JsonObject settingsJson = responseDoc["settings"].to<JsonObject>();
        fillStirrerSettingsJson(settingsJson, g_settings);
        JsonObject stirrer = responseDoc["stirrer"].to<JsonObject>();
        fillStirrerJson(stirrer, g_state);

        String json;
        serializeJson(responseDoc, json);
        request->send(200, "application/json", json);
      });
}
