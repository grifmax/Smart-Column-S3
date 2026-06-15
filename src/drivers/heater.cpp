/**
 * Smart-Column S3 - Драйвер нагревателя
 *
 * ШИМ управление SSR-40DA через оптрон PC817
 * Медленный ШИМ (1 Гц) для твердотельного реле
 */

#include "heater.h"

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
#include "driver/gpio.h"
#include "esp32-hal-timer.h"
#endif

#include <math.h>

// =============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// =============================================================================

static volatile uint8_t currentPower = 0;    // Текущая мощность 0-100%
static uint16_t targetPowerWatts = 0;
static uint8_t targetPower = 0;     // Целевая мощность (для ramp)
static uint8_t rampStartPower = 0;  // BUG-2 fix: начальная мощность для линейной интерполяции
static uint32_t rampStartTime = 0;
static uint32_t rampDuration = 0;
static bool ramping = false;
static bool boosterEnabled = false;

static bool isDemoHardwareSuppressed() {
    return g_settings.demoMode;
}

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
// Переменные для симистора
static hw_timer_t* triac_timer = nullptr;
static volatile uint16_t triac_delay_us = TRIAC_MAX_ALPHA_US; // Задержка отпирания по умолчанию (почти полный off)
static volatile uint32_t zero_cross_count = 0;
static volatile uint32_t triac_fire_count = 0;
static volatile uint32_t last_zero_cross_us = 0;
static int32_t triac_feedback_trim_us = 0;
static uint32_t last_feedback_update_ms = 0;
static float last_feedback_error_w = 0.0f;
static bool closed_loop_active = false;
#endif

namespace {

constexpr float TRIAC_FEEDBACK_TOLERANCE_PCT = 0.05f;
constexpr float TRIAC_FEEDBACK_TOLERANCE_W = 35.0f;
constexpr float TRIAC_FEEDBACK_SAMPLE_MAX_AGE_MS = 2500.0f;
constexpr float TRIAC_FEEDBACK_KP_US_PER_W = 0.9f;
constexpr int32_t TRIAC_FEEDBACK_MAX_STEP_US = 220;
constexpr int32_t TRIAC_FEEDBACK_TRIM_LIMIT_US = 2800;

void writeBooster(bool enabled) {
    digitalWrite(PIN_SSR_HEATER, enabled ? HIGH : LOW);
}

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
inline void IRAM_ATTR fireTriacPulse() {
    triac_fire_count++;
    gpio_set_level((gpio_num_t)PIN_TRIAC, 1);
    esp_rom_delay_us(TRIAC_PULSE_WIDTH_US);
    gpio_set_level((gpio_num_t)PIN_TRIAC, 0);
}

uint8_t wattsToPercent(uint16_t watts) {
    const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
        ? g_settings.equipment.heaterPowerW
        : DEFAULT_HEATER_POWER_W;
    if (heaterMaxW == 0) return 0;
    const uint32_t scaled =
        (static_cast<uint32_t>(watts) * 100U + heaterMaxW / 2U) / heaterMaxW;
    return static_cast<uint8_t>(scaled > 100U ? 100U : scaled);
}

uint16_t percentToWatts(uint8_t percent) {
    const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
        ? g_settings.equipment.heaterPowerW
        : DEFAULT_HEATER_POWER_W;
    return static_cast<uint16_t>((static_cast<uint32_t>(heaterMaxW) * percent) / 100U);
}

uint16_t clampTargetWatts(uint16_t watts) {
    const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
        ? g_settings.equipment.heaterPowerW
        : DEFAULT_HEATER_POWER_W;
    return watts > heaterMaxW ? heaterMaxW : watts;
}

void syncPercentFromTargetWatts() {
    currentPower = wattsToPercent(targetPowerWatts);
}

uint16_t calculateTriacDelayForWatts(uint16_t watts, float currentVoltage) {
    if (watts == 0) return TRIAC_MAX_ALPHA_US;
    const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
        ? g_settings.equipment.heaterPowerW
        : DEFAULT_HEATER_POWER_W;
    if (watts >= heaterMaxW) return TRIAC_MIN_ALPHA_US;

    const float resistance = (230.0f * 230.0f) / heaterMaxW;
    const float voltage = currentVoltage > 10.0f ? currentVoltage : 230.0f;
    const float maxPossiblePowerW = (voltage * voltage) / resistance;
    if (watts >= maxPossiblePowerW) {
        return TRIAC_MIN_ALPHA_US;
    }

    float powerRatio = static_cast<float>(watts) / maxPossiblePowerW;
    if (powerRatio < 0.0f) powerRatio = 0.0f;
    if (powerRatio > 1.0f) powerRatio = 1.0f;

    const float delayUs = 10000.0f * acosf(2.0f * powerRatio - 1.0f) / PI;
    if (delayUs > TRIAC_MAX_ALPHA_US) return TRIAC_MAX_ALPHA_US;
    if (delayUs < TRIAC_MIN_ALPHA_US) return TRIAC_MIN_ALPHA_US;
    return static_cast<uint16_t>(delayUs);
}

void applyTriacTarget() {
    if (targetPowerWatts == 0) {
        triac_delay_us = TRIAC_MAX_ALPHA_US;
        triac_feedback_trim_us = 0;
        closed_loop_active = false;
        last_feedback_error_w = 0.0f;
        return;
    }

    const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
        ? g_settings.equipment.heaterPowerW
        : DEFAULT_HEATER_POWER_W;
    if (targetPowerWatts >= heaterMaxW) {
        triac_delay_us = TRIAC_MIN_ALPHA_US;
        triac_feedback_trim_us = 0;
        closed_loop_active = false;
        last_feedback_error_w = 0.0f;
        return;
    }

    const float voltage = g_state.power.voltage > 10.0f ? g_state.power.voltage : 230.0f;
    const uint16_t baseDelayUs = calculateTriacDelayForWatts(targetPowerWatts, voltage);
    const int32_t correctedDelayUs = static_cast<int32_t>(baseDelayUs) + triac_feedback_trim_us;
    triac_delay_us = static_cast<uint16_t>(
        correctedDelayUs < TRIAC_MIN_ALPHA_US ? TRIAC_MIN_ALPHA_US :
        correctedDelayUs > TRIAC_MAX_ALPHA_US ? TRIAC_MAX_ALPHA_US :
        correctedDelayUs
    );
}

void resetFeedbackState() {
    triac_feedback_trim_us = 0;
    last_feedback_update_ms = 0;
    last_feedback_error_w = 0.0f;
    closed_loop_active = false;
}
#endif

} // namespace

