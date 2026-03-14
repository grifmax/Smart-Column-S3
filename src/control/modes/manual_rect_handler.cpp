#include "../fsm_utils.h"
#include "../../drivers/heater.h"
#include "../../drivers/pump.h"
#include "../../drivers/valves.h"
#include "../watt_control.h"
#include "../../interface/mqtt.h"
#include "../../storage/logger.h"
#include <Arduino.h>

namespace FSM {
namespace ManualRect {

static bool alertSent = false;
static uint32_t lastFloodTime = 0;

void update(SystemState& state, const Settings& settings) {
    if (state.temps.valid[TEMP_CUBE] && state.temps.cube >= getWaterAutoStartTempC(settings)) {
        if (!Valves::getWater()) Valves::setWater(true);
    }

    float floodP = WattControl::calculateFloodPressure(settings.equipment.columnHeightMm, settings.equipment.packingCoeff);
    float critP = floodP * PRESSURE_CRIT_MULT;

    if (state.pressure.ok && state.pressure.cube >= critP && (millis() - lastFloodTime > 5000)) {
        lastFloodTime = millis();
        uint8_t power = Heater::getPower();
        uint8_t newP = (uint8_t)(power * 0.85f);
        if (newP < 30) newP = 30;
        Heater::setPower(newP);
        MQTT::publishNotification("Захлёб!", "Давление критическое! Мощность ТЭНа снижена.", "warning");
    }

    float collected = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
    if (collected < 0.0f) collected = 0.0f;

    switch (state.rectPhase) {
        case RectPhase::HEADS:
            state.stats.headsVolume = collected;
            if (state.stats.headsVolume > 0 && !alertSent) { /* Логика уведомлений */ }
            break;
        case RectPhase::BODY:
            state.stats.bodyVolume = collected;
            break;
        case RectPhase::TAILS:
            state.stats.tailsVolume = collected;
            break;
        default: break;
    }
}

void setPhase(SystemState& state, RectPhase phase) {
    state.rectPhase = phase;
    setPhaseStartVolumeMl(state.pump.totalVolumeMl);
    setPhaseStartTime(millis());
    alertSent = false;
    
    const char* phaseName = "Unknown";
    switch(phase) {
        case RectPhase::IDLE: phaseName = "Ожидание"; break;
        case RectPhase::HEATING: phaseName = "Нагрев"; break;
        case RectPhase::STABILIZATION: phaseName = "Стабилизация"; break;
        case RectPhase::HEADS: phaseName = "Головы"; break;
        case RectPhase::BODY: phaseName = "Тело"; break;
        case RectPhase::TAILS: phaseName = "Хвосты"; break;
        case RectPhase::FINISH: phaseName = "Завершено"; break;
    }
    
    LOG_I("ManualRect: phase changed to %s", phaseName);
    Logger::logf(0, "ManualRect: Фаза изменена на %s", phaseName);
}

} // namespace ManualRect
} // namespace FSM
