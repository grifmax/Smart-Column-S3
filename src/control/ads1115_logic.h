#pragma once

#include <cstdint>

// ADS1115 single-shot policy. The transport and clock are injected so channel
// setup, bounded polling, and failure paths can be tested without hardware.
namespace Ads1115Logic {

constexpr uint16_t kOsStart = 0x8000;
constexpr uint16_t kGainOne = 0x0200;       // +/-4.096 V
constexpr uint16_t kSingleShot = 0x0100;
constexpr uint16_t kRate128Sps = 0x0080;
constexpr uint16_t kDisableComparator = 0x0003;

constexpr uint16_t singleEndedConfig(uint8_t channel) {
  return channel < 4
             ? static_cast<uint16_t>(kOsStart | 0x4000U |
                                     (static_cast<uint16_t>(channel) << 12) |
                                     kGainOne | kSingleShot | kRate128Sps |
                                     kDisableComparator)
             : 0;
}

constexpr bool conversionReady(uint16_t config) {
  return (config & kOsStart) != 0;
}

inline float voltsFromRaw(int16_t raw) {
  return static_cast<float>(raw) * (4.096f / 32768.0f);
}

constexpr bool timedOut(uint32_t nowMs, uint32_t startedAtMs,
                        uint32_t timeoutMs) {
  return static_cast<uint32_t>(nowMs - startedAtMs) >= timeoutMs;
}

template <typename Bus, typename Clock>
bool readSingleEnded(Bus &bus, Clock &clock, uint8_t channel,
                     uint32_t timeoutMs, int16_t &raw, float &volts) {
  raw = 0;
  volts = 0.0f;
  const uint16_t config = singleEndedConfig(channel);
  if (config == 0 || !bus.writeRegister(0x01, config)) return false;

  const uint32_t startedAtMs = clock.nowMs();
  while (!timedOut(clock.nowMs(), startedAtMs, timeoutMs)) {
    uint16_t currentConfig = 0;
    if (!bus.readRegister(0x01, currentConfig)) return false;
    if (conversionReady(currentConfig)) {
      uint16_t result = 0;
      if (!bus.readRegister(0x00, result)) return false;
      raw = static_cast<int16_t>(result);
      volts = voltsFromRaw(raw);
      return true;
    }
    clock.waitOneMs();
  }
  return false;
}

} // namespace Ads1115Logic
