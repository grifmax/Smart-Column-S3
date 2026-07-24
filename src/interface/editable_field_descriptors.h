#pragma once

#include <stddef.h>
#include <stdint.h>

// Canonical metadata for every scalar value that can be edited on the TFT.
// The display and HTTP handlers must use this layer instead of carrying their
// own min/max/step copies. Runtime-only fields are included here too, so a
// future API can expose them without inventing a second set of limits.
namespace EditableFields {

enum class Id : uint8_t {
  EQUIPMENT_HEATER_POWER_W = 0,
  EQUIPMENT_COLUMN_HEIGHT_MM,
  EQUIPMENT_CUBE_VOLUME_L,
  EQUIPMENT_PACKING_COEFFICIENT,
  RECT_FEED_VOLUME_L,
  RECT_FEED_ABV_PERCENT,
  RECT_HEADS_PERCENT,
  RECT_BODY_PERCENT,
  RECT_TAILS_PERCENT,
  RECT_HEADS_SPEED_ML_H_KW_FIELD,
  RECT_BODY_SPEED_ML_H_KW,
  RECT_STABILIZATION_MIN,
  RECT_PURGE_MIN,
  DIST_SPEED_ML_H,
  DIST_HEADS_VOLUME_ML,
  DIST_TARGET_VOLUME_ML,
  DIST_END_TEMPERATURE_C,
  DIST_POWER_PERCENT,
  PUMP_CALIBRATION_ML_PER_REV,
  SENSOR_TEMPERATURE_OFFSET_C,
  SENSOR_PRESSURE_ZERO_OFFSET_MMHG,
  SENSOR_HYDROMETER_DENSITY_OFFSET,
  SENSOR_HYDROMETER_REFERENCE_DENSITY,
  SENSOR_BODY_LEVEL_THRESHOLD_V,
  SENSOR_LEAK_THRESHOLD_V,
  PUMP_CALIBRATION_MEASURED_VOLUME_ML,
  MANUAL_HEATER_POWER_PERCENT,
  MANUAL_PUMP_SPEED_ML_H,
  MANUAL_RECT_SPEED_ML_H,
  MANUAL_RECT_POWER_PERCENT,
  MANUAL_RECT_HEADS_TARGET_ML,
  MANUAL_RECT_BODY_TARGET_ML,
  MANUAL_RECT_TAILS_TARGET_ML,
  MASH_STEP_TEMPERATURE_C,
  MASH_STEP_DURATION_MIN,
  HOLD_STEP_TEMPERATURE_C,
  HOLD_STEP_DURATION_MIN,
  DIST_TAILS_VOLUME_ML,
  FRACTION_PROGRAM_HEATING_TARGET_C,
  NBK_POWER_W,
  NBK_PUMP_SPEED_ML_H,
  NBK_BOTTOM_TEMP_C,
  NBK_TOP_TARGET_TEMP_C,
  NBK_TARGET_VOLUME_ML,
  FERM_TARGET_TEMP_C,
  FERM_HYSTERESIS_C,
  FERM_COOLING_MIN_ON_SEC,
  FERM_COOLING_MIN_OFF_SEC,
  FERM_DURATION_HOURS,
  STIRRER_DEFAULT_SPEED_PERCENT,
  COOLING_PWM_MIN_DUTY,
  COOLING_PWM_MAX_DUTY,
  COOLING_PWM_STARTUP_DUTY,
  FIELD_COUNT
};

struct Descriptor {
  Id id;
  const char *key;
  const char *labelRu;
  const char *labelEn;
  const char *unit;
  float minValue;
  float maxValue;
  float step;
  float fastStep;
  uint8_t decimals;
};

const Descriptor &get(Id id);
const char *label(Id id, bool russian);

// Clamps non-finite input and snaps valid input to the descriptor's increment.
// This lets REST and TFT produce the same persistent representation.
float normalize(Id id, float value);
uint16_t normalizeU16(Id id, uint32_t value);

} // namespace EditableFields
