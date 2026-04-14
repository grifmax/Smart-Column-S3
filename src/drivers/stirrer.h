/**
 * Smart-Column S3 - Драйвер мешалки куба (0-10В через MCP4725 + MCP6001)
 *
 * MCP4725 — 12-бит DAC, I2C-адрес 0x60 (или 0x61).
 * MCP4725 VOUT 0–3.3В → MCP6001 (K=3) → 0–10В → управление регулятором мешалки.
 */

#ifndef STIRRER_H
#define STIRRER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace Stirrer {

    /**
     * Инициализация DAC MCP4725
     * @return true если инициализация успешна
     */
    bool init();

    /**
     * Установить скорость мешалки
     * @param percent 0–100%
     */
    void setSpeed(uint8_t percent);

    /**
     * Запустить мешалку
     * @param percent Скорость 0–100% (0 = использует значение по умолчанию из настроек)
     */
    void start(uint8_t percent = 0);

    /**
     * Немедленно остановить мешалку (вывод 0В)
     */
    void stop();

    /**
     * @return true если мешалка работает
     */
    bool isRunning();

    /**
     * @return Текущая скорость 0–100%
     */
    uint8_t getSpeed();

    /**
     * @return true если MCP4725 найден и инициализирован
     */
    bool isAvailable();

    /**
     * Обновление состояния g_state.stirrer (вызывать из loop)
     */
    void syncState(SystemState& state);

} // namespace Stirrer

#endif // STIRRER_H
