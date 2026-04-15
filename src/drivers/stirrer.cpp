/**
 * Smart-Column S3 - Драйвер мешалки куба
 *
 * Управление через MCP4725 (12-бит DAC, I2C).
 * Шкала: 0% → 0В, 100% → 3.3В (после ОУ ×3 = ~10В)
 * DAC: 12 бит = 0..4095, VCC=3.3В → 3.3В / 4096 ≈ 0.806 мВ/шаг
 */

#include "stirrer.h"
#include <Adafruit_MCP4725.h>

// I2C адрес MCP4725 (A0=GND → 0x60, A0=VCC → 0x61)
#ifndef I2C_ADDR_MCP4725
  #define I2C_ADDR_MCP4725 0x60
#endif

// Максимальное значение DAC (12 бит)
#define DAC_MAX_VALUE 4095

namespace Stirrer {

static Adafruit_MCP4725 s_dac;
static bool   s_available = false;
static bool   s_running   = false;
static uint8_t s_speedPct = 0;

// Перевод процентов (0-100) в значение DAC (0-4095)
static inline uint16_t percentToDac(uint8_t pct) {
    if (pct == 0)   return 0;
    if (pct >= 100) return DAC_MAX_VALUE;
    return (uint16_t)((uint32_t)pct * DAC_MAX_VALUE / 100);
}

bool init() {
    // Пробуем инициализировать MCP4725
    if (!s_dac.begin(I2C_ADDR_MCP4725)) {
        LOG_W("Stirrer: MCP4725 not found at 0x%02X", I2C_ADDR_MCP4725);
        s_available = false;
        return false;
    }
    // Выход 0 при старте
    s_dac.setVoltage(0, false);
    s_available = true;
    s_running   = false;
    s_speedPct  = 0;
    LOG_I("Stirrer: MCP4725 OK, addr=0x%02X", I2C_ADDR_MCP4725);
    return true;
}

void setSpeed(uint8_t percent) {
    if (!s_available) return;
    if (percent > 100) percent = 100;

    if (percent == 0) {
        s_running = false;
        s_speedPct = 0;
        s_dac.setVoltage(0, false);
        LOG_D("Stirrer: setSpeed 0%% → output off");
        return;
    }

    s_speedPct = percent;
    if (!s_running) {
        LOG_D("Stirrer: speed preset to %u%% while stopped", percent);
        return;
    }

    uint16_t dacVal = percentToDac(percent);
    s_dac.setVoltage(dacVal, false);
    LOG_D("Stirrer: setSpeed %u%% → DAC %u", percent, dacVal);
}

void start(uint8_t percent) {
    if (!s_available) return;
    if (percent == 0) percent = (s_speedPct > 0) ? s_speedPct : 50; // fallback: last speed or 50%
    s_running = true;
    setSpeed(percent);
    LOG_I("Stirrer: started at %u%%", percent);
}

void stop() {
    if (!s_available) return;
    s_running  = false;
    s_speedPct = 0;
    s_dac.setVoltage(0, false);
    LOG_I("Stirrer: stopped");
}

bool isRunning() {
    return s_running && s_available;
}

uint8_t getSpeed() {
    return s_running ? s_speedPct : 0;
}

bool isAvailable() {
    return s_available;
}

void syncState(SystemState& state) {
    state.stirrer.running      = s_running && s_available;
    state.stirrer.speedPercent = s_running ? s_speedPct : 0;
    state.stirrer.available    = s_available;
    state.stirrer.lastUpdate   = millis();
}

} // namespace Stirrer
