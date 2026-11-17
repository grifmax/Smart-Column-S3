"""Host check of the live-chart ring buffer reset semantics.

Run: python scripts/test_live_chart_reset.py
The struct, reset and iteration code are extracted from
src/live_chart_history.cpp; only the point type is made comparable for the test.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = Path.home() / 'AppData/Local/hermes/cache/scratch'


def extract(source, signature):
    if source.count(signature) != 1:
        raise ValueError(f'expected one {signature!r}')
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        if end >= len(source):
            raise ValueError(f'unbalanced {signature!r}')
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


class RingResetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('g++') or str(Path.home() / '.platformio/packages/toolchain-gccmingw32/bin/g++.exe')
        if not Path(compiler).is_file():
            raise RuntimeError('Native C++ compiler not found')
        SCRATCH.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix='ring-reset-', dir=SCRATCH)
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        source = (ROOT / 'src/live_chart_history.cpp').read_text(encoding='utf8')
        parts = [extract(source, sig) + ';' for sig in (
            'template <size_t Capacity>\nstruct LiveChartRingBuffer {',
        )]
        parts += [extract(source, sig) for sig in (
            'template <size_t Capacity>\nvoid appendPoint(',
            'template <size_t Capacity, typename Fn>\nvoid forEachPointOldestFirst(',
        )]
        harness = r'''
#include <cstddef>
#include <cstdio>
#include <vector>
struct LiveChartPoint {
  int id = -1;
  LiveChartPoint() : id(-1) {}
  explicit LiveChartPoint(int value) : id(value) {}
};
''' + '\n'.join(parts) + r'''
int main() {
  LiveChartRingBuffer<4> buffer;
  for (int i = 0; i < 6; ++i) appendPoint(buffer, LiveChartPoint{i});
  buffer.reset();
  if (buffer.count != 0) { std::puts("FAIL: count after reset"); return 1; }
  int seen = 0;
  forEachPointOldestFirst(buffer, [&](const LiveChartPoint&) { ++seen; });
  if (seen != 0) { std::puts("FAIL: stale points visible after reset"); return 1; }
  std::vector<int> after;
  for (int i = 100; i < 103; ++i) appendPoint(buffer, LiveChartPoint{i});
  forEachPointOldestFirst(buffer, [&](const LiveChartPoint& p) { after.push_back(p.id); });
  if (after != std::vector<int>({100,101,102})) { std::puts("FAIL: order after reset"); return 1; }
  std::puts("PASS: counters-only reset hides stale slots and preserves order");
  return 0;
}
'''
        cpp = work / 'ring.cpp'
        cls.binary = work / 'ring.exe'
        cpp.write_text(harness, encoding='utf8')
        env = dict(os.environ)
        env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
        result = subprocess.run([compiler, '-std=c++11', '-static', str(cpp), '-o', str(cls.binary)],
                                capture_output=True, text=True, env=env)
        if result.returncode:
            raise RuntimeError(f'Compilation failed ({result.returncode}): {result.stdout}{result.stderr}')

    def test_extracted_ring_reset(self):
        result = subprocess.run([str(self.binary)], capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main(verbosity=2)
