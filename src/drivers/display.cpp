/**
 * Smart-Column S3 - Драйвер дисплея
 *
 * TFT 3.5" ILI9488 (основной)
 * Использует LovyanGFX для TFT
 */

#include "display.h"
#include "control/fsm.h"
#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/valves.h"
#include "interface/localization.h"
#include "storage/nvs_manager.h"
#include <LovyanGFX.hpp>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <esp_task_wdt.h>


#if TFT_ENABLED
#define LGFX_USE_V1
// =============================================================================
// LovyanGFX конфигурация для ILI9488 (только
// дисплей)
// =============================================================================

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

public:
  LGFX(void) {
    // SPI bus configuration
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST; // HSPI
      cfg.spi_mode = 0;
      // 40MHz на ILI9488 часто нестабилен на длинных
      // проводах/клонах.
      cfg.freq_write = 27000000;
      cfg.freq_read = 8000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = TFT_SCLK;
      cfg.pin_mosi = TFT_MOSI;
      cfg.pin_miso = TFT_MISO;
      cfg.pin_dc = TFT_DC;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    // Panel configuration
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = TFT_CS;
      cfg.pin_rst = TFT_RST;
      cfg.pin_busy = -1;
      cfg.memory_width = 320;
      cfg.memory_height = 480;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};

static LGFX tft;
static bool tft_ok = false;

// XPT2046 touch - hardware SPI on separate bus
static bool touch_ok = false;
static SPIClass touchSpi(HSPI);
static XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

bool touchReadRaw(int16_t *x, int16_t *y) {
#ifndef TOUCH_IGNORE_IRQ
  if (!touch.touched())
    return false;
#else
  if (!touch.tirqTouched() && !touch.touched())
    return false;
#endif

  TS_Point p = touch.getPoint();
  *x = p.x;
  *y = p.y;

  // Filter out invalid readings
  if (*x < 100 || *x > 4000 || *y < 100 || *y > 4000) {
#ifdef TOUCH_DEBUG_RAW
    return true;
#else
    return false;
#endif
  }

  return true;
}

static void getTouchCalibration(int16_t *xMin, int16_t *xMax, int16_t *yMin,
                                int16_t *yMax) {
  if (g_settings.touchCal.valid) {
    *xMin = g_settings.touchCal.xMin;
    *xMax = g_settings.touchCal.xMax;
    *yMin = g_settings.touchCal.yMin;
    *yMax = g_settings.touchCal.yMax;
  } else {
    *xMin = TOUCH_CAL_X_MIN;
    *xMax = TOUCH_CAL_X_MAX;
    *yMin = TOUCH_CAL_Y_MIN;
    *yMax = TOUCH_CAL_Y_MAX;
  }
}

// Read calibrated touch (screen coordinates)
bool touchRead(int16_t *sx, int16_t *sy) {
  int16_t rawX, rawY;
  if (!touchReadRaw(&rawX, &rawY)) {
    return false;
  }

  int16_t xMin, xMax, yMin, yMax;
  getTouchCalibration(&xMin, &xMax, &yMin, &yMax);

  // Map to screen coordinates using calibration
  *sx = map(rawX, xMin, xMax, 0, TFT_WIDTH);
  *sy = map(rawY, yMin, yMax, 0, TFT_HEIGHT);

  // Clamp to screen bounds
  *sx = constrain(*sx, 0, TFT_WIDTH - 1);
  *sy = constrain(*sy, 0, TFT_HEIGHT - 1);

  return true;
}

static bool readTouchRawFiltered(int16_t *x, int16_t *y) {
  int32_t sumX = 0;
  int32_t sumY = 0;
  uint8_t samples = 0;
  for (uint8_t i = 0; i < 5; i++) {
    int16_t rx = 0;
    int16_t ry = 0;
    if (touchReadRaw(&rx, &ry)) {
      sumX += rx;
      sumY += ry;
      samples++;
    }
    delay(2);
  }
  if (samples == 0)
    return false;
  *x = sumX / samples;
  *y = sumY / samples;
  return true;
}

static bool detectCalibrationRequest() {
  const uint32_t start = millis();
  while (millis() - start < 800) {
    int16_t rx = 0;
    int16_t ry = 0;
    if (touchReadRaw(&rx, &ry)) {
      return true;
    }
    delay(20);
  }
  return false;
}

// =============================================================================
// UI helpers
// =============================================================================
static const int16_t UI_HEADER_H = 40; // Немного уменьшил
static const int16_t UI_FOOTER_H =
    65; // Немного увеличил для шрифта
static const int16_t UI_CONTENT_Y =
    10; // Начинаем почти сверху
static const int16_t UI_CONTENT_H = TFT_HEIGHT - UI_FOOTER_H - 10;

// Industrial HMI palette for the built-in TFT
#define COLOR_PRIMARY tft.color565(0, 168, 140)
#define COLOR_SUCCESS tft.color565(94, 184, 108)
#define COLOR_DANGER tft.color565(208, 72, 72)
#define COLOR_WARNING tft.color565(230, 170, 34)
#define COLOR_INFO tft.color565(64, 154, 220)
#define COLOR_DARK_GREY tft.color565(64, 70, 78)
#define COLOR_LIGHT_GREY tft.color565(212, 218, 222)

enum UiScreen : uint8_t {
  UI_DASHBOARD = 0,
  UI_CONTROL,
  UI_SETTINGS,
  UI_SERVICE,
  UI_MODE_MONITOR,
  UI_EQUIPMENT,
  UI_RECT_PARAMS,
  UI_DIST_PARAMS,
  UI_CALIBRATION,
  UI_MANUAL,
  UI_MASHING,
  UI_HOLD,
  UI_VALUE_EDIT,
  UI_ALL_TEMPS
};

struct UiState {
  UiScreen currentScreen = UI_DASHBOARD;
  UiScreen rootScreen = UI_DASHBOARD;
  UiScreen stack[6] = {};
  uint8_t stackDepth = 0;
  bool needsRedraw = true;
  UiScreen lastRenderedScreen = UI_DASHBOARD;

  bool touchPressed = false;
  uint32_t lastTapMs = 0;
  uint32_t ignoreTapUntilMs = 0;
  int16_t touchDownX = 0;
  int16_t touchDownY = 0;
  int16_t touchLastX = 0;
  int16_t touchLastY = 0;
  uint32_t touchDownMs = 0;

  bool calibrating = false;
  uint8_t calStep = 0;
  uint8_t calSkip = 0;
  int16_t calRawX[4] = {0};
  int16_t calRawY[4] = {0};
  // Continuous raw sampling during held press for calibration accuracy.
  bool calIsCollecting = false;
  int32_t calSumRawX = 0;
  int32_t calSumRawY = 0;
  uint16_t calSampleCount = 0;

  bool modeSwitchConfirm = false;
  Mode modeSwitchTarget = Mode::IDLE;
};

static UiState ui;

static const uint16_t UI_TAP_MOVE_TOLERANCE = 24;
static const uint32_t UI_TAP_MAX_DURATION_MS = 700;
static const uint32_t UI_TAP_MIN_INTERVAL_MS = 220;
static const uint32_t UI_SCREEN_SWITCH_GUARD_MS = 280;

struct TouchEvent {
  bool pressed = false;
  bool tapped = false;
  bool released = false;
  int16_t x = 0;
  int16_t y = 0;
};

static TouchEvent readTouchEvent() {
  TouchEvent ev;
  int16_t sx = 0;
  int16_t sy = 0;
  const uint32_t now = millis();
  bool pressed = touch_ok && touchRead(&sx, &sy);
  ev.pressed = pressed;
  ev.x = sx;
  ev.y = sy;

  if (pressed && !ui.touchPressed) {
    ui.touchDownX = sx;
    ui.touchDownY = sy;
    ui.touchLastX = sx;
    ui.touchLastY = sy;
    ui.touchDownMs = now;
  } else if (pressed && ui.touchPressed) {
    ui.touchLastX = sx;
    ui.touchLastY = sy;
  }

  if (!pressed && ui.touchPressed) {
    ev.released = true;
    ev.x = ui.touchLastX;
    ev.y = ui.touchLastY;

    const uint32_t touchDurationMs = now - ui.touchDownMs;
    const int32_t dx = static_cast<int32_t>(ui.touchLastX) -
                       static_cast<int32_t>(ui.touchDownX);
    const int32_t dy = static_cast<int32_t>(ui.touchLastY) -
                       static_cast<int32_t>(ui.touchDownY);
    const uint32_t move = static_cast<uint32_t>(abs(dx) + abs(dy));

    if (now >= ui.ignoreTapUntilMs &&
        touchDurationMs <= UI_TAP_MAX_DURATION_MS &&
        move <= UI_TAP_MOVE_TOLERANCE &&
        (now - ui.lastTapMs) > UI_TAP_MIN_INTERVAL_MS) {
      ev.tapped = true;
      ui.lastTapMs = now;
    }
  }

  ui.touchPressed = pressed;
  return ev;
}

static void armScreenSwitchGuard() {
  const uint32_t now = millis();
  ui.ignoreTapUntilMs = now + UI_SCREEN_SWITCH_GUARD_MS;
}

static void pushScreen(UiScreen screen) {
  if (ui.currentScreen == screen)
    return;
  if (ui.stackDepth < 6) {
    ui.stack[ui.stackDepth++] = ui.currentScreen;
  }
  ui.currentScreen = screen;
  ui.needsRedraw = true;
  armScreenSwitchGuard();
}

static void popScreen() {
  UiScreen target = ui.rootScreen;
  if (ui.stackDepth > 0) {
    target = ui.stack[--ui.stackDepth];
  }
  if (ui.currentScreen == target)
    return;
  ui.currentScreen = target;
  ui.needsRedraw = true;
  armScreenSwitchGuard();
}

static void switchRoot(UiScreen screen) {
  const bool changed = (ui.rootScreen != screen) ||
                       (ui.currentScreen != screen) || (ui.stackDepth != 0);
  ui.rootScreen = screen;
  ui.currentScreen = screen;
  ui.stackDepth = 0;
  if (changed) {
    ui.needsRedraw = true;
    armScreenSwitchGuard();
  }
}

typedef void (*ValueSaveCallback)(float);
struct ValueEditState {
  char label[32];
  float value;
  float min;
  float max;
  float step;
  float fastStep;
  ValueSaveCallback onSave;
  char unit[16];
  uint8_t decimals;
  bool returnToModeMonitor = false;
};
static ValueEditState edit;

static void openValueEdit(const char *label, float val, float min, float max,
                          float step, float fastStep, ValueSaveCallback cb,
                          const char *unit = "", uint8_t decimals = 1,
                          bool returnToModeMonitor = false) {
  strncpy(edit.label, label, sizeof(edit.label) - 1);
  edit.label[sizeof(edit.label) - 1] = '\0';
  edit.value = val;
  edit.min = min;
  edit.max = max;
  edit.step = step;
  edit.fastStep = fastStep;
  edit.onSave = cb;
  strncpy(edit.unit, unit, sizeof(edit.unit) - 1);
  edit.unit[sizeof(edit.unit) - 1] = '\0';
  edit.decimals = decimals;
  edit.returnToModeMonitor = returnToModeMonitor;
  pushScreen(UI_VALUE_EDIT);
}

struct DistUiParams {
  float speedMlH = 500.0f;
  float headsVolumeMl = 0.0f;
  float targetVolumeMl = 3000.0f;
  float endTempC = 96.0f;
  float powerPercent = 100.0f;
  float tailsVolumeMl = 0.0f;
};

static DistUiParams distUi;

struct ManualRectUiParams {
  float speedMlH = 600.0f;
  float powerPercent = 60.0f;
  float headsTargetMl = 300.0f;
  float bodyTargetMl = 2500.0f;
  float tailsTargetMl = 800.0f;
};

static ManualRectUiParams manualRectUi;
static uint8_t rectParamsPage = 0; // 0 = профиль СС/фракции, 1 = техпараметры
static uint8_t g_editMashStepIdx = 0;
static uint8_t g_editHoldStepIdx = 0;

static float clampUiFloat(float v, float vmin, float vmax) {
  if (v < vmin)
    return vmin;
  if (v > vmax)
    return vmax;
  return v;
}

static const char *rectFeedstockName(uint8_t feedstock, bool ru) {
  switch (feedstock) {
  case 0:
    return ru ? "Сахар" : "Sugar";
  case 1:
    return ru ? "Мука/зерно" : "Flour/Grain";
  case 2:
    return ru ? "Солод" : "Malt";
  case 3:
    return ru ? "Фрукты" : "Fruit";
  case 4:
    return ru ? "Меласса" : "Molasses";
  case 5:
    return ru ? "Виноград" : "Grape/Wine";
  case 6:
    return ru ? "Мед" : "Honey";
  default:
    return ru ? "Другое" : "Other";
  }
}

static void rectFeedstockDefaults(uint8_t feedstock, float &headsPct,
                                  float &bodyPct, float &tailsPct) {
  switch (feedstock) {
  case 0: // Сахар
    headsPct = 6.0f;
    bodyPct = 84.0f;
    tailsPct = 10.0f;
    break;
  case 1: // Мука/зерно
    headsPct = 8.0f;
    bodyPct = 80.0f;
    tailsPct = 12.0f;
    break;
  case 2: // Солод
    headsPct = 7.0f;
    bodyPct = 81.0f;
    tailsPct = 12.0f;
    break;
  case 3: // Фрукты
    headsPct = 5.0f;
    bodyPct = 75.0f;
    tailsPct = 20.0f;
    break;
  case 4: // Меласса
    headsPct = 8.0f;
    bodyPct = 74.0f;
    tailsPct = 18.0f;
    break;
  case 5: // Виноград/вино
    headsPct = 6.0f;
    bodyPct = 78.0f;
    tailsPct = 16.0f;
    break;
  case 6: // Мед
    headsPct = 7.0f;
    bodyPct = 79.0f;
    tailsPct = 14.0f;
    break;
  default:
    headsPct = RECT_HEADS_PERCENT_DEFAULT;
    bodyPct = RECT_BODY_PERCENT_DEFAULT;
    tailsPct = RECT_TAILS_PERCENT_DEFAULT;
    break;
  }
}

static void normalizeRectFractions() {
  g_settings.rectParams.headsPercent =
      clampUiFloat(g_settings.rectParams.headsPercent, 0.0f, 40.0f);
  g_settings.rectParams.bodyPercent =
      clampUiFloat(g_settings.rectParams.bodyPercent, 0.0f, 100.0f);
  g_settings.rectParams.tailsPercent =
      clampUiFloat(g_settings.rectParams.tailsPercent, 0.0f, 100.0f);

  float sum = g_settings.rectParams.headsPercent +
              g_settings.rectParams.bodyPercent +
              g_settings.rectParams.tailsPercent;
  if (sum > 100.0f) {
    float excess = sum - 100.0f;
    if (g_settings.rectParams.tailsPercent >= excess) {
      g_settings.rectParams.tailsPercent -= excess;
      excess = 0.0f;
    } else {
      excess -= g_settings.rectParams.tailsPercent;
      g_settings.rectParams.tailsPercent = 0.0f;
    }
    if (excess > 0.0f) {
      g_settings.rectParams.bodyPercent -= excess;
      if (g_settings.rectParams.bodyPercent < 0.0f) {
        g_settings.rectParams.bodyPercent = 0.0f;
      }
    }
  }
}

static void applyRectFeedstockDefaultsAndSave(uint8_t feedstock) {
  g_settings.rectParams.feedstock = feedstock;
  float heads = 0.0f;
  float body = 0.0f;
  float tails = 0.0f;
  rectFeedstockDefaults(feedstock, heads, body, tails);
  g_settings.rectParams.headsPercent = heads;
  g_settings.rectParams.bodyPercent = body;
  g_settings.rectParams.tailsPercent = tails;
  normalizeRectFractions();
  NVSManager::saveSettings(g_settings);
}

static MashProfile mashProfileDefault;
static TempStep holdStepsDefault[3];
static uint8_t holdStepsCount = 1;

struct UiLiveCache {
  Mode mode = Mode::IDLE;
  RectPhase phase = RectPhase::IDLE;
  bool paused = false;
  float tCube = 0.0f;
  float tTop = 0.0f;
  float tReflux = 0.0f;
  float tTsa = 0.0f;
  float tWaterIn = 0.0f;
  float tWaterOut = 0.0f;
  float power = 0.0f;
  float pumpSpeed = 0.0f;
  float voltage = 0.0f;
  float pressure = 0.0f;
  uint32_t uptime = 0;
  uint32_t lastUpdateMs = 0;
};

static UiLiveCache uiLive;

struct DashboardRenderCache {
  char status[64] = {0};
  char phaseTimer[32] = {0};
  char processState[24] = {0};
  char safetyState[24] = {0};
  char cube[16] = {0};
  char power[16] = {0};
  char top[16] = {0};
  char reflux[16] = {0};
  char pump[16] = {0};
  char tsa[16] = {0};
  char infoLine[96] = {0};
  char ioLine[96] = {0};
  char uptime[16] = {0};
  uint8_t phaseProgress = 0;
  uint8_t layoutKey = 0xFF;
};

static DashboardRenderCache g_dashboardCache;
static char g_modeTileCache[12][20] = {{0}};
static Mode g_modeRuntimeMode = Mode::IDLE;
static uint32_t g_modeRuntimeStartUptime = 0;

struct DisplayRuntimeStatsInternal {
  uint32_t framesRendered = 0;
  uint32_t slowFrames = 0;
  uint32_t watchdogRecoveries = 0;
  uint32_t hardWatchdogRecoveries = 0;
  uint32_t hardWatchdogFailures = 0;
  uint16_t lastFrameMs = 0;
  uint16_t maxFrameMs = 0;
  uint32_t lastFrameAtMs = 0;
  uint16_t lastUpdateGapMs = 0;
  uint16_t maxUpdateGapMs = 0;
  uint32_t updateGapOverruns = 0;
  uint32_t lastUpdateCallAtMs = 0;
  uint8_t consecutiveSlowFrames = 0;
  uint8_t consecutiveHardFrames = 0;
  uint8_t softRecoveriesInWindow = 0;
  uint32_t softRecoveryWindowStartedMs = 0;
  uint32_t lastHardRecoveryAtMs = 0;
};

static DisplayRuntimeStatsInternal g_displayStats;

static uint32_t getForceRefreshIntervalMs() {
  extern Settings g_settings;
  switch (g_settings.displaySettings.refreshProfile) {
    case DisplayRefreshProfile::SAFE:   return 5000;
    case DisplayRefreshProfile::FAST:   return 500;
    case DisplayRefreshProfile::NORMAL:
    default:                            return 1000;
  }
}

static const uint16_t DISPLAY_SLOW_FRAME_MS = 120;
static const uint16_t DISPLAY_HARD_FRAME_MS = 250;
static const uint8_t DISPLAY_SOFT_WD_THRESHOLD = 3;
static const uint8_t DISPLAY_HARD_FRAME_BURST_THRESHOLD = 6;
static const uint8_t DISPLAY_SOFT_WD_BURST_FOR_HARD = 3;
static const uint32_t DISPLAY_SOFT_WD_WINDOW_MS = 12000;
static const uint32_t DISPLAY_HARD_RECOVERY_COOLDOWN_MS = 15000;
static const uint16_t DISPLAY_UPDATE_GAP_WARN_MS = INTERVAL_DISPLAY_UPDATE * 3;

static const int16_t CTRL_BW = 225;
static const int16_t CTRL_BH = 48;
static const int16_t CTRL_X1 = 10;
static const int16_t CTRL_X2 = 245;
static const int16_t CTRL_STATUS_Y = 8;
static const int16_t CTRL_STATUS_H = 30;
static const int16_t CTRL_ACTION_BW = 82;
static const int16_t CTRL_ACTION_BH = 22;
static const int16_t CTRL_ACTION_Y = 12;
static const int16_t CTRL_ACTION_GAP = 6;
static const int16_t CTRL_Y1 = 44;
static const int16_t CTRL_Y2 = 96;
static const int16_t CTRL_Y3 = 148;
static const int16_t CTRL_Y4 = 200;
static const int16_t ROOT_STATUS_Y = 8;
static const int16_t ROOT_STATUS_H = 44;
static const int16_t ROOT_PANEL_Y = 56;
static const int16_t ROOT_PANEL_H = 154;
static const int16_t ROOT_INFO_Y = ROOT_PANEL_Y + ROOT_PANEL_H + 8;
static const int16_t ROOT_INFO_H = 40;
static const int16_t ROOT_LEFT_X = 10;
static const int16_t ROOT_LEFT_W = 154;
static const int16_t ROOT_RIGHT_X = ROOT_LEFT_X + ROOT_LEFT_W + 6;
static const int16_t ROOT_RIGHT_W = TFT_WIDTH - ROOT_RIGHT_X - 10;
static const int16_t ROOT_GRID_COL_GAP = 6;
static const int16_t ROOT_GRID_ROW_GAP = 5;
static const int16_t ROOT_RIGHT_TILE_W =
    (ROOT_RIGHT_W - ROOT_GRID_COL_GAP) / 2;
static const int16_t ROOT_RIGHT_TILE_H =
    (ROOT_PANEL_H - ROOT_GRID_ROW_GAP * 2) / 3;

static bool hit(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw,
                int16_t rh) {
  return (x >= rx && x <= (rx + rw) && y >= ry && y <= (ry + rh));
}

static bool hitPad(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw,
                   int16_t rh, int16_t pad) {
  return hit(x, y, rx - pad, ry - pad, rw + (pad * 2), rh + (pad * 2));
}

static bool isModeRunning(const SystemState &state) {
  return state.mode != Mode::IDLE;
}

static bool isMonitorRootScreen(UiScreen screen) {
  return (screen == UI_DASHBOARD || screen == UI_MODE_MONITOR);
}

static bool isManualAccessAllowed(const SystemState &state) {
  return (state.mode == Mode::IDLE || state.mode == Mode::MANUAL_RECT);
}

static uint16_t dimmedButtonColor() {
  return (g_settings.theme == 1) ? tft.color565(92, 98, 104)
                                 : tft.color565(126, 132, 138);
}

static uint16_t modeButtonColor(const SystemState &state, Mode target,
                                uint16_t idleColor) {
  if (state.mode == target)
    return COLOR_SUCCESS;
  if (isModeRunning(state))
    return dimmedButtonColor();
  return idleColor;
}

static void startModeFromControl(Mode mode) {
  switch (mode) {
  case Mode::RECTIFICATION:
  case Mode::DISTILLATION:
  case Mode::MANUAL_RECT:
  case Mode::NBK:
  case Mode::FERMENTATION:
    if (mode == Mode::DISTILLATION) {
      FSM::Distillation::setParams(distUi.speedMlH, distUi.headsVolumeMl,
                                   distUi.targetVolumeMl, distUi.endTempC);
      FSM::Distillation::setPowerPercent(
          static_cast<uint8_t>(distUi.powerPercent));
    }
    FSM::startMode(g_state, g_settings, mode);
    break;
  case Mode::MASHING:
    FSM::Mashing::start(g_state, &mashProfileDefault);
    break;
  case Mode::HOLD:
    FSM::Hold::start(g_state, holdStepsDefault, holdStepsCount);
    break;
  default:
    break;
  }
}

static void requestModeSwitch(Mode target) {
  ui.modeSwitchConfirm = true;
  ui.modeSwitchTarget = target;
  ui.needsRedraw = true;
}

static void startOrRequestMode(const SystemState &state, Mode target) {
  if (state.mode == target)
    return;
  if (isModeRunning(state)) {
    requestModeSwitch(target);
    return;
  }
  startModeFromControl(target);
}

// =============================================================================
// Settings Callbacks
// =============================================================================
static void saveHeaterPower(float val) {
  g_settings.equipment.heaterPowerW = (uint16_t)val;
  NVSManager::saveSettings(g_settings);
}
static void saveColumnHeight(float val) {
  g_settings.equipment.columnHeightMm = (uint16_t)val;
  NVSManager::saveSettings(g_settings);
}
static void saveCubeVolume(float val) {
  g_settings.equipment.cubeVolumeL = val;
  NVSManager::saveSettings(g_settings);
}
static void savePackingCoeff(float val) {
  g_settings.equipment.packingCoeff = val;
  NVSManager::saveSettings(g_settings);
}

static void saveHeadsPercent(float val) {
  g_settings.rectParams.headsPercent = val;
  normalizeRectFractions();
  NVSManager::saveSettings(g_settings);
}
static void saveBodyPercent(float val) {
  g_settings.rectParams.bodyPercent = val;
  normalizeRectFractions();
  NVSManager::saveSettings(g_settings);
}
static void saveTailsPercent(float val) {
  g_settings.rectParams.tailsPercent = val;
  normalizeRectFractions();
  NVSManager::saveSettings(g_settings);
}
static void saveFeedVolume(float val) {
  g_settings.rectParams.feedVolumeL = clampUiFloat(val, 1.0f, 250.0f);
  NVSManager::saveSettings(g_settings);
}
static void saveFeedAbv(float val) {
  g_settings.rectParams.feedAbvPercent = clampUiFloat(val, 1.0f, 96.0f);
  NVSManager::saveSettings(g_settings);
}
static void saveHeadsSpeed(float val) {
  g_settings.rectParams.headsSpeedMlHKw = val;
  NVSManager::saveSettings(g_settings);
}
static void saveBodySpeed(float val) {
  g_settings.rectParams.bodySpeedMlHKw = val;
  NVSManager::saveSettings(g_settings);
}
static void saveStabMin(float val) {
  g_settings.rectParams.stabilizationMin = (uint16_t)val;
  NVSManager::saveSettings(g_settings);
}
static void savePurgeMin(float val) {
  g_settings.rectParams.purgeMin = (uint16_t)val;
  NVSManager::saveSettings(g_settings);
}

static void persistDistUi() {
  g_settings.distillationUi.speedMlH = distUi.speedMlH;
  g_settings.distillationUi.headsVolumeMl = distUi.headsVolumeMl;
  g_settings.distillationUi.targetVolumeMl = distUi.targetVolumeMl;
  g_settings.distillationUi.endTempC = distUi.endTempC;
  g_settings.distillationUi.powerPercent = distUi.powerPercent;
  g_settings.distillationUi.tailsVolumeMl = distUi.tailsVolumeMl;
  NVSManager::saveSettings(g_settings);
}

static void applyDistUiRuntime() {
  FSM::Distillation::setParams(distUi.speedMlH, distUi.headsVolumeMl,
                               distUi.targetVolumeMl, distUi.endTempC);
  FSM::Distillation::setPowerPercent(static_cast<uint8_t>(distUi.powerPercent));
}

static void saveDistSpeed(float val) {
  distUi.speedMlH = val;
  applyDistUiRuntime();
  persistDistUi();
}
static void saveDistHeads(float val) {
  distUi.headsVolumeMl = val;
  applyDistUiRuntime();
  persistDistUi();
}
static void saveDistTarget(float val) {
  distUi.targetVolumeMl = val;
  applyDistUiRuntime();
  persistDistUi();
}
static void saveDistEndTemp(float val) {
  distUi.endTempC = val;
  applyDistUiRuntime();
  persistDistUi();
}
static void saveDistPower(float val) {
  if (val < 0.0f)
    val = 0.0f;
  if (val > 100.0f)
    val = 100.0f;
  distUi.powerPercent = val;
  applyDistUiRuntime();
  persistDistUi();
}
static void saveDistTails(float val) {
  distUi.tailsVolumeMl = val;
  persistDistUi();
}

static void saveManualRectSpeed(float val) {
  if (val < 0.0f)
    val = 0.0f;
  manualRectUi.speedMlH = val;
  if (g_state.mode == Mode::MANUAL_RECT) {
    if (val <= 0.0f)
      Pump::stop();
    else
      Pump::start(val);
  }
}
static void saveManualRectPower(float val) {
  if (val < 0.0f)
    val = 0.0f;
  if (val > 100.0f)
    val = 100.0f;
  manualRectUi.powerPercent = val;
  if (g_state.mode == Mode::MANUAL_RECT) {
    Heater::setPower(static_cast<uint8_t>(val));
  }
}
static void saveManualRectHeadsTarget(float val) {
  if (val < 0.0f)
    val = 0.0f;
  manualRectUi.headsTargetMl = val;
}
static void saveManualRectBodyTarget(float val) {
  if (val < 0.0f)
    val = 0.0f;
  manualRectUi.bodyTargetMl = val;
}
static void saveManualRectTailsTarget(float val) {
  if (val < 0.0f)
    val = 0.0f;
  manualRectUi.tailsTargetMl = val;
}

