#pragma once

#include <stdint.h>

#include "runtime_metrics_logic.h"

namespace ControlRuntimeMetrics {

using Snapshot = ControlRuntimeMetricsLogic::Snapshot;

void recordCycle(uint32_t startUs, uint32_t endUs);
void recordControlBatch(uint32_t durationUs);
void recordControlCommandBatch(uint32_t durationUs,
                               uint32_t commandsProcessed);
void recordOutputCommandBatch(uint32_t durationUs,
                              uint32_t commandsProcessed);
void recordSafetyTrip(uint32_t sampleAgeMs, uint32_t outputStopUs);
void recordRepeatedSafetyTrip();
void recordStaleSensorCheck();
void recordSnapshotCopy(uint32_t durationUs);
Snapshot getSnapshot();

} // namespace ControlRuntimeMetrics
