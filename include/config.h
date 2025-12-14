/**
 * Smart-Column S3 - Конфигурация
 *
 * Версия: 1.4.1
 * Платформа: ESP32-S3 DevKitC-1 N16R8
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// ВЕРСИЯ
// =============================================================================

#define FW_VERSION      "1.4.1"
#define FW_NAME         "Smart-Column S3"
#define FW_DATE         __DATE__

// =============================================================================
// ПИНЫ GPIO
// =============================================================================

// --- I2C шина (BMP280 x2, ADS1115) ---
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         9       // GPIO9 (GPIO22 не существует на S3!)

// --- OneWire (DS18B20 x7) ---
#define PIN_ONEWIRE         4

// --- Управление нагревом ---
#define PIN_SSR_HEATER      5       // SSR через PC817

// --- PZEM-004T (измеритель мощности) ---
#define PIN_PZEM_RX         44      // UART0 RX (подключить к TX PZEM)
#define PIN_PZEM_TX         43      // UART0 TX (подключить к RX PZEM)
#define PZEM_UART_NUM       0       // UART0 для PZEM-004T (аппаратный)
#define PZEM_BAUD_RATE      9600    // Скорость PZEM-004T

// --- Шаговый насос (TMC2209) ---
#define PIN_PUMP_STEP       6
#define PIN_PUMP_DIR        7
#define PIN_PUMP_EN         15

// --- Клапаны (MOSFET) ---
#define PIN_VALVE_WATER     16      // Охлаждение
#define PIN_VALVE_HEADS     17      // Отбор голов
#define PIN_VALVE_UNO       18      // Непрерывный отбор

// --- Опциональные выходы ---
#define PIN_SERVO_FRACTION  8       // Фракционник (PWM 50Hz)
#define PIN_VALVE_STARTSTOP 14      // Клапан старт-стоп (ШИМ) (был 9 - конфликт с I2C SCL!)

// --- Датчики ---
#define PIN_FLOW_SENSOR     3       // YF-S201 (счётчик импульсов)
#define PIN_LEVEL_SENSOR    13      // Оптический датчик уровня (опция)

// --- Интерфейс ---
#define PIN_BUZZER          38
#define PIN_BTN_1           0       // Boot кнопка
#define PIN_BTN_2           10
#define PIN_BTN_3           11
#define PIN_BTN_4           12

// --- TFT дисплей (SPI) - определены в platformio.ini ---
// MOSI=35, SCLK=36, CS=37, DC=39, RST=40

// --- OLED резервный (I2C) ---
#define OLED_ADDRESS        0x3C

// =============================================================================
// I2C АДРЕСА
// =============================================================================

#define I2C_ADDR_BMP280_1   0x76    // Атмосферное давление
#define I2C_ADDR_BMP280_2   0x77    // Резерв
#define I2C_ADDR_ADS1115    0x48    // АЦП 16-бит

// =============================================================================
// ADS1115 КАНАЛЫ
// =============================================================================

#define ADS_CHANNEL_PRESSURE    0   // MPX5010DP (давление куба)
#define ADS_CHANNEL_RESERVE_1   1
#define ADS_CHANNEL_RESERVE_2   2
#define ADS_CHANNEL_RESERVE_3   3

// =============================================================================
// ТЕРМОМЕТРЫ DS18B20 (индексы)
// =============================================================================

#define TEMP_CUBE           0       // Куб
#define TEMP_COLUMN_BOTTOM  1       // Царга низ (T_base)
#define TEMP_COLUMN_TOP     2       // Царга верх
#define TEMP_REFLUX         3       // Дефлегматор
#define TEMP_TSA            4       // Выход ТСА
#define TEMP_WATER_IN       5       // Вода вход
#define TEMP_WATER_OUT      6       // Вода выход
#define TEMP_COUNT          7

// =============================================================================
// ПАРАМЕТРЫ ОБОРУДОВАНИЯ (по умолчанию)
// =============================================================================

// Колонна
#define DEFAULT_COLUMN_HEIGHT_MM    1500    // Высота царги, мм
#define DEFAULT_PACKING_COEFF       15.0f   // СПН 3.5: мм рт.ст./м
#define DEFAULT_HEATER_POWER_W      3000    // Мощность ТЭНа, Вт
#define DEFAULT_CUBE_VOLUME_L       50      // Объём куба, л

// Насос
#define DEFAULT_PUMP_ML_PER_REV     0.5f    // мл/оборот
#define PUMP_STEPS_PER_REV          200     // Шагов на оборот
#define PUMP_MICROSTEPS             16      // Микрошаги TMC2209
#define PUMP_MAX_SPEED              1000    // Макс. шагов/сек
#define PUMP_ACCELERATION           500     // Ускорение

// Серво фракционник
#define SERVO_MIN_PULSE             500     // мкс
#define SERVO_MAX_PULSE             2500    // мкс
#define SERVO_MOVE_DELAY_MS         2000    // Пауза после поворота

// =============================================================================
// ФРАКЦИОННИК (5 позиций)
// =============================================================================

#define FRACTION_COUNT              5

// Углы по умолчанию (градусы)
#define FRACTION_ANGLE_HEADS        0
#define FRACTION_ANGLE_SUBHEADS     36
#define FRACTION_ANGLE_BODY         72
#define FRACTION_ANGLE_PRETAILS     108
#define FRACTION_ANGLE_TAILS        144

// Имена фракций
#define FRACTION_NAME_HEADS         "Головы"
#define FRACTION_NAME_SUBHEADS      "Подголовники"
#define FRACTION_NAME_BODY          "Тело"
#define FRACTION_NAME_PRETAILS      "Предхвостье"
#define FRACTION_NAME_TAILS         "Хвосты"

// =============================================================================
// ПОРОГИ БЕЗОПАСНОСТИ
// =============================================================================

#define SAFETY_TEMP_TSA_MAX         55.0f   // °C - прорыв паров
#define SAFETY_TEMP_WATER_OUT_MAX   70.0f   // °C - перегрев воды
#define SAFETY_VOLTAGE_MIN          190.0f  // V - низкое напряжение
#define SAFETY_VOLTAGE_MAX          250.0f  // V - высокое напряжение
#define SAFETY_SENSOR_TIMEOUT_MS    5000    // мс - таймаут датчика

// Давление (множители от P_захлёб)
#define PRESSURE_WORK_MULT          0.75f   // Рабочее
#define PRESSURE_WARN_MULT          0.90f   // Предупреждение
#define PRESSURE_CRIT_MULT          1.05f   // Аварийное

// =============================================================================
// ПАРАМЕТРЫ РЕЖИМОВ
// =============================================================================

// Авто-ректификация
#define RECT_STABILIZATION_TIME_MIN 20      // Стабилизация "на себя", мин
#define RECT_STABILIZATION_DELTA    0.1f    // °C за 5 мин
#define RECT_HEADS_PERCENT_DEFAULT  8       // % голов от АС
#define RECT_HEADS_SPEED_ML_H_KW    50      // мл/час на кВт
#define RECT_PURGE_TIME_MIN         10      // Продувка между фракциями

// Smart Decrement
#define DECREMENT_TRIGGER_DELTA     0.15f   // °C выше T_base → стоп
#define DECREMENT_RESUME_DELTA      0.10f   // °C выше T_base → старт
#define DECREMENT_WAIT_MAX_SEC      300     // Макс. ожидание, сек
#define DECREMENT_SPEED_MULT        0.85f   // Множитель снижения
#define DECREMENT_MIN_SPEED_ML_H_KW 50      // Минимум → хвосты

// УНО цикл
#define UNO_ON_SEC_DEFAULT          3       // Клапан открыт, сек
#define UNO_OFF_SEC_DEFAULT         60      // Клапан закрыт, сек

// Переход в хвосты
#define TAILS_TEMP_CUBE_MIN         99.0f   // °C - окончание погона
#define TAILS_TEMP_DELTA_MAX        0.3f    // °C - нестабильность

// =============================================================================
// КАЛИБРОВКА (по умолчанию)
// =============================================================================

// PZEM-004T (не требует калибровки - уже откалиброван)
#define PZEM_VOLTAGE_ALARM_MIN      190.0f  // V - мин. напряжение
#define PZEM_VOLTAGE_ALARM_MAX      250.0f  // V - макс. напряжение
#define PZEM_CURRENT_MAX            30.0f   // A - максимальный ток

// MPX5010DP
#define MPX5010_OFFSET              0.2f    // В при 0 кПа
#define MPX5010_SENSITIVITY         0.45f   // В/кПа

// =============================================================================
// ИНТЕРВАЛЫ (мс)
// =============================================================================

#define INTERVAL_TEMP_READ          1000    // Чтение температур
#define INTERVAL_PRESSURE_READ      500     // Чтение давления
#define INTERVAL_POWER_READ         1000    // Чтение мощности (PZEM-004T)
#define INTERVAL_FLOW_READ          1000    // Чтение потока воды
#define INTERVAL_DISPLAY_UPDATE     250     // Обновление дисплея
#define INTERVAL_WEB_BROADCAST      1000    // WebSocket broadcast
#define INTERVAL_LOG_WRITE          5000    // Запись в лог
#define INTERVAL_SAFETY_CHECK       100     // Проверка безопасности

// =============================================================================
// СЕТЬ
// =============================================================================

#define WIFI_CONNECT_TIMEOUT_MS     15000
#define WIFI_AP_SSID                "SmartColumn-S3"
#define WIFI_AP_PASS                "12345678"
#define MDNS_HOSTNAME               "smart-column"
#define WEB_SERVER_PORT             80
#define WEBSOCKET_PORT              81

// =============================================================================
// SPIFFS
// =============================================================================

#define LOG_FILE_PREFIX             "/logs/"
#define LOG_FILE_EXT                ".csv"
#define LOG_MAX_SIZE_BYTES          1048576 // 1 МБ на файл
#define LOG_MAX_FILES               10

// =============================================================================
// NVS NAMESPACE
// =============================================================================

#define NVS_NAMESPACE               "smartcol"

// Ключи NVS
#define NVS_KEY_WIFI_SSID           "wifi_ssid"
#define NVS_KEY_WIFI_PASS           "wifi_pass"
#define NVS_KEY_TG_TOKEN            "tg_token"
#define NVS_KEY_TG_CHAT             "tg_chat"
#define NVS_KEY_LANGUAGE            "lang"
#define NVS_KEY_THEME               "theme"
#define NVS_KEY_SOUND               "sound"
#define NVS_KEY_COLUMN_HEIGHT       "col_height"
#define NVS_KEY_PACKING_TYPE        "pack_type"
#define NVS_KEY_PACKING_COEFF       "pack_coeff"
#define NVS_KEY_HEATER_POWER        "heater_pwr"
#define NVS_KEY_CUBE_VOLUME         "cube_vol"
#define NVS_KEY_TEMP_OFFSETS        "temp_offs"
#define NVS_KEY_PUMP_ML_REV         "pump_mlrev"
#define NVS_KEY_PRESSURE_FLOOD      "p_flood"
#define NVS_KEY_HYDRO_POINTS        "hydro_pts"
#define NVS_KEY_FRACTION_ANGLES     "frac_ang"
#define NVS_KEY_FRACTION_ENABLED    "frac_en"

// =============================================================================
// ПИД РЕГУЛЯТОР
// =============================================================================

#define PID_KP_DEFAULT              2.0f
#define PID_KI_DEFAULT              0.5f
#define PID_KD_DEFAULT              1.0f
#define PID_OUTPUT_MIN              0
#define PID_OUTPUT_MAX              100

// =============================================================================
// ШИМ
// =============================================================================

#define PWM_FREQ_HEATER             1       // Гц (медленный для SSR)
#define PWM_FREQ_VALVE              1000    // Гц (клапан старт-стоп)
#define PWM_RESOLUTION              8       // бит (0-255)

// Каналы LEDC
#define LEDC_CHANNEL_HEATER         0
#define LEDC_CHANNEL_VALVE          1

// =============================================================================
// КНОПКИ
// =============================================================================

#define BTN_DEBOUNCE_MS             50
#define BTN_LONG_PRESS_MS           2000
#define BTN_REPEAT_MS               200

// =============================================================================
// ЗУММЕР
// =============================================================================

#define BUZZER_FREQ_LOW             1000    // Гц
#define BUZZER_FREQ_HIGH            2000    // Гц
#define BUZZER_DURATION_SHORT       100     // мс
#define BUZZER_DURATION_LONG        500     // мс

// =============================================================================
// DEBUG
// =============================================================================

#define DEBUG_SERIAL                1       // Вывод в Serial
#define DEBUG_LEVEL                 2       // 0=OFF, 1=ERROR, 2=INFO, 3=DEBUG

#if DEBUG_SERIAL
    #define LOG_E(fmt, ...) if(DEBUG_LEVEL >= 1) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__)
    #define LOG_I(fmt, ...) if(DEBUG_LEVEL >= 2) Serial.printf("[I] " fmt "\n", ##__VA_ARGS__)
    #define LOG_D(fmt, ...) if(DEBUG_LEVEL >= 3) Serial.printf("[D] " fmt "\n", ##__VA_ARGS__)
#else
    #define LOG_E(fmt, ...)
    #define LOG_I(fmt, ...)
    #define LOG_D(fmt, ...)
#endif

#endif // CONFIG_H
