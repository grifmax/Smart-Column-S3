/**
 * Smart-Column S3 - Драйвер клапанов и фракционника
 *
 * - Электроклапаны 12V NC через MOSFET (вода, головы, УНО)
 * - Клапан старт-стоп с ШИМ управлением
 * - Сервопривод MG996R для фракционника (5 позиций)
 */

#include "valves.h"
#include <ESP32Servo.h>

// =============================================================================
// ГЛОБАЛЬНЫЕ ОБЪЕКТЫ
// =============================================================================

static Servo fractionatorServo;

// =============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// =============================================================================

// Состояние клапанов
static bool valveWater = false;
static bool valveHeads = false;
static bool valveUno = false;
static uint8_t valveStartStopDuty = 0;

// Фракционник
static bool fractionatorEnabled = false;
static Fraction currentFraction = Fraction::HEADS;
static uint8_t currentAngle = FRACTION_ANGLE_HEADS;
static uint8_t fractionAngles[FRACTION_COUNT] = {
    FRACTION_ANGLE_HEADS,
    FRACTION_ANGLE_SUBHEADS,
    FRACTION_ANGLE_BODY,
    FRACTION_ANGLE_PRETAILS,
    FRACTION_ANGLE_TAILS
};

// =============================================================================
// ПУБЛИЧНЫЙ ИНТЕРФЕЙС
// =============================================================================

