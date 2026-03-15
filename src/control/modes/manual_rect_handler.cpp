#include "../fsm_utils.h"
#include "../../drivers/heater.h"
#include "../../drivers/pump.h"
#include "../../drivers/valves.h"
#include "../v2/reason_codes.h"
#include "../v2/safety_policy.h"
#include "../v2/status_adapter.h"
#include "../../interface/mqtt.h"
#include "../../storage/logger.h"
#include <Arduino.h>

namespace FSM {
namespace ManualRect {

namespace {

ControlV2::ReasonCodeV2 getPhaseTransitionReason(RectPhase fromPhase, RectPhase toPhase) {
    if (toPhase == RectPhase::HEATING && fromPhase == RectPhase::IDLE) {
        return ControlV2::ReasonCodeV2::RC_MODE_START_REQUEST;
    }
    if (toPhase == RectPhase::IDLE) {
        return ControlV2::ReasonCodeV2::RC_MANUAL_OPERATOR_STOP;
    }
    return ControlV2::ReasonCodeV2::RC_MANUAL_OPERATOR_SWITCH;
}

const char* getPhaseTransitionMessage(RectPhase fromPhase, RectPhase toPhase) {
    if (toPhase == RectPhase::HEATING && fromPhase == RectPhase::IDLE) {
        return "Manual rectification started";
    }
    switch (toPhase) {
        case RectPhase::HEADS: return "Manual switch to heads";
        case RectPhase::BODY: return "Manual switch to body";
        case RectPhase::TAILS: return "Manual switch to tails";
        case RectPhase::FINISH: return "Manual switch to finish";
        case RectPhase::IDLE: return "Manual rectification stopped by operator";
        default: return "Manual rectification phase switched";
    }
}

} // namespace

static bool alertSent = false;
static uint32_t lastFloodTime = 0;

void update(SystemState& state, const Settings& settings) {
    if (state.temps.valid[TEMP_CUBE] && state.temps.cube >= getWaterAutoStartTempC(settings)) {
        if (!Valves::getWater()) Valves::setWater(true);
    }

    const uint32_t now = millis();
    const ControlV2::ManualRectFloodPolicyV2 floodPolicy =
        ControlV2::SafetyPolicyV2::evaluateManualRectFloodPower(Heater::getPower(), state, settings, now,
                                                                lastFloodTime);

    if (floodPolicy.stepdownRecommended) {
        lastFloodTime = now;
        Heater::setPower(floodPolicy.appliedPowerPercent);
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
    const RectPhase previousPhase = state.rectPhase;
    if (state.mode == Mode::MANUAL_RECT && previousPhase != phase) {
        ControlV2::notePhaseTransition(Mode::MANUAL_RECT,
                                       static_cast<uint16_t>(previousPhase),
                                       static_cast<uint16_t>(phase),
                                       getPhaseTransitionReason(previousPhase, phase),
                                       getPhaseTransitionMessage(previousPhase, phase));
    }

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
