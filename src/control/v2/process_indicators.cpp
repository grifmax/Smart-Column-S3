#include "process_indicators.h"
#include "safety_policy.h"

namespace ControlV2 {

namespace {

float safeDivide(float numerator, float denominator, float fallback = 0.0f) {
    return (denominator > 0.0001f) ? (numerator / denominator) : fallback;
}

float clampRange(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

float estimateAbsoluteAlcoholMl(const Settings& settings) {
    float volumeL = settings.rectParams.feedVolumeL;
    if (volumeL <= 0.1f) {
        volumeL = settings.equipment.cubeVolumeL;
    }
    volumeL = clampRange(volumeL, 1.0f, 250.0f);

    float abv = clampRange(settings.rectParams.feedAbvPercent, 1.0f, 96.0f);
    return volumeL * 1000.0f * (abv / 100.0f);
}

float getCurrentModeTargetTemp(const SystemState& state, const Settings& settings) {
    switch (state.mode) {
        case Mode::MASHING:
            return state.mashing.targetTemp;
        case Mode::HOLD:
            return state.hold.targetTemp;
        case Mode::FERMENTATION:
            return settings.fermentation.targetTempC;
        default:
            return 0.0f;
    }
}

} // namespace

float ProcessIndicatorsEngineV2::clamp01(float value) {
    return clampRange(value, 0.0f, 1.0f);
}

float ProcessIndicatorsEngineV2::absf(float value) {
    return value < 0.0f ? -value : value;
}

ProcessIndicatorsV2 ProcessIndicatorsEngineV2::evaluate(const SystemState& state,
                                                        const Settings& settings,
                                                        IndicatorRuntimeStateV2& runtime) {
    ProcessIndicatorsV2 out;
    const uint32_t now = millis();
    const uint32_t ageMs =
        (state.temps.lastUpdate > 0 && now >= state.temps.lastUpdate) ? (now - state.temps.lastUpdate)
                                                                      : UINT32_MAX;

    out.sensorFreshnessOk = state.temps.lastUpdate > 0 && ageMs <= SAFETY_SENSOR_TIMEOUT_MS;
    out.processHealth = clamp01(state.health.overallHealth / 100.0f);

    if (runtime.initialized && runtime.lastUpdateMs > 0 && now > runtime.lastUpdateMs) {
        const float dtMin = static_cast<float>(now - runtime.lastUpdateMs) / 60000.0f;
        if (dtMin > 0.0001f) {
            out.heatingRateCPerMin = (state.temps.cube - runtime.lastCubeTempC) / dtMin;
            out.topTempRateCPerMin = (state.temps.columnTop - runtime.lastColumnTopTempC) / dtMin;
            out.pressureRateMmHgPerMin = (state.pressure.cube - runtime.lastPressureMmHg) / dtMin;
        }
    }

    runtime.initialized = true;
    runtime.lastUpdateMs = now;
    runtime.lastCubeTempC = state.temps.cube;
    runtime.lastColumnTopTempC = state.temps.columnTop;
    runtime.lastPressureMmHg = state.pressure.cube;

    out.coolingMarginC = settings.safety.waterOutMaxC - state.temps.waterOut;
    out.distPressureMargin = settings.safety.pressureMaxMmHg - state.pressure.cube;
    out.nbkPressureMargin = out.distPressureMargin;

    out.boilingDetected =
        (state.temps.valid[TEMP_CUBE] && state.temps.cube >= 78.0f) ||
        (state.temps.valid[TEMP_COLUMN_BOTTOM] && state.temps.columnBottom >= 78.0f);

    const float pressureRatio = clamp01(safeDivide(state.pressure.cube, settings.safety.pressureMaxMmHg, 0.0f));
    const float pressureTrendRatio = clamp01(safeDivide(absf(out.pressureRateMmHgPerMin),
                                                        settings.safety.pressureRiseRateMmHgMin, 0.0f));
    const float coolingPenalty = clamp01(safeDivide(-out.coolingMarginC, 10.0f, 0.0f));

    out.floodRisk = clamp01(pressureRatio * 0.55f + pressureTrendRatio * 0.30f + coolingPenalty * 0.15f);
    out.pressureStable = absf(out.pressureRateMmHgPerMin) <=
                         clampRange(settings.safety.pressureRiseRateMmHgMin * 0.35f, 0.3f, 20.0f);

    const float topDriftPenalty = clamp01(absf(out.topTempRateCPerMin) / 1.0f);
    const float pressureDriftPenalty = clamp01(absf(out.pressureRateMmHgPerMin) /
                                               clampRange(settings.safety.pressureRiseRateMmHgMin, 1.0f, 200.0f));
    const float coolingDriftPenalty = clamp01(safeDivide(5.0f - out.coolingMarginC, 5.0f, 0.0f));
    out.stabilityIndex =
        clamp01(1.0f - (topDriftPenalty * 0.45f + pressureDriftPenalty * 0.35f + coolingDriftPenalty * 0.20f));
    out.columnStable = out.stabilityIndex >= 0.7f && out.floodRisk < 0.65f;

    out.powerLimited = SafetyPolicyV2::isNbkPressurePowerLimitActive(state, settings) ||
                       SafetyPolicyV2::isManualRectFloodPowerLimitActive(state, settings);
    out.recoveryActive = false;
    out.takeoffAllowed = out.sensorFreshnessOk && out.coolingMarginC > 0.0f && out.floodRisk < 0.8f;

    if (state.mode == Mode::RECTIFICATION || state.mode == Mode::MANUAL_RECT) {
        const float aaMl = estimateAbsoluteAlcoholMl(settings);
        const float headsTargetMl =
            clampRange(aaMl * (settings.rectParams.headsPercent / 100.0f), 10.0f, aaMl);
        const float bodyTargetMl =
            clampRange(aaMl * (settings.rectParams.bodyPercent / 100.0f), 0.0f, aaMl);

        out.headsCompletionScore =
            clamp01(safeDivide(state.stats.headsVolume, headsTargetMl, 0.0f) * 0.8f + out.stabilityIndex * 0.2f);
        out.bodyEndScore =
            clamp01(safeDivide(state.stats.bodyVolume, bodyTargetMl > 1.0f ? bodyTargetMl : 1.0f, 0.0f) * 0.6f +
                    clamp01(safeDivide(state.temps.cube - 96.0f, 4.0f, 0.0f)) * 0.4f);
    }

    if (state.mode == Mode::DISTILLATION) {
        out.distHeatingComplete = state.temps.valid[TEMP_CUBE] && state.temps.cube >= 78.0f;
        out.distHeadsOptionalComplete = state.stats.headsVolume > 0.0f;
        out.distBodyNearEnd = state.temps.valid[TEMP_CUBE] && state.temps.cube >= 94.0f;
    }

    if (state.mode == Mode::NBK) {
        out.steamReady = state.temps.valid[TEMP_CUBE] && state.temps.cube >= 98.0f;
        out.nbkColumnLoad = clamp01(safeDivide(state.pump.speedMlPerHour, settings.nbk.pumpSpeedMlH, 0.0f));
        out.feedEnergyBalance =
            clamp01(safeDivide(state.power.power,
                               static_cast<float>(settings.nbk.powerW > 0 ? settings.nbk.powerW : 1),
                               0.0f) -
                    out.nbkColumnLoad * 0.5f + 0.5f);
        out.nbkWorkingStable = out.steamReady && out.pressureStable && out.floodRisk < 0.75f;
        out.nbkFeedAllowed = out.nbkWorkingStable && out.nbkPressureMargin > 0.0f;
        out.finishLikely = state.mode == Mode::NBK && state.nbkPhase == NbkPhase::WORKING &&
                           state.temps.valid[TEMP_COLUMN_BOTTOM] &&
                           state.temps.columnBottom < settings.nbk.columnBottomTempThresholdC;
    }

    const float targetTemp = getCurrentModeTargetTemp(state, settings);
    if (targetTemp > 0.0f && state.temps.valid[TEMP_CUBE]) {
        const float error = targetTemp - state.temps.cube;
        out.tempInBand = absf(error) <= 1.0f;
        out.stepReady = out.tempInBand;
        out.stepHoldStable = out.tempInBand && absf(out.heatingRateCPerMin) < 0.8f;
        out.heatingTooSlow = error > 2.0f && out.heatingRateCPerMin < 0.15f;
        out.overshootRisk = error < 0.5f && out.heatingRateCPerMin > 0.4f;
        out.targetReached = out.tempInBand;
    }

    if (state.mode == Mode::FERMENTATION && state.temps.valid[TEMP_CUBE]) {
        const float error = settings.fermentation.targetTempC - state.temps.cube;
        out.fermTempInBand = absf(error) <= settings.fermentation.hysteresisC;
        out.longDeviation = absf(error) > settings.fermentation.hysteresisC * 2.0f;
        out.heatingDemand = error > settings.fermentation.hysteresisC;
        out.coolingDemand = error < -settings.fermentation.hysteresisC;
        out.targetReached = out.fermTempInBand;
    }

    return out;
}

} // namespace ControlV2
