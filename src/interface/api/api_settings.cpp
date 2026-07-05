#include "api_routes.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <string.h>

#include "config.h"
#include "control/fsm.h"
#include "control/watt_control.h"
#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/sensors.h"
#include "drivers/stirrer.h"
#include "drivers/valves.h"
#include "interface/mqtt.h"
#include "interface/security.h"
#include "interface/webserver_shared.h"
#include "storage/logger.h"
#include "storage/nvs_manager.h"

static const char *getPackingTypeKey(PackingType type) {
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

static void fillStirrerSettingsJson(JsonObject settings,
                                    const Settings &source) {
  settings["enabled"] = source.stirrer.enabled;
  settings["defaultSpeedPercent"] = source.stirrer.defaultSpeedPercent;
  settings["autoMashing"] = source.stirrer.autoMashing;
  settings["autoFermentation"] = source.stirrer.autoFermentation;
  settings["autoNbk"] = source.stirrer.autoNbk;
}

static void getRectFeedstockDefaults(uint8_t feedstock, float &headsPct,
                                     float &bodyPct, float &tailsPct) {
  switch (feedstock) {
  case 0:
    headsPct = 6.0f;
    bodyPct = 84.0f;
    tailsPct = 10.0f;
    break;
  case 1:
    headsPct = 8.0f;
    bodyPct = 80.0f;
    tailsPct = 12.0f;
    break;
  case 2:
    headsPct = 7.0f;
    bodyPct = 81.0f;
    tailsPct = 12.0f;
    break;
  case 3:
    headsPct = 5.0f;
    bodyPct = 75.0f;
    tailsPct = 20.0f;
    break;
  case 4:
    headsPct = 8.0f;
    bodyPct = 74.0f;
    tailsPct = 18.0f;
    break;
  case 5:
    headsPct = 6.0f;
    bodyPct = 78.0f;
    tailsPct = 16.0f;
    break;
  case 6:
    headsPct = 7.0f;
    bodyPct = 79.0f;
    tailsPct = 14.0f;
    break;
  default:
    headsPct = RECT_HEADS_PERCENT_DEFAULT;
    bodyPct = RECT_BODY_PERCENT_DEFAULT;
    tailsPct = RECT_TAILS_PERCENT_DEFAULT;
    break;
  }
}

static void normalizeRectFractions(RectParams &params) {
  params.headsPercent = clampFloatRange(params.headsPercent, 0.0f, 40.0f);
  params.bodyPercent = clampFloatRange(params.bodyPercent, 0.0f, 100.0f);
  params.tailsPercent = clampFloatRange(params.tailsPercent, 0.0f, 100.0f);

  float sum = params.headsPercent + params.bodyPercent + params.tailsPercent;
  if (sum <= 100.0f) {
    return;
  }

  float excess = sum - 100.0f;
  if (params.tailsPercent >= excess) {
    params.tailsPercent -= excess;
    return;
  }

  excess -= params.tailsPercent;
  params.tailsPercent = 0.0f;
  params.bodyPercent =
      clampFloatRange(params.bodyPercent - excess, 0.0f, 100.0f);
}

void registerSettingsRoutes(AsyncWebServer &server) {
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
        JsonObject temperatureTopology = resp["temperatureTopology"].to<JsonObject>();
        fillTemperatureTopologyJson(temperatureTopology, g_settings.equipment);
        JsonObject supportedModes = resp["supportedModes"].to<JsonObject>();
        fillTemperatureModeSupportJson(supportedModes, g_settings);

        String json;
        serializeJson(resp, json);
        request->send(200, "application/json", json);
      });

  server.on("/api/settings/safety", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              JsonDocument doc;
              doc["pressureMaxMmHg"] = g_settings.safety.pressureMaxMmHg;
              doc["tsaMaxC"] = g_settings.safety.tsaMaxC;
              doc["waterOutMaxC"] = g_settings.safety.waterOutMaxC;
              doc["waterOutRiseRateCMin"] =
                  g_settings.safety.waterOutRiseRateCMin;
              doc["pressureRiseRateMmHgMin"] =
                  g_settings.safety.pressureRiseRateMmHgMin;

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "/api/settings/safety", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        if (!doc["pressureMaxMmHg"].isNull()) {
          g_settings.safety.pressureMaxMmHg = clampFloatRange(
              doc["pressureMaxMmHg"].as<float>(), 5.0f, 200.0f);
        }
        if (!doc["tsaMaxC"].isNull()) {
          g_settings.safety.tsaMaxC =
              clampFloatRange(doc["tsaMaxC"].as<float>(), 35.0f, 120.0f);
        }
        if (!doc["waterOutMaxC"].isNull()) {
          g_settings.safety.waterOutMaxC = clampFloatRange(
              doc["waterOutMaxC"].as<float>(), 30.0f, 120.0f);
        }
        if (!doc["waterOutRiseRateCMin"].isNull()) {
          g_settings.safety.waterOutRiseRateCMin = clampFloatRange(
              doc["waterOutRiseRateCMin"].as<float>(), 0.5f, 60.0f);
        }
        if (!doc["pressureRiseRateMmHgMin"].isNull()) {
          g_settings.safety.pressureRiseRateMmHgMin = clampFloatRange(
              doc["pressureRiseRateMmHgMin"].as<float>(), 1.0f, 200.0f);
        }

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        request->send(200, "application/json", "{\"success\":true}");
      });

  server.on("/api/settings/security", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              JsonDocument doc;
              doc["authEnabled"] = g_settings.security.authEnabled;
              doc["rateLimitEnabled"] = g_settings.security.rateLimitEnabled;
              doc["username"] = g_settings.security.username;
              doc["passwordConfigured"] =
                  (g_settings.security.password[0] != '\0');

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "/api/settings/security", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        const bool authEnabled = !doc["authEnabled"].isNull()
                                     ? doc["authEnabled"].as<bool>()
                                     : g_settings.security.authEnabled;
        const bool rateLimitEnabled = !doc["rateLimitEnabled"].isNull()
                                          ? doc["rateLimitEnabled"].as<bool>()
                                          : g_settings.security.rateLimitEnabled;
        const bool hasUsernameField = !doc["username"].isNull();
        const bool hasPasswordField = !doc["password"].isNull();
        const char *username = hasUsernameField
                                   ? (doc["username"] | "")
                                   : g_settings.security.username;
        const char *password =
            hasPasswordField ? (doc["password"] | "") : nullptr;

        if (authEnabled && (!username || strlen(username) == 0)) {
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"Username required\"}");
          return;
        }

        const bool hasStoredPassword =
            (g_settings.security.password[0] != '\0');
        if (authEnabled &&
            (!hasStoredPassword && (!password || strlen(password) == 0))) {
          request->send(
              400, "application/json",
              "{\"success\":false,\"error\":\"Password required to enable auth\"}");
          return;
        }

        g_settings.security.authEnabled = authEnabled;
        g_settings.security.rateLimitEnabled = rateLimitEnabled;
        if (hasUsernameField && username) {
          strlcpy(g_settings.security.username, username,
                  sizeof(g_settings.security.username));
        }
        if (hasPasswordField && password && strlen(password) > 0) {
          strlcpy(g_settings.security.password, password,
                  sizeof(g_settings.security.password));
        }

        if (!NVSManager::saveSettings(g_settings)) {
          Logger::logf(2, "Security settings save failed");
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        applySecuritySettings();
        Logger::logf(
            0, "Security settings updated: auth=%s, rateLimit=%s, user=%s",
            authEnabled ? "enabled" : "disabled",
            rateLimitEnabled ? "enabled" : "disabled",
            g_settings.security.username);
        request->send(200, "application/json", "{\"success\":true}");
      });

  server.on("/api/settings/nbk", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              JsonDocument doc;
              doc["powerW"] = g_settings.nbk.powerW;
              doc["pumpSpeedMlH"] = g_settings.nbk.pumpSpeedMlH;
              doc["columnBottomTempThresholdC"] =
                  g_settings.nbk.columnBottomTempThresholdC;

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "/api/settings/nbk", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        if (!doc["powerW"].isNull()) {
          g_settings.nbk.powerW =
              clampFloatRange(doc["powerW"].as<float>(), 500.0f, 5500.0f);
        }
        if (!doc["pumpSpeedMlH"].isNull()) {
          g_settings.nbk.pumpSpeedMlH = clampFloatRange(
              doc["pumpSpeedMlH"].as<float>(), 100.0f, 120000.0f);
        }
        if (!doc["columnBottomTempThresholdC"].isNull()) {
          g_settings.nbk.columnBottomTempThresholdC = clampFloatRange(
              doc["columnBottomTempThresholdC"].as<float>(), 50.0f, 110.0f);
        }

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        request->send(200, "application/json", "{\"success\":true}");
      });

  server.on("/api/settings/fermentation", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              JsonDocument doc;
              doc["targetTempC"] = g_settings.fermentation.targetTempC;
              doc["hysteresisC"] = g_settings.fermentation.hysteresisC;
              doc["useHeater"] = g_settings.fermentation.useHeater;

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "/api/settings/fermentation", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        if (!doc["targetTempC"].isNull()) {
          g_settings.fermentation.targetTempC = clampFloatRange(
              doc["targetTempC"].as<float>(), 5.0f, 45.0f);
        }
        if (!doc["hysteresisC"].isNull()) {
          g_settings.fermentation.hysteresisC = clampFloatRange(
              doc["hysteresisC"].as<float>(), 0.1f, 10.0f);
        }
        if (!doc["useHeater"].isNull()) {
          g_settings.fermentation.useHeater = doc["useHeater"].as<bool>();
        }

        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        request->send(200, "application/json", "{\"success\":true}");
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

  server.on("/api/settings/mqtt", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              JsonDocument doc;
              doc["enabled"] = g_settings.mqtt.enabled;
              doc["server"] = g_settings.mqtt.server;
              doc["port"] = g_settings.mqtt.port;
              doc["username"] = g_settings.mqtt.username;
              doc["password"] = g_settings.mqtt.password;
              doc["baseTopic"] = g_settings.mqtt.baseTopic;
              doc["publishInterval"] = g_settings.mqtt.publishInterval;
              doc["discovery"] = g_settings.mqtt.discovery;
              doc["connected"] = MQTT::isConnected();

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "/api/settings/mqtt", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        const bool enabled = doc["enabled"] | g_settings.mqtt.enabled;
        const char *serverValue = !doc["server"].isNull()
                                      ? (doc["server"] | "")
                                      : g_settings.mqtt.server;
        uint16_t port = !doc["port"].isNull()
                            ? static_cast<uint16_t>(doc["port"] |
                                                    g_settings.mqtt.port)
                            : g_settings.mqtt.port;
        const char *username = !doc["username"].isNull()
                                   ? (doc["username"] | "")
                                   : g_settings.mqtt.username;
        const char *password = !doc["password"].isNull()
                                   ? (doc["password"] | "")
                                   : g_settings.mqtt.password;
        const char *baseTopic = !doc["baseTopic"].isNull()
                                    ? (doc["baseTopic"] | "")
                                    : g_settings.mqtt.baseTopic;
        const bool discovery = !doc["discovery"].isNull()
                                   ? static_cast<bool>(doc["discovery"])
                                   : g_settings.mqtt.discovery;
        uint32_t publishInterval = !doc["publishInterval"].isNull()
                                       ? static_cast<uint32_t>(
                                             doc["publishInterval"] |
                                             g_settings.mqtt.publishInterval)
                                       : g_settings.mqtt.publishInterval;

        if (enabled && (!serverValue || serverValue[0] == '\0')) {
          Logger::logf(1,
                       "MQTT settings rejected: server is required when enabled");
          request->send(
              400, "application/json",
              "{\"success\":false,\"error\":\"MQTT server is required when enabled\"}");
          return;
        }
        if (port == 0) {
          port = 1883;
        }
        if (!baseTopic || baseTopic[0] == '\0') {
          baseTopic = "smart-column";
        }
        if (publishInterval < 1000) {
          publishInterval = 1000;
        }
        if (publishInterval > 60000) {
          publishInterval = 60000;
        }

        g_settings.mqtt.enabled = enabled;
        strlcpy(g_settings.mqtt.server, serverValue,
                sizeof(g_settings.mqtt.server));
        g_settings.mqtt.port = port;
        strlcpy(g_settings.mqtt.username, username,
                sizeof(g_settings.mqtt.username));
        strlcpy(g_settings.mqtt.password, password,
                sizeof(g_settings.mqtt.password));
        strlcpy(g_settings.mqtt.baseTopic, baseTopic,
                sizeof(g_settings.mqtt.baseTopic));
        g_settings.mqtt.publishInterval = publishInterval;
        g_settings.mqtt.discovery = discovery;

        if (!NVSManager::saveSettings(g_settings)) {
          Logger::logf(2, "MQTT settings save failed");
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        request->send(200, "application/json", "{\"success\":true}");
        Logger::logf(
            0,
            "MQTT settings updated: %s, server=%s:%u, topic=%s, interval=%lums",
            g_settings.mqtt.enabled ? "enabled" : "disabled",
            g_settings.mqtt.server[0] ? g_settings.mqtt.server : "-",
            g_settings.mqtt.port, g_settings.mqtt.baseTopic,
            static_cast<unsigned long>(g_settings.mqtt.publishInterval));

        MQTT::disconnect();
        if (g_settings.mqtt.enabled && g_settings.mqtt.server[0] != '\0') {
          MQTT::setBaseTopic(g_settings.mqtt.baseTopic);
          MQTT::init(g_settings.mqtt.server, g_settings.mqtt.port,
                     g_settings.mqtt.username[0]
                         ? g_settings.mqtt.username
                         : nullptr,
                     g_settings.mqtt.password[0]
                         ? g_settings.mqtt.password
                         : nullptr);
          if (WiFi.status() == WL_CONNECTED) {
            MQTT::handle();
          }
        }
      });

  server.on(
      "/api/settings/mqtt/test", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (!g_settings.mqtt.enabled) {
          Logger::logf(1, "MQTT test rejected: MQTT is disabled");
          request->send(400, "application/json",
                        "{\"success\":false,\"error\":\"MQTT disabled\"}");
          return;
        }
        if (g_settings.mqtt.server[0] == '\0') {
          Logger::logf(1, "MQTT test rejected: broker is not configured");
          request->send(
              400, "application/json",
              "{\"success\":false,\"error\":\"MQTT server is not configured\"}");
          return;
        }
        if (WiFi.status() != WL_CONNECTED) {
          Logger::logf(1, "MQTT test rejected: WiFi STA is not connected");
          request->send(
              503, "application/json",
              "{\"success\":false,\"error\":\"WiFi STA not connected\"}");
          return;
        }

        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}", true)) {
          return;
        }
        const char *message =
            doc["message"] | "Smart-Column S3: MQTT test from Web UI";

        if (!MQTT::isConnected()) {
          MQTT::setBaseTopic(g_settings.mqtt.baseTopic);
          MQTT::init(g_settings.mqtt.server, g_settings.mqtt.port,
                     g_settings.mqtt.username[0]
                         ? g_settings.mqtt.username
                         : nullptr,
                     g_settings.mqtt.password[0]
                         ? g_settings.mqtt.password
                         : nullptr);
          for (uint8_t i = 0; i < 20 && !MQTT::isConnected(); ++i) {
            MQTT::handle();
            delay(50);
          }
        }

        if (!MQTT::isConnected()) {
          Logger::logf(1, "MQTT test failed: broker unavailable");
          request->send(
              503, "application/json",
              "{\"success\":false,\"error\":\"MQTT broker unavailable\"}");
          return;
        }

        MQTT::publishNotification("MQTT test", message, "info");
        Logger::logf(0, "MQTT test notification sent");
        request->send(200, "application/json", "{\"success\":true}");
      });

  server.on("/api/settings/rect", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              JsonDocument doc;
              const RectParams &params = g_settings.rectParams;

              doc["feedstock"] = params.feedstock;
              doc["feedVolumeL"] = params.feedVolumeL;
              doc["feedAbvPercent"] = params.feedAbvPercent;
              doc["headsPercent"] = params.headsPercent;
              doc["bodyPercent"] = params.bodyPercent;
              doc["tailsPercent"] = params.tailsPercent;
              doc["headsSpeedMlHKw"] = params.headsSpeedMlHKw;
              doc["bodySpeedMlHKw"] = params.bodySpeedMlHKw;
              doc["stabilizationMin"] = params.stabilizationMin;
              doc["purgeMin"] = params.purgeMin;
              doc["baroCorrectionEnabled"] = params.baroCorrectionEnabled;

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on(
      "/api/settings/rect", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"error\":\"Invalid JSON\"}")) {
          return;
        }

        JsonObject root = doc.as<JsonObject>();
        JsonObject params =
            root["params"].is<JsonObject>() ? root["params"].as<JsonObject>()
                                            : root;

        RectParams updated = g_settings.rectParams;
        bool fractionsUpdated = false;

        if (!params["feedstock"].isNull()) {
          int feedstock = params["feedstock"].as<int>();
          if (feedstock < 0) {
            feedstock = 0;
          }
          if (feedstock > 7) {
            feedstock = 7;
          }
          updated.feedstock = static_cast<uint8_t>(feedstock);
        }

        const bool applyFeedstockDefaults =
            params["applyFeedstockDefaults"] | false;
        if (applyFeedstockDefaults) {
          getRectFeedstockDefaults(updated.feedstock, updated.headsPercent,
                                   updated.bodyPercent, updated.tailsPercent);
          fractionsUpdated = true;
        }

        if (!params["feedVolumeL"].isNull()) {
          updated.feedVolumeL = clampFloatRange(
              params["feedVolumeL"].as<float>(), 1.0f, 250.0f);
        }
        if (!params["feedAbvPercent"].isNull()) {
          updated.feedAbvPercent = clampFloatRange(
              params["feedAbvPercent"].as<float>(), 1.0f, 96.0f);
        }
        if (!params["headsPercent"].isNull()) {
          updated.headsPercent = clampFloatRange(
              params["headsPercent"].as<float>(), 0.0f, 40.0f);
          fractionsUpdated = true;
        }
        if (!params["bodyPercent"].isNull()) {
          updated.bodyPercent = clampFloatRange(
              params["bodyPercent"].as<float>(), 0.0f, 100.0f);
          fractionsUpdated = true;
        }
        if (!params["tailsPercent"].isNull()) {
          updated.tailsPercent = clampFloatRange(
              params["tailsPercent"].as<float>(), 0.0f, 100.0f);
          fractionsUpdated = true;
        }
        if (!params["headsSpeedMlHKw"].isNull()) {
          updated.headsSpeedMlHKw = clampFloatRange(
              params["headsSpeedMlHKw"].as<float>(), 10.0f, 2000.0f);
        }
        if (!params["bodySpeedMlHKw"].isNull()) {
          updated.bodySpeedMlHKw = clampFloatRange(
              params["bodySpeedMlHKw"].as<float>(), 50.0f, 3000.0f);
        }
        if (!params["stabilizationMin"].isNull()) {
          updated.stabilizationMin =
              clampU16Range(params["stabilizationMin"].as<uint32_t>(), 1, 180);
        }
        if (!params["purgeMin"].isNull()) {
          updated.purgeMin =
              clampU16Range(params["purgeMin"].as<uint32_t>(), 1, 120);
        }
        if (!params["baroCorrectionEnabled"].isNull()) {
          updated.baroCorrectionEnabled =
              params["baroCorrectionEnabled"].as<bool>();
        }

        if (fractionsUpdated) {
          normalizeRectFractions(updated);
        }

        g_settings.rectParams = updated;
        if (!NVSManager::saveSettings(g_settings)) {
          request->send(500, "application/json",
                        "{\"success\":false,\"error\":\"Failed to save settings\"}");
          return;
        }

        JsonDocument out;
        out["success"] = true;
        out["feedstock"] = g_settings.rectParams.feedstock;
        out["feedVolumeL"] = g_settings.rectParams.feedVolumeL;
        out["feedAbvPercent"] = g_settings.rectParams.feedAbvPercent;
        out["headsPercent"] = g_settings.rectParams.headsPercent;
        out["bodyPercent"] = g_settings.rectParams.bodyPercent;
        out["tailsPercent"] = g_settings.rectParams.tailsPercent;
        out["headsSpeedMlHKw"] = g_settings.rectParams.headsSpeedMlHKw;
        out["bodySpeedMlHKw"] = g_settings.rectParams.bodySpeedMlHKw;
        out["stabilizationMin"] = g_settings.rectParams.stabilizationMin;
        out["purgeMin"] = g_settings.rectParams.purgeMin;
        out["baroCorrectionEnabled"] =
            g_settings.rectParams.baroCorrectionEnabled;

        String json;
        serializeJson(out, json);
        request->send(200, "application/json", json);
      });

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
          int powerPercent = doc["power"] | 0;
          if (powerPercent < 0) {
            powerPercent = 0;
          }
          if (powerPercent > 100) {
            powerPercent = 100;
          }
          powerWatts = static_cast<uint16_t>(
              (static_cast<uint32_t>(heaterMaxW) * powerPercent) / 100U);
        }
        Heater::setPowerWatts(powerWatts);

        const uint8_t powerPercent =
            heaterMaxW > 0
                ? static_cast<uint8_t>(
                      (static_cast<uint32_t>(powerWatts) * 100U +
                       heaterMaxW / 2U) /
                      heaterMaxW)
                : 0;
        char resp[160];
        snprintf(resp, sizeof(resp),
                 "{\"success\":true,\"powerW\":%u,\"powerPercent\":%u}",
                 powerWatts, powerPercent);
        request->send(200, "application/json", resp);
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
        WattControl::setOverrideWatts(static_cast<int16_t>(powerWatts));

        char resp[128];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"powerW\":%d}",
                 powerWatts);
        request->send(200, "application/json", resp);
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

        const float speed = doc["speed"] | 0.0f;
        if (speed <= 0.0f) {
          Pump::stop();
          request->send(200, "application/json",
                        "{\"success\":true,\"running\":false}");
          return;
        }

        Pump::start(speed);
        char resp[128];
        snprintf(resp, sizeof(resp),
                 "{\"success\":true,\"running\":true,\"speed\":%.1f}", speed);
        request->send(200, "application/json", resp);
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
        if (allOff) {
          Valves::closeAll();
          request->send(200, "application/json", "{\"success\":true}");
          return;
        }

        if (!doc["water"].isNull()) {
          Valves::setWater(doc["water"].as<bool>());
        }
        if (!doc["heads"].isNull()) {
          Valves::setHeads(doc["heads"].as<bool>());
        }
        if (!doc["uno"].isNull()) {
          Valves::setUno(doc["uno"].as<bool>());
        }
        if (!doc["startStopDuty"].isNull()) {
          const uint8_t duty =
              clampU8Range(doc["startStopDuty"].as<uint32_t>(), 0, 255);
          Valves::setStartStop(duty);
        }

        request->send(200, "application/json", "{\"success\":true}");
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

        if (g_state.mode != Mode::MANUAL_RECT) {
          request->send(400, "application/json",
                        "{\"error\":\"Not in MANUAL_RECT mode\"}");
          return;
        }

        if (doc["phase"].isNull()) {
          request->send(400, "application/json",
                        "{\"error\":\"Missing phase\"}");
          return;
        }

        const uint8_t phaseId = doc["phase"].as<uint8_t>();
        FSM::ManualRect::setPhase(g_state, static_cast<RectPhase>(phaseId));

        char resp[128];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"phase\":%d}",
                 static_cast<int>(phaseId));
        request->send(200, "application/json", resp);
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

        bool changed = false;
        if (!doc["heads"].isNull()) {
          const float v = doc["heads"].as<float>();
          g_state.stats.headsVolume = (v < 0.0f) ? 0.0f : v;
          changed = true;
        }
        if (!doc["body"].isNull()) {
          const float v = doc["body"].as<float>();
          g_state.stats.bodyVolume = (v < 0.0f) ? 0.0f : v;
          changed = true;
        }
        if (!doc["tails"].isNull()) {
          const float v = doc["tails"].as<float>();
          g_state.stats.tailsVolume = (v < 0.0f) ? 0.0f : v;
          changed = true;
        }

        if (!changed) {
          request->send(400, "application/json",
                        "{\"error\":\"At least one field is required\"}");
          return;
        }

        const bool syncTotal = doc["syncTotal"] | true;
        if (syncTotal) {
          g_state.pump.totalVolumeMl = g_state.stats.headsVolume +
                                       g_state.stats.bodyVolume +
                                       g_state.stats.tailsVolume;
        }

        JsonDocument out;
        out["success"] = true;
        out["heads"] = g_state.stats.headsVolume;
        out["body"] = g_state.stats.bodyVolume;
        out["tails"] = g_state.stats.tailsVolume;
        out["totalMl"] = g_state.pump.totalVolumeMl;
        String json;
        serializeJson(out, json);
        request->send(200, "application/json", json);
      });

  server.on(
      "/api/settings/demo", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        if (!deserializeRequestJsonBody(
                request, data, len, index, total, doc,
                "{\"success\":false,\"message\":\"Invalid JSON\"}")) {
          return;
        }

        const bool enabled = doc["enabled"] | false;
        g_settings.demoMode = enabled;

        LOG_I("Demo mode %s", enabled ? "ENABLED" : "DISABLED");
        Logger::logf(0, "Demo mode %s", enabled ? "enabled" : "disabled");

        Preferences prefs;
        prefs.begin("settings", false);
        prefs.putBool("demoMode", enabled);
        prefs.end();

        char response[128];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"demoMode\":%s}",
                 enabled ? "true" : "false");
        request->send(200, "application/json", response);
      });

  server.on("/api/settings/demo", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              char response[64];
              snprintf(response, sizeof(response), "{\"demoMode\":%s}",
                       g_settings.demoMode ? "true" : "false");
              request->send(200, "application/json", response);
            });

  server.on("/api/reboot/status", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              JsonDocument doc;
              const uint32_t totalReboots = g_rebootTracker.totalReboots;
              const uint32_t wdtReboots = g_rebootTracker.wdtReboots;
              const uint32_t crashReboots = g_rebootTracker.crashReboots;
              const uint32_t userReboots = g_rebootTracker.userReboots;
              const uint32_t otherReboots =
                  totalReboots > (wdtReboots + crashReboots + userReboots)
                      ? totalReboots -
                            (wdtReboots + crashReboots + userReboots)
                      : 0;
              const uint8_t lastReason = g_rebootTracker.lastReason;
              const bool lastWasWdt =
                  lastReason == ESP_RST_WDT || lastReason == ESP_RST_TASK_WDT ||
                  lastReason == ESP_RST_INT_WDT;
              const bool lastWasCrash = lastReason == ESP_RST_PANIC;
              const bool lastWasUser =
                  lastReason == ESP_RST_SW || lastReason == ESP_RST_EXT;

              doc["lastReason"] = g_rebootTracker.lastReason;
              doc["lastReasonStr"] = g_rebootTracker.lastReasonStr;
              doc["totalReboots"] = totalReboots;
              doc["wdtReboots"] = wdtReboots;
              doc["crashReboots"] = crashReboots;
              doc["userReboots"] = userReboots;
              doc["otherReboots"] = otherReboots;
              doc["uptimeSec"] = millis() / 1000UL;
              doc["healthOverall"] = g_state.health.overallHealth;
              doc["freeHeap"] = ESP.getFreeHeap();
              doc["lastReasonKind"] =
                  lastWasWdt ? "wdt"
                             : lastWasCrash ? "crash"
                                            : lastWasUser ? "user" : "other";

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    LOG_W("Reboot requested via API");
    Logger::logf(1, "System reboot requested via API");
    request->send(200, "application/json",
                  "{\"success\":true,\"message\":\"Rebooting...\"}");

    delay(500);
    ESP.restart();
  });
}
