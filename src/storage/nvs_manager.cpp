/**
 * Smart-Column S3 - Менеджер NVS (Non-Volatile Storage)
 *
 * Сохранение и загрузка настроек во флеш-память
 */

#include "nvs_manager.h"
#include <Preferences.h>
#include <ArduinoJson.h>

#include "../interface/wifi_profiles.h"

static Preferences prefs;

namespace {

void clearWiFiProfiles(WiFiSettings& wifi) {
    wifi.profileCount = 0;
    for (uint8_t i = 0; i < WIFI_MAX_PROFILES; ++i) {
        memset(&wifi.profiles[i], 0, sizeof(WiFiProfile));
        wifi.profiles[i].enabled = true;
        strlcpy(wifi.profiles[i].subnet, "255.255.255.0", sizeof(wifi.profiles[i].subnet));
    }
}

void loadWiFiProfilesFromNvs(WiFiSettings& wifi) {
    clearWiFiProfiles(wifi);

    String json = prefs.getString(NVS_KEY_WIFI_PROFILES, "");
    if (json.isEmpty()) {
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    if (error) {
        LOG_W("NVS: WiFi profiles JSON parse failed: %s", error.c_str());
        return;
    }

    JsonArray profiles = doc["profiles"].is<JsonArray>() ? doc["profiles"].as<JsonArray>() : doc.as<JsonArray>();
    if (profiles.isNull()) {
        LOG_W("NVS: WiFi profiles JSON has no array");
        return;
    }

    uint8_t index = 0;
    for (JsonObject profile : profiles) {
        if (index >= WIFI_MAX_PROFILES) break;

        WiFiProfile& target = wifi.profiles[index];
        target.enabled = profile["enabled"] | true;
        strlcpy(target.ssid, profile["ssid"] | "", sizeof(target.ssid));
        strlcpy(target.password, profile["password"] | "", sizeof(target.password));
        target.useStaticIp = profile["useStaticIp"] | false;
        strlcpy(target.ip, profile["ip"] | "", sizeof(target.ip));
        strlcpy(target.gateway, profile["gateway"] | "", sizeof(target.gateway));
        strlcpy(target.subnet, profile["subnet"] | "255.255.255.0", sizeof(target.subnet));
        strlcpy(target.dns1, profile["dns1"] | "", sizeof(target.dns1));
        strlcpy(target.dns2, profile["dns2"] | "", sizeof(target.dns2));

        if (target.ssid[0] != '\0' && target.enabled) {
            ++index;
        }
    }

    wifi.profileCount = index;
    WiFiProfiles::compactProfiles(wifi);
}

void saveWiFiProfilesToNvs(const WiFiSettings& source) {
    WiFiSettings wifi = source;
    WiFiProfiles::compactProfiles(wifi);

    JsonDocument doc;
    JsonArray profiles = doc["profiles"].to<JsonArray>();
    for (uint8_t i = 0; i < wifi.profileCount && i < WIFI_MAX_PROFILES; ++i) {
        const WiFiProfile& profile = wifi.profiles[i];
        JsonObject item = profiles.add<JsonObject>();
        item["enabled"] = profile.enabled;
        item["ssid"] = profile.ssid;
        item["password"] = profile.password;
        item["useStaticIp"] = profile.useStaticIp;
        item["ip"] = profile.ip;
        item["gateway"] = profile.gateway;
        item["subnet"] = profile.subnet;
        item["dns1"] = profile.dns1;
        item["dns2"] = profile.dns2;
    }

    String json;
    serializeJson(doc, json);
    prefs.putString(NVS_KEY_WIFI_PROFILES, json);
}

void clearHydrometerCalibration(HydrometerCalibration& cal) {
    cal.densityOffset = 0.0f;
    cal.pointCount = 0;
    memset(cal.abvPoints, 0, sizeof(cal.abvPoints));
    memset(cal.pressurePoints, 0, sizeof(cal.pressurePoints));
}

void loadHydrometerCalibrationFromNvs(HydrometerCalibration& cal) {
    clearHydrometerCalibration(cal);

    String json = prefs.getString(NVS_KEY_HYDRO_POINTS, "");
    if (json.isEmpty()) {
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    if (error) {
        LOG_W("NVS: Hydrometer calibration JSON parse failed: %s", error.c_str());
        return;
    }

    cal.densityOffset = doc["densityOffset"] | 0.0f;

    JsonArray abvArray = doc["abvPoints"].is<JsonArray>() ? doc["abvPoints"].as<JsonArray>() : JsonArray();
    JsonArray pressureArray = doc["pressurePoints"].is<JsonArray>() ? doc["pressurePoints"].as<JsonArray>() : JsonArray();
    if (abvArray.isNull() || pressureArray.isNull()) {
        return;
    }

    const size_t pointCount = min(abvArray.size(), pressureArray.size());
    cal.pointCount = static_cast<uint8_t>(min(pointCount, static_cast<size_t>(10)));
    for (uint8_t i = 0; i < cal.pointCount; ++i) {
        cal.abvPoints[i] = abvArray[i] | 0.0f;
        cal.pressurePoints[i] = pressureArray[i] | 0.0f;
    }
}

void saveHydrometerCalibrationToNvs(const HydrometerCalibration& cal) {
    JsonDocument doc;
    doc["densityOffset"] = cal.densityOffset;
    doc["pointCount"] = cal.pointCount;

    JsonArray abvArray = doc["abvPoints"].to<JsonArray>();
    JsonArray pressureArray = doc["pressurePoints"].to<JsonArray>();
    for (uint8_t i = 0; i < cal.pointCount && i < 10; ++i) {
        abvArray.add(cal.abvPoints[i]);
        pressureArray.add(cal.pressurePoints[i]);
    }

    String json;
    serializeJson(doc, json);
    prefs.putString(NVS_KEY_HYDRO_POINTS, json);
}

} // namespace

namespace NVSManager {

bool init() {
    LOG_I("NVS: Initializing...");
    LOG_I("NVS: Ready");
    return true;
}

bool loadSettings(Settings& settings) {
    LOG_I("NVS: Loading settings...");

    prefs.begin(NVS_NAMESPACE, true); // Read-only

    // WiFi
    prefs.getString(NVS_KEY_WIFI_SSID, settings.wifi.ssid, sizeof(settings.wifi.ssid));
    prefs.getString(NVS_KEY_WIFI_PASS, settings.wifi.password, sizeof(settings.wifi.password));
    loadWiFiProfilesFromNvs(settings.wifi);
    WiFiProfiles::syncLegacyFields(settings.wifi);

    // MQTT
    settings.mqtt.enabled = prefs.getBool(NVS_KEY_MQTT_ENABLED, settings.mqtt.enabled);
    prefs.getString(NVS_KEY_MQTT_SERVER, settings.mqtt.server, sizeof(settings.mqtt.server));
    settings.mqtt.port = prefs.getUShort(NVS_KEY_MQTT_PORT, 1883);
    prefs.getString(NVS_KEY_MQTT_USERNAME, settings.mqtt.username, sizeof(settings.mqtt.username));
    prefs.getString(NVS_KEY_MQTT_PASSWORD, settings.mqtt.password, sizeof(settings.mqtt.password));
    prefs.getString(NVS_KEY_MQTT_BASE_TOPIC, settings.mqtt.baseTopic, sizeof(settings.mqtt.baseTopic));
    settings.mqtt.publishInterval = prefs.getUInt(NVS_KEY_MQTT_INTERVAL, 10000);
    settings.mqtt.discovery = prefs.getBool(NVS_KEY_MQTT_DISCOVERY, settings.mqtt.discovery);

    // Cloud tunnel
    settings.cloud.enabled = prefs.getBool(NVS_KEY_CLOUD_ENABLED, false);
    prefs.getString(NVS_KEY_CLOUD_URL, settings.cloud.tunnelUrl, sizeof(settings.cloud.tunnelUrl));
    prefs.getString(NVS_KEY_CLOUD_TOKEN, settings.cloud.token, sizeof(settings.cloud.token));
    prefs.getString(NVS_KEY_CLOUD_TOKEN_ID, settings.cloud.tokenId, sizeof(settings.cloud.tokenId));

    // Оборудование
    settings.equipment.columnHeightMm = prefs.getUShort(NVS_KEY_COLUMN_HEIGHT, DEFAULT_COLUMN_HEIGHT_MM);
    settings.equipment.heaterPowerW = prefs.getUShort(NVS_KEY_HEATER_POWER, DEFAULT_HEATER_POWER_W);
    settings.equipment.cubeVolumeL = prefs.getFloat(NVS_KEY_CUBE_VOLUME, (float)DEFAULT_CUBE_VOLUME_L);
    settings.equipment.packingCoeff = prefs.getFloat(NVS_KEY_PACKING_COEFF, DEFAULT_PACKING_COEFF);
    settings.equipment.minHeaterSubmergeL = prefs.getFloat(NVS_KEY_MIN_HEATER_SUBMERGE, DEFAULT_MIN_HEATER_SUBMERGE_L);
    settings.equipment.waterAutoStartCubeTempC = prefs.getFloat(NVS_KEY_WATER_AUTOSTART_CUBE_TEMP, DEFAULT_WATER_AUTOSTART_CUBE_TEMP_C);
    settings.fractionator.enabled = prefs.getBool(NVS_KEY_FRACTION_MASTER, settings.fractionator.enabled);
    if (prefs.getBytesLength(NVS_KEY_FRACTION_ANGLES) == sizeof(settings.fractionator.angles)) {
        prefs.getBytes(NVS_KEY_FRACTION_ANGLES, settings.fractionator.angles, sizeof(settings.fractionator.angles));
    }
    if (prefs.getBytesLength(NVS_KEY_FRACTION_ENABLED) == sizeof(settings.fractionator.positionsEnabled)) {
        prefs.getBytes(NVS_KEY_FRACTION_ENABLED, settings.fractionator.positionsEnabled, sizeof(settings.fractionator.positionsEnabled));
    }

    // Дисплей
    settings.displaySettings.enabled = prefs.getBool("disp_en", true);
    settings.displaySettings.brightness = prefs.getUChar("disp_br", 255);
    settings.displaySettings.rotation = prefs.getChar("disp_rot", 1);
    settings.displaySettings.showLogo = prefs.getBool("disp_logo", true);
    settings.displaySettings.refreshProfile = (DisplayRefreshProfile)prefs.getUChar(NVS_KEY_DISPLAY_REFRESH, 0);

    // Калибровка насоса
    settings.pumpCal.mlPerRevolution = prefs.getFloat(NVS_KEY_PUMP_ML_REV, DEFAULT_PUMP_ML_PER_REV);
    loadHydrometerCalibrationFromNvs(settings.hydroCal);

    // Ректификация
    settings.rectParams.feedstock = prefs.getUChar(NVS_KEY_RECT_FEEDSTOCK, 0);
    settings.rectParams.feedVolumeL = prefs.getFloat(NVS_KEY_RECT_FEED_VOL, (float)DEFAULT_CUBE_VOLUME_L);
    settings.rectParams.feedAbvPercent = prefs.getFloat(NVS_KEY_RECT_FEED_ABV, RECT_FEED_ABV_DEFAULT);
    settings.rectParams.headsPercent = prefs.getFloat(NVS_KEY_RECT_HEADS_PCT, RECT_HEADS_PERCENT_DEFAULT);
    settings.rectParams.bodyPercent = prefs.getFloat(NVS_KEY_RECT_BODY_PCT, RECT_BODY_PERCENT_DEFAULT);
    settings.rectParams.tailsPercent = prefs.getFloat(NVS_KEY_RECT_TAILS_PCT, RECT_TAILS_PERCENT_DEFAULT);
    settings.rectParams.headsSpeedMlHKw = prefs.getFloat(NVS_KEY_RECT_HEADS_SPEED, RECT_HEADS_SPEED_ML_H_KW);
    settings.rectParams.bodySpeedMlHKw = prefs.getFloat(NVS_KEY_RECT_BODY_SPEED, RECT_HEADS_SPEED_ML_H_KW * 2);
    settings.rectParams.stabilizationMin = prefs.getUShort(NVS_KEY_RECT_STAB_MIN, RECT_STABILIZATION_TIME_MIN);
    settings.rectParams.purgeMin = prefs.getUShort(NVS_KEY_RECT_PURGE_MIN, RECT_PURGE_TIME_MIN);
    settings.rectParams.baroCorrectionEnabled =
        prefs.getBool(NVS_KEY_RECT_BARO_ENABLED, RECT_BARO_CORRECTION_ENABLED_DEFAULT != 0);

    // Дистилляция
    settings.distillationUi.speedMlH = prefs.getFloat(NVS_KEY_DIST_SPEED, 500.0f);
    settings.distillationUi.headsVolumeMl = prefs.getFloat(NVS_KEY_DIST_HEADS_VOL, 0.0f);
    settings.distillationUi.targetVolumeMl = prefs.getFloat(NVS_KEY_DIST_TARGET_VOL, 3000.0f);
    settings.distillationUi.endTempC = prefs.getFloat(NVS_KEY_DIST_END_TEMP, 96.0f);
    settings.distillationUi.powerPercent = prefs.getFloat(NVS_KEY_DIST_POWER_PCT, 100.0f);
    settings.distillationUi.tailsVolumeMl = prefs.getFloat(NVS_KEY_DIST_TAILS_VOL, 0.0f);

    // Web security
    settings.security.authEnabled = prefs.getBool(NVS_KEY_WEB_AUTH_ENABLED, false);
    settings.security.rateLimitEnabled = prefs.getBool(NVS_KEY_WEB_RATE_LIMIT, true);
    prefs.getString(NVS_KEY_WEB_USERNAME, settings.security.username, sizeof(settings.security.username));
    prefs.getString(NVS_KEY_WEB_PASSWORD, settings.security.password, sizeof(settings.security.password));

    // NBK & Fermentation
    settings.nbk.powerW = prefs.getFloat(NVS_KEY_NBK_POWER, 2500.0f);
    settings.nbk.pumpSpeedMlH = prefs.getFloat(NVS_KEY_NBK_PUMP_SPEED, 20000.0f);
    settings.nbk.columnBottomTempThresholdC = prefs.getFloat(NVS_KEY_NBK_BOTTOM_TEMP, 95.0f);
    settings.fermentation.targetTempC = prefs.getFloat(NVS_KEY_FERM_TARGET_TEMP, 28.0f);
    settings.fermentation.hysteresisC = prefs.getFloat(NVS_KEY_FERM_HYSTERESIS, 0.5f);
    settings.fermentation.useHeater = prefs.getBool(NVS_KEY_FERM_USE_HEATER, true);

    // Мешалка
    settings.stirrer.enabled = prefs.getBool(NVS_KEY_STIRRER_ENABLED, settings.stirrer.enabled);
    settings.stirrer.defaultSpeedPercent =
        prefs.getUChar(NVS_KEY_STIRRER_SPEED, settings.stirrer.defaultSpeedPercent);
    if (settings.stirrer.defaultSpeedPercent == 0 ||
        settings.stirrer.defaultSpeedPercent > 100) {
        settings.stirrer.defaultSpeedPercent = 50;
    }
    settings.stirrer.autoMashing =
        prefs.getBool(NVS_KEY_STIRRER_AUTO_MASH, settings.stirrer.autoMashing);
    settings.stirrer.autoFermentation =
        prefs.getBool(NVS_KEY_STIRRER_AUTO_FERM, settings.stirrer.autoFermentation);
    settings.stirrer.autoNbk =
        prefs.getBool(NVS_KEY_STIRRER_AUTO_NBK, settings.stirrer.autoNbk);

    // Безопасность
    settings.safety.pressureMaxMmHg = prefs.getFloat(NVS_KEY_SAFETY_PRESSURE_MAX, DEFAULT_SAFETY_PRESSURE_MAX_MMHG);
    settings.safety.tsaMaxC = prefs.getFloat(NVS_KEY_SAFETY_TSA_MAX, DEFAULT_SAFETY_TSA_MAX_C);
    settings.safety.waterOutMaxC = prefs.getFloat(NVS_KEY_SAFETY_WATER_OUT_MAX, DEFAULT_SAFETY_WATER_OUT_MAX_C);
    settings.safety.waterOutRiseRateCMin = prefs.getFloat(NVS_KEY_SAFETY_WATER_OUT_RISE_RATE, DEFAULT_SAFETY_WATER_OUT_RISE_RATE_C_MIN);
    settings.safety.pressureRiseRateMmHgMin = prefs.getFloat(NVS_KEY_SAFETY_PRESSURE_RISE_RATE, DEFAULT_SAFETY_PRESSURE_RISE_RATE_MMHG_MIN);

    // Калибровка тача
    settings.touchCal.xMin = prefs.getInt(NVS_KEY_TOUCH_XMIN, TOUCH_CAL_X_MIN);
    settings.touchCal.xMax = prefs.getInt(NVS_KEY_TOUCH_XMAX, TOUCH_CAL_X_MAX);
    settings.touchCal.yMin = prefs.getInt(NVS_KEY_TOUCH_YMIN, TOUCH_CAL_Y_MIN);
    settings.touchCal.yMax = prefs.getInt(NVS_KEY_TOUCH_YMAX, TOUCH_CAL_Y_MAX);
    settings.touchCal.valid = prefs.getBool(NVS_KEY_TOUCH_VALID, false);

    // Прочее
    settings.language = prefs.getUChar(NVS_KEY_LANGUAGE, 0);
    settings.theme = prefs.getUChar(NVS_KEY_THEME, 0);
    settings.soundEnabled = prefs.getBool(NVS_KEY_SOUND, true);
    settings.lastRebootReason = prefs.getUChar(NVS_KEY_LAST_REBOOT_REASON, 0);

    prefs.end();
    LOG_I("NVS: Settings loaded");
    return true;
}

bool saveSettings(const Settings& settings) {
    LOG_I("NVS: Saving settings...");

    prefs.begin(NVS_NAMESPACE, false); // Read-write

    // WiFi
    prefs.putString(NVS_KEY_WIFI_SSID, settings.wifi.ssid);
    prefs.putString(NVS_KEY_WIFI_PASS, settings.wifi.password);
    saveWiFiProfilesToNvs(settings.wifi);

    // MQTT
    prefs.putBool(NVS_KEY_MQTT_ENABLED, settings.mqtt.enabled);
    prefs.putString(NVS_KEY_MQTT_SERVER, settings.mqtt.server);
    prefs.putUShort(NVS_KEY_MQTT_PORT, settings.mqtt.port);
    prefs.putString(NVS_KEY_MQTT_USERNAME, settings.mqtt.username);
    prefs.putString(NVS_KEY_MQTT_PASSWORD, settings.mqtt.password);
    prefs.putString(NVS_KEY_MQTT_BASE_TOPIC, settings.mqtt.baseTopic);
    prefs.putUInt(NVS_KEY_MQTT_INTERVAL, settings.mqtt.publishInterval);
    prefs.putBool(NVS_KEY_MQTT_DISCOVERY, settings.mqtt.discovery);

    // Cloud tunnel
    prefs.putBool(NVS_KEY_CLOUD_ENABLED, settings.cloud.enabled);
    prefs.putString(NVS_KEY_CLOUD_URL, settings.cloud.tunnelUrl);
    prefs.putString(NVS_KEY_CLOUD_TOKEN, settings.cloud.token);
    prefs.putString(NVS_KEY_CLOUD_TOKEN_ID, settings.cloud.tokenId);

    // Оборудование
    prefs.putUShort(NVS_KEY_COLUMN_HEIGHT, settings.equipment.columnHeightMm);
    prefs.putUShort(NVS_KEY_HEATER_POWER, settings.equipment.heaterPowerW);
    prefs.putFloat(NVS_KEY_CUBE_VOLUME, settings.equipment.cubeVolumeL);
    prefs.putFloat(NVS_KEY_PACKING_COEFF, settings.equipment.packingCoeff);
    prefs.putFloat(NVS_KEY_MIN_HEATER_SUBMERGE, settings.equipment.minHeaterSubmergeL);
    prefs.putFloat(NVS_KEY_WATER_AUTOSTART_CUBE_TEMP, settings.equipment.waterAutoStartCubeTempC);
    prefs.putBool(NVS_KEY_FRACTION_MASTER, settings.fractionator.enabled);
    prefs.putBytes(NVS_KEY_FRACTION_ANGLES, settings.fractionator.angles, sizeof(settings.fractionator.angles));
    prefs.putBytes(NVS_KEY_FRACTION_ENABLED, settings.fractionator.positionsEnabled, sizeof(settings.fractionator.positionsEnabled));

    // Дисплей
    prefs.putBool("disp_en", settings.displaySettings.enabled);
    prefs.putUChar("disp_br", settings.displaySettings.brightness);
    prefs.putChar("disp_rot", settings.displaySettings.rotation);
    prefs.putBool("disp_logo", settings.displaySettings.showLogo);
    prefs.putUChar(NVS_KEY_DISPLAY_REFRESH, (uint8_t)settings.displaySettings.refreshProfile);

    // Калибровка насоса
    prefs.putFloat(NVS_KEY_PUMP_ML_REV, settings.pumpCal.mlPerRevolution);
    saveHydrometerCalibrationToNvs(settings.hydroCal);

    // Ректификация
    prefs.putUChar(NVS_KEY_RECT_FEEDSTOCK, settings.rectParams.feedstock);
    prefs.putFloat(NVS_KEY_RECT_FEED_VOL, settings.rectParams.feedVolumeL);
    prefs.putFloat(NVS_KEY_RECT_FEED_ABV, settings.rectParams.feedAbvPercent);
    prefs.putFloat(NVS_KEY_RECT_HEADS_PCT, settings.rectParams.headsPercent);
    prefs.putFloat(NVS_KEY_RECT_BODY_PCT, settings.rectParams.bodyPercent);
    prefs.putFloat(NVS_KEY_RECT_TAILS_PCT, settings.rectParams.tailsPercent);
    prefs.putFloat(NVS_KEY_RECT_HEADS_SPEED, settings.rectParams.headsSpeedMlHKw);
    prefs.putFloat(NVS_KEY_RECT_BODY_SPEED, settings.rectParams.bodySpeedMlHKw);
    prefs.putUShort(NVS_KEY_RECT_STAB_MIN, settings.rectParams.stabilizationMin);
    prefs.putUShort(NVS_KEY_RECT_PURGE_MIN, settings.rectParams.purgeMin);
    prefs.putBool(NVS_KEY_RECT_BARO_ENABLED, settings.rectParams.baroCorrectionEnabled);

    // Дистилляция
    prefs.putFloat(NVS_KEY_DIST_SPEED, settings.distillationUi.speedMlH);
    prefs.putFloat(NVS_KEY_DIST_HEADS_VOL, settings.distillationUi.headsVolumeMl);
    prefs.putFloat(NVS_KEY_DIST_TARGET_VOL, settings.distillationUi.targetVolumeMl);
    prefs.putFloat(NVS_KEY_DIST_END_TEMP, settings.distillationUi.endTempC);
    prefs.putFloat(NVS_KEY_DIST_POWER_PCT, settings.distillationUi.powerPercent);
    prefs.putFloat(NVS_KEY_DIST_TAILS_VOL, settings.distillationUi.tailsVolumeMl);

    // Web security
    prefs.putBool(NVS_KEY_WEB_AUTH_ENABLED, settings.security.authEnabled);
    prefs.putBool(NVS_KEY_WEB_RATE_LIMIT, settings.security.rateLimitEnabled);
    prefs.putString(NVS_KEY_WEB_USERNAME, settings.security.username);
    prefs.putString(NVS_KEY_WEB_PASSWORD, settings.security.password);

    // NBK & Fermentation
    prefs.putFloat(NVS_KEY_NBK_POWER, settings.nbk.powerW);
    prefs.putFloat(NVS_KEY_NBK_PUMP_SPEED, settings.nbk.pumpSpeedMlH);
    prefs.putFloat(NVS_KEY_NBK_BOTTOM_TEMP, settings.nbk.columnBottomTempThresholdC);
    prefs.putFloat(NVS_KEY_FERM_TARGET_TEMP, settings.fermentation.targetTempC);
    prefs.putFloat(NVS_KEY_FERM_HYSTERESIS, settings.fermentation.hysteresisC);
    prefs.putBool(NVS_KEY_FERM_USE_HEATER, settings.fermentation.useHeater);

    // Мешалка
    prefs.putBool(NVS_KEY_STIRRER_ENABLED, settings.stirrer.enabled);
    prefs.putUChar(
        NVS_KEY_STIRRER_SPEED,
        settings.stirrer.defaultSpeedPercent == 0 ? 50
                                                  : settings.stirrer.defaultSpeedPercent);
    prefs.putBool(NVS_KEY_STIRRER_AUTO_MASH, settings.stirrer.autoMashing);
    prefs.putBool(NVS_KEY_STIRRER_AUTO_FERM, settings.stirrer.autoFermentation);
    prefs.putBool(NVS_KEY_STIRRER_AUTO_NBK, settings.stirrer.autoNbk);

    // Безопасность
    prefs.putFloat(NVS_KEY_SAFETY_PRESSURE_MAX, settings.safety.pressureMaxMmHg);
    prefs.putFloat(NVS_KEY_SAFETY_TSA_MAX, settings.safety.tsaMaxC);
    prefs.putFloat(NVS_KEY_SAFETY_WATER_OUT_MAX, settings.safety.waterOutMaxC);
    prefs.putFloat(NVS_KEY_SAFETY_WATER_OUT_RISE_RATE, settings.safety.waterOutRiseRateCMin);
    prefs.putFloat(NVS_KEY_SAFETY_PRESSURE_RISE_RATE, settings.safety.pressureRiseRateMmHgMin);

    // Калибровка тача
    prefs.putInt(NVS_KEY_TOUCH_XMIN, settings.touchCal.xMin);
    prefs.putInt(NVS_KEY_TOUCH_XMAX, settings.touchCal.xMax);
    prefs.putInt(NVS_KEY_TOUCH_YMIN, settings.touchCal.yMin);
    prefs.putInt(NVS_KEY_TOUCH_YMAX, settings.touchCal.yMax);
    prefs.putBool(NVS_KEY_TOUCH_VALID, settings.touchCal.valid);

    // Прочее
    prefs.putUChar(NVS_KEY_LANGUAGE, settings.language);
    prefs.putUChar(NVS_KEY_THEME, settings.theme);
    prefs.putBool(NVS_KEY_SOUND, settings.soundEnabled);
    prefs.putUChar(NVS_KEY_LAST_REBOOT_REASON, settings.lastRebootReason);

    prefs.end();
    LOG_I("NVS: Settings saved");
    return true;
}

void reset() {
    LOG_I("NVS: Resetting to defaults...");
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    LOG_I("NVS: Reset complete");
}

bool loadWiFi(WiFiSettings& wifi) {
    Settings settings{};
    if (!loadSettings(settings)) return false;
    wifi = settings.wifi;
    return true;
}

bool saveWiFi(const WiFiSettings& wifi) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_WIFI_SSID, wifi.ssid);
    prefs.putString(NVS_KEY_WIFI_PASS, wifi.password);
    saveWiFiProfilesToNvs(wifi);
    prefs.end();
    return true;
}

} // namespace NVSManager
