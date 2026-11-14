#pragma once

#include <stdint.h>

namespace ControlRuntimeMetricsLogic {

struct Snapshot {
  uint32_t cycleCount = 0;
  uint32_t lastCycleUs = 0;
  uint32_t maxCycleUs = 0;
  uint32_t maxCycleGapUs = 0;
  uint32_t cyclesLe1Ms = 0;
  uint32_t cyclesLe5Ms = 0;
  uint32_t cyclesLe10Ms = 0;
  uint32_t cyclesGt10Ms = 0;
  uint32_t controlBatchCount = 0;
  uint32_t lastControlBatchUs = 0;
  uint32_t maxControlBatchUs = 0;
  uint32_t controlCommandBatchCount = 0;
  uint32_t controlCommandsProcessed = 0;
  uint32_t lastControlCommandBatchUs = 0;
  uint32_t maxControlCommandBatchUs = 0;
  uint32_t outputCommandBatchCount = 0;
  uint32_t outputCommandsProcessed = 0;
  uint32_t lastOutputCommandBatchUs = 0;
  uint32_t maxOutputCommandBatchUs = 0;
  uint32_t safetyTripCount = 0;
  uint32_t safetySampleAgeKnownCount = 0;
  uint32_t lastSafetySampleAgeMs = 0;
  uint32_t maxSafetySampleAgeMs = 0;
  uint32_t lastOutputStopUs = 0;
  uint32_t maxOutputStopUs = 0;
  uint32_t repeatedSafetyTripCount = 0;
  uint32_t staleSensorCheckCount = 0;
  uint32_t snapshotCopyCount = 0;
  uint32_t lastSnapshotCopyUs = 0;
  uint32_t maxSnapshotCopyUs = 0;
};

inline void recordCycle(Snapshot &snapshot, uint32_t durationUs,
                        uint32_t gapUs) {
  ++snapshot.cycleCount;
  snapshot.lastCycleUs = durationUs;
  if (durationUs > snapshot.maxCycleUs) snapshot.maxCycleUs = durationUs;
  if (gapUs > snapshot.maxCycleGapUs) snapshot.maxCycleGapUs = gapUs;
  if (durationUs <= 1000) {
    ++snapshot.cyclesLe1Ms;
  } else if (durationUs <= 5000) {
    ++snapshot.cyclesLe5Ms;
  } else if (durationUs <= 10000) {
    ++snapshot.cyclesLe10Ms;
  } else {
    ++snapshot.cyclesGt10Ms;
  }
}

inline void recordControlBatch(Snapshot &snapshot, uint32_t durationUs) {
  ++snapshot.controlBatchCount;
  snapshot.lastControlBatchUs = durationUs;
  if (durationUs > snapshot.maxControlBatchUs) {
    snapshot.maxControlBatchUs = durationUs;
  }
}

inline void recordControlCommandBatch(Snapshot &snapshot, uint32_t durationUs,
                                      uint32_t commandsProcessed) {
  ++snapshot.controlCommandBatchCount;
  snapshot.controlCommandsProcessed += commandsProcessed;
  snapshot.lastControlCommandBatchUs = durationUs;
  if (durationUs > snapshot.maxControlCommandBatchUs) {
    snapshot.maxControlCommandBatchUs = durationUs;
  }
}

inline void recordOutputCommandBatch(Snapshot &snapshot, uint32_t durationUs,
                                     uint32_t commandsProcessed) {
  ++snapshot.outputCommandBatchCount;
  snapshot.outputCommandsProcessed += commandsProcessed;
  snapshot.lastOutputCommandBatchUs = durationUs;
  if (durationUs > snapshot.maxOutputCommandBatchUs) {
    snapshot.maxOutputCommandBatchUs = durationUs;
  }
}

inline uint32_t sampleAgeMs(uint32_t nowMs, uint32_t sampleAtMs,
                            bool sampleValid) {
  return sampleValid ? nowMs - sampleAtMs : UINT32_MAX;
}

inline void recordSafetyTrip(Snapshot &snapshot, uint32_t sampleAgeMs,
                             uint32_t outputStopUs) {
  ++snapshot.safetyTripCount;
  snapshot.lastSafetySampleAgeMs = sampleAgeMs;
  if (sampleAgeMs != UINT32_MAX) {
    ++snapshot.safetySampleAgeKnownCount;
  }
  if (sampleAgeMs != UINT32_MAX &&
      sampleAgeMs > snapshot.maxSafetySampleAgeMs) {
    snapshot.maxSafetySampleAgeMs = sampleAgeMs;
  }
  snapshot.lastOutputStopUs = outputStopUs;
  if (outputStopUs > snapshot.maxOutputStopUs) {
    snapshot.maxOutputStopUs = outputStopUs;
  }
}

inline void recordRepeatedSafetyTrip(Snapshot &snapshot) {
  ++snapshot.repeatedSafetyTripCount;
}

inline void recordStaleSensorCheck(Snapshot &snapshot) {
  ++snapshot.staleSensorCheckCount;
}

inline void recordSnapshotCopy(Snapshot &snapshot, uint32_t durationUs) {
  ++snapshot.snapshotCopyCount;
  snapshot.lastSnapshotCopyUs = durationUs;
  if (durationUs > snapshot.maxSnapshotCopyUs) {
    snapshot.maxSnapshotCopyUs = durationUs;
  }
}

} // namespace ControlRuntimeMetricsLogic
