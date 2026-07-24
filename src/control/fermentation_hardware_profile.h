#pragma once

#include "fermentation_hardware_profile_logic.h"

// Compile-time board profile adapter. It only describes assigned topology;
// it cannot activate a second zone or dispatch a hardware command by itself.
namespace FermentationHardwareProfile {

using Profile = FermentationHardwareProfileLogic::Profile;
using Validation = FermentationHardwareProfileLogic::Validation;

Profile get();
Validation validate();
bool isSecondZoneAvailable();

} // namespace FermentationHardwareProfile
