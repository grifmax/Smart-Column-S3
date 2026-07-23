#include <math.h>

#include <unity.h>

#include <../../src/interface/editable_field_descriptors.h>
#include <../../src/interface/editable_field_descriptors.cpp>

void test_editable_field_descriptor_exposes_canonical_tft_metadata() {
  const EditableFields::Descriptor &descriptor = EditableFields::get(
      EditableFields::Id::RECT_STABILIZATION_MIN);

  TEST_ASSERT_EQUAL_STRING("stabilizationMin", descriptor.key);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, descriptor.minValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 180.0f, descriptor.maxValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, descriptor.step);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, descriptor.fastStep);
  TEST_ASSERT_EQUAL_UINT8(0, descriptor.decimals);
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
}
