#include "fsm_utils.h"
#include <Arduino.h>
#include "watt_control.h"

namespace FSM {

static uint32_t phaseStartTime = 0;
static float phaseStartVolumeMl = 0.0f;

float clampFloat(float v, float vmin, float vmax) {
    if (v < vmin) return vmin;
    if (v > vmax) return vmax;
    return v;
}

float getWaterAutoStartTempC(const Settings& settings) {
    return clampFloat(settings.equipment.waterAutoStartCubeTempC, 20.0f, 60.0f);
}

float getAtmosphereHpa(const SystemState& state) {
    if (state.pressure.ok &&
        state.pressure.atmosphere > 850.0f &&
        state.pressure.atmosphere < 1100.0f) {
        return state.pressure.atmosphere;
    }
    return RECT_PRESSURE_STD_HPA;
}

float pressureAdjustedCubeTemp(float baseTempC, const SystemState& state) {
    const float hpa = getAtmosphereHpa(state);
    return baseTempC + (hpa - RECT_PRESSURE_STD_HPA) * RECT_TEMP_COMP_C_PER_HPA;
}

float estimateChargeAbvPercent(const SystemState& state) {
    if (state.hydrometer.valid && state.hydrometer.abv > 0.0f &&
        state.hydrometer.abv < 100.0f) {
        return state.hydrometer.abv;
    }
    return 40.0f;
}

uint8_t getProcessHeaterPower(const SystemState& state, const Settings& settings, uint8_t fallbackPercent) {
    if (state.pressure.ok) {
        return WattControl::update(state, settings);
    }
    return fallbackPercent;
}

uint32_t getPhaseStartTime() { return phaseStartTime; }
void setPhaseStartTime(uint32_t time) { phaseStartTime = time; }
float getPhaseStartVolumeMl() { return phaseStartVolumeMl; }
void setPhaseStartVolumeMl(float volume) { phaseStartVolumeMl = volume; }

} // namespace FSM
