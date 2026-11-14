#!/usr/bin/env python3
"""Standalone host tests for ui/hmi_touch_logic.h; never runs on import.

Run: python scripts/test_hmi_touch.py
Uses the PlatformIO MinGW compiler directly (no pio, firmware, or device I/O).
All generated sources and binaries live in the Hermes scratch directory.
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
#include "ui/hmi_touch_logic.h"
#include <iostream>
#include <string>

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << "line " << __LINE__ << ": " << #condition << '\n'; \
  return 1; } } while (false)

static HmiTouch::Event tick(HmiTouch::State& state, uint32_t now,
                            bool pressed, int16_t x = 100, int16_t y = 100) {
  const HmiTouch::Sample sample = {pressed, x, y};
  return HmiTouch::update(state, sample, now);
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string name(argv[1]);
  HmiTouch::State state;
  if (name == "quick_tap") {
    HmiTouch::Event event = tick(state, 0, false);
    CHECK(!event.pressed && !event.released && !event.tapped);
    event = tick(state, 25, true, 120, 80);
    CHECK(event.pressed && !event.released && !event.tapped);
    CHECK(event.x == 120 && event.y == 80);
    event = tick(state, 50, false, -1, -1);
    CHECK(event.pressed && !event.released && !event.tapped);
    CHECK(event.x == 120 && event.y == 80);
    event = tick(state, 89, false);
    CHECK(event.pressed && !event.released && !event.tapped);
    event = tick(state, 90, false);
    CHECK(!event.pressed && event.released && event.tapped);
    CHECK(event.x == 120 && event.y == 80);
    event = tick(state, 115, false);
    CHECK(!event.pressed && !event.released && !event.tapped);
    return 0;
  }
  if (name == "hold_bounds") {
    const uint32_t holds[] = {0, 1, 19, 20, 25, 1499, 1500, 1501, 5000};
    for (unsigned i = 0; i < sizeof(holds) / sizeof(holds[0]); ++i) {
      HmiTouch::State fresh;
      CHECK(tick(fresh, 100, true).pressed);
      CHECK(!tick(fresh, 100 + holds[i], false).released);
      const HmiTouch::Event event = tick(fresh, 140 + holds[i], false);
      CHECK(event.released && !event.pressed);
      CHECK(event.tapped == (holds[i] >= 20 && holds[i] <= 1500));
      CHECK(!tick(fresh, 150 + holds[i], false).tapped);
    }
    return 0;
  }
  if (name == "false_bounce") {
    for (uint32_t gap = 10; gap <= 30; gap += 10) {
      HmiTouch::State fresh;
      tick(fresh, 100, true);
      CHECK(!tick(fresh, 125, false).released);
      CHECK(!tick(fresh, 125 + gap, true, 103, 102).released);
      CHECK(!tick(fresh, 180, false).released);
      const HmiTouch::Event event = tick(fresh, 220, false);
      CHECK(event.released && event.tapped && !event.pressed);
      CHECK(event.x == 103 && event.y == 102);
      CHECK(!tick(fresh, 245, false).released);
    }
    return 0;
  }
  if (name == "excursion") {
    const int16_t offsets[][2] = {{0, 0}, {12, 12}, {13, 12}, {25, 0}, {-25, 0}};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
      HmiTouch::State fresh;
      tick(fresh, 100, true);
      tick(fresh, 125, true, 100 + offsets[i][0], 100 + offsets[i][1]);
      tick(fresh, 150, true); // returning to down position never restores a drag
      tick(fresh, 175, false);
      const HmiTouch::Event event = tick(fresh, 215, false);
      CHECK(event.released);
      CHECK(event.tapped == (i <= 1));
    }
    return 0;
  }
  if (name == "extreme_coordinates") {
    tick(state, 100, true, -32768, -32768);
    tick(state, 125, true, 32767, 32767);
    tick(state, 150, true, -32768, -32768);
    tick(state, 175, false);
    const HmiTouch::Event event = tick(state, 215, false);
    CHECK(event.released && !event.tapped);
    return 0;
  }
  if (name == "tap_gap") {
    const uint32_t gaps[] = {70, 100, 159, 160, 161, 200};
    for (unsigned i = 0; i < sizeof(gaps) / sizeof(gaps[0]); ++i) {
      HmiTouch::State fresh;
      tick(fresh, 0, true);
      tick(fresh, 25, false);
      CHECK(tick(fresh, 65, false).tapped);
      tick(fresh, gaps[i], true);
      tick(fresh, gaps[i] + 25, false);
      const HmiTouch::Event event = tick(fresh, gaps[i] + 65, false);
      CHECK(event.released);
      CHECK(event.tapped == (gaps[i] >= 160));
      // A rejected tap must not extend lockout of the next valid tap.
      tick(fresh, gaps[i] + 300, true);
      tick(fresh, gaps[i] + 325, false);
      CHECK(tick(fresh, gaps[i] + 365, false).tapped);
    }
    return 0;
  }
  if (name == "millis_wrap") {
    tick(state, 0xfffffff0u, true);
    tick(state, 9, false);
    CHECK(!tick(state, 48, false).released);
    CHECK(tick(state, 49, false).tapped);
    // Debounce itself also spans wrap.
    HmiTouch::State bounce;
    tick(bounce, 0xffffffb0u, true);
    tick(bounce, 0xfffffff0u, false);
    CHECK(!tick(bounce, 23, false).released);
    CHECK(tick(bounce, 24, false).tapped);
    // Accepted tap spacing spans wrap too.
    HmiTouch::State gaps;
    tick(gaps, 0xffffff80u, true);
    tick(gaps, 0xffffff99u, false);
    CHECK(tick(gaps, 0xffffffc1u, false).tapped);
    tick(gaps, 0x20u, true);
    tick(gaps, 0x39u, false);
    CHECK(tick(gaps, 0x61u, false).tapped);
    return 0;
  }
  if (name == "screen_guard") {
    HmiTouch::armScreenGuard(state, 100);
    tick(state, 379, true); // started in guard: remains blocked after expiry
    tick(state, 404, false);
    HmiTouch::Event event = tick(state, 444, false);
    CHECK(event.released && !event.tapped);
    tick(state, 500, true);
    tick(state, 525, false);
    CHECK(tick(state, 565, false).tapped);
    HmiTouch::State exact;
    HmiTouch::armScreenGuard(exact, 100);
    tick(exact, 380, true); // deadline is inclusive for re-enabling taps
    tick(exact, 405, false);
    CHECK(tick(exact, 445, false).tapped);
    HmiTouch::State midHold;
    tick(midHold, 100, true);
    HmiTouch::armScreenGuard(midHold, 125, 10);
    tick(midHold, 150, false);
    event = tick(midHold, 190, false);
    CHECK(event.released && !event.tapped);
    HmiTouch::State disabled;
    HmiTouch::armScreenGuard(disabled, 0, 0);
    tick(disabled, 0, true);
    tick(disabled, 25, false);
    CHECK(tick(disabled, 65, false).tapped);
    return 0;
  }
  if (name == "guard_wrap") {
    HmiTouch::armScreenGuard(state, 0xffffffd0u, 80); // deadline wraps to 32
    tick(state, 0xfffffff0u, true);
    tick(state, 9, false);
    HmiTouch::Event event = tick(state, 49, false);
    CHECK(event.released && !event.tapped);
    tick(state, 100, true);
    tick(state, 125, false);
    CHECK(tick(state, 165, false).tapped);
    HmiTouch::State zeroDeadline;
    HmiTouch::armScreenGuard(zeroDeadline, 0xfffffff0u, 16);
    tick(zeroDeadline, 0, true);
    tick(zeroDeadline, 25, false);
    CHECK(tick(zeroDeadline, 65, false).tapped);
    // Clear an expired guard on idle updates; never resurrect it half a wrap later.
    HmiTouch::State idle;
    HmiTouch::armScreenGuard(idle, 0, 40);
    tick(idle, 40, false);
    tick(idle, 0x80000040u, true);
    tick(idle, 0x80000059u, false);
    CHECK(tick(idle, 0x80000081u, false).tapped);
    return 0;
  }
  if (name == "release_then_new_press") {
    tick(state, 0, true, 100, 100);
    tick(state, 25, false);
    // No idle sample at 65; the next press must not erase the old release.
    HmiTouch::Event event = tick(state, 200, true, 200, 150);
    CHECK(event.pressed && event.released && event.tapped);
    CHECK(event.x == 100 && event.y == 100);
    event = tick(state, 225, false);
    CHECK(event.pressed && !event.released && !event.tapped);
    event = tick(state, 265, false);
    CHECK(event.released && !event.tapped); // emission gap 65 ms < 160
    CHECK(event.x == 200 && event.y == 150);
    CHECK(!tick(state, 290, false).released);
    return 0;
  }
  if (name == "bounce_does_not_restart_hold") {
    tick(state, 100, true);
    tick(state, 1580, false);
    tick(state, 1600, true); // 20-ms false bounce, not a fresh contact
    tick(state, 1625, false);
    const HmiTouch::Event event = tick(state, 1665, false);
    CHECK(event.released && !event.tapped);
    return 0;
  }
  if (name == "held_no_repeat") {
    for (uint32_t now = 0; now <= 2000; now += 25) {
      const HmiTouch::Event event = tick(state, now, true);
      CHECK(event.pressed && !event.released && !event.tapped);
    }
    tick(state, 2025, false);
    const HmiTouch::Event event = tick(state, 2065, false);
    CHECK(event.released && !event.tapped && !event.pressed);
    return 0;
  }
  if (name == "twenty_five_ms_ticks") {
    tick(state, 0, true);
    tick(state, 25, false);
    CHECK(!tick(state, 50, false).released);
    CHECK(tick(state, 75, false).tapped);
    return 0;
  }
  if (name == "bounce_excursion") {
    tick(state, 0, true);
    tick(state, 25, false);
    tick(state, 50, true, 130, 100);
    tick(state, 75, true);
    tick(state, 100, false);
    CHECK(!tick(state, 140, false).tapped);
    return 0;
  }
  if (name == "drag_resets_next_contact") {
    tick(state, 0, true);
    tick(state, 25, true, 150, 100);
    tick(state, 50, false);
    CHECK(!tick(state, 90, false).tapped);
    tick(state, 125, true);
    tick(state, 150, false);
    CHECK(tick(state, 190, false).tapped);
    return 0;
  }
  if (name == "independent_states") {
    HmiTouch::State other;
    HmiTouch::armScreenGuard(state, 0);
    tick(state, 0, true);
    tick(other, 0, true);
    tick(state, 25, false);
    tick(other, 25, false);
    CHECK(!tick(state, 65, false).tapped);
    CHECK(tick(other, 65, false).tapped);
    return 0;
  }
  if (name == "no_hidden_gestures") {
    for (uint32_t now = 0; now <= 1000; now += 25) {
      const HmiTouch::Event event = tick(state, now, false, 32767, -32768);
      CHECK(!event.pressed && !event.released && !event.tapped);
    }
    return 0;
  }
  std::cerr << "unknown case: " << name << '\n';
  return 2;
}
'''


class TouchLogicTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not COMPILER.is_file():
            raise RuntimeError(f"Host C++ compiler not found: {COMPILER}")
        SCRATCH.mkdir(parents=True, exist_ok=True)
        cls.work = tempfile.TemporaryDirectory(prefix="hmi-touch-", dir=SCRATCH)
        cls.addClassCleanup(cls.work.cleanup)
        source = Path(cls.work.name) / "touch_test.cpp"
        source.write_text(HARNESS, encoding="utf-8")
        cls.binary = Path(cls.work.name) / "touch_test.exe"
        cls.env = dict(os.environ)
        cls.env["PATH"] = str(COMPILER.parent) + os.pathsep + cls.env.get("PATH", "")
        result = subprocess.run(
            [str(COMPILER), "-std=c++11", "-Wall", "-Wextra", "-Werror",
             "-pedantic", "-static", "-I", str(ROOT / "src"),
             str(source), "-o", str(cls.binary)],
            env=cls.env, capture_output=True, text=True, timeout=60,
        )
        if result.returncode:
            raise AssertionError(f"Host compile failed:\n{result.stdout}{result.stderr}")

    def run_case(self, name):
        result = subprocess.run(
            [str(self.binary), name], env=self.env, capture_output=True,
            text=True, timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_quick_tap(self):
        self.run_case("quick_tap")

    def test_hold_bounds(self):
        self.run_case("hold_bounds")

    def test_false_bounce(self):
        self.run_case("false_bounce")

    def test_excursion(self):
        self.run_case("excursion")

    def test_extreme_coordinates(self):
        self.run_case("extreme_coordinates")

    def test_tap_gap(self):
        self.run_case("tap_gap")

    def test_millis_wrap(self):
        self.run_case("millis_wrap")

    def test_screen_guard(self):
        self.run_case("screen_guard")

    def test_guard_wrap(self):
        self.run_case("guard_wrap")

    def test_release_then_new_press(self):
        self.run_case("release_then_new_press")

    def test_bounce_does_not_restart_hold(self):
        self.run_case("bounce_does_not_restart_hold")

    def test_held_no_repeat(self):
        self.run_case("held_no_repeat")

    def test_twenty_five_ms_ticks(self):
        self.run_case("twenty_five_ms_ticks")

    def test_bounce_excursion(self):
        self.run_case("bounce_excursion")

    def test_drag_resets_next_contact(self):
        self.run_case("drag_resets_next_contact")

    def test_independent_states(self):
        self.run_case("independent_states")

    def test_no_hidden_gestures(self):
        self.run_case("no_hidden_gestures")


if __name__ == "__main__":
    unittest.main(verbosity=2)
