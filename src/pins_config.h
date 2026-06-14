/**
 * Smart-Column S3 - Pin Profiles
 * 
 * Поддержка различных ревизий плат через BOARD_REV
 */

#ifndef PINS_CONFIG_H
#define PINS_CONFIG_H

// Ревизии плат
#define BOARD_REV_V1_0    1  // Базовая (OLED)
#define BOARD_REV_V2_0    2  // Текущая (TFT + Touch)
#define BOARD_REV_CUSTOM  99

#ifndef BOARD_REV
  #define BOARD_REV BOARD_REV_V2_0
#endif

#if BOARD_REV == BOARD_REV_V2_0
  #define BOARD_REV_LABEL "V2.0"
#elif BOARD_REV == BOARD_REV_V1_0
  #define BOARD_REV_LABEL "V1.0"
#else
  #define BOARD_REV_LABEL "CUSTOM"
#endif

// =============================================================================
// ПРОФИЛЬ: BOARD_REV_V2_0 (Текущая стандартная)
// =============================================================================
#if BOARD_REV == BOARD_REV_V2_0

  // --- Основные пины управления ---
  #define PIN_HEATER          5
  #define PIN_PUMP_STEP       6
  #define PIN_PUMP_DIR        7
  #define PIN_PUMP_EN         15
  #define PIN_VALVE_WATER     16
  #define PIN_VALVE_HEADS     17
  #define PIN_VALVE_UNO       18
  #define PIN_BUZZER          38
  #define PIN_TEMP_SENSORS    4

  // --- Пины для симисторного регулятора (Phase Control) ---
  // Используются свободные GPIO ESP32-S3 (например, 45 и 46)
  #define PIN_ZERO_CROSS      45 
  #define PIN_TRIAC           46

  // --- PZEM-004T (UART1) ---
  #define PIN_PZEM_RX         20
  #define PIN_PZEM_TX         19

  // --- I2C ---
  #define PIN_I2C_SDA         21
  #define PIN_I2C_SCL         9

  // --- Периферия ---
  #define PIN_SERVO_FRACTION  8
  #define PIN_VALVE_STARTSTOP 14
  #define PIN_FLOW_SENSOR     3
  #define PIN_LEVEL_SENSOR    1

  // --- TFT SPI ---
  #define TFT_SCLK            10
  #define TFT_MOSI            11
  #define TFT_MISO            13
  #define TFT_CS              2
  #define TFT_DC              39
  #define TFT_RST             40

  // --- Touch ---
  #define TOUCH_CLK           47
  #define TOUCH_DIN           48
  #define TOUCH_DO            41
  #define TOUCH_CS            12
  #define TOUCH_IRQ           42

#elif BOARD_REV == BOARD_REV_V1_0
  // Тут можно описать пины для старой версии если нужно
#endif

#endif // PINS_CONFIG_H
