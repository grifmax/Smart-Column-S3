#include "runtime_metrics.h"

#include <freertos/FreeRTOS.h>

namespace ControlRuntimeMetrics {
namespace {

portMUX_TYPE g_metricsMux = portMUX_INITIALIZER_UNLOCKED;
Snapshot g_metrics;
uint32_t g_previousCycleStartUs = 0;

} // namespace

void recordCycle(uint32_t startUs, uint32_t endUs) {
  const uint32_t durationUs = endUs - startUs;
  portENTER_CRITICAL(&g_metricsMux);
  const uint32_t gapUs = g_previousCycleStartUs == 0
                             ? 0
                             : startUs - g_previousCycleStartUs;
  g_previousCycleStartUs = startUs;
  ControlRuntimeMetricsLogic::recordCycle(g_metrics, durationUs, gapUs);
  portEXIT_CRITICAL(&g_metricsMux);
}

void recordControlBatch(uint32_t durationUs) {
  portENTER_CRITICAL(&g_metricsMux);
  ControlRuntimeMetricsLogic::recordControlBatch(g_metrics, durationUs);
  portEXIT_CRITICAL(&g_metricsMux);
}

void recordControlCommandBatch(uint32_t durationUs,
                               uint32_t commandsProcessed) {
  portENTER_CRITICAL(&g_metricsMux);
  ControlRuntimeMetricsLogic::recordControlCommandBatch(
      g_metrics, durationUs, commandsProcessed);
  portEXIT_CRITICAL(&g_metricsMux);
}

void recordOutputCommandBatch(uint32_t durationUs,
                              uint32_t commandsProcessed) {
  portENTER_CRITICAL(&g_metricsMux);
  ControlRuntimeMetricsLogic::recordOutputCommandBatch(
      g_metrics, durationUs, commandsProcessed);
  portEXIT_CRITICAL(&g_metricsMux);
}

void recordSafetyTrip(uint32_t sampleAgeMs, uint32_t outputStopUs) {
  portENTER_CRITICAL(&g_metricsMux);
  ControlRuntimeMetricsLogic::recordSafetyTrip(g_metrics, sampleAgeMs,
                                                outputStopUs);
  portEXIT_CRITICAL(&g_metricsMux);
}

void recordRepeatedSafetyTrip() {
  portENTER_CRITICAL(&g_metricsMux);
  ControlRuntimeMetricsLogic::recordRepeatedSafetyTrip(g_metrics);
  portEXIT_CRITICAL(&g_metricsMux);
}

void recordStaleSensorCheck() {
  portENTER_CRITICAL(&g_metricsMux);
  ControlRuntimeMetricsLogic::recordStaleSensorCheck(g_metrics);
  portEXIT_CRITICAL(&g_metricsMux);
}

void recordSnapshotCopy(uint32_t durationUs) {
  portENTER_CRITICAL(&g_metricsMux);
  ControlRuntimeMetricsLogic::recordSnapshotCopy(g_metrics, durationUs);
  portEXIT_CRITICAL(&g_metricsMux);
}

Snapshot getSnapshot() {
  Snapshot snapshot;
  portENTER_CRITICAL(&g_metricsMux);
  snapshot = g_metrics;
  portEXIT_CRITICAL(&g_metricsMux);
  return snapshot;
}

} // namespace ControlRuntimeMetrics