// =============================================================================
// ОБРАБОТЧИКИ ПРЕРЫВАНИЙ (ISR) ДЛЯ TRIAC
// =============================================================================

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC

// One-shot таймер открывает симистор в нужной точке полупериода
static void IRAM_ATTR triac_timer_alarm_isr() {
    if (triac_timer != nullptr) {
        timerAlarmDisable(triac_timer);
    }
    fireTriacPulse();
}

// Обработчик прерывания детектора Zero-Cross
static void IRAM_ATTR zero_cross_isr_handler() {
    const uint32_t nowUs = micros();
    if (last_zero_cross_us != 0 &&
        (nowUs - last_zero_cross_us) < TRIAC_ZERO_CROSS_LOCKOUT_US) {
        return;
    }
    last_zero_cross_us = nowUs;
    zero_cross_count++;

    // Выключаем симистор на всякий случай
    gpio_set_level((gpio_num_t)PIN_TRIAC, 0);

    if (triac_timer == nullptr) return;

    if (currentPower == 0 || triac_delay_us >= TRIAC_MAX_ALPHA_US) {
        // Мощность 0, ничего не делаем, симистор останется закрытым
        return;
    }

    if (currentPower == 100 && triac_delay_us <= TRIAC_MIN_ALPHA_US) {
        // Мощность 100%, открываем симистор сразу
        fireTriacPulse();
        return;
    }

    // Заряжаем one-shot таймер от начала текущего полупериода
    timerAlarmDisable(triac_timer);
    timerWrite(triac_timer, 0);
    timerAlarmWrite(triac_timer, triac_delay_us, false);
    timerAlarmEnable(triac_timer);
}

#endif

// =============================================================================
// ПУБЛИЧНЫЙ ИНТЕРФЕЙС
// =============================================================================

namespace Heater {

void init() {
    LOG_I("Heater: Initializing...");

    pinMode(PIN_SSR_HEATER, OUTPUT);
    writeBooster(false);
    boosterEnabled = false;

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
    LOG_I("Heater: Mode = TRIAC (Phase Control)");
    
    // 1. Настройка PIN_TRIAC как выход
    gpio_config_t out_conf = {};
    out_conf.intr_type = GPIO_INTR_DISABLE;
    out_conf.mode = GPIO_MODE_OUTPUT;
    out_conf.pin_bit_mask = (1ULL << PIN_TRIAC);
    out_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    out_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&out_conf);
    gpio_set_level((gpio_num_t)PIN_TRIAC, 0);

    // 2. Инициализация hardware timer (80 MHz / 80 = 1 МГц, 1 тик = 1 мкс)
    triac_timer = timerBegin(0, 80, true);
    if (triac_timer == nullptr) {
        LOG_E("Heater: TRIAC timer init failed");
    } else {
        timerAttachInterrupt(triac_timer, &triac_timer_alarm_isr, true);
        timerAlarmWrite(triac_timer, TRIAC_MAX_ALPHA_US, false);
        timerAlarmDisable(triac_timer);
    }

