#pragma once

#include <stdio.h>

/**
 * Дата сборки прошивки в виде дд.мм.гггг.
 *
 * Компилятор даёт __DATE__ как "Nov  9 2026": месяц словом, день с ведущим
 * пробелом. Разбираем это на этапе компиляции, чтобы на экране рядом с
 * версией стояла понятная дата, а не английская строка.
 */
namespace FwBuildInfo {

template <size_t N>
constexpr int monthFromAbbrev(const char (&d)[N]) {
  // C++11: ровно одно выражение, никаких локальных переменных и ветвлений
  return (d[0] == 'J' && d[1] == 'a')  ? 1
         : (d[0] == 'F')                ? 2
         : (d[0] == 'M' && d[2] == 'r') ? 3
         : (d[0] == 'A' && d[1] == 'p') ? 4
         : (d[0] == 'M')                ? 5
         : (d[0] == 'J' && d[2] == 'n') ? 6
         : (d[0] == 'J')                ? 7
         : (d[0] == 'A')                ? 8
         : (d[0] == 'S')                ? 9
         : (d[0] == 'O')                ? 10
         : (d[0] == 'N')                ? 11
                                        : 12;
}

template <size_t N>
constexpr int dayFromDate(const char (&d)[N]) {
  // "Nov  9 2026" — день занимает позиции 4..5, однозначный сдвинут вправо
  return d[4] == ' ' ? (d[5] - '0')
                     : ((d[4] - '0') * 10 + (d[5] - '0'));
}

template <size_t N>
constexpr int yearFromDate(const char (&d)[N]) {
  return (d[7] - '0') * 1000 + (d[8] - '0') * 100 + (d[9] - '0') * 10 +
         (d[10] - '0');
}

constexpr int kDay = dayFromDate(__DATE__);
constexpr int kMonth = monthFromAbbrev(__DATE__);
constexpr int kYear = yearFromDate(__DATE__);

/** "09.11.2026" — для мелкой строки рядом с версией. */
inline void format(char *out, size_t size) {
  if (out == nullptr || size == 0) {
    return;
  }
  snprintf(out, size, "%02d.%02d.%04d", kDay, kMonth, kYear);
}

}  // namespace FwBuildInfo
