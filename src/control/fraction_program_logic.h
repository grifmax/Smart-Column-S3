#pragma once

#include <cstdint>

namespace FractionProgramLogic {

enum class EndReason : uint8_t {
  None = 0,
  Volume,
  Time,
  Temperature,
  Level,
  Manual
};

inline EndReason selectEndReason(bool manualAdvanceRequested, bool volumeReached,
                                 bool timeReached, bool temperatureReached,
                                 bool levelReached) {
  if (manualAdvanceRequested) return EndReason::Manual;
  if (volumeReached) return EndReason::Volume;
  if (timeReached) return EndReason::Time;
  if (temperatureReached) return EndReason::Temperature;
  if (levelReached) return EndReason::Level;
  return EndReason::None;
}

inline bool shouldFinish(bool manualAdvanceRequested, bool volumeReached,
                         bool timeReached, bool temperatureReached,
                         bool levelReached) {
  return selectEndReason(manualAdvanceRequested, volumeReached, timeReached,
                         temperatureReached, levelReached) != EndReason::None;
}

inline uint32_t advanceTimestampAfterPause(uint32_t timestampMs,
                                           uint32_t pausedDurationMs) {
  return timestampMs + pausedDurationMs;
}
constexpr uint32_t kRoutingTimeoutMs = 30000;

inline bool hasRouteTimedOut(bool routeReady, uint32_t routingElapsedMs,
                             uint32_t timeoutMs = kRoutingTimeoutMs) {
  return !routeReady && routingElapsedMs >= timeoutMs;
}
inline bool isRouteSettled(bool routeReady, uint32_t routingElapsedMs,
                           uint32_t settlingMs) {
  return routeReady && routingElapsedMs >= settlingMs;
}
} // namespace FractionProgramLogic