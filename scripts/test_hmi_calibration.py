"""Verify 4-point touch calibration maps the shown targets, not screen corners."""
from pathlib import Path
import os,re,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class CalibrationRegression(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  s=(ROOT/'src/drivers/display.cpp').read_text(encoding='utf-8')
  m=re.search(r'static void applyTouchCalibration\(\) \{',s);assert m
  a=m.start();b=s.index('{',a)+1;depth=1
  while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
  function=s[a:b]
  code=r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#define TFT_WIDTH 480
#define TFT_HEIGHT 320
struct TouchCalibration{int16_t xMin=0,xMax=0,yMin=0,yMax=0;bool valid=false;};
struct Settings{TouchCalibration touchCal;}g_settings;
struct UI{int16_t calRawX[4],calRawY[4];}ui;
static char hmi2TouchCalibrationMessage[64];
namespace NVSManager{static bool saveOk=true;bool saveSettings(const Settings&){return saveOk;}}
'''
  helper=ROOT/'src/ui/hmi_touch_calibration_logic.h'
  if helper.exists():code+='\n#include "src/ui/hmi_touch_calibration_logic.h"\n'
  code+=function+r'''
int main(int argc,char**argv){
 int test=std::atoi(argv[1]);
 for(int i=0;i<4;++i){
  int x=(i==0||i==3)?40:439;
  int y=(i<2)?60:279;
  ui.calRawX[i]=200+x*7;
  ui.calRawY[i]=300+y*9;
  if(test==2)ui.calRawX[i]=4000-x*7;
  if(test==3)ui.calRawX[i]=1000;
  if(test==5)ui.calRawY[i]=2100-y*5;
  if(test==6)ui.calRawY[i]=1500+y*5;
 }
 if(test==4){g_settings.touchCal.xMin=123;g_settings.touchCal.valid=true;NVSManager::saveOk=false;}
 applyTouchCalibration();
 std::printf("%d %d %d %d %d\n",g_settings.touchCal.xMin,g_settings.touchCal.xMax,g_settings.touchCal.yMin,g_settings.touchCal.yMax,g_settings.touchCal.valid);
}
'''
  compiler=shutil.which('g++') or str(Path.home()/'.platformio/packages/toolchain-gccmingw32/bin/g++.exe')
  cls.tmp=tempfile.TemporaryDirectory(prefix='touch-cal-',dir=Path.home()/'AppData/Local/hermes/cache/scratch');cls.addClassCleanup(cls.tmp.cleanup);work=Path(cls.tmp.name);cpp=work/'cal.cpp';cls.exe=work/'cal.exe';cpp.write_text(code,encoding='utf-8')
  env=dict(os.environ);env['PATH']=str(Path(compiler).parent)+os.pathsep+env.get('PATH','')
  r=subprocess.run([compiler,'-std=c++11','-static','-I',str(ROOT),str(cpp),'-o',str(cls.exe)],env=env,capture_output=True,text=True)
  if r.returncode:raise RuntimeError(r.stderr)
 def trace(self,n):return tuple(map(int,subprocess.check_output([str(self.exe),str(n)],text=True).split()))
 def test_target_insets_are_extrapolated_to_edges(self):self.assertEqual((200,3553,300,3171,1),self.trace(1))
 def test_reversed_axis_is_preserved(self):self.assertEqual((4000,647,300,3171,1),self.trace(2))
 def test_degenerate_samples_are_not_saved(self):self.assertEqual(0,self.trace(3)[4])
 def test_nvs_failure_restores_previous_calibration(self):self.assertEqual((123,0,0,0,1),self.trace(4))
 def test_short_reversed_axis_span_is_rejected(self):self.assertEqual(0,self.trace(5)[4])
 def test_short_axis_span_is_rejected(self):self.assertEqual(0,self.trace(6)[4])
if __name__=='__main__':unittest.main(verbosity=2)
