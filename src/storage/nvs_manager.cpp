/**
 * Smart-Column S3 - Менеджер NVS (Non-Volatile Storage)
 *
 * Сохранение и загрузка настроек во флеш-память
 */

#include "nvs_manager.h"
#include <Preferences.h>

static Preferences prefs;

namespace NVSManager {

void init() {
    LOG_I("NVS: Initializing...");
    // Preferences автоматически инициализируется
    LOG_I("NVS: Ready");
}

void loadSettings(Settings& settings) {
    LOG_I("NVS: Loading settings...");

    prefs.begin(NVS_NAMESPACE, true); // Read-only

    // WiFi
    prefs.getString(NVS_KEY_WIFI_SSID, settings.wifi.ssid, sizeof(settings.wifi.ssid));
    prefs.getString(NVS_KEY_WIFI_PASS, settings.wifi.password, sizeof(settings.wifi.password));

    // Telegram
    prefs.getString(NVS_KEY_TG_TOKEN, settings.telegram.token, sizeof(settings.telegram.token));
    prefs.getString(NVS_KEY_TG_CHAT, settings.telegram.chatId, sizeof(settings.telegram.chatId));

    // Оборудование
    settings.equipment.columnHeightMm = prefs.getUShort(NVS_KEY_COLUMN_HEIGHT, DEFAULT_COLUMN_HEIGHT_MM);
    settings.equipment.heaterPowerW = prefs.getUShort(NVS_KEY_HEATER_POWER, DEFAULT_HEATER_POWER_W);
    settings.equipment.cubeVolumeL = prefs.getUShort(NVS_KEY_CUBE_VOLUME, DEFAULT_CUBE_VOLUME_L);
    settings.equipment.packingCoeff = prefs.getFloat(NVS_KEY_PACKING_COEFF, DEFAULT_PACKING_COEFF);

    // Калибровка насоса
    settings.pumpCal.mlPerRevolution = prefs.getFloat(NVS_KEY_PUMP_ML_REV, DEFAULT_PUMP_ML_PER_REV);

    // Прочее
    settings.language = prefs.getUChar(NVS_KEY_LANGUAGE, 0);
    settings.theme = prefs.getUChar(NVS_KEY_THEME, 0);
    settings.soundEnabled = prefs.getBool(NVS_KEY_SOUND, true);

    prefs.end();

    LOG_I("NVS: Settings loaded");
}

void saveSettings(const Settings& settings) {
    LOG_I("NVS: Saving settings...");

    prefs.begin(NVS_NAMESPACE, false); // Read-write

    // WiFi
    prefs.putString(NVS_KEY_WIFI_SSID, settings.wifi.ssid);
    prefs.putString(NVS_KEY_WIFI_PASS, settings.wifi.password);

    // Telegram
    prefs.putString(NVS_KEY_TG_TOKEN, settings.telegram.token);
    prefs.putString(NVS_KEY_TG_CHAT, settings.telegram.chatId);

    // Оборудование
    prefs.putUShort(NVS_KEY_COLUMN_HEIGHT, settings.equipment.columnHeightMm);
    prefs.putUShort(NVS_KEY_HEATER_POWER, settings.equipment.heaterPowerW);
    prefs.putUShort(NVS_KEY_CUBE_VOLUME, settings.equipment.cubeVolumeL);
    prefs.putFloat(NVS_KEY_PACKING_COEFF, settings.equipment.packingCoeff);

    // Калибровка насоса
    prefs.putFloat(NVS_KEY_PUMP_ML_REV, settings.pumpCal.mlPerRevolution);

    // Прочее
    prefs.putUChar(NVS_KEY_LANGUAGE, settings.language);
    prefs.putUChar(NVS_KEY_THEME, settings.theme);
    prefs.putBool(NVS_KEY_SOUND, settings.soundEnabled);

    prefs.end();

    LOG_I("NVS: Settings saved");
}

void reset() {
    LOG_I("NVS: Resetting to defaults...");
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    LOG_I("NVS: Reset complete");
}

} // namespace NVSManager