static void saveMashStepTemp(float val) {
  if (g_editMashStepIdx >= mashProfileDefault.stepCount)
    return;
  mashProfileDefault.steps[g_editMashStepIdx].temperature = val;
  if (g_state.mode == Mode::MASHING &&
      g_state.mashing.currentStep == g_editMashStepIdx) {
    g_state.mashing.targetTemp = val;
  }
}
static void saveMashStepDuration(float val) {
  if (g_editMashStepIdx >= mashProfileDefault.stepCount)
    return;
  if (val < 1.0f)
    val = 1.0f;
  mashProfileDefault.steps[g_editMashStepIdx].duration =
      static_cast<uint16_t>(val);
  if (g_state.mode == Mode::MASHING &&
      g_state.mashing.currentStep == g_editMashStepIdx) {
    g_state.mashing.stepDuration = static_cast<uint32_t>(val) * 60UL;
    g_state.mashing.tempInRange = false;
    g_state.mashing.inRangeStartTime = 0;
  }
}

static void saveHoldStepTemp(float val) {
  if (g_editHoldStepIdx >= holdStepsCount)
    return;
  holdStepsDefault[g_editHoldStepIdx].temperature = val;
  if (g_state.mode == Mode::HOLD &&
      g_state.hold.currentStep == g_editHoldStepIdx) {
    g_state.hold.targetTemp = val;
  }
}
static void saveHoldStepDuration(float val) {
  if (g_editHoldStepIdx >= holdStepsCount)
    return;
  if (val < 1.0f)
    val = 1.0f;
  holdStepsDefault[g_editHoldStepIdx].duration = static_cast<uint16_t>(val);
  if (g_state.mode == Mode::HOLD &&
      g_state.hold.currentStep == g_editHoldStepIdx) {
    g_state.hold.steps[g_editHoldStepIdx].duration = static_cast<uint16_t>(val);
    g_state.hold.tempInRange = false;
    g_state.hold.inRangeStartTime = 0;
  }
}

static void savePumpCal(float val) {
  g_settings.pumpCal.mlPerRevolution = val;
  NVSManager::saveSettings(g_settings);
  Pump::setCalibration(val);
}
static void saveManualHeater(float val) { Heater::setPower((uint8_t)val); }
static void saveManualPump(float val) {
  if (val <= 0)
    Pump::stop();
  else
    Pump::start(val);
}

static bool handleNavigationTap(int16_t tx, int16_t ty,
                                const SystemState &state) {
  if (ui.modeSwitchConfirm) {
    return false;
  }

  // Value edit uses the full bottom area for the Save action.
  // Do not let tab navigation intercept taps there.
  if (ui.currentScreen == UI_VALUE_EDIT) {
    return false;
  }

  // Кнопка НАЗАД (теперь в верхнем правом
  // углу на под-экранах)
  bool isRoot =
      (isMonitorRootScreen(ui.currentScreen) ||
       ui.currentScreen == UI_CONTROL || ui.currentScreen == UI_SETTINGS ||
       ui.currentScreen == UI_SERVICE);

  if (!isRoot && hit(tx, ty, TFT_WIDTH - 110, 0, 110, 50)) {
    popScreen();
    return true;
  }

  // Tabs
  if (ty >= (TFT_HEIGHT - UI_FOOTER_H)) {
    int tab = tx / (TFT_WIDTH / 4);
    if (tab >= 0 && tab < 4) {
      if (tab == 0) {
        switchRoot(isModeRunning(state) ? UI_MODE_MONITOR : UI_DASHBOARD);
      } else if (tab == 1) {
        switchRoot(UI_CONTROL);
      } else if (tab == 2) {
        switchRoot(UI_SETTINGS);
      } else {
        switchRoot(UI_SERVICE);
      }
    }
    return true;
  }

  return false;
}

static bool handleModeMonitorTap(int16_t tx, int16_t ty,
                                 const SystemState &state) {
  const bool ru = (g_settings.language == 0);
  const int16_t y0 = 56;
  const int16_t hTile = 48;
  const int16_t g = 6;
  const int16_t w3 = (TFT_WIDTH - 20 - g * 2) / 3;
  const int16_t x1 = 10;
  const int16_t x2 = x1 + w3 + g;
  const int16_t x3 = x2 + w3 + g;
  const int16_t y1 = y0 + hTile + g;
  const int16_t y2 = y1 + hTile + g;

  if (state.mode == Mode::DISTILLATION) {
    const int16_t g2 = 6;
    const int16_t w2 = (TFT_WIDTH - 20 - g2) / 2;
    const int16_t x2d = x1 + w2 + g2;
    const int16_t y1d = y0 + 48 + g2;
    if (hit(tx, ty, x2d, y0, w2, 74)) {
      openValueEdit(ru ? "Мощность дист." : "Dist power", distUi.powerPercent,
                    0, 100, 1, 10, saveDistPower, "%", 0, true);
      return true;
    }
    if (hit(tx, ty, x2d, y1d, w2, 74)) {
      openValueEdit(msg(Msg::END_TEMP), distUi.endTempC, 80, 100, 0.1f, 1.0f,
                    saveDistEndTemp, "C", 1, true);
      return true;
    }
    return false;
  }

  if (state.mode == Mode::MANUAL_RECT) {
    const int16_t leftX = 10;
    const int16_t leftW = 154;
    const int16_t rowH = 28;
    const int16_t rowGap = 4;
    for (uint8_t i = 0; i < 5; i++) {
      const int16_t ry = y0 + i * (rowH + rowGap);
      if (!hit(tx, ty, leftX, ry, leftW, rowH))
        continue;
      switch (i) {
      case 0:
        openValueEdit(ru ? "Скорость отбора" : "Takeoff speed",
                      manualRectUi.speedMlH, 0, 5000, 10, 100,
                      saveManualRectSpeed, "ml/h", 0, true);
        return true;
      case 1:
        openValueEdit(ru ? "Мощность руч." : "Manual power",
                      manualRectUi.powerPercent, 0, 100, 1, 10,
                      saveManualRectPower, "%", 0, true);
        return true;
      case 2:
        openValueEdit(ru ? "Головы цель" : "Heads target",
                      manualRectUi.headsTargetMl, 0, 10000, 10, 100,
                      saveManualRectHeadsTarget, "ml", 0, true);
        return true;
      case 3:
        openValueEdit(ru ? "Тело цель" : "Body target",
                      manualRectUi.bodyTargetMl, 0, 50000, 100, 1000,
                      saveManualRectBodyTarget, "ml", 0, true);
        return true;
      case 4:
        openValueEdit(ru ? "Хвосты цель" : "Tails target",
                      manualRectUi.tailsTargetMl, 0, 50000, 100, 1000,
                      saveManualRectTailsTarget, "ml", 0, true);
        return true;
      default:
        break;
      }
    }
    return false;
  }

  if (state.mode == Mode::MASHING) {
    const uint8_t steps = mashProfileDefault.stepCount;
    if (steps == 0)
      return false;
    const int16_t listX = 10;
    const int16_t listW = TFT_WIDTH - 20;
    const int16_t rowGap = 4;
    const int16_t listH = 156;
    int16_t rowH = (listH - (steps - 1) * rowGap) / steps;
    if (rowH < 28)
      rowH = 28;
    for (uint8_t i = 0; i < steps; i++) {
      const int16_t ry = y0 + i * (rowH + rowGap);
      if (!hit(tx, ty, listX, ry, listW, rowH))
        continue;
      g_editMashStepIdx = i;
      if (tx < (listX + listW / 2)) {
        openValueEdit(ru ? "Температура шага" : "Step temperature",
                      mashProfileDefault.steps[i].temperature, 30, 90, 0.1f,
                      1.0f, saveMashStepTemp, "C", 1, true);
      } else {
        openValueEdit(ru ? "Длительность шага" : "Step duration",
                      mashProfileDefault.steps[i].duration, 1, 240, 1, 10,
                      saveMashStepDuration, "min", 0, true);
      }
      return true;
    }
    return false;
  }

  if (state.mode == Mode::HOLD) {
    const uint8_t steps = holdStepsCount;
    if (steps == 0)
      return false;
    const int16_t listX = 10;
    const int16_t listW = TFT_WIDTH - 20;
    const int16_t rowGap = 4;
    const int16_t listH = 156;
    int16_t rowH = (listH - (steps - 1) * rowGap) / steps;
    if (rowH < 32)
      rowH = 32;
    for (uint8_t i = 0; i < steps; i++) {
      const int16_t ry = y0 + i * (rowH + rowGap);
      if (!hit(tx, ty, listX, ry, listW, rowH))
        continue;
      g_editHoldStepIdx = i;
      if (tx < (listX + listW / 2)) {
        openValueEdit(ru ? "Температура удерж." : "Hold temperature",
                      holdStepsDefault[i].temperature, 30, 95, 0.1f, 1.0f,
                      saveHoldStepTemp, "C", 1, true);
      } else {
        openValueEdit(ru ? "Время удерж." : "Hold duration",
                      holdStepsDefault[i].duration, 1, 720, 1, 15,
                      saveHoldStepDuration, "min", 0, true);
      }
      return true;
    }
    return false;
  }

  // Auto-rectification: keep current screen mostly read-only for now.
  return false;
}

static bool handleScreenTap(int16_t tx, int16_t ty, const SystemState &state) {
  switch (ui.currentScreen) {
  case UI_DASHBOARD:
    // Tap on temperature tiles goes to All Temps
    if (ty >= 56 && ty <= 202) {
      pushScreen(UI_ALL_TEMPS);
      return true;
    }
    // Tap on dashboard elsewhere goes to Control
    if (ty > UI_HEADER_H && ty < (TFT_HEIGHT - UI_FOOTER_H)) {
      switchRoot(UI_CONTROL);
      return true;
    }
    break;
  case UI_MODE_MONITOR:
    if (ty > UI_HEADER_H && ty < (TFT_HEIGHT - UI_FOOTER_H)) {
      if (handleModeMonitorTap(tx, ty, state)) {
        return true;
      }
      // Tap on temperature tiles (right side on monitor) goes to All Temps
      if (tx >= 236 && ty >= 56 && ty <= 202) {
        pushScreen(UI_ALL_TEMPS);
        return true;
      }
      // Fallback: tap on left status panel opens Control menu.
      if (tx < (TFT_WIDTH / 2)) {
        switchRoot(UI_CONTROL);
        return true;
      }
      return true;
    }
    break;

  case UI_CONTROL:
    if (ui.modeSwitchConfirm) {
      const int16_t mx = 30;
      const int16_t my = 78;
      const int16_t mw = TFT_WIDTH - 60;
      const int16_t by = my + 102;
      if (hit(tx, ty, mx + 20, by, 150, 42)) {
        ui.modeSwitchConfirm = false;
        return true;
      }
      if (hit(tx, ty, mx + mw - 170, by, 150, 42)) {
        const Mode target = ui.modeSwitchTarget;
        ui.modeSwitchConfirm = false;
        FSM::stopMode(g_state);
        startModeFromControl(target);
        return true;
      }
      return true;
    }

    {
      const int16_t stopX = TFT_WIDTH - CTRL_ACTION_BW - 10;
      const int16_t pauseX = stopX - CTRL_ACTION_BW - CTRL_ACTION_GAP;
      if (hit(tx, ty, pauseX, CTRL_ACTION_Y, CTRL_ACTION_BW, CTRL_ACTION_BH)) {
        if (state.mode != Mode::IDLE) {
          if (state.paused)
            FSM::resume(g_state);
          else
            FSM::pause(g_state);
        }
        return true;
      }
      if (hit(tx, ty, stopX, CTRL_ACTION_Y, CTRL_ACTION_BW, CTRL_ACTION_BH)) {
        if (state.mode != Mode::IDLE) {
          FSM::stopMode(g_state);
        }
        return true;
      }
    }

    if (hit(tx, ty, CTRL_X1, CTRL_Y1, CTRL_BW, CTRL_BH)) {
      startOrRequestMode(state, Mode::RECTIFICATION);
      return true;
    } else if (hit(tx, ty, CTRL_X2, CTRL_Y1, CTRL_BW, CTRL_BH)) {
      startOrRequestMode(state, Mode::DISTILLATION);
      return true;
    } else if (hit(tx, ty, CTRL_X1, CTRL_Y2, CTRL_BW, CTRL_BH)) {
      startOrRequestMode(state, Mode::MANUAL_RECT);
      return true;
    } else if (hit(tx, ty, CTRL_X2, CTRL_Y2, CTRL_BW, CTRL_BH)) {
      startOrRequestMode(state, Mode::MASHING);
      return true;
    } else if (hit(tx, ty, CTRL_X1, CTRL_Y3, CTRL_BW, CTRL_BH)) {
      startOrRequestMode(state, Mode::HOLD);
      return true;
    } else if (hit(tx, ty, CTRL_X2, CTRL_Y3, CTRL_BW, CTRL_BH)) {
      startOrRequestMode(state, Mode::NBK);
      return true;
    } else if (hit(tx, ty, CTRL_X1, CTRL_Y4, CTRL_BW, CTRL_BH)) {
      startOrRequestMode(state, Mode::FERMENTATION);
      return true;
    } else if (hit(tx, ty, CTRL_X2, CTRL_Y4, CTRL_BW, CTRL_BH)) {
      if (isManualAccessAllowed(state)) {
        pushScreen(UI_MANUAL);
      }
      return true;
    }
    break;

  case UI_VALUE_EDIT:
    if (hit(tx, ty, 10, 138, 108, 56)) {
      edit.value -= edit.fastStep;
      if (edit.value < edit.min)
        edit.value = edit.min;
      return true;
    } else if (hit(tx, ty, 124, 138, 108, 56)) {
      edit.value -= edit.step;
      if (edit.value < edit.min)
        edit.value = edit.min;
      return true;
    } else if (hit(tx, ty, 238, 138, 108, 56)) {
      edit.value += edit.step;
      if (edit.value > edit.max)
        edit.value = edit.max;
      return true;
    } else if (hit(tx, ty, 352, 138, 108, 56)) {
      edit.value += edit.fastStep;
      if (edit.value > edit.max)
        edit.value = edit.max;
      return true;
    } else if (hit(tx, ty, 10, 202, TFT_WIDTH - 20, 44)) {
      if (edit.onSave)
        edit.onSave(edit.value);
      if (edit.returnToModeMonitor && isModeRunning(g_state)) {
        switchRoot(UI_MODE_MONITOR);
      } else {
        popScreen();
      }
      return true;
    }
    break;

  case UI_SETTINGS:
    if (hit(tx, ty, 10, 53, 225, 63)) {
      pushScreen(UI_EQUIPMENT);
      return true;
    } else if (hit(tx, ty, 245, 53, 225, 63)) {
      pushScreen(UI_RECT_PARAMS);
      return true;
    } else if (hit(tx, ty, 10, 124, 225, 63)) {
      pushScreen(UI_DIST_PARAMS);
      return true;
    } else if (hit(tx, ty, 245, 124, 225, 63)) {
      pushScreen(UI_CALIBRATION);
      return true;
    } else if (hit(tx, ty, 10, 197, 145, 32)) {
      g_settings.theme = (g_settings.theme == 0) ? 1 : 0;
      NVSManager::saveSettings(g_settings);
      return true;
    } else if (hit(tx, ty, 165, 197, 145, 32)) {
      g_settings.soundEnabled = !g_settings.soundEnabled;
      NVSManager::saveSettings(g_settings);
      return true;
    } else if (hit(tx, ty, 320, 197, 145, 32)) {
      g_settings.language = (g_settings.language == 0) ? 1 : 0;
      NVSManager::saveSettings(g_settings);
      return true;
    }
    break;

  case UI_EQUIPMENT:
    if (hit(tx, ty, 10, 48, 225, 78)) {
        openValueEdit(msg(Msg::HEATER_POWER), g_settings.equipment.heaterPowerW,
                      1000, 10000, 100, 500, saveHeaterPower, "W", 0);
        return true;
    } else if (hit(tx, ty, 245, 48, 225, 78)) {
        openValueEdit(msg(Msg::COLUMN_HEIGHT),
                      g_settings.equipment.columnHeightMm, 500, 3000, 50, 200,
                      saveColumnHeight, "mm", 0);
        return true;
    } else if (hit(tx, ty, 10, 138, 225, 78)) {
        openValueEdit(msg(Msg::CUBE_VOLUME), g_settings.equipment.cubeVolumeL,
                      5, 200, 1, 10, saveCubeVolume, "L", 1);
        return true;
    } else if (hit(tx, ty, 245, 138, 225, 78)) {
        openValueEdit(msg(Msg::PACKING_COEFF),
                      g_settings.equipment.packingCoeff, 1, 15, 0.1, 1,
                      savePackingCoeff, "", 2);
        return true;
    }
    break;

  case UI_RECT_PARAMS:
    if (hit(tx, ty, 10, 48, 460, 26)) {
      rectParamsPage = (rectParamsPage == 0) ? 1 : 0;
      return true;
    }

    if (ty >= 82 && ty < 232) {
      const int16_t rowH = 46;
      const int16_t rowGap = 6;
      const int16_t rowPitch = rowH + rowGap;
      const int16_t row = (ty - 82) / rowPitch;
      const int16_t col = (tx >= 245) ? 1 : 0;
      if (row < 0 || row > 2) {
        break;
      }
      const int16_t rowTop = 82 + row * rowPitch;
      if (!hit(tx, ty, col == 0 ? 10 : 245, rowTop, 225, rowH)) {
        break;
      }
      const bool ru = (g_settings.language == 0);

      if (rectParamsPage == 0) {
        switch ((row * 2) + col) {
        case 0:
          applyRectFeedstockDefaultsAndSave(
              (g_settings.rectParams.feedstock + 1) % 8);
          return true;
        case 1:
          openValueEdit(ru ? "Объем СС" : "Feed volume",
                        g_settings.rectParams.feedVolumeL, 1.0f, 250.0f, 0.5f,
                        5.0f, saveFeedVolume, "L", 1);
          return true;
        case 2:
          openValueEdit(ru ? "Крепость СС" : "Feed ABV",
                        g_settings.rectParams.feedAbvPercent, 1.0f, 96.0f, 0.5f,
                        2.0f, saveFeedAbv, "%", 1);
          return true;
        case 3:
          openValueEdit(msg(Msg::HEADS_PERCENT),
                        g_settings.rectParams.headsPercent, 0, 25, 0.5f, 2.0f,
                        saveHeadsPercent, "%", 1);
          return true;
        case 4:
          openValueEdit(ru ? "Тело %" : "Body %",
                        g_settings.rectParams.bodyPercent, 40, 98, 0.5f, 2.0f,
                        saveBodyPercent, "%", 1);
          return true;
        case 5:
          openValueEdit(ru ? "Хвосты %" : "Tails %",
                        g_settings.rectParams.tailsPercent, 0, 40, 0.5f, 2.0f,
                        saveTailsPercent, "%", 1);
          return true;
        default:
          break;
        }
      } else {
        switch ((row * 2) + col) {
        case 0:
          openValueEdit(msg(Msg::HEADS_SPEED),
                        g_settings.rectParams.headsSpeedMlHKw, 10, 1000, 10,
                        100, saveHeadsSpeed, "ml/h/k", 0);
          return true;
        case 1:
          openValueEdit(msg(Msg::BODY_SPEED),
                        g_settings.rectParams.bodySpeedMlHKw, 50, 3000, 50, 200,
                        saveBodySpeed, "ml/h/k", 0);
          return true;
        case 2:
          openValueEdit(msg(Msg::STABILIZATION),
                        g_settings.rectParams.stabilizationMin, 1, 120, 1, 10,
                        saveStabMin, "min", 0);
          return true;
        case 3:
          openValueEdit(msg(Msg::PURGE_TIME), g_settings.rectParams.purgeMin, 1,
                        60, 1, 5, savePurgeMin, "min", 0);
          return true;
        default:
          break;
        }
      }
    }
    break;

  case UI_DIST_PARAMS:
    if (hit(tx, ty, 10, 48, 225, 78)) {
        openValueEdit(msg(Msg::DIST_SPEED), distUi.speedMlH, 50, 5000, 50, 500,
                      saveDistSpeed, "ml/h", 0);
        return true;
    } else if (hit(tx, ty, 245, 48, 225, 78)) {
        openValueEdit(msg(Msg::HEADS_VOLUME), distUi.headsVolumeMl, 0, 5000, 10,
                      100, saveDistHeads, "ml", 0);
        return true;
    } else if (hit(tx, ty, 10, 138, 225, 78)) {
        openValueEdit(msg(Msg::TARGET_VOLUME), distUi.targetVolumeMl, 0, 50000,
                      100, 1000, saveDistTarget, "ml", 0);
        return true;
    } else if (hit(tx, ty, 245, 138, 225, 78)) {
        openValueEdit(msg(Msg::END_TEMP), distUi.endTempC, 80, 100, 0.1, 1,
                      saveDistEndTemp, "C", 1);
        return true;
    }
    break;

  case UI_CALIBRATION:
    if (hit(tx, ty, 10, 52, 225, 86)) {
      openValueEdit(msg(Msg::PUMP_CALIBRATION),
                    g_settings.pumpCal.mlPerRevolution, 0.001, 5.0, 0.001, 0.05,
                    savePumpCal, "ml/r", 3);
      return true;
    } else if (hit(tx, ty, 245, 52, 225, 86)) {
      Display::startTouchCalibration();
      return true;
    }
    break;
  case UI_MANUAL:
    if (!isManualAccessAllowed(state)) {
      return true;
    }
    if (hit(tx, ty, 10, 48, 225, 86)) {
        openValueEdit(msg(Msg::HEATER_POWER), Heater::getPower(), 0, 100, 1, 10,
                      saveManualHeater, "%", 0);
        return true;
    } else if (hit(tx, ty, 245, 48, 225, 86)) {
        openValueEdit(msg(Msg::PUMP), state.pump.speedMlPerHour, 0, 5000, 10,
                      100, saveManualPump, "ml/h", 0);
        return true;
    }
    if (ty >= 146 && ty < 222) {
      if (hit(tx, ty, 10, 146, 145, 76)) {
        Valves::setWater(!Valves::getWater());
        return true;
      } else if (hit(tx, ty, 165, 146, 145, 76)) {
        Valves::setHeads(!Valves::getHeads());
        return true;
      } else if (hit(tx, ty, 320, 146, 145, 76)) {
        Valves::setUno(!Valves::getUno());
        return true;
      }
    }
    break;

  case UI_SERVICE:
    // Tap on diagnostic rows goes to All Temps
    if (ty >= 180 && ty <= 260) {
      pushScreen(UI_ALL_TEMPS);
      return true;
    }
    break;

  default:
    break;
  }

  return false;
}

static uint16_t colorBg() {
  return (g_settings.theme == 1) ? tft.color565(14, 16, 18)
                                 : tft.color565(206, 212, 216);
}

static uint16_t colorFg() {
  return (g_settings.theme == 1) ? TFT_WHITE : tft.color565(20, 24, 28);
}

static uint16_t colorAccent() { return COLOR_PRIMARY; }

static uint16_t colorCard() {
  return (g_settings.theme == 1) ? tft.color565(28, 32, 36)
                                 : tft.color565(232, 236, 240);
}

static uint16_t colorBorder() {
  return (g_settings.theme == 1) ? tft.color565(116, 122, 130)
                                 : tft.color565(88, 96, 104);
}

static uint16_t colorMuted() {
  return (g_settings.theme == 1) ? tft.color565(172, 178, 184)
                                 : tft.color565(76, 82, 88);
}

static uint16_t colorNavBg() {
  return (g_settings.theme == 1) ? tft.color565(8, 10, 12)
                                 : tft.color565(182, 190, 196);
}

static uint16_t colorNavInactive() {
  return (g_settings.theme == 1) ? tft.color565(40, 44, 50)
                                 : tft.color565(156, 164, 170);
}

static uint16_t colorSoftFill() {
  return (g_settings.theme == 1) ? tft.color565(58, 62, 68)
                                 : tft.color565(214, 220, 224);
}

static uint16_t colorButtonBody() {
  return (g_settings.theme == 1) ? tft.color565(22, 24, 28)
                                 : tft.color565(78, 84, 90);
}

static void clearRow(int16_t y, int16_t h = 24) {
  tft.fillRect(10, y - 2, TFT_WIDTH - 20, h + 4, colorBg());
}

static void drawCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t bg);

static void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h,
                            uint8_t percent, uint16_t fill) {
  if (w <= 0 || h <= 0)
    return;
  tft.fillRect(x, y, w, h, colorNavInactive());
  tft.drawRect(x, y, w, h, colorBorder());
  if (w > 2 && h > 2) {
    tft.drawRect(x + 1, y + 1, w - 2, h - 2, colorBg());
  }
  if (percent == 0)
    return;
  const int16_t innerW = (w > 4) ? (w - 4) : 0;
  const int16_t innerH = (h > 4) ? (h - 4) : 0;
  const uint8_t cappedPercent = (percent > 100) ? 100 : percent;
  const int16_t fillW = (innerW * cappedPercent) / 100;
  if (fillW > 0 && innerH > 0) {
    tft.fillRect(x + 2, y + 2, fillW, innerH, fill);
  }
}

static size_t utf8PrevCodepointStart(const char *text, size_t end) {
  if (text == nullptr || end == 0)
    return 0;
  size_t pos = end - 1;
  while (pos > 0 && ((static_cast<uint8_t>(text[pos]) & 0xC0u) == 0x80u)) {
    pos--;
  }
  return pos;
}

static void copyFittedText(const char *text, int16_t maxWidth, char *out,
                           size_t outSize, const char *suffix = "...") {
  if (out == nullptr || outSize == 0) {
    return;
  }
  out[0] = '\0';
  if (text == nullptr || text[0] == '\0' || maxWidth <= 0) {
    return;
  }

  strncpy(out, text, outSize - 1);
  out[outSize - 1] = '\0';
  if (tft.textWidth(out) <= maxWidth) {
    return;
  }

  const int16_t suffixW = tft.textWidth(suffix);
  if (suffixW >= maxWidth) {
    return;
  }

  char scratch[256];
  strncpy(scratch, out, sizeof(scratch) - 1);
  scratch[sizeof(scratch) - 1] = '\0';

  size_t len = strlen(scratch);
  while (len > 0) {
    len = utf8PrevCodepointStart(scratch, len);
    scratch[len] = '\0';

    char candidate[256];
    snprintf(candidate, sizeof(candidate), "%s%s", scratch, suffix);
    if (tft.textWidth(candidate) <= maxWidth) {
      strncpy(out, candidate, outSize - 1);
      out[outSize - 1] = '\0';
      return;
    }
  }
}

static void drawStateBadge(int16_t x, int16_t y, int16_t w, int16_t h,
                           const char *label, uint16_t bg) {
  tft.fillRect(x, y, w, h, bg);
  tft.drawRect(x, y, w, h, colorBorder());
  if (w > 8 && h > 4) {
    tft.fillRect(x + 1, y + 1, 6, h - 2, colorNavBg());
  }
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextDatum(middle_center);
  char labelBuf[32];
  copyFittedText(label, w - 16, labelBuf, sizeof(labelBuf));
  tft.drawString(labelBuf, x + w / 2, y + h / 2);
  tft.setTextDatum(top_left);
}

static const char *getDisplayModeName(Mode mode) {
  const bool ru = (g_settings.language == 0);
  switch (mode) {
  case Mode::RECTIFICATION:
    return ru ? "Ректиф." : "Rect";
  case Mode::DISTILLATION:
    return ru ? "Дистил." : "Distill";
  case Mode::MANUAL_RECT:
    return ru ? "Ручн. рект." : "Man Rect";
  case Mode::MASHING:
    return ru ? "Затирка" : "Mash";
  case Mode::HOLD:
    return ru ? "Пастер." : "Hold";
  case Mode::NBK:
    return "NBK";
  case Mode::FERMENTATION:
    return ru ? "Брожение" : "Ferment";
  case Mode::IDLE:
  default:
    return ru ? "Ожидание" : "Idle";
  }
}

static const char *getDisplayRectPhaseName(RectPhase phase) {
  const bool ru = (g_settings.language == 0);
  switch (phase) {
  case RectPhase::HEATING:
    return ru ? "Нагрев" : "Heating";
  case RectPhase::STABILIZATION:
    return ru ? "Стаб." : "Stabil.";
  case RectPhase::HEADS:
    return ru ? "Головы" : "Heads";
  case RectPhase::POST_HEADS_STABILIZATION:
    return ru ? "Стаб. 2" : "Stabil. 2";
  case RectPhase::BODY:
    return ru ? "Тело" : "Body";
  case RectPhase::TAILS:
    return ru ? "Хвосты" : "Tails";
  case RectPhase::PURGE:
    return ru ? "Продув" : "Purge";
  case RectPhase::FINISH:
    return ru ? "Финиш" : "Finish";
  case RectPhase::COMPLETED:
    return ru ? "Готово" : "Done";
  case RectPhase::IDLE:
  default:
    return ru ? "Ожид." : "Idle";
  }
}

static const char *getDisplayNbkPhaseName(NbkPhase phase) {
  const bool ru = (g_settings.language == 0);
  switch (phase) {
  case NbkPhase::HEATING:
    return ru ? "Нагрев" : "Heating";
  case NbkPhase::STABILIZATION:
    return ru ? "Старт" : "Start";
  case NbkPhase::WORKING:
    return ru ? "Работа" : "Run";
  case NbkPhase::FINISH:
    return ru ? "Финиш" : "Finish";
  case NbkPhase::COMPLETED:
    return ru ? "Готово" : "Done";
  case NbkPhase::IDLE:
  default:
    return ru ? "Ожид." : "Idle";
  }
}