namespace Valves {

void init() {
    LOG_I("Valves: Initializing...");

    // Настройка пинов клапанов (активный HIGH для открытия)
    pinMode(PIN_VALVE_WATER, OUTPUT);
    pinMode(PIN_VALVE_HEADS, OUTPUT);
    pinMode(PIN_VALVE_UNO, OUTPUT);

    // Начальное состояние - все закрыты
    digitalWrite(PIN_VALVE_WATER, LOW);
    digitalWrite(PIN_VALVE_HEADS, LOW);
    digitalWrite(PIN_VALVE_UNO, LOW);

    // Настройка ШИМ для клапана старт-стоп
    ledcSetup(LEDC_CHANNEL_VALVE, PWM_FREQ_VALVE, PWM_RESOLUTION);
    ledcAttachPin(PIN_VALVE_STARTSTOP, LEDC_CHANNEL_VALVE);
    ledcWrite(LEDC_CHANNEL_VALVE, 0);

    LOG_I("Valves: Init complete");
}

void initFractionator() {
    LOG_I("Valves: Initializing fractionator servo...");

    // Разрешить до 16 серво-каналов
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    // Подключить сервопривод
    fractionatorServo.setPeriodHertz(50); // 50 Гц стандарт для серво
    fractionatorServo.attach(PIN_SERVO_FRACTION, SERVO_MIN_PULSE, SERVO_MAX_PULSE);

    // Установить в позицию "Головы"
    fractionatorServo.write(FRACTION_ANGLE_HEADS);
    currentFraction = Fraction::HEADS;
    currentAngle = FRACTION_ANGLE_HEADS;
    fractionatorEnabled = true;

    LOG_I("Valves: Fractionator ready (angle=%d)", currentAngle);

    // Пауза для установки серво
    delay(SERVO_MOVE_DELAY_MS);
}

// =========================================================================
// ОСНОВНЫЕ КЛАПАНЫ
// =========================================================================

void setWater(bool open) {
    digitalWrite(PIN_VALVE_WATER, open ? HIGH : LOW);
    valveWater = open;
    LOG_D("Valves: Water %s", open ? "OPEN" : "CLOSED");
}

bool getWater() {
    return valveWater;
}

void setHeads(bool open) {
    digitalWrite(PIN_VALVE_HEADS, open ? HIGH : LOW);
    valveHeads = open;
    LOG_D("Valves: Heads %s", open ? "OPEN" : "CLOSED");
}

bool getHeads() {
    return valveHeads;
}

void setUno(bool open) {
    digitalWrite(PIN_VALVE_UNO, open ? HIGH : LOW);
    valveUno = open;
    LOG_D("Valves: UNO %s", open ? "OPEN" : "CLOSED");
}

bool getUno() {
    return valveUno;
}

void setStartStop(uint8_t duty) {
    ledcWrite(LEDC_CHANNEL_VALVE, duty);
    valveStartStopDuty = duty;
    LOG_D("Valves: StartStop PWM=%d", duty);
}

uint8_t getStartStop() {
    return valveStartStopDuty;
}

void closeAll() {
    LOG_I("Valves: Closing all valves");
    setWater(false);
    setHeads(false);
    setUno(false);
    setStartStop(0);
}

// =========================================================================
// ФРАКЦИОННИК
// =========================================================================

void setFraction(Fraction fraction, bool smooth) {
    if (!fractionatorEnabled) {
        LOG_E("Valves: Fractionator not initialized!");
        return;
    }

    uint8_t idx = static_cast<uint8_t>(fraction);
    if (idx >= FRACTION_COUNT) {
        LOG_E("Valves: Invalid fraction index %d", idx);
        return;
    }

    uint8_t targetAngle = fractionAngles[idx];

    if (smooth && abs(targetAngle - currentAngle) > 10) {
        // Плавный поворот
        int8_t step = (targetAngle > currentAngle) ? 1 : -1;
        for (int angle = currentAngle; angle != targetAngle; angle += step) {
            fractionatorServo.write(angle);
            delay(15); // ~1 секунда на 60 градусов
        }
    }

    fractionatorServo.write(targetAngle);
    currentAngle = targetAngle;
    currentFraction = fraction;

    const char* names[] = {
        FRACTION_NAME_HEADS,
        FRACTION_NAME_SUBHEADS,
        FRACTION_NAME_BODY,
        FRACTION_NAME_PRETAILS,
        FRACTION_NAME_TAILS
    };

    LOG_I("Valves: Fractionator → %s (angle=%d)", names[idx], targetAngle);

    // Пауза для стабилизации
    delay(SERVO_MOVE_DELAY_MS);
}

void setFractionAngle(uint8_t angle) {
    if (!fractionatorEnabled) return;

    if (angle > 180) angle = 180;

    fractionatorServo.write(angle);
    currentAngle = angle;

    LOG_D("Valves: Fractionator angle set to %d", angle);
}

Fraction getCurrentFraction() {
    return currentFraction;
}

Fraction getNextEnabledFraction(const FractionatorSettings& settings) {
    // Начать с следующей фракции
    uint8_t current = static_cast<uint8_t>(currentFraction);
    uint8_t next = (current + 1) % FRACTION_COUNT;

    // Найти следующую активную
    for (uint8_t i = 0; i < FRACTION_COUNT; i++) {
        uint8_t idx = (next + i) % FRACTION_COUNT;
        if (settings.positionsEnabled[idx]) {
            return static_cast<Fraction>(idx);
        }
    }

    // Если ни одна не активна - вернуть текущую
    return currentFraction;
}

void nextFraction(const FractionatorSettings& settings) {
    Fraction next = getNextEnabledFraction(settings);

    if (next != currentFraction) {
        setFraction(next, true);
    } else {
        LOG_I("Valves: No next enabled fraction");
    }
}

// =========================================================================
// УНО ЦИКЛ
// =========================================================================

void updateUno(UnoParams& params) {
    if (!params.enabled) return;

    uint32_t now = millis();
    uint32_t elapsed = now - params.lastToggle;

    if (params.state) {
        // Клапан открыт - проверить время
        if (elapsed >= params.onSeconds * 1000UL) {
            setUno(false);
            params.state = false;
            params.lastToggle = now;
            LOG_D("UNO: Valve closed (cycle)");
        }
    } else {
        // Клапан закрыт - проверить время
        if (elapsed >= params.offSeconds * 1000UL) {
            setUno(true);
            params.state = true;
            params.lastToggle = now;
            LOG_D("UNO: Valve opened (cycle)");
        }
    }
}

} // namespace Valves
