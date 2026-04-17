/**
 * Smart-Column S3 - Конфигурация системы
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// ВЕРСИЯ ПРОШИВКИ
// =============================================================================

#define FIRMWARE_VERSION "2.2.5"
#define FW_NAME "Smart-Column-S3"
#define FW_VERSION FIRMWARE_VERSION
#define FW_DATE __DATE__

// Включение функций
#define DISPLAY_ENABLED // Включить поддержку дисплея
// #define BUTTONS_ENABLED // Включить поддержку кнопок управления
// Отдельная задача для генерации шагов насоса
#define PUMP_TASK_ENABLED 0
#define PUMP_TASK_CORE 0
#define PUMP_TASK_DELAY_MS 1
// Временное отключение сетевых сервисов (OTA/MQTT/Cloud)
#define NETWORK_SERVICES_ENABLED 1
// Включить веб-интерфейс
#define WEB_SERVER_ENABLED 1
// Форсировать AP-режим для диагностики
#define FORCE_AP_MODE 0
// Тестовый режим насоса (отключает остальную логику)
#define PUMP_TEST_MODE 0

// =============================================================================
// КОНФИГУРАЦИЯ ПИНОВ И ПЛАТЫ
// =============================================================================

#include "pins_config.h"

// Алиасы для обратной совместимости
#define PIN_SSR_HEATER PIN_HEATER
#define PIN_PUMP PIN_PUMP_STEP
#define PIN_ONEWIRE PIN_TEMP_SENSORS
#define PIN_PZEM_RX_LEGACY PIN_PZEM_RX
#define PZEM_UART_NUM 1
#define PZEM_BAUD_RATE 9600
#define DISPLAY_SDA_PIN PIN_I2C_SDA
#define DISPLAY_SCL_PIN PIN_I2C_SCL
#define PIN_VALVE PIN_VALVE_WATER

// --- Кнопки управления (если не используются, ставим -1) ---
#define PIN_BUTTON_UP    -1
#define PIN_BUTTON_DOWN  -1
#define PIN_BUTTON_OK    -1
#define PIN_BUTTON_BACK  -1

// =============================================================================
// OLED ДИСПЛЕЙ
// =============================================================================

#define DISPLAY_RESET_PIN -1         // Сброс (-1 если не используется)
#define DISPLAY_ADDRESS 0x3C         // I2C адрес (0x3C для 128x64)
#define OLED_ADDRESS DISPLAY_ADDRESS // Алиас для совместимости
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

// =============================================================================
// TFT ILI9488 3.5" 480x320 + Touch XPT2046 (LovyanGFX)
// =============================================================================

#define TFT_ENABLED 1        // Включить TFT дисплей (0 = только OLED)

// Калибровка тача (определены после калибровки)
#define TOUCH_CAL_X_MIN  472
#define TOUCH_CAL_X_MAX  3726
#define TOUCH_CAL_Y_MIN  528
#define TOUCH_CAL_Y_MAX  3548

// Параметры дисплея
#define TFT_WIDTH  480
#define TFT_HEIGHT 320

// =============================================================================
// I2C АДРЕСА
// =============================================================================

#define I2C_ADDR_BMP280_1 0x76 // Атмосферное давление
#define I2C_ADDR_BMP280_2 0x77 // Резерв
#define I2C_ADDR_ADS1115 0x48  // АЦП 16-бит
#define I2C_ADDR_MCP4725 0x60  // DAC мешалки (A0=GND → 0x60, A0=VCC → 0x61)

// =============================================================================
// ADS1115 КАНАЛЫ
// =============================================================================

#define ADS_CHANNEL_PRESSURE 0 // MPX5010DP (давление куба)
#define ADS_CHANNEL_RESERVE_1 1
#define ADS_CHANNEL_RESERVE_2 2
#define ADS_CHANNEL_RESERVE_3 3

// =============================================================================
// ТЕРМОМЕТРЫ DS18B20 (7 датчиков)
// =============================================================================

#define TEMP_CUBE 0          // Куб
#define TEMP_COLUMN_BOTTOM 1 // Царга низ (T_base)
#define TEMP_COLUMN_TOP 2    // Царга верх
#define TEMP_REFLUX 3        // Дефлегматор
#define TEMP_TSA 4           // Выход ТСА
#define TEMP_WATER_IN 5      // Вода вход
#define TEMP_WATER_OUT 6     // Вода выход
#define TEMP_COUNT 7
#define TEMP_PRODUCT TEMP_TSA // Алиас для совместимости

// Для обратной совместимости
#define MAX_TEMP_SENSORS TEMP_COUNT

// =============================================================================
// ПОРОГИ БЕЗОПАСНОСТИ
// =============================================================================

#define SAFETY_TEMP_TSA_MAX 55.0f       // °C - прорыв паров
#define SAFETY_TEMP_WATER_OUT_MAX 70.0f // °C - перегрев воды
#define SAFETY_VOLTAGE_MIN 190.0f       // V - низкое напряжение
#define SAFETY_VOLTAGE_MAX 250.0f       // V - высокое напряжение
#define SAFETY_SENSOR_TIMEOUT_MS 5000   // мс - таймаут датчика

// Давление (множители от P_захлёб)
#define PRESSURE_WORK_MULT 0.75f // Рабочее
#define PRESSURE_WARN_MULT 0.90f // Предупреждение
#define PRESSURE_CRIT_MULT 1.05f // Аварийное

// =============================================================================
// ПИД РЕГУЛЯТОР
// =============================================================================

#define PID_KP_DEFAULT 2.0f
#define PID_KI_DEFAULT 0.5f
#define PID_KD_DEFAULT 1.0f
#define PID_OUTPUT_MIN 0
#define PID_OUTPUT_MAX 100

// =============================================================================
// ШИМ (PWM) И УПРАВЛЕНИЕ НАГРЕВОМ
// =============================================================================

// Режимы управления нагревателем
#define HEATER_MODE_SSR 0    // Твердотельное реле (медленный ШИМ)
#define HEATER_MODE_TRIAC 1  // Симистор с детектором Zero-Cross (Phase Control)

// Текущий выбранный режим управления ТЭНом (по умолчанию SSR для обратной совместимости)
// Для использования симистора установите HEATER_MODE_TRIAC
#define HEATER_CONTROL_MODE HEATER_MODE_SSR

#define PWM_FREQ_HEATER 1    // Гц (медленный для SSR)
#define PWM_FREQ_VALVE 1000  // Гц
#define PWM_RESOLUTION 8     // бит (0-255)

// Настройки Phase Control (симистор)
#define TRIAC_MAX_POWER_W 3000   // Максимальная мощность ТЭНа (Вт) для расчетов
#define TRIAC_PULSE_WIDTH_US 20  // Длительность отпирающего импульса симистора (мкс)
#define TRIAC_MIN_ALPHA_US 100   // Минимальная задержка от нуля (мкс)
#define TRIAC_MAX_ALPHA_US 9500  // Максимальная задержка от нуля (мкс - почти выключено)

// Каналы LEDC
#define LEDC_CHANNEL_HEATER 0
#define LEDC_CHANNEL_VALVE 1
#define LEDC_CHANNEL_PUMP 2

// =============================================================================
// НАСТРОЙКИ КНОПОК
// =============================================================================

#define BUTTON_DEBOUNCE_MS 50      // Время дребезга (мс)
#define BUTTON_HOLD_TIME_MS 500    // Время удержания (мс)
#define BUTTON_REPEAT_DELAY_MS 200 // Интервал автоповтора (мс)

// =============================================================================
// НАСТРОЙКИ ИНТЕРФЕЙСА
// =============================================================================

#define MENU_TIMEOUT_MS 30000 // Таймаут возврата к главному экрану
#define DISPLAY_TIMEOUT_MS 0  // Таймаут отключения (0 = не отключать)
#define MAX_MENU_ITEMS 10     // Максимум элементов в меню

#include "secrets.h"

// Wi-Fi настройки
#define WIFI_SSID     WIFI_SSID_DEFAULT
#define WIFI_PASSWORD WIFI_PASSWORD_DEFAULT

// Настройки регулирования мощности
#define POWER_CONTROL_INTERVAL                                                 \
  1000                      // Интервал обновления регулировки мощности (мс)
#define SSR_PWM_FREQUENCY 5 // Частота ШИМ для симисторного регулятора (Гц)
#define SSR_CONTROL_INTERVAL 200 // Интервал обновления ШИМ (мс)

// Интервалы обновления (мс)
#define INTERVAL_SAFETY_CHECK 1000  // Проверка безопасности
#define INTERVAL_TEMP_READ 1000     // Чтение температур
#define INTERVAL_PRESSURE_READ 2000 // Чтение давления
#define INTERVAL_POWER_READ 1000    // Чтение мощности
#define INTERVAL_DISPLAY_UPDATE 200 // Обновление дисплея
#define INTERVAL_WEB_BROADCAST 2000 // Отправка данных по WebSocket
#define INTERVAL_WEB_BROADCAST_FULL 10000 // Полный пакет данных
#define INTERVAL_LOG_WRITE 60000    // Запись в лог (1 мин)

// WiFi настройки
#define WIFI_CONNECT_TIMEOUT_MS 10000  // Таймаут подключения к WiFi
#define WIFI_AP_SSID "Smart-Column-S3" // SSID точки доступа
#define WIFI_AP_PASS "12345678"        // Пароль точки доступа

// Значения по умолчанию для оборудования
#define DEFAULT_COLUMN_HEIGHT_MM 1500 // Высота колонны (мм)
#define DEFAULT_PACKING_COEFF 15.0f   // СПН коэффициент: мм рт.ст./м
#define DEFAULT_HEATER_POWER_W 3000   // Мощность нагревателя (Вт)
#define DEFAULT_CUBE_VOLUME_L 37.0f   // Объем куба (л)
#define DEFAULT_MIN_HEATER_SUBMERGE_L 7.5f // Мин. уровень жидкости над ТЭН (л)
#define DEFAULT_WATER_AUTOSTART_CUBE_TEMP_C 45.0f // Автостарт воды по температуре куба (°C)

// Параметры насоса по умолчанию
#define DEFAULT_PUMP_ML_PER_REV 0.5f // мл на оборот
#define PUMP_STEPS_PER_REV 200       // Шагов на оборот
#define PUMP_MICROSTEPS 16           // Микрошаги TMC2209
#define PUMP_MAX_SPEED 4000          // Макс. шагов/сек (было 1000)
#define PUMP_ACCELERATION 500        // Ускорение

// Серво фракционник
#define SERVO_MIN_PULSE 500      // мкс
#define SERVO_MAX_PULSE 2500     // мкс
#define SERVO_MOVE_DELAY_MS 2000 // Пауза после поворота

// =============================================================================
// ФРАКЦИОННИК (5 позиций)
// =============================================================================

#define FRACTION_COUNT 5

// Углы фракционатора (градусы)
#define FRACTION_ANGLE_HEADS 0      // Головы
#define FRACTION_ANGLE_SUBHEADS 36  // Подголовники
#define FRACTION_ANGLE_BODY 72      // Тело
#define FRACTION_ANGLE_PRETAILS 108 // Предхвостье
#define FRACTION_ANGLE_TAILS 144    // Хвосты

// Имена фракций
#define FRACTION_NAME_HEADS "Головы"
#define FRACTION_NAME_SUBHEADS "Подголовники"
#define FRACTION_NAME_BODY "Тело"
#define FRACTION_NAME_PRETAILS "Предхвостье"
#define FRACTION_NAME_TAILS "Хвосты"

// =============================================================================
// ПАРАМЕТРЫ РЕЖИМОВ
// =============================================================================

// Авто-ректификация
#define RECT_STABILIZATION_TIME_MIN 20 // Стабилизация "на себя", мин
#define RECT_STABILIZATION_DELTA 0.1f  // °C за 5 мин
#define RECT_HEADS_PERCENT_DEFAULT 8   // % голов от АС
#define RECT_BODY_PERCENT_DEFAULT 84   // % тела от АС
#define RECT_TAILS_PERCENT_DEFAULT 8   // % хвостов от АС
#define RECT_HEADS_SPEED_ML_H_KW 50    // мл/час на кВт
#define RECT_PURGE_TIME_MIN 10         // Продувка между фракциями
#define RECT_FEED_ABV_DEFAULT 40.0f    // Крепость спирта-сырца по умолчанию, %

// Пороги аварий (могут быть изменены в настройках)
#define DEFAULT_SAFETY_PRESSURE_MAX_MMHG 50.0f
#define DEFAULT_SAFETY_TSA_MAX_C 55.0f
#define DEFAULT_SAFETY_WATER_OUT_MAX_C 70.0f
#define DEFAULT_SAFETY_WATER_OUT_RISE_RATE_C_MIN 8.0f
#define DEFAULT_SAFETY_PRESSURE_RISE_RATE_MMHG_MIN 20.0f

// Справочные переходы по температуре куба (при 1013.25 hPa)
#define RECT_CUBE_BODY_TO_TAILS_BASE_C 94.5f
#define RECT_CUBE_FINISH_BASE_C 99.0f
#define RECT_TEMP_COMP_C_PER_HPA 0.049f
#define RECT_PRESSURE_STD_HPA 1013.25f

// Smart Decrement (умное снижение скорости)
#define DECREMENT_TRIGGER_DELTA 0.15f  // °C выше T_base → стоп
#define DECREMENT_RESUME_DELTA 0.10f   // °C выше T_base → старт
#define DECREMENT_WAIT_MAX_SEC 300     // Макс. ожидание, сек
#define DECREMENT_SPEED_MULT 0.85f     // Множитель снижения
#define DECREMENT_MIN_SPEED_ML_H_KW 50 // Минимум → хвосты

// =============================================================================
// SPIFFS / LittleFS
// =============================================================================

#define LOG_FILE_PREFIX "/logs/"
#define LOG_FILE_EXT ".csv"
#define LOG_MAX_SIZE_BYTES 1048576 // 1 МБ на файл
#define LOG_MAX_FILES 10

// =============================================================================
// СЕТЬ
// =============================================================================

#define MDNS_HOSTNAME "smart-column"
#define WEB_SERVER_PORT 80
#define WEBSOCKET_PORT 81

// =============================================================================
// КАЛИБРОВКА
// =============================================================================

// PZEM-004T
#define PZEM_VOLTAGE_ALARM_MIN 190.0f // V - мин. напряжение
#define PZEM_VOLTAGE_ALARM_MAX 250.0f // V - макс. напряжение
#define PZEM_CURRENT_MAX 30.0f        // A - максимальный ток

// MPX5010DP (датчик давления)
#define MPX5010_OFFSET 0.2f       // В при 0 кПа
#define MPX5010_SENSITIVITY 0.45f // В/кПа

// =============================================================================
// NVS NAMESPACE
// =============================================================================

#define NVS_NAMESPACE "smartcol"

// Ключи NVS
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"
#define NVS_KEY_WIFI_PROFILES "wifi_profiles"
#define NVS_KEY_MQTT_ENABLED "mq_en"
#define NVS_KEY_MQTT_SERVER "mq_srv"
#define NVS_KEY_MQTT_PORT "mq_port"
#define NVS_KEY_MQTT_USERNAME "mq_user"
#define NVS_KEY_MQTT_PASSWORD "mq_pass"
#define NVS_KEY_MQTT_BASE_TOPIC "mq_topic"
#define NVS_KEY_MQTT_INTERVAL "mq_intvl"
#define NVS_KEY_MQTT_DISCOVERY "mq_disc"
#define NVS_KEY_LANGUAGE "lang"
#define NVS_KEY_THEME "theme"
#define NVS_KEY_SOUND "sound"
#define NVS_KEY_COLUMN_HEIGHT "col_height"
#define NVS_KEY_PACKING_TYPE "pack_type"
#define NVS_KEY_PACKING_COEFF "pack_coeff"
#define NVS_KEY_HEATER_POWER "heater_pwr"
#define NVS_KEY_CUBE_VOLUME "cube_vol"
#define NVS_KEY_MIN_HEATER_SUBMERGE "heat_sub_l"
#define NVS_KEY_WATER_AUTOSTART_CUBE_TEMP "water_auto_t"
#define NVS_KEY_TEMP_OFFSETS "temp_offs"
#define NVS_KEY_PUMP_ML_REV "pump_mlrev"
#define NVS_KEY_PRESSURE_FLOOD "p_flood"
#define NVS_KEY_HYDRO_POINTS "hydro_pts"
#define NVS_KEY_FRACTION_ANGLES "frac_ang"
#define NVS_KEY_FRACTION_ENABLED "frac_en"
#define NVS_KEY_FRACTION_MASTER "frac_master"
#define NVS_KEY_RECT_HEADS_PCT "rect_hpct"
#define NVS_KEY_RECT_BODY_PCT "rect_bpct"
#define NVS_KEY_RECT_TAILS_PCT "rect_tpct"
#define NVS_KEY_RECT_HEADS_SPEED "rect_hspd"
#define NVS_KEY_RECT_BODY_SPEED "rect_bspd"
#define NVS_KEY_RECT_STAB_MIN "rect_stab"
#define NVS_KEY_RECT_PURGE_MIN "rect_prg"
#define NVS_KEY_RECT_FEED_VOL "rect_fvol"
#define NVS_KEY_RECT_FEED_ABV "rect_fabv"
#define NVS_KEY_RECT_FEEDSTOCK "rect_feed"
#define NVS_KEY_DIST_SPEED "dist_spd"
#define NVS_KEY_DIST_HEADS_VOL "dist_hvol"
#define NVS_KEY_DIST_TARGET_VOL "dist_tvol"
#define NVS_KEY_DIST_END_TEMP "dist_etmp"
#define NVS_KEY_DIST_POWER_PCT "dist_ppct"
#define NVS_KEY_DIST_TAILS_VOL "dist_xvol"
#define NVS_KEY_WEB_AUTH_ENABLED "web_auth"
#define NVS_KEY_WEB_RATE_LIMIT "web_rate"
#define NVS_KEY_WEB_USERNAME "web_user"
#define NVS_KEY_WEB_PASSWORD "web_pass"
#define NVS_KEY_NBK_POWER "nbk_pwr"
#define NVS_KEY_NBK_PUMP_SPEED "nbk_spd"
#define NVS_KEY_NBK_BOTTOM_TEMP "nbk_btm"
#define NVS_KEY_FERM_TARGET_TEMP "ferm_tgt"
#define NVS_KEY_FERM_HYSTERESIS "ferm_hyst"
#define NVS_KEY_FERM_USE_HEATER "ferm_heat"
#define NVS_KEY_SAFETY_PRESSURE_MAX "safe_pmax"
#define NVS_KEY_SAFETY_TSA_MAX "safe_tsa"
#define NVS_KEY_SAFETY_WATER_OUT_MAX "safe_wout"
#define NVS_KEY_SAFETY_WATER_OUT_RISE_RATE "safe_wrpm"
#define NVS_KEY_SAFETY_PRESSURE_RISE_RATE "safe_prpm"

// Cloud tunnel settings
#define NVS_KEY_CLOUD_ENABLED "cl_en"
#define NVS_KEY_CLOUD_URL "cl_url"
#define NVS_KEY_CLOUD_TOKEN "cl_tok"
#define NVS_KEY_CLOUD_TOKEN_ID "cl_tid"

// Мешалка
#define NVS_KEY_STIRRER_ENABLED "stir_en"
#define NVS_KEY_STIRRER_SPEED   "stir_spd"
#define NVS_KEY_STIRRER_AUTO_MASH "stir_amash"
#define NVS_KEY_STIRRER_AUTO_FERM "stir_aferm"
#define NVS_KEY_STIRRER_AUTO_NBK  "stir_anbk"

// Touch calibration
#define NVS_KEY_TOUCH_XMIN "tch_xmin"
#define NVS_KEY_TOUCH_XMAX "tch_xmax"
#define NVS_KEY_TOUCH_YMIN "tch_ymin"
#define NVS_KEY_TOUCH_YMAX "tch_ymax"
#define NVS_KEY_TOUCH_VALID "tch_valid"
#define NVS_KEY_DISPLAY_REFRESH "disp_refr"
#define NVS_KEY_LAST_REBOOT_REASON "boot_reason"

// =============================================================================
// МАКРОСЫ ЛОГИРОВАНИЯ
// =============================================================================

#define DEBUG_SERIAL 1
#define DEBUG_LEVEL 2 // 0=OFF, 1=ERROR, 2=INFO, 3=DEBUG

#if DEBUG_SERIAL
#define LOG_E(fmt, ...)                                                        \
  if (DEBUG_LEVEL >= 1)                                                        \
  Serial.printf("[E] " fmt "\n", ##__VA_ARGS__)
#define LOG_W(fmt, ...)                                                        \
  if (DEBUG_LEVEL >= 2)                                                        \
  Serial.printf("[W] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN LOG_W // Алиас для совместимости
#define LOG_I(fmt, ...)                                                        \
  if (DEBUG_LEVEL >= 2)                                                        \
  Serial.printf("[I] " fmt "\n", ##__VA_ARGS__)
#define LOG_D(fmt, ...)                                                        \
  if (DEBUG_LEVEL >= 3)                                                        \
  Serial.printf("[D] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_E(fmt, ...)
#define LOG_W(fmt, ...)
#define LOG_WARN(fmt, ...)
#define LOG_I(fmt, ...)
#define LOG_D(fmt, ...)
#endif

#endif // CONFIG_H
