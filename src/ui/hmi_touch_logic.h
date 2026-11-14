#pragma once

#include <stdint.h>

// Pure touch recognition only: input coordinates must already be calibrated.
// No Arduino, SPI, globals, delays, callbacks, or control actions.
namespace HmiTouch {

struct Sample {
  bool pressed;
  int16_t x;
  int16_t y;
};

struct Event {
  bool pressed = false;
  bool tapped = false;
  bool released = false;
  int16_t x = 0;
  int16_t y = 0;
};

static constexpr uint32_t kReleaseDebounceMs = 40;
static constexpr uint32_t kMinHoldMs = 20;
static constexpr uint32_t kMaxHoldMs = 1500;
static constexpr uint32_t kMaxExcursionPx = 24;
static constexpr uint32_t kMinTapGapMs = 160;
static constexpr uint32_t kScreenGuardMs = 280;

struct State {
  bool pressed = false;
  bool releasePending = false;
  uint32_t downAtMs = 0;
  uint32_t releaseAtMs = 0;
  int16_t lastX = 0;
  int16_t lastY = 0;
  int16_t downX = 0;
  int16_t downY = 0;
  uint32_t maxExcursionPx = 0;
  bool hasLastTap = false;
  uint32_t lastTapAtMs = 0;
  bool guardActive = false;
  uint32_t guardUntilMs = 0;
  bool tapBlocked = false;
};

// Durations and intervals between updates must be below 2^31 ms.
// A transition invalidates an in-flight contact, even if held past the guard.
inline void armScreenGuard(State &state, uint32_t now,
                           uint32_t durationMs = kScreenGuardMs) {
  state.guardUntilMs = now + durationMs;
  state.guardActive = durationMs != 0;
  if (state.pressed) state.tapBlocked = true;
}

// Feed monotonically advancing uint32_t timestamps, normally every 25 ms.
// Press is immediate; release needs 40 ms since the first false sample.
// Hold excludes the release debounce. Gap is measured between emitted taps.
// On a release event x/y refer to the finished contact; otherwise to the
// current contact. A new press can coexist with the preceding release event.
inline Event update(State &state, const Sample &sample, uint32_t now) {
  Event event;
  if (state.guardActive &&
      static_cast<uint32_t>(now - state.guardUntilMs) < 0x80000000u) {
    state.guardActive = false;
  }

  if (state.pressed && !sample.pressed && !state.releasePending) {
    state.releasePending = true;
    state.releaseAtMs = now;
  }
  // Complete an observed release before consuming a new true sample. A true
  // sample after <40 ms is bounce; after >=40 ms it starts a new contact.
  if (state.pressed && state.releasePending &&
      static_cast<uint32_t>(now - state.releaseAtMs) >= kReleaseDebounceMs) {
    state.pressed = false;
    state.releasePending = false;
    event.released = true;
    event.x = state.lastX;
    event.y = state.lastY;
    const uint32_t holdMs = state.releaseAtMs - state.downAtMs;
    event.tapped = !state.tapBlocked && !state.guardActive &&
                   holdMs >= kMinHoldMs && holdMs <= kMaxHoldMs &&
                   state.maxExcursionPx <= kMaxExcursionPx &&
                   (!state.hasLastTap ||
                    static_cast<uint32_t>(now - state.lastTapAtMs) >= kMinTapGapMs);
    if (event.tapped) {
      state.hasLastTap = true;
      state.lastTapAtMs = now;
    }
  }

  if (sample.pressed) {
    if (!state.pressed) {
      state.downAtMs = now;
      state.downX = sample.x;
      state.downY = sample.y;
      state.maxExcursionPx = 0;
      state.tapBlocked = state.guardActive;
    }
    const int32_t dx = static_cast<int32_t>(sample.x) - state.downX;
    const int32_t dy = static_cast<int32_t>(sample.y) - state.downY;
    const uint32_t excursion = static_cast<uint32_t>(
        (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
    if (excursion > state.maxExcursionPx) state.maxExcursionPx = excursion;
    state.pressed = true;
    state.releasePending = false;
    state.lastX = sample.x;
    state.lastY = sample.y;
  }
  event.pressed = state.pressed;
  if (!event.released) {
    event.x = state.lastX;
    event.y = state.lastY;
  }
  return event;
}

} // namespace HmiTouch
