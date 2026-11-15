#include <unity.h>

#include <../../src/control/rate_estimator_logic.h>

using RateEstimatorLogic::Estimator;

namespace {

constexpr uint32_t kWindowMs = 45000;
constexpr uint8_t kMinPoints = 10;

// Заполняет оценщик отсчётами с шагом 1 с; valueFn получает секунды от начала.
template <typename ValueFn>
void fill(Estimator &estimator, uint32_t startMs, uint8_t count, ValueFn valueFn) {
  for (uint8_t i = 0; i < count; ++i) {
    const uint32_t ms = startMs + static_cast<uint32_t>(i) * 1000UL;
    RateEstimatorLogic::push(estimator, ms, valueFn(i));
  }
}

}  // namespace

// Шум квантования DS18B20 (+-1 разряд = 0.0625 C) не должен выглядеть как
// скорость: именно на этом ловились ложные WATER_RISE_RATE.
void test_noise_does_not_look_like_a_rise() {
  Estimator estimator;
  fill(estimator, 1000, 46, [](uint8_t i) {
    return 40.0f + ((i % 2 == 0) ? 0.0625f : -0.0625f);
  });
  float slope = 0.0f;
  TEST_ASSERT_TRUE(RateEstimatorLogic::slopePerMin(
      estimator, 1000 + 45 * 1000UL, kWindowMs, kMinPoints, slope));
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, slope);
}

// Настоящий рост 8 C/мин должен читаться как 8 C/мин.
void test_real_ramp_is_measured() {
  Estimator estimator;
  fill(estimator, 0, 46, [](uint8_t i) {
    return 40.0f + 8.0f * (static_cast<float>(i) / 60.0f);
  });
  float slope = 0.0f;
  TEST_ASSERT_TRUE(RateEstimatorLogic::slopePerMin(
      estimator, 45 * 1000UL, kWindowMs, kMinPoints, slope));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 8.0f, slope);
}

// Одиночный выброс не должен превращаться в аварию.
void test_single_spike_is_suppressed() {
  Estimator estimator;
  fill(estimator, 0, 46, [](uint8_t i) {
    return i == 20 ? 45.0f : 40.0f;
  });
  float slope = 0.0f;
  TEST_ASSERT_TRUE(RateEstimatorLogic::slopePerMin(
      estimator, 45 * 1000UL, kWindowMs, kMinPoints, slope));
  TEST_ASSERT_TRUE(fabsf(slope) < 2.0f);
}

// Мало точек — вердикта нет, а не «ноль».
void test_too_few_points_reports_no_estimate() {
  Estimator estimator;
  fill(estimator, 0, 5, [](uint8_t) { return 40.0f; });
  float slope = 123.0f;
  TEST_ASSERT_FALSE(RateEstimatorLogic::slopePerMin(
      estimator, 5 * 1000UL, kWindowMs, kMinPoints, slope));
}

// Отсчёты старше окна не участвуют: после паузы в подаче данных оценка
// строится только по свежим точкам.
void test_samples_outside_window_are_ignored() {
  Estimator estimator;
  fill(estimator, 0, 20, [](uint8_t) { return 20.0f; });
  fill(estimator, 120000, 20, [](uint8_t) { return 50.0f; });
  float slope = 0.0f;
  TEST_ASSERT_TRUE(RateEstimatorLogic::slopePerMin(
      estimator, 120000 + 19 * 1000UL, kWindowMs, kMinPoints, slope));
  // Внутри окна только ровные 50 C, старая ступень 20 -> 50 не попадает.
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, slope);
}

void test_estimator_reset_clears_history() {
  Estimator estimator;
  fill(estimator, 0, 46, [](uint8_t i) { return 40.0f + i; });
  RateEstimatorLogic::reset(estimator);
  float slope = 0.0f;
  TEST_ASSERT_EQUAL_UINT8(0, estimator.count);
  TEST_ASSERT_FALSE(RateEstimatorLogic::slopePerMin(
      estimator, 50000, kWindowMs, kMinPoints, slope));
}

void setUp(void) {}

void tearDown(void) {}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_noise_does_not_look_like_a_rise);
  RUN_TEST(test_real_ramp_is_measured);
  RUN_TEST(test_single_spike_is_suppressed);
  RUN_TEST(test_too_few_points_reports_no_estimate);
  RUN_TEST(test_samples_outside_window_are_ignored);
  RUN_TEST(test_estimator_reset_clears_history);
  return UNITY_END();
}
