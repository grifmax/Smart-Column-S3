#include "editable_field_descriptors.h"

#include <math.h>

namespace EditableFields {
namespace {

constexpr Descriptor kDescriptors[] = {
    {Id::EQUIPMENT_HEATER_POWER_W, "heaterPowerW", "Мощность ТЭНа",
     "Heater power", "W", 1000.0f, 10000.0f, 100.0f, 500.0f, 0},
    {Id::EQUIPMENT_COLUMN_HEIGHT_MM, "columnHeightMm", "Высота колонны",
     "Column height", "mm", 500.0f, 3000.0f, 50.0f, 200.0f, 0},
    {Id::EQUIPMENT_CUBE_VOLUME_L, "cubeVolumeL", "Объём куба",
     "Cube volume", "L", 5.0f, 250.0f, 1.0f, 10.0f, 1},
    {Id::EQUIPMENT_PACKING_COEFFICIENT, "packingCoeff", "Коэффициент РПН",
     "Packing coefficient", "", 1.0f, 15.0f, 0.1f, 1.0f, 2},
    {Id::RECT_FEED_VOLUME_L, "feedVolumeL", "Объём СС", "Feed volume",
     "L", 1.0f, 250.0f, 0.5f, 5.0f, 1},
    {Id::RECT_FEED_ABV_PERCENT, "feedAbvPercent", "Крепость СС",
     "Feed ABV", "%", 1.0f, 96.0f, 0.5f, 2.0f, 1},
    {Id::RECT_HEADS_PERCENT, "headsPercent", "Головы", "Heads", "%", 0.0f,
     40.0f, 0.5f, 2.0f, 1},
    {Id::RECT_BODY_PERCENT, "bodyPercent", "Тело", "Body", "%", 0.0f,
     100.0f, 0.5f, 2.0f, 1},
    {Id::RECT_TAILS_PERCENT, "tailsPercent", "Хвосты", "Tails", "%", 0.0f,
     100.0f, 0.5f, 2.0f, 1},
    {Id::RECT_HEADS_SPEED_ML_H_KW_FIELD, "headsSpeedMlHKw", "Скорость голов",
     "Heads speed", "ml/h/kW", 10.0f, 2000.0f, 10.0f, 100.0f, 0},
    {Id::RECT_BODY_SPEED_ML_H_KW, "bodySpeedMlHKw", "Скорость тела",
     "Body speed", "ml/h/kW", 50.0f, 3000.0f, 50.0f, 200.0f, 0},
    {Id::RECT_STABILIZATION_MIN, "stabilizationMin", "Стабилизация",
     "Stabilization", "min", 1.0f, 180.0f, 1.0f, 10.0f, 0},
    {Id::RECT_PURGE_MIN, "purgeMin", "Продувка", "Purge time", "min",
     1.0f, 120.0f, 1.0f, 5.0f, 0},
    {Id::DIST_SPEED_ML_H, "speedMlH", "Скорость отбора", "Takeoff speed",
     "ml/h", 50.0f, 5000.0f, 50.0f, 500.0f, 0},
    {Id::DIST_HEADS_VOLUME_ML, "headsVolumeMl", "Объём голов",
     "Heads volume", "ml", 0.0f, 5000.0f, 10.0f, 100.0f, 0},
    {Id::DIST_TARGET_VOLUME_ML, "targetVolumeMl", "Целевой объём",
     "Target volume", "ml", 0.0f, 50000.0f, 100.0f, 1000.0f, 0},
    {Id::DIST_END_TEMPERATURE_C, "endTempC", "Температура окончания",
     "End temperature", "C", 80.0f, 100.0f, 0.1f, 1.0f, 1},
    {Id::DIST_POWER_PERCENT, "powerPercent", "Мощность дистилляции",
     "Distillation power", "%", 0.0f, 100.0f, 1.0f, 10.0f, 0},
    {Id::PUMP_CALIBRATION_ML_PER_REV, "mlPerRev", "Калибровка насоса",
     "Pump calibration", "ml/r", 0.001f, 5.0f, 0.001f, 0.05f, 3},
    {Id::MANUAL_HEATER_POWER_PERCENT, "manualHeaterPower", "Мощность ТЭНа",
     "Heater power", "%", 0.0f, 100.0f, 1.0f, 10.0f, 0},
    {Id::MANUAL_PUMP_SPEED_ML_H, "manualPumpSpeed", "Насос", "Pump",
     "ml/h", 0.0f, 5000.0f, 10.0f, 100.0f, 0},
    {Id::MANUAL_RECT_SPEED_ML_H, "manualRectSpeed", "Скорость отбора",
     "Takeoff speed", "ml/h", 0.0f, 5000.0f, 10.0f, 100.0f, 0},
    {Id::MANUAL_RECT_POWER_PERCENT, "manualRectPower", "Ручная мощность",
     "Manual power", "%", 0.0f, 100.0f, 1.0f, 10.0f, 0},
    {Id::MANUAL_RECT_HEADS_TARGET_ML, "manualRectHeadsTarget", "Головы: цель",
     "Heads target", "ml", 0.0f, 10000.0f, 10.0f, 100.0f, 0},
    {Id::MANUAL_RECT_BODY_TARGET_ML, "manualRectBodyTarget", "Тело: цель",
     "Body target", "ml", 0.0f, 50000.0f, 100.0f, 1000.0f, 0},
    {Id::MANUAL_RECT_TAILS_TARGET_ML, "manualRectTailsTarget", "Хвосты: цель",
     "Tails target", "ml", 0.0f, 50000.0f, 100.0f, 1000.0f, 0},
    {Id::MASH_STEP_TEMPERATURE_C, "mashStepTemperature", "Температура шага",
     "Step temperature", "C", 30.0f, 90.0f, 0.1f, 1.0f, 1},
    {Id::MASH_STEP_DURATION_MIN, "mashStepDuration", "Длительность шага",
     "Step duration", "min", 1.0f, 240.0f, 1.0f, 10.0f, 0},
    {Id::HOLD_STEP_TEMPERATURE_C, "holdStepTemperature", "Температура выдержки",
     "Hold temperature", "C", 30.0f, 95.0f, 0.1f, 1.0f, 1},
    {Id::HOLD_STEP_DURATION_MIN, "holdStepDuration", "Время выдержки",
     "Hold duration", "min", 1.0f, 720.0f, 1.0f, 15.0f, 0},
};

static_assert(sizeof(kDescriptors) / sizeof(kDescriptors[0]) ==
                  static_cast<size_t>(Id::FIELD_COUNT),
              "Every editable field must have a descriptor");

} // namespace

const Descriptor &get(Id id) {
  const size_t index = static_cast<size_t>(id);
  if (index >= static_cast<size_t>(Id::FIELD_COUNT)) {
    return kDescriptors[0];
  }
  return kDescriptors[index];
}

const char *label(Id id, bool russian) {
  const Descriptor &descriptor = get(id);
  return russian ? descriptor.labelRu : descriptor.labelEn;
}

float normalize(Id id, float value) {
  const Descriptor &descriptor = get(id);
  if (!isfinite(value)) {
    return descriptor.minValue;
  }
  if (value <= descriptor.minValue) {
    return descriptor.minValue;
  }
  if (value >= descriptor.maxValue) {
    return descriptor.maxValue;
  }
  if (descriptor.step <= 0.0f) {
    return value;
  }

  const float snapped = descriptor.minValue +
                        roundf((value - descriptor.minValue) / descriptor.step) *
                            descriptor.step;
  if (snapped <= descriptor.minValue) {
    return descriptor.minValue;
  }
  if (snapped >= descriptor.maxValue) {
    return descriptor.maxValue;
  }
  return snapped;
}

uint16_t normalizeU16(Id id, uint32_t value) {
  return static_cast<uint16_t>(normalize(id, static_cast<float>(value)) + 0.5f);
}

} // namespace EditableFields