static const char *getDisplayFermPhaseName(FermentationPhase phase) {
  const bool ru = (g_settings.language == 0);
  switch (phase) {
  case FermentationPhase::RUNNING:
    return ru ? "Работа" : "Run";
  case FermentationPhase::COMPLETED:
    return ru ? "Готово" : "Done";
  case FermentationPhase::IDLE:
  default:
    return ru ? "Ожид." : "Idle";
  }
}

static const char *getDisplayPhaseName(const SystemState &state) {
  const bool ru = (g_settings.language == 0);
  static char holdBuf[20];
  switch (state.mode) {
  case Mode::RECTIFICATION:
  case Mode::DISTILLATION:
  case Mode::MANUAL_RECT:
    return getDisplayRectPhaseName(state.rectPhase);
  case Mode::MASHING:
    if (state.mashing.stepName[0] != '\0')
      return state.mashing.stepName;
    return ru ? "Шаг затирки" : "Mash step";
  case Mode::HOLD:
    if (state.hold.active) {
      snprintf(holdBuf, sizeof(holdBuf), ru ? "Шаг %u" : "Step %u",
               static_cast<unsigned>(state.hold.currentStep + 1));
      return holdBuf;
    }
    return ru ? "Ожид." : "Idle";
  case Mode::NBK:
    return getDisplayNbkPhaseName(state.nbkPhase);
  case Mode::FERMENTATION:
    return getDisplayFermPhaseName(state.fermPhase);
  case Mode::IDLE:
  default:
    return ru ? "Ожид." : "Idle";
  }
}

static void drawHeader(const char *title, bool showBack) {
  if (!showBack)
    return; // Убираем дублирующий тулбар на
            // главных экранах

  // Отрисовываем только на под-экранах
  tft.fillRect(0, 0, TFT_WIDTH, UI_HEADER_H, colorNavBg());
  tft.fillRect(0, 0, TFT_WIDTH, 4, colorAccent());
  tft.drawFastHLine(0, UI_HEADER_H - 1, TFT_WIDTH, colorBorder());
  tft.drawFastHLine(0, UI_HEADER_H - 2, TFT_WIDTH, colorAccent());

  int16_t bw = 110;
  int16_t bh = UI_HEADER_H - 10;
  const int16_t bx = TFT_WIDTH - bw - 5;
  const int16_t by = (UI_HEADER_H - bh) / 2;

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextDatum(middle_left);
  char titleBuf[96];
  copyFittedText(title, bx - 26, titleBuf, sizeof(titleBuf));
  tft.drawString(titleBuf, 14, UI_HEADER_H / 2);
  tft.fillRect(bx, by, bw, bh, colorButtonBody());
  tft.drawRect(bx, by, bw, bh, colorBorder());
  tft.fillRect(bx + 2, by + 2, 8, bh - 4, colorAccent());
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(middle_center);
  char backBuf[32];
  copyFittedText(msg(Msg::BTN_BACK), bw - 20, backBuf, sizeof(backBuf));
  tft.drawString(backBuf, bx + (bw / 2) + 4, UI_HEADER_H / 2);

  tft.setTextDatum(top_left);
}

static void drawTabs(UiScreen current) {
  const bool ru = (g_settings.language == 0);
  const char *labels[4] = {ru ? "МОНИТОР" : "DASH", ru ? "УПРАВЛ" : "CTRL",
                           ru ? "НАСТРОЙ" : "SET", ru ? "СЕРВИС" : "INFO"};

  const int16_t navY = TFT_HEIGHT - UI_FOOTER_H;
  const int16_t gap = 4;
  const int16_t bw = (TFT_WIDTH - (gap * 5)) / 4;
  const int16_t bh = UI_FOOTER_H - 12;

  tft.fillRect(0, navY, TFT_WIDTH, UI_FOOTER_H, colorNavBg());
  tft.drawFastHLine(0, navY, TFT_WIDTH, colorBorder());
  tft.drawFastHLine(0, navY + 1, TFT_WIDTH, colorAccent());

  for (int i = 0; i < 4; i++) {
    const int16_t x = gap + i * (bw + gap);
    const int16_t y = navY + 6;
    bool active = false;
    if (i == 0)
      active = isMonitorRootScreen(current);
    else if (i == 1)
      active = (current == UI_CONTROL);
    else if (i == 2)
      active = (current == UI_SETTINGS);
    else
      active = (current == UI_SERVICE);
    const uint16_t bg = active ? colorButtonBody() : colorNavInactive();
    const uint16_t fg = active ? TFT_WHITE : colorFg();

    tft.fillRect(x, y, bw, bh, bg);
    tft.drawRect(x, y, bw, bh, colorBorder());
    if (active) {
      tft.fillRect(x + 2, y + 2, bw - 4, 7, colorAccent());
    } else {
      tft.fillRect(x + 2, y + 2, bw - 4, 5, colorSoftFill());
    }

    tft.setTextColor(fg);
    tft.setTextSize(1);
    tft.setFont(&fonts::efontJA_16);
    tft.setTextDatum(middle_center);
    char tabBuf[24];
    copyFittedText(labels[i], bw - 20, tabBuf, sizeof(tabBuf));
    tft.drawString(tabBuf, x + bw / 2, y + (bh / 2) + 5);

    // Quick status dots to improve at-a-glance readability.
    if (i == 1 && g_state.mode != Mode::IDLE) {
      const uint16_t dot = g_state.paused ? COLOR_WARNING : COLOR_SUCCESS;
      tft.fillRect(x + bw - 12, y + 11, 7, 7, dot);
    }
    if (i == 3 && !g_state.safetyOk) {
      tft.fillRect(x + bw - 12, y + 11, 7, 7, COLOR_DANGER);
    }
  }

  tft.setFont(&fonts::efontJA_16);
  tft.setTextDatum(top_left);
}

static void drawValueRow(int16_t y, const char *label, const char *value,
                         bool highlighted = true) {
  const int16_t rowX = 10;
  const int16_t rowY = y - 8;
  const int16_t rowW = TFT_WIDTH - 20;
  const int16_t rowH = 30;
  const uint16_t accent = highlighted ? colorAccent() : colorNavInactive();

  drawCard(rowX, rowY, rowW, rowH, colorCard());
  tft.fillRect(rowX + 1, rowY + 1, 6, rowH - 2, accent);
  tft.drawFastHLine(rowX + 8, rowY + rowH - 1, rowW - 9, colorBorder());
  tft.setTextColor(colorFg());
  tft.setTextSize(1);

  uint16_t tw = tft.textWidth(value);
  int16_t boxW = (tw < 92) ? 92 : tw + 22;
  int16_t boxX = rowX + rowW - boxW - 10;
  const int16_t boxY = rowY + 4;
  const int16_t boxH = rowH - 8;
  const int16_t labelMaxW = boxX - rowX - 22;
  char labelBuf[96];
  tft.setTextDatum(middle_left);
  copyFittedText(label, labelMaxW, labelBuf, sizeof(labelBuf));
  tft.drawString(labelBuf, rowX + 14, rowY + (rowH / 2) + 1);

  if (highlighted) {
    tft.fillRect(boxX, boxY, boxW, boxH, colorButtonBody());
    tft.drawRect(boxX, boxY, boxW, boxH, colorBorder());
    tft.fillRect(boxX + 2, boxY + 2, boxW - 4, 5, colorAccent());
    tft.setTextColor(TFT_WHITE);
  } else {
    tft.fillRect(boxX, boxY, boxW, boxH, colorBg());
    tft.drawRect(boxX, boxY, boxW, boxH, colorBorder());
    tft.fillRect(boxX + 2, boxY + 2, 6, boxH - 4, colorSoftFill());
    tft.setTextColor(colorAccent());
  }

  tft.setTextDatum(middle_center);
  char valueBuf[48];
  copyFittedText(value, boxW - 16, valueBuf, sizeof(valueBuf));
  tft.drawString(valueBuf, boxX + boxW / 2, boxY + (boxH / 2) + 1);
  tft.setTextDatum(top_left);
  tft.setTextColor(colorFg());
}

static void drawButton(int16_t x, int16_t y, int16_t w, int16_t h,
                       const char *label, uint16_t bg, uint16_t fg) {
  const uint16_t body = colorButtonBody();
  const int16_t stripeH = (h >= 44) ? 8 : 6;
  const int16_t dividerY = y + stripeH + 2;
  uint8_t labelSize = (w >= 140 && h >= 42) ? 2 : 1;

  tft.fillRect(x, y, w, h, body);
  tft.drawRect(x, y, w, h, colorBorder());
  if (w > 2 && h > 2) {
    tft.drawRect(x + 1, y + 1, w - 2, h - 2, colorBg());
  }
  tft.fillRect(x + 2, y + 2, w - 4, stripeH, bg);
  tft.drawFastHLine(x + 2, dividerY, w - 4, colorBorder());

  tft.setTextColor((g_settings.theme == 1) ? fg : TFT_WHITE);
  tft.setTextSize(labelSize);
  if (tft.textWidth(label) > (w - 20) && labelSize > 1) {
    labelSize = 1;
    tft.setTextSize(labelSize);
  }
  tft.setTextDatum(middle_center);
  char labelBuf[64];
  copyFittedText(label, w - 18, labelBuf, sizeof(labelBuf));
  tft.drawString(labelBuf, x + w / 2, y + (h / 2) + ((labelSize > 1) ? 6 : 5));
  tft.setTextDatum(top_left);
  tft.setTextSize(1);
}

static void drawCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t bg) {
  tft.fillRect(x, y, w, h, bg);
  tft.drawRect(x, y, w, h, colorBorder());
  if (w > 2 && h > 2) {
    tft.drawRect(x + 1, y + 1, w - 2, h - 2, colorBg());
  }
}

static void drawPanelHeader(int16_t x, int16_t y, int16_t w, const char *title,
                            uint16_t bg, uint16_t fg = TFT_WHITE) {
  tft.fillRect(x + 1, y + 1, w - 2, 24, colorNavBg());
  tft.fillRect(x + 1, y + 1, 8, 24, bg);
  tft.drawFastHLine(x + 1, y + 23, w - 2, colorBorder());
  tft.setTextColor(fg);
  tft.setTextSize(1);
  tft.setTextDatum(middle_left);
  char titleBuf[96];
  copyFittedText(title, w - 24, titleBuf, sizeof(titleBuf));
  tft.drawString(titleBuf, x + 14, y + 12);
  tft.setTextDatum(top_left);
}

static void drawCompactKeyValueRow(int16_t x, int16_t y, int16_t w,
                                   const char *label, const char *value,
                                   uint16_t valueColor = COLOR_PRIMARY) {
  tft.setTextColor(colorMuted());
  tft.setTextSize(1);
  tft.setTextDatum(middle_left);
  char labelBuf[64];
  char valueBuf[48];
  copyFittedText(label, (w / 2) - 8, labelBuf, sizeof(labelBuf));
  copyFittedText(value, (w / 2) - 8, valueBuf, sizeof(valueBuf));
  tft.drawString(labelBuf, x, y);
  tft.setTextColor(valueColor);
  tft.setTextDatum(middle_right);
  tft.drawString(valueBuf, x + w, y);
  tft.setTextDatum(top_left);
}

static void drawValueTileShell(int16_t x, int16_t y, int16_t w, int16_t h,
                               const char *label) {
  drawCard(x, y, w, h, colorCard());
  tft.fillRect(x + 1, y + 1, w - 2, 18, colorNavBg());
  tft.fillRect(x + 1, y + 1, 6, 18, colorAccent());
  tft.drawFastHLine(x + 1, y + 19, w - 2, colorBorder());
  tft.setTextColor(colorMuted());
  tft.setTextSize(1);
  tft.setTextDatum(top_left);
  char labelBuf[48];
  copyFittedText(label, w - 18, labelBuf, sizeof(labelBuf));
  tft.drawString(labelBuf, x + 12, y + 5);
  tft.setTextDatum(top_left);
}

static void drawFooterHint(const char *text, uint16_t tone = COLOR_INFO) {
  if (text == nullptr || text[0] == '\0')
    return;
  const int16_t x = 10;
  const int16_t y = TFT_HEIGHT - UI_FOOTER_H - 18;
  const int16_t w = TFT_WIDTH - 20;
  const int16_t h = 16;
  drawCard(x, y, w, h, colorCard());
  tft.fillRect(x + 1, y + 1, 6, h - 2, tone);
  tft.setTextColor(colorMuted());
  tft.setTextSize(1);
  tft.setTextDatum(middle_center);
  char hintBuf[120];
  copyFittedText(text, w - 20, hintBuf, sizeof(hintBuf));
  tft.drawString(hintBuf, TFT_WIDTH / 2, y + (h / 2) + 1);
  tft.setTextDatum(top_left);
}

