"""Native tests for the pure one-address-per-role sensor mapping rules."""
from pathlib import Path
import os, shutil, subprocess, tempfile, unittest
ROOT = Path(__file__).resolve().parents[1]


class SensorCalibrationLogicTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('g++') or str(
            Path.home() / '.platformio/packages/toolchain-gccmingw32/bin/g++.exe')
        scratch = Path.home() / 'AppData/Local/hermes/cache/scratch'
        scratch.mkdir(parents=True, exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(prefix='sensor-cal-', dir=scratch)
        cls.addClassCleanup(cls.tmp.cleanup)
        work = Path(cls.tmp.name)
        cpp = work / 'sensor_cal.cpp'
        cls.exe = work / 'sensor_cal.exe'
        cpp.write_text(r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "src/control/sensor_calibration_logic.h"

static void address(uint8_t out[8], uint8_t tail) {
  std::memset(out, 0, 8);
  out[0] = 0x28;
  out[7] = tail;
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  int scenario = std::atoi(argv[1]);
  uint8_t assigned[7][8] = {};
  uint8_t discovered[3][8] = {};
  address(discovered[0], 1);
  address(discovered[1], 2);
  address(discovered[2], 3);

  if (scenario == 1) {
    // Reassigning a used address clears the previous role.
    SensorCalibrationLogic::assignAddress(assigned, 0, discovered[0]);
    SensorCalibrationLogic::assignAddress(assigned, 3, discovered[0]);
    std::printf("%d %d\n",
                SensorCalibrationLogic::isZeroAddress(assigned[0]) ? 1 : 0,
                SensorCalibrationLogic::roleOwner(assigned, discovered[0], 3));
    return 0;
  }
  if (scenario == 2) {
    // Auto-assign fills empty roles in order and skips an occupied address.
    SensorCalibrationLogic::assignAddress(assigned, 5, discovered[1]);
    const uint8_t changed = SensorCalibrationLogic::autoAssign(
        assigned, discovered, 3);
    std::printf("%u %d %d %d\n", changed,
                SensorCalibrationLogic::isZeroAddress(assigned[0]) ? 1 : 0,
                SensorCalibrationLogic::addressesEqual(assigned[0], discovered[0]) ? 1 : 0,
                SensorCalibrationLogic::addressesEqual(assigned[1], discovered[2]) ? 1 : 0);
    return 0;
  }
  if (scenario == 3) {
    // A zero address is never accepted, and an empty scan changes nothing.
    uint8_t zero[8] = {};
    std::memset(discovered[0], 0, 8);
    std::printf("%d %u\n",
                SensorCalibrationLogic::assignAddress(assigned, 0, zero) ? 1 : 0,
                SensorCalibrationLogic::autoAssign(assigned, discovered, 1));
    return 0;
  }
  if (scenario == 4) {
    // Clearing a role leaves other roles intact.
    SensorCalibrationLogic::assignAddress(assigned, 2, discovered[0]);
    SensorCalibrationLogic::assignAddress(assigned, 4, discovered[1]);
    SensorCalibrationLogic::clearAddress(assigned, 2);
    std::printf("%d %d\n",
                SensorCalibrationLogic::isZeroAddress(assigned[2]) ? 1 : 0,
                SensorCalibrationLogic::isZeroAddress(assigned[4]) ? 0 : 1);
    return 0;
  }
  return 2;
}
''', encoding='utf-8')
        env = dict(os.environ)
        env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
        result = subprocess.run(
            [compiler, '-std=c++11', '-static', '-I', str(ROOT), str(cpp), '-o', str(cls.exe)],
            env=env, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def run_case(self, scenario):
        return tuple(map(int, subprocess.check_output(
            [str(self.exe), str(scenario)], text=True).split()))

    def test_reassigning_used_address_clears_previous_role(self):
        self.assertEqual((1, -1), self.run_case(1))

    def test_auto_assign_keeps_occupied_address_and_fills_next(self):
        self.assertEqual((2, 0, 1, 1), self.run_case(2))

    def test_zero_address_is_rejected(self):
        self.assertEqual((0, 0), self.run_case(3))

    def test_clear_touches_only_the_requested_role(self):
        self.assertEqual((1, 1), self.run_case(4))


if __name__ == '__main__':
    unittest.main(verbosity=2)
