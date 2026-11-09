#pragma once

#include <cmath>
#include <stdint.h>

namespace PzemSamplePolicy {

// Физические границы снимка PZEM-004T. Это только проверка диапазона:
// межопросный дельта-фильтр здесь сознательно отсутствует, иначе реальный
// скачок нагрузки (0 -> 9 A, 0 -> 2000 Вт) отбрасывается как "выброс" и
// показания в UI застревают на старом значении.
static constexpr float kVoltageMax = 300.0f;
static constexpr float kPowerMax = 10000.0f;

inline bool isPlausibleCurrent(float currentA, float currentMaxA) {
    return std::isfinite(currentA) && currentA >= 0.0f &&
           currentA <= currentMaxA;
}

inline bool isPlausibleSnapshot(float voltageV, float currentA, float powerW,
                                float currentMaxA) {
    return std::isfinite(voltageV) && voltageV >= 0.0f &&
           voltageV <= kVoltageMax && isPlausibleCurrent(currentA, currentMaxA) &&
           std::isfinite(powerW) && powerW >= 0.0f && powerW <= kPowerMax;
}

}  // namespace PzemSamplePolicy
