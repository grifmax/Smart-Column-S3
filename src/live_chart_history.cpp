#include "live_chart_history.h"

#include <math.h>
#include <string.h>

#include "config.h"
#include "control/v2/status_adapter.h"

namespace LiveChartHistory {

namespace {

constexpr uint16_t FLAG_TEMP_CUBE = 1 << 0;
constexpr uint16_t FLAG_TEMP_COLUMN_BOTTOM = 1 << 1;
constexpr uint16_t FLAG_TEMP_COLUMN_TOP = 1 << 2;
constexpr uint16_t FLAG_TEMP_REFLUX = 1 << 3;
constexpr uint16_t FLAG_TEMP_TSA = 1 << 4;
constexpr uint16_t FLAG_TEMP_WATER_IN = 1 << 5;
constexpr uint16_t FLAG_TEMP_WATER_OUT = 1 << 6;
constexpr uint16_t FLAG_PRESSURE_CUBE = 1 << 7;
constexpr uint16_t FLAG_PRESSURE_FLOOD = 1 << 8;
constexpr uint16_t FLAG_POWER = 1 << 9;
constexpr uint16_t FLAG_ABV = 1 << 10;

struct LiveChartPoint {
    uint32_t sampleMs = 0;
    uint16_t flags = 0;
    float cube = 0.0f;
    float columnBottom = 0.0f;
    float columnTop = 0.0f;
    float reflux = 0.0f;
    float tsa = 0.0f;
    float waterIn = 0.0f;
    float waterOut = 0.0f;
    float pressureCube = 0.0f;
    float pressureAtm = 1013.25f;
    float pressureFlood = 0.0f;
    float voltage = 0.0f;
    float current = 0.0f;
    float power = 0.0f;
    float energy = 0.0f;
    float frequency = 0.0f;
    float pf = 0.0f;
    float pumpSpeed = 0.0f;
    float pumpVolume = 0.0f;
    float abv = 0.0f;
    float volumeHeads = 0.0f;
    float volumeBody = 0.0f;
    float volumeTails = 0.0f;
};

template <size_t Capacity>
struct LiveChartRingBuffer {
    LiveChartPoint points[Capacity] = {};
    size_t count = 0;
    size_t next = 0;
};

LiveChartRingBuffer<MINUTE_POINTS_CAPACITY> g_minuteBuffer;
LiveChartRingBuffer<AGGREGATE_POINTS_CAPACITY> g_aggregateBuffer;
uint32_t g_lastMinuteSampleMs = 0;
uint32_t g_lastAggregateSampleMs = 0;

void resetBufferState() {
    memset(&g_minuteBuffer, 0, sizeof(g_minuteBuffer));
    memset(&g_aggregateBuffer, 0, sizeof(g_aggregateBuffer));
    g_lastMinuteSampleMs = 0;
    g_lastAggregateSampleMs = 0;
}

template <size_t Capacity>
void appendPoint(LiveChartRingBuffer<Capacity>& buffer,
                 const LiveChartPoint& point) {
    buffer.points[buffer.next] = point;
    buffer.next = (buffer.next + 1) % Capacity;
    if (buffer.count < Capacity) {
        buffer.count++;
    }
}

template <size_t Capacity, typename Fn>
void forEachPointOldestFirst(const LiveChartRingBuffer<Capacity>& buffer,
                             Fn&& callback) {
    if (buffer.count == 0) {
        return;
    }

    const size_t start =
        (buffer.count < Capacity) ? 0 : buffer.next;
    for (size_t i = 0; i < buffer.count; ++i) {
        const size_t index = (start + i) % Capacity;
        callback(buffer.points[index]);
    }
}

bool isFlagSet(uint16_t flags, uint16_t flag) {
    return (flags & flag) != 0;
}

void appendIfFlag(JsonObject point, uint16_t flags, uint16_t flag,
                  const char* key, float value) {
    if (isFlagSet(flags, flag)) {
        point[key] = value;
    }
}

void appendPointJson(JsonArray array, const LiveChartPoint& sample) {
    JsonObject point = array.add<JsonObject>();
    point["ms"] = sample.sampleMs;

    appendIfFlag(point, sample.flags, FLAG_TEMP_CUBE, "t_cube", sample.cube);
    appendIfFlag(point, sample.flags, FLAG_TEMP_COLUMN_BOTTOM, "t_column_bottom",
                 sample.columnBottom);
    appendIfFlag(point, sample.flags, FLAG_TEMP_COLUMN_TOP, "t_column_top",
                 sample.columnTop);
    appendIfFlag(point, sample.flags, FLAG_TEMP_REFLUX, "t_reflux", sample.reflux);
    appendIfFlag(point, sample.flags, FLAG_TEMP_TSA, "t_tsa", sample.tsa);
    appendIfFlag(point, sample.flags, FLAG_TEMP_WATER_IN, "t_water_in",
                 sample.waterIn);
    appendIfFlag(point, sample.flags, FLAG_TEMP_WATER_OUT, "t_water_out",
                 sample.waterOut);
    appendIfFlag(point, sample.flags, FLAG_PRESSURE_CUBE, "p_cube",
                 sample.pressureCube);
    appendIfFlag(point, sample.flags, FLAG_PRESSURE_FLOOD, "p_flood",
                 sample.pressureFlood);

    point["p_atm"] = sample.pressureAtm;

    if (isFlagSet(sample.flags, FLAG_POWER)) {
        point["voltage"] = sample.voltage;
        point["current"] = sample.current;
        point["power"] = sample.power;
        point["energy"] = sample.energy;
        point["frequency"] = sample.frequency;
        point["pf"] = sample.pf;
        point["pzem_ok"] = true;
    } else {
        point["pzem_ok"] = false;
    }

    point["pump_speed"] = sample.pumpSpeed;
    point["pump_volume"] = sample.pumpVolume;
    point["volume_heads"] = sample.volumeHeads;
    point["volume_body"] = sample.volumeBody;
    point["volume_tails"] = sample.volumeTails;

    if (isFlagSet(sample.flags, FLAG_ABV)) {
        point["abv"] = sample.abv;
    }
}

LiveChartPoint capturePoint(const SystemState& state, uint32_t nowMs) {
    LiveChartPoint point{};
    point.sampleMs = nowMs;
    point.pressureAtm = state.pressure.atmosphere;
    point.pumpSpeed = state.pump.speedMlPerHour;
    point.pumpVolume = state.pump.totalVolumeMl;
    point.volumeHeads = state.stats.headsVolume;
    point.volumeBody = state.stats.bodyVolume;
    point.volumeTails = state.stats.tailsVolume;

    if (state.temps.valid[TEMP_CUBE]) {
        point.flags |= FLAG_TEMP_CUBE;
        point.cube = state.temps.cube;
    }
    if (state.temps.valid[TEMP_COLUMN_BOTTOM]) {
        point.flags |= FLAG_TEMP_COLUMN_BOTTOM;
        point.columnBottom = state.temps.columnBottom;
    }
    if (state.temps.valid[TEMP_COLUMN_TOP]) {
        point.flags |= FLAG_TEMP_COLUMN_TOP;
        point.columnTop = state.temps.columnTop;
    }
    if (state.temps.valid[TEMP_REFLUX]) {
        point.flags |= FLAG_TEMP_REFLUX;
        point.reflux = state.temps.reflux;
    }
    if (state.temps.valid[TEMP_TSA]) {
        point.flags |= FLAG_TEMP_TSA;
        point.tsa = state.temps.tsa;
    }
    if (state.temps.valid[TEMP_WATER_IN]) {
        point.flags |= FLAG_TEMP_WATER_IN;
        point.waterIn = state.temps.waterIn;
    }
    if (state.temps.valid[TEMP_WATER_OUT]) {
        point.flags |= FLAG_TEMP_WATER_OUT;
        point.waterOut = state.temps.waterOut;
    }

    if (state.pressure.ok) {
        point.flags |= FLAG_PRESSURE_CUBE;
        point.pressureCube = state.pressure.cube;
    }

    const auto& indicators = ControlV2::getLatestIndicators();
    const float floodMargin = isfinite(indicators.distPressureMargin)
                                  ? indicators.distPressureMargin
                                  : indicators.nbkPressureMargin;
    if (isfinite(floodMargin)) {
        point.flags |= FLAG_PRESSURE_FLOOD;
        point.pressureFlood = floodMargin;
    }

    if (state.health.pzemOk) {
        point.flags |= FLAG_POWER;
        point.voltage = state.power.voltage;
        point.current = state.power.current;
        point.power = state.power.power;
        point.energy = state.power.energy;
        point.frequency = state.power.frequency;
        point.pf = state.power.powerFactor;
    }

    if (state.hydrometer.valid) {
        point.flags |= FLAG_ABV;
        point.abv = state.hydrometer.abv;
    }

    return point;
}

bool intervalElapsed(uint32_t nowMs, uint32_t lastMs, uint32_t intervalMs) {
    return lastMs == 0 || nowMs < lastMs || (nowMs - lastMs) >= intervalMs;
}

} // namespace

void init() {
    resetBufferState();
}

void clear() {
    resetBufferState();
}

void recordState(const SystemState& state, uint32_t nowMs) {
    const LiveChartPoint sample = capturePoint(state, nowMs);

    if (intervalElapsed(nowMs, g_lastMinuteSampleMs, MINUTE_INTERVAL_MS)) {
        appendPoint(g_minuteBuffer, sample);
        g_lastMinuteSampleMs = nowMs;
    }

    if (intervalElapsed(nowMs, g_lastAggregateSampleMs, AGGREGATE_INTERVAL_MS)) {
        appendPoint(g_aggregateBuffer, sample);
        g_lastAggregateSampleMs = nowMs;
    }
}

void fillJson(JsonObject root) {
    root["generatedAtMs"] = millis();
    root["minuteIntervalSec"] = MINUTE_INTERVAL_MS / 1000;
    root["aggregateIntervalSec"] = AGGREGATE_INTERVAL_MS / 1000;

    JsonArray minute = root["minute"].to<JsonArray>();
    forEachPointOldestFirst(g_minuteBuffer, [&](const LiveChartPoint& sample) {
        appendPointJson(minute, sample);
    });

    JsonArray aggregate = root["aggregate"].to<JsonArray>();
    forEachPointOldestFirst(g_aggregateBuffer, [&](const LiveChartPoint& sample) {
        appendPointJson(aggregate, sample);
    });
}

} // namespace LiveChartHistory