static int16_t drawWrappedTextBlock(int16_t x, int16_t y, int16_t w,
                                    const char *text, uint16_t color,
                                    uint8_t textSize = 1,
                                    uint8_t maxLines = 8,
                                    int16_t lineGap = 3) {
  if (text == nullptr || text[0] == '\0' || w <= 0 || maxLines == 0)
    return y;

  char source[320];
  strncpy(source, text, sizeof(source) - 1);
  source[sizeof(source) - 1] = '\0';

  const int16_t centerX = x + (w / 2);
  const int16_t lineH = 12 + (textSize * 8);
  uint8_t linesDrawn = 0;

  tft.setTextColor(color);
  tft.setTextSize(textSize);
  tft.setTextDatum(top_center);

  char *paragraphCtx = nullptr;
  for (char *paragraph = strtok_r(source, "\n", &paragraphCtx);
       paragraph != nullptr && linesDrawn < maxLines;
       paragraph = strtok_r(nullptr, "\n", &paragraphCtx)) {
    char paragraphBuf[320];
    strncpy(paragraphBuf, paragraph, sizeof(paragraphBuf) - 1);
    paragraphBuf[sizeof(paragraphBuf) - 1] = '\0';

    char line[200] = {0};
    char *wordCtx = nullptr;
    for (char *word = strtok_r(paragraphBuf, " ", &wordCtx); word != nullptr;
         word = strtok_r(nullptr, " ", &wordCtx)) {
      char candidate[200];
      if (line[0] == '\0') {
        char fittedWord[128];
        copyFittedText(word, w - 4, fittedWord, sizeof(fittedWord));
        snprintf(candidate, sizeof(candidate), "%s", fittedWord);
      } else {
        snprintf(candidate, sizeof(candidate), "%s %s", line, word);
      }

      if (line[0] != '\0' && tft.textWidth(candidate) > (w - 4)) {
        tft.drawString(line, centerX, y + linesDrawn * (lineH + lineGap));
        linesDrawn++;
        if (linesDrawn >= maxLines)
          break;
        strncpy(line, word, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
      } else {
        strncpy(line, candidate, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
      }
    }

    if (linesDrawn >= maxLines)
      break;
    if (line[0] != '\0') {
      tft.drawString(line, centerX, y + linesDrawn * (lineH + lineGap));
      linesDrawn++;
    }
  }

  tft.setTextDatum(top_left);
  tft.setTextSize(1);
  return y + linesDrawn * (lineH + lineGap);
}

static void drawFullscreenOverlay(const char *title, const char *message,
                                  uint16_t tone, const char *footer = nullptr,
                                  uint8_t messageSize = 1) {
  const int16_t panelX = 18;
  const int16_t panelY = 18;
  const int16_t panelW = TFT_WIDTH - 36;
  const int16_t panelH = TFT_HEIGHT - 36;
  const int16_t bodyX = panelX + 18;
  const int16_t bodyY = panelY + 44;
  const int16_t bodyW = panelW - 36;
  const int16_t bodyH = panelH - 68;

  tft.fillScreen(colorNavBg());
  tft.fillRect(0, 0, TFT_WIDTH, 6, tone);
  drawCard(panelX, panelY, panelW, panelH, colorCard());
  drawPanelHeader(panelX, panelY, panelW, title, tone);
  drawCard(bodyX, bodyY, bodyW, bodyH, colorBg());
  tft.fillRect(bodyX + 1, bodyY + 1, 8, bodyH - 2, tone);

  int16_t textBottom = drawWrappedTextBlock(bodyX + 28, bodyY + 22, bodyW - 56,
                                            message, colorFg(), messageSize, 8, 5);
  if (footer != nullptr && footer[0] != '\0') {
    const int16_t footerY =
        (textBottom + 18 > bodyY + bodyH - 34) ? (bodyY + bodyH - 30)
                                               : (textBottom + 18);
    drawWrappedTextBlock(bodyX + 28, footerY, bodyW - 56, footer, colorMuted(),
                         1, 3, 3);
  }
}

static void drawModeSwitchOverlay(const SystemState &state, bool ru) {
  const int16_t mx = 30;
  const int16_t my = 78;
  const int16_t mw = TFT_WIDTH - 60;
  const int16_t mh = 150;
  const int16_t bodyX = mx + 16;
  const int16_t bodyY = my + 34;
  const int16_t bodyW = mw - 32;
  const int16_t bodyH = 54;
  const int16_t by = my + 102;

  drawCard(mx, my, mw, mh, colorCard());
  drawPanelHeader(mx, my, mw, ru ? "СМЕНА РЕЖИМА" : "SWITCH MODE",
                  COLOR_WARNING);
  drawCard(bodyX, bodyY, bodyW, bodyH, colorBg());
  tft.fillRect(bodyX + 1, bodyY + 1, 8, bodyH - 2, COLOR_WARNING);

  char cur[64];
  char next[64];
  char overlayText[160];
  snprintf(cur, sizeof(cur), ru ? "Сейчас: %s" : "Current: %s",
           getDisplayModeName(state.mode));
  snprintf(next, sizeof(next), ru ? "Перейти: %s ?" : "Switch to: %s ?",
           getDisplayModeName(ui.modeSwitchTarget));
  snprintf(overlayText, sizeof(overlayText), "%s\n%s", cur, next);
  drawWrappedTextBlock(bodyX + 24, bodyY + 10, bodyW - 48, overlayText,
                       colorFg(), 1, 4, 4);

  drawButton(mx + 20, by, 150, 42, ru ? "ОТМЕНА" : "CANCEL", COLOR_DARK_GREY,
             TFT_WHITE);
  drawButton(mx + mw - 170, by, 150, 42, ru ? "ПЕРЕЙТИ" : "SWITCH",
             COLOR_DANGER, TFT_WHITE);
}

static void drawValueTileValue(int16_t x, int16_t y, int16_t w, int16_t h,
                               const char *value, const char *unit,
                               uint16_t color) {
  // Clear only value area to avoid visible full-tile flicker on periodic
  // updates.
  const int16_t valueAreaY = y + 20;
  const int16_t valueAreaH = h - 22;
  if (valueAreaH > 0) {
    tft.fillRect(x + 2, valueAreaY, w - 4, valueAreaH, colorCard());
  }

  uint8_t valueSize = (w >= 135 && h >= 64) ? 3 : 2;
  tft.setTextSize(valueSize);
  if (tft.textWidth(value) > (w - 18) && valueSize > 1) {
    valueSize--;
  }

  tft.setTextColor(color);
  tft.setTextSize(valueSize);

  if (h < 48) {
    // Small tiles: value+unit right-aligned in tile.
    // Center value vertically within the tile boundaries
    const int16_t valueY = y + h / 2;
    const int16_t rightPad = x + w - 4;
    tft.setTextSize(1);
    const int16_t unitW = tft.textWidth(unit);
    tft.setTextColor(tft.color565(100, 100, 100));
    tft.setTextDatum(middle_right);
    tft.drawString(unit, rightPad, valueY);
    tft.setTextColor(color);
    tft.setTextSize(valueSize);
    tft.setTextDatum(middle_right);
    tft.drawString(value, rightPad - unitW - 2, valueY);
  } else {
    tft.setTextDatum(middle_center);
    const int16_t valueX = x + w / 2;
    const int16_t valueY = y + h / 2 + ((valueSize >= 3) ? 5 : 3);
    tft.drawString(value, valueX, valueY);
    if (valueSize >= 3) {
      // Slight overdraw to visually thicken key numbers without changing fonts.
      tft.drawString(value, valueX + 1, valueY);
    }

    tft.setTextColor(tft.color565(100, 100, 100));
    tft.setTextSize(1);
    tft.setTextDatum(bottom_right);
    tft.drawString(unit, x + w - 8, y + h - 8);
  }

  tft.setTextDatum(top_left);
}

static void drawValueTile(int16_t x, int16_t y, int16_t w, int16_t h,
                          const char *label, const char *value,
                          const char *unit, uint16_t color) {
  drawValueTileShell(x, y, w, h, label);
  drawValueTileValue(x, y, w, h, value, unit, color);
}

static void formatUptimeCompact(uint32_t uptimeSec, char *out, size_t outSize) {
  const uint32_t h = uptimeSec / 3600UL;
  const uint32_t m = (uptimeSec % 3600UL) / 60UL;
  const uint32_t s = uptimeSec % 60UL;
  snprintf(out, outSize, "%02lu:%02lu:%02lu", (unsigned long)h,
           (unsigned long)m, (unsigned long)s);
}

static void formatDurationCompact(uint32_t sec, char *out, size_t outSize) {
  const uint32_t h = sec / 3600UL;
  const uint32_t m = (sec % 3600UL) / 60UL;
  const uint32_t s = sec % 60UL;
  if (h > 0) {
    snprintf(out, outSize, "%02lu:%02lu:%02lu", (unsigned long)h,
             (unsigned long)m, (unsigned long)s);
  } else {
    snprintf(out, outSize, "%02lu:%02lu", (unsigned long)m, (unsigned long)s);
  }
}

static uint32_t getModeRunElapsedSec(const SystemState &state) {
  if (state.mode != g_modeRuntimeMode) {
    g_modeRuntimeMode = state.mode;
    g_modeRuntimeStartUptime = state.uptime;
  }
  if (state.mode == Mode::IDLE) {
    return 0;
  }
  if (state.uptime < g_modeRuntimeStartUptime) {
    g_modeRuntimeStartUptime = state.uptime;
    return 0;
  }
  return state.uptime - g_modeRuntimeStartUptime;
}

struct RootHeaderState {
  char status[64] = {0};
  char timer[32] = {0};
  const char *procState = nullptr;
  const char *safetyState = nullptr;
  uint16_t procColor = COLOR_INFO;
  uint16_t safetyColor = COLOR_SUCCESS;
  uint8_t phaseProgress = 0;
};

static RootHeaderState buildRootHeaderState(
    const SystemState &state,
    uint32_t phaseElapsedOverrideSec = 0xFFFFFFFFUL) {
  const bool ru = (g_settings.language == 0);
  RootHeaderState header;
  if (state.mode == Mode::IDLE) {
    snprintf(header.status, sizeof(header.status), "%s",
             getDisplayModeName(state.mode));
  } else {
    snprintf(header.status, sizeof(header.status), "%s / %s",
             getDisplayModeName(state.mode), getDisplayPhaseName(state));
  }

  header.procState =
      (state.mode == Mode::IDLE)
          ? (ru ? "ГОТОВ" : "READY")
          : (state.paused ? (ru ? "ПАУЗА" : "PAUSE")
                          : (ru ? "РАБОТА" : "RUN"));
  header.procColor = (state.mode == Mode::IDLE)
                         ? COLOR_INFO
                         : (state.paused ? COLOR_WARNING : COLOR_SUCCESS);
  header.safetyState =
      state.safetyOk ? (ru ? "НОРМА" : "SAFE") : (ru ? "АВАРИЯ" : "ALARM");
  header.safetyColor = state.safetyOk ? COLOR_SUCCESS : COLOR_DANGER;

  const uint32_t phaseElapsedSec =
      (phaseElapsedOverrideSec == 0xFFFFFFFFUL) ? FSM::getPhaseElapsedSec()
                                                : phaseElapsedOverrideSec;
  const uint32_t phaseTargetSec = FSM::getPhaseTargetSec(state, g_settings);
  header.phaseProgress = FSM::getPhaseProgressPercent(state, g_settings);

  char elapsedBuf[16];
  char targetBuf[16];
  formatDurationCompact(phaseElapsedSec, elapsedBuf, sizeof(elapsedBuf));
  if (phaseTargetSec > 0) {
    formatDurationCompact(phaseTargetSec, targetBuf, sizeof(targetBuf));
    snprintf(header.timer, sizeof(header.timer), "%s %s/%s",
             ru ? "Фаза" : "Phase", elapsedBuf, targetBuf);
  } else {
    snprintf(header.timer, sizeof(header.timer), "%s %s",
             ru ? "Фаза" : "Phase", elapsedBuf);
  }
  return header;
}

static void drawRootScaffold(UiScreen screen) {
  tft.fillScreen(colorBg());
  drawHeader(msg(Msg::MONITOR), false);
  drawTabs(screen);
  drawCard(10, ROOT_STATUS_Y, TFT_WIDTH - 20, ROOT_STATUS_H, colorCard());
  drawCard(10, ROOT_INFO_Y, TFT_WIDTH - 20, ROOT_INFO_H, colorCard());
}

static void renderRootStatusBar(const RootHeaderState &header, bool full) {
  const int16_t statusX = 20;
  const int16_t statusY = ROOT_STATUS_Y + 5;
  const int16_t statusW = 300;
  const int16_t statusH = 34;
  const int16_t badgeX = 332;
  const int16_t badgeW = 128;
  const int16_t badgeH = 14;

  if (full || strcmp(g_dashboardCache.status, header.status) != 0 ||
      strcmp(g_dashboardCache.phaseTimer, header.timer) != 0 ||
      g_dashboardCache.phaseProgress != header.phaseProgress) {
    if (!full) {
      tft.fillRect(statusX, statusY, statusW, statusH, colorCard());
    }
    tft.setTextColor(colorAccent());
    tft.setTextSize(1);
    tft.setFont(&fonts::efontJA_16);
    tft.setTextDatum(top_left);
    char statusBuf[64];
    char timerBuf[32];
    copyFittedText(header.status, statusW - 6, statusBuf, sizeof(statusBuf));
    copyFittedText(header.timer, statusW - 6, timerBuf, sizeof(timerBuf));
    tft.drawString(statusBuf, statusX + 2, statusY + 1);
    tft.setTextColor(colorMuted());
    tft.drawString(timerBuf, statusX + 2, statusY + 14);
    drawProgressBar(statusX + 2, statusY + 27, statusW - 6, 6,
                    header.phaseProgress, colorAccent());

    strncpy(g_dashboardCache.status, header.status,
            sizeof(g_dashboardCache.status));
    g_dashboardCache.status[sizeof(g_dashboardCache.status) - 1] = '\0';
    strncpy(g_dashboardCache.phaseTimer, header.timer,
            sizeof(g_dashboardCache.phaseTimer));
    g_dashboardCache.phaseTimer[sizeof(g_dashboardCache.phaseTimer) - 1] =
        '\0';
    g_dashboardCache.phaseProgress = header.phaseProgress;
  }

  if (full || strcmp(g_dashboardCache.processState, header.procState) != 0 ||
      strcmp(g_dashboardCache.safetyState, header.safetyState) != 0) {
    if (!full) {
      tft.fillRect(badgeX - 4, ROOT_STATUS_Y + 4, badgeW + 8, 34, colorCard());
    }
    drawStateBadge(badgeX, ROOT_STATUS_Y + 6, badgeW, badgeH, header.procState,
                   header.procColor);
    drawStateBadge(badgeX, ROOT_STATUS_Y + 24, badgeW, badgeH,
                   header.safetyState, header.safetyColor);

    strncpy(g_dashboardCache.processState, header.procState,
            sizeof(g_dashboardCache.processState));
    g_dashboardCache.processState[sizeof(g_dashboardCache.processState) - 1] =
        '\0';
    strncpy(g_dashboardCache.safetyState, header.safetyState,
            sizeof(g_dashboardCache.safetyState));
    g_dashboardCache.safetyState[sizeof(g_dashboardCache.safetyState) - 1] =
        '\0';
  }
}

static void renderRootFooter(const char *infoLine, const char *auxLine,
                             const char *uptime, bool full) {
  if (full || strcmp(g_dashboardCache.infoLine, infoLine) != 0 ||
      strcmp(g_dashboardCache.ioLine, auxLine) != 0 ||
      strcmp(g_dashboardCache.uptime, uptime) != 0) {
    if (!full) {
      tft.fillRect(14, ROOT_INFO_Y + 3, TFT_WIDTH - 28, 34, colorCard());
    }
    tft.setTextColor(colorFg());
    tft.setTextSize(1);
    tft.setTextDatum(middle_left);
    char infoBuf[96];
    char auxBuf[96];
    copyFittedText(infoLine, 350, infoBuf, sizeof(infoBuf));
    copyFittedText(auxLine, 350, auxBuf, sizeof(auxBuf));
    tft.drawString(infoBuf, 20, ROOT_INFO_Y + 13);
    tft.setTextColor(colorMuted());
    tft.drawString(auxBuf, 20, ROOT_INFO_Y + 28);
    tft.setTextColor(COLOR_PRIMARY);
    tft.setTextDatum(middle_right);
    tft.drawString(uptime, TFT_WIDTH - 18, ROOT_INFO_Y + 13);

    strncpy(g_dashboardCache.infoLine, infoLine,
            sizeof(g_dashboardCache.infoLine));
    g_dashboardCache.infoLine[sizeof(g_dashboardCache.infoLine) - 1] = '\0';
    strncpy(g_dashboardCache.ioLine, auxLine, sizeof(g_dashboardCache.ioLine));
    g_dashboardCache.ioLine[sizeof(g_dashboardCache.ioLine) - 1] = '\0';
    strncpy(g_dashboardCache.uptime, uptime, sizeof(g_dashboardCache.uptime));
    g_dashboardCache.uptime[sizeof(g_dashboardCache.uptime) - 1] = '\0';
  }
}

static void renderDashboard(const SystemState &state, bool full) {
  const bool ru = (g_settings.language == 0);
  RootHeaderState header = buildRootHeaderState(state);
  const int16_t barY = ROOT_STATUS_Y;
  const int16_t statusX = 20;
  const int16_t statusY = barY + 5;
  const int16_t statusW = 300;
  const int16_t statusH = 34;
  const int16_t badgeX = 332;
  const int16_t badgeW = 128;
  const int16_t badgeH = 14;
  const int16_t tileW = ROOT_RIGHT_TILE_W;
  const int16_t tileH = ROOT_RIGHT_TILE_H;
  const int16_t row1Y = ROOT_PANEL_Y;
  const int16_t row2Y = ROOT_PANEL_Y + tileH + ROOT_GRID_ROW_GAP;
  const int16_t row3Y = ROOT_PANEL_Y + (tileH + ROOT_GRID_ROW_GAP) * 2;
  const int16_t x1 = ROOT_RIGHT_X;
  const int16_t x2 = ROOT_RIGHT_X + tileW + ROOT_GRID_COL_GAP;
  const int16_t infoY = ROOT_INFO_Y;
  const bool hasWaterIn = state.temps.valid[TEMP_WATER_IN];
  const bool hasWaterOut = state.temps.valid[TEMP_WATER_OUT];

  enum DashboardProfile : uint8_t {
    DASH_PROFILE_IDLE = 0,
    DASH_PROFILE_RECT = 1,
    DASH_PROFILE_GENERIC = 2
  };
  DashboardProfile profile = DASH_PROFILE_GENERIC;
  if (state.mode == Mode::IDLE) {
    profile = DASH_PROFILE_IDLE;
  } else if (state.mode == Mode::RECTIFICATION) {
    profile = DASH_PROFILE_RECT;
  }

  const uint8_t layoutKey = static_cast<uint8_t>(
      (static_cast<uint8_t>(profile) << 2) | (hasWaterIn ? 0x01 : 0x00) |
      (hasWaterOut ? 0x02 : 0x00));

  if (full) {
    tft.fillScreen(colorBg());
    drawHeader(msg(Msg::MONITOR), false);
    drawTabs(UI_DASHBOARD);
    drawCard(10, barY, TFT_WIDTH - 20, 44, colorCard());
    drawCard(10, infoY, TFT_WIDTH - 20, 40, colorCard());
    memset(&g_dashboardCache, 0, sizeof(g_dashboardCache));
    g_dashboardCache.layoutKey = 0xFF;
  }

  char statusBuf[64];
  snprintf(statusBuf, sizeof(statusBuf), "%s / %s",
           getDisplayModeName(state.mode), getDisplayPhaseName(state));

  const char *procState =
      (state.mode == Mode::IDLE)
          ? (ru ? "ОЖИД." : "IDLE")
          : (state.paused ? (ru ? "ПАУЗА" : "PAUSE") : (ru ? "РАБОТА" : "RUN"));
  const uint16_t procColor =
      (state.mode == Mode::IDLE)
          ? COLOR_INFO
          : (state.paused ? COLOR_WARNING : COLOR_SUCCESS);
  const char *safetyState =
      state.safetyOk ? (ru ? "БЕЗОП." : "SAFE") : (ru ? "ТРЕВОГА" : "ALARM");
  const uint16_t safetyColor = state.safetyOk ? COLOR_SUCCESS : COLOR_DANGER;

  const uint32_t phaseElapsedSec = FSM::getPhaseElapsedSec();
  const uint32_t phaseTargetSec = FSM::getPhaseTargetSec(state, g_settings);
  const uint8_t phaseProgress = FSM::getPhaseProgressPercent(state, g_settings);
  char elapsedBuf[16];
  char targetBuf[16];
  char timerBuf[32];
  formatDurationCompact(phaseElapsedSec, elapsedBuf, sizeof(elapsedBuf));
  if (phaseTargetSec > 0) {
    formatDurationCompact(phaseTargetSec, targetBuf, sizeof(targetBuf));
    snprintf(timerBuf, sizeof(timerBuf), "%s %s/%s", ru ? "Фаза" : "Phase",
             elapsedBuf, targetBuf);
  } else {
    snprintf(timerBuf, sizeof(timerBuf), "%s %s", ru ? "Фаза" : "Phase",
             elapsedBuf);
  }

  if (full || strcmp(g_dashboardCache.status, statusBuf) != 0 ||
      strcmp(g_dashboardCache.phaseTimer, timerBuf) != 0 ||
      g_dashboardCache.phaseProgress != phaseProgress) {
    if (!full) {
      tft.fillRect(statusX, statusY, statusW, statusH, colorCard());
    }
    tft.setTextColor(colorAccent());
    tft.setTextSize(1);
    tft.setFont(&fonts::efontJA_16);
    tft.setTextDatum(top_left);
    tft.drawString(statusBuf, statusX + 2, statusY + 1);
    tft.setTextColor(tft.color565(120, 130, 140));
    tft.drawString(timerBuf, statusX + 2, statusY + 14);

    const int16_t pbX = statusX + 2;
    const int16_t pbY = statusY + 27;
    const int16_t pbW = statusW - 6;
    const int16_t pbH = 6;
    drawProgressBar(pbX, pbY, pbW, pbH, phaseProgress, colorAccent());
    tft.setFont(&fonts::efontJA_16);
    tft.setTextDatum(top_left);
    strncpy(g_dashboardCache.status, statusBuf,
            sizeof(g_dashboardCache.status));
    g_dashboardCache.status[sizeof(g_dashboardCache.status) - 1] = '\0';
    strncpy(g_dashboardCache.phaseTimer, timerBuf,
            sizeof(g_dashboardCache.phaseTimer));
    g_dashboardCache.phaseTimer[sizeof(g_dashboardCache.phaseTimer) - 1] = '\0';
    g_dashboardCache.phaseProgress = phaseProgress;
  }

  if (full || strcmp(g_dashboardCache.processState, procState) != 0 ||
      strcmp(g_dashboardCache.safetyState, safetyState) != 0) {
    if (!full) {
      tft.fillRect(badgeX - 4, barY + 4, badgeW + 8, 34, colorCard());
    }

    drawStateBadge(badgeX, barY + 6, badgeW, badgeH, procState, procColor);
    drawStateBadge(badgeX, barY + 24, badgeW, badgeH, safetyState, safetyColor);

    strncpy(g_dashboardCache.processState, procState,
            sizeof(g_dashboardCache.processState));
    g_dashboardCache.processState[sizeof(g_dashboardCache.processState) - 1] =
        '\0';
    strncpy(g_dashboardCache.safetyState, safetyState,
            sizeof(g_dashboardCache.safetyState));
    g_dashboardCache.safetyState[sizeof(g_dashboardCache.safetyState) - 1] =
        '\0';
  }

  const bool layoutChanged = full || (g_dashboardCache.layoutKey != layoutKey);
  const int16_t tileX[6] = {x1, x2, x1, x2, x1, x2};
  const int16_t tileY[6] = {row1Y, row1Y, row2Y, row2Y, row3Y, row3Y};
  const char *tileLabels[6] = {nullptr, nullptr, nullptr,
                               nullptr, nullptr, nullptr};
  const char *tileUnits[6] = {
      "°C", "°C", "°C", "°C", msg(Msg::UNIT_W), msg(Msg::UNIT_ML_H)};
  uint16_t tileColors[6] = {COLOR_DANGER,  colorAccent(), COLOR_INFO,
                            COLOR_WARNING, COLOR_WARNING, COLOR_SUCCESS};
  char tileValues[6][16] = {};

  tileLabels[0] = msg(Msg::CUBE_TEMP);
  tileLabels[1] = msg(Msg::TOP_T);
  tileLabels[2] = msg(Msg::REFLUX_T);
  tileLabels[3] = msg(Msg::TSA_T);

  snprintf(tileValues[0], sizeof(tileValues[0]), "%.1f", state.temps.cube);
  snprintf(tileValues[1], sizeof(tileValues[1]), "%.1f", state.temps.columnTop);
  snprintf(tileValues[2], sizeof(tileValues[2]), "%.1f", state.temps.reflux);
  snprintf(tileValues[3], sizeof(tileValues[3]), "%.1f", state.temps.tsa);

  if (profile == DASH_PROFILE_IDLE) {
    tileLabels[4] =
        hasWaterIn ? (ru ? "ОХЛ ВХ" : "WATER IN") : (ru ? "СЕТЬ" : "MAINS");
    tileLabels[5] = hasWaterOut ? (ru ? "ОХЛ ВЫХ" : "WATER OUT")
                                : (ru ? "МОЩНОСТЬ" : "POWER");
    if (hasWaterIn) {
      snprintf(tileValues[4], sizeof(tileValues[4]), "%.1f",
               state.temps.waterIn);
      tileUnits[4] = "°C";
      tileColors[4] = COLOR_INFO;
    } else {
      snprintf(tileValues[4], sizeof(tileValues[4]), "%.0f",
               state.power.voltage);
      tileUnits[4] = "V";
      tileColors[4] = COLOR_PRIMARY;
    }
    if (hasWaterOut) {
      snprintf(tileValues[5], sizeof(tileValues[5]), "%.1f",
               state.temps.waterOut);
      tileUnits[5] = "°C";
      tileColors[5] = COLOR_INFO;
    } else {
      snprintf(tileValues[5], sizeof(tileValues[5]), "%.0f", state.power.power);
      tileUnits[5] = msg(Msg::UNIT_W);
      tileColors[5] = COLOR_WARNING;
    }
  } else if (profile == DASH_PROFILE_RECT) {
    tileLabels[4] = msg(Msg::HEATER_POWER);
    snprintf(tileValues[4], sizeof(tileValues[4]), "%.0f", state.power.power);
    if (hasWaterOut) {
      tileLabels[5] = ru ? "ОХЛ ВЫХ" : "WATER OUT";
      snprintf(tileValues[5], sizeof(tileValues[5]), "%.1f",
               state.temps.waterOut);
      tileUnits[5] = "°C";
      tileColors[5] = COLOR_INFO;
    } else {
      tileLabels[5] = ru ? "ОТБОР" : "TAKEOFF";
      snprintf(tileValues[5], sizeof(tileValues[5]), "%.0f",
               state.pump.speedMlPerHour);
      tileUnits[5] = msg(Msg::UNIT_ML_H);
      tileColors[5] = COLOR_SUCCESS;
    }
  } else {
    tileLabels[4] = msg(Msg::HEATER_POWER);
    tileLabels[5] =
        hasWaterOut ? (ru ? "ОХЛ ВЫХ" : "WATER OUT") : msg(Msg::PUMP);
    snprintf(tileValues[4], sizeof(tileValues[4]), "%.0f", state.power.power);
    if (hasWaterOut) {
      snprintf(tileValues[5], sizeof(tileValues[5]), "%.1f",
               state.temps.waterOut);
      tileUnits[5] = "°C";
      tileColors[5] = COLOR_INFO;
    } else {
      snprintf(tileValues[5], sizeof(tileValues[5]), "%.0f",
               state.pump.speedMlPerHour);
      tileUnits[5] = msg(Msg::UNIT_ML_H);
      tileColors[5] = COLOR_SUCCESS;
    }
  }

  if (layoutChanged) {
    drawCard(ROOT_LEFT_X, ROOT_PANEL_Y, ROOT_LEFT_W, ROOT_PANEL_H, colorCard());
    for (uint8_t i = 0; i < 6; i++) {
      drawValueTileShell(tileX[i], tileY[i], tileW, tileH, tileLabels[i]);
    }
    g_dashboardCache.cube[0] = '\0';
    g_dashboardCache.top[0] = '\0';
    g_dashboardCache.reflux[0] = '\0';
    g_dashboardCache.tsa[0] = '\0';
    g_dashboardCache.power[0] = '\0';
    g_dashboardCache.pump[0] = '\0';
    g_dashboardCache.infoLine[0] = '\0';
    g_dashboardCache.ioLine[0] = '\0';
    g_dashboardCache.layoutKey = layoutKey;
  }

  char *tileCache[6] = {g_dashboardCache.cube,   g_dashboardCache.top,
                        g_dashboardCache.reflux, g_dashboardCache.tsa,
                        g_dashboardCache.power,  g_dashboardCache.pump};

  for (uint8_t i = 0; i < 6; i++) {
    if (full || strcmp(tileCache[i], tileValues[i]) != 0) {
      drawValueTileValue(tileX[i], tileY[i], tileW, tileH, tileValues[i],
                         tileUnits[i], tileColors[i]);
      strncpy(tileCache[i], tileValues[i], 15);
      tileCache[i][15] = '\0';
    }
  }

  char infoBuf[96];
  if (profile == DASH_PROFILE_IDLE) {
    snprintf(infoBuf, sizeof(infoBuf), "%s",
             ru ? "Ожидание: выберите режим в Управлении"
                : "Idle: choose mode in Control");
  } else if (ru) {
    snprintf(infoBuf, sizeof(infoBuf), "Гол %.0f | Тело %.0f | Хв %.0f мл",
             state.stats.headsVolume, state.stats.bodyVolume,
             state.stats.tailsVolume);
  } else {
    snprintf(infoBuf, sizeof(infoBuf), "Heads %.0f | Body %.0f | Tails %.0f ml",
             state.stats.headsVolume, state.stats.bodyVolume,
             state.stats.tailsVolume);
  }

  const char *k1 = Valves::getWater() ? "ON" : "--";
  const char *k2 = Valves::getHeads() ? "ON" : "--";
  const char *k3 = (Heater::getPower() > 0) ? "ON" : "--";
  char waterBuf[24];
  if (state.temps.valid[TEMP_WATER_IN] && state.temps.valid[TEMP_WATER_OUT]) {
    snprintf(waterBuf, sizeof(waterBuf), "%.1f/%.1f", state.temps.waterIn,
             state.temps.waterOut);
  } else if (state.temps.valid[TEMP_WATER_OUT]) {
    snprintf(waterBuf, sizeof(waterBuf), "--/%.1f", state.temps.waterOut);
  } else if (state.temps.valid[TEMP_WATER_IN]) {
    snprintf(waterBuf, sizeof(waterBuf), "%.1f/--", state.temps.waterIn);
  } else {
    strncpy(waterBuf, "--/--", sizeof(waterBuf));
    waterBuf[sizeof(waterBuf) - 1] = '\0';
  }
  char ioBuf[96];
  if (profile == DASH_PROFILE_IDLE) {
    snprintf(ioBuf, sizeof(ioBuf), "W %s | V %.0f | P %.0f", waterBuf,
             state.power.voltage, state.pressure.cube);
  } else {
    snprintf(ioBuf, sizeof(ioBuf), "W %s | V %.0f | P %.0f | K1%s K2%s K3%s",
             waterBuf, state.power.voltage, state.pressure.cube, k1, k2, k3);
  }

  char upBuf[16];
  formatUptimeCompact(state.uptime, upBuf, sizeof(upBuf));

  drawCard(ROOT_LEFT_X, ROOT_PANEL_Y, ROOT_LEFT_W, ROOT_PANEL_H, colorCard());
  drawPanelHeader(ROOT_LEFT_X, ROOT_PANEL_Y, ROOT_LEFT_W, header.procState,
                  header.procColor);
  if (state.mode == Mode::IDLE) {
    char mainsBuf[24];
    char pressBuf[24];
    snprintf(mainsBuf, sizeof(mainsBuf), "%.0f V", state.power.voltage);
    snprintf(pressBuf, sizeof(pressBuf), "%.0f mm", state.pressure.cube);
    drawCompactKeyValueRow(ROOT_LEFT_X + 8, ROOT_PANEL_Y + 36, ROOT_LEFT_W - 16,
                           ru ? "ДАЛЕЕ" : "NEXT",
                           ru ? "УПРАВЛ." : "CONTROL", colorAccent());
    drawCompactKeyValueRow(ROOT_LEFT_X + 8, ROOT_PANEL_Y + 58, ROOT_LEFT_W - 16,
                           ru ? "СЕТЬ" : "MAINS", mainsBuf, COLOR_PRIMARY);
    drawCompactKeyValueRow(ROOT_LEFT_X + 8, ROOT_PANEL_Y + 80, ROOT_LEFT_W - 16,
                           ru ? "ДАВЛ." : "PRESS", pressBuf, COLOR_WARNING);
    drawCompactKeyValueRow(ROOT_LEFT_X + 8, ROOT_PANEL_Y + 102, ROOT_LEFT_W - 16,
                           ru ? "ОХЛ." : "COOL", waterBuf, COLOR_INFO);
    drawCompactKeyValueRow(ROOT_LEFT_X + 8, ROOT_PANEL_Y + 124, ROOT_LEFT_W - 16,
                           ru ? "ФАЗА" : "PHASE", getDisplayPhaseName(state),
                           colorAccent());
  } else {
    char rowBuf[24];
    drawCompactKeyValueRow(ROOT_LEFT_X + 8, ROOT_PANEL_Y + 36, ROOT_LEFT_W - 16,
                           ru ? "РЕЖИМ" : "MODE", getDisplayModeName(state.mode),
                           colorAccent());
    snprintf(rowBuf, sizeof(rowBuf), "%.0f мл", state.stats.headsVolume);
    drawCompactKeyValueRow(ROOT_LEFT_X + 8, ROOT_PANEL_Y + 58, ROOT_LEFT_W - 16,
                           ru ? "ГОЛОВЫ" : "HEADS", rowBuf, COLOR_WARNING);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f мл", state.stats.bodyVolume);
    drawCompactKeyValueRow(ROOT_LEFT_X + 8, ROOT_PANEL_Y + 80, ROOT_LEFT_W - 16,
                           ru ? "ТЕЛО" : "BODY", rowBuf, COLOR_SUCCESS);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f мл", state.stats.tailsVolume);
    drawCompactKeyValueRow(ROOT_LEFT_X + 8, ROOT_PANEL_Y + 102, ROOT_LEFT_W - 16,
                           ru ? "ХВОСТЫ" : "TAILS", rowBuf, COLOR_DANGER);
    drawCompactKeyValueRow(ROOT_LEFT_X + 8, ROOT_PANEL_Y + 124, ROOT_LEFT_W - 16,
                           ru ? "ФАЗА" : "PHASE", getDisplayPhaseName(state),
                           colorAccent());
  }
  drawStateBadge(ROOT_LEFT_X + 8, ROOT_PANEL_Y + ROOT_PANEL_H - 22,
                 ROOT_LEFT_W - 16, 14, header.safetyState, header.safetyColor);

  if (full || strcmp(g_dashboardCache.infoLine, infoBuf) != 0 ||
      strcmp(g_dashboardCache.ioLine, ioBuf) != 0 ||
      strcmp(g_dashboardCache.uptime, upBuf) != 0) {
    if (!full) {
      tft.fillRect(14, infoY + 3, TFT_WIDTH - 28, 34, colorCard());
    }
    tft.setTextColor(colorFg());
    tft.setTextSize(1);
    tft.setTextDatum(middle_left);
    tft.drawString(infoBuf, 20, infoY + 13);
    tft.setTextColor(tft.color565(120, 130, 140));
    tft.drawString(ioBuf, 20, infoY + 28);
    tft.setTextColor(COLOR_PRIMARY);
    tft.setTextDatum(middle_right);
    tft.drawString(upBuf, TFT_WIDTH - 18, infoY + 13);

    strncpy(g_dashboardCache.infoLine, infoBuf,
            sizeof(g_dashboardCache.infoLine));
    g_dashboardCache.infoLine[sizeof(g_dashboardCache.infoLine) - 1] = '\0';
    strncpy(g_dashboardCache.ioLine, ioBuf, sizeof(g_dashboardCache.ioLine));
    g_dashboardCache.ioLine[sizeof(g_dashboardCache.ioLine) - 1] = '\0';
    strncpy(g_dashboardCache.uptime, upBuf, sizeof(g_dashboardCache.uptime));
    g_dashboardCache.uptime[sizeof(g_dashboardCache.uptime) - 1] = '\0';
  }

  tft.setFont(&fonts::efontJA_16);
  tft.setTextDatum(top_left);
}

static void renderModeMonitorCustom(const SystemState &state, bool full);
static void renderModeMonitorCustomHmi(const SystemState &state, bool full);

static void renderModeMonitor(const SystemState &state, bool full) {
  if (state.mode != Mode::RECTIFICATION) {
    renderModeMonitorCustomHmi(state, full);
    return;
  }

  const bool ru = (g_settings.language == 0);
  RootHeaderState header = buildRootHeaderState(state);
  const int16_t barY = ROOT_STATUS_Y;
  const int16_t statusX = 20;
  const int16_t statusY = barY + 5;
  const int16_t statusW = 300;
  const int16_t statusH = 34;
  const int16_t badgeX = 332;
  const int16_t badgeW = 128;
  const int16_t badgeH = 14;

  const int16_t panelY = ROOT_PANEL_Y;
  const int16_t panelH = ROOT_PANEL_H;
  const int16_t leftX = ROOT_LEFT_X;
  const int16_t leftW = ROOT_LEFT_W;
  const int16_t rightX = ROOT_RIGHT_X;
  const int16_t rightW = ROOT_RIGHT_W;
  const int16_t colGap = ROOT_GRID_COL_GAP;
  const int16_t rowGap = ROOT_GRID_ROW_GAP;
  const int16_t tileW = (rightW - colGap) / 2;
  const int16_t tileH = (panelH - rowGap * 2) / 3;
  const int16_t infoY = ROOT_INFO_Y;

  if (full) {
    tft.fillScreen(colorBg());
    drawHeader(msg(Msg::MONITOR), false);
    drawTabs(UI_MODE_MONITOR);
    drawCard(10, barY, TFT_WIDTH - 20, 44, colorCard());
    drawCard(leftX, panelY, leftW, panelH, colorCard());

    drawValueTileShell(rightX, panelY, tileW, tileH, msg(Msg::CUBE_TEMP));
    drawValueTileShell(rightX + tileW + colGap, panelY, tileW, tileH,
                       msg(Msg::TOP_T));
    drawValueTileShell(rightX, panelY + tileH + rowGap, tileW, tileH,
                       msg(Msg::REFLUX_T));
    drawValueTileShell(rightX + tileW + colGap, panelY + tileH + rowGap, tileW,
                       tileH, msg(Msg::TSA_T));
    drawValueTileShell(rightX, panelY + (tileH + rowGap) * 2, tileW, tileH,
                       msg(Msg::HEATER_POWER));
    drawValueTileShell(
        rightX + tileW + colGap, panelY + (tileH + rowGap) * 2, tileW, tileH,
        state.temps.valid[TEMP_WATER_OUT] ? (ru ? "ОХЛ ВЫХ" : "WATER OUT")
                                          : msg(Msg::PUMP));

    drawCard(10, infoY, TFT_WIDTH - 20, 40, colorCard());
    memset(&g_dashboardCache, 0, sizeof(g_dashboardCache));
    g_dashboardCache.layoutKey = 0xEE;
  }

  char statusBuf[64];
  snprintf(statusBuf, sizeof(statusBuf), "%s / %s",
           getDisplayModeName(state.mode), getDisplayPhaseName(state));

  const char *procState =
      state.paused ? (ru ? "ПАУЗА" : "PAUSE") : (ru ? "РАБОТА" : "RUN");
  const uint16_t procColor = state.paused ? COLOR_WARNING : COLOR_SUCCESS;
  const char *safetyState =
      state.safetyOk ? (ru ? "БЕЗОП." : "SAFE") : (ru ? "ТРЕВОГА" : "ALARM");
  const uint16_t safetyColor = state.safetyOk ? COLOR_SUCCESS : COLOR_DANGER;

  const uint32_t phaseElapsedSec = FSM::getPhaseElapsedSec();
  const uint32_t phaseTargetSec = FSM::getPhaseTargetSec(state, g_settings);
  const uint8_t phaseProgress = FSM::getPhaseProgressPercent(state, g_settings);
  char elapsedBuf[16];
  char targetBuf[16];
  char timerBuf[32];
  formatDurationCompact(phaseElapsedSec, elapsedBuf, sizeof(elapsedBuf));
  if (phaseTargetSec > 0) {
    formatDurationCompact(phaseTargetSec, targetBuf, sizeof(targetBuf));
    snprintf(timerBuf, sizeof(timerBuf), "%s %s/%s", ru ? "Фаза" : "Phase",
             elapsedBuf, targetBuf);
  } else {
    snprintf(timerBuf, sizeof(timerBuf), "%s %s", ru ? "Фаза" : "Phase",
             elapsedBuf);
  }

  if (full || strcmp(g_dashboardCache.status, statusBuf) != 0 ||
      strcmp(g_dashboardCache.phaseTimer, timerBuf) != 0 ||
      g_dashboardCache.phaseProgress != phaseProgress) {
    if (!full) {
      tft.fillRect(statusX, statusY, statusW, statusH, colorCard());
    }
    tft.setTextColor(colorAccent());
    tft.setTextSize(1);
    tft.setFont(&fonts::efontJA_16);
    tft.setTextDatum(top_left);
    tft.drawString(statusBuf, statusX + 2, statusY + 1);
    tft.setTextColor(tft.color565(120, 130, 140));
    tft.drawString(timerBuf, statusX + 2, statusY + 14);

    const int16_t pbX = statusX + 2;
    const int16_t pbY = statusY + 27;
    const int16_t pbW = statusW - 6;
    const int16_t pbH = 6;
    drawProgressBar(pbX, pbY, pbW, pbH, phaseProgress, colorAccent());

    strncpy(g_dashboardCache.status, statusBuf,
            sizeof(g_dashboardCache.status));
    g_dashboardCache.status[sizeof(g_dashboardCache.status) - 1] = '\0';
    strncpy(g_dashboardCache.phaseTimer, timerBuf,
            sizeof(g_dashboardCache.phaseTimer));
    g_dashboardCache.phaseTimer[sizeof(g_dashboardCache.phaseTimer) - 1] = '\0';
    g_dashboardCache.phaseProgress = phaseProgress;
  }

  if (full || strcmp(g_dashboardCache.processState, procState) != 0 ||
      strcmp(g_dashboardCache.safetyState, safetyState) != 0) {
    if (!full) {
      tft.fillRect(badgeX - 4, barY + 4, badgeW + 8, 34, colorCard());
    }
    drawStateBadge(badgeX, barY + 6, badgeW, badgeH, procState, procColor);
    drawStateBadge(badgeX, barY + 24, badgeW, badgeH, safetyState, safetyColor);

    strncpy(g_dashboardCache.processState, procState,
            sizeof(g_dashboardCache.processState));
    g_dashboardCache.processState[sizeof(g_dashboardCache.processState) - 1] =
        '\0';
    strncpy(g_dashboardCache.safetyState, safetyState,
            sizeof(g_dashboardCache.safetyState));
    g_dashboardCache.safetyState[sizeof(g_dashboardCache.safetyState) - 1] =
        '\0';
  }

  char summaryBuf[96];
  if (ru) {
    snprintf(summaryBuf, sizeof(summaryBuf),
             "ОКНО РЕЖИМА\nГол %.0f  Тело %.0f  Хв %.0f",
             state.stats.headsVolume, state.stats.bodyVolume,
             state.stats.tailsVolume);
  } else {
    snprintf(summaryBuf, sizeof(summaryBuf),
             "MODE WINDOW\nH %.0f  B %.0f  T %.0f", state.stats.headsVolume,
             state.stats.bodyVolume, state.stats.tailsVolume);
  }

  char val[16];
  char topLine[32];
  snprintf(topLine, sizeof(topLine), "V %.0f  P %.0f", state.power.voltage,
           state.pressure.cube);
  if (full || strcmp(g_dashboardCache.infoLine, summaryBuf) != 0 ||
      strcmp(g_dashboardCache.ioLine, topLine) != 0) {
    if (!full) {
      tft.fillRect(leftX + 6, panelY + 8, leftW - 12, panelH - 16, colorCard());
    }
    tft.setTextColor(colorFg());
    tft.setTextSize(1);
    tft.setTextDatum(top_left);
    tft.drawString(ru ? "РЕЖИМНОЕ ОКНО" : "MODE WINDOW", leftX + 10,
                   panelY + 10);
    tft.setTextColor(tft.color565(120, 130, 140));
    tft.drawString(topLine, leftX + 10, panelY + 28);
    tft.setTextColor(colorFg());
    if (ru) {
      snprintf(val, sizeof(val), "Гол %.0f мл", state.stats.headsVolume);
      tft.drawString(val, leftX + 10, panelY + 52);
      snprintf(val, sizeof(val), "Тело %.0f мл", state.stats.bodyVolume);
      tft.drawString(val, leftX + 10, panelY + 72);
      snprintf(val, sizeof(val), "Хв %.0f мл", state.stats.tailsVolume);
      tft.drawString(val, leftX + 10, panelY + 92);
    } else {
      snprintf(val, sizeof(val), "Heads %.0f ml", state.stats.headsVolume);
      tft.drawString(val, leftX + 10, panelY + 52);
      snprintf(val, sizeof(val), "Body %.0f ml", state.stats.bodyVolume);
      tft.drawString(val, leftX + 10, panelY + 72);
      snprintf(val, sizeof(val), "Tails %.0f ml", state.stats.tailsVolume);
      tft.drawString(val, leftX + 10, panelY + 92);
    }

    const char *k1 = Valves::getWater() ? "ON" : "--";
    const char *k2 = Valves::getHeads() ? "ON" : "--";
    const char *k3 = (Heater::getPower() > 0) ? "ON" : "--";
    snprintf(val, sizeof(val), "K1%s K2%s K3%s", k1, k2, k3);
    tft.setTextColor(tft.color565(120, 130, 140));
    tft.drawString(val, leftX + 10, panelY + 120);

    strncpy(g_dashboardCache.infoLine, summaryBuf,
            sizeof(g_dashboardCache.infoLine));
    g_dashboardCache.infoLine[sizeof(g_dashboardCache.infoLine) - 1] = '\0';
    strncpy(g_dashboardCache.ioLine, topLine, sizeof(g_dashboardCache.ioLine));
    g_dashboardCache.ioLine[sizeof(g_dashboardCache.ioLine) - 1] = '\0';
  }

  {
    const char *k1 = Valves::getWater() ? "ON" : "--";
    const char *k2 = Valves::getHeads() ? "ON" : "--";
    const char *k3 = (Heater::getPower() > 0) ? "ON" : "--";
    char rowBuf[24];
    drawCard(leftX, panelY, leftW, panelH, colorCard());
    drawPanelHeader(leftX, panelY, leftW, getDisplayPhaseName(state),
                    state.paused ? COLOR_WARNING : colorAccent());
    drawCompactKeyValueRow(leftX + 8, panelY + 36, leftW - 16,
                           ru ? "РЕЖИМ" : "MODE",
                           getDisplayModeName(state.mode), colorAccent());
    snprintf(rowBuf, sizeof(rowBuf), "%.0f %s", state.stats.headsVolume,
             ru ? "мл" : "ml");
    drawCompactKeyValueRow(leftX + 8, panelY + 58, leftW - 16,
                           ru ? "ГОЛОВЫ" : "HEADS", rowBuf, COLOR_WARNING);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f %s", state.stats.bodyVolume,
             ru ? "мл" : "ml");
    drawCompactKeyValueRow(leftX + 8, panelY + 80, leftW - 16,
                           ru ? "ТЕЛО" : "BODY", rowBuf, COLOR_SUCCESS);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f %s", state.stats.tailsVolume,
             ru ? "мл" : "ml");
    drawCompactKeyValueRow(leftX + 8, panelY + 102, leftW - 16,
                           ru ? "ХВОСТЫ" : "TAILS", rowBuf, COLOR_DANGER);
    snprintf(rowBuf, sizeof(rowBuf), "K1%s K2%s K3%s", k1, k2, k3);
    drawStateBadge(leftX + 8, panelY + panelH - 22, leftW - 16, 14, rowBuf,
                   header.safetyColor);
  }

  snprintf(val, sizeof(val), "%.1f", state.temps.cube);
  if (full || strcmp(g_dashboardCache.cube, val) != 0) {
    drawValueTileValue(rightX, panelY, tileW, tileH, val, "°C", COLOR_DANGER);
    strncpy(g_dashboardCache.cube, val, sizeof(g_dashboardCache.cube));
    g_dashboardCache.cube[sizeof(g_dashboardCache.cube) - 1] = '\0';
  }
  snprintf(val, sizeof(val), "%.1f", state.temps.columnTop);
  if (full || strcmp(g_dashboardCache.top, val) != 0) {
    drawValueTileValue(rightX + tileW + colGap, panelY, tileW, tileH, val,
                       "°C", colorAccent());
    strncpy(g_dashboardCache.top, val, sizeof(g_dashboardCache.top));
    g_dashboardCache.top[sizeof(g_dashboardCache.top) - 1] = '\0';
  }
  snprintf(val, sizeof(val), "%.1f", state.temps.reflux);
  if (full || strcmp(g_dashboardCache.reflux, val) != 0) {
    drawValueTileValue(rightX, panelY + tileH + rowGap, tileW, tileH, val,
                       "°C", COLOR_INFO);
    strncpy(g_dashboardCache.reflux, val, sizeof(g_dashboardCache.reflux));
    g_dashboardCache.reflux[sizeof(g_dashboardCache.reflux) - 1] = '\0';
  }
  snprintf(val, sizeof(val), "%.1f", state.temps.tsa);
  if (full || strcmp(g_dashboardCache.tsa, val) != 0) {
    drawValueTileValue(rightX + tileW + colGap, panelY + tileH + rowGap, tileW,
                       tileH, val, "°C", COLOR_WARNING);
    strncpy(g_dashboardCache.tsa, val, sizeof(g_dashboardCache.tsa));
    g_dashboardCache.tsa[sizeof(g_dashboardCache.tsa) - 1] = '\0';
  }
  snprintf(val, sizeof(val), "%.0f", state.power.power);
  if (full || strcmp(g_dashboardCache.power, val) != 0) {
    drawValueTileValue(rightX, panelY + (tileH + rowGap) * 2, tileW, tileH, val,
                       msg(Msg::UNIT_W), COLOR_WARNING);
    strncpy(g_dashboardCache.power, val, sizeof(g_dashboardCache.power));
    g_dashboardCache.power[sizeof(g_dashboardCache.power) - 1] = '\0';
  }
  if (state.temps.valid[TEMP_WATER_OUT]) {
    snprintf(val, sizeof(val), "%.1f", state.temps.waterOut);
  } else {
    snprintf(val, sizeof(val), "%.0f", state.pump.speedMlPerHour);
  }
  if (full || strcmp(g_dashboardCache.pump, val) != 0) {
    drawValueTileValue(
        rightX + tileW + colGap, panelY + (tileH + rowGap) * 2, tileW, tileH,
        val, state.temps.valid[TEMP_WATER_OUT] ? "°C" : msg(Msg::UNIT_ML_H),
        state.temps.valid[TEMP_WATER_OUT] ? COLOR_INFO : COLOR_SUCCESS);
    strncpy(g_dashboardCache.pump, val, sizeof(g_dashboardCache.pump));
    g_dashboardCache.pump[sizeof(g_dashboardCache.pump) - 1] = '\0';
  }

  char upBuf[16];
  formatUptimeCompact(state.uptime, upBuf, sizeof(upBuf));
  if (full || strcmp(g_dashboardCache.uptime, upBuf) != 0) {
    if (!full) {
      tft.fillRect(14, infoY + 3, TFT_WIDTH - 28, 34, colorCard());
    }
    tft.setTextColor(tft.color565(120, 130, 140));
    tft.setTextSize(1);
    tft.setTextDatum(middle_left);
    tft.drawString(ru ? "Окно режима активно" : "Mode window active", 20,
                   infoY + 20);
    tft.setTextColor(COLOR_PRIMARY);
    tft.setTextDatum(middle_right);
    tft.drawString(upBuf, TFT_WIDTH - 18, infoY + 20);
    strncpy(g_dashboardCache.uptime, upBuf, sizeof(g_dashboardCache.uptime));
    g_dashboardCache.uptime[sizeof(g_dashboardCache.uptime) - 1] = '\0';
  }

  tft.setFont(&fonts::efontJA_16);
  tft.setTextDatum(top_left);
}

