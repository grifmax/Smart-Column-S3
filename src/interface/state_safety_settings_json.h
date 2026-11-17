#pragma once

#include <ArduinoJson.h>

namespace StateSafetySettingsJson {

// Один набор полей для REST и обоих кадров WebSocket; облако его не содержит.
template <typename Safety>
inline void fill(JsonObject obj, const Safety& safety) {
  obj["pressureMaxMmHg"] = safety.pressureMaxMmHg;
  obj["tsaMaxC"] = safety.tsaMaxC;
  obj["waterOutMaxC"] = safety.waterOutMaxC;
  obj["waterOutRiseRateCMin"] = safety.waterOutRiseRateCMin;
  obj["pressureRiseRateMmHgMin"] = safety.pressureRiseRateMmHgMin;
}

template <typename Safety>
inline void fillAliases(JsonObject obj, const Safety& safety) {
  obj["safety_pressure_max_mmhg"] = safety.pressureMaxMmHg;
  obj["safety_tsa_max_c"] = safety.tsaMaxC;
  obj["safety_water_out_max_c"] = safety.waterOutMaxC;
  obj["safety_water_out_rise_rate_c_min"] = safety.waterOutRiseRateCMin;
  obj["safety_pressure_rise_rate_mmhg_min"] = safety.pressureRiseRateMmHgMin;
}

} // namespace StateSafetySettingsJson
