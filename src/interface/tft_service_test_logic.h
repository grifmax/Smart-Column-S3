#pragma once

#include <cstdint>

// Platform-independent availability contract for the TFT equipment test page.
// The display fills this from the actual board profile and initialized drivers;
// unavailable hardware is deliberately not presented as an actionable control.
namespace TftServiceTestLogic {

enum class Device : uint8_t {
  Heater = 0,
  Pump,
  Stirrer,
  WaterValve,
  HeadsValve,
  BodyValve,
  TailsValve,
  UnoValve,
  CoolingPwm,
  Fractionator,
  Sensors,
};

struct Availability {
  bool heater = false;
  bool pump = false;
  bool stirrer = false;
  bool waterValve = false;
  bool headsValve = false;
  bool bodyValve = false;
  bool tailsValve = false;
  bool unoValve = false;
  bool coolingPwm = false;
  bool fractionator = false;
  bool sensors = false;
};

inline bool isAvailable(Device device, const Availability &availability) {
  switch (device) {
  case Device::Heater:
    return availability.heater;
  case Device::Pump:
    return availability.pump;
  case Device::Stirrer:
    return availability.stirrer;
  case Device::WaterValve:
    return availability.waterValve;
  case Device::HeadsValve:
    return availability.headsValve;
  case Device::BodyValve:
    return availability.bodyValve;
  case Device::TailsValve:
    return availability.tailsValve;
  case Device::UnoValve:
    return availability.unoValve;
  case Device::CoolingPwm:
    return availability.coolingPwm;
  case Device::Fractionator:
    return availability.fractionator;
  case Device::Sensors:
    return availability.sensors;
  }
  return false;
}

} // namespace TftServiceTestLogic
