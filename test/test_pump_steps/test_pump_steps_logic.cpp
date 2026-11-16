// Тесты санитайзера пар «шагов на оборот × микрошаги».
//
// Зачем это отдельно от остальных тестов: в эксплуатации уже был случай,
// когда в поле «шагов на оборот» лежало произведение импульсов (6400), а
// микрошаги остались 32, и насос подавал вдвое больше заданного. Санитайзер
// существует ровно чтобы такой мусор из NVS не доходил до расчёта подачи,
// поэтому проверяем не только счастливый путь, но и все варианты мусора.

#include <unity.h>

#include <../../src/control/pump_steps_logic.h>

using namespace PumpSteps;

void setUp() {}
void tearDown() {}

// --- Валидные значения ----------------------------------------------------

void test_valid_pair_multiplies() {
  TEST_ASSERT_EQUAL_UINT32(3200, pulsesPerRevolution(200, 16, 200, 16));
  TEST_ASSERT_EQUAL_UINT32(6400, pulsesPerRevolution(200, 32, 200, 16));
  TEST_ASSERT_EQUAL_UINT32(1600, pulsesPerRevolution(400, 4, 200, 16));
}

void test_microsteps_must_be_power_of_two() {
  TEST_ASSERT_TRUE(isMicrostepsValue(1));
  TEST_ASSERT_TRUE(isMicrostepsValue(2));
  TEST_ASSERT_TRUE(isMicrostepsValue(16));
  TEST_ASSERT_TRUE(isMicrostepsValue(64));

  // Драйверы с перемычками дают только степени двойки; 20 и 48 — признак
  // испорченного значения, а не реальной конфигурации.
  TEST_ASSERT_FALSE(isMicrostepsValue(0));
  TEST_ASSERT_FALSE(isMicrostepsValue(3));
  TEST_ASSERT_FALSE(isMicrostepsValue(20));
  TEST_ASSERT_FALSE(isMicrostepsValue(48));
  TEST_ASSERT_FALSE(isMicrostepsValue(128));
}

void test_steps_range() {
  TEST_ASSERT_TRUE(isStepsValue(1));
  TEST_ASSERT_TRUE(isStepsValue(200));
  TEST_ASSERT_TRUE(isStepsValue(2000));

  TEST_ASSERT_FALSE(isStepsValue(0));
  TEST_ASSERT_FALSE(isStepsValue(2001));
  // Именно этот случай и случился в поле: произведение импульсов записали
  // в поле шагов.
  TEST_ASSERT_FALSE(isStepsValue(6400));
}

// --- Мусор в NVS ----------------------------------------------------------

void test_pulses_written_into_steps_field_is_rejected() {
  // 6400 шагов физически невозможно для этого мотора: максимальное
  // произведение 2000×64, а 6400 не проходит проверку диапазона.
  const uint32_t pulses = pulsesPerRevolution(6400, 32, 200, 16);
  TEST_ASSERT_EQUAL_UINT32(3200, pulses);
}

void test_absurd_microsteps_fall_back_to_default() {
  TEST_ASSERT_EQUAL_UINT32(3200, pulsesPerRevolution(200, 128, 200, 16));
  TEST_ASSERT_EQUAL_UINT32(3200, pulsesPerRevolution(200, 0, 200, 16));
}

void test_zero_steps_falls_back() {
  TEST_ASSERT_EQUAL_UINT32(3200, pulsesPerRevolution(0, 16, 200, 16));
}

void test_broken_default_microsteps_falls_back_to_sixteen() {
  // Если и значение по умолчанию битое, санитайзер обязан вернуть рабочую
  // пару, а не унаследовать мусор.
  TEST_ASSERT_EQUAL_UINT32(3200, pulsesPerRevolution(0, 0, 200, 0));
  TEST_ASSERT_EQUAL_UINT32(6400, pulsesPerRevolution(0, 0, 200, 32));
}

// --- Потолок произведения -------------------------------------------------

void test_product_ceiling() {
  // Ровно на границе — допустимо.
  TEST_ASSERT_EQUAL_UINT32(51200, pulsesPerRevolution(1600, 32, 200, 16));
  // Выше границы — откат к паре по умолчанию.
  TEST_ASSERT_EQUAL_UINT32(3600, pulsesPerRevolution(1800, 32, 200, 18));
  TEST_ASSERT_EQUAL_UINT32(3200, pulsesPerRevolution(2000, 64, 200, 16));
}

void test_defaults_returned_as_product() {
  // Проверяем, что при откате возвращается именно произведение
  // переданных значений по умолчанию, а не захардкоженные 3200.
  TEST_ASSERT_EQUAL_UINT32(6400, pulsesPerRevolution(0, 0, 200, 32));
  TEST_ASSERT_EQUAL_UINT32(1600, pulsesPerRevolution(0, 0, 400, 4));
}

// --- Реальные конфигурации проекта ---------------------------------------

void test_project_configurations() {
  // 200 шагов × 16 микрошагов — рабочая конфигурация TMC2209 по перемычкам.
  TEST_ASSERT_EQUAL_UINT32(3200, pulsesPerRevolution(200, 16, 200, 16));
  // 200 × 32 — то, что ошибочно сохранялось раньше.
  TEST_ASSERT_EQUAL_UINT32(6400, pulsesPerRevolution(200, 32, 200, 16));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_pair_multiplies);
  RUN_TEST(test_microsteps_must_be_power_of_two);
  RUN_TEST(test_steps_range);
  RUN_TEST(test_pulses_written_into_steps_field_is_rejected);
  RUN_TEST(test_absurd_microsteps_fall_back_to_default);
  RUN_TEST(test_zero_steps_falls_back);
  RUN_TEST(test_broken_default_microsteps_falls_back_to_sixteen);
  RUN_TEST(test_product_ceiling);
  RUN_TEST(test_defaults_returned_as_product);
  RUN_TEST(test_project_configurations);
  return UNITY_END();
}