#include <unity.h>

#include <../../src/control/rect_takeoff_logic.h>

using namespace RectTakeoffLogic;

void setUp() {}
void tearDown() {}

static const uint32_t kCycleSec = 60; // RECT_SR_CONTROL_CYCLE_SEC

// --- ML_H: непрерывный отбор -------------------------------------------

void test_continuous_takeoff_is_always_open() {
  uint32_t cycle = 1;
  uint32_t open = 1;
  TEST_ASSERT_TRUE(isTakeoffWindowOpen(0, 0.0f, 0, 0, 0, kCycleSec, &cycle, &open));
  TEST_ASSERT_EQUAL_UINT32(0, cycle);
  TEST_ASSERT_EQUAL_UINT32(0, open);
  TEST_ASSERT_TRUE(isTakeoffWindowOpen(0, 5.0f, 900, 90, 123456, kCycleSec));
}

// --- SR_RATIO: флегмовое число -----------------------------------------

void test_sr_ratio_zero_closes_window() {
  uint32_t cycle = 0;
  uint32_t open = 99;
  TEST_ASSERT_FALSE(
      isTakeoffWindowOpen(1, 0.0f, 0, 0, 0, kCycleSec, &cycle, &open));
  TEST_ASSERT_EQUAL_UINT32(kCycleSec * 1000UL, cycle);
  TEST_ASSERT_EQUAL_UINT32(0, open);
}

void test_sr_ratio_divides_cycle_by_ratio_plus_one() {
  uint32_t open = 0;
  // ratio 1 => половина цикла открыто; 60 с цикла => 30 000 мс
  TEST_ASSERT_TRUE(isTakeoffWindowOpen(1, 1.0f, 0, 0, 0, kCycleSec, nullptr, &open));
  TEST_ASSERT_EQUAL_UINT32(30000, open);

  // ratio 3 => четверть цикла
  TEST_ASSERT_TRUE(isTakeoffWindowOpen(1, 3.0f, 0, 0, 0, kCycleSec, nullptr, &open));
  TEST_ASSERT_EQUAL_UINT32(15000, open);

  // ratio 9 => 6 000 мс
  TEST_ASSERT_TRUE(isTakeoffWindowOpen(1, 9.0f, 0, 0, 0, kCycleSec, nullptr, &open));
  TEST_ASSERT_EQUAL_UINT32(6000, open);
}

void test_sr_ratio_window_boundaries() {
  uint32_t open = 0;
  isTakeoffWindowOpen(1, 1.0f, 0, 0, 0, kCycleSec, nullptr, &open); // 30000
  const uint32_t cycleMs = kCycleSec * 1000UL;

  // на границе открытия — ещё открыто
  TEST_ASSERT_TRUE(isTakeoffWindowOpen(1, 1.0f, 0, 0, 0, kCycleSec));
  TEST_ASSERT_TRUE(isTakeoffWindowOpen(1, 1.0f, 0, 0, open - 1, kCycleSec));
  // ровно в момент закрытия — закрыто
  TEST_ASSERT_FALSE(isTakeoffWindowOpen(1, 1.0f, 0, 0, open, kCycleSec));
  TEST_ASSERT_FALSE(isTakeoffWindowOpen(1, 1.0f, 0, 0, cycleMs - 1, kCycleSec));
  // следующий цикл снова открыт
  TEST_ASSERT_TRUE(isTakeoffWindowOpen(1, 1.0f, 0, 0, cycleMs, kCycleSec));
}

void test_sr_ratio_never_yields_zero_open_time() {
  uint32_t open = 0;
  // Расчётное окно не должно стать нулевым; фактическое попадание зависит
  // от частоты выполнения цикла управления.
  TEST_ASSERT_TRUE(
      isTakeoffWindowOpen(1, 100000.0f, 0, 0, 0, kCycleSec, nullptr, &open));
  TEST_ASSERT_EQUAL_UINT32(1, open);
}

void test_sr_ratio_ignores_autonomous_settings() {
  uint32_t open = 0;
  // Поля автоцикла не должны влиять на режим флегмы.
  TEST_ASSERT_TRUE(
      isTakeoffWindowOpen(1, 1.0f, 10, 9, 0, kCycleSec, nullptr, &open));
  TEST_ASSERT_EQUAL_UINT32(30000, open);
}

// --- AUTONOMOUS: цикл и пауза ------------------------------------------

