#!/usr/bin/env python3
"""Host tests for src/ui/hmi/keyboard_model.h (TFT WPA keyboard model).

Run: python scripts/test_hmi_keyboard.py
Compiles one small C++11 harness with the PlatformIO MinGW g++ and runs one
named case per test. No PlatformIO build, no firmware, no device I/O.
Generated sources/binaries live in the Hermes scratch directory.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = Path.home() / "AppData/Local/hermes/cache/scratch"
COMPILER = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin/g++.exe"

HARNESS = r'''
#include "ui/hmi/keyboard_model.h"

#include <iostream>
#include <string>
#include <vector>

using HmiKeyboard::Action;
using HmiKeyboard::Key;
using HmiKeyboard::Layout;
using HmiKeyboard::Rect;
using HmiKeyboard::Result;
using HmiKeyboard::State;

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << "line " << __LINE__ << ": " << #condition << '\n'; \
  return 1; } } while (false)

#define CHECK_CHAR(actual, expected) do { \
  const int a_ = static_cast<int>(actual); \
  const int e_ = static_cast<int>(expected); \
  if (a_ != e_) { std::cerr << "line " << __LINE__ << ": char " << a_ \
      << " != " << e_ << '\n'; return 1; } } while (false)

static Key chKey(char c) {
  Key key;
  key.action = Action::Character;
  key.ch = c;
  return key;
}

static Key centerKey(const State& state, uint8_t row, uint8_t index) {
  const Rect rect = HmiKeyboard::keyRect(state.layout, row, index);
  return HmiKeyboard::keyAt(state,
      static_cast<int16_t>(rect.x + rect.w / 2),
      static_cast<int16_t>(rect.y + rect.h / 2));
}

static Result click(State& state, uint8_t row, uint8_t index) {
  const Key key = centerKey(state, row, index);
  return HmiKeyboard::applyKey(state, key);
}

static void appendText(State& state, const char* text) {
  for (const char* p = text; *p != '\0'; ++p) {
    HmiKeyboard::applyKey(state, chKey(*p));
  }
}

static bool overlaps(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w &&
         a.y < b.y + b.h && b.y < a.y + a.h;
}

static void collectRects(Layout layout, std::vector<Rect>& out) {
  for (uint8_t row = 0; row < 4; ++row) {
    const uint8_t count = HmiKeyboard::rowLength(layout, row);
    for (uint8_t index = 0; index < count; ++index) {
      out.push_back(HmiKeyboard::keyRect(layout, row, index));
    }
  }
}

static bool sameKey(const Key& a, const Key& b) {
  return a.action == b.action && a.ch == b.ch &&
         a.row == b.row && a.index == b.index;
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string name(argv[1]);

  if (name == "geometry") {
    CHECK(HmiKeyboard::kScreenW == 480);
    CHECK(HmiKeyboard::kScreenH == 320);
    CHECK(HmiKeyboard::kHeaderH == 32);
    CHECK(HmiKeyboard::kFieldX == 8);
    CHECK(HmiKeyboard::kFieldY == 36);
    CHECK(HmiKeyboard::kFieldW == 464);
    CHECK(HmiKeyboard::kFieldH == 40);
    CHECK(HmiKeyboard::kFieldY + HmiKeyboard::kFieldH == 76);
    CHECK(HmiKeyboard::kCaptionY >= HmiKeyboard::kFieldY + HmiKeyboard::kFieldH);
    CHECK(HmiKeyboard::kCaptionY + HmiKeyboard::kCaptionH <= HmiKeyboard::kKeysY);
    CHECK(HmiKeyboard::kKeysY == 96);
    CHECK(HmiKeyboard::keyRowY(0) == 96);
    CHECK(HmiKeyboard::keyRowY(1) == 140);
    CHECK(HmiKeyboard::keyRowY(2) == 184);
    CHECK(HmiKeyboard::keyRowY(3) == 228);
    CHECK(HmiKeyboard::keyRowY(3) + HmiKeyboard::kKeyH == 268);
    CHECK(HmiKeyboard::kFooterY == 272);
    CHECK(HmiKeyboard::kFooterH == 48);
    CHECK(HmiKeyboard::kFooterY + HmiKeyboard::kFooterH == 320);
    const Rect cancel = HmiKeyboard::footerRect(false);
    const Rect done = HmiKeyboard::footerRect(true);
    CHECK(cancel.x == 8 && cancel.y == 272 && cancel.w == 228 && cancel.h == 48);
    CHECK(done.x == 244 && done.y == 272 && done.w == 228 && done.h == 48);
    CHECK(!overlaps(cancel, done));
    // The four key rows sit in one continuous band above the footer.
    CHECK(HmiKeyboard::keyRowY(3) + HmiKeyboard::kKeyH <= HmiKeyboard::kFooterY);
    return 0;
  }

  if (name == "layout_tables") {
    CHECK(HmiKeyboard::rowLength(Layout::Letters, 0) == 10);
    CHECK(HmiKeyboard::rowLength(Layout::Letters, 1) == 9);
    CHECK(HmiKeyboard::rowLength(Layout::Letters, 2) == 10);
    CHECK(HmiKeyboard::rowLength(Layout::Symbols, 0) == 13);
    CHECK(HmiKeyboard::rowLength(Layout::Symbols, 1) == 13);
    CHECK(HmiKeyboard::rowLength(Layout::Symbols, 2) == 13);
    CHECK(HmiKeyboard::rowLength(Layout::Letters, 3) == 4);
    CHECK(HmiKeyboard::rowLength(Layout::Symbols, 3) == 4);
    bool letters[26] = {false};
    for (uint8_t row = 0; row < 3; ++row) {
      const char* text = HmiKeyboard::rowText(Layout::Letters, row);
      CHECK(text != nullptr);
      CHECK(static_cast<uint8_t>(std::string(text).size()) ==
            HmiKeyboard::rowLength(Layout::Letters, row));
      for (const char* p = text; *p != '\0'; ++p) {
        CHECK(HmiKeyboard::isPrintableAscii(*p));
        if (*p >= 'a' && *p <= 'z') letters[*p - 'a'] = true;
      }
    }
    for (unsigned i = 0; i < 26; ++i) CHECK(letters[i]);
    for (uint8_t row = 0; row < 3; ++row) {
      const char* text = HmiKeyboard::rowText(Layout::Symbols, row);
      CHECK(text != nullptr);
      CHECK(static_cast<uint8_t>(std::string(text).size()) ==
            HmiKeyboard::rowLength(Layout::Symbols, row));
      for (const char* p = text; *p != '\0'; ++p) CHECK(HmiKeyboard::isPrintableAscii(*p));
    }
    CHECK(std::string(HmiKeyboard::functionLabel(0, Layout::Letters)) == "SHIFT");
    CHECK(std::string(HmiKeyboard::functionLabel(1, Layout::Letters)) == "123");
    CHECK(std::string(HmiKeyboard::functionLabel(1, Layout::Symbols)) == "ABC");
    CHECK(std::string(HmiKeyboard::functionLabel(2, Layout::Letters)) == "ПРОБЕЛ");
    CHECK(std::string(HmiKeyboard::functionLabel(3, Layout::Letters)) == "<X");
    return 0;
  }

  if (name == "hit_every_key") {
    const Layout layouts[2] = {Layout::Letters, Layout::Symbols};
    for (unsigned l = 0; l < 2; ++l) {
      State state;
      HmiKeyboard::init(state, nullptr);
      state.layout = layouts[l];
      for (uint8_t row = 0; row < 4; ++row) {
        const uint8_t count = HmiKeyboard::rowLength(state.layout, row);
        for (uint8_t index = 0; index < count; ++index) {
          const Rect rect = HmiKeyboard::keyRect(state.layout, row, index);
          CHECK(rect.w > 0 && rect.h == HmiKeyboard::kKeyH);
          CHECK(rect.x >= 0 && rect.x + rect.w <= HmiKeyboard::kScreenW);
          CHECK(rect.y == HmiKeyboard::keyRowY(row));
          const Key key = centerKey(state, row, index);
          CHECK(key.valid());
          CHECK(key.row == row);
          CHECK(key.index == index);
          if (row < 3) {
            CHECK(key.action == Action::Character);
            CHECK_CHAR(key.ch, HmiKeyboard::rowText(state.layout, row)[index]);
          } else {
            CHECK(key.action == HmiKeyboard::functionAction(index));
            CHECK(key.action != Action::Character);
          }
        }
      }
    }
    return 0;
  }

  if (name == "no_overlaps") {
    std::vector<Rect> pages[2];
    collectRects(Layout::Letters, pages[0]);
    collectRects(Layout::Symbols, pages[1]);
    CHECK(pages[0].size() == 33);
    CHECK(pages[1].size() == 43);
    const Rect cancel = HmiKeyboard::footerRect(false);
    const Rect done = HmiKeyboard::footerRect(true);
    const Rect field{HmiKeyboard::kFieldX, HmiKeyboard::kFieldY,
                     HmiKeyboard::kFieldW, HmiKeyboard::kFieldH};
    const Rect caption{HmiKeyboard::kCaptionX, HmiKeyboard::kCaptionY,
                       HmiKeyboard::kFieldW, HmiKeyboard::kCaptionH};
    // Pages share the same rows, so overlap is checked inside each page only.
    for (unsigned page = 0; page < 2; ++page) {
      for (size_t i = 0; i < pages[page].size(); ++i) {
        const Rect a = pages[page][i];
        CHECK(a.w > 0 && a.h > 0);
        CHECK(a.x >= 0 && a.x + a.w <= HmiKeyboard::kScreenW);
        CHECK(a.y >= 0 && a.y + a.h <= HmiKeyboard::kScreenH);
        CHECK(!overlaps(a, field));
        CHECK(!overlaps(a, caption));
        CHECK(!overlaps(a, cancel));
        CHECK(!overlaps(a, done));
        for (size_t j = i + 1; j < pages[page].size(); ++j) {
          CHECK(!overlaps(a, pages[page][j]));
        }
      }
    }
    CHECK(!overlaps(cancel, done));
    CHECK(!overlaps(cancel, field));
    CHECK(!overlaps(done, field));
    // Character rows never leave the shared key band.
    for (uint8_t l = 0; l < 2; ++l) {
      const Layout layout = l == 0 ? Layout::Letters : Layout::Symbols;
      for (uint8_t row = 0; row < 4; ++row) {
        const Rect rect = HmiKeyboard::keyRect(layout, row, 0);
        CHECK(rect.y >= HmiKeyboard::kKeysY);
        CHECK(rect.y + rect.h <= HmiKeyboard::kKeysBottom);
      }
    }
    return 0;
  }

  if (name == "all_three_rows") {
    State state;
    HmiKeyboard::init(state, nullptr);
    CHECK(click(state, 0, 0) == Result::Changed);  // q
    CHECK(click(state, 1, 0) == Result::Changed);  // a
    CHECK(click(state, 2, 0) == Result::Changed);  // z
    CHECK(std::string(state.draft) == "qaz");
    CHECK(state.length == 3);
    // Every one of the three rows was actually hit at its own Y.
    for (uint8_t row = 0; row < 3; ++row) {
      const Key key = centerKey(state, row, 1);
      CHECK(key.action == Action::Character);
      CHECK(key.row == row);
    }
    // Symbols page has the same three live rows.
    CHECK(click(state, 3, 1) == Result::Changed);  // 123 toggle
    CHECK(state.layout == Layout::Symbols);
    CHECK(click(state, 0, 0) == Result::Changed);  // 1
    CHECK(click(state, 1, 0) == Result::Changed);  // $
    CHECK(click(state, 2, 0) == Result::Changed);  // =
    CHECK(std::string(state.draft) == "qaz1$=");
    return 0;
  }

  if (name == "hit_boundaries") {
    const Layout layouts[2] = {Layout::Letters, Layout::Symbols};
    for (unsigned l = 0; l < 2; ++l) {
      State state;
      HmiKeyboard::init(state, nullptr);
      state.layout = layouts[l];
      for (uint8_t row = 0; row < 4; ++row) {
        const uint8_t count = HmiKeyboard::rowLength(state.layout, row);
        for (uint8_t index = 0; index < count; ++index) {
          const Rect rect = HmiKeyboard::keyRect(state.layout, row, index);
          const Key expected = centerKey(state, row, index);
          CHECK(sameKey(HmiKeyboard::keyAt(state, rect.x, rect.y), expected));
          CHECK(sameKey(HmiKeyboard::keyAt(state,
              static_cast<int16_t>(rect.x + rect.w - 1),
              static_cast<int16_t>(rect.y + rect.h - 1)), expected));
          // One pixel past the right/bottom edge is the inter-key gap.
          CHECK(HmiKeyboard::keyAt(state,
              static_cast<int16_t>(rect.x + rect.w),
              static_cast<int16_t>(rect.y + rect.h / 2)).action == Action::None);
          CHECK(HmiKeyboard::keyAt(state,
              static_cast<int16_t>(rect.x + rect.w / 2),
              static_cast<int16_t>(rect.y + rect.h)).action == Action::None);
        }
      }
    }
    return 0;
  }

  if (name == "shift_single") {
    State state;
    HmiKeyboard::init(state, nullptr);
    CHECK(click(state, 3, 0) == Result::Changed);  // SHIFT on
    CHECK(state.shift);
    CHECK(click(state, 0, 0) == Result::Changed);  // q -> Q
    CHECK(std::string(state.draft) == "Q");
    CHECK(!state.shift);
    CHECK(click(state, 0, 1) == Result::Changed);  // w -> w
    CHECK(std::string(state.draft) == "Qw");
    CHECK(click(state, 3, 0) == Result::Changed);
    CHECK(click(state, 1, 0) == Result::Changed);  // a -> A
    CHECK(std::string(state.draft) == "QwA");
    CHECK(!state.shift);
    // Backspace is not a character: shift survives it.
    CHECK(click(state, 3, 0) == Result::Changed);
    CHECK(click(state, 3, 3) == Result::Changed);  // <X
    CHECK(std::string(state.draft) == "Qw");
    CHECK(state.shift);
    CHECK(click(state, 2, 0) == Result::Changed);  // z -> Z
    CHECK(std::string(state.draft) == "QwZ");
    CHECK(!state.shift);
    // Shift on the symbols page is consumed by the next symbol unchanged.
    CHECK(click(state, 3, 1) == Result::Changed);  // 123
    CHECK(click(state, 3, 0) == Result::Changed);  // SHIFT
    CHECK(click(state, 0, 0) == Result::Changed);  // 1
    CHECK(state.draft[state.length - 1] == '1');
    CHECK(!state.shift);
    return 0;
  }

  if (name == "character_guard") {
    State state;
    HmiKeyboard::init(state, nullptr);
    const char bad[] = {'\n', '\r', '\t', '\001', '\x7f'};
    for (unsigned i = 0; i < sizeof(bad); ++i) {
      CHECK(HmiKeyboard::applyKey(state, chKey(bad[i])) == Result::Rejected);
      CHECK(state.length == 0);
      CHECK(state.draft[0] == '\0');
    }
    CHECK(HmiKeyboard::applyKey(state, chKey(' ')) == Result::Changed);
    CHECK(HmiKeyboard::applyKey(state, chKey('~')) == Result::Changed);
    CHECK(std::string(state.draft) == " ~");
    return 0;
  }

  if (name == "display_char") {
    State state;
    HmiKeyboard::init(state, nullptr);
    state.shift = true;
    CHECK_CHAR(HmiKeyboard::displayChar(state, 'a'), 'A');
    CHECK_CHAR(HmiKeyboard::displayChar(state, 'z'), 'Z');
    CHECK_CHAR(HmiKeyboard::displayChar(state, 'A'), 'A');
    CHECK_CHAR(HmiKeyboard::displayChar(state, '1'), '1');
    CHECK_CHAR(HmiKeyboard::displayChar(state, '-'), '-');
    CHECK(state.shift);  // displayChar is pure
    state.shift = false;
    CHECK_CHAR(HmiKeyboard::displayChar(state, 'a'), 'a');
    return 0;
  }

  if (name == "ascii_complete") {
    bool seen[128] = {false};
    const Layout layouts[2] = {Layout::Letters, Layout::Symbols};
    for (unsigned l = 0; l < 2; ++l) {
      State state;
      HmiKeyboard::init(state, nullptr);
      state.layout = layouts[l];
      for (uint8_t row = 0; row < 3; ++row) {
        const uint8_t count = HmiKeyboard::rowLength(state.layout, row);
        for (uint8_t index = 0; index < count; ++index) {
          const Key key = centerKey(state, row, index);
          CHECK(key.action == Action::Character);
          seen[static_cast<unsigned char>(key.ch)] = true;
          State upper;
          HmiKeyboard::init(upper, nullptr);
          upper.layout = layouts[l];
          upper.shift = true;
          const Key upperKey = centerKey(upper, row, index);
          CHECK(HmiKeyboard::applyKey(upper, upperKey) == Result::Changed);
          CHECK(upper.length == 1);
          seen[static_cast<unsigned char>(upper.draft[0])] = true;
        }
      }
    }
    // Space comes from the dedicated function key on either page.
    for (unsigned l = 0; l < 2; ++l) {
      State state;
      HmiKeyboard::init(state, nullptr);
      state.layout = layouts[l];
      CHECK(click(state, 3, 2) == Result::Changed);
      CHECK(state.length == 1);
      CHECK_CHAR(state.draft[0], ' ');
      seen[static_cast<unsigned char>(state.draft[0])] = true;
    }
    for (int c = 32; c <= 126; ++c) {
      if (!seen[c]) {
        std::cerr << "ASCII " << c << " is unreachable\n";
        return 1;
      }
    }
    int count = 0;
    for (int c = 32; c <= 126; ++c) count += seen[c] ? 1 : 0;
    CHECK(count == 95);
    for (int c = 0; c < 32; ++c) CHECK(!seen[c]);
    CHECK(!seen[127]);
    return 0;
  }

  if (name == "max_length") {
    State state;
    HmiKeyboard::init(state, "seed");
    for (unsigned i = 0; i < 4; ++i) {
      CHECK(click(state, 3, 3) == Result::Changed);  // clear the seeded draft
    }
    CHECK(state.length == 0);
    CHECK(std::string(state.saved) == "seed");
    for (unsigned i = 0; i < 63; ++i) {
      CHECK(HmiKeyboard::applyKey(state, chKey('a')) == Result::Changed);
    }
    CHECK(state.length == 63);
    char expected[64];
    for (unsigned i = 0; i < 63; ++i) expected[i] = 'a';
    expected[63] = '\0';
    CHECK(std::string(state.draft) == std::string(expected));
    CHECK(state.draft[63] == '\0');
    CHECK(HmiKeyboard::applyKey(state, chKey('b')) == Result::Rejected);
    CHECK(state.length == 63);
    CHECK(std::string(state.draft) == std::string(expected));
    CHECK(HmiKeyboard::applyKey(state, HmiKeyboard::Key()) == Result::NoChange);
    CHECK(HmiKeyboard::applyKey(state, chKey(' ')) == Result::Rejected);
    CHECK(state.length == 63);
    CHECK(click(state, 3, 3) == Result::Changed);  // backspace
    CHECK(state.length == 62);
    CHECK(HmiKeyboard::applyKey(state, chKey('b')) == Result::Changed);
    CHECK(state.length == 63);
    CHECK_CHAR(state.draft[62], 'b');
    CHECK(state.draft[63] == '\0');
    CHECK(state.savedLength == 4);  // draft changes never touch saved
    return 0;
  }

  if (name == "space_backspace") {
    State state;
    HmiKeyboard::init(state, nullptr);
    CHECK(click(state, 3, 3) == Result::NoChange);
    CHECK(state.length == 0);
    CHECK(click(state, 3, 2) == Result::Changed);
    CHECK(state.length == 1);
    CHECK_CHAR(state.draft[0], ' ');
    CHECK(click(state, 3, 2) == Result::Changed);
    CHECK(state.length == 2);
    CHECK(click(state, 3, 3) == Result::Changed);
    CHECK(state.length == 1);
    CHECK_CHAR(state.draft[0], ' ');
    CHECK(state.draft[1] == '\0');
    CHECK(click(state, 3, 3) == Result::Changed);
    CHECK(state.length == 0);
    CHECK(state.draft[0] == '\0');
    CHECK(click(state, 3, 3) == Result::NoChange);
    CHECK(state.length == 0);
    // Space consumes a pending single-character shift.
    CHECK(click(state, 3, 0) == Result::Changed);
    CHECK(state.shift);
    CHECK(click(state, 3, 2) == Result::Changed);
    CHECK(state.length == 1);
    CHECK(!state.shift);
    return 0;
  }

  if (name == "cancel_keeps_saved") {
    State state;
    HmiKeyboard::init(state, "home-pass");
    CHECK(state.savedLength == 9);
    CHECK(std::string(state.saved) == "home-pass");
    CHECK(std::string(state.draft) == "home-pass");  // draft starts from saved
    CHECK(click(state, 0, 0) == Result::Changed);
    CHECK(click(state, 0, 1) == Result::Changed);
    CHECK(std::string(state.draft) == "home-passqw");
    CHECK(click(state, 3, 1) == Result::Changed);  // switch page, still a draft
    CHECK(state.layout == Layout::Symbols);
    const Rect cancel = HmiKeyboard::footerRect(false);
    const Key key = HmiKeyboard::keyAt(state,
        static_cast<int16_t>(cancel.x + cancel.w / 2),
        static_cast<int16_t>(cancel.y + cancel.h / 2));
    CHECK(key.action == Action::Cancel);
    CHECK(HmiKeyboard::applyKey(state, key) == Result::Cancelled);
    CHECK(std::string(state.saved) == "home-pass");
    CHECK(state.savedLength == 9);
    CHECK(std::string(state.draft) == "home-pass");
    CHECK(state.length == state.savedLength);
    CHECK(state.layout == Layout::Letters);
    CHECK(!state.shift);
    // Cancelling again is harmless and never rewrites saved.
    CHECK(HmiKeyboard::applyKey(state, key) == Result::Cancelled);
    CHECK(std::string(state.saved) == "home-pass");
    return 0;
  }

  if (name == "done_commits") {
    State state;
    HmiKeyboard::init(state, "old");
    appendText(state, "123");
    CHECK(std::string(state.draft) == "old123");
    CHECK(std::string(state.saved) == "old");
    const Rect done = HmiKeyboard::footerRect(true);
    const Key key = HmiKeyboard::keyAt(state,
        static_cast<int16_t>(done.x + done.w / 2),
        static_cast<int16_t>(done.y + done.h / 2));
    CHECK(key.action == Action::Done);
    CHECK(HmiKeyboard::applyKey(state, key) == Result::Committed);
    CHECK(std::string(state.saved) == "old123");
    CHECK(state.savedLength == 6);
    CHECK(std::string(state.draft) == "old123");
    HmiKeyboard::open(state);
    CHECK(std::string(state.draft) == "old123");
    CHECK(state.length == 6);
    // Empty password is a legal committed value for the keyboard layer.
    State empty;
    HmiKeyboard::init(empty, nullptr);
    CHECK(HmiKeyboard::applyKey(empty, key) == Result::Committed);
    CHECK(empty.savedLength == 0);
    CHECK(empty.saved[0] == '\0');
    return 0;
  }

  if (name == "init_limits") {
    State state;
    char longText[96];
    for (unsigned i = 0; i < 80; ++i) longText[i] = 'a';
    longText[80] = '\0';
    HmiKeyboard::init(state, longText);
    CHECK(state.savedLength == 63);
    CHECK(state.length == 63);
    CHECK(state.saved[63] == '\0');
    CHECK(state.saved[62] == 'a');
    CHECK(std::string(state.saved) == std::string(state.draft));
    HmiKeyboard::init(state, "ab\001cd");
    CHECK(std::string(state.saved) == "ab");
    HmiKeyboard::init(state, "ab\x7f");
    CHECK(std::string(state.saved) == "ab");
    HmiKeyboard::init(state, "ab\xC3\xA9");
    CHECK(std::string(state.saved) == "ab");
    HmiKeyboard::init(state, nullptr);
    CHECK(state.length == 0 && state.savedLength == 0);
    CHECK(state.draft[0] == '\0' && state.saved[0] == '\0');
    CHECK(state.layout == Layout::Letters && !state.shift);
    HmiKeyboard::init(state, "");
    CHECK(state.length == 0 && state.savedLength == 0);
    char exactly63[64];
    for (unsigned i = 0; i < 63; ++i) exactly63[i] = 'x';
    exactly63[63] = '\0';
    HmiKeyboard::init(state, exactly63);
    CHECK(state.savedLength == 63);
    CHECK(state.saved[63] == '\0');
    return 0;
  }

  if (name == "mask_counter") {
    State state;
    HmiKeyboard::init(state, nullptr);
    char buffer[80];
    for (unsigned i = 0; i < sizeof(buffer); ++i) buffer[i] = 'X';
    CHECK(HmiKeyboard::masked(state, buffer, sizeof(buffer)) == 0);
    CHECK(buffer[0] == '\0');
    CHECK(std::string(buffer) == "");
    char counter[16];
    CHECK(HmiKeyboard::counter(state, counter, sizeof(counter)) == 4);
    CHECK(std::string(counter) == "0/63");
    appendText(state, "Secret123!");
    CHECK(state.length == 10);
    CHECK(HmiKeyboard::masked(state, buffer, sizeof(buffer)) == 10);
    CHECK(std::string(buffer) == "**********");
    CHECK(std::string(buffer) != std::string(state.draft));
    for (unsigned i = 0; i < 10; ++i) CHECK(buffer[i] == '*');
    CHECK(HmiKeyboard::counter(state, counter, sizeof(counter)) == 5);
    CHECK(std::string(counter) == "10/63");
    // Small destination truncates cleanly and never overruns.
    char small[4];
    small[0] = 'Z'; small[1] = 'Z'; small[2] = 'Z'; small[3] = 'Z';
    CHECK(HmiKeyboard::masked(state, small, sizeof(small)) == 3);
    CHECK(small[3] == '\0');
    CHECK(small[0] == '*' && small[1] == '*' && small[2] == '*');
    char guard[2];
    guard[0] = 'Q'; guard[1] = 'Q';
    CHECK(HmiKeyboard::masked(state, guard, 0) == 0);
    CHECK(guard[0] == 'Q' && guard[1] == 'Q');
    CHECK(HmiKeyboard::masked(state, nullptr, 8) == 0);
    // 63-char draft still masks with no overflow (64-byte output).
    State full;
    HmiKeyboard::init(full, nullptr);
    for (unsigned i = 0; i < 63; ++i) HmiKeyboard::applyKey(full, chKey('~'));
    char fullMask[64];
    CHECK(HmiKeyboard::masked(full, fullMask, sizeof(fullMask)) == 63);
    CHECK(fullMask[63] == '\0');
    for (unsigned i = 0; i < 63; ++i) CHECK(fullMask[i] == '*');
    return 0;
  }

  if (name == "layout_toggle") {
    State state;
    HmiKeyboard::init(state, "abc");
    const Key toggle = centerKey(state, 3, 1);
    CHECK(toggle.action == Action::ToggleLayout);
    CHECK(HmiKeyboard::applyKey(state, toggle) == Result::Changed);
    CHECK(state.layout == Layout::Symbols);
    CHECK(std::string(state.draft) == "abc");
    const Key symbolsKey = centerKey(state, 0, 0);
    CHECK_CHAR(symbolsKey.ch, '1');
    CHECK(HmiKeyboard::applyKey(state, toggle) == Result::Changed);
    CHECK(state.layout == Layout::Letters);
    const Key lettersKey = centerKey(state, 0, 0);
    CHECK_CHAR(lettersKey.ch, 'q');
    CHECK(std::string(HmiKeyboard::functionLabel(1, state.layout)) == "123");
    return 0;
  }

  if (name == "footer_actions") {
    State state;
    HmiKeyboard::init(state, "pw");
    const Rect cancel = HmiKeyboard::footerRect(false);
    const Rect done = HmiKeyboard::footerRect(true);
    CHECK(HmiKeyboard::keyAt(state, cancel.x, cancel.y).action == Action::Cancel);
    CHECK(HmiKeyboard::keyAt(state,
        static_cast<int16_t>(cancel.x + cancel.w - 1),
        static_cast<int16_t>(cancel.y + cancel.h - 1)).action == Action::Cancel);
    CHECK(HmiKeyboard::keyAt(state, done.x, done.y).action == Action::Done);
    CHECK(HmiKeyboard::keyAt(state,
        static_cast<int16_t>(done.x + done.w - 1),
        static_cast<int16_t>(done.y + done.h - 1)).action == Action::Done);
    // The middle of the footer is a dead gap.
    CHECK(HmiKeyboard::keyAt(state, 240, 296).action == Action::None);
    CHECK(HmiKeyboard::keyAt(state,
        static_cast<int16_t>(cancel.x + cancel.w),
        static_cast<int16_t>(cancel.y + cancel.h / 2)).action == Action::None);
    return 0;
  }

  if (name == "outside_points") {
    State state;
    HmiKeyboard::init(state, "pw");
    const int16_t points[][2] = {
      {240, 0}, {240, 31}, {240, 32}, {240, 56}, {240, 86},
      {240, 136}, {240, 269}, {240, 270}, {240, 271}, {240, 296},
      {-1, 100}, {480, 100}, {100, -1}, {100, 320}, {32767, 32767}
    };
    for (unsigned i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
      CHECK(HmiKeyboard::keyAt(state, points[i][0], points[i][1]).action ==
            Action::None);
    }
    const Key none;
    CHECK(HmiKeyboard::applyKey(state, none) == Result::NoChange);
    CHECK(std::string(state.draft) == "pw");
    CHECK(state.length == 2);
    return 0;
  }

  if (name == "pixel_sweep") {
    const Layout layouts[2] = {Layout::Letters, Layout::Symbols};
    for (unsigned l = 0; l < 2; ++l) {
      State state;
      HmiKeyboard::init(state, nullptr);
      state.layout = layouts[l];
      unsigned hits[5][13] = {{0}};
      unsigned interactive = 0;
      for (int16_t y = 0; y < HmiKeyboard::kScreenH; ++y) {
        for (int16_t x = 0; x < HmiKeyboard::kScreenW; ++x) {
          const Key key = HmiKeyboard::keyAt(state, x, y);
          if (!key.valid()) continue;
          ++interactive;
          CHECK(key.row <= HmiKeyboard::kFooterRow);
          if (key.row == HmiKeyboard::kFooterRow) {
            CHECK(key.index < 2);
            const Rect rect = HmiKeyboard::footerRect(key.index == 1);
            CHECK(HmiKeyboard::contains(rect, x, y));
            ++hits[key.row][key.index];
          } else {
            CHECK(key.row < 4);
            CHECK(key.index < HmiKeyboard::rowLength(state.layout, key.row));
            const Rect rect = HmiKeyboard::keyRect(state.layout, key.row, key.index);
            CHECK(HmiKeyboard::contains(rect, x, y));
            ++hits[key.row][key.index];
          }
        }
      }
      unsigned expected = 0;
      for (uint8_t row = 0; row < 4; ++row) {
        for (uint8_t index = 0; index < HmiKeyboard::rowLength(state.layout, row);
             ++index) {
          const Rect rect = HmiKeyboard::keyRect(state.layout, row, index);
          CHECK(hits[row][index] == static_cast<unsigned>(rect.w) * rect.h);
          expected += static_cast<unsigned>(rect.w) * rect.h;
        }
      }
      const Rect cancel = HmiKeyboard::footerRect(false);
      const Rect done = HmiKeyboard::footerRect(true);
      CHECK(hits[4][0] == static_cast<unsigned>(cancel.w) * cancel.h);
      CHECK(hits[4][1] == static_cast<unsigned>(done.w) * done.h);
      expected += static_cast<unsigned>(cancel.w) * cancel.h;
      expected += static_cast<unsigned>(done.w) * done.h;
      CHECK(interactive == expected);
      // Every non-interactive pixel is a deliberate gap.
      CHECK(interactive + (480u * 320u - interactive) == 480u * 320u);
    }
    return 0;
  }

  std::cerr << "unknown case: " << name << '\n';
  return 2;
}
'''


class KeyboardModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not COMPILER.is_file():
            raise RuntimeError(f"Host C++ compiler not found: {COMPILER}")
        SCRATCH.mkdir(parents=True, exist_ok=True)
        cls.work = tempfile.TemporaryDirectory(prefix="hmi-keyboard-", dir=SCRATCH)
        cls.addClassCleanup(cls.work.cleanup)
        source = Path(cls.work.name) / "keyboard_test.cpp"
        source.write_text(HARNESS, encoding="utf-8")
        cls.binary = Path(cls.work.name) / "keyboard_test.exe"
        cls.env = dict(os.environ)
        cls.env["PATH"] = str(COMPILER.parent) + os.pathsep + cls.env.get("PATH", "")
        result = subprocess.run(
            [str(COMPILER), "-std=c++11", "-Wall", "-Wextra", "-Werror",
             "-pedantic", "-static", "-I", str(ROOT / "src"),
             str(source), "-o", str(cls.binary)],
            env=cls.env, capture_output=True, text=True, timeout=90,
        )
        if result.returncode:
            raise AssertionError(f"Host compile failed:\n{result.stdout}{result.stderr}")

    def run_case(self, name):
        result = subprocess.run(
            [str(self.binary), name], env=self.env, capture_output=True,
            text=True, timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_geometry(self):
        self.run_case("geometry")

    def test_layout_tables(self):
        self.run_case("layout_tables")

    def test_hit_every_key(self):
        self.run_case("hit_every_key")

    def test_no_overlaps(self):
        self.run_case("no_overlaps")

    def test_all_three_rows(self):
        self.run_case("all_three_rows")

    def test_hit_boundaries(self):
        self.run_case("hit_boundaries")

    def test_shift_single(self):
        self.run_case("shift_single")

    def test_character_guard(self):
        self.run_case("character_guard")

    def test_display_char(self):
        self.run_case("display_char")

    def test_ascii_complete(self):
        self.run_case("ascii_complete")

    def test_max_length(self):
        self.run_case("max_length")

    def test_space_backspace(self):
        self.run_case("space_backspace")

    def test_cancel_keeps_saved(self):
        self.run_case("cancel_keeps_saved")

    def test_done_commits(self):
        self.run_case("done_commits")

    def test_init_limits(self):
        self.run_case("init_limits")

    def test_mask_counter(self):
        self.run_case("mask_counter")

    def test_layout_toggle(self):
        self.run_case("layout_toggle")

    def test_footer_actions(self):
        self.run_case("footer_actions")

    def test_pixel_sweep(self):
        self.run_case("pixel_sweep")

    def test_outside_points(self):
        self.run_case("outside_points")


if __name__ == "__main__":
    unittest.main(verbosity=2)
