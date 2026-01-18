/**
 * Smart-Column S3 - Менеджер NVS (Non-Volatile Storage)
 *
 * Сохранение и загрузка настроек во флеш-память
 */

#include "nvs_manager.h"
#include <Preferences.h>

static Preferences prefs;

namespace NVSManager {

bool init() {
    LOG_I("NVS: Initializing...");
    // Preferences автоматически инициализируется
    LOG_I("NVS: Ready");
    return true;
}

bool loadSettings(Settings& settings) {
    LOG_I("NVS: Loading settings...");

    prefs.begin(NVS_NAMESPACE, true); // Read-only

    // WiFi
    prefs.getString(NVS_KEY_WIFI_SSID, settings.wifi.ssid, sizeof(settings.wifi.ssid));
    prefs.getString(NVS_KEY_WIFI_PASS, settings.wifi.password, sizeof(settings.wifi.password));

    // Telegram
    prefs.getString(NVS_KEY_TG_TOKEN, settings.telegram.token, sizeof(settings.telegram.token));
    prefs.getString(NVS_KEY_TG_CHAT, settings.telegram.chatId, sizeof(settings.telegram.chatId));

    // Cloud tunnel
    settings.cloud.enabled = prefs.getBool(NVS_KEY_CLOUD_ENABLED, false);
    prefs.getString(NVS_KEY_CLOUD_URL, settings.cloud.tunnelUrl, sizeof(settings.cloud.tunnelUrl));
    prefs.getString(NVS_KEY_CLOUD_TOKEN, settings.cloud.token, sizeof(settings.cloud.token));
    prefs.getString(NVS_KEY_CLOUD_TOKEN_ID, settings.cloud.tokenId, sizeof(settings.cloud.tokenId));

    // Оборудование
    settings.equipment.columnHeightMm = prefs.getUShort(NVS_KEY_COLUMN_HEIGHT, DEFAULT_COLUMN_HEIGHT_MM);
    settings.equipment.heaterPowerW = prefs.getUShort(NVS_KEY_HEATER_POWER, DEFAULT_HEATER_POWER_W);
    settings.equipment.cubeVolumeL = prefs.getUShort(NVS_KEY_CUBE_VOLUME, DEFAULT_CUBE_VOLUME_L);
    settings.equipment.packingCoeff = prefs.getFloat(NVS_KEY_PACKING_COEFF, DEFAULT_PACKING_COEFF);

    // Калибровка насоса
    settings.pumpCal.mlPerRevolution = prefs.getFloat(NVS_KEY_PUMP_ML_REV, DEFAULT_PUMP_ML_PER_REV);

    // Ректификация
    settings.rectParams.headsPercent = prefs.getFloat(NVS_KEY_RECT_HEADS_PCT, RECT_HEADS_PERCENT_DEFAULT);
    settings.rectParams.headsSpeedMlHKw = prefs.getFloat(NVS_KEY_RECT_HEADS_SPEED, RECT_HEADS_SPEED_ML_H_KW);
    settings.rectParams.bodySpeedMlHKw = prefs.getFloat(NVS_KEY_RECT_BODY_SPEED, RECT_HEADS_SPEED_ML_H_KW * 2);
    settings.rectParams.stabilizationMin = prefs.getUShort(NVS_KEY_RECT_STAB_MIN, RECT_STABILIZATION_TIME_MIN);
    settings.rectParams.purgeMin = prefs.getUShort(NVS_KEY_RECT_PURGE_MIN, RECT_PURGE_TIME_MIN);

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

    // Telegram
    prefs.putString(NVS_KEY_TG_TOKEN, settings.telegram.token);
    prefs.putString(NVS_KEY_TG_CHAT, settings.telegram.chatId);

    // Cloud tunnel
    prefs.putBool(NVS_KEY_CLOUD_ENABLED, settings.cloud.enabled);
    prefs.putString(NVS_KEY_CLOUD_URL, settings.cloud.tunnelUrl);
    prefs.putString(NVS_KEY_CLOUD_TOKEN, settings.cloud.token);
    prefs.putString(NVS_KEY_CLOUD_TOKEN_ID, settings.cloud.tokenId);

    // Оборудование
    prefs.putUShort(NVS_KEY_COLUMN_HEIGHT, settings.equipment.columnHeightMm);
    prefs.putUShort(NVS_KEY_HEATER_POWER, settings.equipment.heaterPowerW);
    prefs.putUShort(NVS_KEY_CUBE_VOLUME, settings.equipment.cubeVolumeL);
    prefs.putFloat(NVS_KEY_PACKING_COEFF, settings.equipment.packingCoeff);

    // Калибровка насоса
    prefs.putFloat(NVS_KEY_PUMP_ML_REV, settings.pumpCal.mlPerRevolution);

    // Ректификация
    prefs.putFloat(NVS_KEY_RECT_HEADS_PCT, settings.rectParams.headsPercent);
    prefs.putFloat(NVS_KEY_RECT_HEADS_SPEED, settings.rectParams.headsSpeedMlHKw);
    prefs.putFloat(NVS_KEY_RECT_BODY_SPEED, settings.rectParams.bodySpeedMlHKw);
    prefs.putUShort(NVS_KEY_RECT_STAB_MIN, settings.rectParams.stabilizationMin);
    prefs.putUShort(NVS_KEY_RECT_PURGE_MIN, settings.rectParams.purgeMin);

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

} // namespace NVSManager
