#ifndef CONTROL_V2_PROCESS_INDICATORS_H
#define CONTROL_V2_PROCESS_INDICATORS_H

#include <Arduino.h>

#include "../../config.h"
#include "../../types.h"

namespace ControlV2 {

struct IndicatorRuntimeStateV2 {
    bool initialized = false;
    uint32_t lastUpdateMs = 0;
    float lastCubeTempC = 0.0f;
    float lastColumnTopTempC = 0.0f;
    float lastPressureMmHg = 0.0f;
};

struct ProcessIndicatorsV2 {
    bool sensorFreshnessOk = false;
    bool pressureStable = false;
    bool boilingDetected = false;
    bool columnStable = false;
    bool targetReached = false;
    bool powerLimited = false;
    bool recoveryActive = false;
    bool takeoffAllowed = false;

    bool distHeatingComplete = false;
    bool distHeadsOptionalComplete = false;
    bool distBodyNearEnd = false;

    bool steamReady = false;
    bool nbkWorkingStable = false;
    bool nbkFeedAllowed = false;
    bool finishLikely = false;

    bool tempInBand = false;
    bool stepReady = false;
    bool stepHoldStable = false;
    bool heatingTooSlow = false;
    bool overshootRisk = false;

    bool fermTempInBand = false;
    bool longDeviation = false;
    bool heatingDemand = false;
    bool coolingDemand = false;

    float processHealth = 0.0f;
    float heatingRateCPerMin = 0.0f;
    float topTempRateCPerMin = 0.0f;
    float pressureRateMmHgPerMin = 0.0f;
    float coolingMarginC = 0.0f;
    float distPressureMargin = 0.0f;
    float nbkPressureMargin = 0.0f;
    float nbkColumnLoad = 0.0f;
    float feedEnergyBalance = 0.0f;
    float stabilityIndex = 0.0f;
    float floodRisk = 0.0f;
    float headsCompletionScore = 0.0f;
    float bodyEndScore = 0.0f;
};

class ProcessIndicatorsEngineV2 {
public:
    static ProcessIndicatorsV2 evaluate(const SystemState& state,
                                        const Settings& settings,
                                        IndicatorRuntimeStateV2& runtime);

private:
    static float clamp01(float value);
    static float absf(float value);
};

} // namespace ControlV2

#endif // CONTROL_V2_PROCESS_INDICATORS_H
