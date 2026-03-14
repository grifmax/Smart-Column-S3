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
static uint8_t rampStartPower = 0;  // BUG-2 fix: начальная мощность для линейной интерполяции
static uint32_t rampStartTime = 0;
static uint32_t rampDuration = 0;
static bool ramping = false;

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
// Переменные для симистора
static gptimer_handle_t triac_timer = NULL;
static volatile uint16_t triac_delay_us = TRIAC_MAX_ALPHA_US; // Задержка отпирания по умолчанию (почти полный off)
#endif

// =============================================================================
// ОБРАБОТЧИКИ ПРЕРЫВАНИЙ (ISR) ДЛЯ TRIAC
// =============================================================================

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC

// Callback когда таймер досчитал до нужной задержки (triac_delay_us)
static bool IRAM_ATTR triac_timer_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data) {
    // 1. Подаем открывающий импульс на симистор
    gpio_set_level((gpio_num_t)PIN_TRIAC, 1);
    
    // 2. Ждем несколько микросекунд (блокирующе в ISR, но очень коротко)
    esp_rom_delay_us(TRIAC_PULSE_WIDTH_US);
    
    // 3. Закрываем оптосимистор (сам симистор закроется при следующем переходе через нуль)
    gpio_set_level((gpio_num_t)PIN_TRIAC, 0);

    // 4. Останавливаем таймер до следующего zero-cross
    gptimer_stop(timer);

    return false; // Не будим high priority task
}

// Обработчик прерывания детектора Zero-Cross
static void IRAM_ATTR zero_cross_isr_handler(void* arg) {
    // Выключаем симистор на всякий случай
    gpio_set_level((gpio_num_t)PIN_TRIAC, 0);

    if (triac_timer == NULL) return;

    if (currentPower == 0 || triac_delay_us >= TRIAC_MAX_ALPHA_US) {
        // Мощность 0, ничего не делаем, симистор останется закрытым
        return;
    }

    if (currentPower == 100 && triac_delay_us <= TRIAC_MIN_ALPHA_US) {
        // Мощность 100%, открываем симистор сразу
        gpio_set_level((gpio_num_t)PIN_TRIAC, 1);
        return;
    }

    // Перезапускаем таймер с новой задержкой
    gptimer_stop(triac_timer);
    
    gptimer_alarm_config_t alarm_config = {};
    alarm_config.alarm_count = triac_delay_us;  // задержка в мкс
    alarm_config.reload_count = 0;
    alarm_config.flags.auto_reload_on_alarm = false;
    
    gptimer_set_alarm_action(triac_timer, &alarm_config);
    gptimer_set_raw_count(triac_timer, 0);
    gptimer_start(triac_timer);
}

#endif

// =============================================================================
// ПУБЛИЧНЫЙ ИНТЕРФЕЙС
// =============================================================================

namespace Heater {

void init() {
    LOG_I("Heater: Initializing...");

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

    // 2. Инициализация gptimer (1 МГц -> 1 тик = 1 мкс)
    gptimer_config_t timer_config = {};
    timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timer_config.direction = GPTIMER_COUNT_UP;
    timer_config.resolution_hz = 1000000; 
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &triac_timer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = triac_timer_alarm_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(triac_timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(triac_timer));

    // 3. Настройка PIN_ZERO_CROSS и прерывания
    gpio_config_t in_conf = {};
    in_conf.intr_type = GPIO_INTR_ANYEDGE; // Детектируем и фронт, и спад (каждый переход через 0)
    in_conf.mode = GPIO_MODE_INPUT;
    in_conf.pin_bit_mask = (1ULL << PIN_ZERO_CROSS);
    in_conf.pull_up_en = GPIO_PULLUP_ENABLE; 
    gpio_config(&in_conf);

    // Подключаем глобальный обработчик прерываний GPIO, если он еще не включен
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)PIN_ZERO_CROSS, zero_cross_isr_handler, NULL);

#else
    LOG_I("Heater: Mode = SSR (Slow PWM)");
    // Настройка LEDC ШИМ
    ledcSetup(LEDC_CHANNEL_HEATER, PWM_FREQ_HEATER, PWM_RESOLUTION);
    ledcAttachPin(PIN_SSR_HEATER, LEDC_CHANNEL_HEATER);

    // Начальное состояние - выключено
    ledcWrite(LEDC_CHANNEL_HEATER, 0);
#endif

    currentPower = 0;
    LOG_I("Heater: Init complete");
}

void setPower(uint8_t percent) {
    // Ограничить диапазон
    if (percent > 100) percent = 100;

    currentPower = percent;
    ramping = false;

#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
    // Для TRIAC: переводим проценты мощности (по умолчанию линейно) в угол
    // В будущем эта функция может быть переопределена или игнорироваться модулем watt_control
    // Линейная аппроксимация (только если не используется стабилизация напряжения):
    if (percent == 0) {
        triac_delay_us = TRIAC_MAX_ALPHA_US;
    } else if (percent == 100) {
        triac_delay_us = TRIAC_MIN_ALPHA_US;
    } else {
        // Обычная линейная интерполяция задержки от 10000 до 0
        triac_delay_us = map(percent, 0, 100, TRIAC_MAX_ALPHA_US, TRIAC_MIN_ALPHA_US);
    }
    LOG_D("Heater: Power set to %d%% (delay=%d us)", percent, triac_delay_us);
#else
    // Преобразовать проценты в ШИМ (0-255)
    uint8_t duty = map(percent, 0, 100, 0, 255);
    ledcWrite(LEDC_CHANNEL_HEATER, duty);
    LOG_D("Heater: Power set to %d%% (duty=%d)", percent, duty);
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

void emergencyStop() {
    LOG_I("Heater: EMERGENCY STOP!");
#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
    triac_delay_us = TRIAC_MAX_ALPHA_US;
    gpio_set_level((gpio_num_t)PIN_TRIAC, 0);
#else
    ledcWrite(LEDC_CHANNEL_HEATER, 0);
#endif
    currentPower = 0;
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

void update() {
    if (!ramping) return;

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
        
#if HEATER_CONTROL_MODE == HEATER_MODE_TRIAC
        triac_delay_us = map(newPower, 0, 100, TRIAC_MAX_ALPHA_US, TRIAC_MIN_ALPHA_US);
#else
        uint8_t duty = map(newPower, 0, 100, 0, 255);
        ledcWrite(LEDC_CHANNEL_HEATER, duty);
#endif
    }
}

} // namespace Heater
