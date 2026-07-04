#include "api_routes.h"

#include <math.h>
#include <string.h>

#include "drivers/sensors.h"
#include "interface/webserver_shared.h"
#include "storage/nvs_manager.h"

static bool isZeroTempAddressLocal(const uint8_t address[8]) {
  if (!address) {
    return true;
  }
  for (uint8_t i = 0; i < 8; ++i) {
    if (address[i] != 0) {
      return false;
    }
  }
  return true;
}

void registerCalibrationRoutes(AsyncWebServer &server) {
  server.on("/api/calibration", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;

    JsonObject pump = doc["pump"].to<JsonObject>();
    pump["mlPerRev"] = g_settings.pumpCal.mlPerRevolution;
    pump["stepsPerRev"] = g_settings.pumpCal.stepsPerRevolution;
    pump["microsteps"] = g_settings.pumpCal.microsteps;

    JsonArray temps = doc["temperatures"].to<JsonArray>();
    for (uint8_t i = 0; i < TEMP_COUNT; i++) {
      JsonObject t = temps.add<JsonObject>();
      t["index"] = i;
      t["name"] = getTempSensorLabel(i);
      t["offset"] = g_settings.tempCal.offsets[i];

      char assignedAddrStr[24];
      formatTempAddress(g_settings.tempCal.addresses[i], assignedAddrStr,
                        sizeof(assignedAddrStr));
      uint8_t detectedAddress[8] = {0};
      char detectedAddrStr[24];
      if (Sensors::getDiscoveredTempAddress(i, detectedAddress)) {
        formatTempAddress(detectedAddress, detectedAddrStr,
                          sizeof(detectedAddrStr));
      } else {
        detectedAddrStr[0] = '\0';
      }
      t["address"] = detectedAddrStr[0] != '\0' ? detectedAddrStr : assignedAddrStr;
      t["assignedAddress"] = assignedAddrStr;
      t["detectedAddress"] = detectedAddrStr;
      t["mappingMode"] = assignedAddrStr[0] != '\0' ? "manual" : "auto";

      float currentTemp = 0;
      switch (i) {
      case TEMP_CUBE:
        currentTemp = g_state.temps.cube;
        break;
      case TEMP_COLUMN_BOTTOM:
        currentTemp = g_state.temps.columnBottom;
        break;
      case TEMP_COLUMN_TOP:
        currentTemp = g_state.temps.columnTop;
        break;
      case TEMP_REFLUX:
        currentTemp = g_state.temps.reflux;
        break;
      case TEMP_TSA:
        currentTemp = g_state.temps.tsa;
        break;
      case TEMP_WATER_IN:
        currentTemp = g_state.temps.waterIn;
        break;
      case TEMP_WATER_OUT:
        currentTemp = g_state.temps.waterOut;
        break;
      }
      t["current"] = currentTemp;
      t["valid"] = g_state.temps.valid[i];
    }

    JsonObject pressureSensor = doc["pressureSensor"].to<JsonObject>();
    pressureSensor["pointCount"] = g_settings.pressureCal.pointCount;
    JsonArray pressureVoltages = pressureSensor["voltagePoints"].to<JsonArray>();
    JsonArray pressureMmHgPoints =
        pressureSensor["pressurePoints"].to<JsonArray>();
    for (uint8_t i = 0; i < g_settings.pressureCal.pointCount; i++) {
      pressureVoltages.add(g_settings.pressureCal.voltagePoints[i]);
      pressureMmHgPoints.add(g_settings.pressureCal.pressurePoints[i]);
    }
    pressureSensor["zeroOffsetMmHg"] = g_settings.pressureCal.zeroOffsetMmHg;
    pressureSensor["ads1115Available"] = g_state.health.ads1115Ok;
    pressureSensor["source"] = "ADS1115 A1 @ 0x48";
    pressureSensor["currentVoltage"] = g_state.pressure.sensorVoltage;
    pressureSensor["currentAdc"] = g_state.pressure.sensorAdc;
    pressureSensor["currentPressure"] = g_state.pressure.cube;
    pressureSensor["valid"] = g_state.pressure.ok;
    pressureSensor["calibrated"] = g_settings.pressureCal.pointCount >= 2;

    JsonObject hydro = doc["hydrometer"].to<JsonObject>();
    hydro["densityOffset"] = g_settings.hydroCal.densityOffset;
    hydro["pointCount"] = g_settings.hydroCal.pointCount;
    JsonArray abvPoints = hydro["abvPoints"].to<JsonArray>();
    JsonArray pressurePoints = hydro["pressurePoints"].to<JsonArray>();
    for (uint8_t i = 0; i < g_settings.hydroCal.pointCount; i++) {
      abvPoints.add(g_settings.hydroCal.abvPoints[i]);
      pressurePoints.add(g_settings.hydroCal.pressurePoints[i]);
    }
    hydro["currentPressure"] = g_state.hydrometer.pressure;
    hydro["currentDensity"] = g_state.hydrometer.density;
    hydro["currentABV"] = g_state.hydrometer.abv;
    hydro["valid"] = g_state.hydrometer.valid;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on(
      "/api/calibration/pump", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid JSON\"}");
          return;
        }

        if (!doc["mlPerRev"].isNull() || !doc["stepsPerRev"].isNull()) {
          if (!doc["mlPerRev"].isNull()) {
            g_settings.pumpCal.mlPerRevolution = doc["mlPerRev"].as<float>();
            LOG_I("Pump mlPerRev: %.3f", g_settings.pumpCal.mlPerRevolution);
          }
          if (!doc["stepsPerRev"].isNull()) {
            g_settings.pumpCal.stepsPerRevolution =
                doc["stepsPerRev"].as<uint16_t>();
            LOG_I("Pump stepsPerRev: %u",
                  g_settings.pumpCal.stepsPerRevolution);
          }
          NVSManager::saveSettings(g_settings);
          request->send(200, "application/json",
                        "{\"status\":\"ok\",\"method\":\"direct\"}");
          return;
        }

        if (!doc["knownVolume"].isNull() && !doc["steps"].isNull()) {
          float knownVolume = doc["knownVolume"].as<float>();
          uint32_t steps = doc["steps"].as<uint32_t>();

          uint16_t stepsPerRev = g_settings.pumpCal.stepsPerRevolution *
                                 g_settings.pumpCal.microsteps;
          float revolutions = static_cast<float>(steps) / stepsPerRev;

          if (revolutions > 0) {
            g_settings.pumpCal.mlPerRevolution = knownVolume / revolutions;
            NVSManager::saveSettings(g_settings);

            LOG_I("Pump calibrated: %.3f ml/rev (from %.1f ml in %u steps)",
                  g_settings.pumpCal.mlPerRevolution, knownVolume, steps);

            JsonDocument resp;
            resp["status"] = "ok";
            resp["method"] = "measured";
            resp["mlPerRev"] = g_settings.pumpCal.mlPerRevolution;

            String json;
            serializeJson(resp, json);
            request->send(200, "application/json", json);
          } else {
            request->send(400, "application/json",
                          "{\"error\":\"Invalid steps\"}");
          }
          return;
        }

        request->send(400, "application/json",
                      "{\"error\":\"Missing parameters\"}");
      });

  server.on(
      "/api/calibration/temp", HTTP_POST, [](AsyncWebServerRequest *request) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid JSON\"}");
          return;
        }

        uint8_t sensorIndex = doc["index"].as<uint8_t>();

        if (sensorIndex >= TEMP_COUNT) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid sensor index\"}");
          return;
        }

        if (!doc["address"].isNull()) {
          const String addressValue = doc["address"].as<String>();
          if (addressValue.length() == 0) {
            memset(g_settings.tempCal.addresses[sensorIndex], 0,
                   sizeof(g_settings.tempCal.addresses[sensorIndex]));
          } else {
            uint8_t parsedAddress[8] = {0};
            if (!parseTempAddressString(addressValue.c_str(), parsedAddress)) {
              request->send(400, "application/json",
                            "{\"error\":\"Invalid sensor address\"}");
              return;
            }
            for (uint8_t role = 0; role < TEMP_COUNT; ++role) {
              if (role == sensorIndex) {
                continue;
              }
              if (memcmp(g_settings.tempCal.addresses[role], parsedAddress, 8) ==
                  0) {
                memset(g_settings.tempCal.addresses[role], 0,
                       sizeof(g_settings.tempCal.addresses[role]));
              }
            }
            memcpy(g_settings.tempCal.addresses[sensorIndex], parsedAddress,
                   sizeof(parsedAddress));
          }

          Sensors::applyCalibration(g_settings.tempCal);
          Sensors::refreshTemperatureInventory();
          NVSManager::saveSettings(g_settings);

          JsonDocument resp;
          resp["status"] = "ok";
          resp["method"] = "address";
          resp["index"] = sensorIndex;
          resp["name"] = getTempSensorLabel(sensorIndex);
          resp["address"] = addressValue;

          String json;
          serializeJson(resp, json);
          request->send(200, "application/json", json);
          return;
        }

        if (!doc["offset"].isNull()) {
          g_settings.tempCal.offsets[sensorIndex] = doc["offset"].as<float>();

          Sensors::applyCalibration(g_settings.tempCal);
          NVSManager::saveSettings(g_settings);

          LOG_I("Temp[%d] calibrated: offset = %.2f°C", sensorIndex,
                g_settings.tempCal.offsets[sensorIndex]);

          request->send(200, "application/json",
                        "{\"status\":\"ok\",\"method\":\"offset\"}");
          return;
        }

        if (!doc["reference"].isNull()) {
          float reference = doc["reference"].as<float>();

          float currentTemp = 0;
          switch (sensorIndex) {
          case TEMP_CUBE:
            currentTemp = g_state.temps.cube;
            break;
          case TEMP_COLUMN_BOTTOM:
            currentTemp = g_state.temps.columnBottom;
            break;
          case TEMP_COLUMN_TOP:
            currentTemp = g_state.temps.columnTop;
            break;
          case TEMP_REFLUX:
            currentTemp = g_state.temps.reflux;
            break;
          case TEMP_TSA:
            currentTemp = g_state.temps.tsa;
            break;
          case TEMP_WATER_IN:
            currentTemp = g_state.temps.waterIn;
            break;
          case TEMP_WATER_OUT:
            currentTemp = g_state.temps.waterOut;
            break;
          }

          float rawTemp = currentTemp - g_settings.tempCal.offsets[sensorIndex];
          g_settings.tempCal.offsets[sensorIndex] = reference - rawTemp;

          Sensors::applyCalibration(g_settings.tempCal);
          NVSManager::saveSettings(g_settings);

          LOG_I("Temp[%d] calibrated to %.2f°C: offset = %.2f°C", sensorIndex,
                reference, g_settings.tempCal.offsets[sensorIndex]);

          JsonDocument resp;
          resp["status"] = "ok";
          resp["method"] = "reference";
          resp["offset"] = g_settings.tempCal.offsets[sensorIndex];

          String json;
          serializeJson(resp, json);
          request->send(200, "application/json", json);
          return;
        }

        request->send(400, "application/json",
                      "{\"error\":\"Missing parameters\"}");
      });

  server.on(
      "/api/calibration/pressure", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid JSON\"}");
          return;
        }

        const bool hasVoltagePoints = !doc["voltagePoints"].isNull();
        const bool hasPressurePoints = !doc["pressurePoints"].isNull();
        if (hasVoltagePoints != hasPressurePoints) {
          request->send(
              400, "application/json",
              "{\"error\":\"voltagePoints and pressurePoints must be provided together\"}");
          return;
        }

        const bool hasZeroOffset = !doc["zeroOffsetMmHg"].isNull();
        if (!hasVoltagePoints && !hasZeroOffset) {
          request->send(400, "application/json",
                        "{\"error\":\"Missing parameters\"}");
          return;
        }

        if (hasVoltagePoints && hasPressurePoints) {
          JsonArray voltageArray = doc["voltagePoints"].as<JsonArray>();
          JsonArray pressureArray = doc["pressurePoints"].as<JsonArray>();
          if (voltageArray.size() != pressureArray.size() ||
              voltageArray.size() > 5) {
            request->send(
                400, "application/json",
                "{\"error\":\"Invalid point count (max 5, must match)\"}");
            return;
          }
          if (voltageArray.size() == 1) {
            request->send(
                400, "application/json",
                "{\"error\":\"At least 2 points are required for active calibration\"}");
            return;
          }

          g_settings.pressureCal.pointCount = 0;
          memset(g_settings.pressureCal.voltagePoints, 0,
                 sizeof(g_settings.pressureCal.voltagePoints));
          memset(g_settings.pressureCal.pressurePoints, 0,
                 sizeof(g_settings.pressureCal.pressurePoints));

          struct PressurePoint {
            float voltage;
            float pressure;
          };
          PressurePoint points[5];
          for (size_t i = 0; i < voltageArray.size(); ++i) {
            const float voltage =
                clampFloatRange(voltageArray[i].as<float>(), 0.0f, 4.096f);
            const float pressure =
                clampFloatRange(pressureArray[i].as<float>(), 0.0f, 75.0f);
            points[i] = {voltage, pressure};
          }

          for (size_t i = 0; i < voltageArray.size(); ++i) {
            for (size_t j = i + 1; j < voltageArray.size(); ++j) {
              if (points[j].voltage < points[i].voltage) {
                PressurePoint tmp = points[i];
                points[i] = points[j];
                points[j] = tmp;
              }
            }
          }

          bool hasDuplicateVoltage = false;
          for (size_t i = 1; i < voltageArray.size(); ++i) {
            if (fabsf(points[i].voltage - points[i - 1].voltage) < 0.0001f) {
              hasDuplicateVoltage = true;
              break;
            }
          }
          if (hasDuplicateVoltage) {
            request->send(400, "application/json",
                          "{\"error\":\"Voltage points must be unique\"}");
            return;
          }

          g_settings.pressureCal.pointCount =
              static_cast<uint8_t>(voltageArray.size());
          for (uint8_t i = 0; i < g_settings.pressureCal.pointCount; ++i) {
            g_settings.pressureCal.voltagePoints[i] = points[i].voltage;
            g_settings.pressureCal.pressurePoints[i] = points[i].pressure;
          }
        }

        if (hasZeroOffset) {
          g_settings.pressureCal.zeroOffsetMmHg = clampFloatRange(
              doc["zeroOffsetMmHg"].as<float>(), -75.0f, 75.0f);
        }

        NVSManager::saveSettings(g_settings);

        JsonDocument resp;
        resp["status"] = "ok";
        resp["pointCount"] = g_settings.pressureCal.pointCount;
        resp["calibrated"] = g_settings.pressureCal.pointCount >= 2;
        resp["zeroOffsetMmHg"] = g_settings.pressureCal.zeroOffsetMmHg;
        JsonArray respVoltages = resp["voltagePoints"].to<JsonArray>();
        JsonArray respPressures = resp["pressurePoints"].to<JsonArray>();
        for (uint8_t i = 0; i < g_settings.pressureCal.pointCount; ++i) {
          respVoltages.add(g_settings.pressureCal.voltagePoints[i]);
          respPressures.add(g_settings.pressureCal.pressurePoints[i]);
        }

        String json;
        serializeJson(resp, json);
        request->send(200, "application/json", json);
      });

  server.on(
      "/api/calibration/hydrometer", HTTP_POST,
      [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid JSON\"}");
          return;
        }

        if (!doc["densityOffset"].isNull()) {
          g_settings.hydroCal.densityOffset = clampFloatRange(
              doc["densityOffset"].as<float>(), -0.250f, 0.250f);
        }

        const bool hasAbvPoints = !doc["abvPoints"].isNull();
        const bool hasPressurePoints = !doc["pressurePoints"].isNull();
        if (hasAbvPoints != hasPressurePoints) {
          request->send(
              400, "application/json",
              "{\"error\":\"abvPoints and pressurePoints must be provided together\"}");
          return;
        }

        if (hasAbvPoints && hasPressurePoints) {
          JsonArray abvArray = doc["abvPoints"].as<JsonArray>();
          JsonArray pressureArray = doc["pressurePoints"].as<JsonArray>();

          if (abvArray.size() != pressureArray.size() || abvArray.size() > 10) {
            request->send(
                400, "application/json",
                "{\"error\":\"Invalid point count (max 10, must match)\"}");
            return;
          }
          if (abvArray.size() == 1) {
            request->send(
                400, "application/json",
                "{\"error\":\"At least 2 points are required for active calibration\"}");
            return;
          }

          g_settings.hydroCal.pointCount = 0;
          memset(g_settings.hydroCal.abvPoints, 0,
                 sizeof(g_settings.hydroCal.abvPoints));
          memset(g_settings.hydroCal.pressurePoints, 0,
                 sizeof(g_settings.hydroCal.pressurePoints));

          struct HydroPoint {
            float pressure;
            float abv;
          };
          HydroPoint points[10];
          for (size_t i = 0; i < abvArray.size(); ++i) {
            const float abv =
                clampFloatRange(abvArray[i].as<float>(), 0.0f, 100.0f);
            const float pressure =
                clampFloatRange(pressureArray[i].as<float>(), 0.500f, 1.200f);
            points[i] = {pressure, abv};
          }

          for (size_t i = 0; i < abvArray.size(); ++i) {
            for (size_t j = i + 1; j < abvArray.size(); ++j) {
              if (points[j].pressure < points[i].pressure) {
                HydroPoint tmp = points[i];
                points[i] = points[j];
                points[j] = tmp;
              }
            }
          }

          bool hasDuplicatePressure = false;
          for (size_t i = 1; i < abvArray.size(); ++i) {
            if (fabsf(points[i].pressure - points[i - 1].pressure) < 0.0001f) {
              hasDuplicatePressure = true;
              break;
            }
          }
          if (hasDuplicatePressure) {
            request->send(400, "application/json",
                          "{\"error\":\"Pressure points must be unique\"}");
            return;
          }

          g_settings.hydroCal.pointCount = static_cast<uint8_t>(abvArray.size());
          for (uint8_t i = 0; i < g_settings.hydroCal.pointCount; ++i) {
            g_settings.hydroCal.abvPoints[i] = points[i].abv;
            g_settings.hydroCal.pressurePoints[i] = points[i].pressure;
          }
        }

        NVSManager::saveSettings(g_settings);

        JsonDocument resp;
        resp["status"] = "ok";
        resp["densityOffset"] = g_settings.hydroCal.densityOffset;
        resp["pointCount"] = g_settings.hydroCal.pointCount;
        JsonArray respAbv = resp["abvPoints"].to<JsonArray>();
        JsonArray respPressure = resp["pressurePoints"].to<JsonArray>();
        for (uint8_t i = 0; i < g_settings.hydroCal.pointCount; ++i) {
          respAbv.add(g_settings.hydroCal.abvPoints[i]);
          respPressure.add(g_settings.hydroCal.pressurePoints[i]);
        }

        String json;
        serializeJson(resp, json);
        request->send(200, "application/json", json);
      });

  server.on("/api/calibration/scan", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              uint8_t addresses[TEMP_COUNT][8] = {};
              uint8_t count = Sensors::scanDS18B20(addresses);
              const uint8_t searchCount = count;

              for (uint8_t role = 0; role < TEMP_COUNT && count < TEMP_COUNT;
                   ++role) {
                uint8_t detectedAddress[8] = {0};
                if (!Sensors::getDiscoveredTempAddress(role, detectedAddress)) {
                  continue;
                }

                bool alreadyListed = false;
                for (uint8_t i = 0; i < count; ++i) {
                  if (memcmp(addresses[i], detectedAddress, 8) == 0) {
                    alreadyListed = true;
                    break;
                  }
                }
                if (alreadyListed) {
                  continue;
                }

                memcpy(addresses[count], detectedAddress, 8);
                count++;
              }

              uint8_t runtimeCount = 0;
              for (uint8_t role = 0; role < TEMP_COUNT; ++role) {
                uint8_t detectedAddress[8] = {0};
                if (Sensors::getDiscoveredTempAddress(role, detectedAddress)) {
                  runtimeCount++;
                }
              }

              JsonDocument doc;
              doc["count"] = count;
              doc["searchCount"] = searchCount;
              doc["runtimeCount"] = runtimeCount;
              doc["source"] = Sensors::getTemperatureBusSourceKey();
              doc["sourceLabel"] = Sensors::getTemperatureBusSourceLabel();

              JsonArray sensors = doc["sensors"].to<JsonArray>();
              for (uint8_t i = 0; i < count; i++) {
                JsonObject s = sensors.add<JsonObject>();

                char addrStr[24];
                snprintf(addrStr, sizeof(addrStr),
                         "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                         addresses[i][0], addresses[i][1], addresses[i][2],
                         addresses[i][3], addresses[i][4], addresses[i][5],
                         addresses[i][6], addresses[i][7]);

                s["index"] = i;
                s["address"] = addrStr;

                int8_t mappedRole = -1;
                for (uint8_t role = 0; role < TEMP_COUNT; ++role) {
                  if (memcmp(g_settings.tempCal.addresses[role], addresses[i], 8) ==
                      0) {
                    mappedRole = static_cast<int8_t>(role);
                    break;
                  }
                }
                s["mappedRole"] = mappedRole;
                if (mappedRole >= 0) {
                  s["mappedRoleName"] =
                      getTempSensorLabel(static_cast<uint8_t>(mappedRole));
                }
                s["valid"] = mappedRole >= 0
                                 ? Sensors::isTempSensorValid(
                                       static_cast<uint8_t>(mappedRole))
                                 : false;
              }

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });

  server.on("/api/calibration/scan/raw", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              uint8_t addresses[TEMP_COUNT][8] = {};
              uint8_t count = Sensors::scanDS18B20(addresses);
              const uint8_t searchCount = count;
              const uint8_t presenceAttempts = 5;
              const uint8_t presenceDetections =
                  Sensors::sampleDs18b20Presence(presenceAttempts);

              JsonDocument doc;
              doc["count"] = count;
              doc["searchCount"] = searchCount;
              doc["presenceAttempts"] = presenceAttempts;
              doc["presenceDetections"] = presenceDetections;
              doc["success"] = true;
              doc["bus"] = "1-wire";
              doc["source"] = Sensors::getTemperatureBusSourceKey();
              doc["sourceLabel"] = Sensors::getTemperatureBusSourceLabel();
              doc["ds2482Available"] = Sensors::isDs2482Available();
              doc["ds2482Address"] = Sensors::getDs2482Address();

              JsonArray searchSensors = doc["searchSensors"].to<JsonArray>();
              for (uint8_t i = 0; i < searchCount; ++i) {
                JsonObject sensor = searchSensors.add<JsonObject>();

                char addrStr[24];
                snprintf(addrStr, sizeof(addrStr),
                         "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                         addresses[i][0], addresses[i][1], addresses[i][2],
                         addresses[i][3], addresses[i][4], addresses[i][5],
                         addresses[i][6], addresses[i][7]);

                sensor["index"] = i;
                sensor["address"] = addrStr;
                sensor["family"] = addresses[i][0];
                sensor["crc"] = addresses[i][7];
              }

              JsonArray runtimeSensors = doc["runtimeSensors"].to<JsonArray>();
              uint8_t runtimeCount = 0;
              for (uint8_t role = 0; role < TEMP_COUNT; ++role) {
                uint8_t detectedAddress[8] = {0};
                if (!Sensors::getDiscoveredTempAddress(role, detectedAddress)) {
                  continue;
                }

                bool alreadyListed = false;
                for (uint8_t i = 0; i < count; ++i) {
                  if (memcmp(addresses[i], detectedAddress, 8) == 0) {
                    alreadyListed = true;
                    break;
                  }
                }
                if (!alreadyListed && count < TEMP_COUNT) {
                  memcpy(addresses[count], detectedAddress, 8);
                  count++;
                }

                JsonObject sensor = runtimeSensors.add<JsonObject>();
                char addrStr[24];
                snprintf(addrStr, sizeof(addrStr),
                         "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                         detectedAddress[0], detectedAddress[1],
                         detectedAddress[2], detectedAddress[3],
                         detectedAddress[4], detectedAddress[5],
                         detectedAddress[6], detectedAddress[7]);
                sensor["role"] = role;
                sensor["roleName"] = getTempSensorLabel(role);
                sensor["address"] = addrStr;
                runtimeCount++;
              }

              JsonArray probeSensors = doc["probeSensors"].to<JsonArray>();
              uint8_t probeCount = 0;
              for (uint8_t role = 0; role < TEMP_COUNT; ++role) {
                if (isZeroTempAddressLocal(g_settings.tempCal.addresses[role])) {
                  continue;
                }

                float probeTemp = -127.0f;
                const bool probeOk = Sensors::probeTempAddress(
                    g_settings.tempCal.addresses[role], &probeTemp);

                JsonObject sensor = probeSensors.add<JsonObject>();
                char addrStr[24];
                snprintf(addrStr, sizeof(addrStr),
                         "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                         g_settings.tempCal.addresses[role][0],
                         g_settings.tempCal.addresses[role][1],
                         g_settings.tempCal.addresses[role][2],
                         g_settings.tempCal.addresses[role][3],
                         g_settings.tempCal.addresses[role][4],
                         g_settings.tempCal.addresses[role][5],
                         g_settings.tempCal.addresses[role][6],
                         g_settings.tempCal.addresses[role][7]);
                sensor["role"] = role;
                sensor["roleName"] = getTempSensorLabel(role);
                sensor["address"] = addrStr;
                sensor["ok"] = probeOk;
                if (probeOk) {
                  sensor["temperature"] = probeTemp;
                  probeCount++;
                }
              }

              doc["runtimeCount"] = runtimeCount;
              doc["probeCount"] = probeCount;
              doc["count"] = count;

              JsonArray sensors = doc["sensors"].to<JsonArray>();
              for (uint8_t i = 0; i < count; ++i) {
                JsonObject sensor = sensors.add<JsonObject>();
                char addrStr[24];
                snprintf(addrStr, sizeof(addrStr),
                         "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                         addresses[i][0], addresses[i][1], addresses[i][2],
                         addresses[i][3], addresses[i][4], addresses[i][5],
                         addresses[i][6], addresses[i][7]);
                sensor["index"] = i;
                sensor["address"] = addrStr;
                sensor["family"] = addresses[i][0];
                sensor["crc"] = addresses[i][7];
              }

              String json;
              serializeJson(doc, json);
              request->send(200, "application/json", json);
            });
}
