"""Host test actual HMI2 renderer functions with a recording display; no hardware."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from test_heater_triac_isr import extract_function
ROOT=Path(__file__).resolve().parents[1]
class HmiRenderRegression(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  compiler=shutil.which('g++') or str(Path.home()/'.platformio/packages/toolchain-gccmingw32/bin/g++.exe')
  scratch=Path.home()/'AppData/Local/hermes/cache/scratch';scratch.mkdir(parents=True,exist_ok=True)
  cls.tmp=tempfile.TemporaryDirectory(prefix='hmi-render-',dir=scratch);cls.addClassCleanup(cls.tmp.cleanup)
  s=(ROOT/'src/drivers/display.cpp').read_text(encoding='utf-8') + '\n' + (ROOT/'src/ui/hmi/hmi2_core.inc').read_text(encoding='utf-8')
  functions=[]
  for sig in ['static void hmi2RenderHome(bool full)','static void hmi2RenderTemps(bool full)']:
   functions.append(extract_function(s,sig))
  # Include the production render-cache state, when present.
  state=''
  if 'struct Hmi2RenderCache {' in s:
   a=s.index('struct Hmi2RenderCache {');b=s.index('static void hmi2DrawHeader',a)
   state=s[a:b]
  code=r'''
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#define TFT_WIDTH 480
#define TFT_HEIGHT 320
#define HMI2_HEADER_H 32
#define HMI2_FOOTER_H 48
#define HMI2_VISIBLE_ROWS 4
#define HMI2_ROW_H 60
#define HMI2_RAIL_W 48
#define HMI2_NAV_Y 1
#define HMI2_NAV_H 30
#define HMI2_NAV_W 116
#define HMI2_NAV_LEFT_X 246
#define HMI2_NAV_RIGHT_X 364
#define COLOR_WARNING 6
#define TFT_WHITE 8
#define TFT_BLACK 9
#define middle_left 1
#define middle_right 2
#define top_left 0
namespace fonts {static int efontJA_16_b,efontJA_16;}
static int fills=0,rowsDrawn=0,valuesDrawn=0,headers=0,rails=0,footers=0,infoDrawn=0;
struct TFT {
 void setFont(const void*){} void setTextSize(float){} void fillScreen(int){++fills;}
 void fillRect(int,int,int,int,int){++fills;} void drawFastHLine(int,int,int,int){++fills;}
 void drawRect(int,int,int,int,int){++fills;} void drawLine(int,int,int,int,int){++fills;}
 int color565(int,int,int){return 0;} void setTextDatum(int){} void setTextColor(int){} void setTextColor(int,int){}
} tft;
int colorButtonBody(){return 5;} int colorBg(){return 0;} int colorFg(){return 1;} int colorMuted(){return 2;} int colorBorder(){return 3;}
void drawDisplayString(const char*,int,int){++valuesDrawn;}
struct State{bool running=false,paused=false;}g_state;
bool isModeRunning(const State&s){return s.running;}
bool hasUnsavedModeSetupDraft(){return false;}
static uint8_t hmi2Scroll=0,hmi2Section=0;
static bool hmi2TouchCalibrationPage=false,hmi2TempsPage=false;
enum class Hmi2RowType:uint8_t{Link,Read,Value,Bool,Notice};
enum class Hmi2Field:uint8_t{None};
struct Hmi2Row {Hmi2RowType type=Hmi2RowType::Read;Hmi2Field field=Hmi2Field::None;const char*label="Куб";char value[64]="82,4";char unit[20]="°C";const char*linkText=nullptr;uint16_t tone=1;};
static Hmi2Row g_hmi2Rows[10];static uint8_t g_hmi2RowCount=4;
struct Hmi2InfoPair{const char*key="Куб";char value[40]="82,4 °C";bool dim=false;};
static Hmi2InfoPair inputInfo[12];
uint8_t hmi2BuildRows(){return g_hmi2RowCount;}
uint8_t hmi2BuildInfoPairs(Hmi2InfoPair*out,uint8_t){std::memcpy(out,inputInfo,sizeof(inputInfo));return 11;}
void hmi2DrawHeader(bool=false){++headers;}
void hmi2DrawRow(int16_t,uint8_t,const Hmi2Row&){++rowsDrawn;}
void hmi2DrawRowValue(int16_t,uint8_t,const Hmi2Row&){++valuesDrawn;}
void hmi2DrawRail(uint8_t){++rails;}
void hmi2DrawFooter(bool,bool){++footers;}
void hmi2DrawInfoFooter(bool){++footers;}
void hmi2DrawInfoCell(uint8_t,const Hmi2InfoPair&,uint8_t,bool){++infoDrawn;}
'''
  code+=state+'\nstatic Hmi2InfoPair g_hmi2InfoCache[12];\n'+'\n'.join(functions)
  code+=r'''
void resetCounters(){fills=rowsDrawn=valuesDrawn=headers=rails=footers=infoDrawn=0;}
int main(int argc,char**argv){
 if(argc!=2)return 2;int n=std::atoi(argv[1]);
 if(n<4){hmi2RenderHome(true);resetCounters();if(n==2)std::strcpy(g_hmi2Rows[1].value,"83,1");if(n==3)hmi2Scroll=1;hmi2RenderHome(false);}
 else{hmi2TempsPage=true;hmi2RenderTemps(true);resetCounters();if(n==5)std::strcpy(inputInfo[1].value,"83,1 °C");hmi2RenderTemps(false);}
 std::printf("%d %d %d %d %d %d %d\n",fills,rowsDrawn,valuesDrawn,headers,rails,footers,infoDrawn);
}
'''
  path=Path(cls.tmp.name);cpp=path/'render.cpp';cls.exe=path/'render.exe';cpp.write_text(code,encoding='utf-8')
  env=dict(os.environ);env['PATH']=str(Path(compiler).parent)+os.pathsep+env.get('PATH','')
  result=subprocess.run([compiler,'-std=c++11','-static',str(cpp),'-o',str(cls.exe)],env=env,capture_output=True,text=True)
  if result.returncode:raise RuntimeError(str(result.returncode)+' '+result.stderr)
 def trace(self,n):return tuple(map(int,subprocess.check_output([str(self.exe),str(n)],text=True).split()))
 def test_unchanged_home_does_not_erase_or_redraw_rows(self):
  result=self.trace(1);self.assertEqual((0,0,0),result[:3]);self.assertEqual((0,0),result[4:6])
 def test_home_temperature_changes_value_only(self):
  result=self.trace(2);self.assertEqual((0,0,1),result[:3]);self.assertEqual((0,0),result[4:6])
 def test_unchanged_info_does_not_erase_or_repaint_cells(self):
  result=self.trace(4);self.assertEqual(0,result[0]);self.assertEqual(0,result[6]);self.assertEqual(0,result[5])
 def test_info_single_value_repaints_only_one_cell_value(self):
  result=self.trace(5);self.assertEqual(0,result[0]);self.assertEqual(1,result[6]);self.assertEqual(0,result[5])
if __name__=='__main__':unittest.main(verbosity=2)
