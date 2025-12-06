/**
 * Smart-Column S3 - Драйвер нагревателя
 *
 * ШИМ управление SSR-40DA через оптрон PC817
 * Медленный ШИМ (1 Гц) для твердотельного реле
 */

#include "heater.h"

// =============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// =============================================================================

static uint8_t currentPower = 0;    // Текущая мощность 0-100%
static uint8_t targetPower = 0;     // Целевая мощность (для ramp)
static uint32_t rampStartTime = 0;
static uint32_t rampDuration = 0;
static bool ramping = false;

// =============================================================================
// ПУБЛИЧНЫЙ ИНТЕРФЕЙС
// =============================================================================

namespace Heater {

void init() {
    LOG_I("Heater: Initializing...");

    // Настройка LEDC ШИМ
    ledcSetup(LEDC_CHANNEL_HEATER, PWM_FREQ_HEATER, PWM_RESOLUTION);
    ledcAttachPin(PIN_SSR_HEATER, LEDC_CHANNEL_HEATER);

    // Начальное состояние - выключено
    ledcWrite(LEDC_CHANNEL_HEATER, 0);
    currentPower = 0;

    LOG_I("Heater: Init complete (PWM %d Hz, %d-bit)",
          PWM_FREQ_HEATER, PWM_RESOLUTION);
}

void setPower(uint8_t percent) {
    // Ограничить диапазон
    if (percent > 100) percent = 100;

    currentPower = percent;
    ramping = false;

    // Преобразовать проценты в ШИМ (0-255)
    uint8_t duty = map(percent, 0, 100, 0, 255);
    ledcWrite(LEDC_CHANNEL_HEATER, duty);

    LOG_D("Heater: Power set to %d%% (duty=%d)", percent, duty);
}

uint8_t getPower() {
    return currentPower;
}

void emergencyStop() {
    LOG_I("Heater: EMERGENCY STOP!");
    ledcWrite(LEDC_CHANNEL_HEATER, 0);
    currentPower = 0;
    targetPower = 0;
    ramping = false;
}

void rampTo(uint8_t targetPercent, uint32_t rampTimeMs) {
    if (targetPercent > 100) targetPercent = 100;

    LOG_I("Heater: Ramping from %d%% to %d%% over %lu ms",
          currentPower, targetPercent, rampTimeMs);

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
    if (currentPower > 10) {
        // Вычислить ожидаемую мощность (например, от ТЭНа 3 кВт)
        float expectedPower = (currentPower / 100.0f) * DEFAULT_HEATER_POWER_W;

        // Допуск ±20%
        float tolerance = expectedPower * 0.2f;

        if (actualPower < (expectedPower - tolerance)) {
            LOG_E("Heater: Health check FAIL! Expected %.0fW, got %.0fW",
                  expectedPower, actualPower);
            return false;
        }
    }

    return true;
}

// Обновление плавного разгона (вызывать из основного loop)
void update() {
    if (!ramping) return;

    uint32_t elapsed = millis() - rampStartTime;

    if (elapsed >= rampDuration) {
        // Разгон завершён
        setPower(targetPower);
        ramping = false;
    } else {
        // Интерполяция
        float progress = (float)elapsed / (float)rampDuration;
        uint8_t newPower = currentPower + progress * (targetPower - currentPower);
        setPower(newPower);
    }
}

} // namespace Heater
