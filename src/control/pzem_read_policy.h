#pragma once

#include <stdint.h>

namespace PzemReadPolicy {

inline bool isFreshSample(bool pzemConnected, bool sampleValid, uint32_t nowMs,
                          uint32_t lastUpdateMs, uint32_t maxAgeMs) {
    return pzemConnected && sampleValid && lastUpdateMs > 0 &&
           static_cast<uint32_t>(nowMs - lastUpdateMs) <= maxAgeMs;
}

inline bool isStaleSample(bool sampleChanged, uint32_t nowMs,
                          uint32_t unchangedSinceMs, uint32_t maxAgeMs) {
    if (sampleChanged || unchangedSinceMs == 0) {
        return false;
    }
    return static_cast<uint32_t>(nowMs - unchangedSinceMs) >= maxAgeMs;
}

}  // namespace PzemReadPolicy
