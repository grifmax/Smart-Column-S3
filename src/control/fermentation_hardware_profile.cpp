#include "fermentation_hardware_profile.h"

#include "config.h"

namespace FermentationHardwareProfile {

Profile get() {
  Profile profile;
  profile.zoneCount = BOARD_FERMENTATION_ZONE_COUNT;
  profile.zones[0].temperatureSensorSlot =
      BOARD_FERMENTATION_ZONE1_TEMP_SENSOR_SLOT;
  profile.zones[0].heaterOutputId = BOARD_FERMENTATION_ZONE1_HEATER_OUTPUT_ID;
  profile.zones[0].coolingOutputId =
      BOARD_FERMENTATION_ZONE1_COOLING_OUTPUT_ID;
  profile.zones[0].stirrerOutputId =
      BOARD_FERMENTATION_ZONE1_STIRRER_OUTPUT_ID;
  profile.zones[1].temperatureSensorSlot =
      BOARD_FERMENTATION_ZONE2_TEMP_SENSOR_SLOT;
  profile.zones[1].heaterOutputId = BOARD_FERMENTATION_ZONE2_HEATER_OUTPUT_ID;
  profile.zones[1].coolingOutputId =
      BOARD_FERMENTATION_ZONE2_COOLING_OUTPUT_ID;
  profile.zones[1].stirrerOutputId =
      BOARD_FERMENTATION_ZONE2_STIRRER_OUTPUT_ID;
  return profile;
}

Validation validate() {
  return FermentationHardwareProfileLogic::validate(get());
}

bool isSecondZoneAvailable() {
  return FermentationHardwareProfileLogic::isSecondZoneAvailable(get());
}

} // namespace FermentationHardwareProfile
