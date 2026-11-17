#pragma once

#include "state_valves_json.h"
#include "../drivers/valves.h"

namespace StateValvesJson {
struct DriverReader {
  static bool getWater() { return Valves::getWater(); }
  static bool getHeads() { return Valves::getHeads(); }
  static bool getBody() { return Valves::getBody(); }
  static bool hasBodyValve() { return Valves::hasBodyValve(); }
  static bool hasTailsValve() { return Valves::hasTailsValve(); }
  static bool getUno() { return Valves::getUno(); }
  static uint8_t getStartStop() { return Valves::getStartStop(); }
};
}
