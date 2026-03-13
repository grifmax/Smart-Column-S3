#include "../fsm_utils.h"
#include "../../drivers/heater.h"
#include "../../drivers/pump.h"
#include "../../drivers/valves.h"
#include "../../storage/logger.h"
#include <Arduino.h>

namespace FSM {
namespace Distillation {

struct ParamsRuntime {
    float speedMlH = 500.0f;
    float headsVolumeMl = 0.0f;
    float targetVolumeMl = 0.0f;
    float endTempC = 96.0f;
    uint8_t powerPercent = 100;
};

static ParamsRuntime g_params;

void setParams(float speedMlH, float headsVolumeMl, float targetVolumeMl, float endTempC) {
    if (speedMlH > 0) g_params.speedMlH = speedMlH;
    if (headsVolumeMl >= 0) g_params.headsVolumeMl = headsVolumeMl;
    if (targetVolumeMl >= 0) g_params.targetVolumeMl = targetVolumeMl;
    if (endTempC > 0) g_params.endTempC = endTempC;

    LOG_I("Distillation params: speed=%.0f ml/h, heads=%.0f ml, target=%.0f ml, endTemp=%.1fC, power=%u%%",
          g_params.speedMlH, g_params.headsVolumeMl, g_params.targetVolumeMl, g_params.endTempC, g_params.powerPercent);
}

void setPowerPercent(uint8_t powerPercent) {
    if (powerPercent > 100) powerPercent = 100;
    g_params.powerPercent = powerPercent;
}

void getParams(float& speedMlH, float& headsVolumeMl, float& targetVolumeMl, float& endTempC, uint8_t& powerPercent) {
    speedMlH = g_params.speedMlH;
    headsVolumeMl = g_params.headsVolumeMl;
    targetVolumeMl = g_params.targetVolumeMl;
    endTempC = g_params.endTempC;
    powerPercent = g_params.powerPercent;
}

void update(SystemState& state, const Settings& settings) {
    uint32_t now = millis();
    uint32_t startTime = getPhaseStartTime();

    if (state.temps.cube >= getWaterAutoStartTempC(settings)) {
        Valves::setWater(true);
    }

    switch (state.rectPhase) {
        case RectPhase::HEATING:
            Heater::setPower(g_params.powerPercent);
            if (state.temps.valid[TEMP_CUBE] && state.temps.cube >= 78.0f) {
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(state.pump.totalVolumeMl);
                if (g_params.headsVolumeMl > 0.0f) {
                    state.rectPhase = RectPhase::HEADS;
                    LOG_I("Distillation: HEATING -> HEADS");
                } else {
                    state.rectPhase = RectPhase::BODY;
                    LOG_I("Distillation: HEATING -> BODY");
                }
            }
            break;

        case RectPhase::HEADS: {
            Heater::setPower(g_params.powerPercent);
            Pump::start(g_params.speedMlH);
            Valves::setHeads(true);
            const float collected = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
            state.stats.headsVolume = collected;
            if (collected >= g_params.headsVolumeMl) {
                Valves::setHeads(false);
                setPhaseStartTime(now);
                setPhaseStartVolumeMl(state.pump.totalVolumeMl);
                state.rectPhase = RectPhase::BODY;
                LOG_I("Distillation: HEADS -> BODY");
            }
            break;
        }

        case RectPhase::BODY: {
            Heater::setPower(g_params.powerPercent);
            Valves::setHeads(false);
            Pump::start(g_params.speedMlH);
            const float collectedBody = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
            state.stats.bodyVolume = collectedBody;
            const bool endByTemp = (g_params.endTempC > 0.0f && state.temps.valid[TEMP_CUBE] && state.temps.cube >= g_params.endTempC);
            const bool endByVolume = (g_params.targetVolumeMl > 0.0f && state.pump.totalVolumeMl >= g_params.targetVolumeMl);
            if (endByTemp || endByVolume) {
                setPhaseStartTime(now);
                state.rectPhase = RectPhase::FINISH;
                LOG_I("Distillation: BODY -> FINISH (%s%s)", endByTemp ? "temp" : "", endByVolume ? " volume" : "");
            }
            break;
        }

        case RectPhase::FINISH:
            Heater::setPower(0);
            Pump::stop();
            Valves::setHeads(false);
            Valves::setWater(true);
            if (now - startTime > 5 * 60 * 1000UL) {
                Valves::closeAll();
                state.rectPhase = RectPhase::IDLE;
                state.mode = Mode::IDLE;
                LOG_I("Distillation: Process complete!");
            }
            break;
        default:
            state.rectPhase = RectPhase::BODY;
            setPhaseStartTime(now);
            setPhaseStartVolumeMl(state.pump.totalVolumeMl);
            break;
    }
}

} // namespace Distillation
} // namespace FSM
