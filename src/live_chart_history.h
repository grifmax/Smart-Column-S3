#ifndef LIVE_CHART_HISTORY_H
#define LIVE_CHART_HISTORY_H

#include <ArduinoJson.h>
#include "types.h"

namespace LiveChartHistory {

constexpr uint32_t MINUTE_INTERVAL_MS = 60000UL;
constexpr uint32_t AGGREGATE_INTERVAL_MS = 600000UL;
constexpr uint8_t MINUTE_POINTS_CAPACITY = 60;
constexpr uint8_t AGGREGATE_POINTS_CAPACITY = 72;

void init();
void clear();
void recordState(const SystemState& state, uint32_t nowMs);
void fillJson(JsonObject root);

} // namespace LiveChartHistory

#endif
