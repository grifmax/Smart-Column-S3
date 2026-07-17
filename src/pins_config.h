/**
 * Smart-Column S3 - Pin Profiles
 *
 * Compile-time профили ревизий плат через BOARD_REV.
 * Источник истины по пинам держим здесь, чтобы сборка не ехала на
 * разрозненных define по проекту.
 */

#ifndef PINS_CONFIG_H
#define PINS_CONFIG_H

// Ревизии плат
#define BOARD_REV_V1_0 1
#define BOARD_REV_V2_0 2
#define BOARD_REV_CUSTOM 99

#ifndef BOARD_REV
#define BOARD_REV BOARD_REV_V2_0
#endif

// =============================================================================
// PROFILE: BOARD_REV_V2_0
// =============================================================================
#if BOARD_REV == BOARD_REV_V2_0

#define BOARD_REV_LABEL "V2.0"
#define BOARD_PROFILE_NAME "ESP32-S3 TFT+Touch"

#define BOARD_HAS_TFT 1
#define BOARD_HAS_TOUCH 1
#define BOARD_HAS_TRIAC 1
#define BOARD_HAS_ZERO_CROSS 1
#define BOARD_HAS_FRACTIONATOR_SERVO 1
#define BOARD_HAS_STARTSTOP_PWM 1

// --- Основные исполнительные пины ---
#define PIN_HEATER 5
// Optional independent heater safety relay. Keep disabled until the board
// wiring assigns a dedicated GPIO; this must not reuse the heater/SSR pin.
#ifndef PIN_HEATER_SAFETY_RELAY
#define PIN_HEATER_SAFETY_RELAY -1
#endif
#ifndef HEATER_SAFETY_RELAY_ACTIVE_LEVEL
#define HEATER_SAFETY_RELAY_ACTIVE_LEVEL 1
#endif
#define PIN_PUMP_STEP 6
#define PIN_PUMP_DIR 7
#define PIN_PUMP_EN 15
#define PIN_VALVE_WATER 16
#define PIN_VALVE_HEADS 17
#define PIN_VALVE_BODY -1
#define PIN_VALVE_TAILS -1
#define PIN_VALVE_UNO 18
#define PIN_BUZZER 38
#define PIN_TEMP_SENSORS 4

// --- Симисторный регулятор ---
#define PIN_ZERO_CROSS 45
#define PIN_TRIAC 46

// --- PZEM-004T (UART1) ---
#define PIN_PZEM_RX 20
#define PIN_PZEM_TX 19

// --- I2C ---
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 9

// --- Периферия ---
#define PIN_SERVO_FRACTION 8
#define PIN_VALVE_STARTSTOP 14
#define PIN_FLOW_SENSOR 3
#define PIN_LEVEL_SENSOR 1

// --- TFT SPI ---
#define TFT_SCLK 10
#define TFT_MOSI 11
#define TFT_MISO 13
#define TFT_CS 2
#define TFT_DC 39
#define TFT_RST 40

// --- Touch ---
#define TOUCH_CLK 47
#define TOUCH_DIN 48
#define TOUCH_DO 41
#define TOUCH_CS 12
#define TOUCH_IRQ 42

// =============================================================================
// PROFILE: BOARD_REV_CUSTOM
// =============================================================================
#elif BOARD_REV == BOARD_REV_CUSTOM

#ifndef BOARD_REV_LABEL
#define BOARD_REV_LABEL "CUSTOM"
#endif

#ifndef BOARD_PROFILE_NAME
#define BOARD_PROFILE_NAME "Custom board profile"
#endif

#ifndef BOARD_HAS_TFT
#define BOARD_HAS_TFT 1
#endif

#ifndef BOARD_HAS_TOUCH
#define BOARD_HAS_TOUCH 1
#endif

#ifndef BOARD_HAS_TRIAC
#define BOARD_HAS_TRIAC 1
#endif

#ifndef BOARD_HAS_ZERO_CROSS
#define BOARD_HAS_ZERO_CROSS 1
#endif

#ifndef BOARD_HAS_FRACTIONATOR_SERVO
#define BOARD_HAS_FRACTIONATOR_SERVO 1
#endif

#ifndef BOARD_HAS_STARTSTOP_PWM
#define BOARD_HAS_STARTSTOP_PWM 1
#endif

#ifndef PIN_HEATER_SAFETY_RELAY
#define PIN_HEATER_SAFETY_RELAY -1
#endif
#ifndef HEATER_SAFETY_RELAY_ACTIVE_LEVEL
#define HEATER_SAFETY_RELAY_ACTIVE_LEVEL 1
#endif

