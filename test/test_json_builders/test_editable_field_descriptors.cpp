#include <math.h>

#include <unity.h>

#include <../../src/control/cooling_pwm_logic.h>
#include <../../src/interface/editable_field_descriptors.h>
#include <../../src/interface/editable_field_descriptors.cpp>
#include <../../src/ui/hmi_page_model.h>

void test_editable_field_descriptor_exposes_canonical_tft_metadata() {
  const EditableFields::Descriptor &descriptor = EditableFields::get(
      EditableFields::Id::RECT_STABILIZATION_MIN);

  TEST_ASSERT_EQUAL_STRING("stabilizationMin", descriptor.key);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, descriptor.minValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 180.0f, descriptor.maxValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, descriptor.step);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, descriptor.fastStep);
  TEST_ASSERT_EQUAL_UINT8(0, descriptor.decimals);

  const EditableFields::Descriptor &pressure = EditableFields::get(
      EditableFields::Id::SENSOR_PRESSURE_ZERO_OFFSET_MMHG);
  TEST_ASSERT_EQUAL_STRING("pressureZeroOffsetMmHg", pressure.key);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -75.0f, pressure.minValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 75.0f, pressure.maxValue);
}

void test_editable_field_normalization_clamps_and_snaps_to_step() {
  using EditableFields::Id;

  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 37.5f,
      EditableFields::normalize(Id::RECT_FEED_VOLUME_L, 37.26f));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 1.0f,
      EditableFields::normalize(Id::RECT_FEED_VOLUME_L, -1.0f));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 250.0f,
      EditableFields::normalize(Id::RECT_FEED_VOLUME_L, 999.0f));
  TEST_ASSERT_FLOAT_WITHIN(
      0.0001f, 0.001f,
      EditableFields::normalize(Id::PUMP_CALIBRATION_ML_PER_REV, NAN));
  TEST_ASSERT_EQUAL_UINT16(
      180, EditableFields::normalizeU16(Id::RECT_STABILIZATION_MIN, 999));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 5500.0f,
      EditableFields::normalize(Id::NBK_POWER_W, 9999.0f));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 0.1f,
      EditableFields::normalize(Id::FERM_HYSTERESIS_C, 0.01f));
  TEST_ASSERT_EQUAL_UINT16(
      100, EditableFields::normalizeU16(Id::COOLING_PWM_HOLD_PERCENT, 999));
  TEST_ASSERT_EQUAL_UINT16(
      10000, EditableFields::normalizeU16(Id::COOLING_PWM_PULL_MS, 20000));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 50000.0f,
      EditableFields::normalize(Id::DIST_TAILS_VOLUME_ML, 999999.0f));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 5000.0f,
      EditableFields::normalize(Id::MANUAL_PUMP_SPEED_ML_H, 99999.0f));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, -75.0f,
      EditableFields::normalize(Id::SENSOR_PRESSURE_ZERO_OFFSET_MMHG, -99.0f));
}

void test_equipment_hmi_fields_expose_canonical_limits_and_steps() {
  using EditableFields::Id;

  const EditableFields::Descriptor &submerge =
      EditableFields::get(Id::EQUIPMENT_MIN_HEATER_SUBMERGE_L);
  TEST_ASSERT_EQUAL_STRING("minHeaterSubmergeL", submerge.key);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, submerge.minValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, submerge.maxValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, submerge.step);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, submerge.fastStep);

  const EditableFields::Descriptor &booster =
      EditableFields::get(Id::EQUIPMENT_BOOSTER_HEATER_POWER_W);
  TEST_ASSERT_EQUAL_STRING("boosterHeaterPowerW", booster.key);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1000.0f, booster.minValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10000.0f, booster.maxValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, booster.step);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1000.0f, booster.fastStep);
  TEST_ASSERT_EQUAL_UINT16(
      3100, EditableFields::normalizeU16(Id::EQUIPMENT_BOOSTER_HEATER_POWER_W,
                                         3120));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 0.6f,
      EditableFields::normalize(Id::EQUIPMENT_MIN_HEATER_SUBMERGE_L, 0.57f));
}

void test_safety_hmi_fields_match_api_limits_and_steps() {
  using EditableFields::Id;

  const EditableFields::Descriptor &pressure =
      EditableFields::get(Id::SAFETY_PRESSURE_MAX_MMHG);
  TEST_ASSERT_EQUAL_STRING("pressureMaxMmHg", pressure.key);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, pressure.minValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 200.0f, pressure.maxValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, pressure.step);

  const EditableFields::Descriptor &waterRise =
      EditableFields::get(Id::SAFETY_WATER_OUT_RISE_RATE_C_MIN);
  TEST_ASSERT_EQUAL_STRING("waterOutRiseRateCMin", waterRise.key);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, waterRise.minValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 60.0f, waterRise.maxValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, waterRise.step);
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 8.5f,
      EditableFields::normalize(Id::SAFETY_WATER_OUT_RISE_RATE_C_MIN,
                                8.26f));
}

void test_cooling_pwm_percent_conversion_handles_limits() {
  TEST_ASSERT_EQUAL_UINT8(0, CoolingPwmLogic::percentToDuty(0));
  TEST_ASSERT_EQUAL_UINT8(13, CoolingPwmLogic::percentToDuty(5));
  TEST_ASSERT_EQUAL_UINT8(255, CoolingPwmLogic::percentToDuty(100));
  TEST_ASSERT_EQUAL_UINT8(255, CoolingPwmLogic::percentToDuty(120));
  TEST_ASSERT_EQUAL_UINT8(0, CoolingPwmLogic::dutyToPercent(0));
  TEST_ASSERT_EQUAL_UINT8(100, CoolingPwmLogic::dutyToPercent(255));
  TEST_ASSERT_EQUAL_UINT8(38, CoolingPwmLogic::dutyToPercent(96));
  TEST_ASSERT_EQUAL_UINT8(5, CoolingPwmLogic::clampHoldPercent(0));
  TEST_ASSERT_EQUAL_UINT8(100, CoolingPwmLogic::clampHoldPercent(120));
  TEST_ASSERT_EQUAL_UINT16(100, CoolingPwmLogic::clampPullMs(0));
  TEST_ASSERT_EQUAL_UINT16(10000, CoolingPwmLogic::clampPullMs(20000));
}

void test_hmi_page_stack_and_row_hit_testing() {
  HmiPage::PageStack stack;
  TEST_ASSERT_TRUE(stack.push(1));
  TEST_ASSERT_TRUE(stack.push(2));
  TEST_ASSERT_EQUAL_UINT8(2, stack.depth);
  TEST_ASSERT_TRUE(stack.replace(3));
  uint8_t page = 0;
  TEST_ASSERT_TRUE(stack.pop(page));
  TEST_ASSERT_EQUAL_UINT8(3, page);
  TEST_ASSERT_TRUE(stack.pop(page));
  TEST_ASSERT_EQUAL_UINT8(1, page);
  TEST_ASSERT_FALSE(stack.pop(page));

  TEST_ASSERT_EQUAL_UINT16(4, HmiPage::rowIndexAt(152, 32, 60, 2, 8, 4));
  TEST_ASSERT_EQUAL_UINT16(8, HmiPage::rowIndexAt(272, 32, 60, 2, 8, 4));
  TEST_ASSERT_EQUAL_UINT16(8, HmiPage::rowIndexAt(10, 32, 60, 0, 8, 4));
}
