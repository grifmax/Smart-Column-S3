#pragma once
#include <stdint.h>

namespace HmiTouchCalibration {
static constexpr int16_t kLeft = 40;
static constexpr int16_t kRight = 439;
static constexpr int16_t kTop = 60;
static constexpr int16_t kBottom = 279;

// A real XPT2046 axis spans well over a thousand raw counts across the panel.
// A much smaller span means the targets were captured before the finger settled
// or during a drag; storing it would squeeze the whole screen into a corner and
// make every tap land in one dead zone.
static constexpr int32_t kMinRawSpan = 1200;

// Extrapolate the measured inset targets to the physical screen edges.
// Reversed raw axes are intentional and preserved. Reject degenerate spans.
inline bool axis(int16_t lowRaw, int16_t highRaw, int16_t lowPixel,
                 int16_t highPixel, int16_t extent, int16_t &rawAtZero,
                 int16_t &rawAtEnd) {
  const int32_t span = static_cast<int32_t>(highRaw) - lowRaw;
  if (span > -kMinRawSpan && span < kMinRawSpan) return false;
  if (highPixel <= lowPixel || extent < 2) return false;
  const int32_t pixels = highPixel - lowPixel;
  const int32_t a = lowRaw - span * lowPixel / pixels;
  const int32_t b = lowRaw + span * (extent - 1 - lowPixel) / pixels;
  if (a < -4095 || a > 8190 || b < -4095 || b > 8190) return false;
  rawAtZero = static_cast<int16_t>(a);
  rawAtEnd = static_cast<int16_t>(b);
  return true;
}
} // namespace HmiTouchCalibration
