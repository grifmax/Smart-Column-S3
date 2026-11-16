#pragma once

#include <cstdint>

namespace RectTakeoffLogic {

inline bool shouldUseFullReflux(bool enabled, bool periodicTakeoff,
                                bool periodicTakeoffActive) {
  return !enabled || (periodicTakeoff && !periodicTakeoffActive);
}

inline bool shouldIntegrateVolume(bool backendActive,
                                  float actualEquivalentRateMlH,
                                  uint32_t elapsedMs) {
  return backendActive && actualEquivalentRateMlH > 0.0f && elapsedMs > 0;
}

inline bool shouldRunBackend(bool phaseTakeoffEnabled, bool routeReady,
                             float equivalentRateMlH, bool periodicTakeoff,
                             bool periodicTakeoffActive) {
  return phaseTakeoffEnabled && routeReady && equivalentRateMlH > 0.0f &&
         (!periodicTakeoff || periodicTakeoffActive);
}

// Окно периодического отбора: открыт ли продуктовый клапан в этот момент и с
// какой скважностью. Функция была продублирована в rect_handler.cpp и
// manual_rect_handler.cpp почти дословно, поэтому вынесена сюда и покрыта
// тестами.
//
// refluxMode повторяет RectRefluxMode из types.h (0 = ML_H, 1 = SR_RATIO,
// 2 = AUTONOMOUS); заголовок остаётся без Arduino, поэтому принимает число, а
// не сам enum. srRatio и cycleSec/pauseSec берутся из RectParams вызывающей
// стороны; нулевой автономный цикл заменяется секундой, пауза ограничена
// циклом минус секунда.
inline bool isTakeoffWindowOpen(uint8_t refluxMode, float srRatio,
                                uint32_t autonomousCycleSec,
                                uint32_t autonomousPauseSec, uint32_t elapsedMs,
                                uint32_t controlCycleSec,
                                uint32_t *cycleMsOut = nullptr,
                                uint32_t *openMsOut = nullptr) {
  if (cycleMsOut) {
    *cycleMsOut = 0;
  }
  if (openMsOut) {
    *openMsOut = 0;
  }

  // 0 — непрерывный отбор: клапан открыт всегда.
  if (refluxMode == 0) {
    return true;
  }

  if (refluxMode == 1) {
    // Флегмовое число задаёт долю открытия в цикле.
    // Защита публичного helper от деления по модулю на нулевой цикл.
    if (controlCycleSec == 0 || controlCycleSec > UINT32_MAX / 1000UL) {
      return false;
    }
    if (srRatio <= 0.0f) {
      if (cycleMsOut) {
        *cycleMsOut = controlCycleSec * 1000UL;
      }
      return false;
    }
    const uint32_t cycleMs = controlCycleSec * 1000UL;
    uint32_t onMs = static_cast<uint32_t>(static_cast<float>(cycleMs) /
                                          (srRatio + 1.0f));
    if (onMs == 0) {
      onMs = 1; // сохраняем прежнее минимальное расчётное окно
    }
    if (cycleMsOut) {
      *cycleMsOut = cycleMs;
    }
    if (openMsOut) {
      *openMsOut = onMs;
    }
    return (elapsedMs % cycleMs) < onMs;
  }

  // Автоцикл: цикл минус пауза открыто, остальное закрыто.
  const uint32_t cycleSec = autonomousCycleSec > 0 ? autonomousCycleSec : 1;
  const uint32_t pauseSec =
      autonomousPauseSec < cycleSec ? autonomousPauseSec : (cycleSec - 1);
  const uint32_t cycleMs = cycleSec * 1000UL;
  const uint32_t onMs = (cycleSec - pauseSec) * 1000UL;
  if (cycleMsOut) {
    *cycleMsOut = cycleMs;
  }
  if (openMsOut) {
    *openMsOut = onMs;
  }
  return (elapsedMs % cycleMs) < onMs;
}

} // namespace RectTakeoffLogic
