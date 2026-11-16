#include <unity.h>

#include <../../src/ui/hmi_touch_logic.h>
#include <../../src/ui/hmi_touch_calibration_logic.h>

void setUp() {}
void tearDown() {}

using HmiTouch::Event;
using HmiTouch::Sample;
using HmiTouch::State;

static Event feed(State &state, bool pressed, int16_t x, int16_t y, uint32_t now) {
  return HmiTouch::update(state, Sample{pressed, x, y}, now);
}

void test_touch_press_is_immediate_and_release_is_debounced() {
  State state;
  Event event = feed(state, true, 120, 80, 100);
  TEST_ASSERT_TRUE(event.pressed);
  TEST_ASSERT_FALSE(event.tapped);
  TEST_ASSERT_FALSE(event.released);

  event = feed(state, false, 120, 80, 150);
  TEST_ASSERT_TRUE(event.pressed);
  TEST_ASSERT_FALSE(event.released);
  TEST_ASSERT_FALSE(event.tapped);

  event = feed(state, false, 120, 80, 190);
  TEST_ASSERT_FALSE(event.pressed);
  TEST_ASSERT_TRUE(event.released);
  TEST_ASSERT_TRUE(event.tapped);
  TEST_ASSERT_EQUAL_INT16(120, event.x);
  TEST_ASSERT_EQUAL_INT16(80, event.y);
}

void test_release_bounce_cancels_pending_release() {
  State state;
  feed(state, true, 50, 60, 100);
  feed(state, false, 50, 60, 130);
  Event event = feed(state, true, 52, 61, 150);
  TEST_ASSERT_TRUE(event.pressed);
  TEST_ASSERT_FALSE(event.released);
  TEST_ASSERT_FALSE(event.tapped);

  feed(state, false, 52, 61, 180);
  event = feed(state, false, 52, 61, 220);
  TEST_ASSERT_TRUE(event.released);
  TEST_ASSERT_TRUE(event.tapped);
}

void test_tap_rejects_short_and_long_holds() {
  State shortHold;
  feed(shortHold, true, 10, 10, 100);
  feed(shortHold, false, 10, 10, 115);
  Event event = feed(shortHold, false, 10, 10, 155);
  TEST_ASSERT_TRUE(event.released);
  TEST_ASSERT_FALSE(event.tapped);

  State longHold;
  feed(longHold, true, 10, 10, 100);
  feed(longHold, false, 10, 10, 1700);
  event = feed(longHold, false, 10, 10, 1740);
  TEST_ASSERT_TRUE(event.released);
  TEST_ASSERT_FALSE(event.tapped);
}

void test_tap_rejects_excursion_over_limit() {
  State state;
  feed(state, true, 100, 100, 100);
  feed(state, true, 112, 112, 150); // Manhattan excursion 24 px.
  feed(state, false, 112, 112, 160);
  Event event = feed(state, false, 112, 112, 200);
  TEST_ASSERT_TRUE(event.released);
  TEST_ASSERT_TRUE(event.tapped);

  State moved;
  feed(moved, true, 100, 100, 100);
  feed(moved, true, 113, 112, 150); // 25 px: drag, not tap.
  feed(moved, false, 113, 112, 160);
  event = feed(moved, false, 113, 112, 200);
  TEST_ASSERT_TRUE(event.released);
  TEST_ASSERT_FALSE(event.tapped);
}

void test_tap_gap_is_measured_between_emitted_taps() {
  State state;
  feed(state, true, 20, 20, 100);
  feed(state, false, 20, 20, 130);
  Event event = feed(state, false, 20, 20, 170);
  TEST_ASSERT_TRUE(event.tapped);

  feed(state, true, 20, 20, 200);
  feed(state, false, 20, 20, 230);
  event = feed(state, false, 20, 20, 270); // Only 100 ms after prior tap.
  TEST_ASSERT_FALSE(event.tapped);

  feed(state, true, 20, 20, 400);
  feed(state, false, 20, 20, 430);
  event = feed(state, false, 20, 20, 470); // 300 ms after prior tap.
  TEST_ASSERT_TRUE(event.tapped);
}

void test_screen_guard_blocks_current_contact_and_new_taps_during_guard() {
  State state;
  feed(state, true, 10, 10, 100);
  HmiTouch::armScreenGuard(state, 110, 280);
  feed(state, false, 10, 10, 150);
  Event event = feed(state, false, 10, 10, 190);
  TEST_ASSERT_TRUE(event.released);
  TEST_ASSERT_FALSE(event.tapped);

  feed(state, true, 10, 10, 200);
  feed(state, false, 10, 10, 230);
  event = feed(state, false, 10, 10, 270);
  TEST_ASSERT_FALSE(event.tapped);

  feed(state, true, 10, 10, 400);
  feed(state, false, 10, 10, 430);
  event = feed(state, false, 10, 10, 470);
  TEST_ASSERT_TRUE(event.tapped);
}

