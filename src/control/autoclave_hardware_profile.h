#pragma once

#include "autoclave_hardware_profile_logic.h"

// Compile-time board profile adapter. BOARD_AUTOCLAVE_* values are explicit
// declarations made after the physical wiring and safety review; they do not
// infer capability from PIN_PUMP_* or PIN_VALVE_*.
namespace AutoclaveHardwareProfile {

using Profile = AutoclaveHardwareProfileLogic::Profile;
using Validation = AutoclaveHardwareProfileLogic::Validation;

Profile get();
Validation validate();
bool isSteamAvailable();
bool isWaterAvailable();

} // namespace AutoclaveHardwareProfile