    // 3. Настройка PIN_ZERO_CROSS и прерывания
    pinMode(PIN_ZERO_CROSS, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ZERO_CROSS), zero_cross_isr_handler, CHANGE);
    zero_cross_count = 0;
    triac_fire_count = 0;
    last_zero_cross_us = 0;
    LOG_I("Heater: TRIAC pulse=%u us, zero-cross lockout=%u us",
          TRIAC_PULSE_WIDTH_US, TRIAC_ZERO_CROSS_LOCKOUT_US);

#else
    LOG_I("Heater: Mode = SSR (Slow PWM)");
    // Настройка LEDC ШИМ
    ledcSetup(LEDC_CHANNEL_HEATER, PWM_FREQ_HEATER, PWM_RESOLUTION);
    ledcAttachPin(PIN_SSR_HEATER, LEDC_CHANNEL_HEATER);

    // Начальное состояние - выключено
    ledcWrite(LEDC_CHANNEL_HEATER, 0);
#endif

    currentPower = 0;
    targetPowerWatts = 0;
    targetPower = 0;
    ramping = false;
#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
    resetFeedbackState();
#endif
    LOG_I("Heater: Init complete");
}

void setPower(uint8_t percent) {
    // Ограничить диапазон
    if (percent > 100) percent = 100;
    setPowerWatts(percentToWatts(percent));
}

void setPowerWatts(uint16_t watts) {
    targetPowerWatts = clampTargetWatts(watts);
    syncPercentFromTargetWatts();
    ramping = false;
    if (isDemoHardwareSuppressed()) {
        return;
    }

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
    const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
        ? g_settings.equipment.heaterPowerW
        : DEFAULT_HEATER_POWER_W;
    if (targetPowerWatts == 0 || targetPowerWatts >= heaterMaxW) {
        resetFeedbackState();
    }
    applyTriacTarget();
    LOG_D("Heater: Target set to %u W (%u%%, delay=%u us)",
          static_cast<unsigned>(targetPowerWatts),
          static_cast<unsigned>(currentPower),
          static_cast<unsigned>(triac_delay_us));
#else
    const uint16_t heaterMaxW = g_settings.equipment.heaterPowerW > 0
        ? g_settings.equipment.heaterPowerW
        : DEFAULT_HEATER_POWER_W;
    const uint8_t duty = heaterMaxW > 0
        ? static_cast<uint8_t>((static_cast<uint32_t>(targetPowerWatts) * 255U) / heaterMaxW)
        : 0;
    ledcWrite(LEDC_CHANNEL_HEATER, duty);
    LOG_D("Heater: Target set to %u W (%u%%, duty=%u)",
          static_cast<unsigned>(targetPowerWatts),
          static_cast<unsigned>(currentPower),
          static_cast<unsigned>(duty));
#endif
}

void setTriacDelay(uint16_t delayUs) {
#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
    if (delayUs > TRIAC_MAX_ALPHA_US) delayUs = TRIAC_MAX_ALPHA_US;
    if (delayUs < TRIAC_MIN_ALPHA_US) delayUs = TRIAC_MIN_ALPHA_US;
    triac_delay_us = delayUs;
#endif
}

uint8_t getPower() {
    return currentPower;
}

uint16_t getTargetPowerWatts() {
    return targetPowerWatts;
}

void setBoosterEnabled(bool enabled) {
    boosterEnabled = enabled;
    if (isDemoHardwareSuppressed()) {
        return;
    }
    writeBooster(enabled);
}

bool isBoosterEnabled() {
    return boosterEnabled;
}

Diagnostics getDiagnostics() {
    Diagnostics diag;
    diag.mainPowerPercent = currentPower;
    diag.powerSetPercent = currentPower;
    diag.targetPowerWatts = targetPowerWatts;
    diag.actualPowerWatts = g_state.power.power;
    diag.powerErrorWatts = targetPowerWatts > 0 ? (static_cast<float>(targetPowerWatts) - g_state.power.power) : 0.0f;
    diag.boosterEnabled = boosterEnabled;
    diag.active = targetPowerWatts > 0 || boosterEnabled;
#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
    diag.closedLoopActive = closed_loop_active;
    diag.zeroCrossCount = zero_cross_count;
    diag.zeroCrossSeen = zero_cross_count > 0;
    diag.triacFireCount = triac_fire_count;
    diag.triacDelayUs = triac_delay_us;
#endif
    return diag;
}

