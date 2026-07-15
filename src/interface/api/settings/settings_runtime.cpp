#include "settings_modules.h"

#include "config.h"
#include "control/fsm.h"
#include "control/rect_takeoff.h"
#include "control/watt_control.h"
#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/valves.h"
#include "interface/webserver_shared.h"

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
        if (g_state.mode == Mode::MANUAL_RECT) {
          FSM::ManualRect::setTakeoffRateMlH(speed);
          char resp[128];
          snprintf(resp, sizeof(resp),
                   "{\"success\":true,\"manualRect\":true,\"speed\":%.1f}",
                   speed > 0.0f ? speed : 0.0f);
          request->send(200, "application/json", resp);
          return;
        }

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
          sendJsonSuccess(request);
          return;
        }

        if (!doc["water"].isNull()) {
          Valves::setWater(doc["water"].as<bool>());
        }
        if (!doc["heads"].isNull()) {
          Valves::setHeads(doc["heads"].as<bool>());
        }
        if (!doc["body"].isNull()) {
          Valves::setBody(doc["body"].as<bool>());
        }
        if (!doc["tails"].isNull()) {
          Valves::setTails(doc["tails"].as<bool>());
        }
        if (!doc["uno"].isNull()) {
          Valves::setUno(doc["uno"].as<bool>());
        }
        if (!doc["startStopDuty"].isNull()) {
          const uint8_t duty =
              clampU8Range(doc["startStopDuty"].as<uint32_t>(), 0, 255);
          Valves::setStartStop(duty);
        }

        sendJsonSuccess(request);
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
          sendJsonError(request, 400, "Not in MANUAL_RECT mode", false);
          return;
        }

        if (doc["phase"].isNull()) {
          sendJsonError(request, 400, "Missing phase", false);
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
          sendJsonError(request, 400, "At least one field is required", false);
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
}
