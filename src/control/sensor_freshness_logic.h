#pragma once

#include <stdint.h>

namespace SensorFreshnessLogic {

inline bool isRecentSample(uint32_t sampleMs, uint32_t nowMs,
                           uint32_t maxAgeMs) {
  return sampleMs != 0 &&
         static_cast<uint32_t>(nowMs - sampleMs) <= maxAgeMs;
}

inline bool isFresh(bool valid, uint32_t sampleMs, uint32_t nowMs,
                    uint32_t maxAgeMs) {
  return valid && isRecentSample(sampleMs, nowMs, maxAgeMs);
}

inline bool isAvailableWhenRequired(bool required, bool valid,
                                    uint32_t sampleMs, uint32_t nowMs,
                                    uint32_t maxAgeMs) {
  return !required || isFresh(valid, sampleMs, nowMs, maxAgeMs);
}

} // namespace SensorFreshnessLogic
