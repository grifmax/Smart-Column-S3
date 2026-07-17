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

bool isZeroTempAddress(const uint8_t address[8]) {
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

bool hasInstalledTempTopology(const TemperatureTopologySettings& topology) {
    return topology.cube || topology.columnBottom || topology.columnTop ||
           topology.reflux || topology.tsa || topology.waterIn ||
           topology.waterOut;
}

void recoverTempTopologyFromCalibration(Settings& settings) {
    if (hasInstalledTempTopology(settings.equipment.temperatureTopology)) {
        return;
    }

    bool recovered = false;
    settings.equipment.temperatureTopology.cube =
        !isZeroTempAddress(settings.tempCal.addresses[TEMP_CUBE]);
    settings.equipment.temperatureTopology.columnBottom =
        !isZeroTempAddress(settings.tempCal.addresses[TEMP_COLUMN_BOTTOM]);
    settings.equipment.temperatureTopology.columnTop =
        !isZeroTempAddress(settings.tempCal.addresses[TEMP_COLUMN_TOP]);
    settings.equipment.temperatureTopology.reflux =
        !isZeroTempAddress(settings.tempCal.addresses[TEMP_REFLUX]);
    settings.equipment.temperatureTopology.tsa =
        !isZeroTempAddress(settings.tempCal.addresses[TEMP_TSA]);
    settings.equipment.temperatureTopology.waterIn =
        !isZeroTempAddress(settings.tempCal.addresses[TEMP_WATER_IN]);
    settings.equipment.temperatureTopology.waterOut =
        !isZeroTempAddress(settings.tempCal.addresses[TEMP_WATER_OUT]);

    recovered = hasInstalledTempTopology(settings.equipment.temperatureTopology);
    if (recovered) {
        LOG_W("NVS: temperatureTopology recovered from mapped DS18B20 roles");
    }
}

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

void clearPressureCalibration(PressureSensorCalibration& cal) {
    cal.pointCount = 0;
    cal.zeroOffsetMmHg = 0.0f;
    memset(cal.voltagePoints, 0, sizeof(cal.voltagePoints));
    memset(cal.pressurePoints, 0, sizeof(cal.pressurePoints));
}

void loadPressureCalibrationFromNvs(PressureSensorCalibration& cal) {
    clearPressureCalibration(cal);

    String json = prefs.getString(NVS_KEY_PRESSURE_POINTS, "");
    if (json.isEmpty()) {
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    if (error) {
        LOG_W("NVS: Pressure calibration JSON parse failed: %s", error.c_str());
        return;
    }

    cal.zeroOffsetMmHg = doc["zeroOffsetMmHg"] | 0.0f;

    JsonArray voltageArray = doc["voltagePoints"].is<JsonArray>() ? doc["voltagePoints"].as<JsonArray>() : JsonArray();
    JsonArray pressureArray = doc["pressurePoints"].is<JsonArray>() ? doc["pressurePoints"].as<JsonArray>() : JsonArray();
    if (voltageArray.isNull() || pressureArray.isNull()) {
        return;
    }

    const size_t pointCount = min(voltageArray.size(), pressureArray.size());
    cal.pointCount = static_cast<uint8_t>(min(pointCount, static_cast<size_t>(5)));
    for (uint8_t i = 0; i < cal.pointCount; ++i) {
        cal.voltagePoints[i] = voltageArray[i] | 0.0f;
        cal.pressurePoints[i] = pressureArray[i] | 0.0f;
    }
}

void savePressureCalibrationToNvs(const PressureSensorCalibration& cal) {
    JsonDocument doc;
    doc["pointCount"] = cal.pointCount;
    doc["zeroOffsetMmHg"] = cal.zeroOffsetMmHg;

    JsonArray voltageArray = doc["voltagePoints"].to<JsonArray>();
    JsonArray pressureArray = doc["pressurePoints"].to<JsonArray>();
    for (uint8_t i = 0; i < cal.pointCount && i < 5; ++i) {
        voltageArray.add(cal.voltagePoints[i]);
        pressureArray.add(cal.pressurePoints[i]);
    }

    String json;
    serializeJson(doc, json);
    prefs.putString(NVS_KEY_PRESSURE_POINTS, json);
}

void migrateSettingsSchema(Settings& settings, uint16_t storedVersion) {
    if (storedVersion >= NVS_SETTINGS_SCHEMA_VERSION) {
        return;
    }

    // Versions before the explicit schema key relied on struct defaults. The
    // fields were already loaded with per-key defaults above; migration only
    // re-applies their safety bounds and never overwrites an existing opt-in.
    if (storedVersion < 3) {
        settings.rectParams.pressureMinPowerPercent =
            constrain(settings.rectParams.pressureMinPowerPercent, 0, 100);
        settings.distillationUi.vaporTempTargetC =
            constrain(settings.distillationUi.vaporTempTargetC, 20.0f, 110.0f);
        settings.distillationUi.vaporTempMinPowerPercent =
            constrain(settings.distillationUi.vaporTempMinPowerPercent, 0, 100);
        settings.distillationUi.vaporTempMaxPowerPercent =
            constrain(settings.distillationUi.vaporTempMaxPowerPercent,
                      settings.distillationUi.vaporTempMinPowerPercent, 100);
    }
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
    const uint16_t storedSchemaVersion =
        prefs.getUShort(NVS_KEY_SETTINGS_SCHEMA, 0);

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
    {
        const uint8_t packingTypeValue = prefs.getUChar(
            NVS_KEY_PACKING_TYPE,
            static_cast<uint8_t>(settings.equipment.packingType));
        settings.equipment.packingType =
            packingTypeValue <= static_cast<uint8_t>(PackingType::CUSTOM)
                ? static_cast<PackingType>(packingTypeValue)
                : PackingType::SPN_3_5;
    }
    settings.equipment.heaterPowerW = prefs.getUShort(NVS_KEY_HEATER_POWER, DEFAULT_HEATER_POWER_W);
    settings.equipment.cubeVolumeL = prefs.getFloat(NVS_KEY_CUBE_VOLUME, (float)DEFAULT_CUBE_VOLUME_L);
    settings.equipment.packingCoeff = prefs.getFloat(NVS_KEY_PACKING_COEFF, DEFAULT_PACKING_COEFF);
    settings.equipment.minHeaterSubmergeL = prefs.getFloat(NVS_KEY_MIN_HEATER_SUBMERGE, DEFAULT_MIN_HEATER_SUBMERGE_L);
    settings.equipment.waterAutoStartCubeTempC = prefs.getFloat(NVS_KEY_WATER_AUTOSTART_CUBE_TEMP, DEFAULT_WATER_AUTOSTART_CUBE_TEMP_C);
    settings.equipment.boosterHeaterEnabled =
        prefs.getBool(NVS_KEY_BOOSTER_HEATER_ENABLED, DEFAULT_BOOSTER_HEATER_ENABLED != 0);
    settings.equipment.boosterHeaterPowerW =
        prefs.getUShort(NVS_KEY_BOOSTER_HEATER_POWER, DEFAULT_BOOSTER_HEATER_POWER_W);
    settings.equipment.boosterHeaterStopCubeTempC =
        prefs.getFloat(NVS_KEY_BOOSTER_STOP_TEMP, DEFAULT_BOOSTER_HEATER_STOP_CUBE_TEMP_C);
    settings.equipment.coolingPwmEnabled =
        prefs.getBool(NVS_KEY_COOLING_PWM_ENABLED, DEFAULT_COOLING_PWM_ENABLED != 0);
    settings.equipment.coolingPwmMinDuty =
        prefs.getUChar(NVS_KEY_COOLING_PWM_MIN_DUTY, DEFAULT_COOLING_PWM_MIN_DUTY);
    settings.equipment.coolingPwmMaxDuty =
        prefs.getUChar(NVS_KEY_COOLING_PWM_MAX_DUTY, DEFAULT_COOLING_PWM_MAX_DUTY);
    settings.equipment.coolingPwmStartupDuty =
        prefs.getUChar(NVS_KEY_COOLING_PWM_STARTUP, DEFAULT_COOLING_PWM_STARTUP_DUTY);
    settings.equipment.useDs2482ForTemps =
        prefs.getBool(NVS_KEY_USE_DS2482_TEMPS, DEFAULT_USE_DS2482_FOR_TEMPS != 0);
    settings.equipment.ds2482Address =
        prefs.getUChar(NVS_KEY_DS2482_ADDRESS, DEFAULT_DS2482_ADDRESS);
    if (settings.equipment.coolingPwmMinDuty > settings.equipment.coolingPwmMaxDuty) {
        settings.equipment.coolingPwmMinDuty = settings.equipment.coolingPwmMaxDuty;
    }
    if (settings.equipment.coolingPwmStartupDuty < settings.equipment.coolingPwmMinDuty) {
        settings.equipment.coolingPwmStartupDuty = settings.equipment.coolingPwmMinDuty;
    }
    if (settings.equipment.coolingPwmStartupDuty > settings.equipment.coolingPwmMaxDuty) {
        settings.equipment.coolingPwmStartupDuty = settings.equipment.coolingPwmMaxDuty;
    }
    settings.equipment.bodyLevelSensorEnabled =
        prefs.getBool(NVS_KEY_BODY_LEVEL_ENABLED, DEFAULT_BODY_LEVEL_SENSOR_ENABLED != 0);
    settings.equipment.bodyLevelThresholdV =
        prefs.getFloat(NVS_KEY_BODY_LEVEL_THRESHOLD, DEFAULT_BODY_LEVEL_THRESHOLD_V);
    settings.equipment.bodyLevelTriggerAbove =
        prefs.getBool(NVS_KEY_BODY_LEVEL_POLARITY, DEFAULT_BODY_LEVEL_TRIGGER_ABOVE != 0);
    settings.equipment.leakSensorEnabled =
        prefs.getBool(NVS_KEY_LEAK_ENABLED, DEFAULT_LEAK_SENSOR_ENABLED != 0);
    settings.equipment.leakThresholdV =
        prefs.getFloat(NVS_KEY_LEAK_THRESHOLD, DEFAULT_LEAK_THRESHOLD_V);
    settings.equipment.leakTriggerAbove =
        prefs.getBool(NVS_KEY_LEAK_POLARITY, DEFAULT_LEAK_TRIGGER_ABOVE != 0);
    const size_t tempTopologyBytes = prefs.getBytesLength(NVS_KEY_TEMP_TOPOLOGY);
    if (tempTopologyBytes == sizeof(settings.equipment.temperatureTopology)) {
        prefs.getBytes(NVS_KEY_TEMP_TOPOLOGY, &settings.equipment.temperatureTopology,
                       sizeof(settings.equipment.temperatureTopology));
    } else {
        // Legacy migration: old installations had no explicit topology key.
        // Keep core process roles enabled, but do not assume cooling-water probes.
        settings.equipment.temperatureTopology.cube = true;
        settings.equipment.temperatureTopology.columnBottom = true;
        settings.equipment.temperatureTopology.columnTop = true;
        settings.equipment.temperatureTopology.reflux = true;
        settings.equipment.temperatureTopology.tsa = true;
        settings.equipment.temperatureTopology.waterIn = false;
        settings.equipment.temperatureTopology.waterOut = false;
    }
    if (settings.equipment.bodyLevelThresholdV < 0.0f) {
        settings.equipment.bodyLevelThresholdV = 0.0f;
    }
    if (settings.equipment.bodyLevelThresholdV > 4.096f) {
        settings.equipment.bodyLevelThresholdV = 4.096f;
    }
    if (settings.equipment.leakThresholdV < 0.0f) {
        settings.equipment.leakThresholdV = 0.0f;
    }
    if (settings.equipment.leakThresholdV > 4.096f) {
        settings.equipment.leakThresholdV = 4.096f;
    }

    memset(&settings.tempCal, 0, sizeof(settings.tempCal));
    const size_t tempCalBytes = prefs.getBytesLength(NVS_KEY_TEMP_OFFSETS);
    if (tempCalBytes == sizeof(settings.tempCal)) {
        prefs.getBytes(NVS_KEY_TEMP_OFFSETS, &settings.tempCal, sizeof(settings.tempCal));
    } else if (tempCalBytes == sizeof(settings.tempCal.offsets)) {
        prefs.getBytes(NVS_KEY_TEMP_OFFSETS, settings.tempCal.offsets, sizeof(settings.tempCal.offsets));
    }
    recoverTempTopologyFromCalibration(settings);

    settings.fractionator.enabled = prefs.getBool(NVS_KEY_FRACTION_MASTER, settings.fractionator.enabled);
    if (prefs.getBytesLength(NVS_KEY_FRACTION_ANGLES) == sizeof(settings.fractionator.angles)) {
        prefs.getBytes(NVS_KEY_FRACTION_ANGLES, settings.fractionator.angles, sizeof(settings.fractionator.angles));
    }
    if (prefs.getBytesLength(NVS_KEY_FRACTION_ENABLED) == sizeof(settings.fractionator.positionsEnabled)) {
        prefs.getBytes(NVS_KEY_FRACTION_ENABLED, settings.fractionator.positionsEnabled, sizeof(settings.fractionator.positionsEnabled));
    }
    const size_t fractionProgramBytes = prefs.getBytesLength(NVS_KEY_FRACTION_PROGRAM);
    if (fractionProgramBytes == sizeof(settings.fractionProgram)) {
        prefs.getBytes(NVS_KEY_FRACTION_PROGRAM, &settings.fractionProgram,
                       sizeof(settings.fractionProgram));
    } else {
        settings.fractionProgram = FractionProgram{};
    }
    if (settings.fractionProgram.schemaVersion != 2 ||
        settings.fractionProgram.stepCount > FRACTION_PROGRAM_MAX_STEPS) {
        settings.fractionProgram = FractionProgram{};
    } else {
        for (uint8_t i = 0; i < settings.fractionProgram.stepCount; ++i) {
            FractionProgramStep& step = settings.fractionProgram.steps[i];
            if (step.routeIndex >= FRACTION_COUNT) step.routeIndex = 0;
            step.endConditions &= FRACTION_PROGRAM_END_VOLUME |
                                  FRACTION_PROGRAM_END_TIME |
                                  FRACTION_PROGRAM_END_TEMPERATURE |
                                  FRACTION_PROGRAM_END_LEVEL;
            if (step.pumpRateMlH < 0.0f) step.pumpRateMlH = 0.0f;
            if (step.endVolumeMl < 0.0f) step.endVolumeMl = 0.0f;
            step.name[sizeof(step.name) - 1] = 0;
        }
    }

    // Дисплей
    settings.displaySettings.enabled = prefs.getBool("disp_en", true);
    settings.displaySettings.brightness = prefs.getUChar("disp_br", 255);
    settings.displaySettings.rotation = prefs.getChar("disp_rot", 1);
    settings.displaySettings.showLogo = prefs.getBool("disp_logo", true);
    settings.displaySettings.refreshProfile = (DisplayRefreshProfile)prefs.getUChar(NVS_KEY_DISPLAY_REFRESH, 0);

    // Калибровка насоса
    settings.pumpCal.mlPerRevolution = prefs.getFloat(NVS_KEY_PUMP_ML_REV, DEFAULT_PUMP_ML_PER_REV);
    loadPressureCalibrationFromNvs(settings.pressureCal);
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
    settings.rectParams.takeoffBackendType = static_cast<RectTakeoffBackendType>(
        prefs.getUChar(NVS_KEY_RECT_TAKEOFF_BACKEND, RECT_TAKEOFF_BACKEND_DEFAULT));
    settings.rectParams.refluxMode = static_cast<RectRefluxMode>(
        prefs.getUChar(NVS_KEY_RECT_REFLUX_MODE, RECT_REFLUX_MODE_DEFAULT));
    settings.rectParams.srRatio =
        prefs.getFloat(NVS_KEY_RECT_SR_RATIO, RECT_SR_RATIO_DEFAULT);
    settings.rectParams.autonomousCycleSec =
        prefs.getUShort(NVS_KEY_RECT_AUTO_CYCLE, RECT_AUTO_CYCLE_SEC_DEFAULT);
    settings.rectParams.autonomousPauseSec =
        prefs.getUShort(NVS_KEY_RECT_AUTO_PAUSE, RECT_AUTO_PAUSE_SEC_DEFAULT);
    settings.rectParams.chimAutoPercent =
        prefs.getFloat(NVS_KEY_RECT_CHIM_AUTO, RECT_CHIM_AUTO_PERCENT_DEFAULT);
    settings.rectParams.chimTimePerH =
        prefs.getFloat(NVS_KEY_RECT_CHIM_TIME, RECT_CHIM_TIME_PER_H_DEFAULT);
    settings.rectParams.chimBegPercent =
        prefs.getFloat(NVS_KEY_RECT_CHIM_BEG, RECT_CHIM_BEG_PERCENT_DEFAULT);
    settings.rectParams.chimMinPercent =
        prefs.getFloat(NVS_KEY_RECT_CHIM_MIN, RECT_CHIM_MIN_PERCENT_DEFAULT);
    settings.rectParams.usePbMode =
        prefs.getUChar(NVS_KEY_RECT_USE_PB, RECT_USE_PB_MODE_DEFAULT);
    settings.rectParams.timpPbMs =
        prefs.getUInt(NVS_KEY_RECT_TIMP_PB, RECT_TIMP_PB_MS_DEFAULT);
    settings.rectParams.routingSettlingMs =
        prefs.getUShort(NVS_KEY_RECT_ROUTE_SETTLE,
                        RECT_ROUTING_SETTLING_MS_DEFAULT);
    settings.rectParams.routingRetargetMinMs =
        prefs.getUShort(NVS_KEY_RECT_ROUTE_RETARGET,
                        RECT_ROUTING_RETARGET_MIN_MS_DEFAULT);
    settings.rectParams.bodyContainerCount =
        prefs.getUChar(NVS_KEY_RECT_BODY_CONTAINERS, 1);
    settings.rectParams.pressureControlEnabled =
        prefs.getBool(NVS_KEY_RECT_PRESSURE_CTRL, false);
    settings.rectParams.pressureMinPowerPercent =
        prefs.getUChar(NVS_KEY_RECT_PRESSURE_MIN_PWR, 30);
    settings.rectParams.valvePulsePeriodMs =
        prefs.getUShort(NVS_KEY_RECT_VALVE_PULSE_PERIOD,
                        RECT_VALVE_PULSE_PERIOD_MS_DEFAULT);
    settings.rectParams.valvePulseMinOpenMs =
        prefs.getUShort(NVS_KEY_RECT_VALVE_PULSE_MIN_OPEN,
                        RECT_VALVE_PULSE_MIN_OPEN_MS_DEFAULT);
    settings.rectParams.valvePulseMaxOpenMs =
        prefs.getUShort(NVS_KEY_RECT_VALVE_PULSE_MAX_OPEN,
                        RECT_VALVE_PULSE_MAX_OPEN_MS_DEFAULT);
    if (prefs.getBytesLength(NVS_KEY_RECT_PHASE_POWER) ==
        sizeof(settings.rectParams.phasePowerPercent)) {
        prefs.getBytes(NVS_KEY_RECT_PHASE_POWER,
                       settings.rectParams.phasePowerPercent,
                       sizeof(settings.rectParams.phasePowerPercent));
    }
    if (static_cast<uint8_t>(settings.rectParams.takeoffBackendType) >
        static_cast<uint8_t>(RectTakeoffBackendType::VALVE_SINGLE_SWITCHED)) {
        settings.rectParams.takeoffBackendType = RectTakeoffBackendType::PUMP;
    }
    if (static_cast<uint8_t>(settings.rectParams.refluxMode) >
        static_cast<uint8_t>(RectRefluxMode::AUTONOMOUS)) {
        settings.rectParams.refluxMode = RectRefluxMode::ML_H;
    }
    settings.rectParams.srRatio =
        constrain(settings.rectParams.srRatio, 0.0f, 20.0f);
    settings.rectParams.autonomousCycleSec =
        settings.rectParams.autonomousCycleSec > 0
            ? settings.rectParams.autonomousCycleSec
            : static_cast<uint16_t>(1);
    if (settings.rectParams.autonomousPauseSec >=
        settings.rectParams.autonomousCycleSec) {
        settings.rectParams.autonomousPauseSec =
            settings.rectParams.autonomousCycleSec - 1;
    }
    settings.rectParams.chimAutoPercent =
        constrain(settings.rectParams.chimAutoPercent, 0.0f, 200.0f);
    settings.rectParams.chimTimePerH =
        constrain(settings.rectParams.chimTimePerH, -2000.0f, 2000.0f);
    settings.rectParams.chimBegPercent =
        constrain(settings.rectParams.chimBegPercent, -100.0f, 200.0f);
    settings.rectParams.chimMinPercent =
        constrain(settings.rectParams.chimMinPercent, 0.0f, 100.0f);
    settings.rectParams.routingSettlingMs =
        constrain(settings.rectParams.routingSettlingMs, 0, 10000);
    settings.rectParams.routingRetargetMinMs =
        constrain(settings.rectParams.routingRetargetMinMs, 0, 30000);
    settings.rectParams.bodyContainerCount =
        constrain(settings.rectParams.bodyContainerCount, 1, 8);
    settings.rectParams.pressureMinPowerPercent =
        constrain(settings.rectParams.pressureMinPowerPercent, 0, 100);
    settings.rectParams.valvePulsePeriodMs =
        constrain(settings.rectParams.valvePulsePeriodMs, 100, 5000);
    settings.rectParams.valvePulseMinOpenMs =
        constrain(settings.rectParams.valvePulseMinOpenMs, 0,
                  settings.rectParams.valvePulsePeriodMs);
    settings.rectParams.valvePulseMaxOpenMs =
        constrain(settings.rectParams.valvePulseMaxOpenMs,
                  settings.rectParams.valvePulseMinOpenMs,
                  settings.rectParams.valvePulsePeriodMs);
    if (settings.rectParams.phasePowerPercent[RECT_POWER_STABILIZATION] == 0) {
        settings.rectParams.phasePowerPercent[RECT_POWER_STABILIZATION] =
            RECT_PHASE_POWER_STAB_DEFAULT;
    }
    if (settings.rectParams.phasePowerPercent[RECT_POWER_HEADS] == 0) {
        settings.rectParams.phasePowerPercent[RECT_POWER_HEADS] =
            RECT_PHASE_POWER_HEADS_DEFAULT;
    }
    if (settings.rectParams.phasePowerPercent[RECT_POWER_BODY] == 0) {
        settings.rectParams.phasePowerPercent[RECT_POWER_BODY] =
            RECT_PHASE_POWER_BODY_DEFAULT;
    }
    if (settings.rectParams.phasePowerPercent[RECT_POWER_TAILS] == 0) {
        settings.rectParams.phasePowerPercent[RECT_POWER_TAILS] =
            RECT_PHASE_POWER_TAILS_DEFAULT;
    }

    // Дистилляция
    settings.distillationUi.speedMlH = prefs.getFloat(NVS_KEY_DIST_SPEED, 500.0f);
    settings.distillationUi.headsVolumeMl = prefs.getFloat(NVS_KEY_DIST_HEADS_VOL, 0.0f);
    settings.distillationUi.targetVolumeMl = prefs.getFloat(NVS_KEY_DIST_TARGET_VOL, 3000.0f);
    settings.distillationUi.endTempC = prefs.getFloat(NVS_KEY_DIST_END_TEMP, 96.0f);
    settings.distillationUi.powerPercent = prefs.getFloat(NVS_KEY_DIST_POWER_PCT, 100.0f);
    settings.distillationUi.powerW = prefs.getFloat(
        NVS_KEY_DIST_POWER_W,
        (settings.equipment.heaterPowerW > 0 ? settings.equipment.heaterPowerW : DEFAULT_HEATER_POWER_W) *
            (settings.distillationUi.powerPercent / 100.0f)
    );
    settings.distillationUi.tailsVolumeMl = prefs.getFloat(NVS_KEY_DIST_TAILS_VOL, 0.0f);
    settings.distillationUi.takeoffBackendType = static_cast<RectTakeoffBackendType>(
        prefs.getUChar(NVS_KEY_DIST_TAKEOFF_BACKEND, RECT_TAKEOFF_BACKEND_DEFAULT));
    settings.distillationUi.valveSafeVentConfirmed =
        prefs.getBool(NVS_KEY_DIST_SAFE_VENT, false);
    settings.distillationUi.vaporTempControlEnabled =
        prefs.getBool(NVS_KEY_DIST_VAPOR_CTRL, false);
    settings.distillationUi.vaporTempTargetC =
        prefs.getFloat(NVS_KEY_DIST_VAPOR_TARGET, 78.0f);
    settings.distillationUi.vaporTempMinPowerPercent =
        prefs.getUChar(NVS_KEY_DIST_VAPOR_MIN_PWR, 30);
    settings.distillationUi.vaporTempMaxPowerPercent =
        prefs.getUChar(NVS_KEY_DIST_VAPOR_MAX_PWR, 100);
    settings.distillationUi.vaporTempTimeoutMin =
        prefs.getUShort(NVS_KEY_DIST_VAPOR_TIMEOUT, 0);
    settings.distillationUi.vaporTempTargetC =
        constrain(settings.distillationUi.vaporTempTargetC, 20.0f, 110.0f);
    settings.distillationUi.vaporTempMinPowerPercent =
        constrain(settings.distillationUi.vaporTempMinPowerPercent, 0, 100);
    settings.distillationUi.vaporTempMaxPowerPercent =
        constrain(settings.distillationUi.vaporTempMaxPowerPercent,
                  settings.distillationUi.vaporTempMinPowerPercent, 100);
    if (static_cast<uint8_t>(settings.distillationUi.takeoffBackendType) >
        static_cast<uint8_t>(RectTakeoffBackendType::VALVE_SINGLE_SWITCHED)) {
        settings.distillationUi.takeoffBackendType = RectTakeoffBackendType::PUMP;
    }

    // Web security
    settings.security.authEnabled = prefs.getBool(NVS_KEY_WEB_AUTH_ENABLED, false);
    settings.security.rateLimitEnabled = prefs.getBool(NVS_KEY_WEB_RATE_LIMIT, true);
    prefs.getString(NVS_KEY_WEB_USERNAME, settings.security.username, sizeof(settings.security.username));
    prefs.getString(NVS_KEY_WEB_PASSWORD, settings.security.password, sizeof(settings.security.password));

    // NBK & Fermentation
    settings.nbk.powerW = prefs.getFloat(NVS_KEY_NBK_POWER, 2500.0f);
    settings.nbk.pumpSpeedMlH = prefs.getFloat(NVS_KEY_NBK_PUMP_SPEED, 20000.0f);
    settings.nbk.columnBottomTempThresholdC = prefs.getFloat(NVS_KEY_NBK_BOTTOM_TEMP, 95.0f);
    settings.nbk.topTempCorrectionEnabled = prefs.getBool(NVS_KEY_NBK_TOP_CORR, false);
    settings.nbk.columnTopTargetTempC = prefs.getFloat(NVS_KEY_NBK_TOP_TARGET, 78.0f);
    settings.nbk.targetVolumeMl = prefs.getFloat(NVS_KEY_NBK_TARGET_VOLUME, 0.0f);
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
    settings.rebootCountTotal = prefs.getUInt(NVS_KEY_REBOOT_TOTAL, 0);
    settings.rebootCountWdt = prefs.getUInt(NVS_KEY_REBOOT_WDT, 0);
    settings.rebootCountCrash = prefs.getUInt(NVS_KEY_REBOOT_CRASH, 0);
    settings.rebootCountUser = prefs.getUInt(NVS_KEY_REBOOT_USER, 0);

    prefs.end();

    if (storedSchemaVersion < NVS_SETTINGS_SCHEMA_VERSION) {
        migrateSettingsSchema(settings, storedSchemaVersion);
        LOG_I("NVS: Migrating settings schema %u -> %u",
              storedSchemaVersion, NVS_SETTINGS_SCHEMA_VERSION);
        if (!saveSettings(settings)) {
            LOG_E("NVS: Settings schema migration failed");
            return false;
        }
    }
    LOG_I("NVS: Settings loaded");
    return true;
}

bool saveSettings(const Settings& settings) {
    LOG_I("NVS: Saving settings...");

    prefs.begin(NVS_NAMESPACE, false); // Read-write
    prefs.putUShort(NVS_KEY_SETTINGS_SCHEMA, NVS_SETTINGS_SCHEMA_VERSION);

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
    prefs.putUChar(NVS_KEY_PACKING_TYPE,
                   static_cast<uint8_t>(settings.equipment.packingType));
    prefs.putUShort(NVS_KEY_HEATER_POWER, settings.equipment.heaterPowerW);
    prefs.putFloat(NVS_KEY_CUBE_VOLUME, settings.equipment.cubeVolumeL);
    prefs.putFloat(NVS_KEY_PACKING_COEFF, settings.equipment.packingCoeff);
    prefs.putFloat(NVS_KEY_MIN_HEATER_SUBMERGE, settings.equipment.minHeaterSubmergeL);
    prefs.putFloat(NVS_KEY_WATER_AUTOSTART_CUBE_TEMP, settings.equipment.waterAutoStartCubeTempC);
    prefs.putBool(NVS_KEY_BOOSTER_HEATER_ENABLED, settings.equipment.boosterHeaterEnabled);
    prefs.putUShort(NVS_KEY_BOOSTER_HEATER_POWER, settings.equipment.boosterHeaterPowerW);
    prefs.putFloat(NVS_KEY_BOOSTER_STOP_TEMP, settings.equipment.boosterHeaterStopCubeTempC);
    prefs.putBool(NVS_KEY_COOLING_PWM_ENABLED, settings.equipment.coolingPwmEnabled);
    prefs.putUChar(NVS_KEY_COOLING_PWM_MIN_DUTY, settings.equipment.coolingPwmMinDuty);
    prefs.putUChar(NVS_KEY_COOLING_PWM_MAX_DUTY, settings.equipment.coolingPwmMaxDuty);
    prefs.putUChar(NVS_KEY_COOLING_PWM_STARTUP, settings.equipment.coolingPwmStartupDuty);
    prefs.putBool(NVS_KEY_USE_DS2482_TEMPS, settings.equipment.useDs2482ForTemps);
    prefs.putUChar(NVS_KEY_DS2482_ADDRESS, settings.equipment.ds2482Address);
    prefs.putBool(NVS_KEY_BODY_LEVEL_ENABLED, settings.equipment.bodyLevelSensorEnabled);
    prefs.putFloat(NVS_KEY_BODY_LEVEL_THRESHOLD, settings.equipment.bodyLevelThresholdV);
    prefs.putBool(NVS_KEY_BODY_LEVEL_POLARITY, settings.equipment.bodyLevelTriggerAbove);
    prefs.putBool(NVS_KEY_LEAK_ENABLED, settings.equipment.leakSensorEnabled);
    prefs.putFloat(NVS_KEY_LEAK_THRESHOLD, settings.equipment.leakThresholdV);
    prefs.putBool(NVS_KEY_LEAK_POLARITY, settings.equipment.leakTriggerAbove);
    prefs.putBytes(NVS_KEY_TEMP_TOPOLOGY, &settings.equipment.temperatureTopology,
                   sizeof(settings.equipment.temperatureTopology));
    prefs.putBytes(NVS_KEY_TEMP_OFFSETS, &settings.tempCal, sizeof(settings.tempCal));
    prefs.putBool(NVS_KEY_FRACTION_MASTER, settings.fractionator.enabled);
    prefs.putBytes(NVS_KEY_FRACTION_ANGLES, settings.fractionator.angles, sizeof(settings.fractionator.angles));
    prefs.putBytes(NVS_KEY_FRACTION_ENABLED, settings.fractionator.positionsEnabled, sizeof(settings.fractionator.positionsEnabled));
    prefs.putBytes(NVS_KEY_FRACTION_PROGRAM, &settings.fractionProgram,
                   sizeof(settings.fractionProgram));

    // Дисплей
    prefs.putBool("disp_en", settings.displaySettings.enabled);
    prefs.putUChar("disp_br", settings.displaySettings.brightness);
    prefs.putChar("disp_rot", settings.displaySettings.rotation);
    prefs.putBool("disp_logo", settings.displaySettings.showLogo);
    prefs.putUChar(NVS_KEY_DISPLAY_REFRESH, (uint8_t)settings.displaySettings.refreshProfile);

    // Калибровка насоса
    prefs.putFloat(NVS_KEY_PUMP_ML_REV, settings.pumpCal.mlPerRevolution);
    savePressureCalibrationToNvs(settings.pressureCal);
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
    prefs.putUChar(NVS_KEY_RECT_TAKEOFF_BACKEND,
                   static_cast<uint8_t>(settings.rectParams.takeoffBackendType));
    prefs.putUChar(NVS_KEY_RECT_REFLUX_MODE,
                   static_cast<uint8_t>(settings.rectParams.refluxMode));
    prefs.putFloat(NVS_KEY_RECT_SR_RATIO, settings.rectParams.srRatio);
    prefs.putUShort(NVS_KEY_RECT_AUTO_CYCLE,
                    settings.rectParams.autonomousCycleSec);
    prefs.putUShort(NVS_KEY_RECT_AUTO_PAUSE,
                    settings.rectParams.autonomousPauseSec);
    prefs.putFloat(NVS_KEY_RECT_CHIM_AUTO, settings.rectParams.chimAutoPercent);
    prefs.putFloat(NVS_KEY_RECT_CHIM_TIME, settings.rectParams.chimTimePerH);
    prefs.putFloat(NVS_KEY_RECT_CHIM_BEG, settings.rectParams.chimBegPercent);
    prefs.putFloat(NVS_KEY_RECT_CHIM_MIN, settings.rectParams.chimMinPercent);
    prefs.putBytes(NVS_KEY_RECT_PHASE_POWER, settings.rectParams.phasePowerPercent,
                   sizeof(settings.rectParams.phasePowerPercent));
    prefs.putUChar(NVS_KEY_RECT_USE_PB, settings.rectParams.usePbMode);
    prefs.putUInt(NVS_KEY_RECT_TIMP_PB, settings.rectParams.timpPbMs);
    prefs.putUShort(NVS_KEY_RECT_ROUTE_SETTLE,
                    settings.rectParams.routingSettlingMs);
    prefs.putUShort(NVS_KEY_RECT_ROUTE_RETARGET,
                    settings.rectParams.routingRetargetMinMs);
    prefs.putUChar(NVS_KEY_RECT_BODY_CONTAINERS,
                   settings.rectParams.bodyContainerCount);
    prefs.putBool(NVS_KEY_RECT_PRESSURE_CTRL,
                  settings.rectParams.pressureControlEnabled);
    prefs.putUChar(NVS_KEY_RECT_PRESSURE_MIN_PWR,
                   settings.rectParams.pressureMinPowerPercent);
    prefs.putUShort(NVS_KEY_RECT_VALVE_PULSE_PERIOD,
                    settings.rectParams.valvePulsePeriodMs);
    prefs.putUShort(NVS_KEY_RECT_VALVE_PULSE_MIN_OPEN,
                    settings.rectParams.valvePulseMinOpenMs);
    prefs.putUShort(NVS_KEY_RECT_VALVE_PULSE_MAX_OPEN,
                    settings.rectParams.valvePulseMaxOpenMs);

    // Дистилляция
    prefs.putFloat(NVS_KEY_DIST_SPEED, settings.distillationUi.speedMlH);
    prefs.putFloat(NVS_KEY_DIST_HEADS_VOL, settings.distillationUi.headsVolumeMl);
    prefs.putFloat(NVS_KEY_DIST_TARGET_VOL, settings.distillationUi.targetVolumeMl);
    prefs.putFloat(NVS_KEY_DIST_END_TEMP, settings.distillationUi.endTempC);
    prefs.putFloat(NVS_KEY_DIST_POWER_W, settings.distillationUi.powerW);
    prefs.putFloat(NVS_KEY_DIST_POWER_PCT, settings.distillationUi.powerPercent);
    prefs.putFloat(NVS_KEY_DIST_TAILS_VOL, settings.distillationUi.tailsVolumeMl);
    prefs.putUChar(NVS_KEY_DIST_TAKEOFF_BACKEND,
                   static_cast<uint8_t>(settings.distillationUi.takeoffBackendType));
    prefs.putBool(NVS_KEY_DIST_SAFE_VENT,
                  settings.distillationUi.valveSafeVentConfirmed);
    prefs.putBool(NVS_KEY_DIST_VAPOR_CTRL,
                  settings.distillationUi.vaporTempControlEnabled);
    prefs.putFloat(NVS_KEY_DIST_VAPOR_TARGET,
                   settings.distillationUi.vaporTempTargetC);
    prefs.putUChar(NVS_KEY_DIST_VAPOR_MIN_PWR,
                   settings.distillationUi.vaporTempMinPowerPercent);
    prefs.putUChar(NVS_KEY_DIST_VAPOR_MAX_PWR,
                   settings.distillationUi.vaporTempMaxPowerPercent);
    prefs.putUShort(NVS_KEY_DIST_VAPOR_TIMEOUT,
                    settings.distillationUi.vaporTempTimeoutMin);

    // Web security
    prefs.putBool(NVS_KEY_WEB_AUTH_ENABLED, settings.security.authEnabled);
    prefs.putBool(NVS_KEY_WEB_RATE_LIMIT, settings.security.rateLimitEnabled);
    prefs.putString(NVS_KEY_WEB_USERNAME, settings.security.username);
    prefs.putString(NVS_KEY_WEB_PASSWORD, settings.security.password);

    // NBK & Fermentation
    prefs.putFloat(NVS_KEY_NBK_POWER, settings.nbk.powerW);
    prefs.putFloat(NVS_KEY_NBK_PUMP_SPEED, settings.nbk.pumpSpeedMlH);
    prefs.putFloat(NVS_KEY_NBK_BOTTOM_TEMP, settings.nbk.columnBottomTempThresholdC);
    prefs.putBool(NVS_KEY_NBK_TOP_CORR, settings.nbk.topTempCorrectionEnabled);
    prefs.putFloat(NVS_KEY_NBK_TOP_TARGET, settings.nbk.columnTopTargetTempC);
    prefs.putFloat(NVS_KEY_NBK_TARGET_VOLUME, settings.nbk.targetVolumeMl);
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
    prefs.putUInt(NVS_KEY_REBOOT_TOTAL, settings.rebootCountTotal);
    prefs.putUInt(NVS_KEY_REBOOT_WDT, settings.rebootCountWdt);
    prefs.putUInt(NVS_KEY_REBOOT_CRASH, settings.rebootCountCrash);
    prefs.putUInt(NVS_KEY_REBOOT_USER, settings.rebootCountUser);

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
