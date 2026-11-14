#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/**
 * HMI v2 — чистая модель клавиатуры Wi-Fi пароля для TFT 480×320.
 *
 * Здесь нет LovyanGFX и обращений к g_state: только геометрия, hit-testing и
 * правка черновика пароля. Рендерер рисует по keyRect()/rowText(), обработчик
 * касаний отдаёт координаты в keyAt() и применяет результат через applyKey().
 *
 * Инварианты:
 *  - пароль — только печатный ASCII 32..126, не длиннее 63 символов (WPA);
 *  - draft правится клавишами, saved меняется только по ГОТОВО;
 *  - ОТМЕНА восстанавливает draft из saved и не трогает saved;
 *  - shift действует ровно на один следующий вставленный символ;
 *  - наружу для показа отдаётся masked(), содержимое пароля нигде не логируется.
 */
namespace HmiKeyboard {

static constexpr int16_t kScreenW = 480;
static constexpr int16_t kScreenH = 320;

static constexpr int16_t kHeaderH = 32;

static constexpr int16_t kFieldX = 8;
static constexpr int16_t kFieldY = 36;
static constexpr int16_t kFieldW = 464;
static constexpr int16_t kFieldH = 40;

/** Строка подписи (SSID) и счётчика длины; рендерер рисует её между полем и клавишами. */
static constexpr int16_t kCaptionX = 8;
static constexpr int16_t kCaptionY = 80;
static constexpr int16_t kCaptionH = 14;

/** Три ряда символов и функциональный ряд: 4 × 40 px с зазором 4 px. */
static constexpr int16_t kKeysX = 2;
static constexpr int16_t kKeysY = 96;
static constexpr int16_t kKeyW = 476;
static constexpr int16_t kKeyH = 40;
static constexpr int16_t kKeyGapX = 3;
static constexpr int16_t kKeyGapY = 4;
static constexpr int16_t kKeysBottom =
    static_cast<int16_t>(kKeysY + 4 * kKeyH + 3 * kKeyGapY);

/** Нижняя панель 48 px: ОТМЕНА слева, ГОТОВО справа. */
static constexpr int16_t kFooterY = 272;
static constexpr int16_t kFooterH = 48;

static constexpr uint8_t kMaxPassword = 63;
static constexpr uint8_t kPasswordBuffer = kMaxPassword + 1;
static constexpr uint8_t kFooterRow = 4;

static_assert(kPasswordBuffer == 64, "WPA password buffer must hold 63 chars + NUL");
static_assert(kFooterY + kFooterH == kScreenH, "footer must end exactly at 320");
static_assert(kKeysBottom + kKeyGapY == kFooterY, "4 px gap between keys and footer");

enum class Layout : uint8_t { Letters = 0, Symbols = 1 };

enum class Action : uint8_t {
  None = 0,
  Character,
  Shift,
  Backspace,
  Space,
  ToggleLayout,
  Cancel,
  Done,
};

/** Что произошло после applyKey(); рендерер решает, нужен ли redraw/закрытие. */
enum class Result : uint8_t {
  NoChange = 0,
  Changed,
  Committed,
  Cancelled,
  Rejected,
};

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

struct Key {
  Action action;
  char ch;       // заполнен только для Action::Character
  uint8_t row;   // 0..2 — символы, 3 — функциональный ряд, 4 — footer
  uint8_t index;

  Key() : action(Action::None), ch(0), row(0), index(0) {}
  bool valid() const { return action != Action::None; }
};

/**
 * Черновик и сохранённое значение живут рядом, чтобы ОТМЕНА/ГОТОВО были
 * атомарными и проверялись host-тестами. draft никогда не выводить в лог.
 */
struct State {
  char draft[kPasswordBuffer];
  char saved[kPasswordBuffer];
  uint8_t length;
  uint8_t savedLength;
  Layout layout;
  bool shift;

