#ifndef CONTROL_V2_TRANSITION_LOGGER_H
#define CONTROL_V2_TRANSITION_LOGGER_H

#include <Arduino.h>

#include "mode_contracts.h"

namespace ControlV2 {

struct PhaseTransitionEventV2 {
    Mode mode = Mode::IDLE;
    uint16_t fromPhaseId = 0;
    uint16_t toPhaseId = 0;
    char fromPhaseToken[32] = "";
    char toPhaseToken[32] = "";
    ReasonCodeV2 reasonCode = ReasonCodeV2::NONE;
    char operatorMessage[96] = "";
    uint32_t timestampMs = 0;
    uint32_t phaseElapsedSec = 0;
    uint32_t modeElapsedSec = 0;
    ProcessIndicatorsV2 indicators;
    ActiveLimitsV2 activeLimits;
    float collectedVolumeMl = 0.0f;
};

class TransitionLoggerV2 {
public:
    static void logPhaseTransition(const PhaseTransitionEventV2& event);
    static String formatPhaseTransition(const PhaseTransitionEventV2& event);
};

} // namespace ControlV2

#endif // CONTROL_V2_TRANSITION_LOGGER_H
