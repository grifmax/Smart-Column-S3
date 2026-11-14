"""Guard the motor-step / microstep pair used by the pump and its calibration."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class PumpStepsLogic(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  code=r'''
#include <cstdio>
#include <cstdlib>
#include "src/control/pump_steps_logic.h"
int main(int argc,char**argv){
  const unsigned steps=std::strtoul(argv[1],nullptr,10);
  const unsigned micro=std::strtoul(argv[2],nullptr,10);
  std::printf("%u\n",PumpSteps::pulsesPerRevolution(steps,micro,200,16));
}
'''
  compiler=shutil.which('g++') or str(Path.home()/'.platformio/packages/toolchain-gccmingw32/bin/g++.exe')
  cls.tmp=tempfile.TemporaryDirectory(prefix='pump-steps-',dir=Path.home()/'AppData/Local/hermes/cache/scratch');cls.addClassCleanup(cls.tmp.cleanup)
  work=Path(cls.tmp.name);cpp=work/'steps.cpp';cls.exe=work/'steps.exe';cpp.write_text(code,encoding='utf-8')
  env=dict(os.environ);env['PATH']=str(Path(compiler).parent)+os.pathsep+env.get('PATH','')
  r=subprocess.run([compiler,'-std=c++11','-static','-I',str(ROOT),str(cpp),'-o',str(cls.exe)],env=env,capture_output=True,text=True)
  if r.returncode:raise RuntimeError(r.stderr)
 def pulses(self,steps,micro):return int(subprocess.check_output([str(self.exe),str(steps),str(micro)],text=True))
 def test_nema17_at_16_microsteps(self):self.assertEqual(3200,self.pulses(200,16))
 def test_nema17_at_32_microsteps(self):self.assertEqual(6400,self.pulses(200,32))
 def test_impulses_saved_as_motor_steps_fall_back(self):self.assertEqual(3200,self.pulses(6400,32))
 def test_non_power_of_two_microsteps_fall_back(self):self.assertEqual(3200,self.pulses(200,12))
 def test_zero_values_fall_back(self):self.assertEqual(3200,self.pulses(0,0))
 def test_absurd_product_falls_back(self):self.assertEqual(3200,self.pulses(2000,64))
if __name__=='__main__':unittest.main(verbosity=2)
