#pragma once

#include <ArduinoJson.h>

namespace StateEquipmentJson {

// Общая последовательная часть: потребитель создаёт объект и записывает
// heaterPowerW раньше; дополнительные поля локального UI остаются на месте.
template <typename DutyReader, typename Equipment>
inline void fillCommon(JsonObject obj, const Equipment& e) {
  obj["cubeVolumeL"] = e.cubeVolumeL;
  obj["minHeaterSubmergeL"] = e.minHeaterSubmergeL;
  obj["waterAutoStartCubeTempC"] = e.waterAutoStartCubeTempC;
  obj["boosterHeaterEnabled"] = e.boosterHeaterEnabled;
  obj["boosterHeaterPowerW"] = e.boosterHeaterPowerW;
  obj["boosterHeaterStopCubeTempC"] = e.boosterHeaterStopCubeTempC;
  obj["coolingPwmEnabled"] = e.coolingPwmEnabled;
  obj["coolingPwmHoldPercent"] = e.coolingPwmHoldPercent;
  obj["coolingPwmPullMs"] = e.coolingPwmPullMs;
  obj["coolingPwmCurrentDuty"] = DutyReader::getStartStop();
}

} // namespace StateEquipmentJson