// Для CUSTOM-профиля все используемые пины должны быть переданы через build_flags
// или отдельный include до подключения config.h.
#ifndef PIN_HEATER
#error "BOARD_REV_CUSTOM requires PIN_HEATER"
#endif
#ifndef PIN_PUMP_STEP
#error "BOARD_REV_CUSTOM requires PIN_PUMP_STEP"
#endif
#ifndef PIN_PUMP_DIR
#error "BOARD_REV_CUSTOM requires PIN_PUMP_DIR"
#endif
#ifndef PIN_PUMP_EN
#error "BOARD_REV_CUSTOM requires PIN_PUMP_EN"
#endif
#ifndef PIN_VALVE_WATER
#error "BOARD_REV_CUSTOM requires PIN_VALVE_WATER"
#endif
#ifndef PIN_VALVE_HEADS
#error "BOARD_REV_CUSTOM requires PIN_VALVE_HEADS"
#endif
#ifndef PIN_VALVE_BODY
#define PIN_VALVE_BODY -1
#endif
#ifndef PIN_VALVE_TAILS
#define PIN_VALVE_TAILS -1
#endif
#ifndef PIN_VALVE_UNO
#error "BOARD_REV_CUSTOM requires PIN_VALVE_UNO"
#endif
#ifndef PIN_BUZZER
#error "BOARD_REV_CUSTOM requires PIN_BUZZER"
#endif
#ifndef PIN_TEMP_SENSORS
#error "BOARD_REV_CUSTOM requires PIN_TEMP_SENSORS"
#endif
#ifndef PIN_ZERO_CROSS
#error "BOARD_REV_CUSTOM requires PIN_ZERO_CROSS"
#endif
#ifndef PIN_TRIAC
#error "BOARD_REV_CUSTOM requires PIN_TRIAC"
#endif
#ifndef PIN_PZEM_RX
#error "BOARD_REV_CUSTOM requires PIN_PZEM_RX"
#endif
#ifndef PIN_PZEM_TX
#error "BOARD_REV_CUSTOM requires PIN_PZEM_TX"
#endif
#ifndef PIN_I2C_SDA
#error "BOARD_REV_CUSTOM requires PIN_I2C_SDA"
#endif
#ifndef PIN_I2C_SCL
#error "BOARD_REV_CUSTOM requires PIN_I2C_SCL"
#endif
#ifndef PIN_SERVO_FRACTION
#error "BOARD_REV_CUSTOM requires PIN_SERVO_FRACTION"
#endif
#ifndef PIN_VALVE_STARTSTOP
#error "BOARD_REV_CUSTOM requires PIN_VALVE_STARTSTOP"
#endif
#ifndef PIN_FLOW_SENSOR
#error "BOARD_REV_CUSTOM requires PIN_FLOW_SENSOR"
#endif
#ifndef PIN_LEVEL_SENSOR
#error "BOARD_REV_CUSTOM requires PIN_LEVEL_SENSOR"
#endif

#if BOARD_HAS_TFT
#ifndef TFT_SCLK
#error "BOARD_REV_CUSTOM with BOARD_HAS_TFT=1 requires TFT_SCLK"
#endif
#ifndef TFT_MOSI
#error "BOARD_REV_CUSTOM with BOARD_HAS_TFT=1 requires TFT_MOSI"
#endif
#ifndef TFT_MISO
#error "BOARD_REV_CUSTOM with BOARD_HAS_TFT=1 requires TFT_MISO"
#endif
#ifndef TFT_CS
#error "BOARD_REV_CUSTOM with BOARD_HAS_TFT=1 requires TFT_CS"
#endif
#ifndef TFT_DC
#error "BOARD_REV_CUSTOM with BOARD_HAS_TFT=1 requires TFT_DC"
#endif
#ifndef TFT_RST
#error "BOARD_REV_CUSTOM with BOARD_HAS_TFT=1 requires TFT_RST"
#endif
#endif

#if BOARD_HAS_TOUCH
#ifndef TOUCH_CLK
#error "BOARD_REV_CUSTOM with BOARD_HAS_TOUCH=1 requires TOUCH_CLK"
#endif
#ifndef TOUCH_DIN
#error "BOARD_REV_CUSTOM with BOARD_HAS_TOUCH=1 requires TOUCH_DIN"
#endif
#ifndef TOUCH_DO
#error "BOARD_REV_CUSTOM with BOARD_HAS_TOUCH=1 requires TOUCH_DO"
#endif
#ifndef TOUCH_CS
#error "BOARD_REV_CUSTOM with BOARD_HAS_TOUCH=1 requires TOUCH_CS"
#endif
#ifndef TOUCH_IRQ
#error "BOARD_REV_CUSTOM with BOARD_HAS_TOUCH=1 requires TOUCH_IRQ"
#endif
#endif

// =============================================================================
// PROFILE: BOARD_REV_V1_0
// =============================================================================
#elif BOARD_REV == BOARD_REV_V1_0

#error "BOARD_REV_V1_0 is a legacy layout and is not fully mapped for the current firmware. Use BOARD_REV_V2_0 or BOARD_REV_CUSTOM."

#else

#error "Unknown BOARD_REV. Supported values: BOARD_REV_V2_0, BOARD_REV_CUSTOM."

#endif

// =============================================================================
// Common validation
// =============================================================================

#if !defined(PIN_HEATER) || !defined(PIN_PUMP_STEP) || !defined(PIN_PUMP_DIR) || \
    !defined(PIN_PUMP_EN) || !defined(PIN_VALVE_WATER) ||                        \
    !defined(PIN_VALVE_HEADS) || !defined(PIN_VALVE_UNO) ||                      \
    !defined(PIN_BUZZER) || !defined(PIN_TEMP_SENSORS) ||                        \
    !defined(PIN_ZERO_CROSS) || !defined(PIN_TRIAC) ||                           \
    !defined(PIN_PZEM_RX) || !defined(PIN_PZEM_TX) ||                            \
    !defined(PIN_I2C_SDA) || !defined(PIN_I2C_SCL) ||                            \
    !defined(PIN_SERVO_FRACTION) || !defined(PIN_VALVE_STARTSTOP) ||             \
    !defined(PIN_FLOW_SENSOR) || !defined(PIN_LEVEL_SENSOR)
#error "Pin profile is incomplete. Check src/pins_config.h for missing required pin defines."
#endif

#endif // PINS_CONFIG_H