void test_autonomous_cycle_opens_for_cycle_minus_pause() {
  uint32_t cycle = 0;
  uint32_t open = 0;
  // 900 с цикл, 90 с пауза => 810 с открыто
  TEST_ASSERT_TRUE(
      isTakeoffWindowOpen(2, 0.0f, 900, 90, 0, kCycleSec, &cycle, &open));
  TEST_ASSERT_EQUAL_UINT32(900000, cycle);
  TEST_ASSERT_EQUAL_UINT32(810000, open);

  // в конце открытой части ещё открыто, в начале паузы — закрыто
  TEST_ASSERT_TRUE(isTakeoffWindowOpen(2, 0.0f, 900, 90, open - 1, kCycleSec));
  TEST_ASSERT_FALSE(isTakeoffWindowOpen(2, 0.0f, 900, 90, open, kCycleSec));
  TEST_ASSERT_FALSE(isTakeoffWindowOpen(2, 0.0f, 900, 90, cycle - 1, kCycleSec));
  TEST_ASSERT_TRUE(isTakeoffWindowOpen(2, 0.0f, 900, 90, cycle, kCycleSec));
}

void test_autonomous_pause_cannot_exceed_cycle() {
  uint32_t open = 0;
  // Пауза больше цикла урезается до «цикл минус секунда», иначе окно
  // закрылось бы целиком и отбор не начался никогда.
  TEST_ASSERT_TRUE(
      isTakeoffWindowOpen(2, 0.0f, 60, 600, 0, kCycleSec, nullptr, &open));
  TEST_ASSERT_EQUAL_UINT32(1000, open);

  // Равные цикл и пауза дают то же самое.
  TEST_ASSERT_TRUE(
      isTakeoffWindowOpen(2, 0.0f, 60, 60, 0, kCycleSec, nullptr, &open));
  TEST_ASSERT_EQUAL_UINT32(1000, open);
}

void test_autonomous_zero_cycle_falls_back_to_one_second() {
  uint32_t cycle = 0;
  uint32_t open = 0;
  TEST_ASSERT_TRUE(
      isTakeoffWindowOpen(2, 0.0f, 0, 0, 0, kCycleSec, &cycle, &open));
  TEST_ASSERT_EQUAL_UINT32(1000, cycle);
  TEST_ASSERT_EQUAL_UINT32(1000, open);
}

void test_autonomous_zero_pause_keeps_window_open() {
  uint32_t open = 0;
  TEST_ASSERT_TRUE(
      isTakeoffWindowOpen(2, 0.0f, 120, 0, 119999, kCycleSec, nullptr, &open));
  TEST_ASSERT_EQUAL_UINT32(120000, open);
}

// --- Общее -------------------------------------------------------------

void test_null_out_params_are_safe() {
  TEST_ASSERT_TRUE(isTakeoffWindowOpen(0, 0.0f, 0, 0, 0, kCycleSec));
  TEST_ASSERT_FALSE(isTakeoffWindowOpen(1, 0.0f, 0, 0, 0, kCycleSec));
  TEST_ASSERT_TRUE(isTakeoffWindowOpen(2, 0.0f, 900, 90, 0, kCycleSec));
}

void test_unknown_mode_behaves_as_autonomous() {
  // Любое значение кроме 0 и 1 трактуется как автоцикл — так же, как было в
  // обработчиках до выноса.
  uint32_t open = 0;
  TEST_ASSERT_TRUE(
      isTakeoffWindowOpen(7, 0.0f, 900, 90, 0, kCycleSec, nullptr, &open));
  TEST_ASSERT_EQUAL_UINT32(810000, open);
}

void test_invalid_sr_control_cycle_fails_closed() {
  uint32_t cycle = 42;
  uint32_t open = 42;
  TEST_ASSERT_FALSE(isTakeoffWindowOpen(1, 1.0f, 0, 0, 1, 0, &cycle, &open));
  TEST_ASSERT_EQUAL_UINT32(0, cycle);
  TEST_ASSERT_EQUAL_UINT32(0, open);
  TEST_ASSERT_FALSE(isTakeoffWindowOpen(1, 1.0f, 0, 0, 1, UINT32_MAX));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_continuous_takeoff_is_always_open);
  RUN_TEST(test_sr_ratio_zero_closes_window);
  RUN_TEST(test_sr_ratio_divides_cycle_by_ratio_plus_one);
  RUN_TEST(test_sr_ratio_window_boundaries);
  RUN_TEST(test_sr_ratio_never_yields_zero_open_time);
  RUN_TEST(test_sr_ratio_ignores_autonomous_settings);
  RUN_TEST(test_autonomous_cycle_opens_for_cycle_minus_pause);
  RUN_TEST(test_autonomous_pause_cannot_exceed_cycle);
  RUN_TEST(test_autonomous_zero_cycle_falls_back_to_one_second);
  RUN_TEST(test_autonomous_zero_pause_keeps_window_open);
  RUN_TEST(test_null_out_params_are_safe);
  RUN_TEST(test_unknown_mode_behaves_as_autonomous);
  RUN_TEST(test_invalid_sr_control_cycle_fails_closed);
  return UNITY_END();
}