static void renderModeMonitorCustomHmi(const SystemState &state, bool full) {
  const bool ru = (g_settings.language == 0);
  const bool hasWaterOut = state.temps.valid[TEMP_WATER_OUT];
  const uint8_t layoutKey = static_cast<uint8_t>(
      0xD0 | ((static_cast<uint8_t>(state.mode) & 0x0F) << 1) |
      (hasWaterOut ? 0x01 : 0x00));
  uint32_t phaseElapsedOverrideSec = 0xFFFFFFFFUL;
  const uint32_t nowMs = millis();

  if (state.mode == Mode::MASHING) {
    phaseElapsedOverrideSec = 0;
    if (state.mashing.tempInRange && state.mashing.inRangeStartTime > 0 &&
        nowMs >= state.mashing.inRangeStartTime) {
      phaseElapsedOverrideSec =
          (nowMs - state.mashing.inRangeStartTime) / 1000UL;
    }
  } else if (state.mode == Mode::HOLD) {
    phaseElapsedOverrideSec = 0;
    if (state.hold.tempInRange && state.hold.inRangeStartTime > 0 &&
        nowMs >= state.hold.inRangeStartTime) {
      phaseElapsedOverrideSec = (nowMs - state.hold.inRangeStartTime) / 1000UL;
    }
  }

  if (full) {
    drawRootScaffold(UI_MODE_MONITOR);
    memset(&g_dashboardCache, 0, sizeof(g_dashboardCache));
    g_dashboardCache.layoutKey = 0xFF;
  }

  const bool layoutChanged = full || (g_dashboardCache.layoutKey != layoutKey);
  if (layoutChanged) {
    memset(g_modeTileCache, 0, sizeof(g_modeTileCache));
    g_dashboardCache.infoLine[0] = '\0';
    g_dashboardCache.ioLine[0] = '\0';
    g_dashboardCache.uptime[0] = '\0';
  }

  RootHeaderState header =
      buildRootHeaderState(state, phaseElapsedOverrideSec);
  renderRootStatusBar(header, full);

  auto updateTile = [&](uint8_t idx, int16_t x, int16_t y, int16_t w, int16_t h,
                        const char *value, const char *unit, uint16_t color) {
    if (full || strcmp(g_modeTileCache[idx], value) != 0) {
      drawValueTileValue(x, y, w, h, value, unit, color);
      strncpy(g_modeTileCache[idx], value, sizeof(g_modeTileCache[idx]) - 1);
      g_modeTileCache[idx][sizeof(g_modeTileCache[idx]) - 1] = '\0';
    }
  };

  const int16_t leftX = ROOT_LEFT_X;
  const int16_t leftW = ROOT_LEFT_W;
  const int16_t rightX = ROOT_RIGHT_X;
  const int16_t rightW = ROOT_RIGHT_W;
  const int16_t tileW = ROOT_RIGHT_TILE_W;
  const int16_t tileH = ROOT_RIGHT_TILE_H;
  const int16_t x2 = rightX + tileW + ROOT_GRID_COL_GAP;
  const int16_t y2 = ROOT_PANEL_Y + tileH + ROOT_GRID_ROW_GAP;
  const int16_t y3 = ROOT_PANEL_Y + (tileH + ROOT_GRID_ROW_GAP) * 2;

  char infoLine[96] = "";
  char auxLine[96] = "";
  char upBuf[16];
  formatDurationCompact(getModeRunElapsedSec(state), upBuf, sizeof(upBuf));

  if (state.mode == Mode::MANUAL_RECT) {
    if (layoutChanged) {
      drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
      drawValueTileShell(rightX, ROOT_PANEL_Y, tileW, tileH, msg(Msg::CUBE_TEMP));
      drawValueTileShell(x2, ROOT_PANEL_Y, tileW, tileH, msg(Msg::TOP_T));
      drawValueTileShell(rightX, y2, tileW, tileH, msg(Msg::REFLUX_T));
      drawValueTileShell(x2, y2, tileW, tileH, msg(Msg::TSA_T));
      drawValueTileShell(rightX, y3, tileW, tileH,
                         ru ? "ДАВЛ. КУБА" : "CUBE PRESS");
      drawValueTileShell(x2, y3, tileW, tileH, msg(Msg::PUMP));
    }

    char rowBuf[32];
    char v[6][20];
    snprintf(v[0], sizeof(v[0]), "%.1f", state.temps.cube);
    snprintf(v[1], sizeof(v[1]), "%.1f", state.temps.columnTop);
    snprintf(v[2], sizeof(v[2]), "%.1f", state.temps.reflux);
    snprintf(v[3], sizeof(v[3]), "%.1f", state.temps.tsa);
    snprintf(v[4], sizeof(v[4]), "%.0f", state.pressure.cube);
    snprintf(v[5], sizeof(v[5]), "%.0f", state.pump.speedMlPerHour);

    updateTile(0, rightX, ROOT_PANEL_Y, tileW, tileH, v[0], "C", COLOR_DANGER);
    updateTile(1, x2, ROOT_PANEL_Y, tileW, tileH, v[1], "C", colorAccent());
    updateTile(2, rightX, y2, tileW, tileH, v[2], "C", COLOR_INFO);
    updateTile(3, x2, y2, tileW, tileH, v[3], "C", COLOR_WARNING);
    updateTile(4, rightX, y3, tileW, tileH, v[4], "mm", COLOR_WARNING);
    updateTile(5, x2, y3, tileW, tileH, v[5], msg(Msg::UNIT_ML_H),
               COLOR_SUCCESS);

    drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
    drawPanelHeader(leftX, ROOT_PANEL_Y, leftW, getDisplayPhaseName(state),
                    state.paused ? COLOR_WARNING : colorAccent());
    snprintf(rowBuf, sizeof(rowBuf), "%.0f ml/h", manualRectUi.speedMlH);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 36, leftW - 16,
                           ru ? "СКОРОСТЬ" : "SPEED", rowBuf, COLOR_SUCCESS);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f %%", manualRectUi.powerPercent);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 58, leftW - 16,
                           ru ? "МОЩНОСТЬ" : "POWER", rowBuf, COLOR_WARNING);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f/%.0f ml", state.stats.headsVolume,
             manualRectUi.headsTargetMl);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 80, leftW - 16,
                           ru ? "ГОЛОВЫ" : "HEADS", rowBuf, COLOR_INFO);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f/%.0f ml", state.stats.bodyVolume,
             manualRectUi.bodyTargetMl);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 102, leftW - 16,
                           ru ? "ТЕЛО" : "BODY", rowBuf, COLOR_PRIMARY);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f/%.0f ml", state.stats.tailsVolume,
             manualRectUi.tailsTargetMl);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 124, leftW - 16,
                           ru ? "ХВОСТЫ" : "TAILS", rowBuf, COLOR_WARNING);
    drawStateBadge(leftX + 8, ROOT_PANEL_Y + ROOT_PANEL_H - 22, leftW - 16, 14,
                   header.procState, header.procColor);

    snprintf(infoLine, sizeof(infoLine),
             ru ? "Ручн. рект.: цели слева, телеметрия справа"
                : "Manual rect: targets left, live telemetry right");
    if (hasWaterOut) {
      snprintf(auxLine, sizeof(auxLine), "V %.0f | Atm %.0f | Water %.1f",
               state.power.voltage, state.pressure.atmosphere * 0.750062f,
               state.temps.waterOut);
    } else {
      snprintf(auxLine, sizeof(auxLine), "V %.0f | Atm %.0f | Pump %.0f",
               state.power.voltage, state.pressure.atmosphere * 0.750062f,
               state.pump.speedMlPerHour);
    }
  } else if (state.mode == Mode::MASHING || state.mode == Mode::HOLD) {
    const bool isMash = (state.mode == Mode::MASHING);
    const uint8_t steps =
        isMash
            ? ((state.mashing.stepCount > 0) ? state.mashing.stepCount
                                             : mashProfileDefault.stepCount)
            : ((state.hold.stepCount > 0) ? state.hold.stepCount
                                          : holdStepsCount);
    const uint8_t currentStep =
        isMash ? state.mashing.currentStep : state.hold.currentStep;
    const float targetTemp =
        isMash ? state.mashing.targetTemp : state.hold.targetTemp;
    const bool inRange =
        isMash ? state.mashing.tempInRange : state.hold.tempInRange;
    const uint32_t currentStepTargetSec =
        isMash
            ? state.mashing.stepDuration
            : ((currentStep < state.hold.stepCount)
                   ? static_cast<uint32_t>(state.hold.steps[currentStep].duration) *
                         60UL
                   : 0UL);
    const bool useCooling =
        (!isMash && currentStep < state.hold.stepCount)
            ? state.hold.steps[currentStep].useCooling
            : false;
    const int16_t rowGap = 4;
    const uint8_t visibleRows = (steps < 4) ? steps : 4;
    const int16_t rowH =
        (visibleRows > 0)
            ? (ROOT_PANEL_H - (visibleRows - 1) * rowGap) / visibleRows
            : ROOT_PANEL_H;
    uint8_t startStep = 0;

    if (steps > visibleRows && visibleRows > 0) {
      startStep = (currentStep > 0) ? static_cast<uint8_t>(currentStep - 1) : 0;
      if (startStep + visibleRows > steps) {
        startStep = steps - visibleRows;
      }
    }

    drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
    drawPanelHeader(leftX, ROOT_PANEL_Y, leftW, getDisplayPhaseName(state),
                    inRange ? COLOR_SUCCESS : COLOR_WARNING);

    char rowBuf[40];
    char timerBuf[24];
    char elapsedBuf[12];
    char targetBuf[12];
    const uint32_t elapsedSec = (phaseElapsedOverrideSec == 0xFFFFFFFFUL)
                                    ? FSM::getPhaseElapsedSec()
                                    : phaseElapsedOverrideSec;
    formatDurationCompact(elapsedSec, elapsedBuf, sizeof(elapsedBuf));
    formatDurationCompact(currentStepTargetSec, targetBuf, sizeof(targetBuf));
    snprintf(timerBuf, sizeof(timerBuf), "%s/%s", elapsedBuf, targetBuf);

    snprintf(rowBuf, sizeof(rowBuf), "%u/%u",
             static_cast<unsigned>(steps == 0 ? 0 : currentStep + 1),
             static_cast<unsigned>(steps));
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 36, leftW - 16,
                           ru ? "ШАГ" : "STEP", rowBuf, colorAccent());
    if (targetTemp > 0.0f) {
      snprintf(rowBuf, sizeof(rowBuf), "%.1f C", targetTemp);
    } else {
      snprintf(rowBuf, sizeof(rowBuf), "%s", ru ? "ПАУЗА" : "PAUSE");
    }
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 58, leftW - 16,
                           ru ? "ЦЕЛЬ" : "TARGET", rowBuf, COLOR_SUCCESS);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 80, leftW - 16,
                           ru ? "ТАЙМЕР" : "TIMER", timerBuf, COLOR_PRIMARY);
    snprintf(rowBuf, sizeof(rowBuf), "%.1f C", state.temps.cube);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 102, leftW - 16,
                           msg(Msg::CUBE_TEMP), rowBuf, COLOR_DANGER);
    if (isMash) {
      if (state.stirrer.running) {
        snprintf(rowBuf, sizeof(rowBuf), "%u%%",
                 static_cast<unsigned>(state.stirrer.speedPercent));
      } else {
        snprintf(rowBuf, sizeof(rowBuf), "%s", ru ? "ВЫКЛ" : "OFF");
      }
      drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 124, leftW - 16,
                             ru ? "МЕШАЛКА" : "STIRRER", rowBuf,
                             state.stirrer.running ? COLOR_SUCCESS
                                                   : colorMuted());
    } else {
      snprintf(rowBuf, sizeof(rowBuf), "%s",
               useCooling ? (ru ? "ОХЛАЖД." : "COOLING")
                          : (ru ? "НАГРЕВ" : "HEATING"));
      drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 124, leftW - 16,
                             ru ? "КОНТУР" : "LOOP", rowBuf,
                             useCooling ? COLOR_INFO : COLOR_WARNING);
    }
    drawStateBadge(leftX + 8, ROOT_PANEL_Y + ROOT_PANEL_H - 22, leftW - 16, 14,
                   inRange ? (ru ? "В ДОПУСКЕ" : "IN RANGE")
                           : (ru ? "НАБОР ТЕМП" : "HEATING"),
                   inRange ? COLOR_SUCCESS : COLOR_WARNING);

    drawCard(rightX, ROOT_PANEL_Y, rightW, ROOT_PANEL_H, colorCard());
    for (uint8_t i = 0; i < visibleRows; i++) {
      const uint8_t stepIndex = startStep + i;
      const int16_t rowY = ROOT_PANEL_Y + i * (rowH + rowGap);
      const bool isCurrent = stepIndex == currentStep;
      const bool completed = stepIndex < currentStep;
      uint8_t progress = completed ? 100 : 0;
      float stepTemp = 0.0f;
      uint16_t stepMinutes = 0;
      bool stepCooling = false;
      const char *stepName = nullptr;

      if (isMash) {
        if (stepIndex < mashProfileDefault.stepCount) {
          stepTemp = mashProfileDefault.steps[stepIndex].temperature;
          stepMinutes = mashProfileDefault.steps[stepIndex].duration;
          stepName = mashProfileDefault.steps[stepIndex].name;
        }
        if (isCurrent && currentStepTargetSec > 0 && inRange) {
          uint32_t p = (elapsedSec * 100UL) / currentStepTargetSec;
          if (p > 100UL) {
            p = 100UL;
          }
          progress = static_cast<uint8_t>(p);
        }
      } else {
        if (stepIndex < holdStepsCount) {
          stepTemp = holdStepsDefault[stepIndex].temperature;
          stepMinutes = holdStepsDefault[stepIndex].duration;
          stepCooling = holdStepsDefault[stepIndex].useCooling;
        }
        if (stepIndex < state.hold.stepCount) {
          stepTemp = state.hold.steps[stepIndex].temperature;
          stepMinutes = state.hold.steps[stepIndex].duration;
          stepCooling = state.hold.steps[stepIndex].useCooling;
        }
        if (isCurrent && currentStepTargetSec > 0 && inRange) {
          uint32_t p = (elapsedSec * 100UL) / currentStepTargetSec;
          if (p > 100UL) {
            p = 100UL;
          }
          progress = static_cast<uint8_t>(p);
        }
      }

      const uint16_t rowBg =
          isCurrent ? tft.color565(235, 245, 255)
                    : (completed ? tft.color565(236, 248, 240) : colorCard());
      drawCard(rightX, rowY, rightW, rowH, rowBg);
      if (isCurrent) {
        tft.fillRect(rightX + 1, rowY + 1, rightW - 2, 3,
                     inRange ? COLOR_SUCCESS : colorAccent());
      }

      char titleBuf[28];
      char metaBuf[16];
      char detailBuf[32];
      if (isMash && stepName != nullptr && stepName[0] != '\0') {
        snprintf(titleBuf, sizeof(titleBuf), "%u. %.18s",
                 static_cast<unsigned>(stepIndex + 1), stepName);
      } else if (!isMash && stepCooling) {
        snprintf(titleBuf, sizeof(titleBuf), "%u. %s",
                 static_cast<unsigned>(stepIndex + 1),
                 ru ? "ОХЛАЖДЕНИЕ" : "COOLING");
      } else {
        snprintf(titleBuf, sizeof(titleBuf), "%u. %s",
                 static_cast<unsigned>(stepIndex + 1),
                 ru ? "СТУПЕНЬ" : "STEP");
      }
      snprintf(metaBuf, sizeof(metaBuf), "%u min",
               static_cast<unsigned>(stepMinutes));
      if (stepTemp > 0.0f) {
        snprintf(detailBuf, sizeof(detailBuf), "%.1f C", stepTemp);
      } else {
        snprintf(detailBuf, sizeof(detailBuf), "%s",
                 ru ? "ПАУЗА БЕЗ НАГРЕВА" : "NO-HEAT PAUSE");
      }

      tft.setTextColor(isCurrent ? colorAccent() : colorFg());
      tft.setTextSize(1);
      tft.setTextDatum(middle_left);
      tft.drawString(titleBuf, rightX + 10, rowY + 10);
      tft.setTextColor(colorMuted());
      tft.setTextDatum(middle_right);
      tft.drawString(metaBuf, rightX + rightW - 10, rowY + 10);
      tft.setTextColor(completed ? COLOR_SUCCESS : COLOR_PRIMARY);
      tft.setTextDatum(middle_left);
      tft.drawString(detailBuf, rightX + 10, rowY + 22);
      drawProgressBar(rightX + 10, rowY + rowH - 7, rightW - 20, 4, progress,
                      completed ? COLOR_SUCCESS : colorAccent());
      tft.setTextDatum(top_left);
    }

    snprintf(infoLine, sizeof(infoLine),
             isMash ? (ru ? "Затирка: шаги справа, цель слева"
                            : "Mashing: step list right, target and timer left")
                    : (ru ? "Пастер.: шаги справа, контур слева"
                          : "Hold: step list right, control loop and target left"));
    if (isMash) {
      snprintf(auxLine, sizeof(auxLine), "P %.0fW | Stir %s | Cube %.1f",
               state.power.power, state.stirrer.running ? "ON" : "OFF",
               state.temps.cube);
    } else {
      snprintf(auxLine, sizeof(auxLine), "P %.0fW | %s | Cube %.1f",
               state.power.power, useCooling ? "Cooling" : "Heating",
               state.temps.cube);
    }
  } else if (state.mode == Mode::DISTILLATION) {
    if (layoutChanged) {
      drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
      drawValueTileShell(rightX, ROOT_PANEL_Y, tileW, tileH, msg(Msg::CUBE_TEMP));
      drawValueTileShell(x2, ROOT_PANEL_Y, tileW, tileH,
                         ru ? "МОЩН. %" : "POWER %");
      drawValueTileShell(rightX, y2, tileW, tileH, msg(Msg::DIST_SPEED));
      drawValueTileShell(x2, y2, tileW, tileH,
                         ru ? "ОТБОР" : "COLLECT");
      drawValueTileShell(rightX, y3, tileW, tileH,
                         ru ? "РАБОТА" : "RUN");
      drawValueTileShell(x2, y3, tileW, tileH, msg(Msg::END_TEMP));
    }

    char rowBuf[24];
    char runBuf[16];
    char v[6][20];
    formatDurationCompact(getModeRunElapsedSec(state), runBuf, sizeof(runBuf));
    snprintf(v[0], sizeof(v[0]), "%.1f", state.temps.cube);
    snprintf(v[1], sizeof(v[1]), "%.0f", distUi.powerPercent);
    snprintf(v[2], sizeof(v[2]), "%.0f", distUi.speedMlH);
    snprintf(v[3], sizeof(v[3]), "%.0f", state.pump.totalVolumeMl);
    snprintf(v[4], sizeof(v[4]), "%s", runBuf);
    snprintf(v[5], sizeof(v[5]), "%.1f", distUi.endTempC);

    updateTile(0, rightX, ROOT_PANEL_Y, tileW, tileH, v[0], "C", COLOR_DANGER);
    updateTile(1, x2, ROOT_PANEL_Y, tileW, tileH, v[1], "%", COLOR_WARNING);
    updateTile(2, rightX, y2, tileW, tileH, v[2], msg(Msg::UNIT_ML_H),
               COLOR_SUCCESS);
    updateTile(3, x2, y2, tileW, tileH, v[3], "ml", COLOR_INFO);
    updateTile(4, rightX, y3, tileW, tileH, v[4], "", COLOR_PRIMARY);
    updateTile(5, x2, y3, tileW, tileH, v[5], "C", COLOR_INFO);

    drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
    drawPanelHeader(leftX, ROOT_PANEL_Y, leftW, getDisplayPhaseName(state),
                    state.paused ? COLOR_WARNING : colorAccent());
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 36, leftW - 16,
                           ru ? "РЕЖИМ" : "MODE",
                           getDisplayModeName(state.mode), colorAccent());
    snprintf(rowBuf, sizeof(rowBuf), "%.0f ml", distUi.headsVolumeMl);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 58, leftW - 16,
                           ru ? "ГОЛОВЫ" : "HEADS", rowBuf, COLOR_WARNING);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f ml", distUi.targetVolumeMl);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 80, leftW - 16,
                           ru ? "ЦЕЛЬ" : "TARGET", rowBuf, COLOR_SUCCESS);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f mm", state.pressure.cube);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 102, leftW - 16,
                           ru ? "ДАВЛ." : "PRESS", rowBuf, COLOR_WARNING);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f V", state.power.voltage);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 124, leftW - 16,
                           ru ? "СЕТЬ" : "MAINS", rowBuf, COLOR_PRIMARY);
    drawStateBadge(leftX + 8, ROOT_PANEL_Y + ROOT_PANEL_H - 22, leftW - 16, 14,
                   header.procState, header.procColor);

    snprintf(infoLine, sizeof(infoLine),
             ru ? "Дистил.: куб, отбор, финиш"
                : "Distillation: cube, speed, volume and finish temp");
    snprintf(auxLine, sizeof(auxLine), "V %.0f | P %.0f | Pump %.0f",
             state.power.voltage, state.pressure.cube,
             state.pump.speedMlPerHour);
  } else if (state.mode == Mode::NBK) {
    if (layoutChanged) {
      drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
      drawValueTileShell(rightX, ROOT_PANEL_Y, tileW, tileH,
                         msg(Msg::COLUMN_BOTTOM));
      drawValueTileShell(x2, ROOT_PANEL_Y, tileW, tileH, msg(Msg::CUBE_TEMP));
      drawValueTileShell(rightX, y2, tileW, tileH, msg(Msg::HEATER_POWER));
      drawValueTileShell(x2, y2, tileW, tileH, msg(Msg::PUMP));
      drawValueTileShell(rightX, y3, tileW, tileH,
                         ru ? "ДАВЛ." : "PRESS");
      drawValueTileShell(x2, y3, tileW, tileH,
                         hasWaterOut ? (ru ? "ОХЛ ВЫХ" : "WATER OUT")
                                     : (ru ? "ЦЕЛЬ" : "TARGET"));
    }

    char rowBuf[24];
    char v[6][20];
    snprintf(v[0], sizeof(v[0]), "%.1f", state.temps.columnBottom);
    snprintf(v[1], sizeof(v[1]), "%.1f", state.temps.cube);
    snprintf(v[2], sizeof(v[2]), "%.0f", state.power.power);
    snprintf(v[3], sizeof(v[3]), "%.0f", state.pump.speedMlPerHour);
    snprintf(v[4], sizeof(v[4]), "%.0f", state.pressure.cube);
    if (hasWaterOut) {
      snprintf(v[5], sizeof(v[5]), "%.1f", state.temps.waterOut);
    } else {
      snprintf(v[5], sizeof(v[5]), "%.0f", g_settings.nbk.pumpSpeedMlH);
    }

    updateTile(0, rightX, ROOT_PANEL_Y, tileW, tileH, v[0], "C", colorAccent());
    updateTile(1, x2, ROOT_PANEL_Y, tileW, tileH, v[1], "C", COLOR_DANGER);
    updateTile(2, rightX, y2, tileW, tileH, v[2], msg(Msg::UNIT_W),
               COLOR_WARNING);
    updateTile(3, x2, y2, tileW, tileH, v[3], msg(Msg::UNIT_ML_H),
               COLOR_SUCCESS);
    updateTile(4, rightX, y3, tileW, tileH, v[4], "mm", COLOR_INFO);
    updateTile(5, x2, y3, tileW, tileH, v[5],
               hasWaterOut ? "C" : msg(Msg::UNIT_ML_H),
               hasWaterOut ? COLOR_INFO : colorAccent());

    drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
    drawPanelHeader(leftX, ROOT_PANEL_Y, leftW, getDisplayPhaseName(state),
                    state.paused ? COLOR_WARNING : colorAccent());
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 36, leftW - 16,
                           ru ? "РЕЖИМ" : "MODE",
                           getDisplayModeName(state.mode), colorAccent());
    snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
             g_settings.nbk.columnBottomTempThresholdC);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 58, leftW - 16,
                           ru ? "ПОРОГ" : "THRESH", rowBuf, COLOR_WARNING);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f ml", g_settings.nbk.targetVolumeMl);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 80, leftW - 16,
                           ru ? "ЦЕЛЬ" : "TARGET", rowBuf, COLOR_SUCCESS);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f ml", state.pump.totalVolumeMl);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 102, leftW - 16,
                           ru ? "ОТБОР" : "COLLECT", rowBuf, COLOR_INFO);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f V", state.power.voltage);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 124, leftW - 16,
                           ru ? "СЕТЬ" : "MAINS", rowBuf, COLOR_PRIMARY);
    drawStateBadge(leftX + 8, ROOT_PANEL_Y + ROOT_PANEL_H - 22, leftW - 16, 14,
                   header.procState, header.procColor);

    snprintf(infoLine, sizeof(infoLine),
             ru ? "НБК: низ колонны, подача, давление"
                : "NBK: column bottom, feed, pressure and cooling");
    snprintf(auxLine, sizeof(auxLine), "Target %.0f | Pump %.0f",
             g_settings.nbk.targetVolumeMl, state.pump.speedMlPerHour);
  } else if (state.mode == Mode::FERMENTATION) {
    if (layoutChanged) {
      drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
      drawValueTileShell(rightX, ROOT_PANEL_Y, tileW, tileH, msg(Msg::CUBE_TEMP));
      drawValueTileShell(x2, ROOT_PANEL_Y, tileW, tileH,
                         ru ? "ЦЕЛЬ" : "TARGET");
      drawValueTileShell(rightX, y2, tileW, tileH,
                         ru ? "ДОПУСК" : "BAND");
      drawValueTileShell(x2, y2, tileW, tileH,
                         ru ? "РАБОТА" : "RUN");
      drawValueTileShell(rightX, y3, tileW, tileH,
                         ru ? "DELTA" : "DELTA");
      drawValueTileShell(x2, y3, tileW, tileH,
                         ru ? "НАГРЕВ" : "HEATER");
    }

    char rowBuf[24];
    char runBuf[20];
    char v[6][20];
    const float delta =
        state.temps.cube - g_settings.fermentation.targetTempC;
    formatDurationCompact(getModeRunElapsedSec(state), runBuf, sizeof(runBuf));
    snprintf(v[0], sizeof(v[0]), "%.1f", state.temps.cube);
    snprintf(v[1], sizeof(v[1]), "%.1f", g_settings.fermentation.targetTempC);
    snprintf(v[2], sizeof(v[2]), "%.1f", g_settings.fermentation.hysteresisC);
    snprintf(v[3], sizeof(v[3]), "%s", runBuf);
    snprintf(v[4], sizeof(v[4]), "%.1f", delta);
    snprintf(v[5], sizeof(v[5]), "%s",
             g_settings.fermentation.useHeater ? "ON" : "OFF");

    updateTile(0, rightX, ROOT_PANEL_Y, tileW, tileH, v[0], "C",
               fabsf(delta) > g_settings.fermentation.hysteresisC
                   ? COLOR_WARNING
                   : COLOR_SUCCESS);
    updateTile(1, x2, ROOT_PANEL_Y, tileW, tileH, v[1], "C", colorAccent());
    updateTile(2, rightX, y2, tileW, tileH, v[2], "C", COLOR_INFO);
    updateTile(3, x2, y2, tileW, tileH, v[3], "", COLOR_PRIMARY);
    updateTile(4, rightX, y3, tileW, tileH, v[4], "C",
               (delta > 0.0f) ? COLOR_WARNING : COLOR_INFO);
    updateTile(5, x2, y3, tileW, tileH, v[5], "",
               g_settings.fermentation.useHeater ? COLOR_SUCCESS
                                                 : colorMuted());

    drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
    drawPanelHeader(leftX, ROOT_PANEL_Y, leftW, getDisplayPhaseName(state),
                    state.paused ? COLOR_WARNING : colorAccent());
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 36, leftW - 16,
                           ru ? "РЕЖИМ" : "MODE",
                           getDisplayModeName(state.mode), colorAccent());
    snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
             g_settings.fermentation.targetTempC);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 58, leftW - 16,
                           ru ? "ЦЕЛЬ" : "TARGET", rowBuf, COLOR_SUCCESS);
    snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
             g_settings.fermentation.hysteresisC);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 80, leftW - 16,
                           ru ? "ДОПУСК" : "BAND", rowBuf, COLOR_INFO);
    snprintf(rowBuf, sizeof(rowBuf), "%.0f h",
             g_settings.fermentation.durationHours);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 102, leftW - 16,
                           ru ? "ПЛАН" : "PLAN", rowBuf, COLOR_PRIMARY);
    drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 124, leftW - 16,
                           ru ? "НАГРЕВ" : "HEATER",
                           g_settings.fermentation.useHeater ? "ON" : "OFF",
                           g_settings.fermentation.useHeater ? COLOR_SUCCESS
                                                             : colorMuted());
    drawStateBadge(leftX + 8, ROOT_PANEL_Y + ROOT_PANEL_H - 22, leftW - 16, 14,
                   header.procState, header.procColor);

    snprintf(infoLine, sizeof(infoLine),
             ru ? "Брожение: цель, допуск, нагрев"
                : "Fermentation: temp, band, delta and heater");
    snprintf(auxLine, sizeof(auxLine), "%s | Plan %.0f h",
             g_settings.fermentation.useHeater ? "Heater ON" : "Heater OFF",
             g_settings.fermentation.durationHours);
  } else {
    renderModeMonitorCustom(state, full);
    return;
  }

  if (layoutChanged) {
    g_dashboardCache.layoutKey = layoutKey;
  }

  renderRootFooter(infoLine, auxLine, upBuf, full);

  tft.setFont(&fonts::efontJA_16);
  tft.setTextDatum(top_left);
}

