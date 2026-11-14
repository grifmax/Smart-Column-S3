#pragma once

#include <stdint.h>

namespace ProcessResumeLogic {

constexpr uint32_t kSnapshotIntervalMs = 5UL * 60UL * 1000UL;
constexpr float kMinSuggestedRecoveryHeadsMl = 50.0f;
constexpr float kMaxSuggestedRecoveryHeadsMl = 100.0f;

inline float clamp(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

inline float suggestedRecoveryHeadsMl(float feedVolumeL,
                                      float cubeVolumeL) {
  if (feedVolumeL <= 0.0f || cubeVolumeL <= 0.0f) {
    return kMinSuggestedRecoveryHeadsMl;
  }
  return clamp(kMaxSuggestedRecoveryHeadsMl * feedVolumeL / cubeVolumeL,
               kMinSuggestedRecoveryHeadsMl,
               kMaxSuggestedRecoveryHeadsMl);
}

inline bool shouldOfferRecoveryHeads(uint8_t phase, float headsVolumeMl,
                                     bool recoveryActive,
                                     uint8_t recoveryTargetPhase,
                                     float recoveryHeadsBaseMl) {
  // RectPhase::BODY is deliberately represented as its stable serialized
  // value here, so this policy remains host-testable without Arduino types.
  const uint8_t targetPhase = recoveryActive ? recoveryTargetPhase : phase;
  const float headsAlreadyCollected = recoveryActive
                                          ? recoveryHeadsBaseMl
                                          : headsVolumeMl;
  return targetPhase == 5U && headsAlreadyCollected > 0.0f;
}

inline bool isRecoverableRectificationPhase(uint8_t phase) {
  // HEATING through PURGE. FINISH is intentionally excluded: a completed
  // collection must never restart because of a stale snapshot.
  return phase >= 1U && phase <= 7U;
}

inline uint32_t crc32(const uint8_t* data, uint32_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (uint32_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = static_cast<uint32_t>(-
          static_cast<int32_t>(crc & 1U));
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

}  // namespace ProcessResumeLogic
