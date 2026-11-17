#pragma once

#include <ArduinoJson.h>

namespace StateValvesJson {

// Порядок чтения сохранён; tails и локальная маска добавляются вызывающим.
template <typename Reader>
inline void fillCommon(JsonObject obj) {
  obj["water"] = Reader::getWater();
  obj["heads"] = Reader::getHeads();
  obj["body"] = Reader::getBody();
  obj["bodyAvailable"] = Reader::hasBodyValve();
  obj["tailsAvailable"] = Reader::hasTailsValve();
  obj["uno"] = Reader::getUno();
  obj["startStopDuty"] = Reader::getStartStop();
}

} // namespace StateValvesJson
