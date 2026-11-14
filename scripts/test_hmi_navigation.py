"""Exercise the production HMI2 tap dispatcher on the host, with no actuators."""
from pathlib import Path
import os,re,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class HmiNavigationRegression(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  source=(ROOT/'src/drivers/display.cpp').read_text(encoding='utf-8') + '\n' + (ROOT/'src/ui/hmi/hmi2_core.inc').read_text(encoding='utf-8')
  signature=re.search(r'static bool hmi2HandleHomeTap\(int16_t tx, int16_t ty,\s+const SystemState &state\) \{',source)
  if not signature:raise RuntimeError('HMI handler definition missing')
  start=signature.start();opening=source.index('{',start);depth=1;end=opening+1
  while depth:
   depth+=(source[end]=='{')-(source[end]=='}');end+=1
  function=source[start:end]
  code=r'''
#include <cstdint>
#include <cstring>
#include <cstdio>
#define HMI_LEGACY 0
#define TFT_WIDTH 480
#define TFT_HEIGHT 320
#define HMI2_HEADER_H 32
#define HMI2_FOOTER_H 48
#define HMI2_ROW_H 60
#define HMI2_RAIL_W 48
#define HMI2_MARGIN_X 0
#define HMI2_NAV_Y 1
#define HMI2_NAV_H 30
#define HMI2_NAV_W 116
#define HMI2_NAV_LEFT_X 246
#define HMI2_NAV_RIGHT_X 364
#define HMI2_VISIBLE_ROWS 4
#define TOUCH_CAL_X_MIN 200
#define TOUCH_CAL_X_MAX 3800
#define TOUCH_CAL_Y_MIN 200
#define TOUCH_CAL_Y_MAX 3800
#define TEMP_COUNT 7
#include "src/ui/hmi_page_model.h"
#include "src/ui/hmi/keyboard_model.h"
enum Mode{IDLE,RECT,DIST};
enum UiScreen{UI_DASHBOARD,UI_MODE_PICKER,UI_PROFILE_PICKER,UI_MODE_PARAMETERS,UI_PREFLIGHT,UI_PROCESS_PARAMS,UI_SERVICE_TEST,UI_DOSING,UI_SENSOR_CALIBRATION,UI_PUMP_CALIBRATION,UI_EQUIPMENT,UI_VALUE_EDIT,UI_ALL_TEMPS,UI_PROCESS_EVENTS,UI_MODE_SETUP};
struct SystemState{Mode mode=IDLE;bool paused=false;};static SystemState g_state;
struct TouchCalibration{int xMin,xMax,yMin,yMax;bool valid;};struct TempCalibration{uint8_t addresses[7][8];float offsets[7]={0};};struct Settings{TouchCalibration touchCal;TempCalibration tempCal;}g_settings;
struct UI{UiScreen currentScreen=UI_DASHBOARD;struct{bool valid=false;Mode mode=IDLE;}setupDraft;int sensorCalibrationPage=0,sensorCalibrationTempRole=0;char sensorCalibrationMessage[144],pumpCalibrationMessage[144];}ui;
namespace EditableFields{enum Id{FIELD,SENSOR_TEMPERATURE_OFFSET_C};struct Descriptor{Id id=FIELD;float step=1,fastStep=10;};float normalize(Id,float v){return v;}}
struct Edit{const EditableFields::Descriptor*descriptor=nullptr;float value=0;void(*onSave)(float)=nullptr;}edit;
namespace DisplayRedrawReason{enum Reason{TAP_ACTION};}
namespace ControlCommandQueue{enum class Type{Stop,Pause,Resume};enum class Reason{ProcessAction};}
namespace TftOperatorOverlayLogic{enum class Action{ProcessStop};}
namespace Display{void startTouchCalibration(){}}
namespace NVSManager{bool saveSettings(const Settings&){return true;}}
namespace Hmi2Field{enum Field{None};}
enum class Hmi2RowType{Link,Read,Value,Bool,Notice};
struct Hmi2Row{Hmi2RowType type=Hmi2RowType::Read;const char*label="Куб";Hmi2Field::Field field=Hmi2Field::None;};
static Hmi2Row g_hmi2Rows[16];static uint8_t count=6;
static uint8_t hmi2Section=0,hmi2Scroll=0,hmi2TempsReturnSection=6,hmi2TempsReturnScroll=0;
static bool hmi2TempsPage=false,hmi2TouchCalibrationPage=false,touch_ok=true;
static char hmi2TouchCalibrationMessage[64];
enum class Hmi2Subpage{None,Modes,Profiles,Journal,Wifi,Sensors,SensorAddresses,SensorOffsets,WifiNetworks,WifiPassword};static Hmi2Subpage hmi2Subpage=Hmi2Subpage::None;
static uint8_t hmi2SensorRole=0,g_hmi2ScanCount=2;static uint8_t g_hmi2ScanAddresses[10][8]={};static char hmi2SensorMessage[64];
static uint8_t hmi2WifiNetworkCount=2;static char hmi2WifiNetworks[10][33]={"Home","Lab"};static char hmi2WifiSsid[64],hmi2WifiMessage[64];
static HmiKeyboard::State hmi2WifiKeyboard;
static int wifiScans=0,passwordOpens=0;
static const Mode kHmi2Modes[]={RECT,DIST};static const int kHmi2SectionCount=8;
static int redraws=0,commands=0,overlays=0,pops=0,pushes=0,lastScreen=-1;
bool hit(int x,int y,int l,int t,int w,int h){return x>=l&&x<l+w&&y>=t&&y<t+h;}
bool isModeRunning(const SystemState&s){return s.mode!=IDLE;}
void popScreen(){++pops;ui.currentScreen=UI_DASHBOARD;}
void pushScreen(UiScreen s){++pushes;lastScreen=s;}
void requestFullScreenRedraw(){++redraws;}
void requestRedraw(DisplayRedrawReason::Reason){++redraws;}
void armScreenSwitchGuard(){}
void openOperatorOverlay(TftOperatorOverlayLogic::Action){++overlays;}
void enqueueTftProcessCommand(ControlCommandQueue::Type,ControlCommandQueue::Reason){++commands;}
uint8_t hmi2BuildRows(){return count;}
void beginModeSetup(Mode){}bool selectSetupProfile(unsigned){return true;}
bool isScreenEditable(UiScreen,const SystemState&){return true;}
void openSetupProfilePicker(){++pushes;lastScreen=UI_PROFILE_PICKER;}
void openModeParameters(Mode){++pushes;lastScreen=UI_MODE_PARAMETERS;}
bool requireModeSetupDraftResolution(){return false;}void refreshTftPreflight(){}
bool hasUnsavedModeSetupDraft(){return false;}
static bool setpointLocked=false;static int savedValues=0;
bool isTftSetpointLocked(EditableFields::Id){return setpointLocked;}
void saveTestValue(float){++savedValues;}
bool saveModeSetupDraft(char*,unsigned){return true;}
void setModeSetupDraftMessage(const char*){}
namespace ServiceTestSession{bool blocksProcessResume(){return true;}}
void openProcessEvents(){++pushes;lastScreen=UI_PROCESS_EVENTS;}
static int assigned=0;static int clearCalls=0;static int autoAssigned=0;
uint8_t hmi2ScanSensors(){return g_hmi2ScanCount;}
int8_t hmi2SensorRoleOwner(const uint8_t*,uint8_t){return -1;}
bool hmi2AssignSensorAddress(uint8_t,const uint8_t*){++assigned;return true;}
bool hmi2ClearSensorRole(uint8_t){++clearCalls;return true;}
uint8_t hmi2AutoAssignSensors(){++autoAssigned;return 0;}
void openValueEdit(int,float,void(*)(float)){++pushes;lastScreen=UI_VALUE_EDIT;}
void saveSensorTemperatureOffset(float){}
void hmi2ScanWifiNetworks(){++wifiScans;}
void hmi2OpenWifiPassword(const char*){++passwordOpens;}
void hmi2SaveWifiPassword(){}
const char* hmi2CurrentSsid(){return "Home";}
void scanSensorCalibrationDs18(){}
bool hmi2ToggleSetting(Hmi2Field::Field){return true;}
bool hmi2AdjustValue(Hmi2Field::Field,int,bool){return true;}
void hmi2OpenValueEditor(Hmi2Field::Field){}
bool hmi2FieldLocked(Hmi2Field::Field){return false;}
'''
  code+=function+r'''
int main(int argc,char**argv){
 if(argc!=2)return 2;int n=std::atoi(argv[1]);
 if(n==1){hmi2TempsPage=true;hmi2TempsReturnSection=6;hmi2TempsReturnScroll=3;hmi2HandleHomeTap(260,16,g_state);}
 if(n==2){hmi2HandleHomeTap(100,122,g_state);}
 if(n==3){g_state.mode=RECT;hmi2TempsPage=true;hmi2HandleHomeTap(400,295,g_state);}
 if(n==4){hmi2Section=6;hmi2Scroll=1;hmi2HandleHomeTap(455,150,g_state);}
 if(n==5){hmi2HandleHomeTap(380,16,g_state);}
 if(n==6){hmi2Subpage=Hmi2Subpage::Modes;ui.currentScreen=UI_MODE_PICKER;hmi2HandleHomeTap(70,295,g_state);}
 if(n==7){g_hmi2Rows[0].type=Hmi2RowType::Link;g_hmi2Rows[0].label="Профиль";hmi2HandleHomeTap(350,60,g_state);}
 if(n==8){g_hmi2Rows[0].type=Hmi2RowType::Link;g_hmi2Rows[0].label="Старт процесса";hmi2HandleHomeTap(350,60,g_state);}
 if(n==9){hmi2Section=0;hmi2HandleHomeTap(260,16,g_state);}
 if(n==10){g_hmi2Rows[0].type=Hmi2RowType::Link;g_hmi2Rows[0].label="Назначение датчиков темп.";hmi2HandleHomeTap(350,60,g_state);}
 if(n==11){hmi2Subpage=Hmi2Subpage::Sensors;hmi2HandleHomeTap(350,60,g_state);}
 if(n==12){hmi2Subpage=Hmi2Subpage::SensorAddresses;hmi2SensorRole=0;hmi2HandleHomeTap(350,122,g_state);}
 if(n==13){hmi2Subpage=Hmi2Subpage::SensorAddresses;hmi2HandleHomeTap(350,60,g_state);}
 if(n==14){hmi2Subpage=Hmi2Subpage::SensorOffsets;hmi2HandleHomeTap(350,60,g_state);}
 if(n==15){count=8;hmi2Subpage=Hmi2Subpage::Sensors;hmi2Scroll=4;hmi2HandleHomeTap(350,240,g_state);}
 if(n==16){count=6;hmi2Subpage=Hmi2Subpage::Wifi;hmi2HandleHomeTap(350,122,g_state);}
 if(n==17){hmi2Subpage=Hmi2Subpage::WifiNetworks;hmi2HandleHomeTap(350,122,g_state);}
 if(n==18){hmi2Subpage=Hmi2Subpage::WifiPassword;hmi2HandleHomeTap(60,110,g_state);}
 if(n==19){hmi2Subpage=Hmi2Subpage::WifiPassword;hmi2HandleHomeTap(60,150,g_state);}
 if(n==20){hmi2Subpage=Hmi2Subpage::WifiPassword;hmi2HandleHomeTap(60,195,g_state);}
 if(n==21){ui.currentScreen=UI_VALUE_EDIT;static EditableFields::Descriptor d;edit.descriptor=&d;edit.onSave=saveTestValue;setpointLocked=true;hmi2HandleHomeTap(350,295,g_state);}
 if(n==22){ui.currentScreen=UI_VALUE_EDIT;static EditableFields::Descriptor d;edit.descriptor=&d;edit.value=1;setpointLocked=true;hmi2HandleHomeTap(300,170,g_state);}
 if(n==23){ui.currentScreen=UI_VALUE_EDIT;static EditableFields::Descriptor d;edit.descriptor=&d;edit.onSave=saveTestValue;hmi2HandleHomeTap(350,295,g_state);}
 std::printf("%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",hmi2TempsPage,hmi2Section,hmi2Scroll,commands,overlays,pops,pushes,lastScreen,static_cast<int>(hmi2Subpage),assigned,clearCalls,autoAssigned,wifiScans,passwordOpens,(int)hmi2WifiKeyboard.length,savedValues,(int)edit.value);
}
'''
  code=code.replace('#include <cstdio>','#include <cstdio>\n#include <cstdlib>')
  compiler=shutil.which('g++') or str(Path.home()/'.platformio/packages/toolchain-gccmingw32/bin/g++.exe')
  scratch=Path.home()/'AppData/Local/hermes/cache/scratch';cls.temp=tempfile.TemporaryDirectory(prefix='hmi-nav-',dir=scratch);cls.addClassCleanup(cls.temp.cleanup)
  work=Path(cls.temp.name);cpp=work/'nav.cpp';cls.exe=work/'nav.exe';cpp.write_text(code,encoding='utf-8')
  env=dict(os.environ);env['PATH']=str(Path(compiler).parent)+os.pathsep+env.get('PATH','')
  r=subprocess.run([compiler,'-std=c++11','-static','-I',str(ROOT),str(cpp),'-o',str(cls.exe)],env=env,capture_output=True,text=True)
  if r.returncode:raise RuntimeError(r.stderr)
 def trace(self,n):return tuple(map(int,subprocess.check_output([str(self.exe),str(n)],text=True).split()))
 def test_info_open_back_preserves_section_and_scroll(self):self.assertEqual((0,6,3),self.trace(1)[:3])
 def test_tapping_cube_opens_thermometers(self):self.assertEqual(1,self.trace(2)[0])
 def test_info_stop_requires_overlay_not_direct_command(self):self.assertEqual((0,1),self.trace(3)[3:5])
 def test_middle_scroll_rail_does_not_scroll(self):self.assertEqual(1,self.trace(4)[2])
 def test_header_next_switches_section(self):self.assertEqual(1,self.trace(5)[1])
 def test_back_from_mode_picker_pops(self):self.assertEqual(1,self.trace(6)[5])
 def test_profile_uses_setup_loader(self):self.assertEqual(1,self.trace(7)[6])
 def test_start_without_selected_mode_opens_picker_not_process(self):self.assertEqual((0,1,1),(self.trace(8)[3],self.trace(8)[6],self.trace(8)[7]))
 def test_main_back_opens_dashboard(self):self.assertEqual(1,self.trace(9)[0])
 def test_settings_opens_sensor_assignment(self):self.assertEqual(5,self.trace(10)[8])
 def test_role_row_opens_free_addresses(self):self.assertEqual(6,self.trace(11)[8])
 def test_free_address_assigns_and_returns(self):self.assertEqual((1,5),(self.trace(12)[9],self.trace(12)[8]))
 def test_used_address_is_not_assigned(self):self.assertEqual((0,6),(self.trace(13)[9],self.trace(13)[8]))
 def test_offset_row_opens_editor(self):self.assertEqual(11,self.trace(14)[7])
 def test_auto_assign_row_runs(self):self.assertEqual(1,self.trace(15)[11])
 def test_wifi_network_row_scans_and_opens_list(self):self.assertEqual((8,1),(self.trace(16)[8],self.trace(16)[12]))
 def test_wifi_list_row_opens_password(self):self.assertEqual(1,self.trace(17)[13])
 def test_keyboard_appends_character(self):self.assertEqual(1,self.trace(18)[14])
 def test_keyboard_second_row_appends(self):self.assertEqual(1,self.trace(19)[14])
 def test_keyboard_third_row_appends(self):self.assertEqual(1,self.trace(20)[14])
 def test_open_editor_cannot_save_after_output_started(self):self.assertEqual((0,0),(self.trace(21)[15],self.trace(21)[5]))
 def test_locked_editor_cannot_change_draft(self):self.assertEqual(1,self.trace(22)[16])
 def test_unlocked_editor_still_saves(self):self.assertEqual((1,1),(self.trace(23)[15],self.trace(23)[5]))
if __name__=='__main__':unittest.main(verbosity=2)