void emergencyStop() {
    LOG_I("Heater: EMERGENCY STOP!");
    if (isDemoHardwareSuppressed()) {
        currentPower = 0;
        targetPowerWatts = 0;
        targetPower = 0;
        ramping = false;
        boosterEnabled = false;
        return;
    }
#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
    triac_delay_us = TRIAC_MAX_ALPHA_US;
    resetFeedbackState();
    if (triac_timer != nullptr) {
        timerAlarmDisable(triac_timer);
    }
    gpio_set_level((gpio_num_t)PIN_TRIAC, 0);
    last_zero_cross_us = 0;
#else
    ledcWrite(LEDC_CHANNEL_HEATER, 0);
#endif
    writeBooster(false);
    boosterEnabled = false;
    currentPower = 0;
    targetPowerWatts = 0;
    targetPower = 0;
    ramping = false;
}

void rampTo(uint8_t targetPercent, uint32_t rampTimeMs) {
    if (targetPercent > 100) targetPercent = 100;

    LOG_I("Heater: Ramping from %d%% to %d%% over %lu ms",
          currentPower, targetPercent, rampTimeMs);

    rampStartPower = currentPower;  // BUG-2 fix: сохраняем стартовую мощность
    targetPower = targetPercent;
    rampStartTime = millis();
    rampDuration = rampTimeMs;
    ramping = true;

    // Если уже нужная мощность - сразу установить
    if (currentPower == targetPower) {
        ramping = false;
    }
}

bool checkHealth(float actualPower) {
    // Если задана мощность > 10%, проверяем реальную
    if (targetPowerWatts > 100) {
        // Допуск ±20%
        float expectedPower = static_cast<float>(targetPowerWatts);

        float tolerance = expectedPower * 0.2f;

        if (actualPower < (expectedPower - tolerance)) {
            LOG_E("Heater: Health check FAIL! Expected %.0fW, got %.0fW",
                  expectedPower, actualPower);
            return false;
        }
    }

    return true;
}

void update() {
    if (ramping) {
        uint32_t elapsed = millis() - rampStartTime;

        if (elapsed >= rampDuration) {
            // Разгон завершён
            setPower(targetPower);
            ramping = false;
        } else {
            // Линейная интерполяция от rampStartPower до targetPower
            // BUG-2 fix: используем rampStartPower вместо currentPower
            float progress = (float)elapsed / (float)rampDuration;
            uint8_t newPower = rampStartPower + (uint8_t)(progress * (float)(targetPower - rampStartPower));
            // Напрямую обновляем, не вызывая setPower, чтобы не сбросить ramping=false
            currentPower = newPower;
            targetPowerWatts = percentToWatts(newPower);
            if (isDemoHardwareSuppressed()) {
                return;
            }

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
            applyTriacTarget();
#else
            uint8_t duty = map(newPower, 0, 100, 0, 255);
            ledcWrite(LEDC_CHANNEL_HEATER, duty);
#endif
        }
    }

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
    if (targetPowerWatts == 0 || currentPower == 0 || currentPower >= 100) {
        closed_loop_active = false;
        return;
    }

    const bool pzemFresh = g_state.health.pzemOk &&
        g_state.power.lastUpdate > 0 &&
        (millis() - g_state.power.lastUpdate) <= TRIAC_FEEDBACK_SAMPLE_MAX_AGE_MS;

    if (!pzemFresh) {
        closed_loop_active = false;
        applyTriacTarget();
        return;
    }

    if (g_state.power.lastUpdate == last_feedback_update_ms) {
        return;
    }
    last_feedback_update_ms = g_state.power.lastUpdate;

    const float actualPower = g_state.power.power;
    const float errorW = static_cast<float>(targetPowerWatts) - actualPower;
    last_feedback_error_w = errorW;

    const float toleranceW = fmaxf(TRIAC_FEEDBACK_TOLERANCE_W,
                                   static_cast<float>(targetPowerWatts) * TRIAC_FEEDBACK_TOLERANCE_PCT);
    if (fabsf(errorW) <= toleranceW) {
        closed_loop_active = true;
        applyTriacTarget();
        return;
    }

    int32_t stepUs = static_cast<int32_t>(roundf(-errorW * TRIAC_FEEDBACK_KP_US_PER_W));
    if (stepUs > TRIAC_FEEDBACK_MAX_STEP_US) stepUs = TRIAC_FEEDBACK_MAX_STEP_US;
    if (stepUs < -TRIAC_FEEDBACK_MAX_STEP_US) stepUs = -TRIAC_FEEDBACK_MAX_STEP_US;

    triac_feedback_trim_us += stepUs;
    if (triac_feedback_trim_us > TRIAC_FEEDBACK_TRIM_LIMIT_US) {
        triac_feedback_trim_us = TRIAC_FEEDBACK_TRIM_LIMIT_US;
    }
    if (triac_feedback_trim_us < -TRIAC_FEEDBACK_TRIM_LIMIT_US) {
        triac_feedback_trim_us = -TRIAC_FEEDBACK_TRIM_LIMIT_US;
    }

    closed_loop_active = true;
    applyTriacTarget();
#endif
}

} // namespace Heater
