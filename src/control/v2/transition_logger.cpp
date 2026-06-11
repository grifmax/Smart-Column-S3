#include "transition_logger.h"

#include "../../storage/logger.h"
#include "../fsm.h"

namespace ControlV2 {

String TransitionLoggerV2::formatPhaseTransition(const PhaseTransitionEventV2& event) {
    String line = "phase_transition";
    line += " mode=";
    line += FSM::getModeName(event.mode);
    line += " from=";
    line += event.fromPhaseToken;
    line += " to=";
    line += event.toPhaseToken;
    line += " reason=";
    line += reasonCodeToString(event.reasonCode);
    line += " phase_elapsed_sec=";
    line += String(event.phaseElapsedSec);
    line += " mode_elapsed_sec=";
    line += String(event.modeElapsedSec);
    line += " stability_index=";
    line += String(event.indicators.stabilityIndex, 3);
    line += " flood_risk=";
    line += String(event.indicators.floodRisk, 3);
    line += " cooling_margin_c=";
    line += String(event.indicators.coolingMarginC, 2);
    line += " power_cap=";
    line += (event.activeLimits.powerCapped ? "1" : "0");
    line += " takeoff_blocked=";
    line += (event.activeLimits.takeoffBlocked ? "1" : "0");
    line += " anti_oscillation=";
    line += (event.activeLimits.antiOscillationActive ? "1" : "0");
    if (event.operatorMessage[0] != '\0') {
        line += " message=\"";
        line += event.operatorMessage;
        line += "\"";
    }
    return line;
}

void TransitionLoggerV2::logPhaseTransition(const PhaseTransitionEventV2& event) {
    const String line = formatPhaseTransition(event);
    Logger::logf(0, "%s", line.c_str());
}

} // namespace ControlV2