  State() : length(0), savedLength(0), layout(Layout::Letters), shift(false) {
    for (uint8_t i = 0; i < kPasswordBuffer; ++i) {
      draft[i] = '\0';
      saved[i] = '\0';
    }
  }
};

inline bool isPrintableAscii(char c) {
  const unsigned char value = static_cast<unsigned char>(c);
  return value >= 32 && value <= 126;
}

inline bool contains(const Rect &rect, int16_t x, int16_t y) {
  return rect.w > 0 && rect.h > 0 && x >= rect.x && x < rect.x + rect.w &&
         y >= rect.y && y < rect.y + rect.h;
}

/** Строки страниц. Латинские буквы — только нижний регистр: верхний даёт shift. */
inline const char *rowText(Layout layout, uint8_t row) {
  static const char *const kLetters[3] = {
      "qwertyuiop",
      "asdfghjkl",
      "zxcvbnm-_.",
  };
  static const char *const kSymbols[3] = {
      "1234567890!\"#",
      "$%&'()*+,/:;<",
      "=>?@[\\]^`{|}~",
  };
  if (row > 2) return nullptr;
  return layout == Layout::Letters ? kLetters[row] : kSymbols[row];
}

/** Число клавиш в ряду; row 3 — функциональный ряд из четырёх клавиш. */
inline uint8_t rowLength(Layout layout, uint8_t row) {
  if (row > 3) return 0;
  if (row == 3) return 4;
  if (layout == Layout::Letters) return row == 1 ? 9 : 10;
  return 13;
}

inline int16_t keyRowY(uint8_t row) {
  return static_cast<int16_t>(kKeysY + row * (kKeyH + kKeyGapY));
}

inline Action functionAction(uint8_t index) {
  switch (index) {
    case 0: return Action::Shift;
    case 1: return Action::ToggleLayout;
    case 2: return Action::Space;
    case 3: return Action::Backspace;
    default: return Action::None;
  }
}

inline const char *functionLabel(uint8_t index, Layout layout) {
  switch (index) {
    case 0: return "SHIFT";
    case 1: return layout == Layout::Letters ? "123" : "ABC";
    case 2: return "ПРОБЕЛ";
    case 3: return "<X";
    default: return "";
  }
}

/** Функциональный ряд: фиксированные ширины, суммарно 476 px с зазорами. */
inline Rect functionRect(uint8_t index) {
  const int16_t y = keyRowY(3);
  switch (index) {
    case 0: return Rect{kKeysX, y, 84, kKeyH};
    case 1: return Rect{89, y, 84, kKeyH};
    case 2: return Rect{176, y, 211, kKeyH};
    case 3: return Rect{390, y, 88, kKeyH};
    default: return Rect{0, 0, 0, 0};
  }
}

inline Rect footerRect(bool done) {
  return done ? Rect{244, kFooterY, 228, kFooterH}
              : Rect{8, kFooterY, 228, kFooterH};
}

/**
 * Прямоугольник клавиши. Остаток ширины распределяется по первым клавишам,
 * поэтому соседние клавиши не перекрываются и не выходят за 480 px.
 */
inline Rect keyRect(Layout layout, uint8_t row, uint8_t index) {
  const uint8_t count = rowLength(layout, row);
  if (index >= count) return Rect{0, 0, 0, 0};
  if (row == 3) return functionRect(index);

  const int16_t usable =
      static_cast<int16_t>(kKeyW - static_cast<int16_t>(count - 1) * kKeyGapX);
  const int16_t base = static_cast<int16_t>(usable / count);
  const int16_t extra = static_cast<int16_t>(usable % count);

  int16_t x = kKeysX;
  for (uint8_t i = 0; i < index; ++i) {
    const int16_t width = static_cast<int16_t>(base + (i < extra ? 1 : 0));
    x = static_cast<int16_t>(x + width + kKeyGapX);
  }
  const int16_t width = static_cast<int16_t>(base + (index < extra ? 1 : 0));
  return Rect{x, keyRowY(row), width, kKeyH};
}

/** Клавиша под точкой; вне интерактивных зон возвращает Action::None. */
inline Key keyAt(const State &state, int16_t x, int16_t y) {
  Key key;
  if (x < 0 || y < 0 || x >= kScreenW || y >= kScreenH) return key;

  if (y >= kFooterY) {
    if (contains(footerRect(false), x, y)) {
      key.action = Action::Cancel;
      key.row = kFooterRow;
      key.index = 0;
    } else if (contains(footerRect(true), x, y)) {
      key.action = Action::Done;
      key.row = kFooterRow;
      key.index = 1;
    }
    return key;
  }

  if (y < kKeysY || y >= kKeysBottom) return key;

  const int16_t step = static_cast<int16_t>(kKeyH + kKeyGapY);
  const int16_t relative = static_cast<int16_t>(y - kKeysY);
  const uint8_t row = static_cast<uint8_t>(relative / step);
  if (relative % step >= kKeyH) return key;  // межрядный зазор

  const uint8_t count = rowLength(state.layout, row);
  for (uint8_t index = 0; index < count; ++index) {
    const Rect rect = keyRect(state.layout, row, index);
    if (!contains(rect, x, y)) continue;
    key.row = row;
    key.index = index;
    if (row < 3) {
      key.action = Action::Character;
      key.ch = rowText(state.layout, row)[index];
    } else {
      key.action = functionAction(index);
    }
    return key;
  }
  return key;  // межклавишный зазор
}

/** Символ с учётом shift; чистая функция, состояние не меняет. */
inline char displayChar(const State &state, char ch) {
  if (state.shift && ch >= 'a' && ch <= 'z') {
    return static_cast<char>(ch - 'a' + 'A');
  }
  return ch;
}

/** Сбросить состояние и загрузить сохранённый пароль (только печатный ASCII). */
inline void init(State &state, const char *saved) {
  state = State();
  if (saved == nullptr) return;

  uint8_t n = 0;
  while (n < kMaxPassword && saved[n] != '\0' && isPrintableAscii(saved[n])) {
    state.saved[n] = saved[n];
    ++n;
  }
  state.savedLength = n;
  state.saved[n] = '\0';
  for (uint8_t i = 0; i < n; ++i) state.draft[i] = state.saved[i];
  state.length = n;
  state.draft[n] = '\0';
}

/** Открыть экран заново: черновик = saved, раскладка и shift сброшены. */
inline void open(State &state) {
  for (uint8_t i = 0; i < state.savedLength; ++i) state.draft[i] = state.saved[i];
  state.length = state.savedLength;
  state.draft[state.length] = '\0';
  state.layout = Layout::Letters;
  state.shift = false;
}

inline Result appendChar(State &state, char ch) {
  if (state.length >= kMaxPassword) return Result::Rejected;
  state.draft[state.length] = ch;
  ++state.length;
  state.draft[state.length] = '\0';
  return Result::Changed;
}

inline Result applyKey(State &state, const Key &key) {
  switch (key.action) {
    case Action::Character: {
      if (!isPrintableAscii(key.ch)) return Result::Rejected;
      const Result result = appendChar(state, displayChar(state, key.ch));
      if (result == Result::Changed && state.shift) state.shift = false;
      return result;
    }
    case Action::Space: {
      const Result result = appendChar(state, ' ');
      if (result == Result::Changed && state.shift) state.shift = false;
      return result;
    }
    case Action::Backspace:
      if (state.length == 0) return Result::NoChange;
      --state.length;
      state.draft[state.length] = '\0';
      return Result::Changed;
    case Action::Shift:
      state.shift = !state.shift;
      return Result::Changed;
    case Action::ToggleLayout:
      state.layout = (state.layout == Layout::Letters) ? Layout::Symbols
                                                       : Layout::Letters;
      return Result::Changed;
    case Action::Cancel:
      for (uint8_t i = 0; i < state.savedLength; ++i) {
        state.draft[i] = state.saved[i];
      }
      state.length = state.savedLength;
      state.draft[state.length] = '\0';
      state.layout = Layout::Letters;
      state.shift = false;
      return Result::Cancelled;
    case Action::Done:
      for (uint8_t i = 0; i < state.length; ++i) state.saved[i] = state.draft[i];
      state.savedLength = state.length;
      state.saved[state.savedLength] = '\0';
      state.layout = Layout::Letters;
      state.shift = false;
      return Result::Committed;
    case Action::None:
    default:
      return Result::NoChange;
  }
}

/** Удобный путь для обработчика касаний: координаты → keyAt → applyKey. */
inline Result applyAt(State &state, int16_t x, int16_t y) {
  return applyKey(state, keyAt(state, x, y));
}

/**
 * Маска для поля ввода: ровно length символов '*', содержимое не раскрывается.
 * Возвращает число записанных звёздочек, всегда ставит NUL при size > 0.
 */
inline size_t masked(const State &state, char *out, size_t size) {
  if (out == nullptr || size == 0) return 0;
  size_t n = state.length;
  if (n > size - 1) n = size - 1;
  for (size_t i = 0; i < n; ++i) out[i] = '*';
  out[n] = '\0';
  return n;
}

/** Счётчик "длина/63" для правого края строки подписи. */
inline size_t counter(const State &state, char *out, size_t size) {
  if (out == nullptr || size == 0) return 0;
  (void)snprintf(out, size, "%u/%u", static_cast<unsigned>(state.length),
                 static_cast<unsigned>(kMaxPassword));
  return strlen(out);
}

}  // namespace HmiKeyboard
