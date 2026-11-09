#pragma once

#include <stdint.h>

/**
 * HMI v2 — чистая модель страницы: окно прокрутки, рельс, нижняя панель и
 * подбор размера шрифта для информационного табло.
 *
 * Здесь нет ни LovyanGFX, ни обращений к g_state: только арифметика, которую
 * можно проверить host-тестами. Рендерер и обработчик касаний используют эти
 * функции, чтобы правило «пять строк на экран» и состав нижней панели жили в
 * одном месте, а не расползались по display.cpp.
 */
namespace HmiPage {

/** Сколько строк помещается в рабочей области при 480×320. */
static constexpr uint8_t kVisibleRows = 5;

/** Минимальная высота ползунка рельса прокрутки, px. */
static constexpr uint16_t kMinThumbPx = 26;

enum class Action : uint8_t {
  None = 0,
  Back,
  Exit,
  Save,
  Pause,
  Stop,
  Service,
  Cancel,
  Done,
};

struct Window {
  uint16_t first;   // индекс первой видимой строки
  uint8_t count;    // сколько строк реально видно
  uint16_t total;   // всего строк на странице
};

struct Footer {
  Action left;
  Action middle;
  Action right;
  bool leftEnabled;
  bool middleEnabled;
  bool rightEnabled;
};

struct Thumb {
  uint16_t top;
  uint16_t height;
};

/** Максимально допустимое смещение прокрутки. */
inline uint16_t maxScroll(uint16_t total, uint8_t visible) {
  return total > visible ? static_cast<uint16_t>(total - visible) : 0;
}

/** Смещение, гарантированно попадающее в [0, maxScroll]. */
inline uint16_t clampScroll(uint16_t scroll, uint16_t total, uint8_t visible) {
  const uint16_t limit = maxScroll(total, visible);
  return scroll > limit ? limit : scroll;
}

/** Видимое окно строк для текущего смещения. */
inline Window windowFor(uint16_t scroll, uint16_t total, uint8_t visible) {
  Window window;
  window.total = total;
  window.first = clampScroll(scroll, total, visible);
  const uint16_t remaining = total > window.first
                                 ? static_cast<uint16_t>(total - window.first)
                                 : 0;
  window.count = remaining < visible ? static_cast<uint8_t>(remaining) : visible;
  return window;
}

/** Нужен ли рельс прокрутки. */
inline bool railVisible(uint16_t total, uint8_t visible) {
  return total > visible;
}

/**
 * Геометрия ползунка: высота пропорциональна видимой части, положение —
 * прогрессу прокрутки. trackHeight — доступная высота дорожки без стрелок.
 */
inline Thumb thumbFor(uint16_t trackHeight, uint16_t total, uint8_t visible,
                      uint16_t scroll) {
  Thumb thumb{0, 0};
  if (trackHeight == 0 || total == 0) {
    return thumb;
  }

  uint32_t height = static_cast<uint32_t>(trackHeight) * visible / total;
  if (height < kMinThumbPx) {
    height = kMinThumbPx;
  }
  if (height > trackHeight) {
    height = trackHeight;
  }

  const uint16_t limit = maxScroll(total, visible);
  const uint16_t position = clampScroll(scroll, total, visible);
  const uint32_t travel = trackHeight - height;
  const uint32_t top = limit == 0 ? 0 : travel * position / limit;

  thumb.height = static_cast<uint16_t>(height);
  thumb.top = static_cast<uint16_t>(top);
  return thumb;
}

/**
 * Состав нижней панели.
 *
 * running       — активен процесс;
 * subScreen     — открыт подэкран поверх раздела;
 * infoScreen    — открыто табло;
 * keyboardScreen— открыт ввод пароля/клавиатура;
 * dirty         — есть несохранённые правки.
 */
inline Footer footerFor(bool running, bool subScreen, bool infoScreen,
                        bool keyboardScreen, bool dirty) {
  if (infoScreen) {
    // Табло — только просмотр: назад и стоп (стоп активен при процессе).
    return Footer{Action::Back, Action::None, Action::Stop, true, false, running};
  }
  if (keyboardScreen) {
    return Footer{Action::Cancel, Action::None, Action::Done, true, false, true};
  }
  if (running) {
    return Footer{Action::Pause, Action::Stop, Action::Service, true, true, true};
  }
  if (subScreen) {
    return Footer{Action::Back, Action::Exit, Action::Save, true, true, dirty};
  }
  return Footer{Action::Back, Action::Exit, Action::Save, false, true, dirty};
}

/**
 * Размер шрифта строки таблицы: чем больше строк, тем мельче текст.
 * Возвращает px в координатах экрана (480×320).
 */
inline uint8_t fontForRows(uint8_t rowsPerColumn, uint16_t areaHeightPx,
                           uint8_t minFontPx, uint8_t maxFontPx) {
  if (rowsPerColumn == 0) {
    return maxFontPx;
  }
  const uint32_t perRow = areaHeightPx / rowsPerColumn;
  uint32_t font = perRow * 52 / 100;
  if (font < minFontPx) {
    font = minFontPx;
  }
  if (font > maxFontPx) {
    font = maxFontPx;
  }
  return static_cast<uint8_t>(font);
}

/** Сколько строк в колонке таблицы при заданном числе колонок. */
inline uint8_t rowsPerColumn(uint16_t total, uint8_t columns) {
  if (columns == 0) {
    return static_cast<uint8_t>(total);
  }
  return static_cast<uint8_t>((total + columns - 1) / columns);
}

}  // namespace HmiPage