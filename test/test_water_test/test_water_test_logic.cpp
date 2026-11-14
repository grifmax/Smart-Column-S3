#include <unity.h>

#include <../../src/control/water_test_logic.h>

using namespace WaterTestLogic;

// --- Мощность -------------------------------------------------------------

void test_power_match_thresholds() {
  // Точное совпадение и мелкая ошибка — годен.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::PASS),
      static_cast<uint8_t>(evaluatePowerMatch(3000.0f, 3000.0f).verdict));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::PASS),
      static_cast<uint8_t>(evaluatePowerMatch(3000.0f, 2850.0f).verdict));
  // 15 % — предупреждение (сбитая калибровка, но процесс возможен).
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::WARN),
      static_cast<uint8_t>(evaluatePowerMatch(3000.0f, 2550.0f).verdict));
  // 30 % — провал: ТЭН, симистор или неверная паспортная мощность.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::FAIL),
      static_cast<uint8_t>(evaluatePowerMatch(3000.0f, 2100.0f).verdict));
  // Нулевая уставка не проверяется.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::SKIP),
      static_cast<uint8_t>(evaluatePowerMatch(0.0f, 0.0f).verdict));
}

void test_heat_rate_requires_growth() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::PASS),
      static_cast<uint8_t>(evaluateHeatRate(0.8f).verdict));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::FAIL),
      static_cast<uint8_t>(evaluateHeatRate(0.0f).verdict));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::FAIL),
      static_cast<uint8_t>(evaluateHeatRate(-0.2f).verdict));
}

// --- Zero-cross -----------------------------------------------------------

void test_zero_cross_requires_real_pulses() {
  // 50 Гц, два фронта на период: 100 импульсов в секунду.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::PASS),
      static_cast<uint8_t>(evaluateZeroCross(6000, 60000).verdict));
  // Мёртвый детектор: ТЭН фактически не управляется.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::FAIL),
      static_cast<uint8_t>(evaluateZeroCross(0, 60000).verdict));
  // Половина импульсов — тоже провал.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::FAIL),
      static_cast<uint8_t>(evaluateZeroCross(3000, 60000).verdict));
  // Нулевое окно измерения не даёт вердикта.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::SKIP),
      static_cast<uint8_t>(evaluateZeroCross(10, 0).verdict));
}

// --- Охлаждение -----------------------------------------------------------

void test_cooling_capacity_math() {
  // 2 л/мин при dT 10 C: 2/60 * 4186 * 10 = 1395 Вт.
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 1395.3f, coolingCapacityW(2.0f, 10.0f));
  // Нет протока — нет теплосъёма.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, coolingCapacityW(0.0f, 10.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, coolingCapacityW(2.0f, 0.0f));
}

void test_cooling_margin_verdict() {
  // Теплосъём 1395 Вт против ТЭНа 1000 Вт: запас 1.2x выдержан.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::PASS),
      static_cast<uint8_t>(evaluateCooling(2.0f, 10.0f, 1000.0f).verdict));
  // Против 2000 Вт тот же теплосъём уже недостаточен.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::FAIL),
      static_cast<uint8_t>(evaluateCooling(2.0f, 10.0f, 2000.0f).verdict));
  // Нулевой проток — провал независимо от dT.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::FAIL),
      static_cast<uint8_t>(evaluateCooling(0.0f, 25.0f, 500.0f).verdict));
}

// --- Отбор ----------------------------------------------------------------

void test_pump_volume_tolerance() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::PASS),
      static_cast<uint8_t>(evaluatePumpVolume(50.0f, 48.0f).verdict));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::WARN),
      static_cast<uint8_t>(evaluatePumpVolume(50.0f, 46.0f).verdict));
  // Срыв прайма: счётчик накрутил, продукт не пошёл.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::FAIL),
      static_cast<uint8_t>(evaluatePumpVolume(50.0f, 10.0f).verdict));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::SKIP),
      static_cast<uint8_t>(evaluatePumpVolume(0.0f, 10.0f).verdict));
}

// --- Датчики --------------------------------------------------------------

void test_sensor_spread() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::PASS),
      static_cast<uint8_t>(evaluateSensorsSpread(24.0f, 24.3f).verdict));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::FAIL),
      static_cast<uint8_t>(evaluateSensorsSpread(24.0f, 25.2f).verdict));
}

void test_pressure_zero() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::PASS),
      static_cast<uint8_t>(evaluatePressureZero(0.4f).verdict));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::PASS),
      static_cast<uint8_t>(evaluatePressureZero(-0.9f).verdict));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::FAIL),
      static_cast<uint8_t>(evaluatePressureZero(3.0f).verdict));
}

// Ключевой регресс: неподключённый MPX5010DP даёт 0 В, и это не «нулевое
// давление», а отсутствие датчика. Проверка обязана это различать, иначе
// pressureControlEnabled работает по фиктивному нулю.
void test_pressure_presence_distinguishes_dead_sensor() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::PASS),
      static_cast<uint8_t>(evaluatePressurePresence(0.20f).verdict));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::PASS),
      static_cast<uint8_t>(evaluatePressurePresence(2.50f).verdict));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::FAIL),
      static_cast<uint8_t>(evaluatePressurePresence(0.0f).verdict));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestVerdict::FAIL),
      static_cast<uint8_t>(evaluatePressurePresence(0.05f).verdict));
}

