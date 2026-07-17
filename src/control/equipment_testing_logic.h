#pragma once

#include <cstdint>

namespace EquipmentTestingLogic {

enum class BlockReason : uint8_t {
  NONE = 0,
  PROCESS_ACTIVE,
  SAFETY_LATCHED,
  ALARM_ACTIVE,
  SAFETY_NOT_OK,
  DEMO_MODE,
};

inline BlockReason evaluateGuard(bool processActive, bool safetyLatched,
                                 bool alarmActive, bool safetyOk,
                                 bool demoMode) {
  if (processActive) return BlockReason::PROCESS_ACTIVE;
  if (safetyLatched) return BlockReason::SAFETY_LATCHED;
  if (alarmActive) return BlockReason::ALARM_ACTIVE;
  if (!safetyOk) return BlockReason::SAFETY_NOT_OK;
  if (demoMode) return BlockReason::DEMO_MODE;
  return BlockReason::NONE;
}

inline bool isBlocked(bool processActive, bool safetyLatched,
                      bool alarmActive, bool safetyOk, bool demoMode) {
  return evaluateGuard(processActive, safetyLatched, alarmActive, safetyOk,
                       demoMode) != BlockReason::NONE;
}

}  // namespace EquipmentTestingLogic
