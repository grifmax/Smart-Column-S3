#pragma once

#include <cstdint>

// Pure approval gate for the autoclave hardware contract. It deliberately
// models physical assignments rather than deriving them from unrelated board
// pins: an existing takeoff pump or cooling valve must never become an
// autoclave output by implication.
namespace AutoclaveHardwareProfileLogic {

struct Profile {
  bool approved = false;
  bool steamEnabled = false;
  bool waterEnabled = false;
  bool heaterAssigned = false;
  bool coolingAssigned = false;
  bool circulationPumpAssigned = false;
  bool drainAssigned = false;
  bool cubeTemperatureAssigned = false;
  bool pressureSensorAssigned = false;
  bool mechanicalSafetyReviewed = false;
  float heatPowerLimitW = 0.0f;
  float pressureMaxMmHg = 0.0f;
  float pressureRiseRateMaxMmHgMin = 0.0f;
};

enum class Validation : uint8_t {
  OK = 0,
  NOT_APPROVED,
  NO_RELEASE_MODE,
  STEAM_HEATER_UNASSIGNED,
  STEAM_TEMPERATURE_UNASSIGNED,
  STEAM_PRESSURE_UNASSIGNED,
  MECHANICAL_SAFETY_UNREVIEWED,
  HEAT_LIMIT_INVALID,
  PRESSURE_LIMIT_INVALID,
  WATER_CIRCULATION_UNASSIGNED,
  WATER_COOLING_UNASSIGNED,
};

inline const char *validationToken(Validation validation) {
  switch (validation) {
  case Validation::NOT_APPROVED:
    return "not_approved";
  case Validation::NO_RELEASE_MODE:
    return "no_release_mode";
  case Validation::STEAM_HEATER_UNASSIGNED:
    return "steam_heater_unassigned";
  case Validation::STEAM_TEMPERATURE_UNASSIGNED:
    return "steam_temperature_unassigned";
  case Validation::STEAM_PRESSURE_UNASSIGNED:
    return "steam_pressure_unassigned";
  case Validation::MECHANICAL_SAFETY_UNREVIEWED:
    return "mechanical_safety_unreviewed";
  case Validation::HEAT_LIMIT_INVALID:
    return "heat_limit_invalid";
  case Validation::PRESSURE_LIMIT_INVALID:
    return "pressure_limit_invalid";
  case Validation::WATER_CIRCULATION_UNASSIGNED:
    return "water_circulation_unassigned";
  case Validation::WATER_COOLING_UNASSIGNED:
    return "water_cooling_unassigned";
  case Validation::OK:
  default:
    return "ok";
  }
}

inline Validation validate(const Profile &profile) {
  if (!profile.approved) return Validation::NOT_APPROVED;
  if (!profile.steamEnabled && !profile.waterEnabled) {
    return Validation::NO_RELEASE_MODE;
  }
  // Both release variants heat a closed vessel, so they require the common
  // review and limits before any individual output can be considered usable.
  if (!profile.heaterAssigned) return Validation::STEAM_HEATER_UNASSIGNED;
  if (!profile.cubeTemperatureAssigned) {
    return Validation::STEAM_TEMPERATURE_UNASSIGNED;
  }
  if (!profile.pressureSensorAssigned) {
    return Validation::STEAM_PRESSURE_UNASSIGNED;
  }
  if (!profile.mechanicalSafetyReviewed) {
    return Validation::MECHANICAL_SAFETY_UNREVIEWED;
  }
  if (profile.heatPowerLimitW <= 0.0f) return Validation::HEAT_LIMIT_INVALID;
  if (profile.pressureMaxMmHg <= 0.0f ||
      profile.pressureRiseRateMaxMmHgMin <= 0.0f) {
    return Validation::PRESSURE_LIMIT_INVALID;
  }
  if (profile.waterEnabled && !profile.circulationPumpAssigned) {
    return Validation::WATER_CIRCULATION_UNASSIGNED;
  }
  if (profile.waterEnabled && !profile.coolingAssigned) {
    return Validation::WATER_COOLING_UNASSIGNED;
  }
  return Validation::OK;
}

inline bool mayOfferSteam(const Profile &profile) {
  return profile.steamEnabled && validate(profile) == Validation::OK;
}

inline bool mayOfferWater(const Profile &profile) {
  return profile.waterEnabled && profile.circulationPumpAssigned &&
         validate(profile) == Validation::OK;
}

} // namespace AutoclaveHardwareProfileLogic
