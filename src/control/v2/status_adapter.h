#ifndef CONTROL_V2_STATUS_ADAPTER_H
#define CONTROL_V2_STATUS_ADAPTER_H

#include "mode_contracts.h"

namespace ControlV2 {

void notePhaseTransition(Mode mode, uint16_t fromPhaseId, uint16_t toPhaseId,
                         ReasonCodeV2 reasonCode, const char* operatorMessage = nullptr);
void noteSafetyOperatorAction(ReasonCodeV2 reasonCode, const char* message,
                              const char* operatorMessage = nullptr);

void updateRuntime(const SystemState& state, const Settings& settings);

const ProcessIndicatorsV2& getLatestIndicators();
const MetricsSnapshotV2& getLatestMetricsSnapshot();
const ModeStatusV2& getLatestModeStatus();

} // namespace ControlV2

#endif // CONTROL_V2_STATUS_ADAPTER_H
