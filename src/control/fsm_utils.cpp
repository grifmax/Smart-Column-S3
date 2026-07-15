#include "fsm_utils.h"
#include <Arduino.h>
#include <math.h>
#include "../drivers/heater.h"
#include "watt_control.h"
#include "v2/safety_supervisor.h"

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

uint16_t getConfiguredHeaterPowerWatts(const Settings& settings) {
    return settings.equipment.heaterPowerW > 0
        ? settings.equipment.heaterPowerW
        : DEFAULT_HEATER_POWER_W;
}

float getConfiguredHeaterPowerKw(uint16_t heaterPowerW) {
    const uint16_t configuredPowerW =
        heaterPowerW > 0 ? heaterPowerW : DEFAULT_HEATER_POWER_W;
    return fmaxf(0.1f, static_cast<float>(configuredPowerW) / 1000.0f);
}

float getConfiguredHeaterPowerKw(const Settings& settings) {
    return getConfiguredHeaterPowerKw(getConfiguredHeaterPowerWatts(settings));
}

float getRectificationColumnCapacityFactor(uint16_t columnHeightMm, float packingCoeff) {
    const float normalizedHeight =
        static_cast<float>(columnHeightMm > 0 ? columnHeightMm : DEFAULT_COLUMN_HEIGHT_MM) /
        static_cast<float>(DEFAULT_COLUMN_HEIGHT_MM);
    const float normalizedPacking =
        (packingCoeff > 0.0f ? packingCoeff : DEFAULT_PACKING_COEFF) /
        DEFAULT_PACKING_COEFF;
    const float combinedFactor =
        sqrtf(fmaxf(0.0f, normalizedHeight * normalizedPacking));

    return clampFloat(combinedFactor, 0.5f, 1.5f);
}

float getRectificationColumnCapacityFactor(const Settings& settings) {
    return getRectificationColumnCapacityFactor(
        settings.equipment.columnHeightMm,
        settings.equipment.packingCoeff
    );
}

float getRectificationTakeoffRateMlH(uint16_t heaterPowerW,
                                     uint16_t columnHeightMm,
                                     float packingCoeff,
                                     float speedMlHKw) {
    return fmaxf(0.0f, speedMlHKw) * getConfiguredHeaterPowerKw(heaterPowerW) *
           getRectificationColumnCapacityFactor(columnHeightMm, packingCoeff);
}

float getRectificationTakeoffRateMlH(const Settings& settings, float speedMlHKw) {
    return getRectificationTakeoffRateMlH(
        settings.equipment.heaterPowerW,
        settings.equipment.columnHeightMm,
        settings.equipment.packingCoeff,
        speedMlHKw
    );
}

float getRectificationDirectTakeoffSpeedMlH(const Settings& settings, RectPhase phase) {
    switch (phase) {
        case RectPhase::HEADS:
            return getRectificationTakeoffRateMlH(
                settings, settings.rectParams.headsSpeedMlHKw);
        case RectPhase::TAILS:
            return getRectificationTakeoffRateMlH(
                       settings, settings.rectParams.bodySpeedMlHKw) *
                   0.6f;
        case RectPhase::BODY:
        default:
            return getRectificationTakeoffRateMlH(
                settings, settings.rectParams.bodySpeedMlHKw);
    }
}

uint16_t applyFullHeatPower(const Settings& settings) {
    const uint16_t targetWatts = getConfiguredHeaterPowerWatts(settings);
    Heater::setPowerWatts(targetWatts);
    return targetWatts;
}

uint8_t getProcessHeaterPower(const SystemState& state, const Settings& settings, uint8_t fallbackPercent) {
    uint8_t requestedPower = fallbackPercent;
    if (state.pressure.ok) {
        requestedPower = WattControl::update(state, settings);
    }

    const ControlV2::ActiveLimitsV2& limits = ControlV2::SafetySupervisorV2::getLiveLimits();
    if (limits.powerCapped && limits.maxHeaterPowerPercent < requestedPower) {
        return limits.maxHeaterPowerPercent;
    }

    return requestedPower;
}

uint16_t applyProcessHeaterPower(const SystemState& state, const Settings& settings, uint8_t fallbackPercent) {
    if (WattControl::hasOverrideWatts()) {
        const int16_t overrideWatts = WattControl::getOverrideWatts();
        const uint16_t targetWatts = overrideWatts > 0 ? static_cast<uint16_t>(overrideWatts) : 0;
        Heater::setPowerWatts(targetWatts);
        return targetWatts;
    }

    const uint8_t requestedPercent = getProcessHeaterPower(state, settings, fallbackPercent);
    const uint16_t heaterMaxW = getConfiguredHeaterPowerWatts(settings);
    const uint16_t targetWatts = static_cast<uint16_t>(
        (static_cast<uint32_t>(heaterMaxW) * requestedPercent) / 100U
    );
    Heater::setPowerWatts(targetWatts);
    return targetWatts;
}

bool shouldRunBoosterHeater(const SystemState& state, const Settings& settings, bool heatingPhase) {
    if (!heatingPhase || !settings.equipment.boosterHeaterEnabled) {
        return false;
    }

    if (state.temps.valid[TEMP_CUBE] &&
        state.temps.cube >= settings.equipment.boosterHeaterStopCubeTempC) {
        return false;
    }

    return true;
}

void applyBoosterHeater(const SystemState& state, const Settings& settings, bool heatingPhase) {
    Heater::setBoosterEnabled(shouldRunBoosterHeater(state, settings, heatingPhase));
}

uint32_t getPhaseStartTime() { return phaseStartTime; }
void setPhaseStartTime(uint32_t time) { phaseStartTime = time; }
float getPhaseStartVolumeMl() { return phaseStartVolumeMl; }
void setPhaseStartVolumeMl(float volume) { phaseStartVolumeMl = volume; }

} // namespace FSM
