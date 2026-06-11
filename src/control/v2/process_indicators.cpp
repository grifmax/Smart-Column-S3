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

float positiveRatio(float value, float scale) {
    return (scale > 0.0001f) ? clampRange(value / scale, 0.0f, 1.0f) : 0.0f;
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

bool hasTempSensor(const SystemState& state, uint8_t index) {
    return index < TEMP_COUNT && state.temps.valid[index];
}

float applyTrustToConfidence(float value, float decisionTrust) {
    if (value < 0.0f) {
        return value;
    }
    const float trust = clampRange(decisionTrust, 0.0f, 1.0f);
    return clampRange(value * trust, 0.0f, 1.0f);
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
    const bool cubeSensorAvailable = hasTempSensor(state, TEMP_CUBE);
    const bool columnBottomAvailable = hasTempSensor(state, TEMP_COLUMN_BOTTOM);
    const bool columnTopAvailable = hasTempSensor(state, TEMP_COLUMN_TOP);
    const bool waterOutAvailable = hasTempSensor(state, TEMP_WATER_OUT);

    out.sensorFreshnessOk = state.temps.lastUpdate > 0 && ageMs <= SAFETY_SENSOR_TIMEOUT_MS;
    out.processHealth = clamp01(state.health.overallHealth / 100.0f);
    out.pressureSensorAvailable = state.pressure.ok;
    out.columnSensorsAvailable = columnBottomAvailable && columnTopAvailable;
    out.coolingSensorAvailable = waterOutAvailable;

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
        (cubeSensorAvailable && state.temps.cube >= 78.0f) ||
        (columnBottomAvailable && state.temps.columnBottom >= 78.0f);

    const float pressureRatio = clamp01(safeDivide(state.pressure.cube, settings.safety.pressureMaxMmHg, 0.0f));
    const float pressureTrendRatio = clamp01(safeDivide(absf(out.pressureRateMmHgPerMin),
                                                        settings.safety.pressureRiseRateMmHgMin, 0.0f));
    const float coolingPenalty = clamp01(safeDivide(-out.coolingMarginC, 10.0f, 0.0f));
    const float coolingReserve = clamp01(safeDivide(out.coolingMarginC, 8.0f, 0.0f));
    out.pressureStable =
        out.pressureSensorAvailable &&
        absf(out.pressureRateMmHgPerMin) <=
            clampRange(settings.safety.pressureRiseRateMmHgMin * 0.35f, 0.3f, 20.0f);
    out.floodRisk = clamp01(pressureRatio * 0.55f + pressureTrendRatio * 0.30f + coolingPenalty * 0.15f);
    const float floodReserve = clamp01(1.0f - out.floodRisk);
    const float pressureConfidence =
        out.pressureStable ? 1.0f : clamp01(1.0f - pressureTrendRatio);
    const float freshnessConfidence = out.sensorFreshnessOk ? 1.0f : 0.0f;

    const float topDriftPenalty = clamp01(absf(out.topTempRateCPerMin) / 1.0f);
    const float pressureDriftPenalty = clamp01(absf(out.pressureRateMmHgPerMin) /
                                               clampRange(settings.safety.pressureRiseRateMmHgMin, 1.0f, 200.0f));
    const float coolingDriftPenalty = clamp01(safeDivide(5.0f - out.coolingMarginC, 5.0f, 0.0f));
    out.stabilityIndex =
        clamp01(1.0f - (topDriftPenalty * 0.45f + pressureDriftPenalty * 0.35f + coolingDriftPenalty * 0.20f));
    out.columnStable = out.stabilityIndex >= 0.7f && out.floodRisk < 0.65f;

    switch (state.mode) {
        case Mode::RECTIFICATION:
        case Mode::MANUAL_RECT:
            out.telemetryCoverage =
                (cubeSensorAvailable ? 0.25f : 0.0f) +
                (out.columnSensorsAvailable ? 0.30f : 0.0f) +
                (out.pressureSensorAvailable ? 0.25f : 0.0f) +
                (out.coolingSensorAvailable ? 0.20f : 0.0f);
            break;
        case Mode::DISTILLATION:
            out.telemetryCoverage =
                (cubeSensorAvailable ? 0.40f : 0.0f) +
                (out.pressureSensorAvailable ? 0.35f : 0.0f) +
                (out.coolingSensorAvailable ? 0.25f : 0.0f);
            break;
        case Mode::NBK:
            out.telemetryCoverage =
                (cubeSensorAvailable ? 0.35f : 0.0f) +
                (columnBottomAvailable ? 0.35f : 0.0f) +
                (out.pressureSensorAvailable ? 0.30f : 0.0f);
            break;
        case Mode::MASHING:
        case Mode::HOLD:
        case Mode::FERMENTATION:
            out.telemetryCoverage = cubeSensorAvailable ? 1.0f : 0.0f;
            break;
        case Mode::IDLE:
        default:
            out.telemetryCoverage =
                (cubeSensorAvailable ? 0.4f : 0.0f) +
                (out.pressureSensorAvailable ? 0.3f : 0.0f) +
                (out.coolingSensorAvailable ? 0.3f : 0.0f);
            break;
    }

    out.decisionTrust = clamp01(out.telemetryCoverage * freshnessConfidence);
    if ((state.mode == Mode::RECTIFICATION || state.mode == Mode::MANUAL_RECT) &&
        (!out.pressureSensorAvailable || !out.columnSensorsAvailable)) {
        out.decisionTrust = clampRange(out.decisionTrust, 0.0f, 0.45f);
    } else if ((state.mode == Mode::RECTIFICATION || state.mode == Mode::MANUAL_RECT) &&
               !out.coolingSensorAvailable) {
        out.decisionTrust = clampRange(out.decisionTrust, 0.0f, 0.65f);
    } else if (state.mode == Mode::DISTILLATION && !out.pressureSensorAvailable) {
        out.decisionTrust = clampRange(out.decisionTrust, 0.0f, 0.55f);
    } else if (state.mode == Mode::NBK &&
               (!out.pressureSensorAvailable || !columnBottomAvailable)) {
        out.decisionTrust = clampRange(out.decisionTrust, 0.0f, 0.45f);
    }

    switch (state.mode) {
        case Mode::RECTIFICATION:
        case Mode::MANUAL_RECT:
            out.adaptiveControlAllowed =
                out.sensorFreshnessOk && out.pressureSensorAvailable &&
                out.columnSensorsAvailable && out.decisionTrust >= 0.70f;
            break;
        case Mode::DISTILLATION:
            out.adaptiveControlAllowed =
                out.sensorFreshnessOk && cubeSensorAvailable &&
                out.pressureSensorAvailable && out.decisionTrust >= 0.60f;
            break;
        case Mode::NBK:
            out.adaptiveControlAllowed =
                out.sensorFreshnessOk && cubeSensorAvailable &&
                columnBottomAvailable && out.pressureSensorAvailable &&
                out.decisionTrust >= 0.70f;
            break;
        case Mode::MASHING:
        case Mode::HOLD:
        case Mode::FERMENTATION:
            out.adaptiveControlAllowed =
                out.sensorFreshnessOk && cubeSensorAvailable &&
                out.decisionTrust >= 0.60f;
            break;
        case Mode::IDLE:
        default:
            out.adaptiveControlAllowed = out.sensorFreshnessOk && out.decisionTrust >= 0.60f;
            break;
    }

    out.degradedModeActive =
        out.sensorFreshnessOk &&
        state.mode != Mode::IDLE &&
        out.decisionTrust < 0.85f;
    if (out.degradedModeActive) {
        out.processHealth = clampRange(out.processHealth,
                                       0.0f,
                                       0.40f + out.decisionTrust * 0.60f);
        out.stabilityIndex *= clampRange(out.decisionTrust, 0.35f, 1.0f);
        out.columnStable = false;
    }

    out.powerLimited = SafetyPolicyV2::isNbkPressurePowerLimitActive(state, settings) ||
                       SafetyPolicyV2::isManualRectFloodPowerLimitActive(state, settings);
    out.recoveryActive = false;
    out.takeoffAllowed =
        out.adaptiveControlAllowed &&
        out.coolingMarginC > 0.0f &&
        out.floodRisk < 0.8f;
    out.powerLimitConfidence =
        clamp01(pressureRatio * 0.30f + pressureTrendRatio * 0.25f +
                coolingPenalty * 0.20f + out.floodRisk * 0.25f);
    if (out.powerLimited) {
        out.powerLimitConfidence =
            clampRange(out.powerLimitConfidence, 0.75f, 1.0f);
    }

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

        const float phaseTakeoffFactor =
            (state.rectPhase == RectPhase::HEADS || state.rectPhase == RectPhase::BODY ||
             state.rectPhase == RectPhase::TAILS)
                ? 1.0f
                : (state.rectPhase == RectPhase::STABILIZATION ||
                   state.rectPhase == RectPhase::POST_HEADS_STABILIZATION)
                      ? 0.65f
                      : 0.35f;
        const float topRiseConfidence = positiveRatio(out.topTempRateCPerMin, 0.25f);
        const float cubeBodyToTails =
            clamp01(safeDivide(state.temps.cube - 97.5f, 2.5f, 0.0f));

        out.takeoffConfidence =
            clamp01((out.stabilityIndex * 0.32f + floodReserve * 0.24f +
                     coolingReserve * 0.16f + pressureConfidence * 0.14f +
                     (out.takeoffAllowed ? 1.0f : 0.0f) * 0.14f) *
                    freshnessConfidence * phaseTakeoffFactor);
        out.headsEndConfidence =
            clamp01(out.headsCompletionScore * 0.60f + out.stabilityIndex * 0.15f +
                    floodReserve * 0.10f + pressureConfidence * 0.10f +
                    (out.takeoffAllowed ? 1.0f : 0.0f) * 0.05f);
        out.bodyEndConfidence =
            clamp01(out.bodyEndScore * 0.65f +
                    clamp01(safeDivide(state.temps.cube - 96.0f, 4.0f, 0.0f)) * 0.20f +
                    topRiseConfidence * 0.10f + (1.0f - coolingReserve) * 0.05f);
        out.tailsTransitionConfidence =
            clamp01(out.bodyEndScore * 0.50f + cubeBodyToTails * 0.25f +
                    topRiseConfidence * 0.15f + pressureConfidence * 0.10f);

        if (state.rectPhase == RectPhase::TAILS || state.rectPhase == RectPhase::PURGE ||
            state.rectPhase == RectPhase::FINISH || state.rectPhase == RectPhase::COMPLETED) {
            out.tailsTransitionConfidence =
                clampRange(out.tailsTransitionConfidence, 0.85f, 1.0f);
        }

        out.takeoffConfidence = applyTrustToConfidence(out.takeoffConfidence, out.decisionTrust);
        out.headsEndConfidence = applyTrustToConfidence(out.headsEndConfidence, out.decisionTrust);
        out.bodyEndConfidence = applyTrustToConfidence(out.bodyEndConfidence, out.decisionTrust);
        out.tailsTransitionConfidence =
            applyTrustToConfidence(out.tailsTransitionConfidence, out.decisionTrust);
    }

    if (state.mode == Mode::DISTILLATION) {
        out.distHeatingComplete = state.temps.valid[TEMP_CUBE] && state.temps.cube >= 78.0f;
        out.distHeadsOptionalComplete = state.stats.headsVolume > 0.0f;
        out.distBodyNearEnd = state.temps.valid[TEMP_CUBE] && state.temps.cube >= 94.0f;

        out.takeoffConfidence =
            clamp01((out.distHeatingComplete ? 0.40f : 0.0f) +
                    freshnessConfidence * 0.15f + pressureConfidence * 0.15f +
                    coolingReserve * 0.15f + floodReserve * 0.15f);
        if (settings.distillationUi.headsVolumeMl > 0.0f) {
            out.headsEndConfidence =
                clamp01(safeDivide(state.stats.headsVolume, settings.distillationUi.headsVolumeMl, 0.0f) * 0.70f +
                        pressureConfidence * 0.15f + coolingReserve * 0.15f);
        }

        const float distTempProgress =
            clamp01(safeDivide(state.temps.cube - 92.0f, 8.0f, 0.0f));
        const float endTempApproach =
            clamp01(safeDivide(state.temps.cube - (settings.distillationUi.endTempC - 2.0f), 3.0f, 0.0f));
        const float topRiseConfidence = positiveRatio(out.topTempRateCPerMin, 0.20f);
        out.bodyEndConfidence =
            clamp01((out.distBodyNearEnd ? 0.40f : 0.0f) + distTempProgress * 0.30f +
                    pressureConfidence * 0.15f + coolingReserve * 0.05f +
                    topRiseConfidence * 0.10f);
        out.tailsTransitionConfidence =
            clamp01((out.distBodyNearEnd ? 0.30f : 0.0f) + endTempApproach * 0.45f +
                    pressureConfidence * 0.15f + topRiseConfidence * 0.10f);

        out.takeoffConfidence = applyTrustToConfidence(out.takeoffConfidence, out.decisionTrust);
        out.headsEndConfidence = applyTrustToConfidence(out.headsEndConfidence, out.decisionTrust);
        out.bodyEndConfidence = applyTrustToConfidence(out.bodyEndConfidence, out.decisionTrust);
        out.tailsTransitionConfidence =
            applyTrustToConfidence(out.tailsTransitionConfidence, out.decisionTrust);
    }

    if (state.mode == Mode::NBK) {
        out.steamReady = state.temps.valid[TEMP_CUBE] && state.temps.cube >= 98.0f;
        out.nbkColumnLoad = clamp01(safeDivide(state.pump.speedMlPerHour, settings.nbk.pumpSpeedMlH, 0.0f));
        out.feedEnergyBalance =
            clamp01(safeDivide(state.power.power,
                               static_cast<float>(settings.nbk.powerW > 0 ? settings.nbk.powerW : 1),
                               0.0f) -
                    out.nbkColumnLoad * 0.5f + 0.5f);
        out.nbkWorkingStable =
            out.steamReady && out.pressureStable &&
            out.floodRisk < 0.75f && out.adaptiveControlAllowed;
        out.nbkFeedAllowed =
            out.nbkWorkingStable && out.nbkPressureMargin > 0.0f;
        out.finishLikely = state.mode == Mode::NBK && state.nbkPhase == NbkPhase::WORKING &&
                           state.temps.valid[TEMP_COLUMN_BOTTOM] &&
                           state.temps.columnBottom < settings.nbk.columnBottomTempThresholdC;

        out.takeoffConfidence = clamp01((out.steamReady ? 0.35f : 0.0f) +
                                        (out.nbkWorkingStable ? 0.25f : 0.0f) +
                                        freshnessConfidence * 0.15f +
                                        pressureConfidence * 0.10f +
                                        floodReserve * 0.15f);
        out.tailsTransitionConfidence =
            clamp01((out.finishLikely ? 0.60f : 0.0f) +
                    clamp01(safeDivide(settings.nbk.columnBottomTempThresholdC - state.temps.columnBottom,
                                       3.0f, 0.0f)) *
                        0.25f +
                    pressureConfidence * 0.15f);

        out.takeoffConfidence = applyTrustToConfidence(out.takeoffConfidence, out.decisionTrust);
        out.tailsTransitionConfidence =
            applyTrustToConfidence(out.tailsTransitionConfidence, out.decisionTrust);
    }

    out.powerLimitConfidence =
        applyTrustToConfidence(out.powerLimitConfidence,
                               out.powerLimited ? clampRange(out.decisionTrust + 0.20f, 0.0f, 1.0f)
                                                : out.decisionTrust);

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
