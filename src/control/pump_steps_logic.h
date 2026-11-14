#pragma once
#include <stdint.h>

// Steps per revolution is the product of motor steps and driver microsteps.
// Both values live in NVS and can be corrupted by older firmware or by a UI
// that saves impulses back into the motor-step field, so every consumer must
// go through this sanitiser instead of trusting the stored pair.
namespace PumpSteps {

static constexpr uint32_t kMinStepsPerRevolution = 1;
static constexpr uint32_t kMaxStepsPerRevolution = 2000;
static constexpr uint32_t kMaxMicrosteps = 64;
static constexpr uint32_t kMaxPulsesPerRevolution = 51200;

inline bool isMicrostepsValue(uint32_t microsteps) {
  return microsteps >= 1 && microsteps <= kMaxMicrosteps &&
         (microsteps & (microsteps - 1)) == 0;
}

inline bool isStepsValue(uint32_t steps) {
  return steps >= kMinStepsPerRevolution && steps <= kMaxStepsPerRevolution;
}

// Returns steps * microsteps, or the default pair when either stored value is
// outside the range a real motor/driver combination can have.
inline uint32_t pulsesPerRevolution(uint32_t steps, uint32_t microsteps,
                                    uint32_t defaultSteps,
                                    uint32_t defaultMicrosteps) {
  if (!isStepsValue(steps) || !isMicrostepsValue(microsteps)) {
    steps = defaultSteps;
    microsteps = isMicrostepsValue(defaultMicrosteps) ? defaultMicrosteps : 16;
  }
  const uint32_t product = steps * microsteps;
  if (product == 0 || product > kMaxPulsesPerRevolution) {
    return defaultSteps * defaultMicrosteps;
  }
  return product;
}

} // namespace PumpSteps
