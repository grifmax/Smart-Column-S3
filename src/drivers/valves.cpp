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

struct ValvePulseState {
    bool active = false;
    uint32_t startedAt = 0;
    uint32_t durationMs = 0;
};
static ValvePulseState g_valvePulses[3];

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

// ARCH-3 fix: неблокирующий автомат плавного движения серво
struct ServoMove {
    bool active = false;
    uint8_t targetAngle = 0;
    uint8_t startAngle = 0;
    uint32_t startTime = 0;
    uint32_t totalMs = 0;   // общее время движения
};
static ServoMove g_servoMove;

static bool isDemoHardwareSuppressed() {
    return g_settings.demoMode;
}

static size_t getValvePulseIndex(Valves::ValveId valve) {
    return static_cast<size_t>(valve);
}

static void clearValvePulse(Valves::ValveId valve) {
    ValvePulseState &pulse = g_valvePulses[getValvePulseIndex(valve)];
    pulse.active = false;
    pulse.startedAt = 0;
    pulse.durationMs = 0;
}

static void cancelValvePulse(Valves::ValveId valve) {
    ValvePulseState &pulse = g_valvePulses[getValvePulseIndex(valve)];
    if (!pulse.active) return;
    clearValvePulse(valve);
}

static void applyWaterState(bool open) {
    valveWater = open;
    if (isDemoHardwareSuppressed()) {
        return;
    }
    digitalWrite(PIN_VALVE_WATER, open ? HIGH : LOW);
}

static void applyHeadsState(bool open) {
    valveHeads = open;
    if (isDemoHardwareSuppressed()) {
        return;
    }
    digitalWrite(PIN_VALVE_HEADS, open ? HIGH : LOW);
}

static void applyUnoState(bool open) {
    valveUno = open;
    if (isDemoHardwareSuppressed()) {
        return;
    }
    digitalWrite(PIN_VALVE_UNO, open ? HIGH : LOW);
}

static void setValveState(Valves::ValveId valve, bool open, bool cancelPulse) {
    if (cancelPulse) {
        cancelValvePulse(valve);
    }

    switch (valve) {
    case Valves::ValveId::WATER:
        applyWaterState(open);
        LOG_D("Valves: Water %s", open ? "OPEN" : "CLOSED");
        break;
    case Valves::ValveId::HEADS:
        applyHeadsState(open);
        LOG_D("Valves: Heads %s", open ? "OPEN" : "CLOSED");
        break;
    case Valves::ValveId::UNO:
        applyUnoState(open);
        LOG_D("Valves: UNO %s", open ? "OPEN" : "CLOSED");
        break;
    }
}

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
    g_servoMove.active = false;

    LOG_I("Valves: Fractionator ready (angle=%d)", currentAngle);
    // ARCH-3 fix: убран delay() при инициализации — серво успеет занять позицию пока проходит первый loop
}

// =========================================================================
// ОСНОВНЫЕ КЛАПАНЫ
// =========================================================================

void setWater(bool open) {
    setValveState(ValveId::WATER, open, true);
}

bool getWater() {
    return valveWater;
}

void setHeads(bool open) {
    setValveState(ValveId::HEADS, open, true);
}

bool getHeads() {
    return valveHeads;
}

void setUno(bool open) {
    setValveState(ValveId::UNO, open, true);
}

bool getUno() {
    return valveUno;
}

void pulse(ValveId valve, uint32_t durationMs) {
    if (durationMs < 100) durationMs = 100;
    if (durationMs > 10000) durationMs = 10000;

    ValvePulseState &pulseState = g_valvePulses[getValvePulseIndex(valve)];
    pulseState.active = true;
    pulseState.startedAt = millis();
    pulseState.durationMs = durationMs;

    setValveState(valve, true, false);
}

bool isPulseActive(ValveId valve) {
    return g_valvePulses[getValvePulseIndex(valve)].active;
}

uint32_t getPulseRemainingMs(ValveId valve) {
    const ValvePulseState &pulseState = g_valvePulses[getValvePulseIndex(valve)];
    if (!pulseState.active) return 0;
    const uint32_t elapsed = millis() - pulseState.startedAt;
    if (elapsed >= pulseState.durationMs) return 0;
    return pulseState.durationMs - elapsed;
}

void setStartStop(uint8_t duty) {
    valveStartStopDuty = duty;
    if (isDemoHardwareSuppressed()) {
        return;
    }
    ledcWrite(LEDC_CHANNEL_VALVE, duty);
    LOG_D("Valves: StartStop PWM=%d", duty);
}

uint8_t getStartStop() {
    return valveStartStopDuty;
}

