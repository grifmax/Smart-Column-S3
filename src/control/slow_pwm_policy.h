#pragma once

#include <stdint.h>

/**
 * Медленный PWM для соленоидного клапана.
 *
 * Быстрый LEDC (1 кГц) на катушку подавать нельзя: клапан гудит, катушка
 * греется, ресурс падает. Здесь период задаётся секундами, скважность —
 * процентами, а выход либо полностью открыт, либо полностью закрыт:
 *
 *   период 5 с, скважность 50 %  ->  2,5 с открыт / 2,5 с закрыт
 *   скважность 100 %             ->  открыт постоянно
 *   скважность 0 %               ->  закрыт постоянно
 *
 * Модуль чистый: никаких GPIO и millis(), время приходит аргументом, поэтому
 * поведение проверяется host-тестами.
 */
namespace SlowPwm {

static constexpr uint16_t kDefaultPeriodSec = 5;
static constexpr uint16_t kMinPeriodSec = 1;
static constexpr uint16_t kMaxPeriodSec = 600;

static constexpr uint8_t kMinDutyPercent = 0;
static constexpr uint8_t kMaxDutyPercent = 100;

/** Период в допустимых границах (0 -> 1 с, больше 600 -> 600). */
inline uint16_t clampPeriodSec(uint32_t seconds) {
  if (seconds < kMinPeriodSec) {
    return kMinPeriodSec;
  }
  if (seconds > kMaxPeriodSec) {
    return kMaxPeriodSec;
  }
  return static_cast<uint16_t>(seconds);
}

/** Скважность в допустимых границах. */
inline uint8_t clampDutyPercent(uint32_t percent) {
  if (percent > kMaxDutyPercent) {
    return kMaxDutyPercent;
  }
  return static_cast<uint8_t>(percent);
}

/** Длительность цикла в миллисекундах. */
/**
 * Пересчёт «сырой» скважности 0..255 (как её отдаёт gateway и API) в проценты.
 * Округление к ближайшему, чтобы 127 не превращалось в 49 %.
 */
inline uint8_t dutyPercentFromRaw(uint8_t raw) {
  return static_cast<uint8_t>((static_cast<uint16_t>(raw) * 100U + 127U) / 255U);
}

inline uint32_t periodMs(uint16_t periodSec) {
  return static_cast<uint32_t>(clampPeriodSec(periodSec)) * 1000UL;
}

/** Сколько клапан открыт внутри цикла. */
inline uint32_t onTimeMs(uint16_t periodSec, uint8_t dutyPercent) {
  const uint32_t period = periodMs(periodSec);
  return period * clampDutyPercent(dutyPercent) / 100UL;
}

/** Сколько клапан закрыт внутри цикла. */
inline uint32_t offTimeMs(uint16_t periodSec, uint8_t dutyPercent) {
  return periodMs(periodSec) - onTimeMs(periodSec, dutyPercent);
}

/**
 * Положение внутри текущего цикла. Отрицательная разница (now раньше старта)
 * трактуется как только что начавшийся цикл; переполнение millis()
 * обрабатывается обычной беззнаковой арифметикой.
 */
inline uint32_t phaseMs(uint32_t nowMs, uint32_t cycleStartMs,
                        uint16_t periodSec) {
  const uint32_t period = periodMs(periodSec);
  if (period == 0) {
    return 0;
  }
  const uint32_t elapsed = nowMs - cycleStartMs;
  return elapsed % period;
}

/** Открыт ли выход в момент nowMs для цикла, начатого cycleStartMs. */
inline bool isOn(uint32_t nowMs, uint32_t cycleStartMs, uint16_t periodSec,
                 uint8_t dutyPercent) {
  const uint8_t duty = clampDutyPercent(dutyPercent);
  if (duty == 0) {
    return false;
  }
  if (duty == kMaxDutyPercent) {
    return true;
  }
  return phaseMs(nowMs, cycleStartMs, periodSec) <
         onTimeMs(periodSec, duty);
}

}  // namespace SlowPwm