#pragma once

#include <stdint.h>

namespace CoolingPwmLogic {

constexpr uint8_t kMinHoldPercent = 5;
constexpr uint8_t kMaxHoldPercent = 100;
constexpr uint16_t kMinPullMs = 100;
constexpr uint16_t kMaxPullMs = 10000;

constexpr uint8_t clampHoldPercent(uint32_t percent) {
  return percent < kMinHoldPercent
             ? kMinHoldPercent
             : percent > kMaxHoldPercent ? kMaxHoldPercent
                                         : static_cast<uint8_t>(percent);
}

constexpr uint16_t clampPullMs(uint32_t pullMs) {
  return pullMs < kMinPullMs
             ? kMinPullMs
             : pullMs > kMaxPullMs ? kMaxPullMs
                                   : static_cast<uint16_t>(pullMs);
}

inline uint8_t percentToDuty(uint32_t percent) {
  if (percent == 0) return 0;
  const uint32_t capped = percent > kMaxHoldPercent ? kMaxHoldPercent : percent;
  return static_cast<uint8_t>((capped * 255U + 50U) / 100U);
}

inline uint8_t dutyToPercent(uint32_t duty) {
  const uint32_t capped = duty > 255U ? 255U : duty;
  return static_cast<uint8_t>((capped * 100U + 127U) / 255U);
}

} // namespace CoolingPwmLogic
