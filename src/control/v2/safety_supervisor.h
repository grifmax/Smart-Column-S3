#ifndef CONTROL_V2_SAFETY_SUPERVISOR_H
#define CONTROL_V2_SAFETY_SUPERVISOR_H

#include "mode_contracts.h"

namespace ControlV2 {

class SafetySupervisorV2 {
public:
    static ActiveLimitsV2 evaluateActiveLimits(const SystemState& state,
                                               const Settings& settings,
                                               const ProcessIndicatorsV2& indicators);
    static SafetyDecisionV2 evaluateDecision(const SystemState& state,
                                             const Settings& settings,
                                             const ProcessIndicatorsV2& indicators,
                                             const ActiveLimitsV2& limits);
    static void applyDecisionToIndicators(const SafetyDecisionV2& decision,
                                          ProcessIndicatorsV2& indicators);
    static const ActiveLimitsV2& getLiveLimits();
};

} // namespace ControlV2

#endif // CONTROL_V2_SAFETY_SUPERVISOR_H
