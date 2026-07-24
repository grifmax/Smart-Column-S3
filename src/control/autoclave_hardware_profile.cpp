#include "autoclave_hardware_profile.h"

#include "config.h"

namespace AutoclaveHardwareProfile {

Profile get() {
  Profile profile;
  profile.approved = BOARD_AUTOCLAVE_PROFILE_APPROVED != 0;
  profile.steamEnabled = BOARD_AUTOCLAVE_STEAM_ENABLED != 0;
  profile.waterEnabled = BOARD_AUTOCLAVE_WATER_ENABLED != 0;
  profile.heaterAssigned = BOARD_AUTOCLAVE_HEATER_ASSIGNED != 0;
  profile.coolingAssigned = BOARD_AUTOCLAVE_COOLING_ASSIGNED != 0;
  profile.circulationPumpAssigned =
      BOARD_AUTOCLAVE_CIRCULATION_PUMP_ASSIGNED != 0;
  profile.drainAssigned = BOARD_AUTOCLAVE_DRAIN_ASSIGNED != 0;
  profile.cubeTemperatureAssigned =
      BOARD_AUTOCLAVE_CUBE_TEMPERATURE_ASSIGNED != 0;
  profile.pressureSensorAssigned =
      BOARD_AUTOCLAVE_PRESSURE_SENSOR_ASSIGNED != 0;
  profile.mechanicalSafetyReviewed =
      BOARD_AUTOCLAVE_MECHANICAL_SAFETY_REVIEWED != 0;
  profile.heatPowerLimitW = BOARD_AUTOCLAVE_HEAT_POWER_LIMIT_W;
  profile.pressureMaxMmHg = BOARD_AUTOCLAVE_PRESSURE_MAX_MMHG;
  profile.pressureRiseRateMaxMmHgMin =
      BOARD_AUTOCLAVE_PRESSURE_RISE_RATE_MAX_MMHG_MIN;
  return profile;
}

Validation validate() {
  return AutoclaveHardwareProfileLogic::validate(get());
}

bool isSteamAvailable() {
  return AutoclaveHardwareProfileLogic::mayOfferSteam(get());
}

bool isWaterAvailable() {
  return AutoclaveHardwareProfileLogic::mayOfferWater(get());
}

} // namespace AutoclaveHardwareProfile
