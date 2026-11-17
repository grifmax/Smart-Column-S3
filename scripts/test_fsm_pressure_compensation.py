"""Characterize production FSM pressure compensation without hardware.

Run: python scripts/test_fsm_pressure_compensation.py
The tested function bodies and constants are extracted from firmware sources.
The pressure/state structs below are a minimal host stub, not hardware models.
"""
from pathlib import Path
import math
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = Path.home() / 'AppData/Local/hermes/cache/scratch'


def extract_function(source, signature):
    if source.count(signature) != 1:
        raise ValueError(f'Expected one function signature: {signature}')
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        if end >= len(source):
            raise ValueError(f'Unbalanced function: {signature}')
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


class FsmPressureCompensationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('g++') or str(Path.home() / '.platformio/packages/toolchain-gccmingw32/bin/g++.exe')
        if not Path(compiler).is_file():
            raise RuntimeError('Native C++ compiler not found')
        SCRATCH.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix='fsm-pressure-', dir=SCRATCH)
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        source = (ROOT / 'src/control/fsm_utils.cpp').read_text(encoding='utf8')
        config = (ROOT / 'src/config.h').read_text(encoding='utf8')
        constants = []
        for name in ('RECT_PRESSURE_STD_HPA', 'RECT_TEMP_COMP_C_PER_HPA',
                     'RECT_CUBE_BODY_TO_TAILS_BASE_C', 'RECT_CUBE_FINISH_BASE_C'):
            matches = re.findall(r'^#define\s+' + name + r'\s+[^\r\n]+', config, re.M)
            if len(matches) != 1:
                raise ValueError(f'Expected one definition of {name}')
            constants.append(matches[0])
        functions = '\n'.join(extract_function(source, sig) for sig in (
            'float getAtmosphereHpa(const SystemState& state)',
            'float pressureAdjustedCubeTemp(float baseTempC, const SystemState& state)',
        ))
        harness = r'''
#include <cmath>
#include <cstdio>
#include <limits>
struct SystemState { struct { bool ok; float atmosphere; } pressure; };
''' + '\n'.join(constants) + '\nnamespace FSM {\n' + functions + r'''
}
static int failures = 0;
void check(bool condition, const char* label) {
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); ++failures; }
}
bool close(float a, float b) { return std::fabs(a-b) < 0.0002f; }
int main() {
  SystemState s{};
  s.pressure.ok = true;
  const float stdhpa = RECT_PRESSURE_STD_HPA;
  const float k = RECT_TEMP_COMP_C_PER_HPA;
  for (float base : {RECT_CUBE_BODY_TO_TAILS_BASE_C, RECT_CUBE_FINISH_BASE_C}) {
    s.pressure.atmosphere = stdhpa;
    check(close(FSM::pressureAdjustedCubeTemp(base,s),base),"standard");
    s.pressure.atmosphere = 1000.0f;
    check(close(FSM::pressureAdjustedCubeTemp(base,s),base+(1000.0f-stdhpa)*k),"lower");
    s.pressure.atmosphere = 1050.0f;
    check(close(FSM::pressureAdjustedCubeTemp(base,s),base+(1050.0f-stdhpa)*k),"higher");
    s.pressure.ok = false;
    check(close(FSM::pressureAdjustedCubeTemp(base,s),base),"invalid sensor");
    s.pressure.ok = true;
  }
  for (float value : {850.0f, 1100.0f, 849.0f, 1101.0f,
                       std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity()}) {
    s.pressure.atmosphere=value;
    check(close(FSM::getAtmosphereHpa(s),stdhpa),"boundary/non-finite fallback");
  }
  for (float value : {std::nextafter(850.0f,1100.0f), std::nextafter(1100.0f,850.0f)}) {
    s.pressure.atmosphere=value;
    check(FSM::getAtmosphereHpa(s)==value,"just inside boundary accepted");
  }
  for (float value : {std::nextafter(850.0f,0.0f), std::nextafter(1100.0f,2000.0f)}) {
    s.pressure.atmosphere=value;
    check(close(FSM::getAtmosphereHpa(s),stdhpa),"just outside boundary fallback");
  }
  if (failures) return 1;
  std::puts("PASS: actual FSM compensation bodies; 19 checks");
  return 0;
}
'''
        harness = '#include <initializer_list>\n' + harness
        cpp = work / 'fsm_pressure.cpp'
        cls.binary = work / 'fsm_pressure.exe'
        cpp.write_text(harness, encoding='utf8')
        env = dict(os.environ)
        env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
        result = subprocess.run([compiler, '-std=c++11', '-static', str(cpp), '-o', str(cls.binary)],
                                capture_output=True, text=True, env=env)
        if result.returncode:
            raise RuntimeError(f'Compilation failed ({result.returncode}): {result.stdout}{result.stderr}')

    def test_actual_fsm_implementation(self):
        result = subprocess.run([str(self.binary)], capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn('PASS:', result.stdout)
        print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main(verbosity=2)