static void renderModeMonitorCustom(const SystemState &state, bool full) {
  const bool ru = (g_settings.language == 0);
  const int16_t panelY = ROOT_PANEL_Y;
  const int16_t panelH = ROOT_PANEL_H;
  const int16_t infoY = ROOT_INFO_Y;
  const bool hasWaterOut = state.temps.valid[TEMP_WATER_OUT];
  const uint8_t layoutKey = static_cast<uint8_t>(
      0xC0 | ((static_cast<uint8_t>(state.mode) & 0x0F) << 1) |
      (hasWaterOut ? 0x01 : 0x00));

  if (full) {
    drawRootScaffold(UI_MODE_MONITOR);
    memset(&g_dashboardCache, 0, sizeof(g_dashboardCache));
    g_dashboardCache.layoutKey = 0xFF;
  }

  const bool layoutChanged = full || (g_dashboardCache.layoutKey != layoutKey);
  if (layoutChanged) {
    memset(g_modeTileCache, 0, sizeof(g_modeTileCache));
    g_dashboardCache.infoLine[0] = '\0';
    g_dashboardCache.ioLine[0] = '\0';
    g_dashboardCache.uptime[0] = '\0';
  }

  uint32_t phaseElapsedSec = FSM::getPhaseElapsedSec();
  const uint32_t nowMs = millis();
  if (state.mode == Mode::MASHING) {
    phaseElapsedSec = 0;
    if (state.mashing.tempInRange && state.mashing.inRangeStartTime > 0 &&
        nowMs >= state.mashing.inRangeStartTime) {
      phaseElapsedSec = (nowMs - state.mashing.inRangeStartTime) / 1000UL;
    }
  } else if (state.mode == Mode::HOLD) {
    phaseElapsedSec = 0;
    if (state.hold.tempInRange && state.hold.inRangeStartTime > 0 &&
        nowMs >= state.hold.inRangeStartTime) {
      phaseElapsedSec = (nowMs - state.hold.inRangeStartTime) / 1000UL;
    }
  }
  RootHeaderState header = buildRootHeaderState(state, phaseElapsedSec);
  renderRootStatusBar(header, full);

  auto updateTile = [&](uint8_t idx, int16_t x, int16_t y, int16_t w, int16_t h,
                        const char *value, const char *unit, uint16_t color) {
    if (full || strcmp(g_modeTileCache[idx], value) != 0) {
      drawValueTileValue(x, y, w, h, value, unit, color);
      strncpy(g_modeTileCache[idx], value, sizeof(g_modeTileCache[idx]) - 1);
      g_modeTileCache[idx][sizeof(g_modeTileCache[idx]) - 1] = '\0';
    }
  };

  char infoLine[96] = "";
  char auxLine[96] = "";
  char upBuf[16];
  formatDurationCompact(getModeRunElapsedSec(state), upBuf, sizeof(upBuf));

  if (state.mode == Mode::DISTILLATION) {
    const int16_t g = 6;
    const int16_t hTile = 74;
    const int16_t w2 = (TFT_WIDTH - 20 - g) / 2;
    const int16_t x1 = 10;
    const int16_t x2 = x1 + w2 + g;
    const int16_t y1 = panelY + hTile + g;

    if (layoutChanged) {
      drawValueTileShell(x1, panelY, w2, hTile, msg(Msg::CUBE_TEMP));
      drawValueTileShell(x2, panelY, w2, hTile, ru ? "МОЩН. %" : "POWER %");
      drawValueTileShell(x1, y1, w2, hTile, ru ? "ВРЕМЯ РАБОТЫ" : "RUN TIME");
      drawValueTileShell(x2, y1, w2, hTile, msg(Msg::END_TEMP));
    }

    char runBuf[16];
    char v[4][20];
    formatDurationCompact(getModeRunElapsedSec(state), runBuf, sizeof(runBuf));
    snprintf(v[0], sizeof(v[0]), "%.1f", state.temps.cube);
    snprintf(v[1], sizeof(v[1]), "%.0f", distUi.powerPercent);
    snprintf(v[2], sizeof(v[2]), "%s", runBuf);
    snprintf(v[3], sizeof(v[3]), "%.1f", distUi.endTempC);

    updateTile(0, x1, panelY, w2, hTile, v[0], "C", COLOR_DANGER);
    updateTile(1, x2, panelY, w2, hTile, v[1], "%", COLOR_WARNING);
    updateTile(2, x1, y1, w2, hTile, v[2], "", COLOR_PRIMARY);
    updateTile(3, x2, y1, w2, hTile, v[3], "C", COLOR_INFO);

    snprintf(infoLine, sizeof(infoLine),
             ru ? "Дистил.: только ключевые параметры"
                : "Distillation: key parameters only");
    snprintf(auxLine, sizeof(auxLine), "V %.0f | P %.0f | Pump %.0f",
             state.power.voltage, state.pressure.cube,
             state.pump.speedMlPerHour);
  } else if (state.mode == Mode::MANUAL_RECT) {
    const int16_t leftX = 10;
    const int16_t leftW = 154;
    const int16_t rowH = 28;
    const int16_t rowGap = 4;
    const int16_t rightX = 170;
    const int16_t rightW = TFT_WIDTH - rightX - 10;
    const int16_t colW = (rightW - 6) / 2;
    const int16_t rightRowH = 36;
    const int16_t rightGap = 4;

    if (layoutChanged) {
      drawCard(leftX, panelY, leftW, panelH, colorCard());
      const char *labels[8] = {msg(Msg::CUBE_TEMP),
                               msg(Msg::TOP_T),
                               msg(Msg::REFLUX_T),
                               msg(Msg::TSA_T),
                               ru ? "ДАВЛ КУБА" : "CUBE PRESS",
                               ru ? "ДАВЛ АТМ"  : "ATM PRESS",
                               ru ? "СЕТЬ"      : "MAINS",
                               hasWaterOut ? (ru ? "ОХЛ ВЫХ" : "WATER OUT") : msg(Msg::PUMP)};
      for (uint8_t i = 0; i < 8; i++) {
        const int16_t cx = rightX + ((i % 2) * (colW + 6));
        const int16_t cy = panelY + ((i / 2) * (rightRowH + rightGap));
        drawCard(cx, cy, colW, rightRowH, colorCard());
        tft.setTextColor(tft.color565(96, 104, 116));
        tft.setTextSize(1);
        tft.setTextDatum(top_left);
        // Split two-word labels onto two lines only if too wide
        const char *sp = strchr(labels[i], ' ');
        if (sp && tft.textWidth(labels[i]) > colW - 8) {
          char first[16];
          size_t len = sp - labels[i];
          if (len >= sizeof(first)) len = sizeof(first) - 1;
          memcpy(first, labels[i], len);
          first[len] = '\0';
          tft.drawString(first, cx + 4, cy + 2);
          tft.drawString(sp + 1, cx + 4, cy + 11);
        } else {
          tft.drawString(labels[i], cx + 4, cy + 2);
        }
        tft.setTextDatum(top_left);
      }
    }

    const char *rowLabels[5] = {
        ru ? "Скорость" : "Speed",
        ru ? "Мощность" : "Power",
        ru ? "Головы"   : "Heads",
        ru ? "Тело"     : "Body",
        ru ? "Хвосты"   : "Tails"
    };
    char rowVals[5][24];
    snprintf(rowVals[0], sizeof(rowVals[0]), "%.0f ml/h",
             manualRectUi.speedMlH);
    snprintf(rowVals[1], sizeof(rowVals[1]), "%.0f %%",
             manualRectUi.powerPercent);
    snprintf(rowVals[2], sizeof(rowVals[2]), "%.0f/%.0f ml",
             manualRectUi.headsTargetMl, state.stats.headsVolume);
    snprintf(rowVals[3], sizeof(rowVals[3]), "%.0f/%.0f ml",
             manualRectUi.bodyTargetMl, state.stats.bodyVolume);
    snprintf(rowVals[4], sizeof(rowVals[4]), "%.0f/%.0f ml",
             manualRectUi.tailsTargetMl, state.stats.tailsVolume);
    for (uint8_t i = 0; i < 5; i++) {
      const int16_t ry = panelY + i * (rowH + rowGap);
      const uint16_t bg = (i <= 1) ? tft.color565(236, 245, 255) : colorCard();
      drawCard(leftX, ry, leftW, rowH, bg);
      tft.setTextColor(tft.color565(90, 100, 112));
      tft.setTextDatum(middle_left);
      tft.drawString(rowLabels[i], leftX + 8, ry + 9);
      tft.setTextColor(colorAccent());
      tft.setTextDatum(middle_right);
      tft.drawString(rowVals[i], leftX + leftW - 8, ry + 9);
    }

    char v[8][20];
    snprintf(v[0], sizeof(v[0]), "%.1f", state.temps.cube);
    snprintf(v[1], sizeof(v[1]), "%.1f", state.temps.columnTop);
    snprintf(v[2], sizeof(v[2]), "%.1f", state.temps.reflux);
    snprintf(v[3], sizeof(v[3]), "%.1f", state.temps.tsa);
    snprintf(v[4], sizeof(v[4]), "%.0f", state.pressure.cube);
    snprintf(v[5], sizeof(v[5]), "%.0f", state.pressure.atmosphere * 0.750062f);
    snprintf(v[6], sizeof(v[6]), "%.0f", state.power.voltage);
    if (hasWaterOut)
      snprintf(v[7], sizeof(v[7]), "%.1f", state.temps.waterOut);
    else
      snprintf(v[7], sizeof(v[7]), "%.0f", state.pump.speedMlPerHour);

    for (uint8_t i = 0; i < 8; i++) {
      const int16_t cx = rightX + ((i % 2) * (colW + 6));
      const int16_t cy = panelY + ((i / 2) * (rightRowH + rightGap));
      const char *unit = "C";
      if (i == 4)
        unit = "mm";
      else if (i == 5)
        unit = "mm";
      else if (i == 6)
        unit = "V";
      else if (i == 7)
        unit = hasWaterOut ? "C" : msg(Msg::UNIT_ML_H);
      uint16_t color =
          (i == 0)
              ? COLOR_DANGER
              : ((i == 1)
                     ? colorAccent()
                     : ((i == 2) ? COLOR_INFO
                                 : ((i == 3) ? COLOR_WARNING : COLOR_PRIMARY)));
      if (i == 4)
        color = COLOR_WARNING;
      if (i == 5)
        color = COLOR_INFO;
      if (i == 7)
        color = hasWaterOut ? COLOR_INFO : COLOR_SUCCESS;
      updateTile(i, cx, cy, colW, rightRowH, v[i], unit, color);
    }

    snprintf(infoLine, sizeof(infoLine),
             ru ? "Ручн. рект.: правка слева, датчики справа"
                : "Manual rect: edit speed/power/fractions on left");
    snprintf(auxLine, sizeof(auxLine),
             ru ? "Куб %.0f | Атм %.0f mm | V %.0f"
                : "Cube %.0f | Atm %.0f mm | V %.0f",
             state.pressure.cube, state.pressure.atmosphere * 0.750062f,
             state.power.voltage);
  } else if (state.mode == Mode::MASHING || state.mode == Mode::HOLD) {
    const bool isMash = (state.mode == Mode::MASHING);
    uint8_t steps = isMash ? mashProfileDefault.stepCount : holdStepsCount;
    if (!isMash && steps == 0 && state.hold.stepCount > 0)
      steps = state.hold.stepCount;
    const int16_t listX = 10;
    const int16_t listW = TFT_WIDTH - 20;
    const int16_t rowGap = 4;
    const int16_t listH = panelH;
    int16_t rowH = (steps > 0) ? (listH - (steps - 1) * rowGap) / steps : listH;
    if (rowH < (isMash ? 28 : 32))
      rowH = isMash ? 28 : 32;

    if (layoutChanged) {
      drawCard(listX, panelY, listW, listH, colorCard());
    }

    if (steps == 0) {
      tft.fillRect(listX + 8, panelY + 8, listW - 16, listH - 16, colorCard());
      tft.setTextColor(colorFg());
      tft.setTextDatum(middle_center);
      tft.drawString(isMash ? "Mashing profile is empty"
                            : "Hold step list is empty",
                     TFT_WIDTH / 2, panelY + listH / 2);
    } else {
      for (uint8_t i = 0; i < steps; i++) {
        const int16_t ry = panelY + i * (rowH + rowGap);
        const bool current =
            isMash ? (state.mashing.active && i == state.mashing.currentStep)
                   : (state.hold.active && i == state.hold.currentStep);
        drawCard(listX, ry, listW, rowH,
                 current ? tft.color565(235, 245, 255) : colorCard());

        float tSet = 0.0f;
        uint16_t dSet = 0;
        if (isMash) {
          tSet = mashProfileDefault.steps[i].temperature;
          dSet = mashProfileDefault.steps[i].duration;
        } else if (i < holdStepsCount) {
          tSet = holdStepsDefault[i].temperature;
          dSet = holdStepsDefault[i].duration;
        } else if (state.hold.active && i < state.hold.stepCount) {
          tSet = state.hold.steps[i].temperature;
          dSet = state.hold.steps[i].duration;
        }

        uint8_t progress = 0;
        if (isMash && state.mashing.active) {
          if (i < state.mashing.currentStep)
            progress = 100;
          else if (i == state.mashing.currentStep && dSet > 0 &&
                   state.mashing.tempInRange &&
                   state.mashing.inRangeStartTime > 0 &&
                   nowMs >= state.mashing.inRangeStartTime) {
            uint32_t elapsedSec =
                (nowMs - state.mashing.inRangeStartTime) / 1000UL;
            uint32_t targetSec = static_cast<uint32_t>(dSet) * 60UL;
            uint32_t p =
                (targetSec > 0) ? ((elapsedSec * 100UL) / targetSec) : 0;
            if (p > 100)
              p = 100;
            progress = static_cast<uint8_t>(p);
          }
        } else if (!isMash && state.hold.active) {
          if (i < state.hold.currentStep)
            progress = 100;
          else if (i == state.hold.currentStep && dSet > 0 &&
                   state.hold.tempInRange && state.hold.inRangeStartTime > 0 &&
                   nowMs >= state.hold.inRangeStartTime) {
            uint32_t elapsedSec =
                (nowMs - state.hold.inRangeStartTime) / 1000UL;
            uint32_t targetSec = static_cast<uint32_t>(dSet) * 60UL;
            uint32_t p =
                (targetSec > 0) ? ((elapsedSec * 100UL) / targetSec) : 0;
            if (p > 100)
              p = 100;
            progress = static_cast<uint8_t>(p);
          }
        }

        char leftBuf[40];
        char rightBuf[24];
        snprintf(leftBuf, sizeof(leftBuf), "%u. %.1f C",
                 static_cast<unsigned>(i + 1), tSet);
        snprintf(rightBuf, sizeof(rightBuf), "%u min",
                 static_cast<unsigned>(dSet));

        tft.setTextColor(current ? colorAccent() : colorFg());
        tft.setTextDatum(middle_left);
        tft.drawString(leftBuf, listX + 10, ry + (isMash ? 10 : 12));
        tft.setTextColor(tft.color565(90, 100, 112));
        tft.setTextDatum(middle_right);
        tft.drawString(rightBuf, listX + listW - 10, ry + (isMash ? 10 : 12));

        const int16_t pbX = listX + 10;
        const int16_t pbY = ry + rowH - 9;
        const int16_t pbW = listW - 20;
        drawProgressBar(pbX, pbY, pbW, 5, progress, colorAccent());
        tft.drawFastVLine(listX + listW / 2, ry + 6, rowH - 12,
                          tft.color565(214, 220, 228));
      }
    }

    if (isMash) {
      snprintf(infoLine, sizeof(infoLine),
               ru ? "Затирка: слева температура, справа время"
                  : "Mashing: tap left half for temp, right half for time");
      snprintf(auxLine, sizeof(auxLine), "Step %u/%u | Target %.1f C",
               static_cast<unsigned>(state.mashing.currentStep + 1),
               static_cast<unsigned>(steps), state.mashing.targetTemp);
    } else {
      snprintf(infoLine, sizeof(infoLine),
               ru ? "Пастер.: слева температура, справа время"
                  : "Hold: tap left half for temp, right half for time");
      snprintf(auxLine, sizeof(auxLine), "Step %u/%u | Cube %.1f C",
               static_cast<unsigned>(state.hold.currentStep + 1),
               static_cast<unsigned>(steps), state.temps.cube);
    }
  } else if (state.mode == Mode::NBK) {
    const int16_t g = 6;
    const int16_t tileW = (TFT_WIDTH - 20 - g * 2) / 3;
    const int16_t tileH = (panelH - g) / 2;
    const int16_t x1 = 10;
    const int16_t x2 = x1 + tileW + g;
    const int16_t x3 = x2 + tileW + g;
    const int16_t y2 = panelY + tileH + g;

    if (layoutChanged) {
      drawValueTileShell(x1, panelY, tileW, tileH, msg(Msg::COLUMN_BOTTOM));
      drawValueTileShell(x2, panelY, tileW, tileH, msg(Msg::CUBE_TEMP));
      drawValueTileShell(x3, panelY, tileW, tileH, msg(Msg::HEATER_POWER));
      drawValueTileShell(x1, y2, tileW, tileH, msg(Msg::PUMP));
      drawValueTileShell(x2, y2, tileW, tileH, ru ? "ДАВЛЕНИЕ" : "PRESSURE");
      drawValueTileShell(x3, y2, tileW, tileH,
                         hasWaterOut ? (ru ? "ОХЛ ВЫХ" : "WATER OUT")
                                     : (ru ? "ЦЕЛЬ" : "TARGET"));
    }

    char v[6][20];
    snprintf(v[0], sizeof(v[0]), "%.1f", state.temps.columnBottom);
    snprintf(v[1], sizeof(v[1]), "%.1f", state.temps.cube);
    snprintf(v[2], sizeof(v[2]), "%.0f", state.power.power);
    snprintf(v[3], sizeof(v[3]), "%.0f", state.pump.speedMlPerHour);
    snprintf(v[4], sizeof(v[4]), "%.0f", state.pressure.cube);
    if (hasWaterOut) {
      snprintf(v[5], sizeof(v[5]), "%.1f", state.temps.waterOut);
    } else {
      snprintf(v[5], sizeof(v[5]), "%.0f", g_settings.nbk.pumpSpeedMlH);
    }

    updateTile(0, x1, panelY, tileW, tileH, v[0], "C", colorAccent());
    updateTile(1, x2, panelY, tileW, tileH, v[1], "C", COLOR_DANGER);
    updateTile(2, x3, panelY, tileW, tileH, v[2], msg(Msg::UNIT_W), COLOR_WARNING);
    updateTile(3, x1, y2, tileW, tileH, v[3], msg(Msg::UNIT_ML_H), COLOR_SUCCESS);
    updateTile(4, x2, y2, tileW, tileH, v[4], "mm", COLOR_INFO);
    updateTile(5, x3, y2, tileW, tileH, v[5], hasWaterOut ? "C" : msg(Msg::UNIT_ML_H),
               hasWaterOut ? COLOR_INFO : colorAccent());

    snprintf(infoLine, sizeof(infoLine),
             ru ? "НБК: низ колонны и подача"
                : "NBK: bottom temp and feed rate");
    snprintf(auxLine, sizeof(auxLine), "%s %.1fC | %s %.0f",
             ru ? "Порог" : "Threshold", g_settings.nbk.columnBottomTempThresholdC,
             ru ? "Цель" : "Target", g_settings.nbk.targetVolumeMl);
  } else if (state.mode == Mode::FERMENTATION) {
    const int16_t g = 6;
    const int16_t tileW = (TFT_WIDTH - 20 - g) / 2;
    const int16_t tileH = (panelH - g) / 2;
    const int16_t x1 = 10;
    const int16_t x2 = x1 + tileW + g;
    const int16_t y2 = panelY + tileH + g;

    if (layoutChanged) {
      drawValueTileShell(x1, panelY, tileW, tileH, msg(Msg::CUBE_TEMP));
      drawValueTileShell(x2, panelY, tileW, tileH, ru ? "ЦЕЛЬ" : "TARGET");
      drawValueTileShell(x1, y2, tileW, tileH, ru ? "ДОПУСК" : "BAND");
      drawValueTileShell(x2, y2, tileW, tileH, ru ? "ВРЕМЯ" : "RUN TIME");
    }

    char v[4][20];
    char runBuf[20];
    formatDurationCompact(getModeRunElapsedSec(state), runBuf, sizeof(runBuf));
    snprintf(v[0], sizeof(v[0]), "%.1f", state.temps.cube);
    snprintf(v[1], sizeof(v[1]), "%.1f", g_settings.fermentation.targetTempC);
    snprintf(v[2], sizeof(v[2]), "%.1f", g_settings.fermentation.hysteresisC);
    snprintf(v[3], sizeof(v[3]), "%s", runBuf);

    updateTile(0, x1, panelY, tileW, tileH, v[0], "C",
               state.temps.cube > g_settings.fermentation.targetTempC +
                                     g_settings.fermentation.hysteresisC
                   ? COLOR_WARNING
                   : COLOR_SUCCESS);
    updateTile(1, x2, panelY, tileW, tileH, v[1], "C", colorAccent());
    updateTile(2, x1, y2, tileW, tileH, v[2], "C", COLOR_INFO);
    updateTile(3, x2, y2, tileW, tileH, v[3], "", COLOR_PRIMARY);

    snprintf(infoLine, sizeof(infoLine),
             ru ? "Брожение: держим температуру"
                : "Fermentation: keep temp in band");
    snprintf(auxLine, sizeof(auxLine), "%s | %s %.0f h",
             g_settings.fermentation.useHeater
                 ? (ru ? "Подогрев включен" : "Heater enabled")
                 : (ru ? "Подогрев выключен" : "Heater disabled"),
             ru ? "План" : "Plan", g_settings.fermentation.durationHours);
  } else {
    drawCard(10, panelY, TFT_WIDTH - 20, panelH, colorCard());
    snprintf(infoLine, sizeof(infoLine),
             "Mode has no dedicated monitor layout");
    snprintf(auxLine, sizeof(auxLine), "");
  }

  if (layoutChanged) {
    g_dashboardCache.layoutKey = layoutKey;
  }

  renderRootFooter(infoLine, auxLine, upBuf, full);

  tft.setFont(&fonts::efontJA_16);
  tft.setTextDatum(top_left);
}

