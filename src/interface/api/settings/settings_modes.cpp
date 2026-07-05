#include "settings_modules.h"

#include "interface/webserver_shared.h"
#include "storage/nvs_manager.h"

namespace {

void getRectFeedstockDefaults(uint8_t feedstock, float &headsPct,
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

void normalizeRectFractions(RectParams &params) {
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

} // namespace

void registerModeSettingsRoutes(AsyncWebServer &server) {
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
}
