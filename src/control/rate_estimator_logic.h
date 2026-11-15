#pragma once

#include <cmath>
#include <cstdint>

// Оценка скорости изменения величины по окну отсчётов методом наименьших
// квадратов. Нужна там, где первая разность по соседним точкам тонет в шуме:
// DS18B20 на 12 битах даёт шаг 0.0625 C, и при периоде опроса 1 с это
// 3.75 C/мин «скорости» — при пороге 8 C/мин получаются ложные аварии.
namespace RateEstimatorLogic {

constexpr uint8_t kCapacity = 48;

struct Sample {
  uint32_t ms = 0;
  float value = 0.0f;
};

struct Estimator {
  Sample samples[kCapacity];
  uint8_t count = 0;
  uint8_t head = 0;  // индекс следующей записи
};

inline void reset(Estimator &estimator) { estimator = Estimator{}; }

inline void push(Estimator &estimator, uint32_t ms, float value) {
  estimator.samples[estimator.head].ms = ms;
  estimator.samples[estimator.head].value = value;
  estimator.head = static_cast<uint8_t>((estimator.head + 1) % kCapacity);
  if (estimator.count < kCapacity) {
    ++estimator.count;
  }
}

// Наклон в единицах величины за минуту по отсчётам не старше windowMs.
// Возвращает false, если точек меньше minPoints или база вырождена.
inline bool slopePerMin(const Estimator &estimator, uint32_t nowMs,
                        uint32_t windowMs, uint8_t minPoints, float &outSlope) {
  float sumT = 0.0f;
  float sumC = 0.0f;
  float sumTT = 0.0f;
  float sumTC = 0.0f;
  uint8_t n = 0;

  for (uint8_t i = 0; i < estimator.count; ++i) {
    const Sample &sample = estimator.samples[i];
    const uint32_t ageMs = nowMs - sample.ms;
    if (ageMs > windowMs) continue;
    // Время в минутах назад от текущего момента. Сдвиг по оси времени не
    // влияет на наклон, поэтому база не нужна и порядок обхода не важен.
    const float t = -static_cast<float>(ageMs) / 60000.0f;
    sumT += t;
    sumC += sample.value;
    sumTT += t * t;
    sumTC += t * sample.value;
    ++n;
  }

  if (n < minPoints) return false;
  const float denominator = static_cast<float>(n) * sumTT - sumT * sumT;
  if (fabsf(denominator) < 1e-9f) return false;

  outSlope = (static_cast<float>(n) * sumTC - sumT * sumC) / denominator;
  return true;
}

}  // namespace RateEstimatorLogic
