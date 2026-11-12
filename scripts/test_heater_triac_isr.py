"""Host regression for the actual TRIAC zero-cross ISR, with hardware I/O stubs.

No device calls are made. Compile the function extracted from heater.cpp and
check that every nonzero target schedules the configured delay (including 100%).
Run: python scripts/test_heater_triac_isr.py
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = Path.home() / 'AppData/Local/hermes/cache/scratch'


def extract_function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


class TriacZeroCrossRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('g++')
        if not compiler:
            compiler = str(Path.home() / '.platformio/packages/toolchain-gccmingw32/bin/g++.exe')
        if not Path(compiler).exists():
            raise RuntimeError('Native C++ compiler not found')
        SCRATCH.mkdir(parents=True, exist_ok=True)
        cls.work = tempfile.TemporaryDirectory(prefix='triac-isr-', dir=SCRATCH)
        cls.addClassCleanup(cls.work.cleanup)
        work = Path(cls.work.name)
        source = (ROOT / 'src/drivers/heater.cpp').read_text(encoding='utf-8')
        config = (ROOT / 'src/config.h').read_text(encoding='utf-8')
        constants = []
        for name in ('TRIAC_MIN_ALPHA_US', 'TRIAC_MAX_ALPHA_US', 'TRIAC_ZERO_CROSS_LOCKOUT_US'):
            constants.append(next(line for line in config.splitlines() if line.startswith('#define ' + name + ' ')))
        function = extract_function(source, 'static void IRAM_ATTR zero_cross_isr_handler()')
        conversions = '\n'.join(extract_function(source, signature) for signature in (
            'uint8_t wattsToPercent(uint16_t watts)',
            'uint16_t calculateTriacDelayForWatts(uint16_t watts, float currentVoltage)',
        ))
        harness = r'''
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#define PI 3.14159265358979323846f
#define DEFAULT_HEATER_POWER_W 3500
struct Settings { struct Equipment { uint16_t heaterPowerW = 3500; } equipment; };
static Settings g_settings;
#define IRAM_ATTR
#define PIN_TRIAC 1
using gpio_num_t = int;
struct hw_timer_t {};
static hw_timer_t timer_instance;
static hw_timer_t* triac_timer = &timer_instance;
static uint8_t currentPower = 0;
static uint16_t triac_delay_us = 9500;
static uint32_t last_zero_cross_us = 0, zero_cross_count = 0;
static uint32_t now_us = 10000;
static unsigned immediate_pulses = 0, timer_alarms = 0;
static uint16_t scheduled_delay = 0;
uint32_t micros() { return now_us; }
void gpio_set_level(gpio_num_t, int) {}
void fireTriacPulse() { ++immediate_pulses; }
void timerAlarmDisable(hw_timer_t*) {}
void timerWrite(hw_timer_t*, uint64_t) {}
void timerAlarmWrite(hw_timer_t*, uint64_t delay, bool) { scheduled_delay = delay; }
void timerAlarmEnable(hw_timer_t*) { ++timer_alarms; }
'''
        harness += '\n'.join(constants) + '\n' + conversions + '\n' + function
        harness += r'''
int main(int argc, char** argv) {
    if (argc == 4) {
        const uint16_t watts = static_cast<uint16_t>(std::atoi(argv[1]));
        currentPower = wattsToPercent(watts);
        triac_delay_us = calculateTriacDelayForWatts(watts, std::atof(argv[2]));
        zero_cross_isr_handler();
        std::printf("%u %u %u %u\n", immediate_pulses, timer_alarms,
                    scheduled_delay, static_cast<unsigned>(zero_cross_count));
        return 0;
    }
    if (argc != 5) return 2;
    currentPower = static_cast<uint8_t>(std::atoi(argv[1]));
    triac_delay_us = static_cast<uint16_t>(std::atoi(argv[2]));
    if (std::atoi(argv[3])) last_zero_cross_us = now_us - 100;
    if (std::atoi(argv[4])) triac_timer = nullptr;
    zero_cross_isr_handler();
    std::printf("%u %u %u %u\n", immediate_pulses, timer_alarms,
                scheduled_delay, static_cast<unsigned>(zero_cross_count));
}
'''
        cpp = work / 'isr.cpp'
        cls.binary = work / 'isr.exe'
        cpp.write_text(harness, encoding='utf-8')
        env = dict(os.environ)
        env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
        result = subprocess.run([compiler, '-std=c++11', '-static', str(cpp), '-o', str(cls.binary)], capture_output=True, text=True, env=env)
        if result.returncode:
            raise RuntimeError(f'Compiler exit {result.returncode}: ' + result.stdout + result.stderr)
        cls.minimum = int(next(x for x in constants if 'TRIAC_MIN_ALPHA_US ' in x).split()[2])
        cls.maximum = int(next(x for x in constants if 'TRIAC_MAX_ALPHA_US ' in x).split()[2])

    def trace(self, percent, delay, locked_out=0, missing_timer=0):
        result = subprocess.run([str(self.binary), str(percent), str(delay), str(locked_out), str(missing_timer)], check=True, capture_output=True, text=True)
        return tuple(map(int, result.stdout.split()))

    def watts_trace(self, watts, voltage):
        result = subprocess.run([str(self.binary), str(watts), str(voltage), 'watts'], check=True, capture_output=True, text=True)
        return tuple(map(int, result.stdout.split()))

    def test_3500_w_always_schedules_minimum(self):
        self.assertEqual((0, 1, self.minimum, 1), self.watts_trace(3500, 230))

    def test_3499_w_at_nominal_voltage_uses_nonzero_delay(self):
        immediate, alarms, delay, count = self.watts_trace(3499, 230)
        self.assertEqual((0, 1, 1), (immediate, alarms, count))
        self.assertGreater(delay, self.minimum)

    def test_3499_w_at_low_voltage_uses_minimum_not_immediate(self):
        self.assertEqual((0, 1, self.minimum, 1), self.watts_trace(3499, 220))

    def test_full_power_observes_minimum_delay(self):
        self.assertEqual((0, 1, self.minimum, 1), self.trace(100, self.minimum),
                         '100% must arm the timer, not fire at the detector edge')

    def test_rounded_3499_w_saturation_observes_same_delay(self):
        # 3499/3500 rounds to 100%; low mains voltage can also saturate delay.
        self.assertEqual((0, 1, self.minimum, 1), self.trace(100, self.minimum))

    def test_partial_power_keeps_timer_path(self):
        self.assertEqual((0, 1, 500, 1), self.trace(99, 500))

    def test_zero_power_never_fires(self):
        self.assertEqual((0, 0, 0, 1), self.trace(0, self.maximum))

    def test_off_delay_never_fires(self):
        self.assertEqual((0, 0, 0, 1), self.trace(100, self.maximum))

    def test_duplicate_detector_edge_is_ignored(self):
        self.assertEqual((0, 0, 0, 0), self.trace(100, self.minimum, locked_out=1))

    def test_missing_timer_never_fires(self):
        self.assertEqual((0, 0, 0, 1), self.trace(100, self.minimum, missing_timer=1))


if __name__ == '__main__':
    unittest.main(verbosity=2)
