#pragma once

#include <cstdint>

// Pure validation for the physical topology of up to two fermentation zones.
// Output identifiers are board-profile identities, not GPIO commands. The
// control logic may use a zone only after its hardware adapter binds that
// identity to an approved driver.
namespace FermentationHardwareProfileLogic {

static constexpr int16_t kUnassigned = -1;
static constexpr uint8_t kMaxZones = 2;

struct ZoneAssignment {
  int16_t temperatureSensorSlot = kUnassigned;
  int16_t heaterOutputId = kUnassigned;
  int16_t coolingOutputId = kUnassigned;
  // A stirrer is optional, but must have its own identity when present.
  int16_t stirrerOutputId = kUnassigned;
};

struct Profile {
  uint8_t zoneCount = 1;
  ZoneAssignment zones[kMaxZones];
};

enum class Validation : uint8_t {
  OK = 0,
  ZONE_COUNT_INVALID,
  ZONE1_TEMPERATURE_UNASSIGNED,
  ZONE1_HEATER_UNASSIGNED,
  ZONE1_COOLING_UNASSIGNED,
  ZONE2_TEMPERATURE_UNASSIGNED,
  ZONE2_HEATER_UNASSIGNED,
  ZONE2_COOLING_UNASSIGNED,
  TEMPERATURE_SENSOR_REUSED,
  OUTPUT_ID_REUSED,
};

inline const char *validationToken(Validation validation) {
  switch (validation) {
  case Validation::ZONE_COUNT_INVALID:
    return "zone_count_invalid";
  case Validation::ZONE1_TEMPERATURE_UNASSIGNED:
    return "zone1_temperature_unassigned";
  case Validation::ZONE1_HEATER_UNASSIGNED:
    return "zone1_heater_unassigned";
  case Validation::ZONE1_COOLING_UNASSIGNED:
    return "zone1_cooling_unassigned";
  case Validation::ZONE2_TEMPERATURE_UNASSIGNED:
    return "zone2_temperature_unassigned";
  case Validation::ZONE2_HEATER_UNASSIGNED:
    return "zone2_heater_unassigned";
  case Validation::ZONE2_COOLING_UNASSIGNED:
    return "zone2_cooling_unassigned";
  case Validation::TEMPERATURE_SENSOR_REUSED:
    return "temperature_sensor_reused";
  case Validation::OUTPUT_ID_REUSED:
    return "output_id_reused";
  case Validation::OK:
  default:
    return "ok";
  }
}

inline bool assigned(int16_t identity) { return identity != kUnassigned; }

inline Validation validateZone(const ZoneAssignment &zone, uint8_t index) {
  if (!assigned(zone.temperatureSensorSlot)) {
    return index == 0 ? Validation::ZONE1_TEMPERATURE_UNASSIGNED
                      : Validation::ZONE2_TEMPERATURE_UNASSIGNED;
  }
  if (!assigned(zone.heaterOutputId)) {
    return index == 0 ? Validation::ZONE1_HEATER_UNASSIGNED
                      : Validation::ZONE2_HEATER_UNASSIGNED;
  }
  if (!assigned(zone.coolingOutputId)) {
    return index == 0 ? Validation::ZONE1_COOLING_UNASSIGNED
                      : Validation::ZONE2_COOLING_UNASSIGNED;
  }
  return Validation::OK;
}

inline bool outputIdsAreUnique(const Profile &profile) {
  int16_t ids[kMaxZones * 3] = {};
  uint8_t count = 0;
  for (uint8_t index = 0; index < profile.zoneCount; ++index) {
    const ZoneAssignment &zone = profile.zones[index];
    ids[count++] = zone.heaterOutputId;
    ids[count++] = zone.coolingOutputId;
    if (assigned(zone.stirrerOutputId)) ids[count++] = zone.stirrerOutputId;
  }
  for (uint8_t current = 0; current < count; ++current) {
    for (uint8_t previous = 0; previous < current; ++previous) {
      if (ids[current] == ids[previous]) return false;
    }
  }
  return true;
}

inline Validation validate(const Profile &profile) {
  if (profile.zoneCount == 0 || profile.zoneCount > kMaxZones) {
    return Validation::ZONE_COUNT_INVALID;
  }
  for (uint8_t index = 0; index < profile.zoneCount; ++index) {
    const Validation zoneValidation = validateZone(profile.zones[index], index);
    if (zoneValidation != Validation::OK) return zoneValidation;
  }
  if (profile.zoneCount == 2 &&
      profile.zones[0].temperatureSensorSlot ==
          profile.zones[1].temperatureSensorSlot) {
    return Validation::TEMPERATURE_SENSOR_REUSED;
  }
  if (!outputIdsAreUnique(profile)) return Validation::OUTPUT_ID_REUSED;
  return Validation::OK;
}

inline bool isSecondZoneAvailable(const Profile &profile) {
  return profile.zoneCount == 2 && validate(profile) == Validation::OK;
}

} // namespace FermentationHardwareProfileLogic
