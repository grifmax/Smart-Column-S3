#pragma once

#include <stdint.h>

/**
 * TFT: блокировка изменения уставок, пока исполнительный выход активен.
 *
 * Правило отдельно от UI, потому что оно должно срабатывать в трёх местах:
 * при отрисовке строки, при шаге «+/−» и в момент «СОХРАНИТЬ» уже открытого
 * редактора (выход могли включить из веба, пока редактор висел открытым).
 * Остановка (СТОП) этим правилом не блокируется.
 */
namespace TftSetpointGuard {

enum class SetpointClass : uint8_t {
  None = 0,        // подпись/ссылка — не значение
  Actuator,        // влияет на работающий ТЭН, насос, клапан, мешалку
  ProcessConstant, // константа установки, не влияет на текущий выход
  RuntimeAllowed,  // штатная правка на ходу (мощность/отбор процесса)
};

struct OutputActivity {
  bool serviceTest = false;
  bool heater = false;
  bool pump = false;
  bool stirrer = false;
  bool valve = false;
  bool dosing = false;
  bool calibration = false;
  bool processRunning = false;
};

inline bool anyLiveOutput(const OutputActivity &activity) {
  return activity.serviceTest || activity.heater || activity.pump ||
         activity.stirrer || activity.valve || activity.dosing ||
         activity.calibration || activity.processRunning;
}

/** Блокируются только уставки исполнителей; константы и runtime-правка — нет. */
inline bool isLocked(SetpointClass kind, const OutputActivity &activity) {
  return kind == SetpointClass::Actuator && anyLiveOutput(activity);
}

} // namespace TftSetpointGuard
