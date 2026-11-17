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

} // namespace StateSafetySettingsJson