void test_touch_timing_survives_millis_rollover() {
  State state;
  const uint32_t down = 0xFFFFFFF0u;
  feed(state, true, 25, 35, down);
  feed(state, false, 25, 35, down + 30u);
  Event event = feed(state, false, 25, 35, down + 70u);
  TEST_ASSERT_TRUE(event.released);
  TEST_ASSERT_TRUE(event.tapped);
}

void test_touch_guard_expiry_survives_millis_rollover() {
  State state;
  const uint32_t now = 0xFFFFFF00u;
  HmiTouch::armScreenGuard(state, now, 280);
  Event event = feed(state, true, 40, 50, now + 300u);
  TEST_ASSERT_TRUE(event.pressed);
  feed(state, false, 40, 50, now + 330u);
  event = feed(state, false, 40, 50, now + 370u);
  TEST_ASSERT_TRUE(event.tapped);
}

void test_calibration_axis_maps_inset_targets_to_screen_edges() {
  int16_t zero = 0;
  int16_t end = 0;
  TEST_ASSERT_TRUE(HmiTouchCalibration::axis(
      1000, 5000, HmiTouchCalibration::kLeft, HmiTouchCalibration::kRight,
      480, zero, end));
  TEST_ASSERT_EQUAL_INT16(599, zero);
  TEST_ASSERT_EQUAL_INT16(5401, end);
}

void test_calibration_axis_preserves_reversed_raw_direction() {
  int16_t zero = 0;
  int16_t end = 0;
  TEST_ASSERT_TRUE(HmiTouchCalibration::axis(
      5000, 1000, HmiTouchCalibration::kLeft, HmiTouchCalibration::kRight,
      480, zero, end));
  TEST_ASSERT_EQUAL_INT16(5401, zero);
  TEST_ASSERT_EQUAL_INT16(599, end);
  TEST_ASSERT_TRUE(zero > end);
}

void test_calibration_axis_accepts_minimum_raw_span() {
  int16_t zero = 0;
  int16_t end = 0;
  TEST_ASSERT_TRUE(HmiTouchCalibration::axis(
      1000, 2200, HmiTouchCalibration::kLeft, HmiTouchCalibration::kRight,
      480, zero, end));
}

void test_calibration_axis_rejects_span_below_minimum() {
  int16_t zero = 0;
  int16_t end = 0;
  TEST_ASSERT_FALSE(HmiTouchCalibration::axis(
      1000, 2199, HmiTouchCalibration::kLeft, HmiTouchCalibration::kRight,
      480, zero, end));
  TEST_ASSERT_FALSE(HmiTouchCalibration::axis(
      2200, 1001, HmiTouchCalibration::kLeft, HmiTouchCalibration::kRight,
      480, zero, end));
}

void test_calibration_axis_rejects_invalid_geometry() {
  int16_t zero = 0;
  int16_t end = 0;
  TEST_ASSERT_FALSE(HmiTouchCalibration::axis(1000, 5000, 50, 50, 480,
                                               zero, end));
  TEST_ASSERT_FALSE(HmiTouchCalibration::axis(1000, 5000, 40, 439, 1,
                                               zero, end));
}

void test_calibration_axis_rejects_extrapolation_outside_supported_range() {
  int16_t zero = 0;
  int16_t end = 0;
  TEST_ASSERT_FALSE(HmiTouchCalibration::axis(
      -4000, 4000, HmiTouchCalibration::kLeft, HmiTouchCalibration::kRight,
      480, zero, end));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_touch_press_is_immediate_and_release_is_debounced);
  RUN_TEST(test_release_bounce_cancels_pending_release);
  RUN_TEST(test_tap_rejects_short_and_long_holds);
  RUN_TEST(test_tap_rejects_excursion_over_limit);
  RUN_TEST(test_tap_gap_is_measured_between_emitted_taps);
  RUN_TEST(test_screen_guard_blocks_current_contact_and_new_taps_during_guard);
  RUN_TEST(test_touch_timing_survives_millis_rollover);
  RUN_TEST(test_touch_guard_expiry_survives_millis_rollover);
  RUN_TEST(test_calibration_axis_maps_inset_targets_to_screen_edges);
  RUN_TEST(test_calibration_axis_preserves_reversed_raw_direction);
  RUN_TEST(test_calibration_axis_accepts_minimum_raw_span);
  RUN_TEST(test_calibration_axis_rejects_span_below_minimum);
  RUN_TEST(test_calibration_axis_rejects_invalid_geometry);
  RUN_TEST(test_calibration_axis_rejects_extrapolation_outside_supported_range);
  return UNITY_END();
}