static void renderControl(const SystemState &state, bool full) {
  if (full) {
    tft.fillScreen(colorBg());
    drawHeader(msg(Msg::CONTROL), false);
    drawTabs(UI_CONTROL);
  }

  char modeBuf[96];
  const bool ru = (g_settings.language == 0);
  const int16_t stopX = TFT_WIDTH - CTRL_ACTION_BW - 10;
  const int16_t pauseX = stopX - CTRL_ACTION_BW - CTRL_ACTION_GAP;
  const bool manualAllowed = isManualAccessAllowed(state);
  snprintf(modeBuf, sizeof(modeBuf),
           (state.mode == Mode::IDLE) ? (ru ? "Режим: %s" : "Mode: %s")
                                      : (ru ? "Активен: %s" : "Active: %s"),
           getDisplayModeName(state.mode));
  if (state.mode == Mode::IDLE) {
    snprintf(modeBuf, sizeof(modeBuf), "%s",
             ru ? "ГОТОВ. ВЫБЕРИТЕ РЕЖИМ" : "READY. SELECT A MODE");
  } else {
    snprintf(modeBuf, sizeof(modeBuf), "%s / %s", getDisplayModeName(state.mode),
             getDisplayPhaseName(state));
  }

  const uint16_t modeTone =
      (state.mode == Mode::IDLE) ? colorAccent()
                                 : (state.paused ? COLOR_WARNING : COLOR_SUCCESS);
  drawCard(10, CTRL_STATUS_Y, TFT_WIDTH - 20, CTRL_STATUS_H, colorCard());
  tft.fillRect(12, CTRL_STATUS_Y + 2, 7, CTRL_STATUS_H - 4, modeTone);
  tft.setTextColor(colorFg());
  tft.setTextDatum(middle_left);
  tft.setTextSize(1);
  char modeLineBuf[96];
  copyFittedText(modeBuf, TFT_WIDTH - 70, modeLineBuf, sizeof(modeLineBuf));
  tft.drawString(modeLineBuf, 26, CTRL_STATUS_Y + CTRL_STATUS_H / 2);
  tft.setTextDatum(top_left);
  drawButton(pauseX, CTRL_ACTION_Y, CTRL_ACTION_BW, CTRL_ACTION_BH,
             state.paused ? msg(Msg::RESUME) : msg(Msg::PAUSE),
             (state.mode == Mode::IDLE) ? dimmedButtonColor() : COLOR_WARNING,
             TFT_WHITE);
  drawButton(stopX, CTRL_ACTION_Y, CTRL_ACTION_BW, CTRL_ACTION_BH, msg(Msg::STOP),
             (state.mode == Mode::IDLE) ? dimmedButtonColor() : COLOR_DANGER,
             TFT_WHITE);

  drawButton(CTRL_X1, CTRL_Y1, CTRL_BW, CTRL_BH, getDisplayModeName(Mode::RECTIFICATION),
             modeButtonColor(state, Mode::RECTIFICATION, colorAccent()),
             TFT_WHITE);
  drawButton(CTRL_X2, CTRL_Y1, CTRL_BW, CTRL_BH, getDisplayModeName(Mode::DISTILLATION),
             modeButtonColor(state, Mode::DISTILLATION, COLOR_INFO),
             TFT_WHITE);

  drawButton(CTRL_X1, CTRL_Y2, CTRL_BW, CTRL_BH, getDisplayModeName(Mode::MANUAL_RECT),
             modeButtonColor(state, Mode::MANUAL_RECT, tft.color565(128, 136, 144)),
             TFT_WHITE);
  drawButton(CTRL_X2, CTRL_Y2, CTRL_BW, CTRL_BH, getDisplayModeName(Mode::MASHING),
             modeButtonColor(state, Mode::MASHING, tft.color565(114, 170, 84)), TFT_WHITE);

  drawButton(CTRL_X1, CTRL_Y3, CTRL_BW, CTRL_BH, getDisplayModeName(Mode::HOLD),
             modeButtonColor(state, Mode::HOLD, tft.color565(210, 150, 56)), TFT_WHITE);

  drawButton(CTRL_X2, CTRL_Y3, CTRL_BW, CTRL_BH, getDisplayModeName(Mode::NBK),
             modeButtonColor(state, Mode::NBK, tft.color565(80, 144, 214)), TFT_WHITE);

  drawButton(CTRL_X1, CTRL_Y4, CTRL_BW, CTRL_BH, getDisplayModeName(Mode::FERMENTATION),
             modeButtonColor(state, Mode::FERMENTATION, tft.color565(72, 168, 152)),
             TFT_WHITE);
  drawButton(CTRL_X2, CTRL_Y4, CTRL_BW, CTRL_BH, ru ? "Узлы" : "Devices",
             manualAllowed ? COLOR_DARK_GREY : dimmedButtonColor(),
             TFT_WHITE);

  if (ui.modeSwitchConfirm) {
    drawModeSwitchOverlay(state, ru);
  }
}

static void renderSettings() {
  tft.fillScreen(colorBg());
  drawHeader(msg(Msg::SETTINGS), false);
  drawTabs(UI_SETTINGS);
  const bool ru = (g_settings.language == 0);

  // Геометрия сетки 2x2 + строка переключателей
  const int16_t COL1_X = 10;
  const int16_t COL2_X = 245;
  const int16_t CARD_W = 225;
  const int16_t ROW1_Y = 53;
  const int16_t ROW2_Y = 124;
  const int16_t CARD_H = 63;
  const int16_t ROW3_Y = 197;
  const int16_t BTN3_H = 32;
  const int16_t BTN3_W = 145;

  // --- КАРТОЧКА 1: Оборудование ---
  drawCard(COL1_X, ROW1_Y, CARD_W, CARD_H, colorCard());
  drawPanelHeader(COL1_X, ROW1_Y, CARD_W, msg(Msg::EQUIPMENT), colorAccent());
  {
    char b1[16], b2[20];
    snprintf(b1, sizeof(b1), "%u %s", g_settings.equipment.heaterPowerW,
             msg(Msg::UNIT_W));
    snprintf(b2, sizeof(b2), "%u %s", g_settings.equipment.columnHeightMm,
             msg(Msg::UNIT_MM));
    drawCompactKeyValueRow(COL1_X + 8, ROW1_Y + 34, CARD_W - 16,
                           ru ? "МОЩН" : "PWR", b1, COLOR_WARNING);
    drawCompactKeyValueRow(COL1_X + 8, ROW1_Y + 50, CARD_W - 16,
                           ru ? "ВЫС. К." : "COL H", b2, COLOR_INFO);
  }

  // --- КАРТОЧКА 2: Параметры ректификации ---
  drawCard(COL2_X, ROW1_Y, CARD_W, CARD_H, colorCard());
  drawPanelHeader(COL2_X, ROW1_Y, CARD_W, msg(Msg::RECT_PARAMS), COLOR_INFO);
  {
    char b1[24], b2[24];
    snprintf(b1, sizeof(b1), "%s",
             rectFeedstockName(g_settings.rectParams.feedstock, ru));
    snprintf(b2, sizeof(b2), "%.0f/%.0f/%.0f%%",
             g_settings.rectParams.headsPercent,
             g_settings.rectParams.bodyPercent,
             g_settings.rectParams.tailsPercent);
    drawCompactKeyValueRow(COL2_X + 8, ROW1_Y + 34, CARD_W - 16,
                           ru ? "СС" : "FEED", b1, colorAccent());
    drawCompactKeyValueRow(COL2_X + 8, ROW1_Y + 50, CARD_W - 16,
                           ru ? "Г/Т/Х" : "H/B/T", b2, COLOR_INFO);
  }

  // --- КАРТОЧКА 3: Параметры дистилляции ---
  drawCard(COL1_X, ROW2_Y, CARD_W, CARD_H, colorCard());
  drawPanelHeader(COL1_X, ROW2_Y, CARD_W, msg(Msg::DIST_PARAMS), COLOR_WARNING);
  {
    char b1[16], b2[16];
    snprintf(b1, sizeof(b1), "%.0f %s", distUi.speedMlH, msg(Msg::UNIT_ML_H));
    snprintf(b2, sizeof(b2), "%.0f ml", distUi.targetVolumeMl);
    drawCompactKeyValueRow(COL1_X + 8, ROW2_Y + 34, CARD_W - 16,
                           ru ? "СКОР" : "SPEED", b1, COLOR_PRIMARY);
    drawCompactKeyValueRow(COL1_X + 8, ROW2_Y + 50, CARD_W - 16,
                           ru ? "ЦЕЛЬ" : "TARGET", b2, COLOR_SUCCESS);
  }

  // --- КАРТОЧКА 4: Калибровка ---
  drawCard(COL2_X, ROW2_Y, CARD_W, CARD_H, colorCard());
  drawPanelHeader(COL2_X, ROW2_Y, CARD_W, msg(Msg::CALIBRATION), COLOR_SUCCESS);
  {
    char b1[20];
    snprintf(b1, sizeof(b1), "%.3f %s", g_settings.pumpCal.mlPerRevolution,
             msg(Msg::UNIT_ML_R));
    drawCompactKeyValueRow(COL2_X + 8, ROW2_Y + 34, CARD_W - 16,
                           ru ? "НАСОС" : "PUMP", b1, COLOR_INFO);
    drawCompactKeyValueRow(COL2_X + 8, ROW2_Y + 50, CARD_W - 16,
                           ru ? "ТАЧ" : "TOUCH",
                           g_settings.touchCal.valid
                               ? (ru ? "калибр." : "done")
                               : (ru ? "не кал." : "raw"),
                           g_settings.touchCal.valid ? COLOR_SUCCESS
                                                     : COLOR_WARNING);
  }

  // --- СТРОКА БЫСТРЫХ ПЕРЕКЛЮЧАТЕЛЕЙ ---
  {
    const bool dark = (g_settings.theme == 1);
    char themeLabel[24];
    snprintf(themeLabel, sizeof(themeLabel), "%s:%s", msg(Msg::THEME),
             dark ? (ru ? "Тмн" : "Drk") : (ru ? "Свт" : "Lgt"));
    drawButton(COL1_X, ROW3_Y, BTN3_W, BTN3_H, themeLabel,
               dark ? tft.color565(58, 64, 72) : tft.color565(160, 170, 178),
               TFT_WHITE);
  }
  {
    char soundLabel[20];
    snprintf(soundLabel, sizeof(soundLabel), "%s:%s", msg(Msg::SOUND),
             g_settings.soundEnabled ? (ru ? "ВКЛ" : "ON")
                                     : (ru ? "ВЫКЛ" : "OFF"));
    drawButton(165, ROW3_Y, BTN3_W, BTN3_H, soundLabel,
               g_settings.soundEnabled ? COLOR_SUCCESS : COLOR_DARK_GREY,
               TFT_WHITE);
  }
  {
    char langLabel[20];
    snprintf(langLabel, sizeof(langLabel), "%s:%s", msg(Msg::LANGUAGE),
             g_settings.language == 0 ? "RU" : "EN");
    drawButton(320, ROW3_Y, BTN3_W, BTN3_H, langLabel, COLOR_INFO, TFT_WHITE);
  }

  // --- FOOTER HINT ---
  char hint[96];
  snprintf(hint, sizeof(hint),
           ru ? "Тап карточки = раздел | кнопки = мгновенно"
              : "Tap card = open section | buttons = instant toggle");
  drawFooterHint(hint, colorAccent());
}

static void renderEquipment() {
  tft.fillScreen(colorBg());
  drawHeader(msg(Msg::EQUIPMENT), true);
  drawTabs(UI_SETTINGS);

  const int16_t x1 = 10;
  const int16_t x2 = 245;
  const int16_t y1 = 48;
  const int16_t y2 = 138;
  const int16_t tileW = 225;
  const int16_t tileH = 78;
  char buf[32];

  snprintf(buf, sizeof(buf), "%u", g_settings.equipment.heaterPowerW);
  drawValueTile(x1, y1, tileW, tileH, msg(Msg::HEATER_POWER), buf,
                msg(Msg::UNIT_W), COLOR_WARNING);

  snprintf(buf, sizeof(buf), "%u", g_settings.equipment.columnHeightMm);
  drawValueTile(x2, y1, tileW, tileH, msg(Msg::COLUMN_HEIGHT), buf,
                msg(Msg::UNIT_MM), COLOR_INFO);

  snprintf(buf, sizeof(buf), "%.1f", g_settings.equipment.cubeVolumeL);
  drawValueTile(x1, y2, tileW, tileH, msg(Msg::CUBE_VOLUME), buf,
                msg(Msg::UNIT_L), COLOR_SUCCESS);

  snprintf(buf, sizeof(buf), "%.2f", g_settings.equipment.packingCoeff);
  drawValueTile(x2, y2, tileW, tileH, msg(Msg::PACKING_COEFF), buf, "",
                colorAccent());
  drawFooterHint(msg(Msg::TAP_TO_EDIT), colorAccent());
}

static void renderRectParams() {
  tft.fillScreen(colorBg());
  const bool ru = (g_settings.language == 0);
  drawHeader(ru ? "РЕКТ. ПАРАМ." : "RECT PARAMS", true);
  drawTabs(UI_SETTINGS);

  const int16_t tileW = 225;
  const int16_t tileH = 46;
  const int16_t x1 = 10;
  const int16_t x2 = 245;
  const int16_t y1 = 82;
  const int16_t y2 = 134;
  const int16_t y3 = 186;
  char tileBuf[32];
  char pageBuf[80];

  snprintf(pageBuf, sizeof(pageBuf), "%s",
           rectParamsPage == 0 ? (ru ? "ТЕХ.ПАРАМ / ФРАКЦИИ" : "TECH / CUTS")
                               : (ru ? "ПРОФИЛЬ / СКОРОСТЬ" : "PROFILE / SPEED"));
  drawButton(10, 48, 460, 26, pageBuf,
             rectParamsPage == 0 ? COLOR_INFO : colorAccent(), TFT_WHITE);

  if (rectParamsPage == 0) {
    snprintf(tileBuf, sizeof(tileBuf), "%s",
             rectFeedstockName(g_settings.rectParams.feedstock, ru));
    drawValueTile(x1, y1, tileW, tileH, msg(Msg::FEEDSTOCK), tileBuf, "", COLOR_INFO);

    snprintf(tileBuf, sizeof(tileBuf), "%.1f", g_settings.rectParams.feedVolumeL);
    drawValueTile(x2, y1, tileW, tileH, msg(Msg::FEED_VOLUME), tileBuf,
                  msg(Msg::UNIT_L), COLOR_PRIMARY);

    snprintf(tileBuf, sizeof(tileBuf), "%.1f", g_settings.rectParams.feedAbvPercent);
    drawValueTile(x1, y2, tileW, tileH, msg(Msg::FEED_ABV), tileBuf, "%",
                  COLOR_WARNING);

    snprintf(tileBuf, sizeof(tileBuf), "%.1f", g_settings.rectParams.headsPercent);
    drawValueTile(x2, y2, tileW, tileH, msg(Msg::HEADS_PERCENT), tileBuf, "%",
                  COLOR_DANGER);

    snprintf(tileBuf, sizeof(tileBuf), "%.1f", g_settings.rectParams.bodyPercent);
    drawValueTile(x1, y3, tileW, tileH, msg(Msg::BODY_PERCENT), tileBuf, "%", COLOR_SUCCESS);

    snprintf(tileBuf, sizeof(tileBuf), "%.1f", g_settings.rectParams.tailsPercent);
    drawValueTile(x2, y3, tileW, tileH, msg(Msg::TAILS_PERCENT), tileBuf, "%",
                  COLOR_WARNING);

    drawFooterHint(ru ? "Тап СЫРЬЁ = следующий тип, фракции по умолчанию"
                      : "Tap feedstock to rotate type + default cuts",
                   COLOR_INFO);
    return;
  }

  snprintf(tileBuf, sizeof(tileBuf), "%.0f", g_settings.rectParams.headsSpeedMlHKw);
  drawValueTile(x1, y1, tileW, tileH, msg(Msg::HEADS_SPEED), tileBuf,
                msg(Msg::UNIT_ML_H_K), COLOR_DANGER);

  snprintf(tileBuf, sizeof(tileBuf), "%.0f", g_settings.rectParams.bodySpeedMlHKw);
  drawValueTile(x2, y1, tileW, tileH, msg(Msg::BODY_SPEED), tileBuf,
                msg(Msg::UNIT_ML_H_K), COLOR_SUCCESS);

  snprintf(tileBuf, sizeof(tileBuf), "%u", g_settings.rectParams.stabilizationMin);
  drawValueTile(x1, y2, tileW, tileH, msg(Msg::STABILIZATION), tileBuf,
                msg(Msg::UNIT_MIN), COLOR_INFO);

  snprintf(tileBuf, sizeof(tileBuf), "%u", g_settings.rectParams.purgeMin);
  drawValueTile(x2, y2, tileW, tileH, msg(Msg::PURGE_TIME), tileBuf,
                msg(Msg::UNIT_MIN), COLOR_WARNING);

  const float atmHpaComp =
      (g_state.pressure.ok && g_state.pressure.atmosphere > 850.0f &&
       g_state.pressure.atmosphere < 1100.0f)
          ? g_state.pressure.atmosphere
          : RECT_PRESSURE_STD_HPA;
  const float bodyToTailsComp =
      RECT_CUBE_BODY_TO_TAILS_BASE_C +
      (atmHpaComp - RECT_PRESSURE_STD_HPA) * RECT_TEMP_COMP_C_PER_HPA;
  const float finishComp =
      RECT_CUBE_FINISH_BASE_C +
      (atmHpaComp - RECT_PRESSURE_STD_HPA) * RECT_TEMP_COMP_C_PER_HPA;

  snprintf(tileBuf, sizeof(tileBuf), "%.1f", bodyToTailsComp);
  drawValueTile(x1, y3, tileW, tileH, msg(Msg::BODY_TO_TAILS), tileBuf, "C",
                colorMuted());

  snprintf(tileBuf, sizeof(tileBuf), "%.1f", finishComp);
  drawValueTile(x2, y3, tileW, tileH, msg(Msg::TAILS_FINISH), tileBuf, "C",
                colorMuted());

  snprintf(tileBuf, sizeof(tileBuf), "P=%.0f hPa | COMP", atmHpaComp);
  drawFooterHint(tileBuf, COLOR_INFO);
}

static void renderDistParams() {
  tft.fillScreen(colorBg());
  const bool ru = (g_settings.language == 0);
  drawHeader(ru ? "ДИСТ. ПАРАМ." : "DIST PARAMS", true);
  drawTabs(UI_SETTINGS);

  const int16_t x1 = 10;
  const int16_t x2 = 245;
  const int16_t y1 = 48;
  const int16_t y2 = 138;
  const int16_t tileW = 225;
  const int16_t tileH = 78;
  char tileBufDist[32];

  snprintf(tileBufDist, sizeof(tileBufDist), "%.0f", distUi.speedMlH);
  drawValueTile(x1, y1, tileW, tileH, msg(Msg::DIST_SPEED), tileBufDist,
                msg(Msg::UNIT_ML_H), COLOR_PRIMARY);

  snprintf(tileBufDist, sizeof(tileBufDist), "%.0f", distUi.headsVolumeMl);
  drawValueTile(x2, y1, tileW, tileH, msg(Msg::HEADS_VOLUME), tileBufDist, "ml",
                COLOR_DANGER);

  snprintf(tileBufDist, sizeof(tileBufDist), "%.0f", distUi.targetVolumeMl);
  drawValueTile(x1, y2, tileW, tileH, msg(Msg::TARGET_VOLUME), tileBufDist, "ml",
                COLOR_SUCCESS);

  snprintf(tileBufDist, sizeof(tileBufDist), "%.1f", distUi.endTempC);
  drawValueTile(x2, y2, tileW, tileH, msg(Msg::END_TEMP), tileBufDist, "C",
                COLOR_WARNING);
  drawFooterHint(msg(Msg::TAP_TO_EDIT), COLOR_WARNING);
  return;

}

static void renderCalibration() {
  tft.fillScreen(colorBg());
  const bool ru = (g_settings.language == 0);
  drawHeader(ru ? "КАЛИБР." : "CALIBRATION", true);
  drawTabs(UI_SETTINGS);

  const int16_t tileY = 52;
  const int16_t tileW = 225;
  const int16_t tileH = 86;
  char buf[32];
  snprintf(buf, sizeof(buf), "%.3f", g_settings.pumpCal.mlPerRevolution);
  drawValueTile(10, tileY, tileW, tileH, msg(Msg::PUMP_CALIBRATION), buf,
                msg(Msg::UNIT_ML_R), COLOR_INFO);

  drawButton(245, tileY, tileW, tileH, msg(Msg::TOUCH_CALIBRATION),
             COLOR_WARNING,
             TFT_WHITE);
  drawFooterHint(ru ? "Сначала ml/об, потом тач"
                    : "Set ml/rev first, then calibrate touch",
                 COLOR_WARNING);
}

static void renderManual(const SystemState &state) {
  tft.fillScreen(colorBg());
  const bool ru = (g_settings.language == 0);
  drawHeader(ru ? "РУЧНЫЕ УЗЛЫ" : "MANUAL I/O", true);
  drawTabs(UI_CONTROL);

  if (!isManualAccessAllowed(state)) {
    char message[160];
    char footer[160];
    snprintf(message, sizeof(message),
             ru ? "Автопроцесс активен. Ручной доступ закрыт."
                : "Manual control is locked while an automatic process is active.");
    snprintf(footer, sizeof(footer),
             ru ? "Текущий режим: %s\nДоступно только в IDLE и ручном экране"
                : "Current mode: %s\nAvailable only in IDLE and MANUAL",
             getDisplayModeName(state.mode));
    drawFullscreenOverlay(ru ? "РУЧНОЙ ДОСТУП" : "MANUAL ACCESS",
                          message, COLOR_WARNING, footer, 1);
    return;
  }

  char buf[32];
  const int16_t tileY = 48;
  const int16_t tileW = 225;
  const int16_t tileH = 86;
  const int16_t valveY = 146;
  const int16_t valveW = 145;
  const int16_t valveH = 76;

  snprintf(buf, sizeof(buf), "%u", Heater::getPower());
  drawValueTile(10, tileY, tileW, tileH, msg(Msg::HEATER_POWER), buf, "%",
                Heater::getPower() > 0 ? COLOR_WARNING : colorMuted());

  snprintf(buf, sizeof(buf), "%.0f", state.pump.speedMlPerHour);
  drawValueTile(245, tileY, tileW, tileH, msg(Msg::PUMP), buf,
                msg(Msg::UNIT_ML_H),
                state.pump.speedMlPerHour > 0.0f ? COLOR_SUCCESS : colorMuted());

  drawButton(10, valveY, valveW, valveH, msg(Msg::VALVE_WATER),
             Valves::getWater() ? COLOR_SUCCESS : COLOR_DARK_GREY, TFT_WHITE);
  drawButton(165, valveY, valveW, valveH, msg(Msg::VALVE_HEADS),
             Valves::getHeads() ? COLOR_SUCCESS : COLOR_DARK_GREY, TFT_WHITE);
  drawButton(320, valveY, valveW, valveH, msg(Msg::VALVE_UNO),
             Valves::getUno() ? COLOR_SUCCESS : COLOR_DARK_GREY, TFT_WHITE);

  drawFooterHint(ru ? "Тап по плитке = настройка, по клапану = переключение"
                    : "Tap tile to set, valve to toggle",
                 colorAccent());
}

static void renderValueEdit() {
  tft.fillScreen(colorBg());
  drawHeader(edit.label, true);
  const bool ru = (g_settings.language == 0);

  char buf[32];
  if (edit.decimals == 0)
    snprintf(buf, sizeof(buf), "%.0f", edit.value);
  else if (edit.decimals == 1)
    snprintf(buf, sizeof(buf), "%.1f", edit.value);
  else
    snprintf(buf, sizeof(buf), "%.3f", edit.value);
  drawValueTile(10, 48, TFT_WIDTH - 20, 78,
                ru ? "ЗНАЧЕНИЕ" : "CURRENT VALUE", buf, edit.unit,
                COLOR_PRIMARY);

  const int16_t bw = 108;
  const int16_t bh = 56;
  const int16_t y = 138;
  drawButton(10, y, bw, bh, "--", tft.color565(200, 50, 50), TFT_WHITE);
  drawButton(124, y, bw, bh, "-", tft.color565(220, 100, 100), TFT_WHITE);
  drawButton(238, y, bw, bh, "+", tft.color565(100, 200, 100), TFT_WHITE);
  drawButton(352, y, bw, bh, "++", tft.color565(50, 180, 50), TFT_WHITE);

  drawButton(10, 202, TFT_WIDTH - 20, 44, msg(Msg::SAVE_AND_CLOSE),
             COLOR_PRIMARY, TFT_WHITE);
  drawFooterHint(ru ? "Слева быстро вниз, справа быстро вверх"
                    : "Fast down on left, fast up on right",
                 colorAccent());
  tft.setTextDatum(top_left);
}

static void renderService(const SystemState &state, bool full) {
  const bool ru = (g_settings.language == 0);
  const int16_t tileW = 225;
  const int16_t tileH = 64;
  const int16_t x1 = 10;
  const int16_t x2 = 245;
  const int16_t y1 = 48;
  const int16_t y2 = 118;
  const int16_t diagY = 198;

  if (full) {
    tft.fillScreen(colorBg());
    drawHeader(msg(Msg::SERVICE), false);
    drawTabs(UI_SERVICE);
    drawValueTileShell(x1, y1, tileW, tileH, msg(Msg::VERSION));
    drawValueTileShell(x2, y1, tileW, tileH, msg(Msg::UPTIME));
    drawValueTileShell(x1, y2, tileW, tileH, msg(Msg::FREE_HEAP));
    drawValueTileShell(x2, y2, tileW, tileH, ru ? "КАДР TFT" : "TFT FRAME");
  }

  char buf[48];
  char uptimeBuf[16];
  char frameBuf[24];

  snprintf(buf, sizeof(buf), "%s", FW_VERSION);
  drawValueTileValue(x1, y1, tileW, tileH, buf, "", COLOR_PRIMARY);

  formatUptimeCompact(state.uptime, uptimeBuf, sizeof(uptimeBuf));
  drawValueTileValue(x2, y1, tileW, tileH, uptimeBuf, "", colorAccent());

  snprintf(buf, sizeof(buf), "%u", ESP.getFreeHeap() / 1024);
  drawValueTileValue(x1, y2, tileW, tileH, buf, "KB", COLOR_SUCCESS);

  snprintf(frameBuf, sizeof(frameBuf), "%u/%u", g_displayStats.lastFrameMs,
           g_displayStats.maxFrameMs);
  drawValueTileValue(x2, y2, tileW, tileH, frameBuf, "ms", COLOR_INFO);

  snprintf(buf, sizeof(buf), "S%lu R%lu H%lu G%u",
           (unsigned long)g_displayStats.slowFrames,
           (unsigned long)g_displayStats.watchdogRecoveries,
           (unsigned long)g_displayStats.hardWatchdogRecoveries,
           (unsigned int)g_displayStats.lastUpdateGapMs);
  drawValueRow(diagY, ru ? "Диагн. TFT" : "TFT diag", buf);
  uint16_t hintTone = COLOR_INFO;
  const char *hintText =
      ru ? "Тап по нижним строкам -> температуры"
         : "Tap lower rows to open temperature screen";
  if (g_displayStats.hardWatchdogRecoveries > 0 ||
      g_displayStats.hardWatchdogFailures > 0) {
    hintTone = COLOR_DANGER;
    hintText = ru ? "Hard recovery TFT. Проверьте питание и SPI"
                  : "Hard TFT recoveries detected, check power and SPI";
  } else if (g_displayStats.watchdogRecoveries > 0 ||
             g_displayStats.maxFrameMs >= DISPLAY_SLOW_FRAME_MS) {
    hintTone = COLOR_WARNING;
    hintText = ru ? "Slow/watchdog кадры TFT. Проверьте нагрузку"
                  : "Slow/watchdog TFT frames detected, check screen load";
  }
  drawFooterHint(hintText, hintTone);
}

