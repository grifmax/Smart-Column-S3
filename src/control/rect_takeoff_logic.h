#pragma once

#include <cstdint>

namespace RectTakeoffLogic {

inline bool shouldUseFullReflux(bool enabled, bool periodicTakeoff,
                                bool periodicTakeoffActive) {
  return !enabled || (periodicTakeoff && !periodicTakeoffActive);
}

inline bool shouldIntegrateVolume(bool backendActive,
                                  float actualEquivalentRateMlH,
                                  uint32_t elapsedMs) {
  return backendActive && actualEquivalentRateMlH > 0.0f && elapsedMs > 0;
}

inline bool shouldRunBackend(bool phaseTakeoffEnabled, bool routeReady,
                             float equivalentRateMlH, bool periodicTakeoff,
                             bool periodicTakeoffActive) {
  return phaseTakeoffEnabled && routeReady && equivalentRateMlH > 0.0f &&
         (!periodicTakeoff || periodicTakeoffActive);
}

} // namespace RectTakeoffLogic
