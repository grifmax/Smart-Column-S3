"""Compile real pump output/conversion functions with LEDC stubs; no device I/O."""
from pathlib import Path
import math
import os
import shutil
import subprocess
import tempfile
import unittest
from test_heater_triac_isr import extract_function

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = Path.home() / 'AppData/Local/hermes/cache/scratch'


class PumpStepOutputRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('g++') or str(Path.home() / '.platformio/packages/toolchain-gccmingw32/bin/g++.exe')
        SCRATCH.mkdir(parents=True, exist_ok=True)
        cls.work = tempfile.TemporaryDirectory(prefix='pump-output-', dir=SCRATCH)
        cls.addClassCleanup(cls.work.cleanup)
        work = Path(cls.work.name)
        source = (ROOT/'src/drivers/pump.cpp').read_text(encoding='utf-8')
        # Copy the driver's real file-scope state (no FreeRTOS/task bodies).
        state = source[source.index('static float mlPerRevolution'):source.index('static bool isDemoHardwareSuppressed()')]
        signatures = [
            'static void updateTotalsFromExactSteps()',
            'static float mlPerHourToStepsPerSec(float mlPerHour)',
            'static float stepsPerSecToMlPerHour(float stepsPerSec)',
            'static void stopPumpPulsesLocked()',
            'static void applyPumpPulsesLocked()',
            'static void syncMotionLocked(uint32_t nowUs)',
        ]
        if 'static uint32_t configuredStepsPerRevolution()' in source:
            signatures.insert(0, 'static uint32_t configuredStepsPerRevolution()')
        functions = '\n'.join(extract_function(source, sig) for sig in signatures)
        harness = r'''
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define DEFAULT_PUMP_ML_PER_REV 0.5f
#define PUMP_STEPS_PER_REV 200
#define PUMP_MICROSTEPS 16
#define PUMP_MAX_SPEED 4000
#define LEDC_CHANNEL_PUMP 2
#define LOG_E(...) ((void)0)
#define LOG_W(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
using TickType_t = unsigned;
using TaskHandle_t = void*;
using SemaphoreHandle_t = void*;
struct Settings { struct Calibration { unsigned stepsPerRevolution=200; unsigned microsteps=32; } pumpCal; };
static Settings g_settings;
static unsigned configs=0, toneCalls=0, writes=0, actualHz=0, duty=0;
static unsigned currentResolution=8;
static bool failHardware=false;
static unsigned configure(unsigned frequency, unsigned bits) {
    ++configs;
    // Match ESP32-S3 XTAL clock divider bounds in the installed LEDC driver.
    const double divider=40000000.0 / (frequency * (1U<<bits));
    if (failHardware || divider < 1.0 || divider >= 1024.0) return 0;
    currentResolution=bits;
    return actualHz=frequency;
}
unsigned ledcChangeFrequency(unsigned, unsigned hz, unsigned bits) { return configure(hz,bits); }
unsigned ledcWriteTone(unsigned, unsigned hz) {
    ++toneCalls;
    if (!hz) { duty=0; return 0; }
    const unsigned result=configure(hz,10);
    if (result) duty=511;
    return result;
}
void ledcWrite(unsigned, unsigned value) { ++writes; duty=value; }
'''
        harness += state + '\n' + functions
        harness += r'''
int main(int argc,char** argv) {
    if (argc<2) return 2;
    mlPerRevolution=3.790244f;
    g_pumpPwmReady=true;
    switch(std::atoi(argv[1])) {
    case 1: { // A stable low speed must keep a single uninterrupted timer setup.
        appliedStepsPerSec=std::atof(argv[2]);
        for(int i=0;i<100;++i) applyPumpPulsesLocked();
        std::printf("%u %u %u %u\n",configs,toneCalls,actualHz,duty);
        break;
    }
    case 2: { // Count output, not a desired rate, if peripheral config fails.
        appliedStepsPerSec=35;
        failHardware=true;
        applyPumpPulsesLocked();
        pumpLastMotionUpdateUs=1000000;
        syncMotionLocked(2000000);
        std::printf("%.6f\n",totalStepsExact);
        break;
    }
    case 3: {
        std::printf("%.6f %.6f\n",mlPerHourToStepsPerSec(150),stepsPerSecToMlPerHour(70.35607f));
        break;
    }
    case 4: {
        appliedStepsPerSec=35;
        applyPumpPulsesLocked();
        appliedStepsPerSec=70;
        applyPumpPulsesLocked();
        appliedStepsPerSec=0;
        applyPumpPulsesLocked();
        std::printf("%u %u %u\n",configs,toneCalls,duty);
        break;
    }
    case 5: {
        totalStepsExact=6400;
        updateTotalsFromExactSteps();
        std::printf("%.6f\n",totalVolumeMl);
        break;
    }
    case 6: {
        appliedStepsPerSec=35.4f;
        applyPumpPulsesLocked();
        pumpLastMotionUpdateUs=1000000;
        syncMotionLocked(2000000);
        std::printf("%.6f\n",totalStepsExact);
        break;
    }
    }
}
'''
        cpp=work/'pump.cpp'; cls.binary=work/'pump.exe'
        cpp.write_text(harness,encoding='utf-8')
        env=dict(os.environ); env['PATH']=str(Path(compiler).parent)+os.pathsep+env.get('PATH','')
        result=subprocess.run([compiler,'-std=c++11','-static',str(cpp),'-o',str(cls.binary)],env=env,capture_output=True,text=True)
        if result.returncode: raise RuntimeError(str(result.returncode)+' '+result.stdout+result.stderr)

    def trace(self,*args):
        result=subprocess.run([str(self.binary),*map(str,args)],check=True,capture_output=True,text=True)
        return list(map(float,result.stdout.split()))

    def test_stable_step_generator_is_not_reset_every_task_tick(self):
        configs,tones,hz,duty=self.trace(1,70)
        self.assertEqual(1,configs)
        self.assertEqual(0,tones)
        self.assertEqual(70,hz)
        self.assertGreater(duty,0)

    def test_low_frequency_is_supported(self):
        for hz in (5,35,70,4000):
            with self.subTest(hz=hz):
                configs,tones,actual,duty=self.trace(1,hz)
                self.assertEqual(hz,actual)
                self.assertEqual(1,configs)
                self.assertGreater(duty,0)

    def test_failed_output_does_not_invent_pumped_volume(self):
        self.assertEqual([0],self.trace(2))

    def test_runtime_32_microsteps_are_used_in_rate_conversion(self):
        steps,mlh=self.trace(3)
        self.assertAlmostEqual(150/3.790244/3600*6400,steps,places=3)
        self.assertAlmostEqual(150,mlh,places=2)

    def test_rate_changes_and_stop_do_not_use_tone(self):
        configs,tones,duty=self.trace(4)
        self.assertEqual((2,0,0),(configs,tones,duty))

    def test_one_configured_revolution_counts_one_calibrated_volume(self):
        self.assertAlmostEqual(3.790244,self.trace(5)[0],places=5)

    def test_volume_tracks_real_quantized_output_frequency(self):
        self.assertEqual([35],self.trace(6))


if __name__=='__main__':
    unittest.main(verbosity=2)