static void renderAllTemps(const SystemState &state, bool full) {
  if (full) {
    tft.fillScreen(colorBg());
    drawHeader(g_settings.language == 0 ? "ТЕМПЕРАТУРЫ" : "TEMPERATURES", true);
    drawTabs(ui.rootScreen);
  }

  const int16_t xStart = 10;
  const int16_t yStart = 48;
  const int16_t tileW = (TFT_WIDTH - 30) / 2;
  const int16_t tileH = 40;
  const int16_t gap = 6;

  const char* labels[TEMP_COUNT] = {
    msg(Msg::CUBE_TEMP),
    msg(Msg::COLUMN_BOTTOM),
    msg(Msg::TOP_T),
    msg(Msg::REFLUX_T),
    msg(Msg::TSA_T),
    g_settings.language == 0 ? "ОХЛ ВХОД" : "WATER IN",
    g_settings.language == 0 ? "ОХЛ ВЫХОД" : "WATER OUT"
  };

  float values[TEMP_COUNT] = {
    state.temps.cube,
    state.temps.columnBottom,
    state.temps.columnTop,
    state.temps.reflux,
    state.temps.tsa,
    state.temps.waterIn,
    state.temps.waterOut
  };

  for (uint8_t i = 0; i < TEMP_COUNT; i++) {
    const int16_t x = xStart + (i % 2) * (tileW + gap);
    const int16_t y = yStart + (i / 2) * (tileH + gap);
    
    char valBuf[16];
    if (state.temps.valid[i]) {
      snprintf(valBuf, sizeof(valBuf), "%.2f", values[i]);
    } else {
      strncpy(valBuf, "---", sizeof(valBuf));
    }

    // Only clear value area if not full redraw
    if (!full) {
      tft.fillRect(x + 8, y + 20, tileW - 16, tileH - 22, colorCard());
    } else {
      drawValueTileShell(x, y, tileW, tileH, labels[i]);
    }

    tft.setTextColor(state.temps.valid[i] ? COLOR_PRIMARY : colorMuted());
    tft.setTextSize(1);
    tft.setFont(&fonts::efontJA_16);
    tft.setTextDatum(middle_center);
    tft.drawString(valBuf, x + (tileW / 2), y + 29);
    tft.setTextDatum(top_left);
  }
}

static void renderTouchCalibration() {
  const bool ru = (g_settings.language == 0);
  const uint16_t tone = (ui.calSkip > 0) ? COLOR_WARNING : COLOR_INFO;
  const int16_t panelX = 18;
  const int16_t panelY = 18;
  const int16_t panelW = TFT_WIDTH - 36;
  const int16_t panelH = TFT_HEIGHT - 36;
  const int16_t infoX = panelX + 18;
  const int16_t infoY = panelY + 44;
  const int16_t infoW = panelW - 36;
  const int16_t infoH = 54;
  const int16_t targetX = panelX + 18;
  const int16_t targetY = infoY + infoH + 12;
  const int16_t targetW = panelW - 36;
  const int16_t targetH = panelH - (targetY - panelY) - 18;

  tft.fillScreen(colorNavBg());
  tft.fillRect(0, 0, TFT_WIDTH, 6, tone);
  drawCard(panelX, panelY, panelW, panelH, colorCard());
  drawPanelHeader(panelX, panelY, panelW, msg(Msg::TOUCH_CAL_TITLE), tone);
  drawCard(infoX, infoY, infoW, infoH, colorBg());
  tft.fillRect(infoX + 1, infoY + 1, 8, infoH - 2, tone);

  for (uint8_t i = 0; i < 4; i++) {
    const int16_t sx = panelX + panelW - 138 + i * 30;
    const int16_t sy = panelY + 8;
    const bool done = (i < ui.calStep);
    const bool current = (!ui.calSkip && i == ui.calStep);
    drawCard(sx, sy, 24, 16, done ? colorButtonBody() : colorBg());
    tft.fillRect(sx + 2, sy + 2, 20, 4,
                 done ? COLOR_SUCCESS
                      : (current ? tone : colorSoftFill()));
    tft.setTextColor(done ? COLOR_SUCCESS : (current ? colorFg() : colorMuted()));
    tft.setTextSize(1);
    tft.setTextDatum(middle_center);
    char stepBuf[4];
    snprintf(stepBuf, sizeof(stepBuf), "%u", i + 1);
    tft.drawString(stepBuf, sx + 12, sy + 10);
    tft.setTextDatum(top_left);
  }

  tft.setTextColor(colorFg());
  tft.setTextSize(1);
  tft.setTextDatum(top_center);
  if (ui.calSkip > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), msg(Msg::TOUCH_CAL_TAP_N), ui.calSkip);
    tft.drawString(buf, TFT_WIDTH / 2, infoY + 9);
    tft.setTextColor(colorMuted());
    tft.drawString(ru ? "Короткие нажатия перед замером" : "Short taps before sampling",
                   TFT_WIDTH / 2, infoY + 28);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "%s  %d/4", msg(Msg::TOUCH_CAL_TOUCH_TARGET),
             ui.calStep + 1);
    tft.drawString(buf, TFT_WIDTH / 2, infoY + 9);
    tft.setTextColor(colorMuted());
    tft.drawString(ru ? "Отпускайте палец после каждого касания"
                      : "Release finger after each target",
                   TFT_WIDTH / 2, infoY + 28);
  }
  tft.setTextDatum(top_left);

  drawCard(targetX, targetY, targetW, targetH, colorBg());
  tft.fillRect(targetX + 1, targetY + 1, 8, targetH - 2, tone);

  if (ui.calSkip > 0) {
    char countBuf[8];
    snprintf(countBuf, sizeof(countBuf), "%u", ui.calSkip);
    tft.setTextColor(tone);
    tft.setTextSize(5);
    tft.setTextDatum(middle_center);
    tft.drawString(countBuf, TFT_WIDTH / 2, targetY + (targetH / 2) - 6);
    tft.setTextSize(1);
    tft.setTextColor(colorMuted());
    tft.drawString(ru ? "Подготовка тач-контроллера" : "Preparing touch controller",
                   TFT_WIDTH / 2, targetY + targetH - 34);
    tft.setTextDatum(top_left);
  } else {
    const int16_t points[4][2] = {{targetX + 34, targetY + 28},
                                  {targetX + targetW - 34, targetY + 28},
                                  {targetX + targetW - 34, targetY + targetH - 28},
                                  {targetX + 34, targetY + targetH - 28}};
    const uint8_t stepIdx = (ui.calStep < 4) ? ui.calStep : 3;
    int16_t px = points[stepIdx][0];
    int16_t py = points[stepIdx][1];
    tft.drawRect(px - 18, py - 18, 36, 36, tone);
    tft.drawRect(px - 10, py - 10, 20, 20, tone);
    tft.fillRect(px - 3, py - 3, 6, 6, tone);
    tft.drawFastHLine(px - 26, py, 52, tone);
    tft.drawFastVLine(px, py - 26, 52, tone);
    tft.setTextColor(colorMuted());
    tft.setTextSize(1);
    tft.setTextDatum(middle_center);
    tft.drawString(ru ? "Коснитесь прямоугольной мишени"
                      : "Touch the rectangular target",
                   TFT_WIDTH / 2, targetY + (targetH / 2) - 8);
    tft.drawString(ru ? "Замер берется по среднему удержанию"
                      : "Sampling uses averaged hold data",
                   TFT_WIDTH / 2, targetY + (targetH / 2) + 12);
    tft.setTextDatum(top_left);
  }
}

static void applyTouchCalibration() {
  int16_t xLeft = (ui.calRawX[0] + ui.calRawX[3]) / 2;
  int16_t xRight = (ui.calRawX[1] + ui.calRawX[2]) / 2;
  int16_t yTop = (ui.calRawY[0] + ui.calRawY[1]) / 2;
  int16_t yBottom = (ui.calRawY[2] + ui.calRawY[3]) / 2;

  g_settings.touchCal.xMin = xLeft;
  g_settings.touchCal.xMax = xRight;
  g_settings.touchCal.yMin = yTop;
  g_settings.touchCal.yMax = yBottom;
  g_settings.touchCal.valid = true;

  NVSManager::saveSettings(g_settings);
}

static bool initDisplayHardware(bool showBootSplash) {
  tft_ok = tft.init();
  if (!tft_ok) {
    touch_ok = false;
    return false;
  }

  tft.setRotation(1); // Landscape (480x320)
  tft.setSwapBytes(true);
  tft.setTextScroll(false);
  tft.setTextSize(1);
  tft.setFont(&fonts::efontJA_16);

  // Re-init dedicated touch SPI to recover after bus faults.
  pinMode(TOUCH_IRQ, INPUT_PULLUP);
  touchSpi.end();
  touchSpi.begin(TOUCH_CLK, TOUCH_DO, TOUCH_DIN, TOUCH_CS);
  touch_ok = touch.begin(touchSpi);
  if (touch_ok) {
    touch.setRotation(1);
  }

  if (showBootSplash) {
    char bootBuf[96];
    snprintf(bootBuf, sizeof(bootBuf), "Smart-Column S3\nILI9488 / XPT2046");
    drawFullscreenOverlay("SYSTEM START", bootBuf, COLOR_SUCCESS,
                          touch_ok ? FW_VERSION : "DISPLAY ONLY", 2);
    drawWrappedTextBlock(70, 214, TFT_WIDTH - 140,
                         touch_ok ? "TFT + TOUCH READY"
                                  : "TFT READY / TOUCH OFFLINE",
                         touch_ok ? COLOR_SUCCESS : COLOR_WARNING, 1, 2, 4);
    delay(1200);
  }

  tft.fillScreen(colorNavBg());
  return true;
}

static bool attemptHardRecovery(uint32_t nowMs) {
  if (nowMs - g_displayStats.lastHardRecoveryAtMs <
      DISPLAY_HARD_RECOVERY_COOLDOWN_MS) {
    return false;
  }

  g_displayStats.lastHardRecoveryAtMs = nowMs;
  LOG_W("Display: hard watchdog recovery requested");

  if (!initDisplayHardware(false)) {
    g_displayStats.hardWatchdogFailures++;
    LOG_E("Display: hard watchdog recovery failed");
    return false;
  }

  g_displayStats.hardWatchdogRecoveries++;
  g_displayStats.consecutiveSlowFrames = 0;
  g_displayStats.consecutiveHardFrames = 0;
  g_displayStats.softRecoveriesInWindow = 0;
  g_displayStats.softRecoveryWindowStartedMs = 0;

  ui.lastRenderedScreen = static_cast<UiScreen>(255);
  ui.needsRedraw = true;
  LOG_W("Display: hard watchdog recovery done");
  return true;
}

#endif // TFT_ENABLED

namespace Display {

void init() {
  LOG_I("Display: Initializing...");

#if TFT_ENABLED
  // Инициализация TFT
  LOG_I("Display: Init TFT (LovyanGFX)...");

  if (initDisplayHardware(true)) {
    LOG_I("Display: TFT + Touch initialized");

    // Профиль затирки по умолчанию
    memset(&mashProfileDefault, 0, sizeof(mashProfileDefault));
    strncpy(mashProfileDefault.name, "Default mash",
            sizeof(mashProfileDefault.name) - 1);
    mashProfileDefault.stepCount = 5;

    mashProfileDefault.steps[0].temperature = 38.0f;
    mashProfileDefault.steps[0].duration = 20;
    strncpy(mashProfileDefault.steps[0].name, "Acid rest",
            sizeof(mashProfileDefault.steps[0].name) - 1);

    mashProfileDefault.steps[1].temperature = 52.0f;
    mashProfileDefault.steps[1].duration = 20;
    strncpy(mashProfileDefault.steps[1].name, "Protein rest",
            sizeof(mashProfileDefault.steps[1].name) - 1);

    mashProfileDefault.steps[2].temperature = 63.0f;
    mashProfileDefault.steps[2].duration = 40;
    strncpy(mashProfileDefault.steps[2].name, "Maltose rest",
            sizeof(mashProfileDefault.steps[2].name) - 1);

    mashProfileDefault.steps[3].temperature = 72.0f;
    mashProfileDefault.steps[3].duration = 20;
    strncpy(mashProfileDefault.steps[3].name, "Saccharification",
            sizeof(mashProfileDefault.steps[3].name) - 1);

    mashProfileDefault.steps[4].temperature = 78.0f;
    mashProfileDefault.steps[4].duration = 10;
    strncpy(mashProfileDefault.steps[4].name, "Mash out",
            sizeof(mashProfileDefault.steps[4].name) - 1);

    holdStepsDefault[0].temperature = 65.0f;
    holdStepsDefault[0].duration = 60;
    holdStepsCount = 1;

    // Загружаем уставки дистилляции из NVS-профиля настроек.
    distUi.speedMlH = g_settings.distillationUi.speedMlH;
    distUi.headsVolumeMl = g_settings.distillationUi.headsVolumeMl;
    distUi.targetVolumeMl = g_settings.distillationUi.targetVolumeMl;
    distUi.endTempC = g_settings.distillationUi.endTempC;
    distUi.powerPercent = g_settings.distillationUi.powerPercent;
    distUi.tailsVolumeMl = g_settings.distillationUi.tailsVolumeMl;
    applyDistUiRuntime();

    ui.currentScreen = UI_DASHBOARD;
    ui.rootScreen = UI_DASHBOARD;
    ui.stackDepth = 0;
    ui.lastRenderedScreen = static_cast<UiScreen>(255);
    ui.needsRedraw = true;
    const bool forceCal = touch_ok ? detectCalibrationRequest() : false;
    ui.calibrating = !g_settings.touchCal.valid || forceCal;
    ui.calStep = 0;
    ui.calSkip = ui.calibrating ? 2 : 0;
  } else {
    LOG_E("Display: TFT init failed");
  }
#endif

  LOG_I("Display: Init complete");
}

void update(const SystemState &state) {
#if TFT_ENABLED
  const uint32_t now = millis();

  if (g_displayStats.lastUpdateCallAtMs > 0) {
    const uint32_t updateGapMs = now - g_displayStats.lastUpdateCallAtMs;
    const uint16_t clampedGapMs =
        static_cast<uint16_t>(updateGapMs > 0xFFFF ? 0xFFFF : updateGapMs);
    g_displayStats.lastUpdateGapMs = clampedGapMs;
    if (clampedGapMs > g_displayStats.maxUpdateGapMs) {
      g_displayStats.maxUpdateGapMs = clampedGapMs;
    }
    if (updateGapMs > DISPLAY_UPDATE_GAP_WARN_MS) {
      g_displayStats.updateGapOverruns++;
    }
  }
  g_displayStats.lastUpdateCallAtMs = now;

  if (!tft_ok) {
    attemptHardRecovery(now);
    return;
  }

  if (ui.calibrating) {
    TouchEvent ev;
    if (touch_ok) {
      const bool wasPressed = ui.touchPressed;
      ev = readTouchEvent();

      // Accumulate raw samples continuously while the finger is held.
      // This gives a stable averaged position for each calibration point.
      // We do NOT use readTouchRawFiltered after ev.tapped because by
      // then the finger is already released and the controller has no data.
      if (ev.pressed) {
        int16_t rx = 0, ry = 0;
        if (touchReadRaw(&rx, &ry)) {
          if (!wasPressed) {
            // First frame of this press: start fresh accumulator.
            ui.calSumRawX = rx;
            ui.calSumRawY = ry;
            ui.calSampleCount = 1;
            ui.calIsCollecting = true;
          } else if (ui.calIsCollecting && ui.calSampleCount < 500) {
            ui.calSumRawX += rx;
            ui.calSumRawY += ry;
            ui.calSampleCount++;
          }
        }
      }

      if (ev.tapped) {
        if (ui.calSkip > 0) {
          ui.calSkip--;
          ui.calIsCollecting = false;
          ui.calSampleCount = 0;
          ui.needsRedraw = true;
        } else if (ui.calIsCollecting && ui.calSampleCount > 0) {
          if (ui.calStep < 4) {
            ui.calRawX[ui.calStep] =
                (int16_t)(ui.calSumRawX / ui.calSampleCount);
            ui.calRawY[ui.calStep] =
                (int16_t)(ui.calSumRawY / ui.calSampleCount);
            ui.calStep++;
            ui.calIsCollecting = false;
            ui.calSampleCount = 0;
            ui.needsRedraw = true;
          }
          if (ui.calStep >= 4) {
            applyTouchCalibration();
            ui.calibrating = false;
            ui.lastRenderedScreen = static_cast<UiScreen>(255);
            ui.needsRedraw = true;
            // Calibration is finished; render normal UI on next update cycle.
            return;
          }
        }
      }
    }

    if (ui.needsRedraw) {
      tft.startWrite();
      renderTouchCalibration();
      tft.endWrite();
      ui.needsRedraw = false;
    }
    return;
  }

  TouchEvent ev = readTouchEvent();

  // Auto-select monitor root window: idle overview vs active mode window.
  if (ui.stackDepth == 0 && isMonitorRootScreen(ui.rootScreen)) {
    const UiScreen desiredMonitor =
        isModeRunning(state) ? UI_MODE_MONITOR : UI_DASHBOARD;
    if (ui.rootScreen != desiredMonitor || ui.currentScreen != desiredMonitor) {
      switchRoot(desiredMonitor);
    }
  }

  if (!ui.needsRedraw && g_displayStats.lastFrameAtMs > 0 &&
      (now - g_displayStats.lastFrameAtMs) > getForceRefreshIntervalMs()) {
    ui.needsRedraw = true;
  }

  if (!ui.needsRedraw) {
    bool changed = false;
    if (ui.currentScreen == UI_DASHBOARD ||
        ui.currentScreen == UI_MODE_MONITOR || ui.currentScreen == UI_CONTROL ||
        ui.currentScreen == UI_SERVICE || ui.currentScreen == UI_MANUAL) {
      if (uiLive.mode != state.mode || uiLive.phase != state.rectPhase ||
          uiLive.paused != state.paused) {
        changed = true;
      }
      if (ui.currentScreen != UI_CONTROL) {
        if (fabsf(uiLive.tCube - state.temps.cube) > 0.1f ||
            fabsf(uiLive.tTop - state.temps.columnTop) > 0.1f ||
            fabsf(uiLive.tReflux - state.temps.reflux) > 0.1f ||
            fabsf(uiLive.tTsa - state.temps.tsa) > 0.1f ||
            fabsf(uiLive.tWaterIn - state.temps.waterIn) > 0.1f ||
            fabsf(uiLive.tWaterOut - state.temps.waterOut) > 0.1f) {
          changed = true;
        }
        if (fabsf(uiLive.power - state.power.power) > 1.0f ||
            fabsf(uiLive.pumpSpeed - state.pump.speedMlPerHour) > 1.0f ||
            fabsf(uiLive.voltage - state.power.voltage) > 1.0f ||
            fabsf(uiLive.pressure - state.pressure.cube) > 1.0f) {
          changed = true;
        }
        if (ui.currentScreen == UI_SERVICE && state.uptime != uiLive.uptime &&
            (now - uiLive.lastUpdateMs) > 1000) {
          changed = true;
        }
      }
      if ((ui.currentScreen == UI_DASHBOARD ||
           ui.currentScreen == UI_MODE_MONITOR) &&
          (now - uiLive.lastUpdateMs) > 1200) {
        // Keep phase timer/progress and service values moving even when
        // temperatures are stable.
        changed = true;
      }
    }
    if (changed && (now - uiLive.lastUpdateMs) > 300) {
      ui.needsRedraw = true;
    }
  }

  if (ev.tapped) {
    bool handled = false;
    // Пробуем прямые координаты
    handled = handleNavigationTap(ev.x, ev.y, state);
    if (!handled) {
      handled = handleScreenTap(ev.x, ev.y, state);
    }

    if (handled) {
      ui.needsRedraw = true;
    }
  }

  if (ui.needsRedraw) {
    const uint32_t frameStartMs = millis();
    uiLive.mode = state.mode;
    uiLive.phase = state.rectPhase;
    uiLive.paused = state.paused;
    uiLive.tCube = state.temps.cube;
    uiLive.tTop = state.temps.columnTop;
    uiLive.tReflux = state.temps.reflux;
    uiLive.tTsa = state.temps.tsa;
    uiLive.tWaterIn = state.temps.waterIn;
    uiLive.tWaterOut = state.temps.waterOut;
    uiLive.power = state.power.power;
    uiLive.pumpSpeed = state.pump.speedMlPerHour;
    uiLive.voltage = state.power.voltage;
    uiLive.pressure = state.pressure.cube;
    uiLive.uptime = state.uptime;
    uiLive.lastUpdateMs = now;
    const bool full = (ui.currentScreen != ui.lastRenderedScreen);
    tft.startWrite();
    switch (ui.currentScreen) {
    case UI_DASHBOARD:
      renderDashboard(state, full);
      break;
    case UI_MODE_MONITOR:
      renderModeMonitor(state, full);
      break;
    case UI_CONTROL:
      renderControl(state, full);
      break;
    case UI_SETTINGS:
      if (full)
        renderSettings();
      break;
    case UI_SERVICE:
      renderService(state, full);
      break;
    case UI_EQUIPMENT:
      renderEquipment();
      break;
    case UI_RECT_PARAMS:
      renderRectParams();
      break;
    case UI_DIST_PARAMS:
      renderDistParams();
      break;
    case UI_CALIBRATION:
      renderCalibration();
      break;
    case UI_VALUE_EDIT:
      renderValueEdit();
      break;
    case UI_MANUAL:
      renderManual(state);
      break;
    case UI_ALL_TEMPS:
      renderAllTemps(state, full);
      break;
    default:
      renderDashboard(state, full);
      break;
    }
    tft.endWrite();

    const uint32_t frameTime = millis() - frameStartMs;
    bool scheduleRecoveryRedraw = false;
    bool requestHardRecovery = false;
    g_displayStats.framesRendered++;
    g_displayStats.lastFrameMs =
        static_cast<uint16_t>(frameTime > 0xFFFF ? 0xFFFF : frameTime);
    g_displayStats.lastFrameAtMs = millis();
    if (g_displayStats.lastFrameMs > g_displayStats.maxFrameMs) {
      g_displayStats.maxFrameMs = g_displayStats.lastFrameMs;
    }

    if (frameTime >= DISPLAY_SLOW_FRAME_MS) {
      g_displayStats.slowFrames++;
      if (!full && frameTime >= DISPLAY_HARD_FRAME_MS) {
        if (g_displayStats.consecutiveSlowFrames < 255)
          g_displayStats.consecutiveSlowFrames++;
        if (g_displayStats.consecutiveHardFrames < 255)
          g_displayStats.consecutiveHardFrames++;
      } else {
        g_displayStats.consecutiveSlowFrames = 0;
        g_displayStats.consecutiveHardFrames = 0;
      }
    } else {
      g_displayStats.consecutiveSlowFrames = 0;
      g_displayStats.consecutiveHardFrames = 0;
    }

    if (g_displayStats.consecutiveSlowFrames >= DISPLAY_SOFT_WD_THRESHOLD) {
      // Soft watchdog: force a full redraw cycle instead of running with a
      // stale frame.
      g_displayStats.watchdogRecoveries++;
      g_displayStats.consecutiveSlowFrames = 0;
      ui.lastRenderedScreen = static_cast<UiScreen>(255);
      scheduleRecoveryRedraw = true;

      if (g_displayStats.softRecoveryWindowStartedMs == 0 ||
          (now - g_displayStats.softRecoveryWindowStartedMs) >
              DISPLAY_SOFT_WD_WINDOW_MS) {
        g_displayStats.softRecoveryWindowStartedMs = now;
        g_displayStats.softRecoveriesInWindow = 1;
      } else if (g_displayStats.softRecoveriesInWindow < 255) {
        g_displayStats.softRecoveriesInWindow++;
      }
    }

    if (g_displayStats.softRecoveryWindowStartedMs > 0 &&
        (now - g_displayStats.softRecoveryWindowStartedMs) >
            DISPLAY_SOFT_WD_WINDOW_MS) {
      g_displayStats.softRecoveryWindowStartedMs = now;
      g_displayStats.softRecoveriesInWindow = 0;
    }

    if (g_displayStats.consecutiveHardFrames >=
            DISPLAY_HARD_FRAME_BURST_THRESHOLD ||
        g_displayStats.softRecoveriesInWindow >=
            DISPLAY_SOFT_WD_BURST_FOR_HARD) {
      requestHardRecovery = true;
    }

    if (requestHardRecovery && attemptHardRecovery(now)) {
      return;
    }

    ui.needsRedraw = scheduleRecoveryRedraw;
    if (!scheduleRecoveryRedraw) {
      ui.lastRenderedScreen = ui.currentScreen;
    }
  }
#endif
}

void showMessage(const char *title, const char *message, uint8_t type) {
#if TFT_ENABLED
  if (tft_ok) {
    uint16_t color = colorAccent();
    if (type == 1) {
      color = COLOR_WARNING;
    } else if (type == 2) {
      color = COLOR_DANGER;
    }
    tft.startWrite();
    drawFullscreenOverlay(title, message, color, FW_VERSION,
                          (message != nullptr && strlen(message) < 24) ? 2 : 1);
    tft.endWrite();
  }
#endif
}

bool needsTouchCalibration() {
#if TFT_ENABLED
  return !g_settings.touchCal.valid;
#else
  return false;
#endif
}

void startTouchCalibration() {
#if TFT_ENABLED
  if (!tft_ok || !touch_ok)
    return;
  ui.calibrating = true;
  ui.calStep = 0;
  ui.calSkip = 2;
  memset(ui.calRawX, 0, sizeof(ui.calRawX));
  memset(ui.calRawY, 0, sizeof(ui.calRawY));
  ui.calIsCollecting = false;
  ui.calSumRawX = 0;
  ui.calSumRawY = 0;
  ui.calSampleCount = 0;
  ui.touchPressed = false;
  ui.touchDownX = 0;
  ui.touchDownY = 0;
  ui.touchLastX = 0;
  ui.touchLastY = 0;
  ui.touchDownMs = 0;
  ui.lastTapMs = 0;
  ui.ignoreTapUntilMs = millis() + 250;
  ui.needsRedraw = true;
#endif
}

bool isTouchCalibrating() {
#if TFT_ENABLED
  return ui.calibrating;
#else
  return false;
#endif
}

RuntimeStats getRuntimeStats() {
  RuntimeStats stats;
#if TFT_ENABLED
  stats.framesRendered = g_displayStats.framesRendered;
  stats.slowFrames = g_displayStats.slowFrames;
  stats.watchdogRecoveries = g_displayStats.watchdogRecoveries;
  stats.hardWatchdogRecoveries = g_displayStats.hardWatchdogRecoveries;
  stats.hardWatchdogFailures = g_displayStats.hardWatchdogFailures;
  stats.lastFrameMs = g_displayStats.lastFrameMs;
  stats.maxFrameMs = g_displayStats.maxFrameMs;
  stats.lastFrameAtMs = g_displayStats.lastFrameAtMs;
  stats.lastUpdateGapMs = g_displayStats.lastUpdateGapMs;
  stats.maxUpdateGapMs = g_displayStats.maxUpdateGapMs;
  stats.updateGapOverruns = g_displayStats.updateGapOverruns;
#endif
  return stats;
}

void showError(const char *error) {
#if TFT_ENABLED
  if (tft_ok) {
    const bool ru = (g_settings.language == 0);
    tft.startWrite();
    drawFullscreenOverlay(ru ? "ОШИБКА" : "ERROR", error, COLOR_DANGER,
                          ru ? "Проверьте питание, датчики и логи"
                             : "Check power, sensors and logs",
                          1);
    tft.endWrite();
  }
#endif
}

} // namespace Display
