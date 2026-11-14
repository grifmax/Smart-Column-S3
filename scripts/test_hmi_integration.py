"""Check HMI ownership integration without parsing unrelated legacy geometry."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class HmiIntegrationContracts(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.display=(ROOT/'src/drivers/display.cpp').read_text(encoding='utf-8') + '\n' + (ROOT/'src/ui/hmi/hmi2_core.inc').read_text(encoding='utf-8')
  cls.main=(ROOT/'src/main.cpp').read_text(encoding='utf-8')
 def test_new_navigation_precedes_legacy_footer(self):
  body=self.display[self.display.index('static bool handleNavigationTap('):self.display.index('static bool handleModeMonitorTap(')]
  self.assertLess(body.index('hmi2OwnsScreen(ui.currentScreen)'),body.index('if (ty >= (TFT_HEIGHT - UI_FOOTER_H))'))
  self.assertLess(body.index('handleOperatorOverlayTap'),body.index('hmi2OwnsScreen(ui.currentScreen)'))
 def test_new_renderer_does_not_fall_through_to_global_status(self):
  block=self.display[self.display.index('if (hmi2OwnsScreen(ui.currentScreen)) {\n                                    hmi2RenderRoot'):]
  prefix=block[:block.index('if (TftOperatorOverlayLogic::isActive')]
  self.assertIn('} else {',prefix)
  self.assertIn('drawGlobalStatusBar(state, false)',prefix.split('} else {',1)[1])
  self.assertNotIn('drawGlobalStatusBar',prefix.split('} else {',1)[0])
 def test_new_screen_does_not_inherit_sparkline_invalidation(self):
  self.assertIn('if (HMI_LEGACY && now - lastSparklineUpdateMs >',self.display)
 def test_touch_poll_is_faster_than_value_presentation(self):
  self.assertIn('HMI_LEGACY ? INTERVAL_DISPLAY_UPDATE : 25U',self.main)
  self.assertIn('now - uiLive.lastUpdateMs >= 250',self.display)
 def test_touch_read_does_not_depend_on_penirq_edge(self):
  body=self.display[self.display.index('bool touchReadRaw(int16_t *x, int16_t *y) {'):]
  body=body[:body.index('static void getTouchCalibration(')]
  self.assertIn('touch.isrWake = true;',body)
  self.assertLess(body.index('touch.isrWake = true'),body.index('touch.getPoint()'))
  self.assertNotIn('tirqTouched',body)
  self.assertIn('TOUCH_Z_PRESSED',body)
 def test_touch_diagnostics_are_exposed(self):
  api=(ROOT/'src/interface/api/api_status.cpp').read_text(encoding='utf-8')
  for key in ('touchPolls','touchPressedReads','touchTaps'):
   self.assertIn('display["%s"]'%key,api)
 def test_header_nav_matches_approved_prototype(self):
  self.assertIn('HMI2_NAV_W = 116',self.display)
  self.assertIn('HMI2_NAV_GAP = 2',self.display)
  self.assertIn('HMI2_NAV_LEFT_X = TFT_WIDTH - (HMI2_NAV_W * 2 + HMI2_NAV_GAP)',self.display)
  self.assertIn('HMI2_NAV_Y = 1',self.display)
  self.assertIn('HMI2_NAV_H = 30',self.display)
  proto=(ROOT/'docs/prototype/tft-menu-v3.html').read_text(encoding='utf-8')
  self.assertIn('width:116px;height:30px',proto)
  self.assertIn('.pager{position:absolute;right:1px',proto)
 def test_nav_button_has_white_inner_border_and_no_stripe(self):
  body=self.display[self.display.index('static void hmi2DrawNavButton'):]
  body=body[:body.index('static void hmi2DrawHeader')]
  self.assertIn('tft.drawRect(x + 1, y + 1, w - 2, h - 2, TFT_WHITE)',body)
  self.assertNotIn('COLOR_DANGER',body)
  self.assertIn('tft.drawLine',body)
 def test_dashboard_is_white_with_black_larger_type(self):
  body=self.display[self.display.index('static void hmi2DrawInfoCell'):]
  body=body[:body.index('static void hmi2RenderTemps')]
  self.assertIn('TFT_BLACK',body)
  self.assertIn('TFT_WHITE',body)
  self.assertIn('12, 20',body)
  temps=self.display[self.display.index('static void hmi2RenderTemps'):]
  temps=temps[:temps.index('static void hmi2RenderHome')]
  self.assertIn('TFT_WHITE',temps)
 def test_old_corners_not_used_as_raw_screen_edges(self):
  self.assertIn('HmiTouchCalibration::axis(left, right',self.display)
  self.assertIn('HmiTouchCalibration::kLeft',self.display)
  self.assertIn('TFT_WIDTH - 1',self.display)
if __name__=='__main__':unittest.main(verbosity=2)