void closeAll() {
    LOG_I("Valves: Closing all valves");
    clearValvePulse(ValveId::WATER);
    clearValvePulse(ValveId::HEADS);
    clearValvePulse(ValveId::UNO);
    setValveState(ValveId::WATER, false, false);
    setValveState(ValveId::HEADS, false, false);
    setValveState(ValveId::UNO, false, false);
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

    if (isDemoHardwareSuppressed()) {
        g_servoMove.active = false;
        currentFraction = fraction;
        currentAngle = fractionAngles[idx];
        return;
    }

    uint8_t targetAngle = fractionAngles[idx];

    const char* names[] = {
        FRACTION_NAME_HEADS,
        FRACTION_NAME_SUBHEADS,
        FRACTION_NAME_BODY,
        FRACTION_NAME_PRETAILS,
        FRACTION_NAME_TAILS
    };

    LOG_I("Valves: Fractionator → %s (angle=%d)", names[idx], targetAngle);

    if (smooth && abs((int)targetAngle - (int)currentAngle) > 10) {
        // ARCH-3 fix: запускаем неблокирующий движок через update()
        // Скорость: ~60° за 1 секунду
        uint8_t angleDist = (uint8_t)abs((int)targetAngle - (int)currentAngle);
        g_servoMove.active = true;
        g_servoMove.startAngle = currentAngle;
        g_servoMove.targetAngle = targetAngle;
        g_servoMove.startTime = millis();
        g_servoMove.totalMs = (uint32_t)angleDist * 1000UL / 60UL + 200UL; // +200ms запас
    } else {
        // Быстрое перемещение
        fractionatorServo.write(targetAngle);
        g_servoMove.active = false;
    }

    currentAngle = targetAngle;
    currentFraction = fraction;
}

void setFractionAngle(uint8_t angle) {
    if (!fractionatorEnabled) return;

    if (angle > 180) angle = 180;

    if (isDemoHardwareSuppressed()) {
        g_servoMove.active = false;
        currentFraction = Fraction::UNKNOWN;
        currentAngle = angle;
        return;
    }

    g_servoMove.active = false;
    fractionatorServo.write(angle);
    currentAngle = angle;
    currentFraction = Fraction::UNKNOWN;

    LOG_D("Valves: Fractionator angle set to %d", angle);
}

Fraction getCurrentFraction() {
    return currentFraction;
}

uint8_t getFractionAngle() {
    if (!g_servoMove.active || g_servoMove.totalMs == 0) {
        return currentAngle;
    }

    const uint32_t elapsed = millis() - g_servoMove.startTime;
    if (elapsed >= g_servoMove.totalMs) {
        return g_servoMove.targetAngle;
    }

    const float progress = (float)elapsed / (float)g_servoMove.totalMs;
    const int interpolated =
        (int)g_servoMove.startAngle +
        (int)(progress * (float)((int)g_servoMove.targetAngle - (int)g_servoMove.startAngle));
    if (interpolated < 0) return 0;
    if (interpolated > 180) return 180;
    return (uint8_t)interpolated;
}

bool isServoMoving() {
    return g_servoMove.active;
}

bool isFractionatorEnabled() {
    return fractionatorEnabled;
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

// ARCH-3 fix: \u043d\u0435\u0431\u043b\u043e\u043a\u0438\u0440\u0443\u044e\u0449\u0435\u0435 \u043e\u0431\u043d\u043e\u0432\u043b\u0435\u043d\u0438\u0435 \u0441\u0435\u0440\u0432\u043e\u043f\u0440\u0438\u0432\u043e\u0434\u0430 (\u0432\u044b\u0437\u044b\u0432\u0430\u0442\u044c \u0438\u0437 loop \u043a\u0430\u0436\u0434\u0443\u044e \u0438\u0442\u0435\u0440\u0430\u0446\u0438\u044e)
void update() {
    for (size_t index = 0; index < 3; ++index) {
        ValvePulseState &pulseState = g_valvePulses[index];
        if (!pulseState.active) continue;

        const uint32_t elapsed = millis() - pulseState.startedAt;
        if (elapsed < pulseState.durationMs) continue;

        pulseState.active = false;
        pulseState.startedAt = 0;
        pulseState.durationMs = 0;
        setValveState(static_cast<ValveId>(index), false, false);
    }

    if (!g_servoMove.active || !fractionatorEnabled) return;

    if (isDemoHardwareSuppressed()) {
        g_servoMove.active = false;
        return;
    }

    uint32_t elapsed = millis() - g_servoMove.startTime;

    if (elapsed >= g_servoMove.totalMs) {
        // \u0414\u0432\u0438\u0436\u0435\u043d\u0438\u0435 \u0437\u0430\u0432\u0435\u0440\u0448\u0435\u043d\u043e
        fractionatorServo.write(g_servoMove.targetAngle);
        g_servoMove.active = false;
        LOG_D("Valves: Servo move done, angle=%d", g_servoMove.targetAngle);
    } else {
        // \u041b\u0438\u043d\u0435\u0439\u043d\u0430\u044f \u0438\u043d\u0442\u0435\u0440\u043f\u043e\u043b\u044f\u0446\u0438\u044f \u0443\u0433\u043b\u0430 \u0441\u0435\u0440\u0432\u043e\u043f\u0440\u0438\u0432\u043e\u0434\u0430
        float progress = (float)elapsed / (float)g_servoMove.totalMs;
        int newAngle = (int)g_servoMove.startAngle +
                       (int)(progress * (float)((int)g_servoMove.targetAngle - (int)g_servoMove.startAngle));
        fractionatorServo.write(newAngle);
    }
}

} // namespace Valves