void test_packing_coeff_estimate() {
  // 22.5 мм рт.ст. на 1.5 м: 22.5 * 1.5 / 1.5 = 22.5.
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.5f, suggestPackingCoeff(22.5f, 1500.0f));
  // Нулевое давление не даёт оценки.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, suggestPackingCoeff(0.0f, 1500.0f));
  // Нулевая высота не даёт оценки.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, suggestPackingCoeff(10.0f, 0.0f));
  // Оценка ограничена сверху.
  TEST_ASSERT_TRUE(suggestPackingCoeff(500.0f, 500.0f) <= 60.0f);
}

// --- Последовательность ---------------------------------------------------

void test_step_sequence_is_linear() {
  WaterTestStep step = WaterTestStep::IDLE;
  int guard = 0;
  while (step != WaterTestStep::COMPLETED && guard < 32) {
    step = nextStep(step);
    ++guard;
  }
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WaterTestStep::COMPLETED),
                          static_cast<uint8_t>(step));
  TEST_ASSERT_TRUE(guard < 32);
}

void test_injection_order_ends_with_real_trip() {
  // Инъекция отвала датчика гарантированно защёлкивает аварию и останавливает
  // режим, поэтому она обязана идти последней перед отчётом.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestStep::INJECTION_PRESSURE_BLOCK),
      static_cast<uint8_t>(nextStep(WaterTestStep::INJECTION_WATER_OFF)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestStep::INJECTION_SENSOR_DROP),
      static_cast<uint8_t>(nextStep(WaterTestStep::INJECTION_PRESSURE_BLOCK)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WaterTestStep::REPORT),
      static_cast<uint8_t>(nextStep(WaterTestStep::INJECTION_SENSOR_DROP)));
}

void test_injection_expectations() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(InjectionExpectation::LOW_WATER),
      static_cast<uint8_t>(injectionExpectation(WaterTestStep::INJECTION_WATER_OFF)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(InjectionExpectation::SENSOR_FAILURE),
      static_cast<uint8_t>(
          injectionExpectation(WaterTestStep::INJECTION_SENSOR_DROP)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(InjectionExpectation::NONE),
      static_cast<uint8_t>(injectionExpectation(WaterTestStep::HEAT_FULL)));
}

void test_step_limits_are_bounded() {
  TEST_ASSERT_TRUE(stepMaxMs(WaterTestStep::HEAT_FULL) > 0);
  TEST_ASSERT_TRUE(stepMaxMs(WaterTestStep::FSM_RUN) > stepMaxMs(WaterTestStep::HEAT_FULL));
  TEST_ASSERT_EQUAL_UINT32(0, stepMaxMs(WaterTestStep::IDLE));
  TEST_ASSERT_TRUE(isInjectionStep(WaterTestStep::INJECTION_SENSOR_DROP));
  TEST_ASSERT_FALSE(isInjectionStep(WaterTestStep::FSM_RUN));
}

void test_water_needed_only_where_heating() {
  TEST_ASSERT_FALSE(stepNeedsWater(WaterTestStep::PRECHECK));
  TEST_ASSERT_TRUE(stepNeedsWater(WaterTestStep::HEAT_FULL));
  TEST_ASSERT_TRUE(stepNeedsWater(WaterTestStep::FSM_RUN));
  // Инъекция «нет воды» сама закрывает клапан, но шаг её требует открытой.
  TEST_ASSERT_EQUAL_UINT8(60, stepHeaterPercent(WaterTestStep::INJECTION_WATER_OFF));
}

// --- Вердикт --------------------------------------------------------------

void test_overall_verdict_priority() {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WaterTestVerdict::FAIL),
                          static_cast<uint8_t>(overallVerdict(5, 1, 1)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WaterTestVerdict::WARN),
                          static_cast<uint8_t>(overallVerdict(5, 1, 0)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WaterTestVerdict::PASS),
                          static_cast<uint8_t>(overallVerdict(5, 0, 0)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WaterTestVerdict::SKIP),
                          static_cast<uint8_t>(overallVerdict(0, 0, 0)));
}

void setUp(void) {}

void tearDown(void) {}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_power_match_thresholds);
  RUN_TEST(test_heat_rate_requires_growth);
  RUN_TEST(test_zero_cross_requires_real_pulses);
  RUN_TEST(test_cooling_capacity_math);
  RUN_TEST(test_cooling_margin_verdict);
  RUN_TEST(test_pump_volume_tolerance);
  RUN_TEST(test_sensor_spread);
  RUN_TEST(test_pressure_zero);
  RUN_TEST(test_pressure_presence_distinguishes_dead_sensor);
  RUN_TEST(test_packing_coeff_estimate);
  RUN_TEST(test_step_sequence_is_linear);
  RUN_TEST(test_injection_order_ends_with_real_trip);
  RUN_TEST(test_injection_expectations);
  RUN_TEST(test_step_limits_are_bounded);
  RUN_TEST(test_water_needed_only_where_heating);
  RUN_TEST(test_overall_verdict_priority);
  return UNITY_END();
}
