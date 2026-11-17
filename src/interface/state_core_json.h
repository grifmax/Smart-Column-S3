#pragma once

#include <ArduinoJson.h>

namespace StateCoreJson {

// Только поля с одинаковой семантикой во всех транспортных ответах.
template <typename State>
inline void fill(JsonObject obj, const State& state) {
  obj["paused"] = state.paused;
  obj["safetyOk"] = state.safetyOk;
  obj["uptime"] = state.uptime;
}

} // namespace StateCoreJson
