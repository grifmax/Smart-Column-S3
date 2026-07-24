/**
 * Smart-Column S3 - Драйвер дисплея
 *
 * TFT 3.5" ILI9488 (основной)
 * Использует LovyanGFX для TFT
 */

#include "display.h"
#include "control/equipment_testing_logic.h"
#include "control/pump_calibration.h"
#include "control/sensor_calibration_logic.h"
#include "control/service_test_session.h"
#include "control/v2/status_adapter.h"
#include "control/fsm.h"
#include "control/power_command_gateway.h"
#include "drivers/heater.h"
#include "drivers/hmi_widgets.h"
#include "drivers/pump.h"
#include "drivers/sensors.h"
#include "drivers/stirrer.h"
#include "drivers/valves.h"
#include "interface/editable_field_descriptors.h"
#include "interface/localization.h"
#include "interface/tft_hmi_policy_logic.h"
#include "interface/tft_operator_overlay_logic.h"
#include "interface/tft_service_test_logic.h"
#include "interface/webserver_shared.h"
#include "profiles.h"
#include "storage/logger.h"
#include "storage/nvs_manager.h"
#include <LovyanGFX.hpp>
#include <SPI.h>
#include <WiFi.h>
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
// New HMI chrome: the network/safety line is visible on every screen.
// Subscreens reuse the remaining header space for the title and Back button.
static const int16_t UI_GLOBAL_STATUS_H = 20;
static const int16_t UI_HEADER_H = 42;
static const int16_t UI_FOOTER_H = 65;  // Немного увеличил для шрифта
static const int16_t UI_CONTENT_Y = 10; // Начинаем почти сверху
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
  UI_VALUE_EDIT,
  UI_ALL_TEMPS,
  UI_MODE_PICKER,
  UI_MODE_SETUP,
  UI_PREFLIGHT,
  UI_PROFILE_PICKER,
  UI_SAFETY_VIEW,
  UI_PROCESS_EVENTS,
  UI_PROCESS_PARAMS,
  UI_MODE_CONFIG,
  UI_MODE_PARAMETERS,
  UI_SERVICE_TEST,
  UI_PUMP_CALIBRATION,
  UI_SENSOR_CALIBRATION
};

enum class UiRootTab : uint8_t {
  MONITOR = 0,
  CONTROL,
  SETTINGS,
  SERVICE,
  NONE
};

using UiEditPolicy = TftHmiPolicyLogic::EditPolicy;

// Fields that a page is allowed to refresh incrementally. The global
// mode/safety/network line is handled separately on every page; this mask
// covers only the page workspace below it.
enum UiLiveFieldMask : uint8_t {
  UI_LIVE_NONE = 0,
  UI_LIVE_PROCESS = 1U << 0,
  UI_LIVE_TEMPERATURES = 1U << 1,
  UI_LIVE_POWER = 1U << 2,
  UI_LIVE_UPTIME = 1U << 3,
  UI_LIVE_PROGRESS = 1U << 4,
};

static constexpr uint8_t UI_LIVE_MONITOR_FIELDS =
    UI_LIVE_PROCESS | UI_LIVE_TEMPERATURES | UI_LIVE_POWER |
    UI_LIVE_PROGRESS;

struct UiScreenPolicy {
  UiRootTab rootTab;
  UiEditPolicy editPolicy;
  bool rootLike;
  bool autoMonitorRoot;
  uint8_t partialRefreshFields;
};

static constexpr UiScreenPolicy kUiScreenPolicies[] = {
    {UiRootTab::MONITOR, UiEditPolicy::ViewOnly, true, true,
     UI_LIVE_MONITOR_FIELDS}, // UI_DASHBOARD
    {UiRootTab::CONTROL, UiEditPolicy::Always, true, false,
     UI_LIVE_PROCESS}, // UI_CONTROL
    {UiRootTab::SETTINGS, UiEditPolicy::Always, true, false,
     UI_LIVE_NONE}, // UI_SETTINGS
    {UiRootTab::SERVICE, UiEditPolicy::ViewOnly, true, false,
     UI_LIVE_TEMPERATURES | UI_LIVE_POWER | UI_LIVE_UPTIME}, // UI_SERVICE
    {UiRootTab::MONITOR, UiEditPolicy::ModeMonitorRuntime, true, true,
     UI_LIVE_MONITOR_FIELDS}, // UI_MODE_MONITOR
    {UiRootTab::SETTINGS, UiEditPolicy::IdleOnly, false, false,
     UI_LIVE_NONE}, // UI_EQUIPMENT
    {UiRootTab::SETTINGS, UiEditPolicy::IdleOnly, false, false,
     UI_LIVE_NONE}, // UI_RECT_PARAMS
    {UiRootTab::SETTINGS, UiEditPolicy::IdleOnly, false, false,
     UI_LIVE_NONE}, // UI_DIST_PARAMS
    {UiRootTab::SETTINGS, UiEditPolicy::IdleOnly, false, false,
     UI_LIVE_NONE}, // UI_CALIBRATION
    {UiRootTab::CONTROL, UiEditPolicy::ManualOnly, false, false,
     UI_LIVE_TEMPERATURES | UI_LIVE_POWER}, // UI_MANUAL
    {UiRootTab::NONE, UiEditPolicy::Always, false, false,
     UI_LIVE_NONE}, // UI_VALUE_EDIT
    {UiRootTab::MONITOR, UiEditPolicy::ViewOnly, false, false,
     UI_LIVE_TEMPERATURES | UI_LIVE_PROGRESS}, // UI_ALL_TEMPS
    {UiRootTab::CONTROL, UiEditPolicy::ViewOnly, true, false,
     UI_LIVE_NONE}, // UI_MODE_PICKER
    {UiRootTab::CONTROL, UiEditPolicy::IdleOnly, false, false,
     UI_LIVE_NONE}, // UI_MODE_SETUP
    {UiRootTab::CONTROL, UiEditPolicy::IdleOnly, false, false,
     UI_LIVE_NONE}, // UI_PREFLIGHT
    {UiRootTab::CONTROL, UiEditPolicy::IdleOnly, false, false,
     UI_LIVE_NONE}, // UI_PROFILE_PICKER
    {UiRootTab::CONTROL, UiEditPolicy::ViewOnly, false, false,
     UI_LIVE_TEMPERATURES | UI_LIVE_POWER}, // UI_SAFETY_VIEW
    {UiRootTab::CONTROL, UiEditPolicy::ViewOnly, false, false,
     UI_LIVE_NONE}, // UI_PROCESS_EVENTS
    {UiRootTab::CONTROL, UiEditPolicy::ModeMonitorRuntime, false, false,
     UI_LIVE_PROCESS | UI_LIVE_TEMPERATURES | UI_LIVE_POWER}, // UI_PROCESS_PARAMS
    {UiRootTab::CONTROL, UiEditPolicy::IdleOnly, false, false,
     UI_LIVE_NONE}, // UI_MODE_CONFIG
    {UiRootTab::CONTROL, UiEditPolicy::IdleOnly, false, false,
     UI_LIVE_NONE}, // UI_MODE_PARAMETERS
    {UiRootTab::SERVICE, UiEditPolicy::ViewOnly, false, false,
     UI_LIVE_TEMPERATURES | UI_LIVE_POWER}, // UI_SERVICE_TEST
    {UiRootTab::SETTINGS, UiEditPolicy::IdleOnly, false, false,
     UI_LIVE_POWER}, // UI_PUMP_CALIBRATION
    {UiRootTab::SETTINGS, UiEditPolicy::IdleOnly, false, false,
     UI_LIVE_TEMPERATURES} // UI_SENSOR_CALIBRATION
};

enum class DisplayRedrawReason : uint8_t {
  NONE = 0,
  SCREEN_ENTER,
  OPERATOR_OVERLAY,
  TAP_ACTION,
  LIVE_DATA_CHANGED,
  TIMER_KEEPALIVE,
  SPARKLINE_REFRESH,
  THEME_CHANGED,
  LANGUAGE_CHANGED,
  LAYOUT_CHANGED,
  RECOVERY_REDRAW
};

struct ModeSetupDraft {
  bool valid = false;
  Mode mode = Mode::IDLE;
  uint32_t dirtyMask = 0;
  bool profileRelevant = false;
  bool profileCompatible = false;
  // The loaded profile is retained separately from the editable selection.
  // It makes discard deterministic and lets navigation refuse to throw away a
  // profile choice implicitly.
  char loadedProfileId[48] = {0};
  char loadedProfileName[64] = {0};
  char profileId[48] = {0};
  char profileName[64] = {0};
  char profilePreview[112] = {0};
};

static constexpr uint8_t TFT_PREFLIGHT_MAX_ITEMS = 24;
static constexpr uint8_t TFT_PREFLIGHT_ROWS_PER_PAGE = 3;

struct TftPreflightSnapshot {
  bool attempted = false;
  bool ready = false;
  uint8_t blockingCount = 0;
  uint8_t warningCount = 0;
  uint8_t itemCount = 0;
  uint32_t checkedAtMs = 0;
  char title[64] = {0};
  char detail[144] = {0};
  char itemTitle[TFT_PREFLIGHT_MAX_ITEMS][64] = {{0}};
  char itemDetail[TFT_PREFLIGHT_MAX_ITEMS][144] = {{0}};
  char itemTone[TFT_PREFLIGHT_MAX_ITEMS][12] = {{0}};
};

struct TftProfileOption {
  char id[48] = {0};
  char name[64] = {0};
  uint16_t useCount = 0;
  bool builtin = false;
};

static constexpr uint8_t TFT_EVENT_CACHE_SIZE = 6;
struct TftEventCache {
  uint8_t count = 0;
  uint8_t level[TFT_EVENT_CACHE_SIZE] = {0};
  uint32_t timestamp[TFT_EVENT_CACHE_SIZE] = {0};
  char message[TFT_EVENT_CACHE_SIZE][128] = {{0}};
};

struct UiState {
  UiScreen currentScreen = UI_DASHBOARD;
  UiScreen rootScreen = UI_DASHBOARD;
  UiScreen returnScreen = UI_DASHBOARD;
  UiScreen stack[6] = {};
  uint8_t stackDepth = 0;
  bool needsRedraw = true;
  UiScreen lastRenderedScreen = UI_DASHBOARD;
  DisplayRedrawReason pendingRedrawReason = DisplayRedrawReason::SCREEN_ENTER;
  bool forceFullRedraw = true;
  uint32_t redrawGeneration = 0;

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

  // New setup route context. It is deliberately separate from g_settings:
  // opening or leaving the preparation card never persists a half-edited run.
  Mode selectedMode = Mode::IDLE;
  char selectedProfileId[48] = {0};
  ModeSetupDraft setupDraft;
  uint8_t profilePickerPage = 0;
  uint8_t preflightPage = 0;
  uint8_t modeConfigPage = 0;
  Mode modeParametersMode = Mode::IDLE;
  uint8_t modeParametersPage = 0;
  uint8_t modeParametersStep = 0;
  uint8_t processMenuPage = 0;
  bool serviceSession = false;
  uint8_t serviceTestPage = 0;
  bool serviceTestOutputActive = false;
  uint32_t serviceTestDeadlineMs = 0;
  ServiceTestSession::Target serviceTestActiveTarget =
      ServiceTestSession::Target::NONE;
  float serviceTestPumpStartVolumeMl = 0.0f;
  char serviceTestMessage[144] = {0};
  char pumpCalibrationMessage[144] = {0};
  uint8_t sensorCalibrationPage = 0;
  uint8_t sensorCalibrationTempRole = 0;
  uint8_t sensorCalibrationScanCount = 0;
  uint8_t sensorCalibrationScanIndex = 0;
  uint8_t sensorCalibrationAddresses[TEMP_COUNT][8] = {};
  char sensorCalibrationMessage[144] = {0};
  char safetyActionMessage[144] = {0};
  char setupDraftMessage[144] = {0};
  TftOperatorOverlayLogic::State operatorOverlay;
  char operatorOverlayMessage[96] = {0};
};

static UiState ui;
static bool handleOperatorOverlayTap(int16_t tx, int16_t ty,
                                     const SystemState &state);
static bool syncOperatorOverlay(const SystemState &state);
static void openOperatorOverlay(TftOperatorOverlayLogic::Action action);
static void closeOperatorOverlay();
static void drawOperatorOverlay(const SystemState &state);
static void setServiceTestMessage(const char *message);
static void stopServiceTestOutputs(const char *message);
static void cancelPumpCalibration(const char *message);
static void setSensorCalibrationMessage(const char *message);
static TftPreflightSnapshot g_tftPreflight;
static TftProfileOption g_tftProfileOptions[MAX_PROFILES];
static uint8_t g_tftProfileOptionCount = 0;
static constexpr uint8_t TFT_PROFILE_ROWS_PER_PAGE = 4;
static TftEventCache g_tftEvents;
static SparklineBuffer<30> cubeTempHistory;
static SparklineBuffer<30> topTempHistory;
static uint32_t lastSparklineUpdateMs = 0;

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

static const char *getDisplayRedrawReasonKey(DisplayRedrawReason reason) {
  switch (reason) {
  case DisplayRedrawReason::SCREEN_ENTER:
    return "screen_enter";
  case DisplayRedrawReason::OPERATOR_OVERLAY:
    return "operator_overlay";
  case DisplayRedrawReason::TAP_ACTION:
    return "tap_action";
  case DisplayRedrawReason::LIVE_DATA_CHANGED:
    return "live_data_changed";
  case DisplayRedrawReason::TIMER_KEEPALIVE:
    return "timer_keepalive";
  case DisplayRedrawReason::SPARKLINE_REFRESH:
    return "sparkline_refresh";
  case DisplayRedrawReason::THEME_CHANGED:
    return "theme_changed";
  case DisplayRedrawReason::LANGUAGE_CHANGED:
    return "language_changed";
  case DisplayRedrawReason::LAYOUT_CHANGED:
    return "layout_changed";
  case DisplayRedrawReason::RECOVERY_REDRAW:
    return "recovery_redraw";
  case DisplayRedrawReason::NONE:
  default:
    return "unknown";
  }
}

static uint8_t getDisplayRedrawReasonPriority(DisplayRedrawReason reason) {
  switch (reason) {
  case DisplayRedrawReason::RECOVERY_REDRAW:
    return 90;
  case DisplayRedrawReason::THEME_CHANGED:
  case DisplayRedrawReason::LANGUAGE_CHANGED:
    return 80;
  case DisplayRedrawReason::OPERATOR_OVERLAY:
    return 75;
  case DisplayRedrawReason::SCREEN_ENTER:
  case DisplayRedrawReason::LAYOUT_CHANGED:
    return 70;
  case DisplayRedrawReason::SPARKLINE_REFRESH:
    return 50;
  case DisplayRedrawReason::TAP_ACTION:
    return 40;
  case DisplayRedrawReason::LIVE_DATA_CHANGED:
    return 30;
  case DisplayRedrawReason::TIMER_KEEPALIVE:
    return 20;
  case DisplayRedrawReason::NONE:
  default:
    return 0;
  }
}

static bool redrawRequiresFullFrame(DisplayRedrawReason reason) {
  switch (reason) {
  case DisplayRedrawReason::SCREEN_ENTER:
  case DisplayRedrawReason::OPERATOR_OVERLAY:
  case DisplayRedrawReason::THEME_CHANGED:
  case DisplayRedrawReason::LANGUAGE_CHANGED:
  case DisplayRedrawReason::LAYOUT_CHANGED:
  case DisplayRedrawReason::RECOVERY_REDRAW:
    return true;
  case DisplayRedrawReason::NONE:
  case DisplayRedrawReason::TAP_ACTION:
  case DisplayRedrawReason::LIVE_DATA_CHANGED:
  case DisplayRedrawReason::TIMER_KEEPALIVE:
  case DisplayRedrawReason::SPARKLINE_REFRESH:
  default:
    return false;
  }
}

static void requestRedraw(DisplayRedrawReason reason, bool forceFull = false) {
  if (!ui.needsRedraw ||
      getDisplayRedrawReasonPriority(reason) >=
          getDisplayRedrawReasonPriority(ui.pendingRedrawReason)) {
    ui.pendingRedrawReason = reason;
  }
  ui.needsRedraw = true;
  ui.redrawGeneration++;
  if (forceFull || redrawRequiresFullFrame(reason)) {
    ui.forceFullRedraw = true;
  }
}

static void pushScreen(UiScreen screen) {
  if (ui.currentScreen == screen)
    return;
  if (ui.stackDepth < 6) {
    ui.stack[ui.stackDepth++] = ui.currentScreen;
  }
  ui.returnScreen = ui.currentScreen;
  ui.currentScreen = screen;
  requestRedraw(DisplayRedrawReason::SCREEN_ENTER);
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
  ui.returnScreen = (ui.stackDepth > 0) ? ui.stack[ui.stackDepth - 1]
                                        : ui.rootScreen;
  requestRedraw(DisplayRedrawReason::SCREEN_ENTER);
  armScreenSwitchGuard();
}

static void switchRoot(UiScreen screen) {
  const bool changed = (ui.rootScreen != screen) ||
                       (ui.currentScreen != screen) || (ui.stackDepth != 0);
  ui.rootScreen = screen;
  ui.currentScreen = screen;
  ui.returnScreen = screen;
  ui.stackDepth = 0;
  ui.serviceSession = TftHmiPolicyLogic::transitionServiceSession(
      ui.serviceSession,
      screen == UI_SERVICE
          ? TftHmiPolicyLogic::ServiceSessionEvent::EnterServiceRoot
          : TftHmiPolicyLogic::ServiceSessionEvent::LeaveServiceRoot);
  if (changed) {
    requestRedraw(DisplayRedrawReason::SCREEN_ENTER);
    armScreenSwitchGuard();
  }
}

typedef void (*ValueSaveCallback)(float);
struct ValueEditState {
  const EditableFields::Descriptor *descriptor = nullptr;
  float value;
  ValueSaveCallback onSave;
  bool returnToModeMonitor = false;
  bool returnToProcessParams = false;
};
static ValueEditState edit;

static void openValueEdit(EditableFields::Id field, float val,
                          ValueSaveCallback cb,
                          bool returnToModeMonitor = false,
                          bool returnToProcessParams = false) {
  edit.descriptor = &EditableFields::get(field);
  edit.value = EditableFields::normalize(field, val);
  edit.onSave = cb;
  edit.returnToModeMonitor = returnToModeMonitor;
  edit.returnToProcessParams = returnToProcessParams;
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
static uint8_t distParamsPage = 0; // 0 = отбор, 1 = программа фракций
static uint8_t g_editMashStepIdx = 0;
static uint8_t g_editHoldStepIdx = 0;

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
      EditableFields::normalize(EditableFields::Id::RECT_HEADS_PERCENT,
                                g_settings.rectParams.headsPercent);
  g_settings.rectParams.bodyPercent =
      EditableFields::normalize(EditableFields::Id::RECT_BODY_PERCENT,
                                g_settings.rectParams.bodyPercent);
  g_settings.rectParams.tailsPercent =
      EditableFields::normalize(EditableFields::Id::RECT_TAILS_PERCENT,
                                g_settings.rectParams.tailsPercent);

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
  uint8_t tempValidMask = 0;
  uint32_t tempsLastUpdate = 0;
  uint32_t uptime = 0;
  bool safetyOk = true;
  char networkStatus[32] = {0};
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
struct GlobalStatusRenderCache {
  char mode[48] = {0};
  char network[32] = {0};
  bool safetyOk = true;
};
static GlobalStatusRenderCache g_globalStatusCache;
static char g_modeTileCache[12][20] = {{0}};
static char g_allTempsValueCache[TEMP_COUNT][20] = {{0}};
static char g_allTempsStatusCache[TEMP_COUNT][12] = {{0}};
static char g_allTempsSummaryCache[96] = {0};
static Mode g_modeRuntimeMode = Mode::IDLE;
static uint32_t g_modeRuntimeStartUptime = 0;

// ILI9488 is updated directly over SPI: keep static presenter snapshots well
// below internal RAM pressure and never allocate a full framebuffer.
static constexpr size_t ILI9488_UI_CACHE_BUDGET_BYTES = 24U * 1024U;
static_assert(sizeof(UiState) + sizeof(TftPreflightSnapshot) +
                  sizeof(g_tftProfileOptions) + sizeof(TftEventCache) +
                  sizeof(UiLiveCache) + sizeof(DashboardRenderCache) +
                  sizeof(GlobalStatusRenderCache) + sizeof(g_modeTileCache) +
                  sizeof(g_allTempsValueCache) +
                  sizeof(g_allTempsStatusCache) +
                  sizeof(g_allTempsSummaryCache) <=
              ILI9488_UI_CACHE_BUDGET_BYTES,
              "TFT presenter cache exceeds the ILI9488 RAM budget");

static void resetAllTempsCache() {
  memset(g_allTempsValueCache, 0, sizeof(g_allTempsValueCache));
  memset(g_allTempsStatusCache, 0, sizeof(g_allTempsStatusCache));
  g_allTempsSummaryCache[0] = '\0';
}

struct DisplayRuntimeStatsInternal {
  uint32_t framesRendered = 0;
  uint32_t fullRedraws = 0;
  uint32_t partialRedraws = 0;
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
  uint32_t redrawReasonScreenEnter = 0;
  uint32_t redrawReasonTapAction = 0;
  uint32_t redrawReasonLiveDataChanged = 0;
  uint32_t redrawReasonTimerKeepalive = 0;
  uint32_t redrawReasonSparklineRefresh = 0;
  uint32_t redrawReasonThemeChanged = 0;
  uint32_t redrawReasonLanguageChanged = 0;
  uint32_t redrawReasonLayoutChanged = 0;
  uint32_t redrawReasonRecovery = 0;
  DisplayRedrawReason lastRedrawReason = DisplayRedrawReason::NONE;
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
  case DisplayRefreshProfile::SAFE:
    return 5000;
  case DisplayRefreshProfile::FAST:
    return 500;
  case DisplayRefreshProfile::NORMAL:
  default:
    return 1000;
  }
}

static uint32_t getLiveRedrawMinGapMs() {
  extern Settings g_settings;
  switch (g_settings.displaySettings.refreshProfile) {
  case DisplayRefreshProfile::SAFE:
    return 900;
  case DisplayRefreshProfile::FAST:
    return 180;
  case DisplayRefreshProfile::NORMAL:
  default:
    return 300;
  }
}

static uint32_t getProgressRefreshIntervalMs() {
  extern Settings g_settings;
  switch (g_settings.displaySettings.refreshProfile) {
  case DisplayRefreshProfile::SAFE:
    return 2200;
  case DisplayRefreshProfile::FAST:
    return 600;
  case DisplayRefreshProfile::NORMAL:
  default:
    return 1200;
  }
}

static uint32_t getSparklineRefreshIntervalMs() {
  extern Settings g_settings;
  switch (g_settings.displaySettings.refreshProfile) {
  case DisplayRefreshProfile::SAFE:
    return 8000;
  case DisplayRefreshProfile::FAST:
    return 3000;
  case DisplayRefreshProfile::NORMAL:
  default:
    return 5000;
  }
}

static uint8_t getUiTempValidMask(const SystemState &state) {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < TEMP_COUNT && i < 8; ++i) {
    if (state.temps.valid[i]) {
      mask |= (1U << i);
    }
  }
  return mask;
}

static bool isTempRoleEnabledByTopology(uint8_t index) {
  switch (index) {
  case TEMP_CUBE:
    return g_settings.equipment.temperatureTopology.cube;
  case TEMP_COLUMN_BOTTOM:
    return g_settings.equipment.temperatureTopology.columnBottom;
  case TEMP_COLUMN_TOP:
    return g_settings.equipment.temperatureTopology.columnTop;
  case TEMP_REFLUX:
    return g_settings.equipment.temperatureTopology.reflux;
  case TEMP_TSA:
    return g_settings.equipment.temperatureTopology.tsa;
  case TEMP_WATER_IN:
    return g_settings.equipment.temperatureTopology.waterIn;
  case TEMP_WATER_OUT:
    return g_settings.equipment.temperatureTopology.waterOut;
  default:
    return false;
  }
}

static uint8_t countExpectedTempSensors() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < TEMP_COUNT; ++i) {
    if (isTempRoleEnabledByTopology(i)) {
      count++;
    }
  }
  return count;
}

static bool isModeRunning(const SystemState &state);

static const UiScreenPolicy &getUiScreenPolicy(UiScreen screen) {
  const size_t screenIndex = static_cast<size_t>(screen);
  if (screenIndex < (sizeof(kUiScreenPolicies) / sizeof(kUiScreenPolicies[0]))) {
    return kUiScreenPolicies[screenIndex];
  }
  return kUiScreenPolicies[0];
}

static UiScreen getMonitorRootScreenForState(const SystemState &state) {
  return isModeRunning(state) ? UI_MODE_MONITOR : UI_DASHBOARD;
}

static bool isRootNavigationScreen(UiScreen screen) {
  return getUiScreenPolicy(screen).rootLike;
}

static bool screenSupportsLiveRefresh(UiScreen screen) {
  return getUiScreenPolicy(screen).partialRefreshFields != UI_LIVE_NONE;
}

static bool hasProcessStateChanged(const SystemState &state) {
  return uiLive.mode != state.mode || uiLive.phase != state.rectPhase ||
         uiLive.paused != state.paused;
}

static bool hasTemperatureDataChanged(const SystemState &state) {
  return uiLive.tempValidMask != getUiTempValidMask(state) ||
         uiLive.tempsLastUpdate != state.temps.lastUpdate ||
         fabsf(uiLive.tCube - state.temps.cube) > 0.1f ||
         fabsf(uiLive.tTop - state.temps.columnTop) > 0.1f ||
         fabsf(uiLive.tReflux - state.temps.reflux) > 0.1f ||
         fabsf(uiLive.tTsa - state.temps.tsa) > 0.1f ||
         fabsf(uiLive.tWaterIn - state.temps.waterIn) > 0.1f ||
         fabsf(uiLive.tWaterOut - state.temps.waterOut) > 0.1f;
}

static bool hasPowerDataChanged(const SystemState &state) {
  return fabsf(uiLive.power - state.power.power) > 1.0f ||
         fabsf(uiLive.pumpSpeed - state.pump.speedMlPerHour) > 1.0f ||
         fabsf(uiLive.voltage - state.power.voltage) > 1.0f ||
         fabsf(uiLive.pressure - state.pressure.cube) > 1.0f;
}

static bool hasPageLiveDataChanged(UiScreen screen, const SystemState &state,
                                   uint32_t now) {
  const uint8_t fields = getUiScreenPolicy(screen).partialRefreshFields;
  if ((fields & UI_LIVE_PROCESS) != 0 && hasProcessStateChanged(state)) {
    return true;
  }
  if ((fields & UI_LIVE_TEMPERATURES) != 0 &&
      hasTemperatureDataChanged(state)) {
    return true;
  }
  if ((fields & UI_LIVE_POWER) != 0 && hasPowerDataChanged(state)) {
    return true;
  }
  if ((fields & UI_LIVE_UPTIME) != 0 && state.uptime != uiLive.uptime &&
      (now - uiLive.lastUpdateMs) > 1000) {
    return true;
  }
  return (fields & UI_LIVE_PROGRESS) != 0 &&
         (now - uiLive.lastUpdateMs) > getProgressRefreshIntervalMs();
}

static constexpr uint16_t ILI9488_PARTIAL_FRAME_TARGET_MS = 80;
static constexpr uint16_t ILI9488_PARTIAL_FRAME_LIMIT_MS = 120;
static constexpr uint16_t ILI9488_FULL_FRAME_LIMIT_MS = 250;
static_assert(ILI9488_PARTIAL_FRAME_TARGET_MS <
                  ILI9488_PARTIAL_FRAME_LIMIT_MS &&
                  ILI9488_PARTIAL_FRAME_LIMIT_MS <
                      ILI9488_FULL_FRAME_LIMIT_MS,
              "Invalid ILI9488 frame budget ordering");
static const uint16_t DISPLAY_SLOW_FRAME_MS = ILI9488_PARTIAL_FRAME_LIMIT_MS;
static const uint16_t DISPLAY_HARD_FRAME_MS = ILI9488_FULL_FRAME_LIMIT_MS;
static const uint8_t DISPLAY_SOFT_WD_THRESHOLD = 3;
static const uint8_t DISPLAY_HARD_FRAME_BURST_THRESHOLD = 6;
static const uint8_t DISPLAY_SOFT_WD_BURST_FOR_HARD = 3;
static const uint32_t DISPLAY_SOFT_WD_WINDOW_MS = 12000;
static const uint32_t DISPLAY_HARD_RECOVERY_COOLDOWN_MS = 15000;
static const uint16_t DISPLAY_UPDATE_GAP_WARN_MS = INTERVAL_DISPLAY_UPDATE * 3;

static const int16_t TFT_BUTTON_GAP = 3;
static const int16_t TFT_BUTTON_MARGIN_X = 6;
static const int16_t CTRL_BW = 232;
static const int16_t CTRL_BH = 43;
static const int16_t CTRL_X1 = TFT_BUTTON_MARGIN_X;
static const int16_t CTRL_X2 = CTRL_X1 + CTRL_BW + TFT_BUTTON_GAP;
static const int16_t CTRL_STATUS_Y = UI_GLOBAL_STATUS_H + 5;
static const int16_t CTRL_STATUS_H = 26;
static const int16_t CTRL_ACTION_BW = 84;
static const int16_t CTRL_ACTION_BH = 22;
static const int16_t CTRL_ACTION_Y = UI_GLOBAL_STATUS_H + 7;
static const int16_t CTRL_ACTION_GAP = TFT_BUTTON_GAP;
static const int16_t CTRL_Y1 = UI_GLOBAL_STATUS_H + 38;
static const int16_t CTRL_Y2 = CTRL_Y1 + CTRL_BH + TFT_BUTTON_GAP;
static const int16_t CTRL_Y3 = CTRL_Y2 + CTRL_BH + TFT_BUTTON_GAP;
static const int16_t CTRL_Y4 = CTRL_Y3 + CTRL_BH + TFT_BUTTON_GAP;
static const int16_t PROCESS_MENU_X1 = 10;
static const int16_t PROCESS_MENU_GAP = TFT_BUTTON_GAP;
static const int16_t PROCESS_MENU_W =
    (TFT_WIDTH - PROCESS_MENU_X1 * 2 - PROCESS_MENU_GAP * 2) / 3;
static const int16_t PROCESS_MENU_X2 = PROCESS_MENU_X1 + PROCESS_MENU_W +
                                       PROCESS_MENU_GAP;
static const int16_t PROCESS_MENU_X3 = PROCESS_MENU_X2 + PROCESS_MENU_W +
                                       PROCESS_MENU_GAP;
static const int16_t PROCESS_MENU_H = 44;
static const int16_t PROCESS_MENU_Y1 = UI_GLOBAL_STATUS_H + 78;
static const int16_t PROCESS_MENU_Y2 =
    PROCESS_MENU_Y1 + PROCESS_MENU_H + PROCESS_MENU_GAP;
static const int16_t SETTINGS_CARD_X1 = TFT_BUTTON_MARGIN_X;
static const int16_t SETTINGS_CARD_W = 232;
static const int16_t SETTINGS_CARD_X2 =
    SETTINGS_CARD_X1 + SETTINGS_CARD_W + TFT_BUTTON_GAP;
static const int16_t SETTINGS_CARD_Y1 = 53;
static const int16_t SETTINGS_CARD_H = 63;
static const int16_t SETTINGS_CARD_Y2 =
    SETTINGS_CARD_Y1 + SETTINGS_CARD_H + TFT_BUTTON_GAP;
static const int16_t SETTINGS_TOGGLE_Y =
    SETTINGS_CARD_Y2 + SETTINGS_CARD_H + TFT_BUTTON_GAP;
static const int16_t SETTINGS_TOGGLE_W = 154;
static const int16_t SETTINGS_TOGGLE_H = 32;
static const int16_t SETTINGS_TOGGLE_X1 = TFT_BUTTON_MARGIN_X;
static const int16_t SETTINGS_TOGGLE_X2 =
    SETTINGS_TOGGLE_X1 + SETTINGS_TOGGLE_W + TFT_BUTTON_GAP;
static const int16_t SETTINGS_TOGGLE_X3 =
    SETTINGS_TOGGLE_X2 + SETTINGS_TOGGLE_W + TFT_BUTTON_GAP;
static const int16_t SETTINGS_PROFILE_X = TFT_BUTTON_MARGIN_X;
static const int16_t SETTINGS_PROFILE_Y =
    SETTINGS_TOGGLE_Y + SETTINGS_TOGGLE_H + TFT_BUTTON_GAP;
static const int16_t SETTINGS_PROFILE_W = SETTINGS_CARD_W;
static const int16_t SETTINGS_PROFILE_H = 32;
static const int16_t SETTINGS_STIRRER_X = SETTINGS_CARD_X2;
static const int16_t SETTINGS_STIRRER_Y = SETTINGS_PROFILE_Y;
static const int16_t SETTINGS_STIRRER_W = SETTINGS_CARD_W;
static const int16_t SETTINGS_STIRRER_H = SETTINGS_PROFILE_H;
static const int16_t MANUAL_VALVE_Y = 146;
static const int16_t MANUAL_VALVE_W = 154;
static const int16_t MANUAL_VALVE_H = 76;
static const int16_t MANUAL_VALVE_X1 = TFT_BUTTON_MARGIN_X;
static const int16_t MANUAL_VALVE_X2 =
    MANUAL_VALVE_X1 + MANUAL_VALVE_W + TFT_BUTTON_GAP;
static const int16_t MANUAL_VALVE_X3 =
    MANUAL_VALVE_X2 + MANUAL_VALVE_W + TFT_BUTTON_GAP;
static const int16_t VALUE_EDIT_BTN_W = 114;
static const int16_t VALUE_EDIT_BTN_H = 56;
static const int16_t VALUE_EDIT_BTN_Y = 138;
static const int16_t VALUE_EDIT_BTN_X1 = TFT_BUTTON_MARGIN_X;
static const int16_t VALUE_EDIT_BTN_X2 =
    VALUE_EDIT_BTN_X1 + VALUE_EDIT_BTN_W + TFT_BUTTON_GAP;
static const int16_t VALUE_EDIT_BTN_X3 =
    VALUE_EDIT_BTN_X2 + VALUE_EDIT_BTN_W + TFT_BUTTON_GAP;
static const int16_t VALUE_EDIT_BTN_X4 =
    VALUE_EDIT_BTN_X3 + VALUE_EDIT_BTN_W + TFT_BUTTON_GAP;
static const int16_t ROOT_FRAME_X = TFT_BUTTON_MARGIN_X;
static const int16_t ROOT_FRAME_W = TFT_WIDTH - ROOT_FRAME_X * 2;
static const int16_t ROOT_STATUS_Y = UI_GLOBAL_STATUS_H + 4;
static const int16_t ROOT_STATUS_H = 40;
static const int16_t ROOT_PANEL_Y =
    ROOT_STATUS_Y + ROOT_STATUS_H + TFT_BUTTON_GAP;
static const int16_t ROOT_PANEL_H = 138;
static const int16_t ROOT_INFO_Y = ROOT_PANEL_Y + ROOT_PANEL_H + TFT_BUTTON_GAP;
static const int16_t ROOT_INFO_H = 36;
static const int16_t ROOT_LEFT_X = ROOT_FRAME_X;
static const int16_t ROOT_LEFT_W = 154;
static const int16_t ROOT_RIGHT_X = ROOT_LEFT_X + ROOT_LEFT_W + TFT_BUTTON_GAP;
static const int16_t ROOT_RIGHT_W = TFT_WIDTH - ROOT_RIGHT_X - ROOT_FRAME_X;
static const int16_t ROOT_GRID_COL_GAP = TFT_BUTTON_GAP;
static const int16_t ROOT_GRID_ROW_GAP = TFT_BUTTON_GAP;
static const int16_t ROOT_RIGHT_TILE_W = (ROOT_RIGHT_W - ROOT_GRID_COL_GAP) / 2;
static const int16_t ROOT_RIGHT_TILE_H =
    (ROOT_PANEL_H - ROOT_GRID_ROW_GAP * 2) / 3;
static const int16_t HOME_TILE_MARGIN_X = 10;
static const int16_t HOME_TILE_GAP = 8;
static const int16_t HOME_TILE_W =
    (TFT_WIDTH - HOME_TILE_MARGIN_X * 2 - HOME_TILE_GAP) / 2;
static const int16_t HOME_TILE_H = 70;
static const int16_t HOME_TILE_Y1 = UI_GLOBAL_STATUS_H + 52;
static const int16_t HOME_TILE_Y2 = HOME_TILE_Y1 + HOME_TILE_H + HOME_TILE_GAP;
static const int16_t HOME_TILE_X1 = HOME_TILE_MARGIN_X;
static const int16_t HOME_TILE_X2 = HOME_TILE_X1 + HOME_TILE_W + HOME_TILE_GAP;
static const int16_t HOME_SAFETY_W = 116;
static const int16_t HOME_SAFETY_H = 24;
static const int16_t HOME_SAFETY_X = TFT_WIDTH - HOME_TILE_MARGIN_X - HOME_SAFETY_W;
static const int16_t HOME_SAFETY_Y = UI_GLOBAL_STATUS_H + 13;

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
  return getUiScreenPolicy(screen).autoMonitorRoot;
}

static bool isRootWorkspaceTap(int16_t ty) {
  return ty >= UI_GLOBAL_STATUS_H && ty < (TFT_HEIGHT - UI_FOOTER_H);
}

static bool isRootMetricsTap(int16_t tx, int16_t ty) {
  return hit(tx, ty, ROOT_RIGHT_X, ROOT_PANEL_Y, ROOT_RIGHT_W, ROOT_PANEL_H);
}

static bool isRootControlTap(int16_t tx, int16_t ty) {
  return isRootWorkspaceTap(ty) && tx < ROOT_RIGHT_X;
}

static bool isServiceTempsTap(int16_t tx, int16_t ty) {
  return hit(tx, ty, 10, 162, 225, 64);
}

static bool isServiceTestTap(int16_t tx, int16_t ty) {
  return hit(tx, ty, 245, 162, 225, 64);
}

static void requestFullScreenRedraw() {
  requestRedraw(DisplayRedrawReason::SCREEN_ENTER, true);
}

static bool isScreenEditable(UiScreen screen, const SystemState &state);

static TftHmiPolicyLogic::RuntimeMode getTftHmiRuntimeMode(Mode mode) {
  using RuntimeMode = TftHmiPolicyLogic::RuntimeMode;
  switch (mode) {
  case Mode::RECTIFICATION:
    return RuntimeMode::Rectification;
  case Mode::MANUAL_RECT:
    return RuntimeMode::ManualRectification;
  case Mode::DISTILLATION:
    return RuntimeMode::Distillation;
  case Mode::MASHING:
    return RuntimeMode::Mashing;
  case Mode::HOLD:
    return RuntimeMode::Hold;
  case Mode::NBK:
    return RuntimeMode::Nbk;
  case Mode::FERMENTATION:
    return RuntimeMode::Fermentation;
  case Mode::IDLE:
  default:
    return RuntimeMode::Idle;
  }
}

static bool isModeMonitorEditAllowed(Mode mode) {
  return TftHmiPolicyLogic::isModeMonitorEditable(
      getTftHmiRuntimeMode(mode));
}

static bool isScreenEditable(UiScreen screen, const SystemState &state) {
  return TftHmiPolicyLogic::resolveFieldVisibility(
             getUiScreenPolicy(screen).editPolicy, isModeRunning(state),
             getTftHmiRuntimeMode(state.mode)) ==
         TftHmiPolicyLogic::FieldVisibility::Editable;
}

static bool isScreenReadOnly(UiScreen screen, const SystemState &state) {
  return TftHmiPolicyLogic::resolveFieldVisibility(
             getUiScreenPolicy(screen).editPolicy, isModeRunning(state),
             getTftHmiRuntimeMode(state.mode)) !=
         TftHmiPolicyLogic::FieldVisibility::Editable;
}

static const char *getModeMonitorAccessHint(Mode mode) {
  const bool ru = (g_settings.language == 0);
  switch (mode) {
  case Mode::DISTILLATION:
    return ru ? "Дистил.: правка мощности и стоп-темп."
              : "Distillation: power and stop-temp editable";
  case Mode::MANUAL_RECT:
    return ru ? "Ручн. рект.: правка слева, датчики справа"
              : "Manual rect: edit left, sensors right";
  case Mode::MASHING:
    return ru ? "Затирка: слева темп., справа время"
              : "Mashing: temp left, time right";
  case Mode::HOLD:
    return ru ? "Пастер.: слева темп., справа время"
              : "Hold: temp left, time right";
  case Mode::RECTIFICATION:
    return ru ? "Ректиф.: только просмотр"
              : "Rectification: view only";
  case Mode::NBK:
    return ru ? "НБК: только просмотр" : "NBK: view only";
  case Mode::FERMENTATION:
    return ru ? "Брожение: только просмотр"
              : "Fermentation: view only";
  default:
    return ru ? "Монитор: только просмотр" : "Monitor: view only";
  }
}

static void setRectificationIndicatorHint(
    char *infoLine, size_t infoLineSize, uint16_t &tone,
    const ControlV2::ModeStatusV2 &modeStatus,
    const ControlV2::ProcessIndicatorsV2 &indicators) {
  const bool ru = (g_settings.language == 0);
  tone = COLOR_INFO;

  if (modeStatus.safetyLatched) {
    tone = COLOR_DANGER;
    snprintf(infoLine, infoLineSize, "%s",
             (modeStatus.operatorMessage[0] != '\0')
                 ? modeStatus.operatorMessage
                 : (ru ? "Safety удерживает процесс"
                       : "Safety latch holds the process"));
    return;
  }

  if (!indicators.sensorFreshnessOk) {
    tone = COLOR_DANGER;
    snprintf(infoLine, infoLineSize, "%s",
             ru ? "Датчики неактуальны: автоматика снижает доверие"
                : "Sensor data stale: automation confidence reduced");
    return;
  }

  if (indicators.recoveryActive) {
    tone = COLOR_WARNING;
    snprintf(infoLine, infoLineSize, "%s",
             (modeStatus.operatorMessage[0] != '\0')
                 ? modeStatus.operatorMessage
                 : (ru ? "Идёт восстановление после ограничения"
                       : "Recovery is active after limiting"));
    return;
  }

  if (indicators.floodRisk >= 0.65f) {
    tone = COLOR_DANGER;
    snprintf(infoLine, infoLineSize, "%s",
             ru ? "Высокий риск захлёба: не ускоряйте отбор"
                : "High flood risk: do not increase takeoff");
    return;
  }

  if (indicators.coolingMarginC <= 0.0f) {
    tone = COLOR_DANGER;
    snprintf(infoLine, infoLineSize, "%s",
             ru ? "Охлаждение на пределе: проверьте воду"
                : "Cooling limit reached: check water");
    return;
  }

  if (modeStatus.activeLimits.takeoffBlocked || !indicators.takeoffAllowed) {
    tone = COLOR_WARNING;
    snprintf(infoLine, infoLineSize, "%s",
             ru ? "Отбор пока запрещён: ждём стабильности"
                : "Takeoff blocked: waiting for stability");
    return;
  }

  if (indicators.coolingMarginC < 5.0f) {
    tone = COLOR_WARNING;
    snprintf(infoLine, infoLineSize, "%s",
             ru ? "Запас охлаждения мал: не форсируйте процесс"
                : "Cooling margin low: avoid forcing the run");
    return;
  }

  if (indicators.bodyEndScore >= 0.80f) {
    tone = COLOR_WARNING;
    snprintf(infoLine, infoLineSize, "%s",
             ru ? "Конец тела вероятен: следите за качеством"
                : "Body end likely: watch product quality");
    return;
  }

  if (indicators.headsCompletionScore >= 0.80f) {
    tone = COLOR_SUCCESS;
    snprintf(infoLine, infoLineSize, "%s",
             ru ? "Головы почти завершены: готовьтесь к телу"
                : "Heads nearly complete: prepare for body");
    return;
  }

  if (indicators.stabilityIndex < 0.45f) {
    tone = COLOR_WARNING;
    snprintf(infoLine, infoLineSize, "%s",
             ru ? "Колонна стабилизируется: ждём спокойный верх"
                : "Column stabilizing: wait for calm top temp");
    return;
  }

  if (indicators.stabilityIndex >= 0.75f && indicators.takeoffAllowed) {
    tone = COLOR_SUCCESS;
    snprintf(infoLine, infoLineSize, "%s",
             ru ? "Процесс устойчив: отбор разрешён"
                : "Process stable: takeoff allowed");
    return;
  }

  snprintf(infoLine, infoLineSize, "%s",
           ru ? "Ректификация под наблюдением indicators"
              : "Rectification monitored by indicators");
}

static uint16_t dimmedButtonColor() {
  return (g_settings.theme == 1) ? tft.color565(92, 98, 104)
                                 : tft.color565(126, 132, 138);
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

// =============================================================================
// New mode setup and backend pre-flight route
// =============================================================================
static const char *getDisplayModeName(Mode mode);

static bool isSetupProfileRelevant(Mode mode) {
  return mode == Mode::RECTIFICATION || mode == Mode::MANUAL_RECT ||
         mode == Mode::DISTILLATION || mode == Mode::MASHING;
}

static bool isSetupProfileCompatible(const Profile &profile, Mode mode) {
  const bool rectProfile = profile.metadata.category == "rectification" ||
                           profile.parameters.mode == "rectification";
  const bool distProfile = profile.metadata.category == "distillation" ||
                           profile.parameters.mode == "distillation";
  const bool mashProfile = profile.metadata.category == "mashing" ||
                           profile.parameters.mode == "mashing";
  return ((mode == Mode::RECTIFICATION || mode == Mode::MANUAL_RECT) &&
          rectProfile) ||
         (mode == Mode::DISTILLATION && distProfile) ||
         (mode == Mode::MASHING && mashProfile);
}

static TftHmiPolicyLogic::DraftState getModeSetupDraftPolicyState() {
  TftHmiPolicyLogic::DraftState policy;
  policy.valid = ui.setupDraft.valid;
  policy.profileRelevant = ui.setupDraft.profileRelevant;
  policy.profileSelected = ui.setupDraft.profileId[0] != '\0';
  policy.profileCompatible = ui.setupDraft.profileCompatible;
  policy.profileChanged =
      strcmp(ui.setupDraft.profileId, ui.setupDraft.loadedProfileId) != 0;
  policy.dirtyMask = ui.setupDraft.dirtyMask;
  return policy;
}

static void applyModeSetupDraftPolicyState(
    const TftHmiPolicyLogic::DraftState &policy) {
  ui.setupDraft.dirtyMask = policy.dirtyMask;
}

static bool hasUnsavedModeSetupDraft() {
  return TftHmiPolicyLogic::hasUnsavedDraft(getModeSetupDraftPolicyState());
}

static void setModeSetupDraftMessage(const char *message) {
  if (message == nullptr) {
    ui.setupDraftMessage[0] = '\0';
    return;
  }
  strncpy(ui.setupDraftMessage, message, sizeof(ui.setupDraftMessage) - 1);
  ui.setupDraftMessage[sizeof(ui.setupDraftMessage) - 1] = '\0';
}

static void clearTftPreflight() {
  g_tftPreflight = TftPreflightSnapshot{};
  ui.preflightPage = 0;
}

static uint8_t getTftPreflightPageCount() {
  return g_tftPreflight.itemCount == 0
             ? 1
             : (g_tftPreflight.itemCount + TFT_PREFLIGHT_ROWS_PER_PAGE - 1) /
                   TFT_PREFLIGHT_ROWS_PER_PAGE;
}

static void loadTftRecentEvents() {
  g_tftEvents = TftEventCache{};
  // Logger already owns the authoritative recent-event ring. Read it only
  // when entering or explicitly refreshing this screen, then retain a small
  // fixed TFT snapshot instead of allocating during live redraws.
  const String json = Logger::getRecentEventsJson(TFT_EVENT_CACHE_SIZE);
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return;
  }
  JsonArray events = doc["events"].as<JsonArray>();
  for (int32_t source = static_cast<int32_t>(events.size()) - 1;
       source >= 0 && g_tftEvents.count < TFT_EVENT_CACHE_SIZE; --source) {
    JsonObject event = events[static_cast<size_t>(source)].as<JsonObject>();
    const uint8_t index = g_tftEvents.count++;
    g_tftEvents.level[index] = event["level"] | 0;
    g_tftEvents.timestamp[index] = event["timestamp"] | 0;
    const char *message = event["message"] | "";
    strncpy(g_tftEvents.message[index], message,
            sizeof(g_tftEvents.message[index]) - 1);
    g_tftEvents.message[index][sizeof(g_tftEvents.message[index]) - 1] =
        '\0';
  }
}

static void openProcessEvents() {
  loadTftRecentEvents();
  pushScreen(UI_PROCESS_EVENTS);
}

static void buildSetupProfilePreview(const Profile &profile, Mode mode,
                                     char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  const bool ru = (g_settings.language == 0);
  switch (mode) {
  case Mode::RECTIFICATION:
  case Mode::MANUAL_RECT:
    snprintf(out, outSize, "%u W | %u %s | %u ml",
             profile.parameters.heater.maxPower,
             profile.parameters.rectification.stabilizationMin,
             ru ? "мин стаб." : "min stab.",
             profile.parameters.rectification.bodyVolume);
    break;
  case Mode::DISTILLATION:
    snprintf(out, outSize, "%u W | %u ml/h | %.1f C",
             profile.parameters.heater.maxPower,
             profile.parameters.distillation.speed,
             profile.parameters.distillation.endTemp);
    break;
  case Mode::MASHING: {
    const size_t steps = profile.parameters.mashing.steps.size();
    const float firstTemp = steps > 0
                                ? profile.parameters.mashing.steps[0].temperature
                                : 0.0f;
    snprintf(out, outSize, "%u %s | %.1f C",
             static_cast<unsigned>(steps), ru ? "шаг(ов)" : "step(s)",
             firstTemp);
    break;
  }
  default:
    snprintf(out, outSize, "%s",
             ru ? "Профиль не меняет уставки этого режима"
                : "Profile does not change this mode settings");
    break;
  }
}

static void setSetupDraftProfile(const Profile &profile, Mode mode) {
  strncpy(ui.selectedProfileId, profile.id.c_str(),
          sizeof(ui.selectedProfileId) - 1);
  ui.selectedProfileId[sizeof(ui.selectedProfileId) - 1] = '\0';
  strncpy(ui.setupDraft.profileId, profile.id.c_str(),
          sizeof(ui.setupDraft.profileId) - 1);
  ui.setupDraft.profileId[sizeof(ui.setupDraft.profileId) - 1] = '\0';
  strncpy(ui.setupDraft.profileName, profile.metadata.name.c_str(),
          sizeof(ui.setupDraft.profileName) - 1);
  ui.setupDraft.profileName[sizeof(ui.setupDraft.profileName) - 1] = '\0';
  ui.setupDraft.profileCompatible = isSetupProfileCompatible(profile, mode);
  buildSetupProfilePreview(profile, mode, ui.setupDraft.profilePreview,
                           sizeof(ui.setupDraft.profilePreview));
}

static void updateModeSetupDraftDirtyState() {
  TftHmiPolicyLogic::DraftState policy = getModeSetupDraftPolicyState();
  TftHmiPolicyLogic::refreshProfileDirty(policy);
  applyModeSetupDraftPolicyState(policy);
}

static uint8_t getTftProfilePageCount() {
  return g_tftProfileOptionCount == 0
             ? 1
             : (g_tftProfileOptionCount + TFT_PROFILE_ROWS_PER_PAGE - 1) /
                   TFT_PROFILE_ROWS_PER_PAGE;
}

static void loadTftProfileOptions(Mode mode) {
  g_tftProfileOptionCount = 0;
  memset(g_tftProfileOptions, 0, sizeof(g_tftProfileOptions));
  if (!isSetupProfileRelevant(mode)) {
    return;
  }

  const std::vector<ProfileListItem> profiles = getProfileList();
  for (const ProfileListItem &item : profiles) {
    if (g_tftProfileOptionCount >= MAX_PROFILES) {
      break;
    }
    Profile profile;
    if (!loadProfile(item.id, profile) ||
        !isSetupProfileCompatible(profile, mode)) {
      continue;
    }
    TftProfileOption &option =
        g_tftProfileOptions[g_tftProfileOptionCount++];
    strncpy(option.id, profile.id.c_str(), sizeof(option.id) - 1);
    option.id[sizeof(option.id) - 1] = '\0';
    strncpy(option.name, profile.metadata.name.c_str(),
            sizeof(option.name) - 1);
    option.name[sizeof(option.name) - 1] = '\0';
    option.useCount = item.useCount;
    option.builtin = item.isBuiltin;
  }
  ui.profilePickerPage = 0;
}

static bool selectSetupProfile(uint8_t index) {
  if (index >= g_tftProfileOptionCount || !ui.setupDraft.valid) {
    return false;
  }
  Profile profile;
  if (!loadProfile(g_tftProfileOptions[index].id, profile) ||
      !isSetupProfileCompatible(profile, ui.setupDraft.mode)) {
    return false;
  }
  setSetupDraftProfile(profile, ui.setupDraft.mode);
  updateModeSetupDraftDirtyState();
  setModeSetupDraftMessage(
      (g_settings.language == 0) ? "Профиль выбран как черновик."
                                 : "Profile selected as a draft.");
  clearTftPreflight();
  return true;
}

static void openSetupProfilePicker() {
  if (!ui.setupDraft.valid || !ui.setupDraft.profileRelevant ||
      g_state.mode != Mode::IDLE) {
    return;
  }
  loadTftProfileOptions(ui.setupDraft.mode);
  pushScreen(UI_PROFILE_PICKER);
}

static bool validateModeSetupDraft(char *reason, size_t reasonSize) {
  if (reason == nullptr || reasonSize == 0) {
    return false;
  }
  reason[0] = '\0';
  if (!ui.setupDraft.valid || ui.setupDraft.mode == Mode::IDLE) {
    snprintf(reason, reasonSize, "%s",
             (g_settings.language == 0) ? "Режим не выбран."
                                        : "Mode is not selected.");
    return false;
  }
  if (!TftHmiPolicyLogic::maySaveDraft(getModeSetupDraftPolicyState())) {
    snprintf(reason, reasonSize, "%s",
             (g_settings.language == 0)
                 ? "Выбранный профиль не совместим с режимом."
                 : "Selected profile is not compatible with this mode.");
    return false;
  }
  if (!ui.setupDraft.profileRelevant || ui.setupDraft.profileId[0] == '\0') {
    return true;
  }

  Profile profile;
  if (!loadProfile(ui.setupDraft.profileId, profile) ||
      !isSetupProfileCompatible(profile, ui.setupDraft.mode)) {
    snprintf(reason, reasonSize, "%s",
             (g_settings.language == 0)
                 ? "Выбранный профиль больше не совместим с режимом."
                 : "Selected profile is no longer compatible with this mode.");
    return false;
  }

  if (ui.setupDraft.mode == Mode::MASHING) {
    const size_t steps = profile.parameters.mashing.steps.size();
    if (steps == 0) {
      snprintf(reason, reasonSize, "%s",
               (g_settings.language == 0)
                   ? "В профиле затирания нет шагов."
                   : "The mashing profile has no steps.");
      return false;
    }
  }
  return true;
}

static bool saveModeSetupDraft(char *reason, size_t reasonSize) {
  if (!validateModeSetupDraft(reason, reasonSize)) {
    return false;
  }
  if (!hasUnsavedModeSetupDraft()) {
    snprintf(reason, reasonSize, "%s",
             (g_settings.language == 0) ? "Черновик уже сохранён."
                                        : "Draft is already saved.");
    return true;
  }
  if (!ui.setupDraft.profileRelevant || ui.setupDraft.profileId[0] == '\0') {
    TftHmiPolicyLogic::DraftState policy = getModeSetupDraftPolicyState();
    TftHmiPolicyLogic::markDraftSaved(policy);
    applyModeSetupDraftPolicyState(policy);
    clearTftPreflight();
    return true;
  }

  Profile profile;
  if (!loadProfile(ui.setupDraft.profileId, profile)) {
    snprintf(reason, reasonSize, "%s",
             (g_settings.language == 0) ? "Не удалось загрузить профиль."
                                        : "Failed to load profile.");
    return false;
  }

  if (ui.setupDraft.mode == Mode::MASHING) {
    const size_t steps = profile.parameters.mashing.steps.size();
    memset(&mashProfileDefault, 0, sizeof(mashProfileDefault));
    strncpy(mashProfileDefault.name, profile.metadata.name.c_str(),
            sizeof(mashProfileDefault.name) - 1);
    mashProfileDefault.name[sizeof(mashProfileDefault.name) - 1] = '\0';
    mashProfileDefault.stepCount =
        static_cast<uint8_t>(steps > 10 ? 10 : steps);
    for (uint8_t i = 0; i < mashProfileDefault.stepCount; ++i) {
      const MashingStepParams &source = profile.parameters.mashing.steps[i];
      mashProfileDefault.steps[i].type = source.type;
      mashProfileDefault.steps[i].temperature = source.temperature;
      mashProfileDefault.steps[i].duration = source.duration;
      strncpy(mashProfileDefault.steps[i].name, source.name.c_str(),
              sizeof(mashProfileDefault.steps[i].name) - 1);
      mashProfileDefault.steps[i]
          .name[sizeof(mashProfileDefault.steps[i].name) - 1] = '\0';
    }
    setActiveProfile(profile.id, profile.metadata.name);
  } else if (!applyProfile(profile.id)) {
    snprintf(reason, reasonSize, "%s",
             (g_settings.language == 0) ? "Не удалось применить профиль."
                                        : "Failed to apply profile.");
    return false;
  }

  if (ui.setupDraft.mode == Mode::DISTILLATION) {
    distUi.speedMlH = g_settings.distillationUi.speedMlH;
    distUi.headsVolumeMl = g_settings.distillationUi.headsVolumeMl;
    distUi.targetVolumeMl = g_settings.distillationUi.targetVolumeMl;
    distUi.endTempC = g_settings.distillationUi.endTempC;
    distUi.powerPercent = g_settings.distillationUi.powerPercent;
    distUi.tailsVolumeMl = g_settings.distillationUi.tailsVolumeMl;
  }
  strncpy(ui.setupDraft.loadedProfileId, ui.setupDraft.profileId,
          sizeof(ui.setupDraft.loadedProfileId) - 1);
  ui.setupDraft.loadedProfileId[sizeof(ui.setupDraft.loadedProfileId) - 1] =
      '\0';
  strncpy(ui.setupDraft.loadedProfileName, ui.setupDraft.profileName,
          sizeof(ui.setupDraft.loadedProfileName) - 1);
  ui.setupDraft.loadedProfileName
      [sizeof(ui.setupDraft.loadedProfileName) - 1] = '\0';
  TftHmiPolicyLogic::DraftState policy = getModeSetupDraftPolicyState();
  TftHmiPolicyLogic::markDraftSaved(policy);
  applyModeSetupDraftPolicyState(policy);
  clearTftPreflight();
  return true;
}

static void loadModeSetupDraft(Mode mode) {
  if (mode == Mode::IDLE) {
    return;
  }

  clearTftPreflight();
  ui.selectedMode = mode;
  ui.setupDraft = ModeSetupDraft{};
  ui.setupDraft.valid = true;
  ui.setupDraft.mode = mode;
  ui.setupDraft.profileRelevant = isSetupProfileRelevant(mode);
  ui.modeConfigPage = 0;
  setModeSetupDraftMessage(nullptr);

  const String activeProfileId = getActiveProfileId();
  const String activeProfileName = getActiveProfileName();
  strncpy(ui.selectedProfileId, activeProfileId.c_str(),
          sizeof(ui.selectedProfileId) - 1);
  ui.selectedProfileId[sizeof(ui.selectedProfileId) - 1] = '\0';
  strncpy(ui.setupDraft.profileId, ui.selectedProfileId,
          sizeof(ui.setupDraft.profileId) - 1);
  ui.setupDraft.profileId[sizeof(ui.setupDraft.profileId) - 1] = '\0';
  strncpy(ui.setupDraft.profileName, activeProfileName.c_str(),
          sizeof(ui.setupDraft.profileName) - 1);
  ui.setupDraft.profileName[sizeof(ui.setupDraft.profileName) - 1] = '\0';

  Profile activeProfile;
  const bool profileLoaded =
      !activeProfileId.isEmpty() && loadProfile(activeProfileId, activeProfile);
  ui.setupDraft.profileCompatible =
      !ui.setupDraft.profileRelevant ||
      (profileLoaded && isSetupProfileCompatible(activeProfile, mode));
  if (profileLoaded && ui.setupDraft.profileCompatible) {
    setSetupDraftProfile(activeProfile, mode);
  }
  strncpy(ui.setupDraft.loadedProfileId, ui.setupDraft.profileId,
          sizeof(ui.setupDraft.loadedProfileId) - 1);
  ui.setupDraft.loadedProfileId[sizeof(ui.setupDraft.loadedProfileId) - 1] =
      '\0';
  strncpy(ui.setupDraft.loadedProfileName, ui.setupDraft.profileName,
          sizeof(ui.setupDraft.loadedProfileName) - 1);
  ui.setupDraft.loadedProfileName
      [sizeof(ui.setupDraft.loadedProfileName) - 1] = '\0';
}

static void discardModeSetupDraft() {
  if (!ui.setupDraft.valid) {
    return;
  }
  const Mode mode = ui.setupDraft.mode;
  loadModeSetupDraft(mode);
  TftHmiPolicyLogic::DraftState policy = getModeSetupDraftPolicyState();
  TftHmiPolicyLogic::markDraftDiscarded(policy);
  applyModeSetupDraftPolicyState(policy);
  setModeSetupDraftMessage(
      (g_settings.language == 0) ? "Черновик отброшен."
                                 : "Draft discarded.");
}

static void beginModeSetup(Mode mode) {
  if (g_state.mode != Mode::IDLE || mode == Mode::IDLE) {
    return;
  }
  // Navigation routes unsaved drafts to UI_MODE_CONFIG first. This is a
  // defensive second guard for any future caller that attempts a direct mode
  // switch from a dirty setup.
  if (TftHmiPolicyLogic::resolveDraftNavigation(
          getModeSetupDraftPolicyState(), ui.setupDraft.mode != mode) ==
      TftHmiPolicyLogic::DraftNavigationResult::ResolveDraft) {
    setModeSetupDraftMessage(
        (g_settings.language == 0)
            ? "Сначала сохраните или отбросьте текущий черновик."
            : "Save or discard the current draft first.");
    pushScreen(UI_MODE_CONFIG);
    return;
  }
  loadModeSetupDraft(mode);
  pushScreen(UI_MODE_SETUP);
}

static bool isModeSetupDraftScreen(UiScreen screen) {
  return screen == UI_MODE_SETUP || screen == UI_PROFILE_PICKER ||
         screen == UI_MODE_CONFIG || screen == UI_MODE_PARAMETERS;
}

static bool requireModeSetupDraftResolution() {
  if (!hasUnsavedModeSetupDraft()) {
    return false;
  }
  if (ui.currentScreen != UI_MODE_CONFIG) {
    pushScreen(UI_MODE_CONFIG);
  }
  return true;
}

static bool refreshTftPreflight() {
  clearTftPreflight();
  g_tftPreflight.attempted = true;
  g_tftPreflight.checkedAtMs = millis();

  if (!ui.setupDraft.valid || ui.setupDraft.mode == Mode::IDLE) {
    snprintf(g_tftPreflight.title, sizeof(g_tftPreflight.title), "%s",
             (g_settings.language == 0) ? "Режим не выбран" : "Mode is not selected");
    snprintf(g_tftPreflight.detail, sizeof(g_tftPreflight.detail), "%s",
             (g_settings.language == 0)
                 ? "Вернитесь к выбору режима и откройте подготовку заново."
                 : "Return to mode selection and open setup again.");
    return false;
  }

  // Deliberately call the very same builder as POST /api/process/preflight.
  // The JSON document is short-lived; the TFT retains only a fixed-size
  // presenter snapshot, so normal redraws do not fragment the heap.
  JsonDocument doc;
  JsonObject params = doc.to<JsonObject>();
  if (ui.setupDraft.profileId[0] != '\0') {
    params["profileId"] = ui.setupDraft.profileId;
  }
  if (!buildProcessPreflight(doc, ui.setupDraft.mode,
                             getModeString(ui.setupDraft.mode), params)) {
    snprintf(g_tftPreflight.title, sizeof(g_tftPreflight.title), "%s",
             (g_settings.language == 0) ? "Pre-flight недоступен"
                                        : "Pre-flight unavailable");
    snprintf(g_tftPreflight.detail, sizeof(g_tftPreflight.detail), "%s",
             (g_settings.language == 0)
                 ? "Backend не вернул результат проверки."
                 : "Backend did not return a check result.");
    return false;
  }

  g_tftPreflight.ready = doc["ready"] | false;
  g_tftPreflight.blockingCount = doc["blockingCount"] | 0;
  g_tftPreflight.warningCount = doc["warningCount"] | 0;
  const char *title = doc["title"] | "Pre-flight";
  const char *detail = doc["detail"] | "";
  strncpy(g_tftPreflight.title, title, sizeof(g_tftPreflight.title) - 1);
  g_tftPreflight.title[sizeof(g_tftPreflight.title) - 1] = '\0';
  strncpy(g_tftPreflight.detail, detail, sizeof(g_tftPreflight.detail) - 1);
  g_tftPreflight.detail[sizeof(g_tftPreflight.detail) - 1] = '\0';

  JsonArray items = doc["items"].as<JsonArray>();
  uint8_t index = 0;
  for (JsonObject item : items) {
    if (index >= TFT_PREFLIGHT_MAX_ITEMS) {
      break;
    }
    const char *itemTitle = item["title"] | "";
    const char *itemDetail = item["detail"] | "";
    const char *itemTone = item["tone"] | "muted";
    strncpy(g_tftPreflight.itemTitle[index], itemTitle,
            sizeof(g_tftPreflight.itemTitle[index]) - 1);
    g_tftPreflight.itemTitle[index]
        [sizeof(g_tftPreflight.itemTitle[index]) - 1] = '\0';
    strncpy(g_tftPreflight.itemDetail[index], itemDetail,
            sizeof(g_tftPreflight.itemDetail[index]) - 1);
    g_tftPreflight.itemDetail[index]
        [sizeof(g_tftPreflight.itemDetail[index]) - 1] = '\0';
    strncpy(g_tftPreflight.itemTone[index], itemTone,
            sizeof(g_tftPreflight.itemTone[index]) - 1);
    g_tftPreflight.itemTone[index]
        [sizeof(g_tftPreflight.itemTone[index]) - 1] = '\0';
    index++;
  }
  g_tftPreflight.itemCount = index;
  return true;
}

static void appendSetupSensorState(char *out, size_t outSize,
                                   const char *label, bool required,
                                   bool ready) {
  if (!required || out == nullptr || outSize == 0) {
    return;
  }
  const size_t used = strlen(out);
  if (used >= outSize - 1) {
    return;
  }
  snprintf(out + used, outSize - used, "%s%s %s", used ? " | " : "", label,
           ready ? "OK" : "--");
}

static void formatSetupRequiredSensors(const SystemState &state, Mode mode,
                                       char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  out[0] = '\0';
  const Safety::RequiredSensorsMask required =
      Safety::getRequiredSensorsForMode(mode, g_settings);
  const bool ru = (g_settings.language == 0);
  appendSetupSensorState(out, outSize, ru ? "Куб" : "Cube", required.cubeTemp,
                         state.temps.valid[TEMP_CUBE]);
  appendSetupSensorState(out, outSize, ru ? "Низ" : "Bottom",
                         required.columnBottomTemp,
                         state.temps.valid[TEMP_COLUMN_BOTTOM]);
  appendSetupSensorState(out, outSize, "TSA", required.tsaTemp,
                         state.temps.valid[TEMP_TSA]);
  appendSetupSensorState(out, outSize, ru ? "Вода" : "Water",
                         required.waterOutTemp,
                         state.temps.valid[TEMP_WATER_OUT]);
  appendSetupSensorState(out, outSize, ru ? "Давл." : "Press.",
                         required.pressure, state.pressure.ok);
  if (out[0] == '\0') {
    snprintf(out, outSize, "%s",
             ru ? "Дополнительные датчики не требуются"
                : "No additional sensors required");
  }
}

static void formatSetupParameters(Mode mode, char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  if (ui.setupDraft.profilePreview[0] != '\0' &&
      ui.setupDraft.profileCompatible) {
    strncpy(out, ui.setupDraft.profilePreview, outSize - 1);
    out[outSize - 1] = '\0';
    return;
  }
  const bool ru = (g_settings.language == 0);
  switch (mode) {
  case Mode::RECTIFICATION:
    snprintf(out, outSize, "%.1f L  |  %.0f%%  |  %u W",
             g_settings.rectParams.feedVolumeL,
             g_settings.rectParams.feedAbvPercent,
             g_settings.equipment.heaterPowerW);
    break;
  case Mode::DISTILLATION:
    snprintf(out, outSize, "%.0f%%  |  %.0f ml/h  |  %.1f C",
             distUi.powerPercent, distUi.speedMlH, distUi.endTempC);
    break;
  case Mode::MANUAL_RECT:
    snprintf(out, outSize, "%s",
             ru ? "Мощность и отбор задаются в процессе"
                : "Power and takeoff are set during the process");
    break;
  case Mode::MASHING:
    snprintf(out, outSize, "%u %s", mashProfileDefault.stepCount,
             ru ? "шаг(ов) температуры" : "temperature step(s)");
    break;
  case Mode::HOLD:
    snprintf(out, outSize, "%u %s", holdStepsCount,
             ru ? "шаг(ов) удержания" : "hold step(s)");
    break;
  case Mode::NBK:
    snprintf(out, outSize, "%s",
             ru ? "Расход и мощность — в параметрах НБК"
                : "Flow and power are in NBK parameters");
    break;
  case Mode::FERMENTATION:
    snprintf(out, outSize, "%s",
             ru ? "Цель и длительность — в параметрах брожения"
                : "Target and duration are in fermentation settings");
    break;
  default:
    snprintf(out, outSize, "%s", ru ? "Нет параметров" : "No parameters");
    break;
  }
}

// =============================================================================
// Settings Callbacks
// =============================================================================
static void saveHeaterPower(float val) {
  g_settings.equipment.heaterPowerW = static_cast<uint16_t>(
      EditableFields::normalize(EditableFields::Id::EQUIPMENT_HEATER_POWER_W,
                                val) +
      0.5f);
  NVSManager::saveSettings(g_settings);
}
static void saveColumnHeight(float val) {
  g_settings.equipment.columnHeightMm = static_cast<uint16_t>(
      EditableFields::normalize(EditableFields::Id::EQUIPMENT_COLUMN_HEIGHT_MM,
                                val) +
      0.5f);
  NVSManager::saveSettings(g_settings);
}
static void saveCubeVolume(float val) {
  g_settings.equipment.cubeVolumeL = EditableFields::normalize(
      EditableFields::Id::EQUIPMENT_CUBE_VOLUME_L, val);
  NVSManager::saveSettings(g_settings);
}
static void savePackingCoeff(float val) {
  g_settings.equipment.packingCoeff = EditableFields::normalize(
      EditableFields::Id::EQUIPMENT_PACKING_COEFFICIENT, val);
  NVSManager::saveSettings(g_settings);
}

static void saveHeadsPercent(float val) {
  g_settings.rectParams.headsPercent = EditableFields::normalize(
      EditableFields::Id::RECT_HEADS_PERCENT, val);
  normalizeRectFractions();
  NVSManager::saveSettings(g_settings);
}
static void saveBodyPercent(float val) {
  g_settings.rectParams.bodyPercent = EditableFields::normalize(
      EditableFields::Id::RECT_BODY_PERCENT, val);
  normalizeRectFractions();
  NVSManager::saveSettings(g_settings);
}
static void saveTailsPercent(float val) {
  g_settings.rectParams.tailsPercent = EditableFields::normalize(
      EditableFields::Id::RECT_TAILS_PERCENT, val);
  normalizeRectFractions();
  NVSManager::saveSettings(g_settings);
}
static void saveFeedVolume(float val) {
  g_settings.rectParams.feedVolumeL = EditableFields::normalize(
      EditableFields::Id::RECT_FEED_VOLUME_L, val);
  NVSManager::saveSettings(g_settings);
}
static void saveFeedAbv(float val) {
  g_settings.rectParams.feedAbvPercent = EditableFields::normalize(
      EditableFields::Id::RECT_FEED_ABV_PERCENT, val);
  NVSManager::saveSettings(g_settings);
}
static void saveHeadsSpeed(float val) {
  g_settings.rectParams.headsSpeedMlHKw = EditableFields::normalize(
      EditableFields::Id::RECT_HEADS_SPEED_ML_H_KW_FIELD, val);
  NVSManager::saveSettings(g_settings);
}
static void saveBodySpeed(float val) {
  g_settings.rectParams.bodySpeedMlHKw = EditableFields::normalize(
      EditableFields::Id::RECT_BODY_SPEED_ML_H_KW, val);
  NVSManager::saveSettings(g_settings);
}
static void saveStabMin(float val) {
  g_settings.rectParams.stabilizationMin = static_cast<uint16_t>(
      EditableFields::normalize(EditableFields::Id::RECT_STABILIZATION_MIN,
                                val) +
      0.5f);
  NVSManager::saveSettings(g_settings);
}
static void savePurgeMin(float val) {
  g_settings.rectParams.purgeMin = static_cast<uint16_t>(
      EditableFields::normalize(EditableFields::Id::RECT_PURGE_MIN, val) +
      0.5f);
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
  distUi.speedMlH = EditableFields::normalize(EditableFields::Id::DIST_SPEED_ML_H,
                                               val);
  PowerCommandGateway::noteFsmSetpoint(
      PowerCommandGateway::Target::PUMP,
      PowerCommandGateway::Reason::TFT_RUNTIME_SETPOINT, distUi.speedMlH);
  applyDistUiRuntime();
  persistDistUi();
}
static void saveDistHeads(float val) {
  distUi.headsVolumeMl = EditableFields::normalize(
      EditableFields::Id::DIST_HEADS_VOLUME_ML, val);
  applyDistUiRuntime();
  persistDistUi();
}
static void saveDistTarget(float val) {
  distUi.targetVolumeMl = EditableFields::normalize(
      EditableFields::Id::DIST_TARGET_VOLUME_ML, val);
  applyDistUiRuntime();
  persistDistUi();
}
static void saveDistEndTemp(float val) {
  distUi.endTempC = EditableFields::normalize(
      EditableFields::Id::DIST_END_TEMPERATURE_C, val);
  applyDistUiRuntime();
  persistDistUi();
}
static void saveDistPower(float val) {
  distUi.powerPercent = EditableFields::normalize(
      EditableFields::Id::DIST_POWER_PERCENT, val);
  PowerCommandGateway::noteFsmSetpoint(
      PowerCommandGateway::Target::HEATER,
      PowerCommandGateway::Reason::TFT_RUNTIME_SETPOINT,
      distUi.powerPercent);
  applyDistUiRuntime();
  persistDistUi();
}
static void saveDistTails(float val) {
  distUi.tailsVolumeMl = EditableFields::normalize(
      EditableFields::Id::DIST_TAILS_VOLUME_ML, val);
  persistDistUi();
}
static void saveFractionProgramHeatingTarget(float val) {
  g_settings.fractionProgram.heatingTargetTemperatureC =
      EditableFields::normalize(
          EditableFields::Id::FRACTION_PROGRAM_HEATING_TARGET_C, val);
  NVSManager::saveSettings(g_settings);
}

static void saveManualRectSpeed(float val) {
  val = EditableFields::normalize(EditableFields::Id::MANUAL_RECT_SPEED_ML_H,
                                  val);
  manualRectUi.speedMlH = val;
  if (g_state.mode == Mode::MANUAL_RECT) {
    PowerCommandGateway::setPumpSpeed(
        PowerCommandGateway::Owner::MANUAL,
        PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT, val);
  }
}
static void saveManualRectPower(float val) {
  val = EditableFields::normalize(
      EditableFields::Id::MANUAL_RECT_POWER_PERCENT, val);
  manualRectUi.powerPercent = val;
  if (g_state.mode == Mode::MANUAL_RECT) {
    PowerCommandGateway::setHeaterPower(
        PowerCommandGateway::Owner::MANUAL,
        PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
        static_cast<uint8_t>(val));
  }
}
static void saveManualRectHeadsTarget(float val) {
  manualRectUi.headsTargetMl = EditableFields::normalize(
      EditableFields::Id::MANUAL_RECT_HEADS_TARGET_ML, val);
}
static void saveManualRectBodyTarget(float val) {
  manualRectUi.bodyTargetMl = EditableFields::normalize(
      EditableFields::Id::MANUAL_RECT_BODY_TARGET_ML, val);
}
static void saveManualRectTailsTarget(float val) {
  manualRectUi.tailsTargetMl = EditableFields::normalize(
      EditableFields::Id::MANUAL_RECT_TAILS_TARGET_ML, val);
}

static void saveMashStepTemp(float val) {
  if (g_editMashStepIdx >= mashProfileDefault.stepCount)
    return;
  val = EditableFields::normalize(EditableFields::Id::MASH_STEP_TEMPERATURE_C,
                                  val);
  mashProfileDefault.steps[g_editMashStepIdx].temperature = val;
  if (g_state.mode == Mode::MASHING &&
      g_state.mashing.currentStep == g_editMashStepIdx) {
    g_state.mashing.targetTemp = val;
  }
}
static void saveMashStepDuration(float val) {
  if (g_editMashStepIdx >= mashProfileDefault.stepCount)
    return;
  val = EditableFields::normalize(EditableFields::Id::MASH_STEP_DURATION_MIN,
                                  val);
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
  val = EditableFields::normalize(EditableFields::Id::HOLD_STEP_TEMPERATURE_C,
                                  val);
  holdStepsDefault[g_editHoldStepIdx].temperature = val;
  if (g_state.mode == Mode::HOLD &&
      g_state.hold.currentStep == g_editHoldStepIdx) {
    g_state.hold.targetTemp = val;
  }
}
static void saveHoldStepDuration(float val) {
  if (g_editHoldStepIdx >= holdStepsCount)
    return;
  val = EditableFields::normalize(EditableFields::Id::HOLD_STEP_DURATION_MIN,
                                  val);
  holdStepsDefault[g_editHoldStepIdx].duration = static_cast<uint16_t>(val);
  if (g_state.mode == Mode::HOLD &&
      g_state.hold.currentStep == g_editHoldStepIdx) {
    g_state.hold.steps[g_editHoldStepIdx].duration = static_cast<uint16_t>(val);
    g_state.hold.tempInRange = false;
    g_state.hold.inRangeStartTime = 0;
  }
}

static void savePumpCal(float val) {
  g_settings.pumpCal.mlPerRevolution = EditableFields::normalize(
      EditableFields::Id::PUMP_CALIBRATION_ML_PER_REV, val);
  NVSManager::saveSettings(g_settings);
  Pump::setCalibration(g_settings.pumpCal.mlPerRevolution);
}

static void setPumpCalibrationMessage(const char *message) {
  snprintf(ui.pumpCalibrationMessage, sizeof(ui.pumpCalibrationMessage), "%s",
           message != nullptr ? message : "");
}

static void savePumpMeasuredVolume(float volumeMl) {
  const float normalized = EditableFields::normalize(
      EditableFields::Id::PUMP_CALIBRATION_MEASURED_VOLUME_ML, volumeMl);
  if (!PumpCalibrationSession::enterMeasuredVolume(normalized)) {
    setPumpCalibrationMessage(
        (g_settings.language == 0)
            ? "Не удалось рассчитать калибровку: проверьте измерение."
            : "Could not calculate calibration: check the measurement.");
    return;
  }
  setPumpCalibrationMessage(
      (g_settings.language == 0)
          ? "Результат рассчитан. Проверьте ошибку и сохраните или повторите."
          : "Result calculated. Review the error, then save or retry.");
}

static void cancelPumpCalibration(const char *message) {
  PumpCalibrationSession::cancel();
  setPumpCalibrationMessage(message);
}

static bool commitPumpCalibration() {
  if (!PumpCalibrationSession::mayCommit()) return false;
  const float candidate = EditableFields::normalize(
      EditableFields::Id::PUMP_CALIBRATION_ML_PER_REV,
      PumpCalibrationSession::getCandidateMlPerRev());
  g_settings.pumpCal.mlPerRevolution = candidate;
  if (!NVSManager::saveSettings(g_settings)) return false;
  Pump::setCalibration(candidate);
  PumpCalibrationSession::complete();
  setPumpCalibrationMessage(
      (g_settings.language == 0) ? "Калибровка сохранена в NVS."
                                 : "Calibration saved to NVS.");
  return true;
}

static void setSensorCalibrationMessage(const char *message) {
  snprintf(ui.sensorCalibrationMessage, sizeof(ui.sensorCalibrationMessage),
           "%s", message != nullptr ? message : "");
}

static float getSensorTemperatureValue(uint8_t role,
                                       const SystemState &state) {
  switch (role) {
  case TEMP_CUBE:
    return state.temps.cube;
  case TEMP_COLUMN_BOTTOM:
    return state.temps.columnBottom;
  case TEMP_COLUMN_TOP:
    return state.temps.columnTop;
  case TEMP_REFLUX:
    return state.temps.reflux;
  case TEMP_TSA:
    return state.temps.tsa;
  case TEMP_WATER_IN:
    return state.temps.waterIn;
  case TEMP_WATER_OUT:
    return state.temps.waterOut;
  default:
    return 0.0f;
  }
}

static void saveSensorTemperatureOffset(float value) {
  const uint8_t role = ui.sensorCalibrationTempRole < TEMP_COUNT
                           ? ui.sensorCalibrationTempRole
                           : TEMP_CUBE;
  const TempCalibration previous = g_settings.tempCal;
  g_settings.tempCal.offsets[role] = EditableFields::normalize(
      EditableFields::Id::SENSOR_TEMPERATURE_OFFSET_C, value);
  if (!NVSManager::saveSettings(g_settings)) {
    g_settings.tempCal = previous;
    setSensorCalibrationMessage(
        (g_settings.language == 0) ? "Не удалось сохранить поправку DS18."
                                   : "Could not save the DS18 offset.");
    return;
  }
  Sensors::applyCalibration(g_settings.tempCal);
  setSensorCalibrationMessage(
      (g_settings.language == 0) ? "Поправка DS18 сохранена в NVS."
                                 : "DS18 offset saved to NVS.");
}

static void saveSensorPressureOffset(float value) {
  const float previous = g_settings.pressureCal.zeroOffsetMmHg;
  g_settings.pressureCal.zeroOffsetMmHg = EditableFields::normalize(
      EditableFields::Id::SENSOR_PRESSURE_ZERO_OFFSET_MMHG, value);
  if (!NVSManager::saveSettings(g_settings)) {
    g_settings.pressureCal.zeroOffsetMmHg = previous;
    setSensorCalibrationMessage(
        (g_settings.language == 0) ? "Не удалось сохранить ноль давления."
                                   : "Could not save pressure zero.");
    return;
  }
  setSensorCalibrationMessage(
      (g_settings.language == 0) ? "Ноль давления сохранён в NVS."
                                 : "Pressure zero saved to NVS.");
}

static void saveSensorHydrometerOffset(float value) {
  const float previous = g_settings.hydroCal.densityOffset;
  g_settings.hydroCal.densityOffset = EditableFields::normalize(
      EditableFields::Id::SENSOR_HYDROMETER_DENSITY_OFFSET, value);
  if (!NVSManager::saveSettings(g_settings)) {
    g_settings.hydroCal.densityOffset = previous;
    setSensorCalibrationMessage(
        (g_settings.language == 0) ? "Не удалось сохранить поправку ареометра."
                                   : "Could not save hydrometer offset.");
    return;
  }
  setSensorCalibrationMessage(
      (g_settings.language == 0) ? "Поправка ареометра сохранена в NVS."
                                 : "Hydrometer offset saved to NVS.");
}

static void saveSensorHydrometerReference(float referenceDensity) {
  const float rawDensity =
      g_state.hydrometer.density - g_settings.hydroCal.densityOffset;
  saveSensorHydrometerOffset(EditableFields::normalize(
      EditableFields::Id::SENSOR_HYDROMETER_DENSITY_OFFSET,
      referenceDensity - rawDensity));
}

static void saveSensorBodyLevelThreshold(float value) {
  const float previous = g_settings.equipment.bodyLevelThresholdV;
  g_settings.equipment.bodyLevelThresholdV = EditableFields::normalize(
      EditableFields::Id::SENSOR_BODY_LEVEL_THRESHOLD_V, value);
  if (!NVSManager::saveSettings(g_settings)) {
    g_settings.equipment.bodyLevelThresholdV = previous;
    setSensorCalibrationMessage(
        (g_settings.language == 0) ? "Не удалось сохранить порог уровня."
                                   : "Could not save level threshold.");
    return;
  }
  setSensorCalibrationMessage(
      (g_settings.language == 0) ? "Порог уровня сохранён в NVS."
                                 : "Level threshold saved to NVS.");
}

static void saveSensorLeakThreshold(float value) {
  const float previous = g_settings.equipment.leakThresholdV;
  g_settings.equipment.leakThresholdV = EditableFields::normalize(
      EditableFields::Id::SENSOR_LEAK_THRESHOLD_V, value);
  if (!NVSManager::saveSettings(g_settings)) {
    g_settings.equipment.leakThresholdV = previous;
    setSensorCalibrationMessage(
        (g_settings.language == 0) ? "Не удалось сохранить порог протечки."
                                   : "Could not save leak threshold.");
    return;
  }
  setSensorCalibrationMessage(
      (g_settings.language == 0) ? "Порог протечки сохранён в NVS."
                                 : "Leak threshold saved to NVS.");
}

static void scanSensorCalibrationDs18() {
  memset(ui.sensorCalibrationAddresses, 0,
         sizeof(ui.sensorCalibrationAddresses));
  ui.sensorCalibrationScanCount =
      Sensors::scanDS18B20(ui.sensorCalibrationAddresses);
  ui.sensorCalibrationScanIndex = 0;
  snprintf(ui.sensorCalibrationMessage, sizeof(ui.sensorCalibrationMessage),
           (g_settings.language == 0) ? "Сканирование: найдено DS18B20: %u."
                                      : "Scan complete: DS18B20 found: %u.",
           ui.sensorCalibrationScanCount);
}

static bool saveSensorTemperatureMap() {
  const uint8_t role = ui.sensorCalibrationTempRole < TEMP_COUNT
                           ? ui.sensorCalibrationTempRole
                           : TEMP_CUBE;
  const TempCalibration previous = g_settings.tempCal;
  if (ui.sensorCalibrationScanIndex >= ui.sensorCalibrationScanCount ||
      !SensorCalibrationLogic::assignAddress(
          g_settings.tempCal.addresses, role,
          ui.sensorCalibrationAddresses[ui.sensorCalibrationScanIndex])) {
    setSensorCalibrationMessage(
        (g_settings.language == 0) ? "Сначала выберите найденный адрес DS18."
                                   : "Select a discovered DS18 address first.");
    return false;
  }
  if (!NVSManager::saveSettings(g_settings)) {
    g_settings.tempCal = previous;
    setSensorCalibrationMessage(
        (g_settings.language == 0) ? "Не удалось сохранить назначение DS18."
                                   : "Could not save DS18 assignment.");
    return false;
  }
  Sensors::applyCalibration(g_settings.tempCal);
  Sensors::refreshTemperatureInventory();
  setSensorCalibrationMessage(
      (g_settings.language == 0) ? "Адрес DS18 назначен и сохранён в NVS."
                                 : "DS18 address assigned and saved to NVS.");
  return true;
}

static bool clearSensorTemperatureMap() {
  const uint8_t role = ui.sensorCalibrationTempRole < TEMP_COUNT
                           ? ui.sensorCalibrationTempRole
                           : TEMP_CUBE;
  const TempCalibration previous = g_settings.tempCal;
  if (!SensorCalibrationLogic::clearAddress(g_settings.tempCal.addresses,
                                            role)) {
    return false;
  }
  if (!NVSManager::saveSettings(g_settings)) {
    g_settings.tempCal = previous;
    setSensorCalibrationMessage(
        (g_settings.language == 0) ? "Не удалось включить автоназначение."
                                   : "Could not enable automatic mapping.");
    return false;
  }
  Sensors::applyCalibration(g_settings.tempCal);
  Sensors::refreshTemperatureInventory();
  setSensorCalibrationMessage(
      (g_settings.language == 0) ? "Для роли включено автоназначение DS18."
                                 : "Automatic DS18 mapping enabled for role.");
  return true;
}

static void zeroSensorPressureAtCurrentValue(const SystemState &state) {
  if (!state.pressure.ok) {
    setSensorCalibrationMessage(
        (g_settings.language == 0) ? "Нет валидного сигнала давления."
                                   : "Pressure signal is not valid.");
    return;
  }
  saveSensorPressureOffset(g_settings.pressureCal.zeroOffsetMmHg +
                           state.pressure.cube);
}
static void saveManualHeater(float val) {
  PowerCommandGateway::setHeaterPower(
      PowerCommandGateway::Owner::MANUAL,
      PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
      static_cast<uint8_t>(EditableFields::normalize(
          EditableFields::Id::MANUAL_HEATER_POWER_PERCENT, val)));
}
static void saveManualPump(float val) {
  val = EditableFields::normalize(EditableFields::Id::MANUAL_PUMP_SPEED_ML_H,
                                  val);
  PowerCommandGateway::setPumpSpeed(
      PowerCommandGateway::Owner::MANUAL,
      PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT, val);
}

static void saveNbkPower(float val) {
  g_settings.nbk.powerW =
      EditableFields::normalize(EditableFields::Id::NBK_POWER_W, val);
  NVSManager::saveSettings(g_settings);
}

static void saveNbkPumpSpeed(float val) {
  g_settings.nbk.pumpSpeedMlH =
      EditableFields::normalize(EditableFields::Id::NBK_PUMP_SPEED_ML_H, val);
  NVSManager::saveSettings(g_settings);
}

static void saveNbkBottomTemp(float val) {
  g_settings.nbk.columnBottomTempThresholdC =
      EditableFields::normalize(EditableFields::Id::NBK_BOTTOM_TEMP_C, val);
  NVSManager::saveSettings(g_settings);
}

static void saveNbkTopTargetTemp(float val) {
  g_settings.nbk.columnTopTargetTempC = EditableFields::normalize(
      EditableFields::Id::NBK_TOP_TARGET_TEMP_C, val);
  NVSManager::saveSettings(g_settings);
}

static void saveNbkTargetVolume(float val) {
  g_settings.nbk.targetVolumeMl =
      EditableFields::normalize(EditableFields::Id::NBK_TARGET_VOLUME_ML, val);
  NVSManager::saveSettings(g_settings);
}

static void saveFermentationTargetTemp(float val) {
  g_settings.fermentation.targetTempC =
      EditableFields::normalize(EditableFields::Id::FERM_TARGET_TEMP_C, val);
  NVSManager::saveSettings(g_settings);
}

static void saveFermentationHysteresis(float val) {
  g_settings.fermentation.hysteresisC =
      EditableFields::normalize(EditableFields::Id::FERM_HYSTERESIS_C, val);
  NVSManager::saveSettings(g_settings);
}

static void saveFermentationCoolingMinOn(float val) {
  g_settings.fermentation.coolingMinOnSec = static_cast<uint16_t>(
      EditableFields::normalize(EditableFields::Id::FERM_COOLING_MIN_ON_SEC,
                                val));
  NVSManager::saveSettings(g_settings);
}

static void saveFermentationCoolingMinOff(float val) {
  g_settings.fermentation.coolingMinOffSec = static_cast<uint16_t>(
      EditableFields::normalize(EditableFields::Id::FERM_COOLING_MIN_OFF_SEC,
                                val));
  NVSManager::saveSettings(g_settings);
}

static void saveFermentationDuration(float val) {
  g_settings.fermentation.durationHours = static_cast<uint16_t>(
      EditableFields::normalize(EditableFields::Id::FERM_DURATION_HOURS,
                                val));
  NVSManager::saveSettings(g_settings);
}

static void normalizeCoolingPwmSettings() {
  if (g_settings.equipment.coolingPwmMinDuty >
      g_settings.equipment.coolingPwmMaxDuty) {
    g_settings.equipment.coolingPwmMinDuty =
        g_settings.equipment.coolingPwmMaxDuty;
  }
  g_settings.equipment.coolingPwmStartupDuty = constrain(
      g_settings.equipment.coolingPwmStartupDuty,
      g_settings.equipment.coolingPwmMinDuty,
      g_settings.equipment.coolingPwmMaxDuty);
}

static void saveStirrerDefaultSpeed(float val) {
  g_settings.stirrer.defaultSpeedPercent = static_cast<uint8_t>(
      EditableFields::normalize(EditableFields::Id::STIRRER_DEFAULT_SPEED_PERCENT,
                                val));
  NVSManager::saveSettings(g_settings);
}

static void saveCoolingPwmMinDuty(float val) {
  g_settings.equipment.coolingPwmMinDuty = static_cast<uint8_t>(
      EditableFields::normalize(EditableFields::Id::COOLING_PWM_MIN_DUTY,
                                val));
  normalizeCoolingPwmSettings();
  NVSManager::saveSettings(g_settings);
}

static void saveCoolingPwmMaxDuty(float val) {
  g_settings.equipment.coolingPwmMaxDuty = static_cast<uint8_t>(
      EditableFields::normalize(EditableFields::Id::COOLING_PWM_MAX_DUTY,
                                val));
  normalizeCoolingPwmSettings();
  NVSManager::saveSettings(g_settings);
}

static void saveCoolingPwmStartupDuty(float val) {
  g_settings.equipment.coolingPwmStartupDuty = static_cast<uint8_t>(
      EditableFields::normalize(EditableFields::Id::COOLING_PWM_STARTUP_DUTY,
                                val));
  normalizeCoolingPwmSettings();
  NVSManager::saveSettings(g_settings);
}

static void setFractionProgramEnabled(bool enabled) {
  g_settings.fractionProgram.enabled = enabled;
  NVSManager::saveSettings(g_settings);
}

static uint8_t getModeParametersPageCount(Mode mode) {
  switch (mode) {
  case Mode::NBK:
  case Mode::FERMENTATION:
    return 2;
  default:
    return 1;
  }
}

static bool getModeParameterField(Mode mode, uint8_t page, uint8_t slot,
                                  EditableFields::Id &field) {
  using Id = EditableFields::Id;
  switch (mode) {
  case Mode::NBK:
    if (page == 0) {
      static constexpr Id kFields[] = {Id::NBK_POWER_W,
                                       Id::NBK_PUMP_SPEED_ML_H,
                                       Id::NBK_BOTTOM_TEMP_C,
                                       Id::NBK_TOP_TARGET_TEMP_C};
      if (slot < sizeof(kFields) / sizeof(kFields[0])) {
        field = kFields[slot];
        return true;
      }
    } else if (page == 1 && slot == 0) {
      field = Id::NBK_TARGET_VOLUME_ML;
      return true;
    }
    return false;
  case Mode::FERMENTATION:
    if (page == 0) {
      static constexpr Id kFields[] = {
          Id::FERM_TARGET_TEMP_C, Id::FERM_HYSTERESIS_C,
          Id::FERM_COOLING_MIN_ON_SEC, Id::FERM_COOLING_MIN_OFF_SEC};
      if (slot < sizeof(kFields) / sizeof(kFields[0])) {
        field = kFields[slot];
        return true;
      }
    } else if (page == 1 && slot == 0) {
      field = Id::FERM_DURATION_HOURS;
      return true;
    }
    return false;
  case Mode::MASHING:
    if (ui.modeParametersStep >= mashProfileDefault.stepCount || slot > 1) {
      return false;
    }
    field = slot == 0 ? Id::MASH_STEP_TEMPERATURE_C
                      : Id::MASH_STEP_DURATION_MIN;
    return true;
  case Mode::HOLD:
    if (ui.modeParametersStep >= holdStepsCount || slot > 1) {
      return false;
    }
    field = slot == 0 ? Id::HOLD_STEP_TEMPERATURE_C
                      : Id::HOLD_STEP_DURATION_MIN;
    return true;
  case Mode::IDLE:
    if (page == 0) {
      static constexpr Id kFields[] = {
          Id::STIRRER_DEFAULT_SPEED_PERCENT, Id::COOLING_PWM_MIN_DUTY,
          Id::COOLING_PWM_MAX_DUTY, Id::COOLING_PWM_STARTUP_DUTY};
      if (slot < sizeof(kFields) / sizeof(kFields[0])) {
        field = kFields[slot];
        return true;
      }
    }
    return false;
  default:
    return false;
  }
}

static float getModeParameterFieldValue(EditableFields::Id field) {
  using Id = EditableFields::Id;
  switch (field) {
  case Id::NBK_POWER_W:
    return g_settings.nbk.powerW;
  case Id::NBK_PUMP_SPEED_ML_H:
    return g_settings.nbk.pumpSpeedMlH;
  case Id::NBK_BOTTOM_TEMP_C:
    return g_settings.nbk.columnBottomTempThresholdC;
  case Id::NBK_TOP_TARGET_TEMP_C:
    return g_settings.nbk.columnTopTargetTempC;
  case Id::NBK_TARGET_VOLUME_ML:
    return g_settings.nbk.targetVolumeMl;
  case Id::FERM_TARGET_TEMP_C:
    return g_settings.fermentation.targetTempC;
  case Id::FERM_HYSTERESIS_C:
    return g_settings.fermentation.hysteresisC;
  case Id::FERM_COOLING_MIN_ON_SEC:
    return g_settings.fermentation.coolingMinOnSec;
  case Id::FERM_COOLING_MIN_OFF_SEC:
    return g_settings.fermentation.coolingMinOffSec;
  case Id::FERM_DURATION_HOURS:
    return g_settings.fermentation.durationHours;
  case Id::STIRRER_DEFAULT_SPEED_PERCENT:
    return g_settings.stirrer.defaultSpeedPercent;
  case Id::COOLING_PWM_MIN_DUTY:
    return g_settings.equipment.coolingPwmMinDuty;
  case Id::COOLING_PWM_MAX_DUTY:
    return g_settings.equipment.coolingPwmMaxDuty;
  case Id::COOLING_PWM_STARTUP_DUTY:
    return g_settings.equipment.coolingPwmStartupDuty;
  case Id::MASH_STEP_TEMPERATURE_C:
    return ui.modeParametersStep < mashProfileDefault.stepCount
               ? mashProfileDefault.steps[ui.modeParametersStep].temperature
               : 0.0f;
  case Id::MASH_STEP_DURATION_MIN:
    return ui.modeParametersStep < mashProfileDefault.stepCount
               ? mashProfileDefault.steps[ui.modeParametersStep].duration
               : 0.0f;
  case Id::HOLD_STEP_TEMPERATURE_C:
    return ui.modeParametersStep < holdStepsCount
               ? holdStepsDefault[ui.modeParametersStep].temperature
               : 0.0f;
  case Id::HOLD_STEP_DURATION_MIN:
    return ui.modeParametersStep < holdStepsCount
               ? holdStepsDefault[ui.modeParametersStep].duration
               : 0.0f;
  default:
    return 0.0f;
  }
}

static ValueSaveCallback getModeParameterSaveCallback(EditableFields::Id field) {
  using Id = EditableFields::Id;
  switch (field) {
  case Id::NBK_POWER_W:
    return saveNbkPower;
  case Id::NBK_PUMP_SPEED_ML_H:
    return saveNbkPumpSpeed;
  case Id::NBK_BOTTOM_TEMP_C:
    return saveNbkBottomTemp;
  case Id::NBK_TOP_TARGET_TEMP_C:
    return saveNbkTopTargetTemp;
  case Id::NBK_TARGET_VOLUME_ML:
    return saveNbkTargetVolume;
  case Id::FERM_TARGET_TEMP_C:
    return saveFermentationTargetTemp;
  case Id::FERM_HYSTERESIS_C:
    return saveFermentationHysteresis;
  case Id::FERM_COOLING_MIN_ON_SEC:
    return saveFermentationCoolingMinOn;
  case Id::FERM_COOLING_MIN_OFF_SEC:
    return saveFermentationCoolingMinOff;
  case Id::FERM_DURATION_HOURS:
    return saveFermentationDuration;
  case Id::STIRRER_DEFAULT_SPEED_PERCENT:
    return saveStirrerDefaultSpeed;
  case Id::COOLING_PWM_MIN_DUTY:
    return saveCoolingPwmMinDuty;
  case Id::COOLING_PWM_MAX_DUTY:
    return saveCoolingPwmMaxDuty;
  case Id::COOLING_PWM_STARTUP_DUTY:
    return saveCoolingPwmStartupDuty;
  case Id::MASH_STEP_TEMPERATURE_C:
    g_editMashStepIdx = ui.modeParametersStep;
    return saveMashStepTemp;
  case Id::MASH_STEP_DURATION_MIN:
    g_editMashStepIdx = ui.modeParametersStep;
    return saveMashStepDuration;
  case Id::HOLD_STEP_TEMPERATURE_C:
    g_editHoldStepIdx = ui.modeParametersStep;
    return saveHoldStepTemp;
  case Id::HOLD_STEP_DURATION_MIN:
    g_editHoldStepIdx = ui.modeParametersStep;
    return saveHoldStepDuration;
  default:
    return nullptr;
  }
}

static void openModeParameters(Mode mode) {
  if (mode == Mode::RECTIFICATION) {
    rectParamsPage = 0;
    pushScreen(UI_RECT_PARAMS);
    return;
  }
  if (mode == Mode::DISTILLATION) {
    distParamsPage = 0;
    pushScreen(UI_DIST_PARAMS);
    return;
  }
  if (mode == Mode::MANUAL_RECT) {
    pushScreen(UI_MANUAL);
    return;
  }
  ui.modeParametersMode = mode;
  ui.modeParametersPage = 0;
  ui.modeParametersStep = 0;
  pushScreen(UI_MODE_PARAMETERS);
}

static bool handleNavigationTap(int16_t tx, int16_t ty,
                                const SystemState &state) {
  // A fullscreen operator overlay is modal: consume every touch before the
  // normal Back/footer navigation gets a chance to act on the obscured screen.
  if (TftOperatorOverlayLogic::isActive(ui.operatorOverlay)) {
    return handleOperatorOverlayTap(tx, ty, state);
  }

  // Value edit uses the full bottom area for the Save action.
  // Do not let tab navigation intercept taps there.
  if (ui.currentScreen == UI_VALUE_EDIT) {
    return false;
  }

  // Кнопка НАЗАД (теперь в верхнем правом
  // углу на под-экранах)
  const bool isRoot = isRootNavigationScreen(ui.currentScreen);

  if (!isRoot && hit(tx, ty, TFT_WIDTH - 110, 0, 110, 50)) {
    if (isModeSetupDraftScreen(ui.currentScreen) &&
        requireModeSetupDraftResolution()) {
      return true;
    }
    if (ui.currentScreen == UI_SERVICE_TEST) {
      const ServiceTestSession::Snapshot session =
          ServiceTestSession::getSnapshot();
      if (session.phase != ServiceTestSession::Phase::CLOSED) {
        if (session.phase == ServiceTestSession::Phase::SAFE_PAUSED ||
            session.phase == ServiceTestSession::Phase::TEST_ACTIVE) {
          stopServiceTestOutputs(
              (g_settings.language == 0)
                  ? "Сначала подтвердите остановку и возврат процесса."
                  : "Confirm stop and process restore first.");
        } else {
          setServiceTestMessage(
              (g_settings.language == 0)
                  ? "Сервисная сессия требует завершения на этом экране."
                  : "Finish the service session on this screen first.");
        }
        return true;
      }
      stopServiceTestOutputs(
          (g_settings.language == 0) ? "Сервисный тест остановлен при выходе."
                                     : "Service test stopped on exit.");
    }
    if (ui.currentScreen == UI_PUMP_CALIBRATION) {
      cancelPumpCalibration(
          (g_settings.language == 0)
              ? "Калибровка отменена; прежнее значение сохранено."
              : "Calibration cancelled; the previous value is retained.");
    }
    popScreen();
    return true;
  }

  // New Home-first navigation footer. Settings are entered from the Home
  // workspace; the footer deliberately keeps only the three high-frequency
  // destinations instead of the old four permanent tabs.
  if (ty >= (TFT_HEIGHT - UI_FOOTER_H)) {
    if (state.mode == Mode::IDLE &&
        isModeSetupDraftScreen(ui.currentScreen) &&
        requireModeSetupDraftResolution()) {
      return true;
    }
    if (ui.currentScreen == UI_SERVICE_TEST) {
      const ServiceTestSession::Snapshot session =
          ServiceTestSession::getSnapshot();
      if (session.phase != ServiceTestSession::Phase::CLOSED) {
        if (session.phase == ServiceTestSession::Phase::SAFE_PAUSED ||
            session.phase == ServiceTestSession::Phase::TEST_ACTIVE) {
          stopServiceTestOutputs(
              (g_settings.language == 0)
                  ? "Сначала подтвердите остановку и возврат процесса."
                  : "Confirm stop and process restore first.");
        } else {
          setServiceTestMessage(
              (g_settings.language == 0)
                  ? "Сервисная сессия требует завершения на этом экране."
                  : "Finish the service session on this screen first.");
        }
        return true;
      }
      stopServiceTestOutputs(
          (g_settings.language == 0) ? "Сервисный тест остановлен при выходе."
                                     : "Service test stopped on exit.");
    }
    if (ui.currentScreen == UI_PUMP_CALIBRATION) {
      cancelPumpCalibration(
          (g_settings.language == 0)
              ? "Калибровка отменена; прежнее значение сохранено."
              : "Calibration cancelled; the previous value is retained.");
    }
    const int action = tx / (TFT_WIDTH / 3);
    if (action == 0) {
      switchRoot(isModeRunning(state) ? UI_MODE_MONITOR : UI_DASHBOARD);
    } else if (action == 1) {
      switchRoot(isModeRunning(state) ? UI_CONTROL : UI_MODE_PICKER);
    } else if (action == 2) {
      switchRoot(UI_SERVICE);
    }
    return true;
  }

  return false;
}

static bool handleModeMonitorTap(int16_t tx, int16_t ty,
                                 const SystemState &state) {
  const int16_t y0 = ROOT_PANEL_Y;
  const int16_t hTile = 48;
  const int16_t g = TFT_BUTTON_GAP;
  const int16_t w3 = (ROOT_FRAME_W - g * 2) / 3;
  const int16_t x1 = ROOT_FRAME_X;
  const int16_t x2 = x1 + w3 + g;
  const int16_t x3 = x2 + w3 + g;
  const int16_t y1 = y0 + hTile + g;
  const int16_t y2 = y1 + hTile + g;

  if (!isModeMonitorEditAllowed(state.mode)) {
    return false;
  }

  if (state.mode == Mode::DISTILLATION) {
    const int16_t g2 = TFT_BUTTON_GAP;
    const int16_t w2 = (ROOT_FRAME_W - g2) / 2;
    const int16_t x2d = x1 + w2 + g2;
    const int16_t y1d = y0 + 74 + g2;
    if (hit(tx, ty, x2d, y0, w2, 74)) {
      openValueEdit(EditableFields::Id::DIST_POWER_PERCENT,
                    distUi.powerPercent, saveDistPower, true);
      return true;
    }
    if (hit(tx, ty, x2d, y1d, w2, 74)) {
      openValueEdit(EditableFields::Id::DIST_END_TEMPERATURE_C,
                    distUi.endTempC, saveDistEndTemp, true);
      return true;
    }
    return false;
  }

  if (state.mode == Mode::MANUAL_RECT) {
    const int16_t leftX = ROOT_LEFT_X;
    const int16_t leftW = ROOT_LEFT_W;
    const int16_t rowGap = ROOT_GRID_ROW_GAP;
    const int16_t rowH = (ROOT_PANEL_H - rowGap * 4) / 5;
    for (uint8_t i = 0; i < 5; i++) {
      const int16_t ry = y0 + i * (rowH + rowGap);
      if (!hit(tx, ty, leftX, ry, leftW, rowH))
        continue;
      switch (i) {
      case 0:
        openValueEdit(EditableFields::Id::MANUAL_RECT_SPEED_ML_H,
                      manualRectUi.speedMlH, saveManualRectSpeed, true);
        return true;
      case 1:
        openValueEdit(EditableFields::Id::MANUAL_RECT_POWER_PERCENT,
                      manualRectUi.powerPercent, saveManualRectPower, true);
        return true;
      case 2:
        openValueEdit(EditableFields::Id::MANUAL_RECT_HEADS_TARGET_ML,
                      manualRectUi.headsTargetMl, saveManualRectHeadsTarget,
                      true);
        return true;
      case 3:
        openValueEdit(EditableFields::Id::MANUAL_RECT_BODY_TARGET_ML,
                      manualRectUi.bodyTargetMl, saveManualRectBodyTarget,
                      true);
        return true;
      case 4:
        openValueEdit(EditableFields::Id::MANUAL_RECT_TAILS_TARGET_ML,
                      manualRectUi.tailsTargetMl, saveManualRectTailsTarget,
                      true);
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
    const int16_t listX = ROOT_FRAME_X;
    const int16_t listW = ROOT_FRAME_W;
    const int16_t rowGap = ROOT_GRID_ROW_GAP;
    const int16_t listH = ROOT_PANEL_H;
    int16_t rowH = (listH - (steps - 1) * rowGap) / steps;
    if (rowH < 28)
      rowH = 28;
    for (uint8_t i = 0; i < steps; i++) {
      const int16_t ry = y0 + i * (rowH + rowGap);
      if (!hit(tx, ty, listX, ry, listW, rowH))
        continue;
      g_editMashStepIdx = i;
      if (tx < (listX + listW / 2)) {
        openValueEdit(EditableFields::Id::MASH_STEP_TEMPERATURE_C,
                      mashProfileDefault.steps[i].temperature,
                      saveMashStepTemp, true);
      } else {
        openValueEdit(EditableFields::Id::MASH_STEP_DURATION_MIN,
                      mashProfileDefault.steps[i].duration,
                      saveMashStepDuration, true);
      }
      return true;
    }
    return false;
  }

  if (state.mode == Mode::HOLD) {
    const uint8_t steps = holdStepsCount;
    if (steps == 0)
      return false;
    const int16_t listX = ROOT_FRAME_X;
    const int16_t listW = ROOT_FRAME_W;
    const int16_t rowGap = ROOT_GRID_ROW_GAP;
    const int16_t listH = ROOT_PANEL_H;
    int16_t rowH = (listH - (steps - 1) * rowGap) / steps;
    if (rowH < 32)
      rowH = 32;
    for (uint8_t i = 0; i < steps; i++) {
      const int16_t ry = y0 + i * (rowH + rowGap);
      if (!hit(tx, ty, listX, ry, listW, rowH))
        continue;
      g_editHoldStepIdx = i;
      if (tx < (listX + listW / 2)) {
        openValueEdit(EditableFields::Id::HOLD_STEP_TEMPERATURE_C,
                      holdStepsDefault[i].temperature, saveHoldStepTemp,
                      true);
      } else {
        openValueEdit(EditableFields::Id::HOLD_STEP_DURATION_MIN,
                      holdStepsDefault[i].duration, saveHoldStepDuration,
                      true);
      }
      return true;
    }
    return false;
  }

  return false;
}

static bool handleDashboardScreenTap(int16_t tx, int16_t ty,
                                     const SystemState &) {
  if (hit(tx, ty, HOME_SAFETY_X, HOME_SAFETY_Y, HOME_SAFETY_W,
          HOME_SAFETY_H)) {
    ui.safetyActionMessage[0] = '\0';
    pushScreen(UI_SAFETY_VIEW);
    return true;
  }
  if (hit(tx, ty, HOME_TILE_X1, HOME_TILE_Y1, HOME_TILE_W, HOME_TILE_H)) {
    switchRoot(UI_MODE_PICKER);
    return true;
  }
  if (hit(tx, ty, HOME_TILE_X2, HOME_TILE_Y1, HOME_TILE_W, HOME_TILE_H)) {
    switchRoot(UI_SETTINGS);
    return true;
  }
  if (hit(tx, ty, HOME_TILE_X1, HOME_TILE_Y2, HOME_TILE_W, HOME_TILE_H)) {
    pushScreen(UI_ALL_TEMPS);
    return true;
  }
  if (hit(tx, ty, HOME_TILE_X2, HOME_TILE_Y2, HOME_TILE_W, HOME_TILE_H)) {
    switchRoot(UI_SERVICE);
    return true;
  }
  return false;
}

static bool handleModeMonitorScreenTap(int16_t tx, int16_t ty,
                                       const SystemState &state) {
  if (!isRootWorkspaceTap(ty)) {
    return false;
  }
  if (handleModeMonitorTap(tx, ty, state)) {
    return true;
  }
  if (isRootMetricsTap(tx, ty)) {
    pushScreen(UI_ALL_TEMPS);
    return true;
  }
  if (isRootControlTap(tx, ty)) {
    switchRoot(UI_CONTROL);
    return true;
  }
  return true;
}

static bool handleControlScreenTap(int16_t tx, int16_t ty,
                                   const SystemState &state) {
  // UI_CONTROL is the active-process action panel. A new run may only begin
  // through MODE_PICKER -> MODE_SETUP -> PREFLIGHT.
  if (!TftHmiPolicyLogic::mayUseActiveControlAction(
          state.mode != Mode::IDLE,
          TftHmiPolicyLogic::ActiveControlAction::Monitor)) {
    switchRoot(UI_MODE_PICKER);
    return true;
  }

  if (hit(tx, ty, PROCESS_MENU_X1, PROCESS_MENU_Y1, PROCESS_MENU_W,
          PROCESS_MENU_H)) {
    switchRoot(UI_MODE_MONITOR);
    return true;
  }
  if (hit(tx, ty, PROCESS_MENU_X2, PROCESS_MENU_Y1, PROCESS_MENU_W,
          PROCESS_MENU_H)) {
    pushScreen(UI_PROCESS_PARAMS);
    return true;
  }
  if (hit(tx, ty, PROCESS_MENU_X3, PROCESS_MENU_Y1, PROCESS_MENU_W,
          PROCESS_MENU_H)) {
    ui.safetyActionMessage[0] = '\0';
    pushScreen(UI_SAFETY_VIEW);
    return true;
  }
  if (hit(tx, ty, PROCESS_MENU_X1, PROCESS_MENU_Y2, PROCESS_MENU_W,
          PROCESS_MENU_H)) {
    openProcessEvents();
    return true;
  }
  if (hit(tx, ty, PROCESS_MENU_X2, PROCESS_MENU_Y2, PROCESS_MENU_W,
          PROCESS_MENU_H)) {
    if (state.paused) {
      if (ServiceTestSession::blocksProcessResume()) {
        snprintf(ui.safetyActionMessage, sizeof(ui.safetyActionMessage), "%s",
                 (g_settings.language == 0)
                     ? "Возврат процесса выполняется только через сервисную сессию."
                     : "Process restore is available only through the service session.");
        return true;
      }
      PowerCommandGateway::noteFsmSetpoint(
          PowerCommandGateway::Target::ALL_OUTPUTS,
          PowerCommandGateway::Reason::TFT_RUNTIME_SETPOINT, 1.0f);
      FSM::resume(g_state);
    } else {
      PowerCommandGateway::noteFsmSafeStop(
          PowerCommandGateway::Reason::TFT_PROCESS_PAUSE);
      FSM::pause(g_state);
    }
    return true;
  }
  if (hit(tx, ty, PROCESS_MENU_X3, PROCESS_MENU_Y2, PROCESS_MENU_W,
          PROCESS_MENU_H)) {
    openOperatorOverlay(TftOperatorOverlayLogic::Action::ProcessStop);
    return true;
  }
  return false;
}

static bool handleSafetyViewScreenTap(int16_t tx, int16_t ty,
                                      const SystemState &state) {
  const int16_t actionY = TFT_HEIGHT - UI_FOOTER_H - 36;
  const int16_t actionW = (TFT_WIDTH - 23) / 2;
  char resetReason[128] = "";
  const bool resetAvailable =
      state.currentAlarm.type != AlarmType::NONE &&
      Safety::canResetNow(state, g_settings, resetReason, sizeof(resetReason));
  const TftHmiPolicyLogic::PostAlarmAction safeAction =
      TftHmiPolicyLogic::selectPostAlarmAction(
          state.currentAlarm.type != AlarmType::NONE,
          state.currentAlarm.acknowledged, resetAvailable);
  if (hit(tx, ty, 10, actionY, actionW, 32)) {
    if (safeAction == TftHmiPolicyLogic::PostAlarmAction::Acknowledge) {
      openOperatorOverlay(TftOperatorOverlayLogic::Action::AlarmAcknowledge);
    }
    return true;
  }
  if (hit(tx, ty, 13 + actionW, actionY, actionW, 32)) {
    if (safeAction != TftHmiPolicyLogic::PostAlarmAction::Reset) {
      snprintf(ui.safetyActionMessage, sizeof(ui.safetyActionMessage), "%s",
               resetReason[0] != '\0'
                   ? resetReason
                   : ((g_settings.language == 0)
                          ? "Сначала подтвердите тревогу; сброс пока запрещён."
                          : "Acknowledge the alarm first; reset is blocked."));
      return true;
    }
    char reason[128] = "";
    if (Safety::reset(g_state, g_settings, reason, sizeof(reason))) {
      snprintf(ui.safetyActionMessage, sizeof(ui.safetyActionMessage), "%s",
               (g_settings.language == 0) ? "Safety-состояние сброшено."
                                          : "Safety state has been reset.");
    } else {
      snprintf(ui.safetyActionMessage, sizeof(ui.safetyActionMessage), "%s",
               reason[0] != '\0'
                   ? reason
                   : ((g_settings.language == 0)
                          ? "Сброс пока запрещён политикой safety."
                          : "Safety policy does not allow reset yet."));
    }
    return true;
  }
  return false;
}

static bool handleProcessEventsScreenTap(int16_t tx, int16_t ty,
                                         const SystemState &) {
  if (hit(tx, ty, 10, TFT_HEIGHT - UI_FOOTER_H - 36, TFT_WIDTH - 20, 32)) {
    loadTftRecentEvents();
    return true;
  }
  return false;
}

static bool handleModePickerScreenTap(int16_t tx, int16_t ty,
                                      const SystemState &state) {
  if (state.mode != Mode::IDLE) {
    switchRoot(UI_MODE_MONITOR);
    return true;
  }
  if (hit(tx, ty, CTRL_X1, CTRL_Y1, CTRL_BW, CTRL_BH)) {
    beginModeSetup(Mode::RECTIFICATION);
    return true;
  }
  if (hit(tx, ty, CTRL_X2, CTRL_Y1, CTRL_BW, CTRL_BH)) {
    beginModeSetup(Mode::DISTILLATION);
    return true;
  }
  if (hit(tx, ty, CTRL_X1, CTRL_Y2, CTRL_BW, CTRL_BH)) {
    beginModeSetup(Mode::MANUAL_RECT);
    return true;
  }
  if (hit(tx, ty, CTRL_X2, CTRL_Y2, CTRL_BW, CTRL_BH)) {
    beginModeSetup(Mode::MASHING);
    return true;
  }
  if (hit(tx, ty, CTRL_X1, CTRL_Y3, CTRL_BW, CTRL_BH)) {
    beginModeSetup(Mode::HOLD);
    return true;
  }
  if (hit(tx, ty, CTRL_X2, CTRL_Y3, CTRL_BW, CTRL_BH)) {
    beginModeSetup(Mode::NBK);
    return true;
  }
  if (hit(tx, ty, CTRL_X1, CTRL_Y4, CTRL_BW, CTRL_BH)) {
    beginModeSetup(Mode::FERMENTATION);
    return true;
  }
  return false;
}

static bool handleModeSetupScreenTap(int16_t tx, int16_t ty,
                                     const SystemState &state) {
  if (!isScreenEditable(UI_MODE_SETUP, state) || !ui.setupDraft.valid) {
    return true;
  }
  if (hit(tx, ty, 10, UI_HEADER_H + 38, TFT_WIDTH - 20, 34) &&
      ui.setupDraft.profileRelevant) {
    openSetupProfilePicker();
    return true;
  }
  if (hit(tx, ty, 10, UI_HEADER_H + 76, TFT_WIDTH - 20, 44)) {
    openModeParameters(ui.setupDraft.mode);
    return true;
  }
  if (hit(tx, ty, 10, TFT_HEIGHT - UI_FOOTER_H - 36, TFT_WIDTH - 20, 32)) {
    if (requireModeSetupDraftResolution()) {
      return true;
    }
    refreshTftPreflight();
    pushScreen(UI_PREFLIGHT);
    return true;
  }
  return false;
}

static bool handleModeConfigScreenTap(int16_t tx, int16_t ty,
                                      const SystemState &state) {
  if (!isScreenEditable(UI_MODE_CONFIG, state) || !ui.setupDraft.valid) {
    return true;
  }

  const int16_t buttonY = TFT_HEIGHT - UI_FOOTER_H - 36;
  const int16_t buttonW = (TFT_WIDTH - 23) / 2;
  if (hit(tx, ty, 10, buttonY, buttonW, 32)) {
    discardModeSetupDraft();
    popScreen();
    return true;
  }
  if (hit(tx, ty, 13 + buttonW, buttonY, buttonW, 32)) {
    char reason[144] = "";
    if (saveModeSetupDraft(reason, sizeof(reason))) {
      setModeSetupDraftMessage(
          hasUnsavedModeSetupDraft()
              ? reason
              : ((g_settings.language == 0) ? "Черновик сохранён."
                                             : "Draft saved."));
      popScreen();
    } else {
      setModeSetupDraftMessage(reason);
    }
    return true;
  }
  return false;
}

static bool handlePreflightScreenTap(int16_t tx, int16_t ty,
                                     const SystemState &state) {
  if (!isScreenEditable(UI_PREFLIGHT, state) || !ui.setupDraft.valid) {
    return true;
  }

  const int16_t buttonY = TFT_HEIGHT - UI_FOOTER_H - 36;
  const int16_t refreshW = 112;
  const int16_t pagerButtonW = 42;
  const int16_t pageX = 10 + refreshW + 6 + pagerButtonW + 4;
  const int16_t pageW = 44;
  const int16_t nextX = pageX + pageW + 4;
  const int16_t startX = nextX + pagerButtonW + 6;
  const uint8_t pageCount = getTftPreflightPageCount();
  if (hit(tx, ty, 10, buttonY, refreshW, 32)) {
    refreshTftPreflight();
    return true;
  }
  if (hit(tx, ty, 10 + refreshW + 6, buttonY, pagerButtonW, 32) &&
      ui.preflightPage > 0) {
    ui.preflightPage--;
    return true;
  }
  if (hit(tx, ty, nextX, buttonY, pagerButtonW, 32) &&
      ui.preflightPage + 1 < pageCount) {
    ui.preflightPage++;
    return true;
  }
  if (hit(tx, ty, startX, buttonY, TFT_WIDTH - startX - 10, 32)) {
    if (hasUnsavedModeSetupDraft()) {
      snprintf(g_tftPreflight.title, sizeof(g_tftPreflight.title), "%s",
               (g_settings.language == 0) ? "Черновик не сохранён"
                                          : "Draft is not saved");
      snprintf(g_tftPreflight.detail, sizeof(g_tftPreflight.detail), "%s",
               (g_settings.language == 0)
                   ? "Вернитесь в подготовку и сохраните изменения."
                   : "Return to setup and save the changes.");
      g_tftPreflight.attempted = true;
      g_tftPreflight.ready = false;
      return true;
    }
    // Refresh immediately before start: a cached green result must never
    // bypass a newly appeared alarm or missing required sensor.
    refreshTftPreflight();
    TftHmiPolicyLogic::StartRouteState startRoute;
    startRoute.idle = state.mode == Mode::IDLE;
    startRoute.draftResolved = !hasUnsavedModeSetupDraft();
    startRoute.preflightReady = g_tftPreflight.ready;
    if (!TftHmiPolicyLogic::mayStartFromTft(startRoute)) {
      return true;
    }
    startModeFromControl(ui.setupDraft.mode);
    switchRoot(UI_MODE_MONITOR);
    return true;
  }
  return false;
}

static bool handleProfilePickerScreenTap(int16_t tx, int16_t ty,
                                         const SystemState &state) {
  if (!isScreenEditable(UI_PROFILE_PICKER, state) || !ui.setupDraft.valid) {
    return true;
  }
  const int16_t firstRowY = UI_HEADER_H + 38;
  const int16_t rowH = 34;
  const int16_t rowPitch = 39;
  for (uint8_t row = 0; row < TFT_PROFILE_ROWS_PER_PAGE; ++row) {
    const uint8_t index = ui.profilePickerPage * TFT_PROFILE_ROWS_PER_PAGE + row;
    if (index >= g_tftProfileOptionCount) {
      break;
    }
    if (hit(tx, ty, 10, firstRowY + row * rowPitch, TFT_WIDTH - 20, rowH)) {
      if (selectSetupProfile(index)) {
        popScreen();
      }
      return true;
    }
  }

  const int16_t pagerY = TFT_HEIGHT - UI_FOOTER_H - 36;
  const uint8_t pageCount = getTftProfilePageCount();
  if (hit(tx, ty, 10, pagerY, 132, 32) && ui.profilePickerPage > 0) {
    ui.profilePickerPage--;
    return true;
  }
  if (hit(tx, ty, TFT_WIDTH - 142, pagerY, 132, 32) &&
      ui.profilePickerPage + 1 < pageCount) {
    ui.profilePickerPage++;
    return true;
  }
  return false;
}

static bool handleProcessParamsScreenTap(int16_t tx, int16_t ty,
                                         const SystemState &state) {
  if (state.mode == Mode::IDLE) {
    return true;
  }
  if (!isScreenEditable(UI_PROCESS_PARAMS, state)) {
    return true;
  }

  const int16_t y = UI_HEADER_H + 86;
  const int16_t x1 = 10;
  const int16_t x2 = 245;
  const int16_t w2 = 225;
  if (state.mode == Mode::DISTILLATION) {
    if (hit(tx, ty, x1, y, w2, 62)) {
      openValueEdit(EditableFields::Id::DIST_POWER_PERCENT,
                    distUi.powerPercent, saveDistPower, false, true);
      return true;
    }
    if (hit(tx, ty, x2, y, w2, 62)) {
      openValueEdit(EditableFields::Id::DIST_END_TEMPERATURE_C,
                    distUi.endTempC, saveDistEndTemp, false, true);
      return true;
    }
    return false;
  }

  if (state.mode == Mode::MANUAL_RECT) {
    if (hit(tx, ty, x1, y, w2, 36)) {
      openValueEdit(EditableFields::Id::MANUAL_RECT_SPEED_ML_H,
                    manualRectUi.speedMlH, saveManualRectSpeed, false, true);
      return true;
    }
    if (hit(tx, ty, x2, y, w2, 36)) {
      openValueEdit(EditableFields::Id::MANUAL_RECT_POWER_PERCENT,
                    manualRectUi.powerPercent, saveManualRectPower, false,
                    true);
      return true;
    }
    if (hit(tx, ty, x1, y + 40, w2, 36)) {
      openValueEdit(EditableFields::Id::MANUAL_RECT_HEADS_TARGET_ML,
                    manualRectUi.headsTargetMl, saveManualRectHeadsTarget,
                    false, true);
      return true;
    }
    if (hit(tx, ty, x2, y + 40, w2, 36)) {
      openValueEdit(EditableFields::Id::MANUAL_RECT_BODY_TARGET_ML,
                    manualRectUi.bodyTargetMl, saveManualRectBodyTarget,
                    false, true);
      return true;
    }
    if (hit(tx, ty, x1, y + 80, TFT_WIDTH - 20, 36)) {
      openValueEdit(EditableFields::Id::MANUAL_RECT_TAILS_TARGET_ML,
                    manualRectUi.tailsTargetMl, saveManualRectTailsTarget,
                    false, true);
      return true;
    }
    return false;
  }

  if (state.mode == Mode::MASHING) {
    const uint8_t step = state.mashing.currentStep;
    if (step >= mashProfileDefault.stepCount) {
      return true;
    }
    g_editMashStepIdx = step;
    if (hit(tx, ty, x1, y, w2, 62)) {
      openValueEdit(EditableFields::Id::MASH_STEP_TEMPERATURE_C,
                    mashProfileDefault.steps[step].temperature,
                    saveMashStepTemp, false, true);
      return true;
    }
    if (hit(tx, ty, x2, y, w2, 62)) {
      openValueEdit(EditableFields::Id::MASH_STEP_DURATION_MIN,
                    mashProfileDefault.steps[step].duration,
                    saveMashStepDuration, false, true);
      return true;
    }
    return false;
  }

  if (state.mode == Mode::HOLD) {
    const uint8_t step = state.hold.currentStep;
    if (step >= holdStepsCount) {
      return true;
    }
    g_editHoldStepIdx = step;
    if (hit(tx, ty, x1, y, w2, 62)) {
      openValueEdit(EditableFields::Id::HOLD_STEP_TEMPERATURE_C,
                    holdStepsDefault[step].temperature, saveHoldStepTemp,
                    false, true);
      return true;
    }
    if (hit(tx, ty, x2, y, w2, 62)) {
      openValueEdit(EditableFields::Id::HOLD_STEP_DURATION_MIN,
                    holdStepsDefault[step].duration, saveHoldStepDuration,
                    false, true);
      return true;
    }
    return false;
  }
  return false;
}

static bool handleValueEditScreenTap(int16_t tx, int16_t ty,
                                     const SystemState &) {
  if (edit.descriptor == nullptr) {
    return true;
  }
  const EditableFields::Id field = edit.descriptor->id;
  if (hit(tx, ty, VALUE_EDIT_BTN_X1, VALUE_EDIT_BTN_Y, VALUE_EDIT_BTN_W,
          VALUE_EDIT_BTN_H)) {
    edit.value = EditableFields::normalize(field,
                                           edit.value - edit.descriptor->fastStep);
    return true;
  }
  if (hit(tx, ty, VALUE_EDIT_BTN_X2, VALUE_EDIT_BTN_Y, VALUE_EDIT_BTN_W,
          VALUE_EDIT_BTN_H)) {
    edit.value = EditableFields::normalize(field,
                                           edit.value - edit.descriptor->step);
    return true;
  }
  if (hit(tx, ty, VALUE_EDIT_BTN_X3, VALUE_EDIT_BTN_Y, VALUE_EDIT_BTN_W,
          VALUE_EDIT_BTN_H)) {
    edit.value = EditableFields::normalize(field,
                                           edit.value + edit.descriptor->step);
    return true;
  }
  if (hit(tx, ty, VALUE_EDIT_BTN_X4, VALUE_EDIT_BTN_Y, VALUE_EDIT_BTN_W,
          VALUE_EDIT_BTN_H)) {
    edit.value = EditableFields::normalize(field,
                                           edit.value + edit.descriptor->fastStep);
    return true;
  }
  if (hit(tx, ty, 10, 202, TFT_WIDTH - 20, 44)) {
    if (edit.onSave)
      edit.onSave(EditableFields::normalize(field, edit.value));
    if (edit.returnToProcessParams && isModeRunning(g_state)) {
      popScreen();
    } else if (edit.returnToModeMonitor && isModeRunning(g_state)) {
      switchRoot(getMonitorRootScreenForState(g_state));
    } else {
      popScreen();
    }
    return true;
  }
  return false;
}

static bool handleSettingsScreenTap(int16_t tx, int16_t ty,
                                    const SystemState &) {
  if (hit(tx, ty, SETTINGS_CARD_X1, SETTINGS_CARD_Y1, SETTINGS_CARD_W,
          SETTINGS_CARD_H)) {
    pushScreen(UI_EQUIPMENT);
    return true;
  }
  if (hit(tx, ty, SETTINGS_CARD_X2, SETTINGS_CARD_Y1, SETTINGS_CARD_W,
          SETTINGS_CARD_H)) {
    pushScreen(UI_RECT_PARAMS);
    return true;
  }
  if (hit(tx, ty, SETTINGS_CARD_X1, SETTINGS_CARD_Y2, SETTINGS_CARD_W,
          SETTINGS_CARD_H)) {
    pushScreen(UI_DIST_PARAMS);
    return true;
  }
  if (hit(tx, ty, SETTINGS_CARD_X2, SETTINGS_CARD_Y2, SETTINGS_CARD_W,
          SETTINGS_CARD_H)) {
    pushScreen(UI_CALIBRATION);
    return true;
  }
  if (hit(tx, ty, SETTINGS_TOGGLE_X1, SETTINGS_TOGGLE_Y, SETTINGS_TOGGLE_W,
          SETTINGS_TOGGLE_H)) {
    g_settings.theme = (g_settings.theme == 0) ? 1 : 0;
    NVSManager::saveSettings(g_settings);
    requestRedraw(DisplayRedrawReason::THEME_CHANGED, true);
    return true;
  }
  if (hit(tx, ty, SETTINGS_TOGGLE_X2, SETTINGS_TOGGLE_Y, SETTINGS_TOGGLE_W,
          SETTINGS_TOGGLE_H)) {
    g_settings.soundEnabled = !g_settings.soundEnabled;
    NVSManager::saveSettings(g_settings);
    return true;
  }
  if (hit(tx, ty, SETTINGS_TOGGLE_X3, SETTINGS_TOGGLE_Y, SETTINGS_TOGGLE_W,
          SETTINGS_TOGGLE_H)) {
    g_settings.language = (g_settings.language == 0) ? 1 : 0;
    NVSManager::saveSettings(g_settings);
    requestRedraw(DisplayRedrawReason::LANGUAGE_CHANGED, true);
    return true;
  }
  if (hit(tx, ty, SETTINGS_PROFILE_X, SETTINGS_PROFILE_Y, SETTINGS_PROFILE_W,
          SETTINGS_PROFILE_H)) {
    switch (g_settings.displaySettings.refreshProfile) {
    case DisplayRefreshProfile::NORMAL:
      g_settings.displaySettings.refreshProfile = DisplayRefreshProfile::SAFE;
      break;
    case DisplayRefreshProfile::SAFE:
      g_settings.displaySettings.refreshProfile = DisplayRefreshProfile::FAST;
      break;
    case DisplayRefreshProfile::FAST:
    default:
      g_settings.displaySettings.refreshProfile = DisplayRefreshProfile::NORMAL;
      break;
    }
    NVSManager::saveSettings(g_settings);
    return true;
  }
  if (hit(tx, ty, SETTINGS_STIRRER_X, SETTINGS_STIRRER_Y, SETTINGS_STIRRER_W,
          SETTINGS_STIRRER_H)) {
    openModeParameters(Mode::IDLE);
    return true;
  }
  return false;
}

static bool handleEquipmentScreenTap(int16_t tx, int16_t ty,
                                     const SystemState &state) {
  if (!isScreenEditable(UI_EQUIPMENT, state)) {
    return true;
  }
  if (hit(tx, ty, 10, 48, 225, 78)) {
    openValueEdit(EditableFields::Id::EQUIPMENT_HEATER_POWER_W,
                  g_settings.equipment.heaterPowerW, saveHeaterPower);
    return true;
  }
  if (hit(tx, ty, 245, 48, 225, 78)) {
    openValueEdit(EditableFields::Id::EQUIPMENT_COLUMN_HEIGHT_MM,
                  g_settings.equipment.columnHeightMm, saveColumnHeight);
    return true;
  }
  if (hit(tx, ty, 10, 138, 225, 78)) {
    openValueEdit(EditableFields::Id::EQUIPMENT_CUBE_VOLUME_L,
                  g_settings.equipment.cubeVolumeL, saveCubeVolume);
    return true;
  }
  if (hit(tx, ty, 245, 138, 225, 78)) {
    openValueEdit(EditableFields::Id::EQUIPMENT_PACKING_COEFFICIENT,
                  g_settings.equipment.packingCoeff, savePackingCoeff);
    return true;
  }
  return false;
}

static bool handleRectParamsScreenTap(int16_t tx, int16_t ty,
                                      const SystemState &state) {
  if (hit(tx, ty, 10, 48, 460, 26)) {
    rectParamsPage = (rectParamsPage == 0) ? 1 : 0;
    return true;
  }

  if (!isScreenEditable(UI_RECT_PARAMS, state)) {
    return true;
  }

  if (ty < 82 || ty >= 232) {
    return false;
  }

  const int16_t rowH = 46;
  const int16_t rowGap = 6;
  const int16_t rowPitch = rowH + rowGap;
  const int16_t row = (ty - 82) / rowPitch;
  const int16_t col = (tx >= 245) ? 1 : 0;
  if (row < 0 || row > 2) {
    return false;
  }

  const int16_t rowTop = 82 + row * rowPitch;
  if (!hit(tx, ty, col == 0 ? 10 : 245, rowTop, 225, rowH)) {
    return false;
  }

  const int itemIndex = (row * 2) + col;
  if (rectParamsPage == 0) {
    switch (itemIndex) {
    case 0:
      applyRectFeedstockDefaultsAndSave(
          (g_settings.rectParams.feedstock + 1) % 8);
      return true;
    case 1:
      openValueEdit(EditableFields::Id::RECT_FEED_VOLUME_L,
                    g_settings.rectParams.feedVolumeL, saveFeedVolume);
      return true;
    case 2:
      openValueEdit(EditableFields::Id::RECT_FEED_ABV_PERCENT,
                    g_settings.rectParams.feedAbvPercent, saveFeedAbv);
      return true;
    case 3:
      openValueEdit(EditableFields::Id::RECT_HEADS_PERCENT,
                    g_settings.rectParams.headsPercent, saveHeadsPercent);
      return true;
    case 4:
      openValueEdit(EditableFields::Id::RECT_BODY_PERCENT,
                    g_settings.rectParams.bodyPercent, saveBodyPercent);
      return true;
    case 5:
      openValueEdit(EditableFields::Id::RECT_TAILS_PERCENT,
                    g_settings.rectParams.tailsPercent, saveTailsPercent);
      return true;
    default:
      return false;
    }
  }

  switch (itemIndex) {
  case 0:
    openValueEdit(EditableFields::Id::RECT_HEADS_SPEED_ML_H_KW_FIELD,
                  g_settings.rectParams.headsSpeedMlHKw, saveHeadsSpeed);
    return true;
  case 1:
    openValueEdit(EditableFields::Id::RECT_BODY_SPEED_ML_H_KW,
                  g_settings.rectParams.bodySpeedMlHKw, saveBodySpeed);
    return true;
  case 2:
    openValueEdit(EditableFields::Id::RECT_STABILIZATION_MIN,
                  g_settings.rectParams.stabilizationMin, saveStabMin);
    return true;
  case 3:
    openValueEdit(EditableFields::Id::RECT_PURGE_MIN,
                  g_settings.rectParams.purgeMin, savePurgeMin);
    return true;
  case 4:
    g_settings.rectParams.baroCorrectionEnabled =
        !g_settings.rectParams.baroCorrectionEnabled;
    NVSManager::saveSettings(g_settings);
    return true;
  default:
    return false;
  }
}

static bool handleDistParamsScreenTap(int16_t tx, int16_t ty,
                                      const SystemState &state) {
  if (hit(tx, ty, 10, 48, 460, 26)) {
    distParamsPage = (distParamsPage == 0) ? 1 : 0;
    return true;
  }
  if (!isScreenEditable(UI_DIST_PARAMS, state)) {
    return true;
  }
  if (distParamsPage == 1) {
    if (hit(tx, ty, 10, 82, 225, 62)) {
      openValueEdit(EditableFields::Id::FRACTION_PROGRAM_HEATING_TARGET_C,
                    g_settings.fractionProgram.heatingTargetTemperatureC,
                    saveFractionProgramHeatingTarget);
      return true;
    }
    if (hit(tx, ty, 245, 82, 225, 62)) {
      setFractionProgramEnabled(!g_settings.fractionProgram.enabled);
      return true;
    }
    return true;
  }
  if (hit(tx, ty, 10, 82, 225, 50)) {
    openValueEdit(EditableFields::Id::DIST_SPEED_ML_H, distUi.speedMlH,
                  saveDistSpeed);
    return true;
  }
  if (hit(tx, ty, 245, 82, 225, 50)) {
    openValueEdit(EditableFields::Id::DIST_HEADS_VOLUME_ML,
                  distUi.headsVolumeMl, saveDistHeads);
    return true;
  }
  if (hit(tx, ty, 10, 138, 225, 50)) {
    openValueEdit(EditableFields::Id::DIST_TARGET_VOLUME_ML,
                  distUi.targetVolumeMl, saveDistTarget);
    return true;
  }
  if (hit(tx, ty, 245, 138, 225, 50)) {
    openValueEdit(EditableFields::Id::DIST_END_TEMPERATURE_C,
                  distUi.endTempC, saveDistEndTemp);
    return true;
  }
  if (hit(tx, ty, 10, 194, TFT_WIDTH - 20, 32)) {
    openValueEdit(EditableFields::Id::DIST_TAILS_VOLUME_ML,
                  distUi.tailsVolumeMl, saveDistTails);
    return true;
  }
  return false;
}

static bool handleCalibrationScreenTap(int16_t tx, int16_t ty,
                                       const SystemState &state) {
  if (!isScreenEditable(UI_CALIBRATION, state)) {
    return true;
  }
  if (hit(tx, ty, 10, 52, 225, 86)) {
    ui.pumpCalibrationMessage[0] = '\0';
    pushScreen(UI_PUMP_CALIBRATION);
    return true;
  }
  if (hit(tx, ty, 245, 52, 225, 86)) {
    Display::startTouchCalibration();
    return true;
  }
  if (hit(tx, ty, 10, 148, TFT_WIDTH - 20, 64)) {
    ui.sensorCalibrationPage = 0;
    ui.sensorCalibrationTempRole = 0;
    ui.sensorCalibrationMessage[0] = '\0';
    scanSensorCalibrationDs18();
    pushScreen(UI_SENSOR_CALIBRATION);
    return true;
  }
  return false;
}

static bool handlePumpCalibrationScreenTap(int16_t tx, int16_t ty,
                                           const SystemState &state) {
  if (state.mode != Mode::IDLE) return true;
  const PumpCalibrationSession::Snapshot calibration =
      PumpCalibrationSession::getSnapshot();
  const int16_t actionY = 178;
  if (calibration.phase == PumpCalibrationSession::Phase::IDLE &&
      hit(tx, ty, 10, actionY, TFT_WIDTH - 20, 38)) {
    if (PumpCalibrationSession::beginPriming(BOARD_PUMP_CALIBRATION_SPEED_ML_H)) {
      setPumpCalibrationMessage(
          (g_settings.language == 0)
              ? "Прокачка линии запущена. Остановите, когда линия заполнена."
              : "Line priming started. Stop when the line is filled.");
    }
    return true;
  }
  if (calibration.phase == PumpCalibrationSession::Phase::PRIMING &&
      hit(tx, ty, 10, actionY, TFT_WIDTH - 20, 38)) {
    if (PumpCalibrationSession::finishPriming()) {
      setPumpCalibrationMessage(
          (g_settings.language == 0)
              ? "Линия заполнена. Установите мерную ёмкость и начните измерение."
              : "Line filled. Place a measuring vessel and start measurement.");
    }
    return true;
  }
  if (calibration.phase == PumpCalibrationSession::Phase::READY_TO_MEASURE &&
      hit(tx, ty, 10, actionY, TFT_WIDTH - 20, 38)) {
    if (PumpCalibrationSession::beginMeasurement(BOARD_PUMP_CALIBRATION_SPEED_ML_H)) {
      setPumpCalibrationMessage(
          (g_settings.language == 0)
              ? "Измерение запущено. Остановите после нужного объёма."
              : "Measurement started. Stop after the desired volume.");
    }
    return true;
  }
  if (calibration.phase == PumpCalibrationSession::Phase::MEASURING &&
      hit(tx, ty, 10, actionY, TFT_WIDTH - 20, 38)) {
    if (PumpCalibrationSession::stopMeasurement()) {
      setPumpCalibrationMessage(
          (g_settings.language == 0)
              ? "Введите фактический объём в мерной ёмкости."
              : "Enter the actual measured volume.");
    }
    return true;
  }
  if (calibration.phase == PumpCalibrationSession::Phase::READY_TO_REVIEW &&
      hit(tx, ty, 10, actionY, TFT_WIDTH - 20, 38)) {
    openValueEdit(EditableFields::Id::PUMP_CALIBRATION_MEASURED_VOLUME_ML,
                  10.0f, savePumpMeasuredVolume);
    return true;
  }
  if (calibration.phase == PumpCalibrationSession::Phase::REVIEW) {
    const int16_t buttonW = (TFT_WIDTH - 23) / 2;
    if (hit(tx, ty, 10, actionY, buttonW, 38)) {
      if (!commitPumpCalibration()) {
        setPumpCalibrationMessage(
            (g_settings.language == 0) ? "Не удалось сохранить калибровку."
                                       : "Could not save calibration.");
      }
      return true;
    }
    if (hit(tx, ty, 13 + buttonW, actionY, buttonW, 38)) {
      if (PumpCalibrationSession::retry()) {
        setPumpCalibrationMessage(
            (g_settings.language == 0)
                ? "Повторите измерение; прежняя калибровка не менялась."
                : "Repeat measurement; previous calibration is unchanged.");
      }
      return true;
    }
  }
  return false;
}

static bool handleSensorCalibrationScreenTap(int16_t tx, int16_t ty,
                                             const SystemState &state) {
  if (state.mode != Mode::IDLE) return true;

  static constexpr int16_t kTabY = 52;
  static constexpr int16_t kTabH = 28;
  static constexpr int16_t kTabW = 112;
  static constexpr int16_t kTabGap = 4;
  for (uint8_t page = 0; page < 4; ++page) {
    const int16_t x = 10 + page * (kTabW + kTabGap);
    if (hit(tx, ty, x, kTabY, kTabW, kTabH)) {
      ui.sensorCalibrationPage = page;
      return true;
    }
  }

  switch (ui.sensorCalibrationPage) {
  case 0: // DS18B20 mapping and offset
    if (hit(tx, ty, 10, 166, 100, 38)) {
      ui.sensorCalibrationTempRole =
          (ui.sensorCalibrationTempRole + TEMP_COUNT - 1) % TEMP_COUNT;
      return true;
    }
    if (hit(tx, ty, 115, 166, 100, 38)) {
      ui.sensorCalibrationTempRole =
          (ui.sensorCalibrationTempRole + 1) % TEMP_COUNT;
      return true;
    }
    if (hit(tx, ty, 220, 166, 250, 38)) {
      const uint8_t role = ui.sensorCalibrationTempRole < TEMP_COUNT
                               ? ui.sensorCalibrationTempRole
                               : TEMP_CUBE;
      openValueEdit(EditableFields::Id::SENSOR_TEMPERATURE_OFFSET_C,
                    g_settings.tempCal.offsets[role],
                    saveSensorTemperatureOffset);
      return true;
    }
    if (ui.sensorCalibrationScanCount > 0 &&
        hit(tx, ty, 10, 210, 90, 38)) {
      ui.sensorCalibrationScanIndex =
          (ui.sensorCalibrationScanIndex + ui.sensorCalibrationScanCount - 1) %
          ui.sensorCalibrationScanCount;
      return true;
    }
    if (ui.sensorCalibrationScanCount > 0 &&
        hit(tx, ty, 105, 210, 90, 38)) {
      ui.sensorCalibrationScanIndex =
          (ui.sensorCalibrationScanIndex + 1) % ui.sensorCalibrationScanCount;
      return true;
    }
    if (hit(tx, ty, 200, 210, 170, 38)) {
      saveSensorTemperatureMap();
      return true;
    }
    if (hit(tx, ty, 375, 210, 95, 38)) {
      clearSensorTemperatureMap();
      return true;
    }
    break;
  case 1: // Pressure
    if (hit(tx, ty, 10, 194, 225, 38)) {
      zeroSensorPressureAtCurrentValue(state);
      return true;
    }
    if (hit(tx, ty, 245, 194, 225, 38)) {
      openValueEdit(EditableFields::Id::SENSOR_PRESSURE_ZERO_OFFSET_MMHG,
                    g_settings.pressureCal.zeroOffsetMmHg,
                    saveSensorPressureOffset);
      return true;
    }
    break;
  case 2: // Hydrometer
    if (hit(tx, ty, 10, 194, 225, 38)) {
      if (!g_state.hydrometer.valid) {
        setSensorCalibrationMessage(
            (g_settings.language == 0) ? "Нет валидного сигнала ареометра."
                                       : "Hydrometer signal is not valid.");
        return true;
      }
      openValueEdit(EditableFields::Id::SENSOR_HYDROMETER_REFERENCE_DENSITY,
                    g_state.hydrometer.density,
                    saveSensorHydrometerReference);
      return true;
    }
    if (hit(tx, ty, 245, 194, 225, 38)) {
      openValueEdit(EditableFields::Id::SENSOR_HYDROMETER_DENSITY_OFFSET,
                    g_settings.hydroCal.densityOffset,
                    saveSensorHydrometerOffset);
      return true;
    }
    break;
  case 3: { // Level and leak thresholds
    if (hit(tx, ty, 355, 90, 115, 52)) {
      const bool previous = g_settings.equipment.bodyLevelTriggerAbove;
      g_settings.equipment.bodyLevelTriggerAbove = !previous;
      if (!NVSManager::saveSettings(g_settings)) {
        g_settings.equipment.bodyLevelTriggerAbove = previous;
        setSensorCalibrationMessage(
            (g_settings.language == 0) ? "Не удалось сохранить полярность уровня."
                                       : "Could not save level polarity.");
      } else {
        setSensorCalibrationMessage(
            (g_settings.language == 0) ? "Полярность уровня сохранена в NVS."
                                       : "Level polarity saved to NVS.");
      }
      return true;
    }
    if (hit(tx, ty, 355, 148, 115, 52)) {
      const bool previous = g_settings.equipment.leakTriggerAbove;
      g_settings.equipment.leakTriggerAbove = !previous;
      if (!NVSManager::saveSettings(g_settings)) {
        g_settings.equipment.leakTriggerAbove = previous;
        setSensorCalibrationMessage(
            (g_settings.language == 0) ? "Не удалось сохранить полярность протечки."
                                       : "Could not save leak polarity.");
      } else {
        setSensorCalibrationMessage(
            (g_settings.language == 0) ? "Полярность протечки сохранена в NVS."
                                       : "Leak polarity saved to NVS.");
      }
      return true;
    }
    if (hit(tx, ty, 10, 210, 225, 38)) {
      openValueEdit(EditableFields::Id::SENSOR_BODY_LEVEL_THRESHOLD_V,
                    g_settings.equipment.bodyLevelThresholdV,
                    saveSensorBodyLevelThreshold);
      return true;
    }
    if (hit(tx, ty, 245, 210, 225, 38)) {
      openValueEdit(EditableFields::Id::SENSOR_LEAK_THRESHOLD_V,
                    g_settings.equipment.leakThresholdV,
                    saveSensorLeakThreshold);
      return true;
    }
    break;
  }
  default:
    break;
  }
  return false;
}

static bool handleManualScreenTap(int16_t tx, int16_t ty,
                                  const SystemState &state) {
  if (!isScreenEditable(UI_MANUAL, state)) {
    return true;
  }
  if (hit(tx, ty, 10, 48, 225, 86)) {
    openValueEdit(EditableFields::Id::MANUAL_HEATER_POWER_PERCENT,
                  Heater::getPower(), saveManualHeater);
    return true;
  }
  if (hit(tx, ty, 245, 48, 225, 86)) {
    openValueEdit(EditableFields::Id::MANUAL_PUMP_SPEED_ML_H,
                  state.pump.speedMlPerHour, saveManualPump);
    return true;
  }
  if (ty >= MANUAL_VALVE_Y && ty < (MANUAL_VALVE_Y + MANUAL_VALVE_H)) {
    if (hit(tx, ty, MANUAL_VALVE_X1, MANUAL_VALVE_Y, MANUAL_VALVE_W,
            MANUAL_VALVE_H)) {
      PowerCommandGateway::setValve(
          PowerCommandGateway::Owner::MANUAL,
          PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
          PowerCommandGateway::Valve::WATER, !Valves::getWater());
      return true;
    }
    if (hit(tx, ty, MANUAL_VALVE_X2, MANUAL_VALVE_Y, MANUAL_VALVE_W,
            MANUAL_VALVE_H)) {
      PowerCommandGateway::setValve(
          PowerCommandGateway::Owner::MANUAL,
          PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
          PowerCommandGateway::Valve::HEADS, !Valves::getHeads());
      return true;
    }
    if (hit(tx, ty, MANUAL_VALVE_X3, MANUAL_VALVE_Y, MANUAL_VALVE_W,
            MANUAL_VALVE_H)) {
      PowerCommandGateway::setValve(
          PowerCommandGateway::Owner::MANUAL,
          PowerCommandGateway::Reason::TFT_MANUAL_OUTPUT,
          PowerCommandGateway::Valve::UNO, !Valves::getUno());
      return true;
    }
  }
  return false;
}

static bool handleModeParametersScreenTap(int16_t tx, int16_t ty,
                                          const SystemState &state) {
  if (!isScreenEditable(UI_MODE_PARAMETERS, state)) {
    return true;
  }

  const Mode mode = ui.modeParametersMode;
  if (hit(tx, ty, 10, 48, TFT_WIDTH - 20, 26) &&
      getModeParametersPageCount(mode) > 1) {
    ui.modeParametersPage =
        (ui.modeParametersPage + 1) % getModeParametersPageCount(mode);
    return true;
  }

  const int16_t row = ty >= 136 && ty < 186 ? 1 : 0;
  if (ty >= 80 && ty < 130 || ty >= 136 && ty < 186) {
    const int16_t col = tx >= 245 ? 1 : 0;
    const int16_t x = col == 0 ? 10 : 245;
    const int16_t y = row == 0 ? 80 : 136;
    if (!hit(tx, ty, x, y, 225, 50)) {
      return false;
    }
    EditableFields::Id field;
    const uint8_t slot = static_cast<uint8_t>(row * 2 + col);
    if (!getModeParameterField(mode, ui.modeParametersPage, slot, field)) {
      return true;
    }
    // Mashing and Hold steps belong to the selected saved profile. The TFT
    // shows them in setup but does not make an unsaved RAM-only profile edit.
    if (mode == Mode::MASHING || mode == Mode::HOLD) {
      return true;
    }
    ValueSaveCallback callback = getModeParameterSaveCallback(field);
    if (callback == nullptr) {
      return true;
    }
    openValueEdit(field, getModeParameterFieldValue(field), callback);
    return true;
  }

  if (ty < 200 || ty >= 232) {
    return false;
  }

  if (mode == Mode::MASHING || mode == Mode::HOLD) {
    const uint8_t stepCount = mode == Mode::MASHING
                                  ? mashProfileDefault.stepCount
                                  : holdStepsCount;
    if (stepCount == 0) {
      return true;
    }
    if (hit(tx, ty, 10, 200, 225, 32) && ui.modeParametersStep > 0) {
      --ui.modeParametersStep;
      return true;
    }
    if (hit(tx, ty, 245, 200, 225, 32) &&
        ui.modeParametersStep + 1 < stepCount) {
      ++ui.modeParametersStep;
      return true;
    }
    return true;
  }

  if (mode == Mode::NBK && hit(tx, ty, 10, 200, TFT_WIDTH - 20, 32)) {
    g_settings.nbk.topTempCorrectionEnabled =
        !g_settings.nbk.topTempCorrectionEnabled;
    NVSManager::saveSettings(g_settings);
    return true;
  }
  if (mode == Mode::FERMENTATION) {
    if (hit(tx, ty, 10, 200, 225, 32)) {
      g_settings.fermentation.useHeater = !g_settings.fermentation.useHeater;
      NVSManager::saveSettings(g_settings);
      return true;
    }
    if (hit(tx, ty, 245, 200, 225, 32)) {
      g_settings.fermentation.useCooling =
          !g_settings.fermentation.useCooling;
      NVSManager::saveSettings(g_settings);
      return true;
    }
    return true;
  }
  if (mode == Mode::IDLE) {
    if (hit(tx, ty, 10, 200, 225, 32)) {
      g_settings.stirrer.enabled = !g_settings.stirrer.enabled;
      if (!g_settings.stirrer.enabled) {
        g_state.stirrer.autoMode = false;
        PowerCommandGateway::stopStirrer(
            PowerCommandGateway::Owner::SERVICE,
            PowerCommandGateway::Reason::TFT_SERVICE_SETTING);
      }
      NVSManager::saveSettings(g_settings);
      return true;
    }
    if (hit(tx, ty, 245, 200, 225, 32)) {
      g_settings.equipment.coolingPwmEnabled =
          !g_settings.equipment.coolingPwmEnabled;
      NVSManager::saveSettings(g_settings);
      return true;
    }
    return true;
  }
  return false;
}

static void setServiceTestMessage(const char *message) {
  if (message == nullptr) {
    ui.serviceTestMessage[0] = '\0';
    return;
  }
  snprintf(ui.serviceTestMessage, sizeof(ui.serviceTestMessage), "%s", message);
}

static const char *getServiceTestTargetLabel(ServiceTestSession::Target target,
                                             bool ru) {
  switch (target) {
  case ServiceTestSession::Target::HEATER:
    return ru ? "ТЭН" : "heater";
  case ServiceTestSession::Target::PUMP:
    return ru ? "насос" : "pump";
  case ServiceTestSession::Target::STIRRER:
    return ru ? "мешалка" : "stirrer";
  case ServiceTestSession::Target::WATER_VALVE:
    return ru ? "клапан воды" : "water valve";
  case ServiceTestSession::Target::HEADS_VALVE:
    return ru ? "клапан голов" : "heads valve";
  case ServiceTestSession::Target::BODY_VALVE:
    return ru ? "клапан тела" : "body valve";
  case ServiceTestSession::Target::TAILS_VALVE:
    return ru ? "клапан хвостов" : "tails valve";
  case ServiceTestSession::Target::UNO_VALVE:
    return ru ? "клапан УНО" : "UNO valve";
  case ServiceTestSession::Target::COOLING_PWM:
    return ru ? "PWM охлаждения" : "cooling PWM";
  case ServiceTestSession::Target::FRACTIONATOR:
    return ru ? "фракционник" : "fractionator";
  case ServiceTestSession::Target::NONE:
  default:
    return ru ? "оборудование" : "equipment";
  }
}

static bool areServiceTestOutputsSafe() {
  const Heater::Diagnostics heater = Heater::getDiagnostics();
  return !heater.active && !Pump::isRunning() && !Stirrer::isRunning() &&
         !Valves::getWater() && !Valves::getHeads() && !Valves::getBody() &&
         !Valves::getTails() && !Valves::getUno() &&
         Valves::getStartStop() == 0 && !Valves::isServoMoving();
}

static void settleServiceTestStopping() {
  const ServiceTestSession::Snapshot session =
      ServiceTestSession::getSnapshot();
  if (session.phase != ServiceTestSession::Phase::STOPPING) return;
  if (!areServiceTestOutputsSafe()) return;
  if (ServiceTestSession::markRestoreReady()) {
    setServiceTestMessage(
        session.resumeProcess
            ? ((g_settings.language == 0)
                   ? "Тест остановлен. Подтвердите возврат процесса."
                   : "Test stopped. Confirm process restore.")
            : ((g_settings.language == 0)
                   ? "Тест остановлен. Исходный процесс останется на паузе."
                   : "Test stopped. The original process stays paused."));
  }
}

static bool serviceTestAllowed(const SystemState &state, char *reason,
                               size_t reasonSize) {
  const ServiceTestSession::Snapshot session =
      ServiceTestSession::getSnapshot();
  const bool ownsPausedProcess =
      ServiceTestSession::mayUsePausedProcess(
          static_cast<uint8_t>(state.mode), state.paused);
  if (state.mode != Mode::IDLE && !ownsPausedProcess) {
    const bool ru = g_settings.language == 0;
    const char *message = ru ? "Сначала запросите сервисную паузу процесса."
                             : "Request a service pause first.";
    if (session.phase == ServiceTestSession::Phase::REQUESTED) {
      message = ru ? "Подтвердите сервисную паузу для выбранного теста."
                   : "Confirm the service pause for the selected test.";
    } else if (session.phase == ServiceTestSession::Phase::STOPPING) {
      message = ru ? "Ожидание подтверждения безопасной остановки."
                   : "Waiting for safe-stop readback.";
    } else if (session.phase == ServiceTestSession::Phase::RESTORE_READY) {
      message = ru ? "Сначала завершите или верните сервисную сессию."
                   : "Finish or restore the service session first.";
    } else if (session.phase == ServiceTestSession::Phase::FAULTED) {
      message = ru ? "Сервис заблокирован safety до acknowledge/reset."
                   : "Service is safety-blocked until acknowledge/reset.";
    }
    if (reason != nullptr && reasonSize > 0) {
      snprintf(reason, reasonSize, "%s", message);
    }
    return false;
  }

  const EquipmentTestingLogic::BlockReason block =
      EquipmentTestingLogic::evaluateGuard(
          state.mode != Mode::IDLE && !ownsPausedProcess,
          Safety::isLatched(state),
          state.currentAlarm.type != AlarmType::NONE, state.safetyOk,
          g_settings.demoMode);
  if (block == EquipmentTestingLogic::BlockReason::NONE) {
    if (reason != nullptr && reasonSize > 0) reason[0] = '\0';
    return true;
  }

  const bool ru = g_settings.language == 0;
  const char *message = ru ? "Проверка оборудования недоступна."
                           : "Equipment testing is unavailable.";
  switch (block) {
  case EquipmentTestingLogic::BlockReason::PROCESS_ACTIVE:
    message = ru ? "Сначала завершите активный процесс."
                 : "Finish the active process first.";
    break;
  case EquipmentTestingLogic::BlockReason::SAFETY_LATCHED:
    message = ru ? "Сначала сбросьте защёлкнутую защиту."
                 : "Reset the latched safety protection first.";
    break;
  case EquipmentTestingLogic::BlockReason::ALARM_ACTIVE:
    message = ru ? "Сначала обработайте активную тревогу."
                 : "Handle the active alarm first.";
    break;
  case EquipmentTestingLogic::BlockReason::SAFETY_NOT_OK:
    message = ru ? "Safety не готов: тест блокирован."
                 : "Safety is not ready: test is blocked.";
    break;
  case EquipmentTestingLogic::BlockReason::DEMO_MODE:
    message = ru ? "В demo-режиме физические тесты запрещены."
                 : "Physical tests are disabled in demo mode.";
    break;
  case EquipmentTestingLogic::BlockReason::NONE:
    break;
  }
  if (reason != nullptr && reasonSize > 0) {
    snprintf(reason, reasonSize, "%s", message);
  }
  return false;
}

static bool requestServicePauseIfNeeded(
    const SystemState &state, ServiceTestSession::Target target) {
  if (state.mode == Mode::IDLE) return false;

  const ServiceTestSession::Snapshot session =
      ServiceTestSession::getSnapshot();
  if (session.phase != ServiceTestSession::Phase::CLOSED) {
    if (session.phase == ServiceTestSession::Phase::TEST_ACTIVE) {
      setServiceTestMessage(
          (g_settings.language == 0)
              ? "Сначала остановите активный сервисный тест."
              : "Stop the active service test first.");
      return true;
    }
    if (ServiceTestSession::mayUsePausedProcess(
            static_cast<uint8_t>(state.mode), state.paused)) {
      return false;
    }
    char reason[144] = "";
    (void)serviceTestAllowed(state, reason, sizeof(reason));
    setServiceTestMessage(reason);
    return true;
  }

  const EquipmentTestingLogic::BlockReason safetyBlock =
      EquipmentTestingLogic::evaluateGuard(
          false, Safety::isLatched(state),
          state.currentAlarm.type != AlarmType::NONE, state.safetyOk,
          g_settings.demoMode);
  if (safetyBlock != EquipmentTestingLogic::BlockReason::NONE) {
    char reason[144] = "";
    (void)serviceTestAllowed(state, reason, sizeof(reason));
    setServiceTestMessage(reason);
    return true;
  }
  // FSM::pause intentionally keeps water open for a hot cube. A full service
  // stop would then defeat that cooling policy, so do not take ownership until
  // the cube is known cold.
  if (!state.temps.valid[TEMP_CUBE] || state.temps.cube >= 45.0f) {
    setServiceTestMessage(
        (g_settings.language == 0)
            ? "Сервисная пауза запрещена: температура куба не подтверждена ниже 45°C."
            : "Service pause is blocked until cube temperature is confirmed below 45C.");
    return true;
  }
  if (!ServiceTestSession::request(static_cast<uint8_t>(state.mode), true,
                                   state.paused, target)) {
    setServiceTestMessage((g_settings.language == 0)
                              ? "Не удалось зарезервировать сервисную сессию."
                              : "Could not reserve the service session.");
    return true;
  }
  setServiceTestMessage((g_settings.language == 0)
                            ? "Подтвердите паузу процесса для сервисного теста."
                            : "Confirm process pause for the service test.");
  openOperatorOverlay(TftOperatorOverlayLogic::Action::ServicePauseForTest);
  return true;
}

static ServiceTestSession::Target getServiceTestTargetForTap(
    int16_t tx, int16_t ty, uint8_t page,
    const TftServiceTestLogic::Availability &availability) {
  const int16_t pageY = UI_HEADER_H + 4;
  const int16_t firstRowY = pageY + 38;
  const int16_t rowH = 42;
  const int16_t rowGap = 8;
  const int16_t x1 = 10;
  const int16_t x2 = 245;
  const int16_t w = 225;
  if (page == 0) {
    if (TftServiceTestLogic::isAvailable(TftServiceTestLogic::Device::Pump,
                                         availability) &&
        hit(tx, ty, x1, firstRowY, w, rowH)) {
      return ServiceTestSession::Target::PUMP;
    }
    if (TftServiceTestLogic::isAvailable(TftServiceTestLogic::Device::Stirrer,
                                         availability) &&
        hit(tx, ty, x2, firstRowY, w, rowH)) {
      return ServiceTestSession::Target::STIRRER;
    }
    if (TftServiceTestLogic::isAvailable(TftServiceTestLogic::Device::Heater,
                                         availability) &&
        hit(tx, ty, x1, firstRowY + rowH + rowGap, w, rowH)) {
      return ServiceTestSession::Target::HEATER;
    }
  } else if (page == 1) {
    if (TftServiceTestLogic::isAvailable(TftServiceTestLogic::Device::WaterValve,
                                         availability) &&
        hit(tx, ty, x1, firstRowY, w, rowH)) {
      return ServiceTestSession::Target::WATER_VALVE;
    }
    if (TftServiceTestLogic::isAvailable(TftServiceTestLogic::Device::HeadsValve,
                                         availability) &&
        hit(tx, ty, x2, firstRowY, w, rowH)) {
      return ServiceTestSession::Target::HEADS_VALVE;
    }
    if (TftServiceTestLogic::isAvailable(TftServiceTestLogic::Device::BodyValve,
                                         availability) &&
        hit(tx, ty, x1, firstRowY + rowH + rowGap, w, rowH)) {
      return ServiceTestSession::Target::BODY_VALVE;
    }
    if (TftServiceTestLogic::isAvailable(TftServiceTestLogic::Device::TailsValve,
                                         availability) &&
        hit(tx, ty, x2, firstRowY + rowH + rowGap, w, rowH)) {
      return ServiceTestSession::Target::TAILS_VALVE;
    }
    if (TftServiceTestLogic::isAvailable(TftServiceTestLogic::Device::UnoValve,
                                         availability) &&
        hit(tx, ty, x1, firstRowY + (rowH + rowGap) * 2, w, rowH)) {
      return ServiceTestSession::Target::UNO_VALVE;
    }
    if (TftServiceTestLogic::isAvailable(TftServiceTestLogic::Device::CoolingPwm,
                                         availability) &&
        hit(tx, ty, x2, firstRowY + (rowH + rowGap) * 2, w, rowH)) {
      return ServiceTestSession::Target::COOLING_PWM;
    }
  } else if (TftServiceTestLogic::isAvailable(
                 TftServiceTestLogic::Device::Fractionator, availability) &&
             hit(tx, ty, 10, firstRowY, TFT_WIDTH - 20, rowH)) {
    return ServiceTestSession::Target::FRACTIONATOR;
  }
  return ServiceTestSession::Target::NONE;
}

static TftServiceTestLogic::Availability
getServiceTestAvailability(const SystemState &state) {
  TftServiceTestLogic::Availability availability;
  availability.heater = PIN_HEATER >= 0;
  availability.pump = PIN_PUMP_STEP >= 0 && PIN_PUMP_DIR >= 0;
  availability.stirrer =
      g_settings.stirrer.enabled && Stirrer::isAvailable();
  availability.waterValve = PIN_VALVE_WATER >= 0;
  availability.headsValve = Valves::hasHeadsValve();
  availability.bodyValve = Valves::hasBodyValve();
  availability.tailsValve = Valves::hasTailsValve();
  availability.unoValve = Valves::hasUnoValve();
  availability.coolingPwm =
      BOARD_HAS_STARTSTOP_PWM && g_settings.equipment.coolingPwmEnabled;
  availability.fractionator =
      BOARD_HAS_FRACTIONATOR_SERVO && g_settings.fractionator.enabled &&
      Valves::isFractionatorEnabled();
  availability.sensors = countExpectedTempSensors() > 0 ||
                         state.health.bmp280Ok || state.health.ads1115Ok ||
                         state.health.pzemOk;
  return availability;
}

static bool beginServiceTestOutput(const SystemState &state,
                                   ServiceTestSession::Target target,
                                   uint32_t timeoutMs) {
  if (requestServicePauseIfNeeded(state, target)) return false;
  if (target == ServiceTestSession::Target::STIRRER &&
      (!state.temps.valid[TEMP_CUBE] ||
       state.temps.cube > BOARD_SERVICE_TEST_STIRRER_MAX_C)) {
    setServiceTestMessage(
        (g_settings.language == 0)
            ? "Тест мешалки требует подтверждённой безопасной температуры куба."
            : "Stirrer test requires a confirmed safe cube temperature.");
    return false;
  }
  char reason[144] = "";
  if (!serviceTestAllowed(state, reason, sizeof(reason))) {
    setServiceTestMessage(reason);
    return false;
  }
  PowerCommandGateway::safeStopAll(PowerCommandGateway::Owner::SERVICE,
                                   PowerCommandGateway::Reason::TFT_SERVICE_TEST);
  ui.serviceTestOutputActive = true;
  ui.serviceTestDeadlineMs = millis() + timeoutMs;
  ui.serviceTestActiveTarget = target;
  ui.serviceTestPumpStartVolumeMl =
      target == ServiceTestSession::Target::PUMP ? Pump::getTotalVolume() : 0.0f;
  if (state.mode != Mode::IDLE && !ServiceTestSession::startTest(target)) {
    PowerCommandGateway::safeStopAll(
        PowerCommandGateway::Owner::SERVICE,
        PowerCommandGateway::Reason::TFT_SERVICE_TEST);
    ui.serviceTestOutputActive = false;
    ui.serviceTestDeadlineMs = 0;
    ui.serviceTestActiveTarget = ServiceTestSession::Target::NONE;
    setServiceTestMessage((g_settings.language == 0)
                              ? "Сервисная сессия не перешла к тесту."
                              : "Service session could not enter test mode.");
    return false;
  }
  return true;
}

static void stopServiceTestOutputs(const char *message) {
  if (ui.serviceTestOutputActive ||
      ServiceTestSession::getSnapshot().phase ==
          ServiceTestSession::Phase::SAFE_PAUSED ||
      ServiceTestSession::getSnapshot().phase ==
          ServiceTestSession::Phase::TEST_ACTIVE) {
    PowerCommandGateway::safeStopAll(
        PowerCommandGateway::Owner::SERVICE,
        PowerCommandGateway::Reason::TFT_SERVICE_TEST);
    ui.serviceTestOutputActive = false;
    ui.serviceTestDeadlineMs = 0;
    ui.serviceTestActiveTarget = ServiceTestSession::Target::NONE;
    (void)ServiceTestSession::beginStopping();
    settleServiceTestStopping();
  }
  if (ServiceTestSession::getSnapshot().phase !=
      ServiceTestSession::Phase::RESTORE_READY) {
    setServiceTestMessage(message);
  }
}

static void runServiceValvePulse(const SystemState &state,
                                 PowerCommandGateway::Valve valve,
                                 ServiceTestSession::Target target,
                                 const char *message) {
  if (!beginServiceTestOutput(state, target,
                              BOARD_SERVICE_TEST_VALVE_PULSE_MS)) {
    return;
  }
  PowerCommandGateway::pulseValve(PowerCommandGateway::Owner::SERVICE,
                                  PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                                  valve, BOARD_SERVICE_TEST_VALVE_PULSE_MS);
  setServiceTestMessage(message);
}

static void updateServiceTestLifecycle(const SystemState &state) {
  const uint32_t now = millis();
  const ServiceTestSession::Snapshot session =
      ServiceTestSession::getSnapshot();

  if (ui.serviceTestDeadlineMs != 0 &&
      static_cast<int32_t>(now - ui.serviceTestDeadlineMs) >= 0) {
    stopServiceTestOutputs(
        (g_settings.language == 0)
            ? "Лимит времени сервисного теста достигнут: выходы отключены."
            : "Service-test time limit reached: outputs stopped.");
  }

  if (ui.serviceTestOutputActive &&
      ui.serviceTestActiveTarget == ServiceTestSession::Target::PUMP &&
      (Pump::getTotalVolume() - ui.serviceTestPumpStartVolumeMl) >=
          BOARD_SERVICE_TEST_PUMP_MAX_VOLUME_ML) {
    stopServiceTestOutputs(
        (g_settings.language == 0)
            ? "Достигнут лимит сервисного объёма насоса: выходы отключены."
            : "Service pump volume limit reached: outputs stopped.");
  }

  if (session.phase == ServiceTestSession::Phase::CLOSED) return;

  const bool safetyReady = state.safetyOk && !Safety::isLatched(state) &&
                           state.currentAlarm.type == AlarmType::NONE &&
                           !g_settings.demoMode;
  const bool mustRemainPaused =
      session.phase == ServiceTestSession::Phase::SAFE_PAUSED ||
      session.phase == ServiceTestSession::Phase::TEST_ACTIVE ||
      session.phase == ServiceTestSession::Phase::STOPPING ||
      session.phase == ServiceTestSession::Phase::RESTORE_READY;
  const bool lifecycleMatches =
      state.mode != Mode::IDLE &&
      session.originalMode == static_cast<uint8_t>(state.mode) &&
      (!mustRemainPaused || state.paused);
  if (!safetyReady || !lifecycleMatches) {
    PowerCommandGateway::safeStopAll(
        PowerCommandGateway::Owner::SERVICE,
        PowerCommandGateway::Reason::TFT_SERVICE_TEST);
    ui.serviceTestOutputActive = false;
    ui.serviceTestDeadlineMs = 0;
    ui.serviceTestActiveTarget = ServiceTestSession::Target::NONE;
    (void)ServiceTestSession::fault();
    setServiceTestMessage(
        (g_settings.language == 0)
            ? "Сервисная сессия аварийно остановлена; возврат процесса запрещён."
            : "Service session faulted; process restore is prohibited.");
    return;
  }

  settleServiceTestStopping();
}

static void updatePumpCalibrationLifecycle(const SystemState &state) {
  if (!PumpCalibrationSession::outputActive()) return;
  const bool safe = state.mode == Mode::IDLE && state.safetyOk &&
                    !Safety::isLatched(state) &&
                    state.currentAlarm.type == AlarmType::NONE &&
                    !g_settings.demoMode;
  if (!safe || PumpCalibrationSession::hasTimedOut(
                   millis(), BOARD_PUMP_CALIBRATION_TIMEOUT_MS)) {
    cancelPumpCalibration(
        safe ? ((g_settings.language == 0)
                    ? "Калибровка остановлена по лимиту времени."
                    : "Calibration stopped by time limit.")
             : ((g_settings.language == 0)
                    ? "Калибровка отменена safety; прежнее значение сохранено."
                    : "Calibration cancelled by safety; previous value retained."));
  }
}

static bool handleServiceScreenTap(int16_t tx, int16_t ty,
                                   const SystemState &) {
  if (isServiceTempsTap(tx, ty)) {
    pushScreen(UI_ALL_TEMPS);
    return true;
  }
  if (isServiceTestTap(tx, ty)) {
    if (ServiceTestSession::getSnapshot().phase ==
        ServiceTestSession::Phase::CLOSED) {
      ui.serviceTestPage = 0;
      ui.serviceTestOutputActive = false;
      ui.serviceTestDeadlineMs = 0;
      ui.serviceTestActiveTarget = ServiceTestSession::Target::NONE;
      setServiceTestMessage(nullptr);
    }
    pushScreen(UI_SERVICE_TEST);
    return true;
  }
  return false;
}

static bool handleServiceTestScreenTap(int16_t tx, int16_t ty,
                                       const SystemState &state) {
  const int16_t pageY = UI_HEADER_H + 4;
  const int16_t firstRowY = pageY + 38;
  const int16_t rowH = 42;
  const int16_t rowGap = 8;
  const int16_t x1 = 10;
  const int16_t x2 = 245;
  const int16_t w = 225;
  const auto availability = getServiceTestAvailability(state);

  if (hit(tx, ty, x1, pageY, 96, 28) && ui.serviceTestPage > 0) {
    --ui.serviceTestPage;
    return true;
  }
  if (hit(tx, ty, TFT_WIDTH - 106, pageY, 96, 28) &&
      ui.serviceTestPage < 2) {
    ++ui.serviceTestPage;
    return true;
  }

  const ServiceTestSession::Target requestedTarget =
      getServiceTestTargetForTap(tx, ty, ui.serviceTestPage, availability);
  if (requestedTarget != ServiceTestSession::Target::NONE &&
      requestServicePauseIfNeeded(state, requestedTarget)) {
    return true;
  }

  if (ui.serviceTestPage == 0) {
    if (TftServiceTestLogic::isAvailable(
            TftServiceTestLogic::Device::Pump, availability) &&
        hit(tx, ty, x1, firstRowY, w, rowH)) {
      if (Pump::isRunning()) {
        stopServiceTestOutputs((g_settings.language == 0)
                                   ? "Сервисный тест насоса остановлен."
                                   : "Pump service test stopped.");
        return true;
      }
      if (!beginServiceTestOutput(state, ServiceTestSession::Target::PUMP,
                                  BOARD_SERVICE_TEST_PUMP_TIMEOUT_MS)) {
        return true;
      }
      PowerCommandGateway::setPumpSpeed(
          PowerCommandGateway::Owner::SERVICE,
          PowerCommandGateway::Reason::TFT_SERVICE_TEST,
          BOARD_SERVICE_TEST_PUMP_SPEED_ML_H);
      setServiceTestMessage((g_settings.language == 0)
                                ? "Насос запущен на сервисной скорости."
                                : "Pump started at service speed.");
      return true;
    }
    if (TftServiceTestLogic::isAvailable(
            TftServiceTestLogic::Device::Stirrer, availability) &&
        hit(tx, ty, x2, firstRowY, w, rowH)) {
      if (Stirrer::isRunning()) {
        stopServiceTestOutputs((g_settings.language == 0)
                                   ? "Сервисный тест мешалки остановлен."
                                   : "Stirrer service test stopped.");
        return true;
      }
      if (!beginServiceTestOutput(state,
                                  ServiceTestSession::Target::STIRRER,
                                  BOARD_SERVICE_TEST_STIRRER_TIMEOUT_MS)) {
        return true;
      }
      const uint8_t speed = g_settings.stirrer.defaultSpeedPercent > 0
                                ? g_settings.stirrer.defaultSpeedPercent
                                : 30;
      PowerCommandGateway::setStirrerSpeed(
          PowerCommandGateway::Owner::SERVICE,
          PowerCommandGateway::Reason::TFT_SERVICE_TEST, speed);
      setServiceTestMessage((g_settings.language == 0)
                                ? "Мешалка запущена на сервисной скорости."
                                : "Stirrer started at service speed.");
      return true;
    }
    if (TftServiceTestLogic::isAvailable(
            TftServiceTestLogic::Device::Heater, availability) &&
        hit(tx, ty, x1, firstRowY + rowH + rowGap, w, rowH)) {
      if (Heater::getDiagnostics().active) {
        stopServiceTestOutputs((g_settings.language == 0)
                                   ? "Сервисный тест ТЭНа остановлен."
                                   : "Heater service test stopped.");
        return true;
      }
      char reason[144] = "";
      if (!serviceTestAllowed(state, reason, sizeof(reason))) {
        setServiceTestMessage(reason);
        return true;
      }
      openOperatorOverlay(TftOperatorOverlayLogic::Action::ServiceHeaterStart);
      return true;
    }
    if (hit(tx, ty, x2, firstRowY + rowH + rowGap, w, rowH)) {
      const ServiceTestSession::Snapshot session =
          ServiceTestSession::getSnapshot();
      if (session.phase == ServiceTestSession::Phase::RESTORE_READY) {
        if (session.resumeProcess) {
          if (!state.safetyOk || Safety::isLatched(state) ||
              state.currentAlarm.type != AlarmType::NONE ||
              !ServiceTestSession::claimProcessResume()) {
            setServiceTestMessage(
                (g_settings.language == 0)
                    ? "Возврат процесса заблокирован safety."
                    : "Process restore is safety-blocked.");
            return true;
          }
          PowerCommandGateway::noteFsmSetpoint(
              PowerCommandGateway::Target::ALL_OUTPUTS,
              PowerCommandGateway::Reason::TFT_RUNTIME_SETPOINT, 1.0f);
          FSM::resume(g_state);
          ControlV2::updateRuntime(g_state, g_settings);
          setServiceTestMessage(
              (g_settings.language == 0)
                  ? "Процесс возвращён из сервисной паузы."
                  : "Process restored from service pause.");
          return true;
        }
        if (ServiceTestSession::closeWithoutResume()) {
          setServiceTestMessage(
              (g_settings.language == 0)
                  ? "Сервис завершён; исходная пауза процесса сохранена."
                  : "Service closed; the original process pause is retained.");
        }
        return true;
      }
      if (session.phase == ServiceTestSession::Phase::FAULTED) {
        if (state.safetyOk && !Safety::isLatched(state) &&
            state.currentAlarm.type == AlarmType::NONE) {
          ServiceTestSession::cancel();
          setServiceTestMessage(
              (g_settings.language == 0)
                  ? "Сервис закрыт после safety; процесс остаётся на паузе."
                  : "Service closed after safety; process stays paused.");
        } else {
          setServiceTestMessage(
              (g_settings.language == 0)
                  ? "Сначала выполните acknowledge/reset safety."
                  : "Acknowledge/reset safety first.");
        }
        return true;
      }
      stopServiceTestOutputs((g_settings.language == 0)
                                 ? "Все сервисные выходы остановлены."
                                 : "All service outputs have been stopped.");
      return true;
    }
    if (TftServiceTestLogic::isAvailable(
            TftServiceTestLogic::Device::Sensors, availability) &&
        hit(tx, ty, 10, firstRowY + (rowH + rowGap) * 2, TFT_WIDTH - 20,
            rowH)) {
      if (ServiceTestSession::getSnapshot().phase !=
          ServiceTestSession::Phase::CLOSED) {
        setServiceTestMessage(
            (g_settings.language == 0)
                ? "Сначала завершите сервисную сессию и верните процесс."
                : "Finish the service session and restore the process first.");
        return true;
      }
      pushScreen(UI_ALL_TEMPS);
      return true;
    }
    return false;
  }

  if (ui.serviceTestPage == 1) {
    if (TftServiceTestLogic::isAvailable(
            TftServiceTestLogic::Device::WaterValve, availability) &&
        hit(tx, ty, x1, firstRowY, w, rowH)) {
      runServiceValvePulse(state, PowerCommandGateway::Valve::WATER,
                           ServiceTestSession::Target::WATER_VALVE,
                           (g_settings.language == 0)
                               ? "Клапан воды: импульс 1 с."
                               : "Water valve: 1 second pulse.");
      return true;
    }
    if (TftServiceTestLogic::isAvailable(
            TftServiceTestLogic::Device::HeadsValve, availability) &&
        hit(tx, ty, x2, firstRowY, w, rowH)) {
      runServiceValvePulse(state, PowerCommandGateway::Valve::HEADS,
                           ServiceTestSession::Target::HEADS_VALVE,
                           (g_settings.language == 0)
                               ? "Клапан голов: импульс 1 с."
                               : "Heads valve: 1 second pulse.");
      return true;
    }
    if (TftServiceTestLogic::isAvailable(
            TftServiceTestLogic::Device::BodyValve, availability) &&
        hit(tx, ty, x1, firstRowY + rowH + rowGap, w, rowH)) {
      runServiceValvePulse(state, PowerCommandGateway::Valve::BODY,
                           ServiceTestSession::Target::BODY_VALVE,
                           (g_settings.language == 0)
                               ? "Клапан тела: импульс 1 с."
                               : "Body valve: 1 second pulse.");
      return true;
    }
    if (TftServiceTestLogic::isAvailable(
            TftServiceTestLogic::Device::TailsValve, availability) &&
        hit(tx, ty, x2, firstRowY + rowH + rowGap, w, rowH)) {
      runServiceValvePulse(state, PowerCommandGateway::Valve::TAILS,
                           ServiceTestSession::Target::TAILS_VALVE,
                           (g_settings.language == 0)
                               ? "Клапан хвостов: импульс 1 с."
                               : "Tails valve: 1 second pulse.");
      return true;
    }
    if (TftServiceTestLogic::isAvailable(
            TftServiceTestLogic::Device::UnoValve, availability) &&
        hit(tx, ty, x1, firstRowY + (rowH + rowGap) * 2, w, rowH)) {
      runServiceValvePulse(state, PowerCommandGateway::Valve::UNO,
                           ServiceTestSession::Target::UNO_VALVE,
                           (g_settings.language == 0)
                               ? "Клапан УНО: импульс 1 с."
                               : "UNO valve: 1 second pulse.");
      return true;
    }
    if (TftServiceTestLogic::isAvailable(
            TftServiceTestLogic::Device::CoolingPwm, availability) &&
        hit(tx, ty, x2, firstRowY + (rowH + rowGap) * 2, w, rowH)) {
      if (Valves::getStartStop() > 0) {
        stopServiceTestOutputs((g_settings.language == 0)
                                   ? "Сервисный тест PWM остановлен."
                                   : "PWM service test stopped.");
        return true;
      }
      if (!beginServiceTestOutput(state,
                                  ServiceTestSession::Target::COOLING_PWM,
                                  BOARD_SERVICE_TEST_PWM_TIMEOUT_MS)) {
        return true;
      }
      const uint8_t duty = BOARD_SERVICE_TEST_PWM_DUTY;
      PowerCommandGateway::setCoolingPwm(
          PowerCommandGateway::Owner::SERVICE,
          PowerCommandGateway::Reason::TFT_SERVICE_TEST, duty);
      setServiceTestMessage((g_settings.language == 0)
                                ? "PWM охлаждения включён на сервисном уровне."
                                : "Cooling PWM enabled at service level.");
      return true;
    }
    return false;
  }

  if (TftServiceTestLogic::isAvailable(
          TftServiceTestLogic::Device::Fractionator, availability) &&
      hit(tx, ty, 10, firstRowY, TFT_WIDTH - 20, rowH)) {
    if (!beginServiceTestOutput(state,
                                ServiceTestSession::Target::FRACTIONATOR,
                                BOARD_SERVICE_TEST_FRACTIONATOR_TIMEOUT_MS)) {
      return true;
    }
    const Fraction next = Valves::getNextEnabledFraction(g_settings.fractionator);
    PowerCommandGateway::setFraction(PowerCommandGateway::Owner::SERVICE,
                                     PowerCommandGateway::Reason::TFT_SERVICE_TEST,
                                     next, true);
    setServiceTestMessage((g_settings.language == 0)
                              ? "Фракционник переведён в следующую позицию."
                              : "Fractionator moved to the next position.");
    return true;
  }
  return false;
}

static bool handleNoopScreenTap(int16_t, int16_t, const SystemState &) {
  return false;
}

typedef bool (*UiTapHandler)(int16_t tx, int16_t ty, const SystemState &state);

static constexpr UiTapHandler kUiTapDispatch[] = {
    handleDashboardScreenTap,   // UI_DASHBOARD
    handleControlScreenTap,     // UI_CONTROL
    handleSettingsScreenTap,    // UI_SETTINGS
    handleServiceScreenTap,     // UI_SERVICE
    handleModeMonitorScreenTap, // UI_MODE_MONITOR
    handleEquipmentScreenTap,   // UI_EQUIPMENT
    handleRectParamsScreenTap,  // UI_RECT_PARAMS
    handleDistParamsScreenTap,  // UI_DIST_PARAMS
    handleCalibrationScreenTap, // UI_CALIBRATION
    handleManualScreenTap,      // UI_MANUAL
    handleValueEditScreenTap,   // UI_VALUE_EDIT
    handleNoopScreenTap,        // UI_ALL_TEMPS
    handleModePickerScreenTap,  // UI_MODE_PICKER
    handleModeSetupScreenTap,   // UI_MODE_SETUP
    handlePreflightScreenTap,   // UI_PREFLIGHT
    handleProfilePickerScreenTap, // UI_PROFILE_PICKER
    handleSafetyViewScreenTap,    // UI_SAFETY_VIEW
    handleProcessEventsScreenTap, // UI_PROCESS_EVENTS
    handleProcessParamsScreenTap, // UI_PROCESS_PARAMS
    handleModeConfigScreenTap,    // UI_MODE_CONFIG
    handleModeParametersScreenTap, // UI_MODE_PARAMETERS
    handleServiceTestScreenTap,    // UI_SERVICE_TEST
    handlePumpCalibrationScreenTap, // UI_PUMP_CALIBRATION
    handleSensorCalibrationScreenTap // UI_SENSOR_CALIBRATION
};

static UiTapHandler getUiTapHandler(UiScreen screen) {
  const size_t screenIndex = static_cast<size_t>(screen);
  if (screenIndex < (sizeof(kUiTapDispatch) / sizeof(kUiTapDispatch[0]))) {
    return kUiTapDispatch[screenIndex];
  }
  return handleNoopScreenTap;
}

static bool handleScreenTap(int16_t tx, int16_t ty, const SystemState &state) {
  return getUiTapHandler(ui.currentScreen)(tx, ty, state);
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
  HMIIndicators::drawProgressBar(tft, x, y, w, h, percent, colorNavInactive(),
                                 fill, COLOR_WARNING);
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

static int16_t displayTextWidth(const char *text) {
  return text ? tft.textWidth(text) : 0;
}

static void drawDisplayString(const char *text, int32_t x, int32_t y) {
  if (text == nullptr) {
    return;
  }
  tft.drawString(text, x, y);
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
  if (displayTextWidth(out) <= maxWidth) {
    return;
  }

  const int16_t suffixW = displayTextWidth(suffix);
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
    if (displayTextWidth(candidate) <= maxWidth) {
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
  drawDisplayString(labelBuf, x + w / 2, y + h / 2);
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
  case NbkPhase::FEED_RAMP:
    return ru ? "Подача" : "Feed ramp";
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
  case FermentationPhase::FERMENTATION:
    return ru ? "Работа" : "Run";
  case FermentationPhase::COMPLETED:
    return ru ? "Готово" : "Done";
  case FermentationPhase::IDLE:
  default:
    return ru ? "Ожид." : "Idle";
  }
}

static const char *
getDisplayRefreshProfileCompactName(DisplayRefreshProfile profile) {
  const bool ru = (g_settings.language == 0);
  switch (profile) {
  case DisplayRefreshProfile::SAFE:
    return ru ? "5с" : "5s";
  case DisplayRefreshProfile::FAST:
    return ru ? "0.5с" : "0.5s";
  case DisplayRefreshProfile::NORMAL:
  default:
    return ru ? "1с" : "1s";
  }
}

static uint16_t getDisplayRefreshProfileColor(DisplayRefreshProfile profile) {
  switch (profile) {
  case DisplayRefreshProfile::SAFE:
    return COLOR_WARNING;
  case DisplayRefreshProfile::FAST:
    return COLOR_SUCCESS;
  case DisplayRefreshProfile::NORMAL:
  default:
    return COLOR_INFO;
  }
}

static const char *getStirrerEnabledCompactName() {
  const bool ru = (g_settings.language == 0);
  if (!g_settings.stirrer.enabled) {
    return ru ? "ВЫКЛ" : "OFF";
  }
  if (!g_state.stirrer.available) {
    return ru ? "НЕТ" : "N/A";
  }
  return ru ? "ВКЛ" : "ON";
}

static uint16_t getStirrerEnabledColor() {
  if (!g_settings.stirrer.enabled) {
    return COLOR_DARK_GREY;
  }
  return g_state.stirrer.available ? COLOR_SUCCESS : COLOR_WARNING;
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

enum class TftTextId : uint8_t {
  PowerShort,
  ColumnShort,
  FeedShort,
  CutsShort,
  SpeedShort,
  TargetShort,
  TouchShort,
  CalDoneShort,
  CalRawShort,
  SettingsHint,
  SettingsReadOnlyHint,
  ViewOnlyHint,
  ManualOnlyHint,
  RectTitle,
  RectPageFeedCuts,
  RectPageFlowTemp,
  RectFeedHint,
  DistTitle,
  CalibrationTitle,
  CalibrationHint,
  ManualTitle,
  ManualHint,
  ManualLockTitle,
  ManualLockMessage,
  ValueTitle,
  ValueHint,
  ServiceFrame,
  ServiceDiag,
  ServiceHintTemps,
  ServiceHintHard,
  ServiceHintSlow
};

static const char *tftText(TftTextId id) {
  const bool ru = (g_settings.language == 0);
  switch (id) {
  case TftTextId::PowerShort:
    return ru ? "МОЩН" : "PWR";
  case TftTextId::ColumnShort:
    return ru ? "ВЫС.К" : "COL H";
  case TftTextId::FeedShort:
    return ru ? "СС" : "FEED";
  case TftTextId::CutsShort:
    return ru ? "Г/Т/Х" : "H/B/T";
  case TftTextId::SpeedShort:
    return ru ? "СКОР" : "SPEED";
  case TftTextId::TargetShort:
    return ru ? "ЦЕЛЬ" : "TARGET";
  case TftTextId::TouchShort:
    return ru ? "ТАЧ" : "TOUCH";
  case TftTextId::CalDoneShort:
    return ru ? "калибр." : "done";
  case TftTextId::CalRawShort:
    return ru ? "не кал." : "raw";
  case TftTextId::SettingsHint:
    return ru ? "Карточки = раздел | низ = быстрые системные"
              : "Cards = sections | bottom = quick system toggles";
  case TftTextId::SettingsReadOnlyHint:
    return ru ? "Во время процесса только просмотр"
              : "View only while process runs";
  case TftTextId::ViewOnlyHint:
    return ru ? "Экран только для контроля"
              : "This screen is view only";
  case TftTextId::ManualOnlyHint:
    return ru ? "Доступно только в IDLE и ручной ректификации"
              : "Available only in idle and manual rectification";
  case TftTextId::RectTitle:
    return ru ? "РЕКТ. ПАРАМ." : "RECT PARAMS";
  case TftTextId::RectPageFeedCuts:
    return ru ? "СЫРЬЁ / ФРАКЦ." : "FEED / CUTS";
  case TftTextId::RectPageFlowTemp:
    return ru ? "СКОР. / ТЕМП." : "FLOW / TEMP";
  case TftTextId::RectFeedHint:
    return ru ? "Тап СЫРЬЁ = тип и фракции" : "Tap feedstock = type + cuts";
  case TftTextId::DistTitle:
    return ru ? "ДИСТ. ПАРАМ." : "DIST PARAMS";
  case TftTextId::CalibrationTitle:
    return ru ? "КАЛИБР." : "CALIBRATION";
  case TftTextId::CalibrationHint:
    return ru ? "Сначала ml/об, потом тач" : "Set ml/rev first, then touch";
  case TftTextId::ManualTitle:
    return ru ? "РУЧНЫЕ УЗЛЫ" : "MANUAL I/O";
  case TftTextId::ManualHint:
    return ru ? "Тап плитки = настройка | клапаны = сразу"
              : "Tap tile = set | valves = instant";
  case TftTextId::ManualLockTitle:
    return ru ? "РУЧНОЙ ДОСТУП" : "MANUAL ACCESS";
  case TftTextId::ManualLockMessage:
    return ru ? "Автопроцесс активен. Ручной доступ закрыт."
              : "Manual control is locked while an automatic process is "
                "active.";
  case TftTextId::ValueTitle:
    return ru ? "ТЕКУЩЕЕ" : "VALUE";
  case TftTextId::ValueHint:
    return ru ? "Слева вниз, справа вверх" : "Left = down, right = up";
  case TftTextId::ServiceFrame:
    return ru ? "КАДР TFT" : "TFT FRAME";
  case TftTextId::ServiceDiag:
    return ru ? "ДИАГН. TFT" : "TFT DIAG";
  case TftTextId::ServiceHintTemps:
    return ru ? "Тап нижнюю строку -> температуры"
              : "Tap lower row -> temperatures";
  case TftTextId::ServiceHintHard:
    return ru ? "Сбои TFT recovery. Проверьте питание и SPI"
              : "Hard TFT recoveries detected. Check power and SPI";
  case TftTextId::ServiceHintSlow:
    return ru ? "Медленные кадры TFT. Проверьте нагрузку"
              : "Slow TFT frames detected. Check display load";
  }
  return "";
}

static void drawGlobalStatusBar(const SystemState &state, bool force);

static void drawHeader(const char *title, bool showBack) {
  drawGlobalStatusBar(g_state, true);
  if (!showBack) {
    return;
  }

  const int16_t titleY = UI_GLOBAL_STATUS_H;
  const int16_t titleH = UI_HEADER_H - UI_GLOBAL_STATUS_H;
  tft.fillRect(0, titleY, TFT_WIDTH, titleH, colorNavBg());
  tft.drawFastHLine(0, titleY, TFT_WIDTH, colorBorder());
  tft.drawFastHLine(0, UI_HEADER_H - 1, TFT_WIDTH, colorBorder());

  const int16_t bw = 110;
  const int16_t bh = titleH - 4;
  const int16_t bx = TFT_WIDTH - bw - 5;
  const int16_t by = titleY + 2;

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextDatum(middle_left);
  char titleBuf[96];
  copyFittedText(title, bx - 26, titleBuf, sizeof(titleBuf));
  drawDisplayString(titleBuf, 14, titleY + (titleH / 2));
  tft.fillRect(bx, by, bw, bh, colorButtonBody());
  tft.drawRect(bx, by, bw, bh, colorBorder());
  tft.fillRect(bx + 2, by + 2, 8, bh - 4, colorAccent());
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(middle_center);
  char backBuf[32];
  copyFittedText(msg(Msg::BTN_BACK), bw - 20, backBuf, sizeof(backBuf));
  drawDisplayString(backBuf, bx + (bw / 2) + 4, titleY + (titleH / 2));

  tft.setTextDatum(top_left);
}

static void drawTabs(UiScreen current) {
  const bool ru = (g_settings.language == 0);
  const bool processActive = (g_state.mode != Mode::IDLE);
  const char *labels[3] = {
      processActive ? (ru ? "МОНИТОР" : "MONITOR")
                    : (ru ? "ГЛАВНАЯ" : "HOME"),
      processActive ? (ru ? "УПРАВЛЕНИЕ" : "CONTROL")
                    : (ru ? "РЕЖИМЫ" : "MODES"),
      ru ? "СЕРВИС" : "SERVICE"};

  const int16_t navY = TFT_HEIGHT - UI_FOOTER_H;
  const int16_t gap = TFT_BUTTON_GAP;
  const int16_t bw = (TFT_WIDTH - (gap * 4)) / 3;
  const int16_t bh = UI_FOOTER_H - 12;
  const bool modeSetupRoute =
      current == UI_MODE_PICKER || current == UI_MODE_SETUP ||
      current == UI_PREFLIGHT || current == UI_PROFILE_PICKER ||
      current == UI_MODE_CONFIG || current == UI_MODE_PARAMETERS;
  const bool processControlRoute =
      current == UI_CONTROL || current == UI_SAFETY_VIEW ||
      current == UI_PROCESS_EVENTS || current == UI_PROCESS_PARAMS;

  tft.fillRect(0, navY, TFT_WIDTH, UI_FOOTER_H, colorNavBg());
  tft.drawFastHLine(0, navY, TFT_WIDTH, colorBorder());
  tft.drawFastHLine(0, navY + 1, TFT_WIDTH, colorAccent());

  for (int i = 0; i < 3; i++) {
    const int16_t x = gap + i * (bw + gap);
    const int16_t y = navY + 6;
    const bool active =
        (i == 0 && current == (processActive ? UI_MODE_MONITOR : UI_DASHBOARD)) ||
        (i == 1 &&
         (processActive ? processControlRoute : modeSetupRoute)) ||
        (i == 2 &&
         (current == UI_SERVICE || current == UI_SERVICE_TEST));
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
    drawDisplayString(tabBuf, x + bw / 2, y + (bh / 2) + 5);

    if (i == 0 && !g_state.safetyOk) {
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

  uint16_t tw = displayTextWidth(value);
  int16_t boxW = (tw < 92) ? 92 : tw + 22;
  int16_t boxX = rowX + rowW - boxW - 10;
  const int16_t boxY = rowY + 4;
  const int16_t boxH = rowH - 8;
  const int16_t labelMaxW = boxX - rowX - 22;
  char labelBuf[96];
  tft.setTextDatum(middle_left);
  copyFittedText(label, labelMaxW, labelBuf, sizeof(labelBuf));
  drawDisplayString(labelBuf, rowX + 14, rowY + (rowH / 2) + 1);

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
  drawDisplayString(valueBuf, boxX + boxW / 2, boxY + (boxH / 2) + 1);
  tft.setTextDatum(top_left);
  tft.setTextColor(colorFg());
}

static bool hasUsableIpAddress(const IPAddress &ip) {
  return ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0;
}

static void formatIpAddress(const IPAddress &ip, char *buf, size_t size) {
  if (buf == nullptr || size == 0) {
    return;
  }
  snprintf(buf, size, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

enum class HmiNetworkState : uint8_t {
  STATION = 0,
  ACCESS_POINT,
  CONNECTING,
  OFFLINE
};

struct HmiNetworkStatus {
  HmiNetworkState state = HmiNetworkState::OFFLINE;
  char summary[32] = {0};
  uint16_t tone = COLOR_DARK_GREY;
};

static HmiNetworkStatus getHmiNetworkStatus() {
  HmiNetworkStatus status;
  const IPAddress stationIp = WiFi.localIP();
  if (WiFi.status() == WL_CONNECTED && hasUsableIpAddress(stationIp)) {
    char ipBuf[24];
    formatIpAddress(stationIp, ipBuf, sizeof(ipBuf));
    status.state = HmiNetworkState::STATION;
    snprintf(status.summary, sizeof(status.summary), "Wi-Fi %s", ipBuf);
    status.tone = COLOR_SUCCESS;
    return status;
  }

  const IPAddress apIp = WiFi.softAPIP();
  if (hasUsableIpAddress(apIp)) {
    char ipBuf[24];
    formatIpAddress(apIp, ipBuf, sizeof(ipBuf));
    status.state = HmiNetworkState::ACCESS_POINT;
    snprintf(status.summary, sizeof(status.summary), "AP %s", ipBuf);
    status.tone = COLOR_INFO;
    return status;
  }

  if (WiFi.status() == WL_IDLE_STATUS) {
    status.state = HmiNetworkState::CONNECTING;
    snprintf(status.summary, sizeof(status.summary), "Wi-Fi ...");
    status.tone = COLOR_WARNING;
    return status;
  }

  snprintf(status.summary, sizeof(status.summary), "Wi-Fi --");
  return status;
}

static void getServiceIpSummary(char *buf, size_t size) {
  if (buf == nullptr || size == 0) {
    return;
  }

  const HmiNetworkStatus status = getHmiNetworkStatus();
  snprintf(buf, size, "%s", status.summary);
}

static void drawGlobalStatusBar(const SystemState &state, bool force) {
  const HmiNetworkStatus network = getHmiNetworkStatus();
  char modeBuf[48];
  snprintf(modeBuf, sizeof(modeBuf), "%s", getDisplayModeName(state.mode));

  const bool changed =
      strcmp(g_globalStatusCache.mode, modeBuf) != 0 ||
      strcmp(g_globalStatusCache.network, network.summary) != 0 ||
      g_globalStatusCache.safetyOk != state.safetyOk;
  if (!force && !changed) {
    return;
  }

  const bool ru = (g_settings.language == 0);
  const uint16_t safetyTone = state.safetyOk ? COLOR_SUCCESS : COLOR_DANGER;
  const char *safetyText =
      state.safetyOk ? (ru ? "НОРМА" : "SAFE") : (ru ? "АВАРИЯ" : "ALARM");
  constexpr int16_t modeX = 8;
  constexpr int16_t modeW = 144;
  constexpr int16_t safetyX = 156;
  constexpr int16_t safetyW = 60;
  constexpr int16_t networkX = 224;
  const int16_t networkW = TFT_WIDTH - networkX - 8;

  tft.fillRect(0, 0, TFT_WIDTH, UI_GLOBAL_STATUS_H, colorNavBg());
  tft.fillRect(0, 0, TFT_WIDTH, 3, safetyTone);
  tft.drawFastHLine(0, UI_GLOBAL_STATUS_H - 1, TFT_WIDTH, colorBorder());

  tft.setTextSize(1);
  tft.setFont(&fonts::efontJA_16);
  tft.setTextDatum(middle_left);
  tft.setTextColor(colorFg());
  char fittedMode[48];
  copyFittedText(modeBuf, modeW, fittedMode, sizeof(fittedMode));
  drawDisplayString(fittedMode, modeX, UI_GLOBAL_STATUS_H / 2 + 2);

  tft.fillRect(safetyX, 4, safetyW, UI_GLOBAL_STATUS_H - 7, safetyTone);
  tft.drawRect(safetyX, 4, safetyW, UI_GLOBAL_STATUS_H - 7, colorBorder());
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(middle_center);
  char fittedSafety[16];
  copyFittedText(safetyText, safetyW - 6, fittedSafety, sizeof(fittedSafety));
  drawDisplayString(fittedSafety, safetyX + safetyW / 2,
                    UI_GLOBAL_STATUS_H / 2 + 2);

  tft.setTextColor(network.tone);
  tft.setTextDatum(middle_right);
  char fittedNetwork[32];
  copyFittedText(network.summary, networkW, fittedNetwork,
                 sizeof(fittedNetwork));
  drawDisplayString(fittedNetwork, TFT_WIDTH - 8, UI_GLOBAL_STATUS_H / 2 + 2);
  tft.setTextDatum(top_left);
  tft.setTextColor(colorFg());

  strncpy(g_globalStatusCache.mode, modeBuf,
          sizeof(g_globalStatusCache.mode) - 1);
  g_globalStatusCache.mode[sizeof(g_globalStatusCache.mode) - 1] = '\0';
  strncpy(g_globalStatusCache.network, network.summary,
          sizeof(g_globalStatusCache.network) - 1);
  g_globalStatusCache.network[sizeof(g_globalStatusCache.network) - 1] =
      '\0';
  g_globalStatusCache.safetyOk = state.safetyOk;
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
  if (displayTextWidth(label) > (w - 20) && labelSize > 1) {
    labelSize = 1;
    tft.setTextSize(labelSize);
  }
  tft.setTextDatum(middle_center);
  char labelBuf[64];
  copyFittedText(label, w - 18, labelBuf, sizeof(labelBuf));
  drawDisplayString(labelBuf, x + w / 2, y + (h / 2) + ((labelSize > 1) ? 6 : 5));
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
  drawDisplayString(titleBuf, x + 14, y + 12);
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
  drawDisplayString(labelBuf, x, y);
  tft.setTextColor(valueColor);
  tft.setTextDatum(middle_right);
  drawDisplayString(valueBuf, x + w, y);
  tft.setTextDatum(top_left);
}

static void drawSummaryHeroBlock(int16_t x, int16_t y, int16_t w, int16_t h,
                                 const char *label, const char *value,
                                 uint16_t tone) {
  drawCard(x, y, w, h, colorBg());
  tft.fillRect(x + 1, y + 1, 6, h - 2, tone);
  tft.drawFastHLine(x + 8, y + h - 1, w - 9, colorBorder());

  tft.setTextColor(colorMuted());
  tft.setTextSize(1);
  tft.setTextDatum(top_left);
  char labelBuf[40];
  copyFittedText(label, w - 18, labelBuf, sizeof(labelBuf));
  drawDisplayString(labelBuf, x + 12, y + 5);

  uint8_t valueSize = (w >= 136 && h >= 42) ? 3 : 2;
  while (valueSize > 1) {
    tft.setTextSize(valueSize);
    if (displayTextWidth(value) <= (w - 18)) {
      break;
    }
    valueSize--;
  }

  tft.setTextColor(tone);
  tft.setTextSize(valueSize);
  tft.setTextDatum(middle_center);
  const int16_t valueY =
      y + ((h >= 42) ? (h / 2 + ((valueSize >= 3) ? 6 : 4)) : (h / 2 + 3));
  drawDisplayString(value, x + (w / 2) + 2, valueY);
  if (valueSize >= 3) {
    drawDisplayString(value, x + (w / 2) + 3, valueY);
  }

  tft.setTextSize(1);
  tft.setTextDatum(top_left);
}

enum class TileValueTone : uint8_t {
  Normal = 0,
  Primary = 1,
  Secondary = 2,
  Duration = 3,
};

static int16_t getValueTileHeaderHeight(int16_t h) {
  return (h >= 60) ? 18 : 16;
}

static void drawValueTileShell(int16_t x, int16_t y, int16_t w, int16_t h,
                               const char *label) {
  const int16_t headerH = getValueTileHeaderHeight(h);
  drawCard(x, y, w, h, colorCard());
  tft.fillRect(x + 1, y + 1, w - 2, headerH, colorNavBg());
  tft.fillRect(x + 1, y + 1, 5, headerH, colorAccent());
  tft.drawFastHLine(x + 1, y + headerH + 1, w - 2, colorBorder());
  tft.setTextColor(colorMuted());
  tft.setTextSize(1);
  tft.setTextDatum(top_left);
  char labelBuf[48];
  copyFittedText(label, w - 16, labelBuf, sizeof(labelBuf));
  drawDisplayString(labelBuf, x + 11, y + ((headerH >= 18) ? 5 : 4));
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
  drawDisplayString(hintBuf, TFT_WIDTH / 2, y + (h / 2) + 1);
  tft.setTextDatum(top_left);
}

static const char *getScreenReadOnlyHint(UiScreen screen,
                                         const SystemState &state) {
  switch (getUiScreenPolicy(screen).editPolicy) {
  case UiEditPolicy::IdleOnly:
    return tftText(TftTextId::SettingsReadOnlyHint);
  case UiEditPolicy::ManualOnly:
    return tftText(TftTextId::ManualOnlyHint);
  case UiEditPolicy::ModeMonitorRuntime:
    return getModeMonitorAccessHint(state.mode);
  case UiEditPolicy::ViewOnly:
  case UiEditPolicy::Always:
  default:
    return tftText(TftTextId::ViewOnlyHint);
  }
}

static uint16_t getScreenReadOnlyTone(UiScreen screen) {
  switch (getUiScreenPolicy(screen).editPolicy) {
  case UiEditPolicy::ViewOnly:
    return COLOR_INFO;
  case UiEditPolicy::IdleOnly:
  case UiEditPolicy::ManualOnly:
  case UiEditPolicy::ModeMonitorRuntime:
  case UiEditPolicy::Always:
  default:
    return COLOR_WARNING;
  }
}

static void drawScreenAccessFooterHint(UiScreen screen,
                                       const SystemState &state,
                                       const char *editableText,
                                       uint16_t editableTone) {
  if (isScreenReadOnly(screen, state)) {
    drawFooterHint(getScreenReadOnlyHint(screen, state),
                   getScreenReadOnlyTone(screen));
    return;
  }
  drawFooterHint(editableText, editableTone);
}

static int16_t drawWrappedTextBlock(int16_t x, int16_t y, int16_t w,
                                    const char *text, uint16_t color,
                                    uint8_t textSize = 1, uint8_t maxLines = 8,
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

      if (line[0] != '\0' && displayTextWidth(candidate) > (w - 4)) {
        drawDisplayString(line, centerX, y + linesDrawn * (lineH + lineGap));
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
      drawDisplayString(line, centerX, y + linesDrawn * (lineH + lineGap));
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

  int16_t textBottom =
      drawWrappedTextBlock(bodyX + 28, bodyY + 22, bodyW - 56, message,
                           colorFg(), messageSize, 8, 5);
  if (footer != nullptr && footer[0] != '\0') {
    const int16_t footerY = (textBottom + 18 > bodyY + bodyH - 34)
                                ? (bodyY + bodyH - 30)
                                : (textBottom + 18);
    drawWrappedTextBlock(bodyX + 28, footerY, bodyW - 56, footer, colorMuted(),
                         1, 3, 3);
  }
}

static constexpr int16_t OPERATOR_OVERLAY_BUTTON_Y = TFT_HEIGHT - 82;
static constexpr int16_t OPERATOR_OVERLAY_BUTTON_H = 34;

static TftOperatorOverlayLogic::Action
getRequiredOperatorOverlay(const SystemState &state) {
  using Action = TftOperatorOverlayLogic::Action;
  if (state.mode == Mode::MASHING && state.paused &&
      state.mashing.waitingForOperator &&
      state.mashing.stepType == MashStepType::OPERATOR_WAIT) {
    return Action::MashOperatorWait;
  }
  if (state.mode == Mode::DISTILLATION && state.fractionProgram.active &&
      state.fractionProgram.waitingForConfirmation) {
    return Action::FractionConfirmation;
  }
  if (state.mode == Mode::RECTIFICATION && state.paused &&
      (state.rectBodyContainerLevelReached ||
       state.rectHeadsContainerLevelReached ||
       state.rectBodyContainerChangePending)) {
    return Action::ContainerChange;
  }
  return Action::None;
}

static void openOperatorOverlay(TftOperatorOverlayLogic::Action action) {
  TftOperatorOverlayLogic::open(ui.operatorOverlay, action);
  ui.operatorOverlayMessage[0] = '\0';
  requestRedraw(DisplayRedrawReason::OPERATOR_OVERLAY);
}

static void closeOperatorOverlay() {
  TftOperatorOverlayLogic::close(ui.operatorOverlay);
  ui.operatorOverlayMessage[0] = '\0';
  // The overlay covered the entire framebuffer; restore the hidden page from
  // its cache contract instead of relying on individual tiles to change.
  requestRedraw(DisplayRedrawReason::OPERATOR_OVERLAY);
}

static bool syncOperatorOverlay(const SystemState &state) {
  if (!state.safetyOk && state.currentAlarm.type != AlarmType::NONE) {
    ui.serviceSession = TftHmiPolicyLogic::transitionServiceSession(
        ui.serviceSession,
        TftHmiPolicyLogic::ServiceSessionEvent::SafetyEmergency);
  }
  const TftOperatorOverlayLogic::Action required =
      getRequiredOperatorOverlay(state);
  if (required != TftOperatorOverlayLogic::Action::None) {
    if (ui.operatorOverlay.action != required) {
      openOperatorOverlay(required);
      return true;
    }
    return false;
  }

  if (TftOperatorOverlayLogic::isAutomatic(ui.operatorOverlay.action)) {
    closeOperatorOverlay();
    return true;
  }
  return false;
}

static void drawOperatorOverlay(const SystemState &state) {
  using Action = TftOperatorOverlayLogic::Action;
  const bool ru = g_settings.language == 0;
  const Action action = ui.operatorOverlay.action;
  const char *title = ru ? "ПОДТВЕРЖДЕНИЕ" : "CONFIRMATION";
  const char *message = "";
  const char *confirm = ru ? "ПОДТВЕРДИТЬ" : "CONFIRM";
  uint16_t tone = COLOR_WARNING;
  char dynamicMessage[192] = {0};

  switch (action) {
  case Action::ProcessStop:
    title = ru ? "ОСТАНОВКА ПРОЦЕССА" : "STOP PROCESS";
    message = ru ? "Нагрев, насос и клапаны будут безопасно отключены. "
                   "Остановить текущий процесс?"
                 : "Heater, pump and valves will be stopped safely. "
                   "Stop the current process?";
    confirm = ru ? "СТОП" : "STOP";
    tone = COLOR_DANGER;
    break;
  case Action::AlarmAcknowledge:
    title = ru ? "ПОДТВЕРЖДЕНИЕ ТРЕВОГИ" : "ACKNOWLEDGE ALARM";
    message = ru ? "Подтвердите, что тревога замечена. Это не сбрасывает "
                   "защиту и не возобновляет процесс."
                 : "Confirm that the alarm was noticed. This does not reset "
                   "safety or resume the process.";
    confirm = ru ? "ПОДТВЕРДИТЬ" : "ACKNOWLEDGE";
    tone = COLOR_DANGER;
    break;
  case Action::MashOperatorWait:
    title = ru ? "ДЕЙСТВИЕ ОПЕРАТОРА" : "OPERATOR ACTION";
    message = state.mashing.operatorAction[0] != '\0'
                  ? state.mashing.operatorAction
                  : (ru ? "Подтвердите переход к следующему шагу затора."
                        : "Confirm transition to the next mash step.");
    confirm = ru ? "ПРОДОЛЖИТЬ" : "CONTINUE";
    break;
  case Action::FractionConfirmation:
    title = ru ? "ПОДТВЕРДИТЕ ТАРУ" : "CONFIRM CONTAINER";
    message = state.fractionProgram.confirmationPrompt[0] != '\0'
                  ? state.fractionProgram.confirmationPrompt
                  : (ru ? "Установите тару для отбора и подтвердите запуск."
                        : "Install the collection container and confirm start.");
    confirm = ru ? "НАЧАТЬ ОТБОР" : "START TAKEOFF";
    tone = COLOR_INFO;
    break;
  case Action::ContainerChange:
    title = ru ? "СМЕНА ТАРЫ" : "CHANGE CONTAINER";
    if (state.rectHeadsContainerLevelReached) {
      message = ru ? "Смените тару голов, перенесите датчик и дождитесь "
                     "снятия его сигнала."
                   : "Change the heads container, move the sensor and wait "
                     "until its signal clears.";
    } else if (state.rectBodyContainerLevelReached) {
      message = ru ? "Смените тару тела, перенесите датчик и дождитесь "
                     "снятия его сигнала."
                   : "Change the body container, move the sensor and wait "
                     "until its signal clears.";
    } else {
      snprintf(dynamicMessage, sizeof(dynamicMessage),
               ru ? "Установите ёмкость %u для тела и подтвердите "
                    "продолжение отбора."
                  : "Install body container %u and confirm takeoff resume.",
               static_cast<unsigned>(state.rectBodyContainerIndex + 1));
      message = dynamicMessage;
    }
    confirm = ru ? "ПРОДОЛЖИТЬ" : "CONTINUE";
    tone = COLOR_WARNING;
    break;
  case Action::ServiceHeaterStart:
    title = ru ? "ТЕСТ ТЭНА" : "HEATER TEST";
    message = ru ? "ТЭН будет включён на сервисном лимите. Убедитесь, что он погружён "
                   "в жидкость и зона безопасна."
                 : "The heater will start at the service limit. Make sure it is submerged "
                   "and the area is safe.";
    confirm = ru ? "ВКЛЮЧИТЬ" : "START";
    tone = COLOR_DANGER;
    break;
  case Action::ServicePauseForTest: {
    const ServiceTestSession::Snapshot session =
        ServiceTestSession::getSnapshot();
    title = ru ? "СЕРВИСНАЯ ПАУЗА" : "SERVICE PAUSE";
    snprintf(dynamicMessage, sizeof(dynamicMessage),
             ru ? "Для теста «%s» процесс будет поставлен на паузу, а выходы "
                  "перейдут в безопасное состояние. Продолжить?"
                : "For the %s test, the process will pause and outputs will "
                  "move to a safe state. Continue?",
             getServiceTestTargetLabel(session.requestedTarget, ru));
    message = dynamicMessage;
    confirm = ru ? "ПАУЗА И ТЕСТ" : "PAUSE AND TEST";
    tone = COLOR_WARNING;
    break;
  }
  case Action::None:
  default:
    return;
  }

  const char *footer = ui.operatorOverlayMessage[0] != '\0'
                           ? ui.operatorOverlayMessage
                           : (ru ? "Повторный тап не отправит вторую команду."
                                 : "A repeated tap cannot send a second command.");
  drawFullscreenOverlay(title, message, tone, footer, 1);

  if (action == Action::ProcessStop ||
      action == Action::ServicePauseForTest) {
    const int16_t gap = 12;
    const int16_t buttonW = (TFT_WIDTH - 80 - gap) / 2;
    drawButton(40, OPERATOR_OVERLAY_BUTTON_Y, buttonW,
               OPERATOR_OVERLAY_BUTTON_H, ru ? "ОТМЕНА" : "CANCEL",
               COLOR_DARK_GREY, TFT_WHITE);
    drawButton(40 + buttonW + gap, OPERATOR_OVERLAY_BUTTON_Y, buttonW,
               OPERATOR_OVERLAY_BUTTON_H, confirm,
               action == Action::ProcessStop ? COLOR_DANGER : tone, TFT_WHITE);
  } else {
    drawButton(120, OPERATOR_OVERLAY_BUTTON_Y, TFT_WIDTH - 240,
               OPERATOR_OVERLAY_BUTTON_H, confirm, tone, TFT_WHITE);
  }
}

static void resumeProcessFromOperatorOverlay() {
  PowerCommandGateway::noteFsmSetpoint(
      PowerCommandGateway::Target::ALL_OUTPUTS,
      PowerCommandGateway::Reason::TFT_RUNTIME_SETPOINT, 1.0f);
  FSM::resume(g_state);
}

static bool handleOperatorOverlayTap(int16_t tx, int16_t ty,
                                     const SystemState &state) {
  using Action = TftOperatorOverlayLogic::Action;
  const Action action = ui.operatorOverlay.action;
  if (action == Action::None) {
    return false;
  }

  if (action == Action::ProcessStop ||
      action == Action::ServicePauseForTest) {
    const int16_t gap = 12;
    const int16_t buttonW = (TFT_WIDTH - 80 - gap) / 2;
    if (hit(tx, ty, 40, OPERATOR_OVERLAY_BUTTON_Y, buttonW,
            OPERATOR_OVERLAY_BUTTON_H)) {
      if (action == Action::ServicePauseForTest) {
        ServiceTestSession::cancel();
        setServiceTestMessage((g_settings.language == 0)
                                  ? "Сервисная пауза отменена оператором."
                                  : "Service pause cancelled by operator.");
      }
      closeOperatorOverlay();
      return true;
    }
    if (!hit(tx, ty, 40 + buttonW + gap, OPERATOR_OVERLAY_BUTTON_Y, buttonW,
             OPERATOR_OVERLAY_BUTTON_H)) {
      return true;
    }
  } else if (!hit(tx, ty, 120, OPERATOR_OVERLAY_BUTTON_Y, TFT_WIDTH - 240,
                  OPERATOR_OVERLAY_BUTTON_H)) {
    return true;
  }

  // A stale second tap is intentionally consumed without touching the FSM,
  // safety policy or command gateway again.
  if (!TftOperatorOverlayLogic::mayDispatch(ui.operatorOverlay)) {
    return true;
  }

  switch (action) {
  case Action::ProcessStop:
    if (TftOperatorOverlayLogic::claimAction(ui.operatorOverlay)) {
      PowerCommandGateway::safeStopAll(
          PowerCommandGateway::Owner::FSM,
          PowerCommandGateway::Reason::TFT_PROCESS_STOP);
      FSM::stopMode(g_state);
      closeOperatorOverlay();
    }
    break;
  case Action::AlarmAcknowledge:
    if (state.currentAlarm.type == AlarmType::NONE ||
        state.currentAlarm.acknowledged) {
      closeOperatorOverlay();
      break;
    }
    if (TftOperatorOverlayLogic::claimAction(ui.operatorOverlay)) {
      Safety::acknowledge(g_state);
      snprintf(ui.safetyActionMessage, sizeof(ui.safetyActionMessage), "%s",
               (g_settings.language == 0) ? "Авария подтверждена оператором."
                                          : "Alarm acknowledged by operator.");
      closeOperatorOverlay();
    }
    break;
  case Action::MashOperatorWait:
    if (!FSM::Mashing::requestManualAdvance(g_state)) {
      snprintf(ui.operatorOverlayMessage, sizeof(ui.operatorOverlayMessage),
               "%s", g_settings.language == 0
                         ? "Ожидание оператора уже не активно."
                         : "Operator wait is no longer active.");
      break;
    }
    if (TftOperatorOverlayLogic::claimAction(ui.operatorOverlay)) {
      resumeProcessFromOperatorOverlay();
      closeOperatorOverlay();
    }
    break;
  case Action::FractionConfirmation:
    if (TftOperatorOverlayLogic::claimAction(ui.operatorOverlay)) {
      (void)FSM::Distillation::confirmFractionProgram(g_state, g_settings);
      closeOperatorOverlay();
    }
    break;
  case Action::ContainerChange:
    if (!FSM::Rectification::confirmContainerChange(g_state, g_settings)) {
      snprintf(ui.operatorOverlayMessage, sizeof(ui.operatorOverlayMessage),
               "%s", g_settings.language == 0
                         ? "Датчик уровня всё ещё активен: продолжение запрещено."
                         : "Level sensor is still active: resume is blocked.");
      break;
    }
    if (TftOperatorOverlayLogic::claimAction(ui.operatorOverlay)) {
      resumeProcessFromOperatorOverlay();
      closeOperatorOverlay();
    }
    break;
  case Action::ServiceHeaterStart:
    if (TftOperatorOverlayLogic::claimAction(ui.operatorOverlay)) {
      if (!beginServiceTestOutput(
              state, ServiceTestSession::Target::HEATER,
              BOARD_SERVICE_TEST_HEATER_TIMEOUT_MS)) {
        closeOperatorOverlay();
        break;
      }
      PowerCommandGateway::setHeaterPower(
          PowerCommandGateway::Owner::SERVICE,
          PowerCommandGateway::Reason::TFT_SERVICE_TEST,
          BOARD_SERVICE_TEST_HEATER_PERCENT);
      snprintf(ui.serviceTestMessage, sizeof(ui.serviceTestMessage), "%s",
               (g_settings.language == 0)
                   ? "ТЭН включён в пределах сервисного лимита; остановите тест после проверки."
                   : "Heater started within the service limit; stop after checking.");
      closeOperatorOverlay();
    }
    break;
  case Action::ServicePauseForTest:
    if (TftOperatorOverlayLogic::claimAction(ui.operatorOverlay)) {
      const ServiceTestSession::Snapshot session =
          ServiceTestSession::getSnapshot();
      const bool sessionMatches =
          session.phase == ServiceTestSession::Phase::REQUESTED &&
          state.mode != Mode::IDLE &&
          session.originalMode == static_cast<uint8_t>(state.mode);
      const bool coldCube = state.temps.valid[TEMP_CUBE] &&
                            state.temps.cube < 45.0f;
      const bool safetyReady = state.safetyOk && !Safety::isLatched(state) &&
                               state.currentAlarm.type == AlarmType::NONE &&
                               !g_settings.demoMode;
      if (!sessionMatches || !coldCube || !safetyReady) {
        PowerCommandGateway::safeStopAll(
            PowerCommandGateway::Owner::SERVICE,
            PowerCommandGateway::Reason::TFT_SERVICE_TEST);
        ServiceTestSession::fault();
        ui.serviceTestOutputActive = false;
        ui.serviceTestDeadlineMs = 0;
        setServiceTestMessage(
            (g_settings.language == 0)
                ? "Сервисная пауза не подтверждена безопасно; возврат заблокирован."
                : "Service pause was not safely confirmed; restore is blocked.");
        closeOperatorOverlay();
        break;
      }
      if (!state.paused) {
        PowerCommandGateway::noteFsmSafeStop(
            PowerCommandGateway::Reason::TFT_PROCESS_PAUSE);
        FSM::pause(g_state);
      }
      PowerCommandGateway::safeStopAll(
          PowerCommandGateway::Owner::SERVICE,
          PowerCommandGateway::Reason::TFT_SERVICE_TEST);
      if (!areServiceTestOutputsSafe() || !ServiceTestSession::markSafePaused()) {
        ServiceTestSession::fault();
        setServiceTestMessage(
            (g_settings.language == 0)
                ? "Не удалось подтвердить безопасную остановку выходов."
                : "Could not confirm safe output stop.");
      } else {
        setServiceTestMessage(
            (g_settings.language == 0)
                ? "Процесс на сервисной паузе. Повторите выбранный тест."
                : "Process is in service pause. Repeat the selected test.");
      }
      closeOperatorOverlay();
    }
    break;
  case Action::None:
  default:
    break;
  }
  return true;
}

static void drawValueTileValueStyled(int16_t x, int16_t y, int16_t w, int16_t h,
                                     const char *value, const char *unit,
                                     uint16_t color, TileValueTone tone,
                                     SparklineBuffer<30> *sparkline = nullptr) {
  const int16_t headerH = getValueTileHeaderHeight(h);
  const int16_t valueAreaY = y + headerH + 2;
  const int16_t valueAreaH = h - headerH - 4;
  if (valueAreaH > 0) {
    tft.fillRect(x + 2, valueAreaY, w - 4, valueAreaH, colorCard());
  }

  const bool timeLike = (value != nullptr && strchr(value, ':') != nullptr);
  uint8_t valueSize = 2;
  switch (tone) {
  case TileValueTone::Primary:
    valueSize = (w >= 145 && h >= 46) ? 3 : ((w >= 110 && h >= 64) ? 3 : 2);
    if (!timeLike && w >= 150 && h >= 72) {
      valueSize = 4;
    }
    break;
  case TileValueTone::Duration:
    valueSize = (w >= 145 && h >= 72) ? 3 : 2;
    break;
  case TileValueTone::Secondary:
    valueSize = (w >= 150 && h >= 84) ? 3 : 2;
    break;
  case TileValueTone::Normal:
  default:
    valueSize =
        (!timeLike && w >= 145 && h >= 46) ? 3 : ((w >= 135 && h >= 64) ? 3 : 2);
    break;
  }

  const int16_t maxValueW =
      (tone == TileValueTone::Primary && h >= 46) ? (w - 14) : (w - 18);
  while (valueSize > 1) {
    tft.setTextSize(valueSize);
    if (displayTextWidth(value) <= maxValueW) {
      break;
    }
    valueSize--;
  }

  tft.setTextColor(color);
  tft.setTextSize(valueSize);

  if (h < 48) {
    const int16_t valueY = y + h / 2;
    const int16_t rightPad = x + w - 4;
    tft.setTextSize(1);
    const int16_t unitW = displayTextWidth(unit);
    tft.setTextColor(tft.color565(100, 100, 100));
    tft.setTextDatum(middle_right);
    drawDisplayString(unit, rightPad, valueY);
    tft.setTextColor(color);
    tft.setTextSize(valueSize);
    drawDisplayString(value, rightPad - unitW - 2, valueY);
  } else {
    tft.setTextDatum(middle_center);
    const int16_t valueX = x + w / 2;
    const int16_t valueY =
        valueAreaY + (valueAreaH / 2) + ((valueSize >= 3) ? 4 : 2);
    drawDisplayString(value, valueX, valueY);
    if (tone == TileValueTone::Primary && valueSize >= 3) {
      drawDisplayString(value, valueX + 1, valueY);
    }

    tft.setTextColor(tft.color565(100, 100, 100));
    tft.setTextSize(1);
    tft.setTextDatum(bottom_right);
    drawDisplayString(unit, x + w - 8, y + h - 8);
  }

  tft.setTextDatum(top_left);

  if (sparkline != nullptr && w > 56 && h > 52) {
    sparkline->draw(tft, x + 8, y + h - 19, w - 44, 12, color);
  }
}

static void drawValueTileValue(int16_t x, int16_t y, int16_t w, int16_t h,
                               const char *value, const char *unit,
                               uint16_t color,
                               SparklineBuffer<30> *sparkline = nullptr) {
  drawValueTileValueStyled(x, y, w, h, value, unit, color,
                           TileValueTone::Normal, sparkline);
}

static void drawValueTile(int16_t x, int16_t y, int16_t w, int16_t h,
                          const char *label, const char *value,
                          const char *unit, uint16_t color,
                          SparklineBuffer<30> *sparkline = nullptr) {
  drawValueTileShell(x, y, w, h, label);
  drawValueTileValue(x, y, w, h, value, unit, color, sparkline);
}

                          static void formatUptimeCompact(
                              uint32_t uptimeSec, char *out, size_t outSize) {
                            const uint32_t h = uptimeSec / 3600UL;
                            const uint32_t m = (uptimeSec % 3600UL) / 60UL;
                            const uint32_t s = uptimeSec % 60UL;
                            snprintf(out, outSize, "%02lu:%02lu:%02lu",
                                     (unsigned long)h, (unsigned long)m,
                                     (unsigned long)s);
                          }

                          static void formatDurationCompact(
                              uint32_t sec, char *out, size_t outSize) {
                            const uint32_t h = sec / 3600UL;
                            const uint32_t m = (sec % 3600UL) / 60UL;
                            const uint32_t s = sec % 60UL;
                            if (h > 0) {
                              snprintf(out, outSize, "%02lu:%02lu:%02lu",
                                       (unsigned long)h, (unsigned long)m,
                                       (unsigned long)s);
                            } else {
                              snprintf(out, outSize, "%02lu:%02lu",
                                       (unsigned long)m, (unsigned long)s);
                            }
                          }

                          static uint32_t getModeRunElapsedSec(
                              const SystemState &state) {
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
                              snprintf(header.status, sizeof(header.status),
                                       "%s", getDisplayModeName(state.mode));
                            } else {
                              snprintf(header.status, sizeof(header.status),
                                       "%s / %s",
                                       getDisplayModeName(state.mode),
                                       getDisplayPhaseName(state));
                            }

                            header.procState =
                                (state.mode == Mode::IDLE)
                                    ? (ru ? "ГОТОВ" : "READY")
                                    : (state.paused ? (ru ? "ПАУЗА" : "PAUSE")
                                                    : (ru ? "РАБОТА" : "RUN"));
                            header.procColor =
                                (state.mode == Mode::IDLE)
                                    ? COLOR_INFO
                                    : (state.paused ? COLOR_WARNING
                                                    : COLOR_SUCCESS);
                            header.safetyState =
                                state.safetyOk ? (ru ? "НОРМА" : "SAFE")
                                               : (ru ? "АВАРИЯ" : "ALARM");
                            header.safetyColor =
                                state.safetyOk ? COLOR_SUCCESS : COLOR_DANGER;

                            const uint32_t phaseElapsedSec =
                                (phaseElapsedOverrideSec == 0xFFFFFFFFUL)
                                    ? FSM::getPhaseElapsedSec()
                                    : phaseElapsedOverrideSec;
                            const uint32_t phaseTargetSec =
                                FSM::getPhaseTargetSec(state, g_settings);
                            header.phaseProgress =
                                FSM::getPhaseProgressPercent(state, g_settings);

                            char elapsedBuf[16];
                            char targetBuf[16];
                            formatDurationCompact(phaseElapsedSec, elapsedBuf,
                                                  sizeof(elapsedBuf));
                            if (phaseTargetSec > 0) {
                              formatDurationCompact(phaseTargetSec, targetBuf,
                                                    sizeof(targetBuf));
                              snprintf(header.timer, sizeof(header.timer),
                                       "%s %s/%s", ru ? "Фаза" : "Phase",
                                       elapsedBuf, targetBuf);
                            } else {
                              snprintf(header.timer, sizeof(header.timer),
                                       "%s %s", ru ? "Фаза" : "Phase",
                                       elapsedBuf);
                            }
                            return header;
                          }

                          static void drawRootScaffold(UiScreen screen) {
                            tft.fillScreen(colorBg());
                            drawHeader(msg(Msg::MONITOR), false);
                            drawTabs(screen);
                            drawCard(ROOT_FRAME_X, ROOT_STATUS_Y, ROOT_FRAME_W,
                                     ROOT_STATUS_H, colorCard());
                            drawCard(ROOT_FRAME_X, ROOT_INFO_Y, ROOT_FRAME_W,
                                     ROOT_INFO_H, colorCard());
                          }

                          static void renderRootStatusBar(
                              const RootHeaderState &header, bool full) {
                            const int16_t statusX = 20;
                            const int16_t statusY = ROOT_STATUS_Y + 5;
                            const int16_t statusW = 300;
                            const int16_t statusH = 34;
                            const int16_t badgeX = 332;
                            const int16_t badgeW = 128;
                            const int16_t badgeH = 14;

                            if (full ||
                                strcmp(g_dashboardCache.status,
                                       header.status) != 0 ||
                                strcmp(g_dashboardCache.phaseTimer,
                                       header.timer) != 0 ||
                                g_dashboardCache.phaseProgress !=
                                    header.phaseProgress) {
                              if (!full) {
                                tft.fillRect(statusX, statusY, statusW, statusH,
                                             colorCard());
                              }
                              tft.setTextColor(colorAccent());
                              tft.setTextSize(1);
                              tft.setFont(&fonts::efontJA_16);
                              tft.setTextDatum(top_left);
                              char statusBuf[64];
                              char timerBuf[32];
                              copyFittedText(header.status, statusW - 6,
                                             statusBuf, sizeof(statusBuf));
                              copyFittedText(header.timer, statusW - 6,
                                             timerBuf, sizeof(timerBuf));
                              drawDisplayString(statusBuf, statusX + 2,
                                             statusY + 1);
                              tft.setTextColor(colorMuted());
                              drawDisplayString(timerBuf, statusX + 2,
                                             statusY + 14);
                              drawProgressBar(
                                  statusX + 2, statusY + 27, statusW - 6, 6,
                                  header.phaseProgress, colorAccent());

                              strncpy(g_dashboardCache.status, header.status,
                                      sizeof(g_dashboardCache.status));
                              g_dashboardCache
                                  .status[sizeof(g_dashboardCache.status) - 1] =
                                  '\0';
                              strncpy(g_dashboardCache.phaseTimer, header.timer,
                                      sizeof(g_dashboardCache.phaseTimer));
                              g_dashboardCache.phaseTimer
                                  [sizeof(g_dashboardCache.phaseTimer) - 1] =
                                  '\0';
                              g_dashboardCache.phaseProgress =
                                  header.phaseProgress;
                            }

                            if (full ||
                                strcmp(g_dashboardCache.processState,
                                       header.procState) != 0 ||
                                strcmp(g_dashboardCache.safetyState,
                                       header.safetyState) != 0) {
                              if (!full) {
                                tft.fillRect(badgeX - 4, ROOT_STATUS_Y + 4,
                                             badgeW + 8, 34, colorCard());
                              }
                              drawStateBadge(badgeX, ROOT_STATUS_Y + 6, badgeW,
                                             badgeH, header.procState,
                                             header.procColor);
                              drawStateBadge(badgeX, ROOT_STATUS_Y + 24, badgeW,
                                             badgeH, header.safetyState,
                                             header.safetyColor);

                              strncpy(g_dashboardCache.processState,
                                      header.procState,
                                      sizeof(g_dashboardCache.processState));
                              g_dashboardCache.processState
                                  [sizeof(g_dashboardCache.processState) - 1] =
                                  '\0';
                              strncpy(g_dashboardCache.safetyState,
                                      header.safetyState,
                                      sizeof(g_dashboardCache.safetyState));
                              g_dashboardCache.safetyState
                                  [sizeof(g_dashboardCache.safetyState) - 1] =
                                  '\0';
                            }
                          }

static void renderRootFooter(
    const char *infoLine, const char *auxLine,
    const char *uptime, bool full) {
                            if (full ||
                                strcmp(g_dashboardCache.infoLine, infoLine) !=
                                    0 ||
                                strcmp(g_dashboardCache.ioLine, auxLine) != 0 ||
                                strcmp(g_dashboardCache.uptime, uptime) != 0) {
                              if (!full) {
                                tft.fillRect(14, ROOT_INFO_Y + 3,
                                             TFT_WIDTH - 28, 34, colorCard());
                              }
                              tft.setTextColor(colorMuted());
                              tft.setTextSize(1);
                              tft.setTextDatum(middle_left);
                              char infoBuf[96];
                              char auxBuf[96];
                              copyFittedText(infoLine, 350, infoBuf,
                                             sizeof(infoBuf));
                              copyFittedText(auxLine, 350, auxBuf,
                                             sizeof(auxBuf));
                              drawDisplayString(infoBuf, 20, ROOT_INFO_Y + 13);
                              tft.setTextColor(tft.color565(96, 104, 114));
                              drawDisplayString(auxBuf, 20, ROOT_INFO_Y + 28);
                              tft.setTextColor(COLOR_PRIMARY);
                              tft.setTextDatum(middle_right);
                              drawDisplayString(uptime, TFT_WIDTH - 18,
                                             ROOT_INFO_Y + 13);

                              strncpy(g_dashboardCache.infoLine, infoLine,
                                      sizeof(g_dashboardCache.infoLine));
                              g_dashboardCache
                                  .infoLine[sizeof(g_dashboardCache.infoLine) -
                                            1] = '\0';
                              strncpy(g_dashboardCache.ioLine, auxLine,
                                      sizeof(g_dashboardCache.ioLine));
                              g_dashboardCache
                                  .ioLine[sizeof(g_dashboardCache.ioLine) - 1] =
                                  '\0';
                              strncpy(g_dashboardCache.uptime, uptime,
                                      sizeof(g_dashboardCache.uptime));
                              g_dashboardCache
                                  .uptime[sizeof(g_dashboardCache.uptime) - 1] =
                                  '\0';
                            }
                          }

                          static void resetModeMonitorRuntimeCache() {
                            memset(g_modeTileCache, 0, sizeof(g_modeTileCache));
                            g_dashboardCache.infoLine[0] = '\0';
                            g_dashboardCache.ioLine[0] = '\0';
                            g_dashboardCache.uptime[0] = '\0';
                          }

                          static void setModeMonitorAccessInfoLine(
                              char *infoLine, size_t infoLineSize, Mode mode) {
                            snprintf(infoLine, infoLineSize, "%s",
                                     getModeMonitorAccessHint(mode));
                          }

                          // The idle root is intentionally a menu, not a
                          // second monitor. Runtime telemetry belongs to
                          // UI_MODE_MONITOR after a process starts.
                          static void renderHome(const SystemState &state,
                                                 bool full) {
                            if (!full) {
                              return;
                            }

                            const bool ru = (g_settings.language == 0);
                            tft.fillScreen(colorBg());
                            drawHeader(ru ? "ГЛАВНАЯ" : "HOME", false);
                            drawTabs(UI_DASHBOARD);

                            drawCard(HOME_TILE_MARGIN_X,
                                     UI_GLOBAL_STATUS_H + 8,
                                     TFT_WIDTH - HOME_TILE_MARGIN_X * 2, 34,
                                     colorCard());
                            tft.setTextSize(1);
                            tft.setFont(&fonts::efontJA_16);
                            tft.setTextColor(colorAccent());
                            tft.setTextDatum(middle_left);
                            drawDisplayString("SMART COLUMN S3",
                                              HOME_TILE_MARGIN_X + 12,
                                              UI_GLOBAL_STATUS_H + 19);
                            tft.setTextColor(colorMuted());
                            tft.setTextDatum(middle_left);
                            drawDisplayString(
                                ru ? "Локальная панель" : "Local HMI",
                                HOME_TILE_MARGIN_X + 174,
                                UI_GLOBAL_STATUS_H + 19);
                            tft.setTextDatum(top_left);

                            drawButton(
                                HOME_SAFETY_X, HOME_SAFETY_Y, HOME_SAFETY_W,
                                HOME_SAFETY_H,
                                state.safetyOk && !Safety::isLatched(state)
                                    ? (ru ? "SAFETY: OK" : "SAFETY: OK")
                                    : (ru ? "SAFETY: ТРЕВОГА"
                                          : "SAFETY: ALARM"),
                                state.safetyOk && !Safety::isLatched(state)
                                    ? COLOR_SUCCESS
                                    : COLOR_DANGER,
                                TFT_WHITE);

                            drawButton(HOME_TILE_X1, HOME_TILE_Y1, HOME_TILE_W,
                                       HOME_TILE_H,
                                       ru ? "РЕЖИМЫ" : "MODES", colorAccent(),
                                       TFT_WHITE);
                            drawButton(HOME_TILE_X2, HOME_TILE_Y1, HOME_TILE_W,
                                       HOME_TILE_H,
                                       ru ? "НАСТРОЙКИ" : "SETTINGS", COLOR_INFO,
                                       TFT_WHITE);
                            drawButton(HOME_TILE_X1, HOME_TILE_Y2, HOME_TILE_W,
                                       HOME_TILE_H,
                                       ru ? "ТЕМПЕРАТУРЫ" : "TEMPERATURES",
                                       COLOR_SUCCESS, TFT_WHITE);
                            drawButton(HOME_TILE_X2, HOME_TILE_Y2, HOME_TILE_W,
                                       HOME_TILE_H,
                                       ru ? "СЕРВИС" : "SERVICE", COLOR_DARK_GREY,
                                       TFT_WHITE);

                            drawFooterHint(
                                ru ? "Выберите раздел для работы с установкой"
                                   : "Choose a section to operate the system",
                                colorAccent());
                          }

                          static void renderDashboard(const SystemState &state,
                                                      bool full) {
                            const bool ru = (g_settings.language == 0);
                            RootHeaderState header =
                                buildRootHeaderState(state);
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
                            const int16_t row2Y =
                                ROOT_PANEL_Y + tileH + ROOT_GRID_ROW_GAP;
                            const int16_t row3Y =
                                ROOT_PANEL_Y + (tileH + ROOT_GRID_ROW_GAP) * 2;
                            const int16_t x1 = ROOT_RIGHT_X;
                            const int16_t x2 =
                                ROOT_RIGHT_X + tileW + ROOT_GRID_COL_GAP;
                            const int16_t infoY = ROOT_INFO_Y;
                            const bool hasWaterIn =
                                state.temps.valid[TEMP_WATER_IN];
                            const bool hasWaterOut =
                                state.temps.valid[TEMP_WATER_OUT];

                            enum DashboardProfile : uint8_t {
                              DASH_PROFILE_IDLE = 0,
                              DASH_PROFILE_RECT = 1,
                              DASH_PROFILE_DIST = 2,
                              DASH_PROFILE_NBK = 3,
                              DASH_PROFILE_FERM = 4,
                              DASH_PROFILE_MANUAL = 5,
                              DASH_PROFILE_MASH = 6,
                              DASH_PROFILE_HOLD = 7,
                              DASH_PROFILE_GENERIC = 8
                            };
                            DashboardProfile profile = DASH_PROFILE_GENERIC;
                            if (state.mode == Mode::IDLE) {
                              profile = DASH_PROFILE_IDLE;
                            } else if (state.mode == Mode::RECTIFICATION) {
                              profile = DASH_PROFILE_RECT;
                            } else if (state.mode == Mode::DISTILLATION) {
                              profile = DASH_PROFILE_DIST;
                            } else if (state.mode == Mode::NBK) {
                              profile = DASH_PROFILE_NBK;
                            } else if (state.mode == Mode::FERMENTATION) {
                              profile = DASH_PROFILE_FERM;
                            } else if (state.mode == Mode::MANUAL_RECT) {
                              profile = DASH_PROFILE_MANUAL;
                            } else if (state.mode == Mode::MASHING) {
                              profile = DASH_PROFILE_MASH;
                            } else if (state.mode == Mode::HOLD) {
                              profile = DASH_PROFILE_HOLD;
                            }

                            const uint8_t layoutKey = static_cast<uint8_t>(
                                (static_cast<uint8_t>(profile) << 2) |
                                (hasWaterIn ? 0x01 : 0x00) |
                                (hasWaterOut ? 0x02 : 0x00));

                            if (full) {
                              tft.fillScreen(colorBg());
                              drawHeader(msg(Msg::MONITOR), false);
                              drawTabs(UI_DASHBOARD);
                              drawCard(ROOT_FRAME_X, barY, ROOT_FRAME_W, 44,
                                       colorCard());
                              drawCard(ROOT_FRAME_X, infoY, ROOT_FRAME_W, 40,
                                       colorCard());
                              memset(&g_dashboardCache, 0,
                                     sizeof(g_dashboardCache));
                              g_dashboardCache.layoutKey = 0xFF;
                            }

                            char statusBuf[64];
                            snprintf(statusBuf, sizeof(statusBuf), "%s / %s",
                                     getDisplayModeName(state.mode),
                                     getDisplayPhaseName(state));

                            const char *procState =
                                (state.mode == Mode::IDLE)
                                    ? (ru ? "ОЖИД." : "IDLE")
                                    : (state.paused ? (ru ? "ПАУЗА" : "PAUSE")
                                                    : (ru ? "РАБОТА" : "RUN"));
                            const uint16_t procColor =
                                (state.mode == Mode::IDLE)
                                    ? COLOR_INFO
                                    : (state.paused ? COLOR_WARNING
                                                    : COLOR_SUCCESS);
                            const char *safetyState =
                                state.safetyOk ? (ru ? "БЕЗОП." : "SAFE")
                                               : (ru ? "ТРЕВОГА" : "ALARM");
                            const uint16_t safetyColor =
                                state.safetyOk ? COLOR_SUCCESS : COLOR_DANGER;

                            const uint32_t phaseElapsedSec =
                                FSM::getPhaseElapsedSec();
                            const uint32_t phaseTargetSec =
                                FSM::getPhaseTargetSec(state, g_settings);
                            const uint8_t phaseProgress =
                                FSM::getPhaseProgressPercent(state, g_settings);
                            char elapsedBuf[16];
                            char targetBuf[16];
                            char timerBuf[32];
                            formatDurationCompact(phaseElapsedSec, elapsedBuf,
                                                  sizeof(elapsedBuf));
                            if (phaseTargetSec > 0) {
                              formatDurationCompact(phaseTargetSec, targetBuf,
                                                    sizeof(targetBuf));
                              snprintf(timerBuf, sizeof(timerBuf), "%s %s/%s",
                                       ru ? "Фаза" : "Phase", elapsedBuf,
                                       targetBuf);
                            } else {
                              snprintf(timerBuf, sizeof(timerBuf), "%s %s",
                                       ru ? "Фаза" : "Phase", elapsedBuf);
                            }

                            if (full ||
                                strcmp(g_dashboardCache.status, statusBuf) !=
                                    0 ||
                                strcmp(g_dashboardCache.phaseTimer, timerBuf) !=
                                    0 ||
                                g_dashboardCache.phaseProgress !=
                                    phaseProgress) {
                              if (!full) {
                                tft.fillRect(statusX, statusY, statusW, statusH,
                                             colorCard());
                              }
                              tft.setTextColor(colorAccent());
                              tft.setTextSize(1);
                              tft.setFont(&fonts::efontJA_16);
                              tft.setTextDatum(top_left);
                              drawDisplayString(statusBuf, statusX + 2,
                                             statusY + 1);
                              tft.setTextColor(tft.color565(120, 130, 140));
                              drawDisplayString(timerBuf, statusX + 2,
                                             statusY + 14);

                              const int16_t pbX = statusX + 2;
                              const int16_t pbY = statusY + 27;
                              const int16_t pbW = statusW - 6;
                              const int16_t pbH = 6;
                              drawProgressBar(pbX, pbY, pbW, pbH, phaseProgress,
                                              colorAccent());
                              tft.setFont(&fonts::efontJA_16);
                              tft.setTextDatum(top_left);
                              strncpy(g_dashboardCache.status, statusBuf,
                                      sizeof(g_dashboardCache.status));
                              g_dashboardCache
                                  .status[sizeof(g_dashboardCache.status) - 1] =
                                  '\0';
                              strncpy(g_dashboardCache.phaseTimer, timerBuf,
                                      sizeof(g_dashboardCache.phaseTimer));
                              g_dashboardCache.phaseTimer
                                  [sizeof(g_dashboardCache.phaseTimer) - 1] =
                                  '\0';
                              g_dashboardCache.phaseProgress = phaseProgress;
                            }

                            if (full ||
                                strcmp(g_dashboardCache.processState,
                                       procState) != 0 ||
                                strcmp(g_dashboardCache.safetyState,
                                       safetyState) != 0) {
                              if (!full) {
                                tft.fillRect(badgeX - 4, barY + 4, badgeW + 8,
                                             34, colorCard());
                              }

                              drawStateBadge(badgeX, barY + 6, badgeW, badgeH,
                                             procState, procColor);
                              drawStateBadge(badgeX, barY + 24, badgeW, badgeH,
                                             safetyState, safetyColor);

                              strncpy(g_dashboardCache.processState, procState,
                                      sizeof(g_dashboardCache.processState));
                              g_dashboardCache.processState
                                  [sizeof(g_dashboardCache.processState) - 1] =
                                  '\0';
                              strncpy(g_dashboardCache.safetyState, safetyState,
                                      sizeof(g_dashboardCache.safetyState));
                              g_dashboardCache.safetyState
                                  [sizeof(g_dashboardCache.safetyState) - 1] =
                                  '\0';
                            }

                            const bool layoutChanged =
                                full ||
                                (g_dashboardCache.layoutKey != layoutKey);
                            if (layoutChanged && !full) {
                              requestRedraw(DisplayRedrawReason::LAYOUT_CHANGED);
                            }
                            const int16_t tileX[6] = {x1, x2, x1, x2, x1, x2};
                            const int16_t tileY[6] = {row1Y, row1Y, row2Y,
                                                      row2Y, row3Y, row3Y};
                            const char *tileLabels[6] = {nullptr, nullptr,
                                                         nullptr, nullptr,
                                                         nullptr, nullptr};
                            const char *tileUnits[6] = {"°C",
                                                        "°C",
                                                        "°C",
                                                        "°C",
                                                        msg(Msg::UNIT_W),
                                                        msg(Msg::UNIT_ML_H)};
                            uint16_t tileColors[6] = {
                                COLOR_DANGER,  colorAccent(), COLOR_INFO,
                                COLOR_WARNING, COLOR_WARNING, COLOR_SUCCESS};
                            char tileValues[6][16] = {};

                            tileLabels[0] = msg(Msg::CUBE_TEMP);
                            tileLabels[1] = msg(Msg::TOP_T);
                            tileLabels[2] = msg(Msg::REFLUX_T);
                            tileLabels[3] = msg(Msg::TSA_T);

                            snprintf(tileValues[0], sizeof(tileValues[0]),
                                     "%.1f", state.temps.cube);
                            snprintf(tileValues[1], sizeof(tileValues[1]),
                                     "%.1f", state.temps.columnTop);
                            snprintf(tileValues[2], sizeof(tileValues[2]),
                                     "%.1f", state.temps.reflux);
                            snprintf(tileValues[3], sizeof(tileValues[3]),
                                     "%.1f", state.temps.tsa);

                            if (profile == DASH_PROFILE_IDLE) {
                              tileLabels[4] = hasWaterIn
                                                  ? (ru ? "ОХЛ ВХ" : "WATER IN")
                                                  : (ru ? "СЕТЬ" : "MAINS");
                              tileLabels[5] =
                                  hasWaterOut ? (ru ? "ОХЛ ВЫХ" : "WATER OUT")
                                              : (ru ? "МОЩНОСТЬ" : "POWER");
                              if (hasWaterIn) {
                                snprintf(tileValues[4], sizeof(tileValues[4]),
                                         "%.1f", state.temps.waterIn);
                                tileUnits[4] = "°C";
                                tileColors[4] = COLOR_INFO;
                              } else {
                                snprintf(tileValues[4], sizeof(tileValues[4]),
                                         "%.0f", state.power.voltage);
                                tileUnits[4] = "V";
                                tileColors[4] = COLOR_PRIMARY;
                              }
                              if (hasWaterOut) {
                                snprintf(tileValues[5], sizeof(tileValues[5]),
                                         "%.1f", state.temps.waterOut);
                                tileUnits[5] = "°C";
                                tileColors[5] = COLOR_INFO;
                              } else {
                                snprintf(tileValues[5], sizeof(tileValues[5]),
                                         "%.0f", state.power.power);
                                tileUnits[5] = msg(Msg::UNIT_W);
                                tileColors[5] = COLOR_WARNING;
                              }
                            } else if (profile == DASH_PROFILE_RECT) {
                              tileLabels[4] = msg(Msg::HEATER_POWER);
                              snprintf(tileValues[4], sizeof(tileValues[4]),
                                       "%.0f", state.power.power);
                              if (hasWaterOut) {
                                tileLabels[5] = ru ? "ОХЛ ВЫХ" : "WATER OUT";
                                snprintf(tileValues[5], sizeof(tileValues[5]),
                                         "%.1f", state.temps.waterOut);
                                tileUnits[5] = "°C";
                                tileColors[5] = COLOR_INFO;
                              } else {
                                tileLabels[5] = ru ? "ОТБОР" : "TAKEOFF";
                                snprintf(tileValues[5], sizeof(tileValues[5]),
                                         "%.0f", state.pump.speedMlPerHour);
                                tileUnits[5] = msg(Msg::UNIT_ML_H);
                                tileColors[5] = COLOR_SUCCESS;
                              }
                            } else if (profile == DASH_PROFILE_DIST) {
                              tileLabels[4] = msg(Msg::HEATER_POWER);
                              tileLabels[5] = ru ? "ОТБОР" : "COLLECT";
                              snprintf(tileValues[4], sizeof(tileValues[4]),
                                       "%.0f", state.power.power);
                              snprintf(tileValues[5], sizeof(tileValues[5]),
                                       "%.0f", state.pump.totalVolumeMl);
                              tileUnits[5] = "ml";
                              tileColors[5] = COLOR_SUCCESS;
                            } else if (profile == DASH_PROFILE_NBK) {
                              tileLabels[4] = ru ? "ДАВЛ." : "PRESS";
                              tileLabels[5] = hasWaterOut
                                                  ? (ru ? "ОХЛ ВЫХ" : "WATER OUT")
                                                  : (ru ? "ПОДАЧА" : "FEED");
                              snprintf(tileValues[4], sizeof(tileValues[4]),
                                       "%.0f", state.pressure.cube);
                              tileUnits[4] = "mm";
                              tileColors[4] = COLOR_WARNING;
                              if (hasWaterOut) {
                                snprintf(tileValues[5], sizeof(tileValues[5]),
                                         "%.1f", state.temps.waterOut);
                                tileUnits[5] = "°C";
                                tileColors[5] = COLOR_INFO;
                              } else {
                                snprintf(tileValues[5], sizeof(tileValues[5]),
                                         "%.0f", g_settings.nbk.pumpSpeedMlH);
                                tileUnits[5] = msg(Msg::UNIT_ML_H);
                                tileColors[5] = COLOR_SUCCESS;
                              }
                            } else if (profile == DASH_PROFILE_FERM) {
                              tileLabels[4] = ru ? "ДОПУСК" : "BAND";
                              tileLabels[5] = ru ? "DELTA" : "DELTA";
                              snprintf(tileValues[4], sizeof(tileValues[4]),
                                       "%.1f", g_settings.fermentation.hysteresisC);
                              snprintf(tileValues[5], sizeof(tileValues[5]),
                                       "%.1f",
                                       state.temps.cube -
                                           g_settings.fermentation.targetTempC);
                              tileUnits[4] = "°C";
                              tileUnits[5] = "°C";
                              tileColors[4] = COLOR_INFO;
                              tileColors[5] =
                                  (state.temps.cube >
                                   (g_settings.fermentation.targetTempC +
                                    g_settings.fermentation.hysteresisC))
                                      ? COLOR_WARNING
                                      : COLOR_SUCCESS;
                            } else if (profile == DASH_PROFILE_MANUAL) {
                              tileLabels[4] = msg(Msg::HEATER_POWER);
                              tileLabels[5] = ru ? "ОТБОР" : "TAKEOFF";
                              snprintf(tileValues[4], sizeof(tileValues[4]),
                                       "%.0f", state.power.power);
                              snprintf(tileValues[5], sizeof(tileValues[5]),
                                       "%.0f", state.pump.speedMlPerHour);
                              tileUnits[5] = msg(Msg::UNIT_ML_H);
                              tileColors[4] = COLOR_WARNING;
                              tileColors[5] = COLOR_SUCCESS;
                            } else if (profile == DASH_PROFILE_MASH) {
                              tileLabels[4] = msg(Msg::HEATER_POWER);
                              tileLabels[5] = ru ? "МЕШАЛКА" : "STIRRER";
                              snprintf(tileValues[4], sizeof(tileValues[4]),
                                       "%.0f", state.power.power);
                              if (state.stirrer.running) {
                                snprintf(tileValues[5], sizeof(tileValues[5]),
                                         "%u",
                                         static_cast<unsigned>(
                                             state.stirrer.speedPercent));
                                tileUnits[5] = "%";
                                tileColors[5] = COLOR_SUCCESS;
                              } else {
                                snprintf(tileValues[5], sizeof(tileValues[5]),
                                         "%s", ru ? "OFF" : "OFF");
                                tileUnits[5] = "";
                                tileColors[5] = colorMuted();
                              }
                              tileColors[4] = COLOR_WARNING;
                            } else if (profile == DASH_PROFILE_HOLD) {
                              tileLabels[4] = msg(Msg::HEATER_POWER);
                              tileLabels[5] = ru ? "КОНТУР" : "LOOP";
                              snprintf(tileValues[4], sizeof(tileValues[4]),
                                       "%.0f", state.power.power);
                              snprintf(tileValues[5], sizeof(tileValues[5]),
                                       "%s",
                                       (state.hold.active &&
                                        state.hold.currentStep <
                                            state.hold.stepCount &&
                                        state.hold.steps[state.hold.currentStep]
                                            .useCooling)
                                           ? (ru ? "ОХЛ." : "COOL")
                                           : (ru ? "НАГР." : "HEAT"));
                              tileUnits[5] = "";
                              tileColors[4] = COLOR_WARNING;
                              tileColors[5] =
                                  (state.hold.active &&
                                   state.hold.currentStep < state.hold.stepCount &&
                                   state.hold.steps[state.hold.currentStep]
                                       .useCooling)
                                      ? COLOR_INFO
                                      : COLOR_WARNING;
                            } else {
                              tileLabels[4] = msg(Msg::HEATER_POWER);
                              tileLabels[5] =
                                  hasWaterOut ? (ru ? "ОХЛ ВЫХ" : "WATER OUT")
                                              : msg(Msg::PUMP);
                              snprintf(tileValues[4], sizeof(tileValues[4]),
                                       "%.0f", state.power.power);
                              if (hasWaterOut) {
                                snprintf(tileValues[5], sizeof(tileValues[5]),
                                         "%.1f", state.temps.waterOut);
                                tileUnits[5] = "°C";
                                tileColors[5] = COLOR_INFO;
                              } else {
                                snprintf(tileValues[5], sizeof(tileValues[5]),
                                         "%.0f", state.pump.speedMlPerHour);
                                tileUnits[5] = msg(Msg::UNIT_ML_H);
                                tileColors[5] = COLOR_SUCCESS;
                              }
                            }

                            if (layoutChanged) {
                              drawCard(ROOT_LEFT_X, ROOT_PANEL_Y, ROOT_LEFT_W,
                                       ROOT_PANEL_H, colorCard());
                              for (uint8_t i = 0; i < 6; i++) {
                                drawValueTileShell(tileX[i], tileY[i], tileW,
                                                   tileH, tileLabels[i]);
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

                            char *tileCache[6] = {
                                g_dashboardCache.cube,   g_dashboardCache.top,
                                g_dashboardCache.reflux, g_dashboardCache.tsa,
                                g_dashboardCache.power,  g_dashboardCache.pump};

                            for (uint8_t i = 0; i < 6; i++) {
                              if (full ||
                                  strcmp(tileCache[i], tileValues[i]) != 0) {
                                SparklineBuffer<30> *spark = nullptr;
                                if (i == 0)
                                  spark = &cubeTempHistory;
                                else if (i == 1)
                                  spark = &topTempHistory;
                                drawValueTileValueStyled(
                                    tileX[i], tileY[i], tileW, tileH,
                                    tileValues[i], tileUnits[i], tileColors[i],
                                    TileValueTone::Primary, spark);
                                strncpy(tileCache[i], tileValues[i], 15);
                                tileCache[i][15] = '\0';
                              }
                            }

                            char infoBuf[96];
                            if (profile == DASH_PROFILE_IDLE) {
                              snprintf(
                                  infoBuf, sizeof(infoBuf), "%s",
                                  ru ? "Ожидание: выберите режим в Управлении"
                                     : "Idle: choose mode in Control");
                            } else if (profile == DASH_PROFILE_DIST) {
                              snprintf(infoBuf, sizeof(infoBuf),
                                       ru ? "Дист.: %.0f/%.0f мл | Гол %.0f"
                                          : "Dist: %.0f/%.0f ml | Heads %.0f",
                                       state.pump.totalVolumeMl,
                                       distUi.targetVolumeMl,
                                       distUi.headsVolumeMl);
                            } else if (profile == DASH_PROFILE_NBK) {
                              snprintf(infoBuf, sizeof(infoBuf),
                                       ru ? "НБК: %.0f/%.0f мл | Подача %.0f"
                                          : "NBK: %.0f/%.0f ml | Feed %.0f",
                                       state.pump.totalVolumeMl,
                                       g_settings.nbk.targetVolumeMl,
                                       g_settings.nbk.pumpSpeedMlH);
                            } else if (profile == DASH_PROFILE_FERM) {
                              snprintf(infoBuf, sizeof(infoBuf),
                                       ru ? "Брож.: цель %.1f | dT %.1f"
                                          : "Ferm: target %.1f | dT %.1f",
                                       g_settings.fermentation.targetTempC,
                                       state.temps.cube -
                                           g_settings.fermentation.targetTempC);
                            } else if (profile == DASH_PROFILE_MANUAL) {
                              snprintf(infoBuf, sizeof(infoBuf),
                                       ru ? "Ручн.: тело %.0f/%.0f | скор. %.0f"
                                          : "Manual: body %.0f/%.0f | speed %.0f",
                                       state.stats.bodyVolume,
                                       manualRectUi.bodyTargetMl,
                                       manualRectUi.speedMlH);
                            } else if (profile == DASH_PROFILE_MASH) {
                              const uint8_t steps =
                                  (state.mashing.stepCount > 0)
                                      ? state.mashing.stepCount
                                      : mashProfileDefault.stepCount;
                              snprintf(infoBuf, sizeof(infoBuf),
                                       ru ? "Затирка: шаг %u/%u | цель %.1f"
                                          : "Mash: step %u/%u | target %.1f",
                                       static_cast<unsigned>(
                                           steps == 0 ? 0
                                                      : state.mashing.currentStep + 1),
                                       static_cast<unsigned>(steps),
                                       state.mashing.targetTemp);
                            } else if (profile == DASH_PROFILE_HOLD) {
                              const uint8_t steps =
                                  (state.hold.stepCount > 0) ? state.hold.stepCount
                                                             : holdStepsCount;
                              snprintf(infoBuf, sizeof(infoBuf),
                                       ru ? "Выдержка: шаг %u/%u | цель %.1f"
                                          : "Hold: step %u/%u | target %.1f",
                                       static_cast<unsigned>(
                                           steps == 0 ? 0
                                                      : state.hold.currentStep + 1),
                                       static_cast<unsigned>(steps),
                                       state.hold.targetTemp);
                            } else if (ru) {
                              snprintf(infoBuf, sizeof(infoBuf),
                                       "Гол %.0f | Тело %.0f | Хв %.0f мл",
                                       state.stats.headsVolume,
                                       state.stats.bodyVolume,
                                       state.stats.tailsVolume);
                            } else {
                              snprintf(infoBuf, sizeof(infoBuf),
                                       "Heads %.0f | Body %.0f | Tails %.0f ml",
                                       state.stats.headsVolume,
                                       state.stats.bodyVolume,
                                       state.stats.tailsVolume);
                            }

                            const char *k1 = Valves::getWater() ? "ON" : "--";
                            const char *k2 = Valves::getHeads() ? "ON" : "--";
                            const char *k3 =
                                (Heater::getPower() > 0) ? "ON" : "--";
                            char waterBuf[24];
                            if (state.temps.valid[TEMP_WATER_IN] &&
                                state.temps.valid[TEMP_WATER_OUT]) {
                              snprintf(waterBuf, sizeof(waterBuf), "%.1f/%.1f",
                                       state.temps.waterIn,
                                       state.temps.waterOut);
                            } else if (state.temps.valid[TEMP_WATER_OUT]) {
                              snprintf(waterBuf, sizeof(waterBuf), "--/%.1f",
                                       state.temps.waterOut);
                            } else if (state.temps.valid[TEMP_WATER_IN]) {
                              snprintf(waterBuf, sizeof(waterBuf), "%.1f/--",
                                       state.temps.waterIn);
                            } else {
                              strncpy(waterBuf, "--/--", sizeof(waterBuf));
                              waterBuf[sizeof(waterBuf) - 1] = '\0';
                            }
                            char ioBuf[96];
                            if (profile == DASH_PROFILE_IDLE) {
                              snprintf(ioBuf, sizeof(ioBuf),
                                       "W %s | V %.0f | P %.0f", waterBuf,
                                       state.power.voltage,
                                       state.pressure.cube);
                            } else {
                              snprintf(
                                  ioBuf, sizeof(ioBuf),
                                  "W %s | V %.0f | P %.0f | K1%s K2%s K3%s",
                                  waterBuf, state.power.voltage,
                                  state.pressure.cube, k1, k2, k3);
                            }

                            char upBuf[16];
                            formatUptimeCompact(state.uptime, upBuf,
                                                sizeof(upBuf));

                            drawCard(ROOT_LEFT_X, ROOT_PANEL_Y, ROOT_LEFT_W,
                                     ROOT_PANEL_H, colorCard());
                            drawPanelHeader(ROOT_LEFT_X, ROOT_PANEL_Y,
                                            ROOT_LEFT_W, header.procState,
                                            header.procColor);
                            {
                              const int16_t summaryX = ROOT_LEFT_X + 8;
                              const int16_t summaryW = ROOT_LEFT_W - 16;
                              const int16_t heroY = ROOT_PANEL_Y + 32;
                              const int16_t heroH = 42;
                              const int16_t rowY1 = ROOT_PANEL_Y + 86;
                              const int16_t rowY2 = ROOT_PANEL_Y + 104;
                              const int16_t rowY3 = ROOT_PANEL_Y + 122;
                              char rowBuf[24];

                              if (profile == DASH_PROFILE_IDLE) {
                                char mainsBuf[24];
                                char pressBuf[24];
                                snprintf(mainsBuf, sizeof(mainsBuf), "%.0f V",
                                         state.power.voltage);
                                snprintf(pressBuf, sizeof(pressBuf), "%.0f mm",
                                         state.pressure.cube);
                                drawSummaryHeroBlock(
                                    summaryX, heroY, summaryW, heroH,
                                    ru ? "ДАЛЕЕ" : "NEXT",
                                    ru ? "УПРАВЛ." : "CONTROL", colorAccent());
                                drawCompactKeyValueRow(summaryX, rowY1,
                                                       summaryW,
                                                       ru ? "СЕТЬ" : "MAINS",
                                                       mainsBuf, COLOR_PRIMARY);
                                drawCompactKeyValueRow(summaryX, rowY2,
                                                       summaryW,
                                                       ru ? "ДАВЛ." : "PRESS",
                                                       pressBuf, COLOR_WARNING);
                                drawCompactKeyValueRow(
                                    summaryX, rowY3, summaryW,
                                    ru ? "ОХЛ." : "COOL", waterBuf, COLOR_INFO);
                              } else if (profile == DASH_PROFILE_DIST) {
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f/%.0f ml",
                                         state.pump.totalVolumeMl,
                                         distUi.targetVolumeMl);
                                drawSummaryHeroBlock(summaryX, heroY, summaryW,
                                                     heroH,
                                                     ru ? "ОТБОР" : "COLLECT",
                                                     rowBuf, COLOR_SUCCESS);
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f ml",
                                         distUi.headsVolumeMl);
                                drawCompactKeyValueRow(summaryX, rowY1, summaryW,
                                                       ru ? "ГОЛОВЫ" : "HEADS",
                                                       rowBuf, COLOR_WARNING);
                                snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
                                         distUi.endTempC);
                                drawCompactKeyValueRow(summaryX, rowY2, summaryW,
                                                       ru ? "ФИНИШ" : "END",
                                                       rowBuf, COLOR_INFO);
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f mm",
                                         state.pressure.cube);
                                drawCompactKeyValueRow(summaryX, rowY3, summaryW,
                                                       ru ? "ДАВЛ." : "PRESS",
                                                       rowBuf, COLOR_WARNING);
                              } else if (profile == DASH_PROFILE_NBK) {
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f/%.0f ml",
                                         state.pump.totalVolumeMl,
                                         g_settings.nbk.targetVolumeMl);
                                drawSummaryHeroBlock(summaryX, heroY, summaryW,
                                                     heroH,
                                                     ru ? "ОТБОР" : "COLLECT",
                                                     rowBuf, COLOR_SUCCESS);
                                snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
                                         g_settings.nbk.columnBottomTempThresholdC);
                                drawCompactKeyValueRow(summaryX, rowY1, summaryW,
                                                       ru ? "ПОРОГ" : "THRESH",
                                                       rowBuf, COLOR_WARNING);
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f ml/h",
                                         g_settings.nbk.pumpSpeedMlH);
                                drawCompactKeyValueRow(summaryX, rowY2, summaryW,
                                                       ru ? "ПОДАЧА" : "FEED",
                                                       rowBuf, COLOR_INFO);
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f V",
                                         state.power.voltage);
                                drawCompactKeyValueRow(summaryX, rowY3, summaryW,
                                                       ru ? "СЕТЬ" : "MAINS",
                                                       rowBuf, COLOR_PRIMARY);
                              } else if (profile == DASH_PROFILE_FERM) {
                                const float delta =
                                    state.temps.cube -
                                    g_settings.fermentation.targetTempC;
                                snprintf(rowBuf, sizeof(rowBuf), "%.1f/%.1f C",
                                         state.temps.cube,
                                         g_settings.fermentation.targetTempC);
                                drawSummaryHeroBlock(summaryX, heroY, summaryW,
                                                     heroH,
                                                     ru ? "ТЕМП." : "TEMP",
                                                     rowBuf,
                                                     fabsf(delta) >
                                                             g_settings.fermentation.hysteresisC
                                                         ? COLOR_WARNING
                                                         : COLOR_SUCCESS);
                                snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
                                         g_settings.fermentation.hysteresisC);
                                drawCompactKeyValueRow(summaryX, rowY1, summaryW,
                                                       ru ? "ДОПУСК" : "BAND",
                                                       rowBuf, COLOR_INFO);
                                snprintf(rowBuf, sizeof(rowBuf), "%.1f C", delta);
                                drawCompactKeyValueRow(summaryX, rowY2, summaryW,
                                                       ru ? "DELTA" : "DELTA",
                                                       rowBuf,
                                                       (delta > 0.0f) ? COLOR_WARNING
                                                                      : COLOR_SUCCESS);
                                drawCompactKeyValueRow(summaryX, rowY3, summaryW,
                                                       ru ? "НАГРЕВ" : "HEATER",
                                                       g_settings.fermentation.useHeater
                                                           ? "ON"
                                                           : "OFF",
                                                       g_settings.fermentation.useHeater
                                                           ? COLOR_SUCCESS
                                                           : colorMuted());
                              } else if (profile == DASH_PROFILE_MANUAL) {
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f ml/h",
                                         state.pump.speedMlPerHour);
                                drawSummaryHeroBlock(summaryX, heroY, summaryW,
                                                     heroH,
                                                     ru ? "ОТБОР" : "TAKEOFF",
                                                     rowBuf, COLOR_SUCCESS);
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f %%", 
                                         manualRectUi.powerPercent);
                                drawCompactKeyValueRow(summaryX, rowY1, summaryW,
                                                       ru ? "МОЩН." : "POWER",
                                                       rowBuf, COLOR_WARNING);
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f/%.0f ml",
                                         state.stats.bodyVolume,
                                         manualRectUi.bodyTargetMl);
                                drawCompactKeyValueRow(summaryX, rowY2, summaryW,
                                                       ru ? "ТЕЛО" : "BODY",
                                                       rowBuf, COLOR_PRIMARY);
                                snprintf(rowBuf, sizeof(rowBuf), "H %.0f T %.0f",
                                         state.stats.headsVolume,
                                         state.stats.tailsVolume);
                                drawCompactKeyValueRow(summaryX, rowY3, summaryW,
                                                       ru ? "ФРАКЦ." : "CUTS",
                                                       rowBuf, COLOR_INFO);
                              } else if (profile == DASH_PROFILE_MASH ||
                                         profile == DASH_PROFILE_HOLD) {
                                const bool isMash =
                                    (profile == DASH_PROFILE_MASH);
                                const uint8_t steps =
                                    isMash
                                        ? ((state.mashing.stepCount > 0)
                                               ? state.mashing.stepCount
                                               : mashProfileDefault.stepCount)
                                        : ((state.hold.stepCount > 0)
                                               ? state.hold.stepCount
                                               : holdStepsCount);
                                const uint8_t currentStep = isMash
                                                                ? state.mashing.currentStep
                                                                : state.hold.currentStep;
                                const float targetTemp = isMash
                                                             ? state.mashing.targetTemp
                                                             : state.hold.targetTemp;
                                const bool inRange = isMash
                                                         ? state.mashing.tempInRange
                                                         : state.hold.tempInRange;
                                const uint32_t targetSec =
                                    isMash
                                        ? state.mashing.stepDuration
                                        : ((currentStep < state.hold.stepCount)
                                               ? static_cast<uint32_t>(
                                                     state.hold.steps[currentStep]
                                                         .duration) *
                                                     60UL
                                               : 0UL);
                                const bool useCooling =
                                    (!isMash && currentStep < state.hold.stepCount)
                                        ? state.hold.steps[currentStep].useCooling
                                        : false;
                                const uint32_t elapsedSec = FSM::getPhaseElapsedSec();
                                char elapsedBuf[12];
                                char targetBuf[12];
                                char heroLabelBuf[24];
                                formatDurationCompact(elapsedSec, elapsedBuf,
                                                      sizeof(elapsedBuf));
                                formatDurationCompact(targetSec, targetBuf,
                                                      sizeof(targetBuf));
                                snprintf(heroLabelBuf, sizeof(heroLabelBuf),
                                         "%s %u/%u",
                                         ru ? "ШАГ" : "STEP",
                                         static_cast<unsigned>(
                                             steps == 0 ? 0 : currentStep + 1),
                                         static_cast<unsigned>(steps));
                                snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
                                         state.temps.cube);
                                drawSummaryHeroBlock(summaryX, heroY, summaryW,
                                                     heroH,
                                                     heroLabelBuf,
                                                     rowBuf,
                                                     inRange ? COLOR_SUCCESS
                                                             : COLOR_WARNING);
                                snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
                                         targetTemp);
                                drawCompactKeyValueRow(summaryX, rowY1, summaryW,
                                                       ru ? "ЦЕЛЬ" : "TARGET",
                                                       rowBuf, COLOR_SUCCESS);
                                snprintf(rowBuf, sizeof(rowBuf), "%s/%s",
                                         elapsedBuf, targetBuf);
                                drawCompactKeyValueRow(summaryX, rowY2, summaryW,
                                                       ru ? "ТАЙМЕР" : "TIMER",
                                                       rowBuf, COLOR_PRIMARY);
                                if (isMash) {
                                  if (state.stirrer.running) {
                                    snprintf(rowBuf, sizeof(rowBuf), "%u%%",
                                             static_cast<unsigned>(
                                                 state.stirrer.speedPercent));
                                  } else {
                                    snprintf(rowBuf, sizeof(rowBuf), "%s",
                                             ru ? "ВЫКЛ" : "OFF");
                                  }
                                  drawCompactKeyValueRow(
                                      summaryX, rowY3, summaryW,
                                      ru ? "МЕШАЛКА" : "STIRRER", rowBuf,
                                      state.stirrer.running ? COLOR_SUCCESS
                                                            : colorMuted());
                                } else {
                                  drawCompactKeyValueRow(
                                      summaryX, rowY3, summaryW,
                                      ru ? "КОНТУР" : "LOOP",
                                      useCooling ? (ru ? "ОХЛ." : "COOL")
                                                 : (ru ? "НАГР." : "HEAT"),
                                      useCooling ? COLOR_INFO : COLOR_WARNING);
                                }
                              } else {
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f %s",
                                         state.stats.bodyVolume,
                                         ru ? "мл" : "ml");
                                drawSummaryHeroBlock(summaryX, heroY, summaryW,
                                                     heroH,
                                                     ru ? "ТЕЛО" : "BODY",
                                                     rowBuf, COLOR_SUCCESS);
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f %s",
                                         state.stats.headsVolume,
                                         ru ? "мл" : "ml");
                                drawCompactKeyValueRow(summaryX, rowY1,
                                                       summaryW,
                                                       ru ? "ГОЛОВЫ" : "HEADS",
                                                       rowBuf, COLOR_WARNING);
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f %s",
                                         state.stats.tailsVolume,
                                         ru ? "мл" : "ml");
                                drawCompactKeyValueRow(summaryX, rowY2,
                                                       summaryW,
                                                       ru ? "ХВОСТЫ" : "TAILS",
                                                       rowBuf, COLOR_DANGER);
                                snprintf(rowBuf, sizeof(rowBuf), "%.0f mm",
                                         state.pressure.cube);
                                drawCompactKeyValueRow(summaryX, rowY3,
                                                       summaryW,
                                                       ru ? "ДАВЛ." : "PRESS",
                                                       rowBuf, COLOR_WARNING);
                              }
                            }
                            drawStateBadge(ROOT_LEFT_X + 8,
                                           ROOT_PANEL_Y + ROOT_PANEL_H - 22,
                                           ROOT_LEFT_W - 16, 14,
                                           header.safetyState,
                                           header.safetyColor);

                            if (full ||
                                strcmp(g_dashboardCache.infoLine, infoBuf) !=
                                    0 ||
                                strcmp(g_dashboardCache.ioLine, ioBuf) != 0 ||
                                strcmp(g_dashboardCache.uptime, upBuf) != 0) {
                              if (!full) {
                                tft.fillRect(14, infoY + 3, TFT_WIDTH - 28, 34,
                                             colorCard());
                              }
                              tft.setTextColor(colorFg());
                              tft.setTextSize(1);
                              tft.setTextDatum(middle_left);
                              drawDisplayString(infoBuf, 20, infoY + 13);
                              tft.setTextColor(tft.color565(120, 130, 140));
                              drawDisplayString(ioBuf, 20, infoY + 28);
                              tft.setTextColor(COLOR_PRIMARY);
                              tft.setTextDatum(middle_right);
                              drawDisplayString(upBuf, TFT_WIDTH - 18, infoY + 13);

                              strncpy(g_dashboardCache.infoLine, infoBuf,
                                      sizeof(g_dashboardCache.infoLine));
                              g_dashboardCache
                                  .infoLine[sizeof(g_dashboardCache.infoLine) -
                                            1] = '\0';
                              strncpy(g_dashboardCache.ioLine, ioBuf,
                                      sizeof(g_dashboardCache.ioLine));
                              g_dashboardCache
                                  .ioLine[sizeof(g_dashboardCache.ioLine) - 1] =
                                  '\0';
                              strncpy(g_dashboardCache.uptime, upBuf,
                                      sizeof(g_dashboardCache.uptime));
                              g_dashboardCache
                                  .uptime[sizeof(g_dashboardCache.uptime) - 1] =
                                  '\0';
                            }

                            tft.setFont(&fonts::efontJA_16);
                            tft.setTextDatum(top_left);
                          }

                          static void renderModeMonitorCustom(
                              const SystemState &state, bool full);
                          static void renderModeMonitorCustomHmi(
                              const SystemState &state, bool full);

                          static void renderModeMonitor(
                              const SystemState &state, bool full) {
                            if (state.mode != Mode::RECTIFICATION) {
                              renderModeMonitorCustomHmi(state, full);
                              return;
                            }

                            const bool ru = (g_settings.language == 0);
                            const ControlV2::ModeStatusV2 &modeStatus =
                                ControlV2::getLatestModeStatus();
                            const ControlV2::ProcessIndicatorsV2 &indicators =
                                ControlV2::getLatestIndicators();
                            RootHeaderState header =
                                buildRootHeaderState(state);
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
                              drawRootScaffold(UI_MODE_MONITOR);
                              drawCard(leftX, panelY, leftW, panelH,
                                       colorCard());

                              drawValueTileShell(rightX, panelY, tileW, tileH,
                                                 msg(Msg::CUBE_TEMP));
                              drawValueTileShell(rightX + tileW + colGap,
                                                 panelY, tileW, tileH,
                                                 msg(Msg::TOP_T));
                              drawValueTileShell(rightX,
                                                 panelY + tileH + rowGap, tileW,
                                                 tileH, msg(Msg::REFLUX_T));
                              drawValueTileShell(rightX + tileW + colGap,
                                                 panelY + tileH + rowGap, tileW,
                                                 tileH, msg(Msg::TSA_T));
                              drawValueTileShell(
                                  rightX, panelY + (tileH + rowGap) * 2, tileW,
                                  tileH, ru ? "СТАБ." : "STAB");
                              drawValueTileShell(
                                  rightX + tileW + colGap,
                                  panelY + (tileH + rowGap) * 2, tileW, tileH,
                                  ru ? "РИСК" : "RISK");

                              drawCard(ROOT_FRAME_X, infoY, ROOT_FRAME_W, 40,
                                       colorCard());
                              memset(&g_dashboardCache, 0,
                                     sizeof(g_dashboardCache));
                              g_dashboardCache.layoutKey = 0xEE;
                            }

                            renderRootStatusBar(header, full);

                            char summaryBuf[96];
                            if (ru) {
                              snprintf(
                                  summaryBuf, sizeof(summaryBuf),
                                  "ОКНО РЕЖИМА\nГол %.0f  Тело %.0f  Хв %.0f",
                                  state.stats.headsVolume,
                                  state.stats.bodyVolume,
                                  state.stats.tailsVolume);
                            } else {
                              snprintf(summaryBuf, sizeof(summaryBuf),
                                       "MODE WINDOW\nH %.0f  B %.0f  T %.0f",
                                       state.stats.headsVolume,
                                       state.stats.bodyVolume,
                                       state.stats.tailsVolume);
                            }

                            char val[16];
                            char topLine[32];
                            snprintf(topLine, sizeof(topLine), "V %.0f  P %.0f",
                                     state.power.voltage, state.pressure.cube);
                            if (full ||
                                strcmp(g_dashboardCache.infoLine, summaryBuf) !=
                                    0 ||
                                strcmp(g_dashboardCache.ioLine, topLine) != 0) {
                              if (!full) {
                                tft.fillRect(leftX + 6, panelY + 8, leftW - 12,
                                             panelH - 16, colorCard());
                              }
                              tft.setTextColor(colorFg());
                              tft.setTextSize(1);
                              tft.setTextDatum(top_left);
                              drawDisplayString(ru ? "РЕЖИМНОЕ ОКНО"
                                                : "MODE WINDOW",
                                             leftX + 10, panelY + 10);
                              tft.setTextColor(tft.color565(120, 130, 140));
                              drawDisplayString(topLine, leftX + 10, panelY + 28);
                              tft.setTextColor(colorFg());
                              if (ru) {
                                snprintf(val, sizeof(val), "Гол %.0f мл",
                                         state.stats.headsVolume);
                                drawDisplayString(val, leftX + 10, panelY + 52);
                                snprintf(val, sizeof(val), "Тело %.0f мл",
                                         state.stats.bodyVolume);
                                drawDisplayString(val, leftX + 10, panelY + 72);
                                snprintf(val, sizeof(val), "Хв %.0f мл",
                                         state.stats.tailsVolume);
                                drawDisplayString(val, leftX + 10, panelY + 92);
                              } else {
                                snprintf(val, sizeof(val), "Heads %.0f ml",
                                         state.stats.headsVolume);
                                drawDisplayString(val, leftX + 10, panelY + 52);
                                snprintf(val, sizeof(val), "Body %.0f ml",
                                         state.stats.bodyVolume);
                                drawDisplayString(val, leftX + 10, panelY + 72);
                                snprintf(val, sizeof(val), "Tails %.0f ml",
                                         state.stats.tailsVolume);
                                drawDisplayString(val, leftX + 10, panelY + 92);
                              }

                              const char *k1 = Valves::getWater() ? "ON" : "--";
                              const char *k2 = Valves::getHeads() ? "ON" : "--";
                              const char *k3 =
                                  (Heater::getPower() > 0) ? "ON" : "--";
                              snprintf(val, sizeof(val), "K1%s K2%s K3%s", k1,
                                       k2, k3);
                              tft.setTextColor(tft.color565(120, 130, 140));
                              drawDisplayString(val, leftX + 10, panelY + 120);

                              strncpy(g_dashboardCache.infoLine, summaryBuf,
                                      sizeof(g_dashboardCache.infoLine));
                              g_dashboardCache
                                  .infoLine[sizeof(g_dashboardCache.infoLine) -
                                            1] = '\0';
                              strncpy(g_dashboardCache.ioLine, topLine,
                                      sizeof(g_dashboardCache.ioLine));
                              g_dashboardCache
                                  .ioLine[sizeof(g_dashboardCache.ioLine) - 1] =
                                  '\0';
                            }

                            {
                              const int16_t summaryX = leftX + 8;
                              const int16_t summaryW = leftW - 16;
                              const int16_t heroY = panelY + 32;
                              const int16_t heroH = 42;
                              const int16_t rowY1 = panelY + 86;
                              const int16_t rowY2 = panelY + 104;
                              const int16_t rowY3 = panelY + 122;
                              const char *k1 = Valves::getWater() ? "ON" : "--";
                              const char *k2 = Valves::getHeads() ? "ON" : "--";
                              const char *k3 =
                                  (Heater::getPower() > 0) ? "ON" : "--";
                              char rowBuf[24];
                              drawCard(leftX, panelY, leftW, panelH,
                                       colorCard());
                              drawPanelHeader(leftX, panelY, leftW,
                                              getDisplayPhaseName(state),
                                              state.paused ? COLOR_WARNING
                                                           : colorAccent());
                              drawCompactKeyValueRow(
                                  leftX + 8, panelY + 36, leftW - 16,
                                  ru ? "РЕЖИМ" : "MODE",
                                  getDisplayModeName(state.mode),
                                  colorAccent());
                              snprintf(rowBuf, sizeof(rowBuf), "%.0f %s",
                                       state.stats.headsVolume,
                                       ru ? "мл" : "ml");
                              drawCompactKeyValueRow(leftX + 8, panelY + 58,
                                                     leftW - 16,
                                                     ru ? "ГОЛОВЫ" : "HEADS",
                                                     rowBuf, COLOR_WARNING);
                              snprintf(rowBuf, sizeof(rowBuf), "%.0f %s",
                                       state.stats.bodyVolume,
                                       ru ? "мл" : "ml");
                              drawCompactKeyValueRow(
                                  leftX + 8, panelY + 80, leftW - 16,
                                  ru ? "ТЕЛО" : "BODY", rowBuf, COLOR_SUCCESS);
                              snprintf(rowBuf, sizeof(rowBuf), "%.0f %s",
                                       state.stats.tailsVolume,
                                       ru ? "мл" : "ml");
                              drawCompactKeyValueRow(leftX + 8, panelY + 102,
                                                     leftW - 16,
                                                     ru ? "ХВОСТЫ" : "TAILS",
                                                     rowBuf, COLOR_DANGER);
                              snprintf(rowBuf, sizeof(rowBuf), "K1%s K2%s K3%s",
                                       k1, k2, k3);
                              drawStateBadge(leftX + 8, panelY + panelH - 22,
                                             leftW - 16, 14, rowBuf,
                                             header.safetyColor);
                              drawCard(leftX, panelY, leftW, panelH,
                                       colorCard());
                              drawPanelHeader(leftX, panelY, leftW,
                                              getDisplayPhaseName(state),
                                              state.paused ? COLOR_WARNING
                                                           : colorAccent());
                              snprintf(rowBuf, sizeof(rowBuf), "%.0f %s",
                                       state.stats.bodyVolume,
                                       ru ? "мл" : "ml");
                              drawSummaryHeroBlock(summaryX, heroY, summaryW,
                                                   heroH, ru ? "ТЕЛО" : "BODY",
                                                   rowBuf, COLOR_SUCCESS);
                              snprintf(rowBuf, sizeof(rowBuf), "%.0f %s",
                                       state.stats.headsVolume,
                                       ru ? "мл" : "ml");
                              drawCompactKeyValueRow(summaryX, rowY1, summaryW,
                                                     ru ? "ГОЛОВЫ" : "HEADS",
                                                     rowBuf, COLOR_WARNING);
                              snprintf(rowBuf, sizeof(rowBuf), "%.0f %s",
                                       state.stats.tailsVolume,
                                       ru ? "мл" : "ml");
                              drawCompactKeyValueRow(summaryX, rowY2, summaryW,
                                                     ru ? "ХВОСТЫ" : "TAILS",
                                                     rowBuf, COLOR_DANGER);
                              snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
                                       indicators.coolingMarginC);
                              drawCompactKeyValueRow(summaryX, rowY3, summaryW,
                                                     ru ? "ОХЛ." : "COOL",
                                                     rowBuf,
                                                     indicators.coolingMarginC >=
                                                             5.0f
                                                         ? COLOR_SUCCESS
                                                         : (indicators
                                                                    .coolingMarginC >
                                                                0.0f
                                                                ? COLOR_WARNING
                                                                : COLOR_DANGER));
                              drawStateBadge(summaryX, panelY + panelH - 22,
                                             summaryW, 14, header.safetyState,
                                             header.safetyColor);
                            }

                            snprintf(val, sizeof(val), "%.1f",
                                     state.temps.cube);
                            if (full ||
                                strcmp(g_dashboardCache.cube, val) != 0) {
                              drawValueTileValue(rightX, panelY, tileW, tileH,
                                                 val, "°C", COLOR_DANGER);
                              drawValueTileValue(rightX, panelY, tileW, tileH,
                                                 val, "°C", COLOR_DANGER,
                                                 &cubeTempHistory);
                              strncpy(g_dashboardCache.cube, val,
                                      sizeof(g_dashboardCache.cube));
                              g_dashboardCache
                                  .cube[sizeof(g_dashboardCache.cube) - 1] =
                                  '\0';
                            }
                            snprintf(val, sizeof(val), "%.1f",
                                     state.temps.columnTop);
                            if (full ||
                                strcmp(g_dashboardCache.top, val) != 0) {
                              drawValueTileValue(rightX + tileW + colGap,
                                                 panelY, tileW, tileH, val,
                                                 "°C", colorAccent(),
                                                 &topTempHistory);
                       strncpy(g_dashboardCache.top, val,
                               sizeof(g_dashboardCache.top));
                       g_dashboardCache.top[sizeof(g_dashboardCache.top) - 1] =
                           '\0';
                            }
                            snprintf(val, sizeof(val), "%.1f",
                                     state.temps.reflux);
                            if (full ||
                                strcmp(g_dashboardCache.reflux, val) != 0) {
                              drawValueTileValue(rightX,
                                                 panelY + tileH + rowGap, tileW,
                                                 tileH, val, "°C", COLOR_INFO);
                              strncpy(g_dashboardCache.reflux, val,
                                      sizeof(g_dashboardCache.reflux));
                              g_dashboardCache
                                  .reflux[sizeof(g_dashboardCache.reflux) - 1] =
                                  '\0';
                            }
                            snprintf(val, sizeof(val), "%.1f", state.temps.tsa);
                            if (full ||
                                strcmp(g_dashboardCache.tsa, val) != 0) {
                              drawValueTileValue(rightX + tileW + colGap,
                                                 panelY + tileH + rowGap, tileW,
                                                 tileH, val, "°C",
                                                 COLOR_WARNING);
                              strncpy(g_dashboardCache.tsa, val,
                                      sizeof(g_dashboardCache.tsa));
                              g_dashboardCache
                                  .tsa[sizeof(g_dashboardCache.tsa) - 1] = '\0';
                            }
                            snprintf(val, sizeof(val), "%.0f",
                                     indicators.stabilityIndex * 100.0f);
                            if (full ||
                                strcmp(g_dashboardCache.power, val) != 0) {
                              drawValueTileValue(
                                  rightX, panelY + (tileH + rowGap) * 2, tileW,
                                  tileH, val, "%",
                                  indicators.stabilityIndex >= 0.75f
                                      ? COLOR_SUCCESS
                                      : (indicators.stabilityIndex >= 0.45f
                                             ? COLOR_WARNING
                                             : COLOR_DANGER));
                              strncpy(g_dashboardCache.power, val,
                                      sizeof(g_dashboardCache.power));
                              g_dashboardCache
                                  .power[sizeof(g_dashboardCache.power) - 1] =
                                  '\0';
                            }
                            snprintf(val, sizeof(val), "%.0f",
                                     indicators.floodRisk * 100.0f);
                            if (full ||
                                strcmp(g_dashboardCache.pump, val) != 0) {
                              drawValueTileValue(
                                  rightX + tileW + colGap,
                                  panelY + (tileH + rowGap) * 2, tileW, tileH,
                                  val, "%",
                                  indicators.floodRisk < 0.35f
                                      ? COLOR_SUCCESS
                                      : (indicators.floodRisk < 0.65f
                                             ? COLOR_WARNING
                                             : COLOR_DANGER));
                              strncpy(g_dashboardCache.pump, val,
                                      sizeof(g_dashboardCache.pump));
                              g_dashboardCache
                                  .pump[sizeof(g_dashboardCache.pump) - 1] =
                                  '\0';
                            }

                            char upBuf[16];
                            char footerHint[96];
                            uint16_t footerTone = COLOR_INFO;
                            setRectificationIndicatorHint(
                                footerHint, sizeof(footerHint), footerTone,
                                modeStatus, indicators);
                            formatUptimeCompact(state.uptime, upBuf,
                                                sizeof(upBuf));
                            if (full ||
                                strcmp(g_dashboardCache.uptime, upBuf) != 0) {
                              if (!full) {
                                tft.fillRect(14, infoY + 3, TFT_WIDTH - 28, 34,
                                             colorCard());
                              }
                              char footerDrawBuf[96];
                              copyFittedText(footerHint, 350, footerDrawBuf,
                                             sizeof(footerDrawBuf));
                              tft.setTextColor(footerTone);
                              tft.setTextSize(1);
                              tft.setTextDatum(middle_left);
                              drawDisplayString(footerDrawBuf, 20, infoY + 20);
                              tft.setTextColor(COLOR_PRIMARY);
                              tft.setTextDatum(middle_right);
                              drawDisplayString(upBuf, TFT_WIDTH - 18, infoY + 20);
                              strncpy(g_dashboardCache.uptime, upBuf,
                                      sizeof(g_dashboardCache.uptime));
                              g_dashboardCache
                                  .uptime[sizeof(g_dashboardCache.uptime) - 1] =
                                  '\0';
                            }

                            tft.setFont(&fonts::efontJA_16);
                            tft.setTextDatum(top_left);
                          }

                          static void renderModeMonitorCustomHmi(
                              const SystemState &state, bool full) {
                            const bool ru = (g_settings.language == 0);
                            const bool hasWaterOut =
                                state.temps.valid[TEMP_WATER_OUT];
                            const uint8_t layoutKey = static_cast<uint8_t>(
                                0xD0 |
                                ((static_cast<uint8_t>(state.mode) & 0x0F)
                                 << 1) |
                                (hasWaterOut ? 0x01 : 0x00));
                            uint32_t phaseElapsedOverrideSec = 0xFFFFFFFFUL;
                            const uint32_t nowMs = millis();

                            if (state.mode == Mode::MASHING) {
                              phaseElapsedOverrideSec = 0;
                              if (state.mashing.tempInRange &&
                                  state.mashing.inRangeStartTime > 0 &&
                                  nowMs >= state.mashing.inRangeStartTime) {
                                phaseElapsedOverrideSec =
                                    (nowMs - state.mashing.inRangeStartTime) /
                                    1000UL;
                              }
                            } else if (state.mode == Mode::HOLD) {
                              phaseElapsedOverrideSec = 0;
                              if (state.hold.tempInRange &&
                                  state.hold.inRangeStartTime > 0 &&
                                  nowMs >= state.hold.inRangeStartTime) {
                                phaseElapsedOverrideSec =
                                    (nowMs - state.hold.inRangeStartTime) /
                                    1000UL;
                              }
                            }

                            if (full) {
                              drawRootScaffold(UI_MODE_MONITOR);
                              memset(&g_dashboardCache, 0,
                                     sizeof(g_dashboardCache));
                              g_dashboardCache.layoutKey = 0xFF;
                            }

                            const bool layoutChanged =
                                full ||
                                (g_dashboardCache.layoutKey != layoutKey);
                            if (layoutChanged && !full) {
                              requestRedraw(DisplayRedrawReason::LAYOUT_CHANGED);
                            }
                            if (layoutChanged) {
                              resetModeMonitorRuntimeCache();
                            }

                            RootHeaderState header = buildRootHeaderState(
                                state, phaseElapsedOverrideSec);
                            renderRootStatusBar(header, full);

                            auto updateTile =
                                [&](uint8_t idx, int16_t x, int16_t y,
                                    int16_t w, int16_t h, const char *value,
                                    const char *unit, uint16_t color,
                                    TileValueTone tone =
                                        TileValueTone::Primary,
                                    SparklineBuffer<30> *sparkline =
                                        nullptr) {
                          if (full ||
                              strcmp(g_modeTileCache[idx], value) != 0) {
                            drawValueTileValueStyled(x, y, w, h, value, unit,
                                                     color, tone, sparkline);
                            strncpy(g_modeTileCache[idx], value,
                                    sizeof(g_modeTileCache[idx]) - 1);
                            g_modeTileCache[idx][sizeof(g_modeTileCache[idx]) -
                                                 1] = '\0';
                          }
                        };

                        const int16_t leftX = ROOT_LEFT_X;
                        const int16_t leftW = ROOT_LEFT_W;
                        const int16_t rightX = ROOT_RIGHT_X;
                        const int16_t rightW = ROOT_RIGHT_W;
                        const int16_t tileW = ROOT_RIGHT_TILE_W;
                        const int16_t tileH = ROOT_RIGHT_TILE_H;
                        const int16_t x2 = rightX + tileW + ROOT_GRID_COL_GAP;
                        const int16_t y2 =
                            ROOT_PANEL_Y + tileH + ROOT_GRID_ROW_GAP;
                        const int16_t y3 =
                            ROOT_PANEL_Y + (tileH + ROOT_GRID_ROW_GAP) * 2;

                        char infoLine[96] = "";
                        char auxLine[96] = "";
                        char upBuf[16];
                        formatDurationCompact(getModeRunElapsedSec(state),
                                              upBuf, sizeof(upBuf));

                        if (state.mode == Mode::MANUAL_RECT) {
                          if (layoutChanged) {
                            drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H,
                                     colorCard());
                            drawValueTileShell(rightX, ROOT_PANEL_Y, tileW,
                                               tileH, msg(Msg::CUBE_TEMP));
                            drawValueTileShell(x2, ROOT_PANEL_Y, tileW, tileH,
                                               msg(Msg::TOP_T));
                            drawValueTileShell(rightX, y2, tileW, tileH,
                                               msg(Msg::REFLUX_T));
                            drawValueTileShell(x2, y2, tileW, tileH,
                                               msg(Msg::TSA_T));
                            drawValueTileShell(rightX, y3, tileW, tileH,
                                               ru ? "ДАВЛ. КУБА"
                                                  : "CUBE PRESS");
                            drawValueTileShell(x2, y3, tileW, tileH,
                                               msg(Msg::PUMP));
                          }

                          char rowBuf[32];
                          char v[6][20];
                          snprintf(v[0], sizeof(v[0]), "%.1f",
                                   state.temps.cube);
                          snprintf(v[1], sizeof(v[1]), "%.1f",
                                   state.temps.columnTop);
                          snprintf(v[2], sizeof(v[2]), "%.1f",
                                   state.temps.reflux);
                          snprintf(v[3], sizeof(v[3]), "%.1f", state.temps.tsa);
                          snprintf(v[4], sizeof(v[4]), "%.0f",
                                   state.pressure.cube);
                          snprintf(v[5], sizeof(v[5]), "%.0f",
                                   state.pump.speedMlPerHour);

                          updateTile(0, rightX, ROOT_PANEL_Y, tileW, tileH,
                                     v[0], "C", COLOR_DANGER,
                                     TileValueTone::Primary, &cubeTempHistory);
                          updateTile(1, x2, ROOT_PANEL_Y, tileW, tileH, v[1], "C",
                                     colorAccent(), TileValueTone::Primary,
                                     &topTempHistory);
               updateTile(2, rightX, y2, tileW, tileH, v[2], "C", COLOR_INFO,
                          TileValueTone::Secondary);
               updateTile(3, x2, y2, tileW, tileH, v[3], "C", COLOR_WARNING,
                          TileValueTone::Secondary);
               updateTile(4, rightX, y3, tileW, tileH, v[4], "mm",
                          COLOR_WARNING, TileValueTone::Secondary);
               updateTile(5, x2, y3, tileW, tileH, v[5], msg(Msg::UNIT_ML_H),
                          COLOR_SUCCESS, TileValueTone::Primary);

               drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
               drawPanelHeader(leftX, ROOT_PANEL_Y, leftW,
                               getDisplayPhaseName(state),
                               state.paused ? COLOR_WARNING : colorAccent());
               snprintf(rowBuf, sizeof(rowBuf), "%.0f ml/h",
                        manualRectUi.speedMlH);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 36, leftW - 16,
                                      ru ? "СКОРОСТЬ" : "SPEED", rowBuf,
                                      COLOR_SUCCESS);
               snprintf(rowBuf, sizeof(rowBuf), "%.0f %%",
                        manualRectUi.powerPercent);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 58, leftW - 16,
                                      ru ? "МОЩНОСТЬ" : "POWER", rowBuf,
                                      COLOR_WARNING);
               snprintf(rowBuf, sizeof(rowBuf), "%.0f/%.0f ml",
                        state.stats.headsVolume, manualRectUi.headsTargetMl);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 80, leftW - 16,
                                      ru ? "ГОЛОВЫ" : "HEADS", rowBuf,
                                      COLOR_INFO);
               snprintf(rowBuf, sizeof(rowBuf), "%.0f/%.0f ml",
                        state.stats.bodyVolume, manualRectUi.bodyTargetMl);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 102, leftW - 16,
                                      ru ? "ТЕЛО" : "BODY", rowBuf,
                                      COLOR_PRIMARY);
               snprintf(rowBuf, sizeof(rowBuf), "%.0f/%.0f ml",
                        state.stats.tailsVolume, manualRectUi.tailsTargetMl);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 124, leftW - 16,
                                      ru ? "ХВОСТЫ" : "TAILS", rowBuf,
                                      COLOR_WARNING);
               drawStateBadge(leftX + 8, ROOT_PANEL_Y + ROOT_PANEL_H - 22,
                              leftW - 16, 14, header.procState,
                              header.procColor);

               setModeMonitorAccessInfoLine(infoLine, sizeof(infoLine),
                                            state.mode);
               if (hasWaterOut) {
                 snprintf(auxLine, sizeof(auxLine),
                          "V %.0f | Atm %.0f | Water %.1f", state.power.voltage,
                          state.pressure.atmosphere * 0.750062f,
                          state.temps.waterOut);
               } else {
                 snprintf(auxLine, sizeof(auxLine),
                          "V %.0f | Atm %.0f | Pump %.0f", state.power.voltage,
                          state.pressure.atmosphere * 0.750062f,
                          state.pump.speedMlPerHour);
               }
                        } else if (state.mode == Mode::MASHING ||
                                   state.mode == Mode::HOLD) {
                          const bool isMash = (state.mode == Mode::MASHING);
                          const uint8_t steps =
                              isMash ? ((state.mashing.stepCount > 0)
                                            ? state.mashing.stepCount
                                            : mashProfileDefault.stepCount)
                                     : ((state.hold.stepCount > 0)
                                            ? state.hold.stepCount
                                            : holdStepsCount);
                          const uint8_t currentStep =
                              isMash ? state.mashing.currentStep
                                     : state.hold.currentStep;
                          const float targetTemp =
                              isMash ? state.mashing.targetTemp
                                     : state.hold.targetTemp;
                          const bool inRange = isMash
                                                   ? state.mashing.tempInRange
                                                   : state.hold.tempInRange;
                          const uint32_t currentStepTargetSec =
                              isMash ? state.mashing.stepDuration
                                     : ((currentStep < state.hold.stepCount)
                                            ? static_cast<uint32_t>(
                                                  state.hold.steps[currentStep]
                                                      .duration) *
                                                  60UL
                                            : 0UL);
                          const bool useCooling =
                              (!isMash && currentStep < state.hold.stepCount)
                                  ? state.hold.steps[currentStep].useCooling
                                  : false;
                          const int16_t rowGap = ROOT_GRID_ROW_GAP;
                          const uint8_t visibleRows = (steps < 4) ? steps : 4;
                          const int16_t rowH =
                              (visibleRows > 0) ? (ROOT_PANEL_H -
                                                   (visibleRows - 1) * rowGap) /
                                                      visibleRows
                                                : ROOT_PANEL_H;
                          uint8_t startStep = 0;

                          if (steps > visibleRows && visibleRows > 0) {
                            startStep =
                                (currentStep > 0)
                                    ? static_cast<uint8_t>(currentStep - 1)
                                    : 0;
                            if (startStep + visibleRows > steps) {
                              startStep = steps - visibleRows;
                            }
                          }

                          drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H,
                                   colorCard());
                          drawPanelHeader(leftX, ROOT_PANEL_Y, leftW,
                                          getDisplayPhaseName(state),
                                          inRange ? COLOR_SUCCESS
                                                  : COLOR_WARNING);

                          char rowBuf[40];
                          char timerBuf[24];
                          char elapsedBuf[12];
                          char targetBuf[12];
                          const uint32_t elapsedSec =
                              (phaseElapsedOverrideSec == 0xFFFFFFFFUL)
                                  ? FSM::getPhaseElapsedSec()
                                  : phaseElapsedOverrideSec;
                          formatDurationCompact(elapsedSec, elapsedBuf,
                                                sizeof(elapsedBuf));
                          formatDurationCompact(currentStepTargetSec, targetBuf,
                                                sizeof(targetBuf));
                          snprintf(timerBuf, sizeof(timerBuf), "%s/%s",
                                   elapsedBuf, targetBuf);

                          snprintf(rowBuf, sizeof(rowBuf), "%u/%u",
                                   static_cast<unsigned>(
                                       steps == 0 ? 0 : currentStep + 1),
                                   static_cast<unsigned>(steps));
                          drawCompactKeyValueRow(
                              leftX + 8, ROOT_PANEL_Y + 36, leftW - 16,
                              ru ? "ШАГ" : "STEP", rowBuf, colorAccent());
                          if (targetTemp > 0.0f) {
                            snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
                                     targetTemp);
                          } else {
                            snprintf(rowBuf, sizeof(rowBuf), "%s",
                                     ru ? "ПАУЗА" : "PAUSE");
                          }
                          drawCompactKeyValueRow(
                              leftX + 8, ROOT_PANEL_Y + 58, leftW - 16,
                              ru ? "ЦЕЛЬ" : "TARGET", rowBuf, COLOR_SUCCESS);
                          drawCompactKeyValueRow(
                              leftX + 8, ROOT_PANEL_Y + 80, leftW - 16,
                              ru ? "ТАЙМЕР" : "TIMER", timerBuf, COLOR_PRIMARY);
                          snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
                                   state.temps.cube);
                          drawCompactKeyValueRow(
                              leftX + 8, ROOT_PANEL_Y + 102, leftW - 16,
                              msg(Msg::CUBE_TEMP), rowBuf, COLOR_DANGER);
                          if (isMash) {
                            if (state.stirrer.running) {
                              snprintf(rowBuf, sizeof(rowBuf), "%u%%",
                                       static_cast<unsigned>(
                                           state.stirrer.speedPercent));
                            } else {
                              snprintf(rowBuf, sizeof(rowBuf), "%s",
                                       ru ? "ВЫКЛ" : "OFF");
                            }
                            drawCompactKeyValueRow(
                                leftX + 8, ROOT_PANEL_Y + 124, leftW - 16,
                                ru ? "МЕШАЛКА" : "STIRRER", rowBuf,
                                state.stirrer.running ? COLOR_SUCCESS
                                                      : colorMuted());
                          } else {
                            snprintf(rowBuf, sizeof(rowBuf), "%s",
                                     useCooling ? (ru ? "ОХЛАЖД." : "COOLING")
                                                : (ru ? "НАГРЕВ" : "HEATING"));
                            drawCompactKeyValueRow(
                                leftX + 8, ROOT_PANEL_Y + 124, leftW - 16,
                                ru ? "КОНТУР" : "LOOP", rowBuf,
                                useCooling ? COLOR_INFO : COLOR_WARNING);
                          }
                          drawStateBadge(
                              leftX + 8, ROOT_PANEL_Y + ROOT_PANEL_H - 22,
                              leftW - 16, 14,
                              inRange ? (ru ? "В ДОПУСКЕ" : "IN RANGE")
                                      : (ru ? "НАБОР ТЕМП" : "HEATING"),
                              inRange ? COLOR_SUCCESS : COLOR_WARNING);

                          drawCard(rightX, ROOT_PANEL_Y, rightW, ROOT_PANEL_H,
                                   colorCard());
                          for (uint8_t i = 0; i < visibleRows; i++) {
                            const uint8_t stepIndex = startStep + i;
                            const int16_t rowY =
                                ROOT_PANEL_Y + i * (rowH + rowGap);
                            const bool isCurrent = stepIndex == currentStep;
                            const bool completed = stepIndex < currentStep;
                            uint8_t progress = completed ? 100 : 0;
                            float stepTemp = 0.0f;
                            uint16_t stepMinutes = 0;
                            bool stepCooling = false;
                            const char *stepName = nullptr;

                            if (isMash) {
                              if (stepIndex < mashProfileDefault.stepCount) {
                                stepTemp = mashProfileDefault.steps[stepIndex]
                                               .temperature;
                                stepMinutes =
                                    mashProfileDefault.steps[stepIndex]
                                        .duration;
                                stepName =
                                    mashProfileDefault.steps[stepIndex].name;
                              }
                              if (isCurrent && currentStepTargetSec > 0 &&
                                  inRange) {
                                uint32_t p =
                                    (elapsedSec * 100UL) / currentStepTargetSec;
                                if (p > 100UL) {
                                  p = 100UL;
                                }
                                progress = static_cast<uint8_t>(p);
                              }
                            } else {
                              if (stepIndex < holdStepsCount) {
                                stepTemp =
                                    holdStepsDefault[stepIndex].temperature;
                                stepMinutes =
                                    holdStepsDefault[stepIndex].duration;
                                stepCooling =
                                    holdStepsDefault[stepIndex].useCooling;
                              }
                              if (stepIndex < state.hold.stepCount) {
                                stepTemp =
                                    state.hold.steps[stepIndex].temperature;
                                stepMinutes =
                                    state.hold.steps[stepIndex].duration;
                                stepCooling =
                                    state.hold.steps[stepIndex].useCooling;
                              }
                              if (isCurrent && currentStepTargetSec > 0 &&
                                  inRange) {
                                uint32_t p =
                                    (elapsedSec * 100UL) / currentStepTargetSec;
                                if (p > 100UL) {
                                  p = 100UL;
                                }
                                progress = static_cast<uint8_t>(p);
                              }
                            }

                            const uint16_t rowBg =
                                isCurrent
                                    ? tft.color565(235, 245, 255)
                                    : (completed ? tft.color565(236, 248, 240)
                                                 : colorCard());
                            drawCard(rightX, rowY, rightW, rowH, rowBg);
                            if (isCurrent) {
                              tft.fillRect(rightX + 1, rowY + 1, rightW - 2, 3,
                                           inRange ? COLOR_SUCCESS
                                                   : colorAccent());
                            }

                            char titleBuf[28];
                            char metaBuf[16];
                            char detailBuf[32];
                            if (isMash && stepName != nullptr &&
                                stepName[0] != '\0') {
                              snprintf(titleBuf, sizeof(titleBuf), "%u. %.18s",
                                       static_cast<unsigned>(stepIndex + 1),
                                       stepName);
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
                              snprintf(detailBuf, sizeof(detailBuf), "%.1f C",
                                       stepTemp);
                            } else {
                              snprintf(detailBuf, sizeof(detailBuf), "%s",
                                       ru ? "ПАУЗА БЕЗ НАГРЕВА"
                                          : "NO-HEAT PAUSE");
                            }

                            tft.setTextColor(isCurrent ? colorAccent()
                                                       : colorFg());
                            tft.setTextSize(1);
                            tft.setTextDatum(middle_left);
                            drawDisplayString(titleBuf, rightX + 10, rowY + 10);
                            tft.setTextColor(colorMuted());
                            tft.setTextDatum(middle_right);
                            drawDisplayString(metaBuf, rightX + rightW - 10,
                                           rowY + 10);
                            tft.setTextColor(completed ? COLOR_SUCCESS
                                                       : COLOR_PRIMARY);
                            tft.setTextDatum(middle_left);
                            drawDisplayString(detailBuf, rightX + 10, rowY + 22);
                            drawProgressBar(rightX + 10, rowY + rowH - 7,
                                            rightW - 20, 4, progress,
                                            completed ? COLOR_SUCCESS
                                                      : colorAccent());
                            tft.setTextDatum(top_left);
                          }

                          setModeMonitorAccessInfoLine(infoLine,
                                                       sizeof(infoLine),
                                                       state.mode);
                          if (isMash) {
                            snprintf(auxLine, sizeof(auxLine),
                                     "P %.0fW | Stir %s | Cube %.1f",
                                     state.power.power,
                                     state.stirrer.running ? "ON" : "OFF",
                                     state.temps.cube);
                          } else {
                            snprintf(auxLine, sizeof(auxLine),
                                     "P %.0fW | %s | Cube %.1f",
                                     state.power.power,
                                     useCooling ? "Cooling" : "Heating",
                                     state.temps.cube);
                          }
                        } else if (state.mode == Mode::DISTILLATION) {
                          if (layoutChanged) {
                            drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H,
                                     colorCard());
                            drawValueTileShell(rightX, ROOT_PANEL_Y, tileW,
                                               tileH, msg(Msg::CUBE_TEMP));
                            drawValueTileShell(x2, ROOT_PANEL_Y, tileW, tileH,
                                               ru ? "МОЩН. %" : "POWER %");
                            drawValueTileShell(rightX, y2, tileW, tileH,
                                               msg(Msg::DIST_SPEED));
                            drawValueTileShell(x2, y2, tileW, tileH,
                                               ru ? "ОТБОР" : "COLLECT");
                            drawValueTileShell(rightX, y3, tileW, tileH,
                                               ru ? "РАБОТА" : "RUN");
                            drawValueTileShell(x2, y3, tileW, tileH,
                                               msg(Msg::END_TEMP));
                          }

                          char rowBuf[24];
                          char runBuf[16];
                          char v[6][20];
                          formatDurationCompact(getModeRunElapsedSec(state),
                                                runBuf, sizeof(runBuf));
                          snprintf(v[0], sizeof(v[0]), "%.1f",
                                   state.temps.cube);
                          snprintf(v[1], sizeof(v[1]), "%.0f",
                                   distUi.powerPercent);
                          snprintf(v[2], sizeof(v[2]), "%.0f", distUi.speedMlH);
                          snprintf(v[3], sizeof(v[3]), "%.0f",
                                   state.pump.totalVolumeMl);
                          snprintf(v[4], sizeof(v[4]), "%s", runBuf);
                          snprintf(v[5], sizeof(v[5]), "%.1f", distUi.endTempC);

                          updateTile(0, rightX, ROOT_PANEL_Y, tileW, tileH,
                                     v[0], "C", COLOR_DANGER,
                                     TileValueTone::Primary, &cubeTempHistory);
                          updateTile(1, x2, ROOT_PANEL_Y, tileW, tileH, v[1], "%",
                                     COLOR_WARNING, TileValueTone::Primary);
                          updateTile(2, rightX, y2, tileW, tileH, v[2],
                                     msg(Msg::UNIT_ML_H), COLOR_SUCCESS,
                                     TileValueTone::Primary);
                          updateTile(3, x2, y2, tileW, tileH, v[3], "ml",
                                     COLOR_INFO, TileValueTone::Secondary);
                          updateTile(4, rightX, y3, tileW, tileH, v[4], "",
                                     COLOR_PRIMARY, TileValueTone::Duration);
                          updateTile(5, x2, y3, tileW, tileH, v[5], "C",
                                     COLOR_INFO, TileValueTone::Secondary);

               drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
               drawPanelHeader(leftX, ROOT_PANEL_Y, leftW,
                               getDisplayPhaseName(state),
                               state.paused ? COLOR_WARNING : colorAccent());
               snprintf(rowBuf, sizeof(rowBuf), "%.0f/%.0f ml",
                        state.pump.totalVolumeMl, distUi.targetVolumeMl);
               drawSummaryHeroBlock(leftX + 8, ROOT_PANEL_Y + 32, leftW - 16,
                                    42, ru ? "ОТБОР" : "COLLECT", rowBuf,
                                    COLOR_SUCCESS);
               snprintf(rowBuf, sizeof(rowBuf), "%.0f ml",
                        distUi.headsVolumeMl);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 86, leftW - 16,
                                      ru ? "ГОЛОВЫ" : "HEADS", rowBuf,
                                      COLOR_WARNING);
               snprintf(rowBuf, sizeof(rowBuf), "%.1f C", distUi.endTempC);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 104, leftW - 16,
                                      ru ? "ФИНИШ" : "END", rowBuf, COLOR_INFO);
               snprintf(rowBuf, sizeof(rowBuf), "%.0f mm", state.pressure.cube);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 122, leftW - 16,
                                      ru ? "ДАВЛ." : "PRESS", rowBuf,
                                      COLOR_WARNING);
               drawStateBadge(leftX + 8, ROOT_PANEL_Y + ROOT_PANEL_H - 22,
                              leftW - 16, 14, header.procState,
                              header.procColor);

               setModeMonitorAccessInfoLine(infoLine, sizeof(infoLine),
                                            state.mode);
               snprintf(auxLine, sizeof(auxLine), "V %.0f | P %.0f | Pump %.0f",
                        state.power.voltage, state.pressure.cube,
                        state.pump.speedMlPerHour);
                        } else if (state.mode == Mode::NBK) {
                          if (layoutChanged) {
                            drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H,
                                     colorCard());
                            drawValueTileShell(rightX, ROOT_PANEL_Y, tileW,
                                               tileH, msg(Msg::COLUMN_BOTTOM));
                            drawValueTileShell(x2, ROOT_PANEL_Y, tileW, tileH,
                                               msg(Msg::CUBE_TEMP));
                            drawValueTileShell(rightX, y2, tileW, tileH,
                                               msg(Msg::HEATER_POWER));
                            drawValueTileShell(x2, y2, tileW, tileH,
                                               msg(Msg::PUMP));
                            drawValueTileShell(rightX, y3, tileW, tileH,
                                               ru ? "ДАВЛ." : "PRESS");
                            drawValueTileShell(
                                x2, y3, tileW, tileH,
                                hasWaterOut ? (ru ? "ОХЛ ВЫХ" : "WATER OUT")
                                            : (ru ? "ЦЕЛЬ" : "TARGET"));
                          }

                          char rowBuf[24];
                          char v[6][20];
                          snprintf(v[0], sizeof(v[0]), "%.1f",
                                   state.temps.columnBottom);
                          snprintf(v[1], sizeof(v[1]), "%.1f",
                                   state.temps.cube);
                          snprintf(v[2], sizeof(v[2]), "%.0f",
                                   state.power.power);
                          snprintf(v[3], sizeof(v[3]), "%.0f",
                                   state.pump.speedMlPerHour);
                          snprintf(v[4], sizeof(v[4]), "%.0f",
                                   state.pressure.cube);
                          if (hasWaterOut) {
                            snprintf(v[5], sizeof(v[5]), "%.1f",
                                     state.temps.waterOut);
                          } else {
                            snprintf(v[5], sizeof(v[5]), "%.0f",
                                     g_settings.nbk.pumpSpeedMlH);
                          }

                          updateTile(0, rightX, ROOT_PANEL_Y, tileW, tileH,
                                     v[0], "C", colorAccent(),
                                     TileValueTone::Primary);
                          updateTile(1, x2, ROOT_PANEL_Y, tileW, tileH, v[1],
                                     "C", COLOR_DANGER, TileValueTone::Primary,
                                     &cubeTempHistory);
                          updateTile(2, rightX, y2, tileW, tileH, v[2],
                                     msg(Msg::UNIT_W), COLOR_WARNING,
                                     TileValueTone::Primary);
                          updateTile(3, x2, y2, tileW, tileH, v[3],
                                     msg(Msg::UNIT_ML_H), COLOR_SUCCESS,
                                     TileValueTone::Primary);
                          updateTile(4, rightX, y3, tileW, tileH, v[4], "mm",
                                     COLOR_INFO, TileValueTone::Secondary);
                          updateTile(5, x2, y3, tileW, tileH, v[5],
                                     hasWaterOut ? "C" : msg(Msg::UNIT_ML_H),
                                     hasWaterOut ? COLOR_INFO : colorAccent(),
                                     TileValueTone::Secondary);

               drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
               drawPanelHeader(leftX, ROOT_PANEL_Y, leftW,
                               getDisplayPhaseName(state),
                               state.paused ? COLOR_WARNING : colorAccent());
               snprintf(rowBuf, sizeof(rowBuf), "%.0f/%.0f ml",
                        state.pump.totalVolumeMl,
                        g_settings.nbk.targetVolumeMl);
               drawSummaryHeroBlock(leftX + 8, ROOT_PANEL_Y + 32, leftW - 16,
                                    42, ru ? "ОТБОР" : "COLLECT", rowBuf,
                                    COLOR_SUCCESS);
               snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
                        g_settings.nbk.columnBottomTempThresholdC);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 86, leftW - 16,
                                      ru ? "ПОРОГ" : "THRESH", rowBuf,
                                      COLOR_WARNING);
               snprintf(rowBuf, sizeof(rowBuf), "%.0f ml/h",
                        g_settings.nbk.pumpSpeedMlH);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 104, leftW - 16,
                                      ru ? "ПОДАЧА" : "FEED", rowBuf,
                                      COLOR_INFO);
               snprintf(rowBuf, sizeof(rowBuf), "%.0f V", state.power.voltage);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 122, leftW - 16,
                                      ru ? "СЕТЬ" : "MAINS", rowBuf,
                                      COLOR_PRIMARY);
               drawStateBadge(leftX + 8, ROOT_PANEL_Y + ROOT_PANEL_H - 22,
                              leftW - 16, 14, header.procState,
                              header.procColor);

               setModeMonitorAccessInfoLine(infoLine, sizeof(infoLine),
                                            state.mode);
               snprintf(auxLine, sizeof(auxLine), "Target %.0f | Pump %.0f",
                        g_settings.nbk.targetVolumeMl,
                        state.pump.speedMlPerHour);
                        } else if (state.mode == Mode::FERMENTATION) {
                          if (layoutChanged) {
                            drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H,
                                     colorCard());
                            drawValueTileShell(rightX, ROOT_PANEL_Y, tileW,
                                               tileH, msg(Msg::CUBE_TEMP));
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
                              state.temps.cube -
                              g_settings.fermentation.targetTempC;
                          formatDurationCompact(getModeRunElapsedSec(state),
                                                runBuf, sizeof(runBuf));
                          snprintf(v[0], sizeof(v[0]), "%.1f",
                                   state.temps.cube);
                          snprintf(v[1], sizeof(v[1]), "%.1f",
                                   g_settings.fermentation.targetTempC);
                          snprintf(v[2], sizeof(v[2]), "%.1f",
                                   g_settings.fermentation.hysteresisC);
                          snprintf(v[3], sizeof(v[3]), "%s", runBuf);
                          snprintf(v[4], sizeof(v[4]), "%.1f", delta);
                          snprintf(v[5], sizeof(v[5]), "%s",
                                   g_settings.fermentation.useHeater ? "ON"
                                                                     : "OFF");

                          updateTile(
                              0, rightX, ROOT_PANEL_Y, tileW, tileH, v[0], "C",
                              fabsf(delta) > g_settings.fermentation.hysteresisC
                                  ? COLOR_WARNING
                                  : COLOR_SUCCESS,
                              TileValueTone::Primary, &cubeTempHistory);
                          updateTile(1, x2, ROOT_PANEL_Y, tileW, tileH, v[1], "C",
                                     colorAccent(), TileValueTone::Primary);
                          updateTile(2, rightX, y2, tileW, tileH, v[2], "C",
                                     COLOR_INFO, TileValueTone::Secondary);
                          updateTile(3, x2, y2, tileW, tileH, v[3], "",
                                     COLOR_PRIMARY, TileValueTone::Duration);
                          updateTile(4, rightX, y3, tileW, tileH, v[4], "C",
                                     (delta > 0.0f) ? COLOR_WARNING : COLOR_INFO,
                                     TileValueTone::Secondary);
                          updateTile(5, x2, y3, tileW, tileH, v[5], "",
                                     g_settings.fermentation.useHeater
                                         ? COLOR_SUCCESS
                                         : colorMuted(),
                                     TileValueTone::Secondary);

               drawCard(leftX, ROOT_PANEL_Y, leftW, ROOT_PANEL_H, colorCard());
               drawPanelHeader(leftX, ROOT_PANEL_Y, leftW,
                               getDisplayPhaseName(state),
                               state.paused ? COLOR_WARNING : colorAccent());
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 36, leftW - 16,
                                      ru ? "РЕЖИМ" : "MODE",
                                      getDisplayModeName(state.mode),
                                      colorAccent());
               snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
                        g_settings.fermentation.targetTempC);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 58, leftW - 16,
                                      ru ? "ЦЕЛЬ" : "TARGET", rowBuf,
                                      COLOR_SUCCESS);
               snprintf(rowBuf, sizeof(rowBuf), "%.1f C",
                        g_settings.fermentation.hysteresisC);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 80, leftW - 16,
                                      ru ? "ДОПУСК" : "BAND", rowBuf,
                                      COLOR_INFO);
               snprintf(rowBuf, sizeof(rowBuf), "%.0f h",
                        g_settings.fermentation.durationHours);
               drawCompactKeyValueRow(leftX + 8, ROOT_PANEL_Y + 102, leftW - 16,
                                      ru ? "ПЛАН" : "PLAN", rowBuf,
                                      COLOR_PRIMARY);
               drawCompactKeyValueRow(
                   leftX + 8, ROOT_PANEL_Y + 124, leftW - 16,
                   ru ? "НАГРЕВ" : "HEATER",
                   g_settings.fermentation.useHeater ? "ON" : "OFF",
                   g_settings.fermentation.useHeater ? COLOR_SUCCESS
                                                     : colorMuted());
               drawStateBadge(leftX + 8, ROOT_PANEL_Y + ROOT_PANEL_H - 22,
                              leftW - 16, 14, header.procState,
                              header.procColor);

                setModeMonitorAccessInfoLine(infoLine, sizeof(infoLine),
                                             state.mode);
               snprintf(auxLine, sizeof(auxLine), "%s | Plan %.0f h",
                        g_settings.fermentation.useHeater ? "Heater ON"
                                                          : "Heater OFF",
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

                            static void
                            renderModeMonitorCustom(const SystemState &state,
                                                    bool full) {
                              const bool ru = (g_settings.language == 0);
                              const int16_t panelY = ROOT_PANEL_Y;
                              const int16_t panelH = ROOT_PANEL_H;
                              const int16_t infoY = ROOT_INFO_Y;
                              const bool hasWaterOut =
                                  state.temps.valid[TEMP_WATER_OUT];
                              const uint8_t layoutKey = static_cast<uint8_t>(
                                  0xC0 |
                                  ((static_cast<uint8_t>(state.mode) & 0x0F)
                                   << 1) |
                                  (hasWaterOut ? 0x01 : 0x00));

                              if (full) {
                                drawRootScaffold(UI_MODE_MONITOR);
                                memset(&g_dashboardCache, 0,
                                       sizeof(g_dashboardCache));
                                g_dashboardCache.layoutKey = 0xFF;
                              }

                              const bool layoutChanged =
                                  full ||
                                  (g_dashboardCache.layoutKey != layoutKey);
                              if (layoutChanged && !full) {
                                requestRedraw(
                                    DisplayRedrawReason::LAYOUT_CHANGED);
                              }
                              if (layoutChanged) {
                                resetModeMonitorRuntimeCache();
                              }

                              uint32_t phaseElapsedSec =
                                  FSM::getPhaseElapsedSec();
                              const uint32_t nowMs = millis();
                              if (state.mode == Mode::MASHING) {
                                phaseElapsedSec = 0;
                                if (state.mashing.tempInRange &&
                                    state.mashing.inRangeStartTime > 0 &&
                                    nowMs >= state.mashing.inRangeStartTime) {
                                  phaseElapsedSec =
                                      (nowMs - state.mashing.inRangeStartTime) /
                                      1000UL;
                                }
                              } else if (state.mode == Mode::HOLD) {
                                phaseElapsedSec = 0;
                                if (state.hold.tempInRange &&
                                    state.hold.inRangeStartTime > 0 &&
                                    nowMs >= state.hold.inRangeStartTime) {
                                  phaseElapsedSec =
                                      (nowMs - state.hold.inRangeStartTime) /
                                      1000UL;
                                }
                              }
                              RootHeaderState header =
                                  buildRootHeaderState(state, phaseElapsedSec);
                              renderRootStatusBar(header, full);

                              auto updateTile =
                                  [&](uint8_t idx, int16_t x, int16_t y,
                                      int16_t w, int16_t h, const char *value,
                                      const char *unit, uint16_t color,
                                      TileValueTone tone =
                                          TileValueTone::Primary,
                                      SparklineBuffer<30> *sparkline =
                                          nullptr) {
                          if (full ||
                              strcmp(g_modeTileCache[idx], value) != 0) {
                            drawValueTileValueStyled(x, y, w, h, value, unit,
                                                     color, tone, sparkline);
                            strncpy(g_modeTileCache[idx], value,
                                    sizeof(g_modeTileCache[idx]) - 1);
                            g_modeTileCache[idx][sizeof(g_modeTileCache[idx]) -
                                                 1] = '\0';
                          }
                        };

                        char infoLine[96] = "";
                        char auxLine[96] = "";
                        char upBuf[16];
                        formatDurationCompact(getModeRunElapsedSec(state),
                                              upBuf, sizeof(upBuf));

                        if (state.mode == Mode::DISTILLATION) {
                          const int16_t g = TFT_BUTTON_GAP;
                          const int16_t hTile = 74;
                          const int16_t w2 = (ROOT_FRAME_W - g) / 2;
                          const int16_t x1 = ROOT_FRAME_X;
                          const int16_t x2 = x1 + w2 + g;
                          const int16_t y1 = panelY + hTile + g;

                          if (layoutChanged) {
                            drawValueTileShell(x1, panelY, w2, hTile,
                                               msg(Msg::CUBE_TEMP));
                            drawValueTileShell(x2, panelY, w2, hTile,
                                               ru ? "МОЩН. %" : "POWER %");
                            drawValueTileShell(x1, y1, w2, hTile,
                                               ru ? "ВРЕМЯ РАБОТЫ"
                                                  : "RUN TIME");
                            drawValueTileShell(x2, y1, w2, hTile,
                                               msg(Msg::END_TEMP));
                          }

                          char runBuf[16];
                          char v[4][20];
                          formatDurationCompact(getModeRunElapsedSec(state),
                                                runBuf, sizeof(runBuf));
                          snprintf(v[0], sizeof(v[0]), "%.1f",
                                   state.temps.cube);
                          snprintf(v[1], sizeof(v[1]), "%.0f",
                                   distUi.powerPercent);
                          snprintf(v[2], sizeof(v[2]), "%s", runBuf);
                          snprintf(v[3], sizeof(v[3]), "%.1f", distUi.endTempC);

                          updateTile(0, x1, panelY, w2, hTile, v[0], "C",
                                     COLOR_DANGER, TileValueTone::Primary,
                                     &cubeTempHistory);
                          updateTile(1, x2, panelY, w2, hTile, v[1], "%", COLOR_WARNING,
                                     TileValueTone::Primary);
               updateTile(2, x1, y1, w2, hTile, v[2], "", COLOR_PRIMARY,
                          TileValueTone::Duration);
               updateTile(3, x2, y1, w2, hTile, v[3], "C", COLOR_INFO,
                          TileValueTone::Secondary);

               setModeMonitorAccessInfoLine(infoLine, sizeof(infoLine),
                                            state.mode);
               snprintf(auxLine, sizeof(auxLine), "V %.0f | P %.0f | Pump %.0f",
                        state.power.voltage, state.pressure.cube,
                        state.pump.speedMlPerHour);
                        } else if (state.mode == Mode::MANUAL_RECT) {
                          const int16_t leftX = ROOT_LEFT_X;
                          const int16_t leftW = ROOT_LEFT_W;
                          const int16_t rowGap = ROOT_GRID_ROW_GAP;
                          const int16_t rowH = (panelH - rowGap * 4) / 5;
                          const int16_t rightX = ROOT_RIGHT_X;
                          const int16_t rightW = ROOT_RIGHT_W;
                          const int16_t colW = (rightW - ROOT_GRID_COL_GAP) / 2;
                          const int16_t rightRowH =
                              (panelH - ROOT_GRID_ROW_GAP * 3) / 4;
                          const int16_t rightGap = ROOT_GRID_ROW_GAP;

                          if (layoutChanged) {
                            drawCard(leftX, panelY, leftW, panelH, colorCard());
                            const char *labels[8] = {
                                msg(Msg::CUBE_TEMP),
                                msg(Msg::TOP_T),
                                msg(Msg::REFLUX_T),
                                msg(Msg::TSA_T),
                                ru ? "ДАВЛ КУБА" : "CUBE PRESS",
                                ru ? "ДАВЛ АТМ" : "ATM PRESS",
                                ru ? "СЕТЬ" : "MAINS",
                                hasWaterOut ? (ru ? "ОХЛ ВЫХ" : "WATER OUT")
                                            : msg(Msg::PUMP)};
                            for (uint8_t i = 0; i < 8; i++) {
                              const int16_t cx =
                                  rightX +
                                  ((i % 2) * (colW + ROOT_GRID_COL_GAP));
                              const int16_t cy =
                                  panelY + ((i / 2) * (rightRowH + rightGap));
                              drawCard(cx, cy, colW, rightRowH, colorCard());
                              tft.setTextColor(tft.color565(96, 104, 116));
                              tft.setTextSize(1);
                              tft.setTextDatum(top_left);
                              // Split two-word labels onto two lines only if
                              // too wide
                              const char *sp = strchr(labels[i], ' ');
                              if (sp && displayTextWidth(labels[i]) > colW - 8) {
                                char first[16];
                                size_t len = sp - labels[i];
                                if (len >= sizeof(first))
                                  len = sizeof(first) - 1;
                                memcpy(first, labels[i], len);
                                first[len] = '\0';
                                drawDisplayString(first, cx + 4, cy + 2);
                                drawDisplayString(sp + 1, cx + 4, cy + 11);
                              } else {
                                drawDisplayString(labels[i], cx + 4, cy + 2);
                              }
                              tft.setTextDatum(top_left);
                            }
                          }

                          const char *rowLabels[5] = {ru ? "Скорость" : "Speed",
                                                      ru ? "Мощность" : "Power",
                                                      ru ? "Головы" : "Heads",
                                                      ru ? "Тело" : "Body",
                                                      ru ? "Хвосты" : "Tails"};
                          char rowVals[5][24];
                          snprintf(rowVals[0], sizeof(rowVals[0]), "%.0f ml/h",
                                   manualRectUi.speedMlH);
                          snprintf(rowVals[1], sizeof(rowVals[1]), "%.0f %%",
                                   manualRectUi.powerPercent);
                          snprintf(rowVals[2], sizeof(rowVals[2]),
                                   "%.0f/%.0f ml", manualRectUi.headsTargetMl,
                                   state.stats.headsVolume);
                          snprintf(rowVals[3], sizeof(rowVals[3]),
                                   "%.0f/%.0f ml", manualRectUi.bodyTargetMl,
                                   state.stats.bodyVolume);
                          snprintf(rowVals[4], sizeof(rowVals[4]),
                                   "%.0f/%.0f ml", manualRectUi.tailsTargetMl,
                                   state.stats.tailsVolume);
                          for (uint8_t i = 0; i < 5; i++) {
                            const int16_t ry = panelY + i * (rowH + rowGap);
                            const uint16_t bg =
                                (i <= 1) ? tft.color565(236, 245, 255)
                                         : colorCard();
                            drawCard(leftX, ry, leftW, rowH, bg);
                            tft.setTextColor(tft.color565(90, 100, 112));
                            tft.setTextDatum(middle_left);
                            drawDisplayString(rowLabels[i], leftX + 8, ry + 9);
                            tft.setTextColor(colorAccent());
                            tft.setTextDatum(middle_right);
                            drawDisplayString(rowVals[i], leftX + leftW - 8,
                                           ry + 9);
                          }

                          char v[8][20];
                          snprintf(v[0], sizeof(v[0]), "%.1f",
                                   state.temps.cube);
                          snprintf(v[1], sizeof(v[1]), "%.1f",
                                   state.temps.columnTop);
                          snprintf(v[2], sizeof(v[2]), "%.1f",
                                   state.temps.reflux);
                          snprintf(v[3], sizeof(v[3]), "%.1f", state.temps.tsa);
                          snprintf(v[4], sizeof(v[4]), "%.0f",
                                   state.pressure.cube);
                          snprintf(v[5], sizeof(v[5]), "%.0f",
                                   state.pressure.atmosphere * 0.750062f);
                          snprintf(v[6], sizeof(v[6]), "%.0f",
                                   state.power.voltage);
                          if (hasWaterOut)
                            snprintf(v[7], sizeof(v[7]), "%.1f",
                                     state.temps.waterOut);
                          else
                            snprintf(v[7], sizeof(v[7]), "%.0f",
                                     state.pump.speedMlPerHour);

                          for (uint8_t i = 0; i < 8; i++) {
                            const int16_t cx =
                                rightX + ((i % 2) * (colW + ROOT_GRID_COL_GAP));
                            const int16_t cy =
                                panelY + ((i / 2) * (rightRowH + rightGap));
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
                                           : ((i == 2)
                                                  ? COLOR_INFO
                                                  : ((i == 3)
                                                         ? COLOR_WARNING
                                                         : COLOR_PRIMARY)));
                            if (i == 4)
                              color = COLOR_WARNING;
                            if (i == 5)
                              color = COLOR_INFO;
                            if (i == 7)
                              color = hasWaterOut ? COLOR_INFO : COLOR_SUCCESS;
                            const TileValueTone tone =
                                (i <= 1 || (i == 7 && !hasWaterOut))
                                    ? TileValueTone::Primary
                                    : TileValueTone::Secondary;
                            updateTile(i, cx, cy, colW, rightRowH, v[i], unit,
                                       color, tone);
                            SparklineBuffer<30> *sparkline = nullptr;
                            if (i == 0)
                              sparkline = &cubeTempHistory;
                            else if (i == 1)
                              sparkline = &topTempHistory;
                            updateTile(i, cx, cy, colW, rightRowH, v[i], unit,
                                       color, tone, sparkline);
                          }

                setModeMonitorAccessInfoLine(infoLine, sizeof(infoLine),
                                             state.mode);
                          snprintf(auxLine, sizeof(auxLine),
                                   ru ? "Куб %.0f | Атм %.0f mm | V %.0f"
                                      : "Cube %.0f | Atm %.0f mm | V %.0f",
                                   state.pressure.cube,
                                   state.pressure.atmosphere * 0.750062f,
                                   state.power.voltage);
                        } else if (state.mode == Mode::MASHING ||
                                   state.mode == Mode::HOLD) {
                          const bool isMash = (state.mode == Mode::MASHING);
                          uint8_t steps = isMash ? mashProfileDefault.stepCount
                                                 : holdStepsCount;
                          if (!isMash && steps == 0 && state.hold.stepCount > 0)
                            steps = state.hold.stepCount;
                          const int16_t listX = ROOT_FRAME_X;
                          const int16_t listW = ROOT_FRAME_W;
                          const int16_t rowGap = ROOT_GRID_ROW_GAP;
                          const int16_t listH = panelH;
                          int16_t rowH =
                              (steps > 0)
                                  ? (listH - (steps - 1) * rowGap) / steps
                                  : listH;
                          if (rowH < (isMash ? 28 : 32))
                            rowH = isMash ? 28 : 32;

                          if (layoutChanged) {
                            drawCard(listX, panelY, listW, listH, colorCard());
                          }

                          if (steps == 0) {
                            tft.fillRect(listX + 8, panelY + 8, listW - 16,
                                         listH - 16, colorCard());
                            tft.setTextColor(colorFg());
                            tft.setTextDatum(middle_center);
                            drawDisplayString(isMash ? "Mashing profile is empty"
                                                  : "Hold step list is empty",
                                           TFT_WIDTH / 2, panelY + listH / 2);
                          } else {
                            for (uint8_t i = 0; i < steps; i++) {
                              const int16_t ry = panelY + i * (rowH + rowGap);
                              const bool current =
                                  isMash ? (state.mashing.active &&
                                            i == state.mashing.currentStep)
                                         : (state.hold.active &&
                                            i == state.hold.currentStep);
                              drawCard(listX, ry, listW, rowH,
                                       current ? tft.color565(235, 245, 255)
                                               : colorCard());

                              float tSet = 0.0f;
                              uint16_t dSet = 0;
                              if (isMash) {
                                tSet = mashProfileDefault.steps[i].temperature;
                                dSet = mashProfileDefault.steps[i].duration;
                              } else if (i < holdStepsCount) {
                                tSet = holdStepsDefault[i].temperature;
                                dSet = holdStepsDefault[i].duration;
                              } else if (state.hold.active &&
                                         i < state.hold.stepCount) {
                                tSet = state.hold.steps[i].temperature;
                                dSet = state.hold.steps[i].duration;
                              }

                              uint8_t progress = 0;
                              if (isMash && state.mashing.active) {
                                if (i < state.mashing.currentStep)
                                  progress = 100;
                                else if (i == state.mashing.currentStep &&
                                         dSet > 0 &&
                                         state.mashing.tempInRange &&
                                         state.mashing.inRangeStartTime > 0 &&
                                         nowMs >=
                                             state.mashing.inRangeStartTime) {
                                  uint32_t elapsedSec =
                                      (nowMs - state.mashing.inRangeStartTime) /
                                      1000UL;
                                  uint32_t targetSec =
                                      static_cast<uint32_t>(dSet) * 60UL;
                                  uint32_t p =
                                      (targetSec > 0)
                                          ? ((elapsedSec * 100UL) / targetSec)
                                          : 0;
                                  if (p > 100)
                                    p = 100;
                                  progress = static_cast<uint8_t>(p);
                                }
                              } else if (!isMash && state.hold.active) {
                                if (i < state.hold.currentStep)
                                  progress = 100;
                                else if (i == state.hold.currentStep &&
                                         dSet > 0 && state.hold.tempInRange &&
                                         state.hold.inRangeStartTime > 0 &&
                                         nowMs >= state.hold.inRangeStartTime) {
                                  uint32_t elapsedSec =
                                      (nowMs - state.hold.inRangeStartTime) /
                                      1000UL;
                                  uint32_t targetSec =
                                      static_cast<uint32_t>(dSet) * 60UL;
                                  uint32_t p =
                                      (targetSec > 0)
                                          ? ((elapsedSec * 100UL) / targetSec)
                                          : 0;
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

                              tft.setTextColor(current ? colorAccent()
                                                       : colorFg());
                              tft.setTextDatum(middle_left);
                              drawDisplayString(leftBuf, listX + 10,
                                             ry + (isMash ? 10 : 12));
                              tft.setTextColor(tft.color565(90, 100, 112));
                              tft.setTextDatum(middle_right);
                              drawDisplayString(rightBuf, listX + listW - 10,
                                             ry + (isMash ? 10 : 12));

                              const int16_t pbX = listX + 10;
                              const int16_t pbY = ry + rowH - 9;
                              const int16_t pbW = listW - 20;
                              drawProgressBar(pbX, pbY, pbW, 5, progress,
                                              colorAccent());
                              tft.drawFastVLine(listX + listW / 2, ry + 6,
                                                rowH - 12,
                                                tft.color565(214, 220, 228));
                            }
                          }

                          if (isMash) {
                            setModeMonitorAccessInfoLine(infoLine,
                                                         sizeof(infoLine),
                                                         state.mode);
                            snprintf(auxLine, sizeof(auxLine),
                                     "Step %u/%u | Target %.1f C",
                                     static_cast<unsigned>(
                                         state.mashing.currentStep + 1),
                                     static_cast<unsigned>(steps),
                                     state.mashing.targetTemp);
                          } else {
                            setModeMonitorAccessInfoLine(infoLine,
                                                         sizeof(infoLine),
                                                         state.mode);
                            snprintf(auxLine, sizeof(auxLine),
                                     "Step %u/%u | Cube %.1f C",
                                     static_cast<unsigned>(
                                         state.hold.currentStep + 1),
                                     static_cast<unsigned>(steps),
                                     state.temps.cube);
                          }
                        } else if (state.mode == Mode::NBK) {
                          const int16_t g = TFT_BUTTON_GAP;
                          const int16_t tileW = (ROOT_FRAME_W - g * 2) / 3;
                          const int16_t tileH = (panelH - g) / 2;
                          const int16_t x1 = ROOT_FRAME_X;
                          const int16_t x2 = x1 + tileW + g;
                          const int16_t x3 = x2 + tileW + g;
                          const int16_t y2 = panelY + tileH + g;

                          if (layoutChanged) {
                            drawValueTileShell(x1, panelY, tileW, tileH,
                                               msg(Msg::COLUMN_BOTTOM));
                            drawValueTileShell(x2, panelY, tileW, tileH,
                                               msg(Msg::CUBE_TEMP));
                            drawValueTileShell(x3, panelY, tileW, tileH,
                                               msg(Msg::HEATER_POWER));
                            drawValueTileShell(x1, y2, tileW, tileH,
                                               msg(Msg::PUMP));
                            drawValueTileShell(x2, y2, tileW, tileH,
                                               ru ? "ДАВЛЕНИЕ" : "PRESSURE");
                            drawValueTileShell(
                                x3, y2, tileW, tileH,
                                hasWaterOut ? (ru ? "ОХЛ ВЫХ" : "WATER OUT")
                                            : (ru ? "ЦЕЛЬ" : "TARGET"));
                          }

                          char v[6][20];
                          snprintf(v[0], sizeof(v[0]), "%.1f",
                                   state.temps.columnBottom);
                          snprintf(v[1], sizeof(v[1]), "%.1f",
                                   state.temps.cube);
                          snprintf(v[2], sizeof(v[2]), "%.0f",
                                   state.power.power);
                          snprintf(v[3], sizeof(v[3]), "%.0f",
                                   state.pump.speedMlPerHour);
                          snprintf(v[4], sizeof(v[4]), "%.0f",
                                   state.pressure.cube);
                          if (hasWaterOut) {
                            snprintf(v[5], sizeof(v[5]), "%.1f",
                                     state.temps.waterOut);
                          } else {
                            snprintf(v[5], sizeof(v[5]), "%.0f",
                                     g_settings.nbk.pumpSpeedMlH);
                          }

                          updateTile(0, x1, panelY, tileW, tileH, v[0], "C",
                                     colorAccent(), TileValueTone::Primary);
                          updateTile(1, x2, panelY, tileW, tileH, v[1], "C",
                                     COLOR_DANGER, TileValueTone::Primary,
                                     &cubeTempHistory);
                          updateTile(2, x3, panelY, tileW, tileH, v[2],
                                     msg(Msg::UNIT_W), COLOR_WARNING,
                                     TileValueTone::Primary);
                          updateTile(3, x1, y2, tileW, tileH, v[3],
                                     msg(Msg::UNIT_ML_H), COLOR_SUCCESS,
                                     TileValueTone::Primary);
                          updateTile(4, x2, y2, tileW, tileH, v[4], "mm",
                                     COLOR_INFO, TileValueTone::Secondary);
                          updateTile(5, x3, y2, tileW, tileH, v[5],
                                     hasWaterOut ? "C" : msg(Msg::UNIT_ML_H),
                                     hasWaterOut ? COLOR_INFO : colorAccent(),
                                     TileValueTone::Secondary);

               setModeMonitorAccessInfoLine(infoLine, sizeof(infoLine),
                                            state.mode);
               snprintf(auxLine, sizeof(auxLine), "%s %.1fC | %s %.0f",
                        ru ? "Порог" : "Threshold",
                        g_settings.nbk.columnBottomTempThresholdC,
                        ru ? "Цель" : "Target", g_settings.nbk.targetVolumeMl);
                        } else if (state.mode == Mode::FERMENTATION) {
                          const int16_t g = TFT_BUTTON_GAP;
                          const int16_t tileW = (ROOT_FRAME_W - g) / 2;
                          const int16_t tileH = (panelH - g) / 2;
                          const int16_t x1 = ROOT_FRAME_X;
                          const int16_t x2 = x1 + tileW + g;
                          const int16_t y2 = panelY + tileH + g;

                          if (layoutChanged) {
                            drawValueTileShell(x1, panelY, tileW, tileH,
                                               msg(Msg::CUBE_TEMP));
                            drawValueTileShell(x2, panelY, tileW, tileH,
                                               ru ? "ЦЕЛЬ" : "TARGET");
                            drawValueTileShell(x1, y2, tileW, tileH,
                                               ru ? "ДОПУСК" : "BAND");
                            drawValueTileShell(x2, y2, tileW, tileH,
                                               ru ? "ВРЕМЯ" : "RUN TIME");
                          }

                          char v[4][20];
                          char runBuf[20];
                          formatDurationCompact(getModeRunElapsedSec(state),
                                                runBuf, sizeof(runBuf));
                          snprintf(v[0], sizeof(v[0]), "%.1f",
                                   state.temps.cube);
                          snprintf(v[1], sizeof(v[1]), "%.1f",
                                   g_settings.fermentation.targetTempC);
                          snprintf(v[2], sizeof(v[2]), "%.1f",
                                   g_settings.fermentation.hysteresisC);
                          snprintf(v[3], sizeof(v[3]), "%s", runBuf);

                          updateTile(
                              0, x1, panelY, tileW, tileH, v[0], "C",
                              state.temps.cube >
                                      g_settings.fermentation.targetTempC +
                                          g_settings.fermentation.hysteresisC
                                  ? COLOR_WARNING
                                  : COLOR_SUCCESS,
                              TileValueTone::Primary, &cubeTempHistory);
                          updateTile(1, x2, panelY, tileW, tileH, v[1], "C", colorAccent(),
                                     TileValueTone::Primary);
               updateTile(2, x1, y2, tileW, tileH, v[2], "C", COLOR_INFO,
                          TileValueTone::Secondary);
               updateTile(3, x2, y2, tileW, tileH, v[3], "", COLOR_PRIMARY,
                          TileValueTone::Duration);

               setModeMonitorAccessInfoLine(infoLine, sizeof(infoLine),
                                            state.mode);
               snprintf(auxLine, sizeof(auxLine), "%s | %s %.0f h",
                        g_settings.fermentation.useHeater
                            ? (ru ? "Подогрев включен" : "Heater enabled")
                            : (ru ? "Подогрев выключен" : "Heater disabled"),
                        ru ? "План" : "Plan",
                        g_settings.fermentation.durationHours);
                        } else {
                          drawCard(ROOT_FRAME_X, panelY, ROOT_FRAME_W, panelH,
                                   colorCard());
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

                              static void
                              renderControl(const SystemState &state,
                                            bool full) {
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(msg(Msg::CONTROL), false);
                                  drawTabs(UI_CONTROL);
                                }
                                const bool ru = (g_settings.language == 0);

                                if (state.mode == Mode::IDLE) {
                                  drawCard(10, UI_GLOBAL_STATUS_H + 12,
                                           TFT_WIDTH - 20, 74, colorCard());
                                  drawPanelHeader(
                                      10, UI_GLOBAL_STATUS_H + 12,
                                      TFT_WIDTH - 20,
                                      ru ? "НЕТ АКТИВНОГО ПРОЦЕССА"
                                         : "NO ACTIVE PROCESS",
                                      colorAccent());
                                  tft.setTextColor(colorMuted());
                                  tft.setTextSize(1);
                                  tft.setTextDatum(middle_center);
                                  drawDisplayString(
                                      ru ? "Для нового запуска откройте РЕЖИМЫ"
                                         : "Open MODES to start a new process",
                                      TFT_WIDTH / 2, UI_GLOBAL_STATUS_H + 58);
                                  tft.setTextDatum(top_left);
                                  drawFooterHint(
                                      ru ? "Режимы → подготовка → pre-flight"
                                         : "Modes → setup → pre-flight",
                                      colorAccent());
                                  return;
                                }

                                const uint16_t modeTone =
                                    state.paused ? COLOR_WARNING : COLOR_SUCCESS;
                                char modeBuf[96];
                                snprintf(modeBuf, sizeof(modeBuf), "%s / %s",
                                         getDisplayModeName(state.mode),
                                         getDisplayPhaseName(state));
                                drawCard(10, CTRL_STATUS_Y, TFT_WIDTH - 20,
                                         CTRL_STATUS_H, colorCard());
                                tft.fillRect(12, CTRL_STATUS_Y + 2, 7,
                                             CTRL_STATUS_H - 4, modeTone);
                                tft.setTextColor(colorFg());
                                tft.setTextDatum(middle_left);
                                tft.setTextSize(1);
                                char modeLineBuf[96];
                                copyFittedText(modeBuf, TFT_WIDTH - 70,
                                               modeLineBuf,
                                               sizeof(modeLineBuf));
                                drawDisplayString(modeLineBuf, 26,
                                               CTRL_STATUS_Y +
                                                   CTRL_STATUS_H / 2);
                                tft.setTextDatum(top_left);
                                const ControlV2::ModeStatusV2 &modeStatus =
                                    ControlV2::getLatestModeStatus();
                                const char *guidance =
                                    modeStatus.operatorMessage[0] != '\0'
                                        ? modeStatus.operatorMessage
                                        : (modeStatus.guidance.action[0] != '\0'
                                               ? modeStatus.guidance.action
                                               : getModeMonitorAccessHint(
                                                     state.mode));
                                drawCard(10, UI_GLOBAL_STATUS_H + 37,
                                         TFT_WIDTH - 20, 36, colorCard());
                                drawPanelHeader(
                                    10, UI_GLOBAL_STATUS_H + 37,
                                    TFT_WIDTH - 20,
                                    modeStatus.guidance.title[0] != '\0'
                                        ? modeStatus.guidance.title
                                        : (ru ? "ДЕЙСТВИЕ ОПЕРАТОРА"
                                              : "OPERATOR ACTION"),
                                    modeTone);
                                tft.setTextColor(colorFg());
                                tft.setTextSize(1);
                                tft.setTextDatum(middle_left);
                                char guidanceBuf[128];
                                copyFittedText(guidance, TFT_WIDTH - 44,
                                               guidanceBuf,
                                               sizeof(guidanceBuf));
                                drawDisplayString(guidanceBuf, 18,
                                                  UI_GLOBAL_STATUS_H + 63);
                                tft.setTextDatum(top_left);

                                const bool runtimeEdit =
                                    isModeMonitorEditAllowed(state.mode);
                                const bool safetyAlert =
                                    !state.safetyOk || Safety::isLatched(state);
                                drawButton(PROCESS_MENU_X1, PROCESS_MENU_Y1,
                                           PROCESS_MENU_W, PROCESS_MENU_H,
                                           ru ? "МОНИТОР" : "MONITOR",
                                           colorAccent(), TFT_WHITE);
                                drawButton(PROCESS_MENU_X2, PROCESS_MENU_Y1,
                                           PROCESS_MENU_W, PROCESS_MENU_H,
                                           ru ? "ПАРАМЕТРЫ" : "PARAMS",
                                           runtimeEdit ? COLOR_INFO
                                                       : dimmedButtonColor(),
                                           TFT_WHITE);
                                drawButton(PROCESS_MENU_X3, PROCESS_MENU_Y1,
                                           PROCESS_MENU_W, PROCESS_MENU_H,
                                           ru ? "SAFETY" : "SAFETY",
                                           safetyAlert ? COLOR_DANGER
                                                       : COLOR_WARNING,
                                           TFT_WHITE);
                                drawButton(PROCESS_MENU_X1, PROCESS_MENU_Y2,
                                           PROCESS_MENU_W, PROCESS_MENU_H,
                                           ru ? "СОБЫТИЯ" : "EVENTS",
                                           COLOR_DARK_GREY, TFT_WHITE);
                                drawButton(PROCESS_MENU_X2, PROCESS_MENU_Y2,
                                           PROCESS_MENU_W, PROCESS_MENU_H,
                                           state.paused ? msg(Msg::RESUME)
                                                        : msg(Msg::PAUSE),
                                           state.paused ? COLOR_SUCCESS
                                                        : COLOR_WARNING,
                                           TFT_WHITE);
                                drawButton(PROCESS_MENU_X3, PROCESS_MENU_Y2,
                                           PROCESS_MENU_W, PROCESS_MENU_H,
                                           msg(Msg::STOP), COLOR_DANGER,
                                           TFT_WHITE);
                                drawFooterHint(
                                    runtimeEdit
                                        ? (ru ? "Параметры этого режима можно менять в Мониторе"
                                              : "This mode allows runtime edits in Monitor")
                                        : (ru ? "Параметры режима сейчас только для просмотра"
                                              : "This mode parameters are view-only"),
                                    modeTone);
                              }

                              static void renderModePicker(
                                  const SystemState &, bool full) {
                                if (!full) {
                                  return;
                                }
                                const bool ru = (g_settings.language == 0);
                                tft.fillScreen(colorBg());
                                drawHeader(ru ? "ВЫБОР РЕЖИМА" : "MODE SELECT",
                                           false);
                                drawTabs(UI_MODE_PICKER);
                                drawCard(10, CTRL_STATUS_Y, TFT_WIDTH - 20,
                                         CTRL_STATUS_H, colorCard());
                                tft.setTextColor(colorMuted());
                                tft.setTextSize(1);
                                tft.setTextDatum(middle_center);
                                drawDisplayString(
                                    ru ? "Выбор не запускает процесс: далее подготовка"
                                       : "Selection does not start: setup follows",
                                    TFT_WIDTH / 2,
                                    CTRL_STATUS_Y + CTRL_STATUS_H / 2);
                                tft.setTextDatum(top_left);
                                drawButton(CTRL_X1, CTRL_Y1, CTRL_BW, CTRL_BH,
                                           getDisplayModeName(Mode::RECTIFICATION),
                                           colorAccent(), TFT_WHITE);
                                drawButton(CTRL_X2, CTRL_Y1, CTRL_BW, CTRL_BH,
                                           getDisplayModeName(Mode::DISTILLATION),
                                           COLOR_INFO, TFT_WHITE);
                                drawButton(CTRL_X1, CTRL_Y2, CTRL_BW, CTRL_BH,
                                           getDisplayModeName(Mode::MANUAL_RECT),
                                           tft.color565(128, 136, 144), TFT_WHITE);
                                drawButton(CTRL_X2, CTRL_Y2, CTRL_BW, CTRL_BH,
                                           getDisplayModeName(Mode::MASHING),
                                           tft.color565(114, 170, 84), TFT_WHITE);
                                drawButton(CTRL_X1, CTRL_Y3, CTRL_BW, CTRL_BH,
                                           getDisplayModeName(Mode::HOLD),
                                           tft.color565(210, 150, 56), TFT_WHITE);
                                drawButton(CTRL_X2, CTRL_Y3, CTRL_BW, CTRL_BH,
                                           getDisplayModeName(Mode::NBK),
                                           tft.color565(80, 144, 214), TFT_WHITE);
                                drawButton(CTRL_X1, CTRL_Y4, CTRL_BW, CTRL_BH,
                                           getDisplayModeName(Mode::FERMENTATION),
                                           tft.color565(72, 168, 152), TFT_WHITE);
                                drawCard(CTRL_X2, CTRL_Y4, CTRL_BW, CTRL_BH,
                                         colorCard());
                                tft.setTextColor(colorMuted());
                                tft.setTextSize(1);
                                tft.setTextDatum(middle_center);
                                drawDisplayString(
                                    ru ? "7 поддерживаемых\nрежимов"
                                       : "7 supported\nmodes",
                                    CTRL_X2 + CTRL_BW / 2,
                                    CTRL_Y4 + CTRL_BH / 2);
                                tft.setTextDatum(top_left);
                              }

                              static uint16_t preflightToneColor(
                                  const char *tone) {
                                if (tone != nullptr && strcmp(tone, "danger") == 0) {
                                  return COLOR_DANGER;
                                }
                                if (tone != nullptr && strcmp(tone, "warn") == 0) {
                                  return COLOR_WARNING;
                                }
                                if (tone != nullptr && strcmp(tone, "good") == 0) {
                                  return COLOR_SUCCESS;
                                }
                                return COLOR_INFO;
                              }

                              static void renderModeParameters(
                                  const SystemState &state, bool full) {
                                const bool ru = (g_settings.language == 0);
                                const Mode mode = ui.modeParametersMode;
                                const bool profileControlled =
                                    mode == Mode::MASHING || mode == Mode::HOLD;
                                const uint8_t pageCount =
                                    getModeParametersPageCount(mode);
                                const uint8_t page =
                                    min(ui.modeParametersPage,
                                        static_cast<uint8_t>(pageCount - 1));
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(ru ? "ПАРАМЕТРЫ РЕЖИМА"
                                                : "MODE PARAMETERS",
                                             true);
                                  drawTabs(UI_MODE_PARAMETERS);
                                }

                                char pageLabel[96];
                                if (mode == Mode::MASHING || mode == Mode::HOLD) {
                                  const uint8_t stepCount =
                                      mode == Mode::MASHING
                                          ? mashProfileDefault.stepCount
                                          : holdStepsCount;
                                  snprintf(pageLabel, sizeof(pageLabel),
                                           "%s · %s %u/%u", getDisplayModeName(mode),
                                           ru ? "ШАГ" : "STEP",
                                           static_cast<unsigned>(
                                               min<uint8_t>(
                                                   static_cast<uint8_t>(
                                                       ui.modeParametersStep + 1),
                                                   stepCount)),
                                           static_cast<unsigned>(stepCount));
                                } else if (pageCount > 1) {
                                  snprintf(pageLabel, sizeof(pageLabel),
                                           "%s · %u/%u · %s",
                                           getDisplayModeName(mode),
                                           static_cast<unsigned>(page + 1),
                                           static_cast<unsigned>(pageCount),
                                           ru ? "НАЖМИТЕ ДЛЯ СТРАНИЦЫ"
                                              : "TAP FOR PAGE");
                                } else {
                                  snprintf(pageLabel, sizeof(pageLabel), "%s",
                                           mode == Mode::IDLE
                                               ? (ru ? "АКСЕССУАРЫ"
                                                     : "ACCESSORIES")
                                               : getDisplayModeName(mode));
                                }
                                drawButton(10, 48, TFT_WIDTH - 20, 26, pageLabel,
                                           pageCount > 1 ? COLOR_INFO
                                                         : colorAccent(),
                                           TFT_WHITE);

                                bool hasField = false;
                                const uint16_t tones[] = {COLOR_INFO,
                                                          COLOR_SUCCESS,
                                                          COLOR_WARNING,
                                                          colorAccent()};
                                for (uint8_t slot = 0; slot < 4; ++slot) {
                                  EditableFields::Id field;
                                  const int16_t x = (slot % 2 == 0) ? 10 : 245;
                                  const int16_t y = slot < 2 ? 80 : 136;
                                  if (!getModeParameterField(mode, page, slot,
                                                             field)) {
                                    drawCard(x, y, 225, 50, colorCard());
                                    continue;
                                  }
                                  hasField = true;
                                  const EditableFields::Descriptor &descriptor =
                                      EditableFields::get(field);
                                  const float value =
                                      getModeParameterFieldValue(field);
                                  char valueText[32];
                                  if (descriptor.decimals == 0) {
                                    snprintf(valueText, sizeof(valueText), "%.0f",
                                             value);
                                  } else if (descriptor.decimals == 1) {
                                    snprintf(valueText, sizeof(valueText), "%.1f",
                                             value);
                                  } else {
                                    snprintf(valueText, sizeof(valueText), "%.3f",
                                             value);
                                  }
                                  drawValueTile(
                                      x, y, 225, 50,
                                      EditableFields::label(field, ru), valueText,
                                      descriptor.unit,
                                      profileControlled ? colorMuted()
                                                        : tones[slot]);
                                }

                                if (!hasField) {
                                  drawCard(10, 80, TFT_WIDTH - 20, 106,
                                           colorCard());
                                  tft.setTextColor(colorMuted());
                                  tft.setTextSize(1);
                                  tft.setTextDatum(middle_center);
                                  drawDisplayString(
                                      ru ? "Выберите совместимый профиль с шагами"
                                         : "Choose a compatible profile with steps",
                                      TFT_WIDTH / 2, 133);
                                  tft.setTextDatum(top_left);
                                }

                                if (mode == Mode::MASHING || mode == Mode::HOLD) {
                                  const uint8_t stepCount =
                                      mode == Mode::MASHING
                                          ? mashProfileDefault.stepCount
                                          : holdStepsCount;
                                  drawButton(10, 200, 225, 32,
                                             ru ? "< ПРЕДЫДУЩИЙ" : "< PREVIOUS",
                                             ui.modeParametersStep > 0
                                                 ? COLOR_INFO
                                                 : dimmedButtonColor(),
                                             TFT_WHITE);
                                  drawButton(245, 200, 225, 32,
                                             ru ? "СЛЕДУЮЩИЙ >" : "NEXT >",
                                             ui.modeParametersStep + 1 < stepCount
                                                 ? COLOR_INFO
                                                 : dimmedButtonColor(),
                                             TFT_WHITE);
                                } else if (mode == Mode::NBK) {
                                  char label[96];
                                  snprintf(label, sizeof(label), "%s: %s",
                                           ru ? "КОРРЕКЦИЯ ВЕРХА"
                                              : "TOP CORRECTION",
                                           g_settings.nbk.topTempCorrectionEnabled
                                               ? (ru ? "ВКЛ" : "ON")
                                               : (ru ? "ВЫКЛ" : "OFF"));
                                  drawButton(
                                      10, 200, TFT_WIDTH - 20, 32, label,
                                      g_settings.nbk.topTempCorrectionEnabled
                                          ? COLOR_SUCCESS
                                          : dimmedButtonColor(),
                                      TFT_WHITE);
                                } else if (mode == Mode::FERMENTATION) {
                                  drawButton(10, 200, 225, 32,
                                             g_settings.fermentation.useHeater
                                                 ? (ru ? "НАГРЕВ: ВКЛ"
                                                       : "HEATER: ON")
                                                 : (ru ? "НАГРЕВ: ВЫКЛ"
                                                       : "HEATER: OFF"),
                                             g_settings.fermentation.useHeater
                                                 ? COLOR_WARNING
                                                 : dimmedButtonColor(),
                                             TFT_WHITE);
                                  drawButton(245, 200, 225, 32,
                                             g_settings.fermentation.useCooling
                                                 ? (ru ? "ОХЛАЖД.: ВКЛ"
                                                       : "COOLING: ON")
                                                 : (ru ? "ОХЛАЖД.: ВЫКЛ"
                                                       : "COOLING: OFF"),
                                             g_settings.fermentation.useCooling
                                                 ? COLOR_SUCCESS
                                                 : dimmedButtonColor(),
                                             TFT_WHITE);
                                } else if (mode == Mode::IDLE) {
                                  drawButton(10, 200, 225, 32,
                                             g_settings.stirrer.enabled
                                                 ? (ru ? "МЕШАЛКА: ВКЛ"
                                                       : "STIRRER: ON")
                                                 : (ru ? "МЕШАЛКА: ВЫКЛ"
                                                       : "STIRRER: OFF"),
                                             g_settings.stirrer.enabled
                                                 ? COLOR_SUCCESS
                                                 : dimmedButtonColor(),
                                             TFT_WHITE);
                                  drawButton(245, 200, 225, 32,
                                             g_settings.equipment.coolingPwmEnabled
                                                 ? (ru ? "PWM: ВКЛ" : "PWM: ON")
                                                 : (ru ? "PWM: ВЫКЛ" : "PWM: OFF"),
                                             g_settings.equipment.coolingPwmEnabled
                                                 ? COLOR_INFO
                                                 : dimmedButtonColor(),
                                             TFT_WHITE);
                                }
                                drawFooterHint(
                                    profileControlled
                                        ? (ru ? "Шаги меняются в сохранённом профиле"
                                              : "Steps are edited in the saved profile")
                                        : isScreenEditable(UI_MODE_PARAMETERS, state)
                                        ? (ru ? "Коснитесь значения для редактирования"
                                              : "Tap a value to edit")
                                        : (ru ? "Редактирование доступно только в ожидании"
                                              : "Editing is available only while idle"),
                                    colorAccent());
                              }

                              static void renderModeSetup(const SystemState &state,
                                                          bool full) {
                                const bool ru = (g_settings.language == 0);
                                const int16_t cardX = 10;
                                const int16_t cardW = TFT_WIDTH - 20;
                                const int16_t buttonY = TFT_HEIGHT - UI_FOOTER_H - 36;
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(ru ? "ПОДГОТОВКА" : "MODE SETUP", true);
                                  drawTabs(UI_MODE_SETUP);
                                }

                                const Mode mode = ui.setupDraft.valid
                                                      ? ui.setupDraft.mode
                                                      : Mode::IDLE;
                                drawCard(cardX, UI_HEADER_H + 4, cardW, 30,
                                         colorCard());
                                drawPanelHeader(cardX, UI_HEADER_H + 4, cardW,
                                                getDisplayModeName(mode),
                                                colorAccent());

                                const uint16_t profileTone =
                                    !ui.setupDraft.profileRelevant
                                        ? COLOR_INFO
                                        : (ui.setupDraft.profileCompatible
                                               ? COLOR_SUCCESS
                                               : COLOR_WARNING);
                                char profileLine[96];
                                if (!ui.setupDraft.profileRelevant) {
                                  snprintf(profileLine, sizeof(profileLine), "%s",
                                           ru ? "Профиль для этого режима необязателен"
                                              : "Profile is optional for this mode");
                                } else if (ui.setupDraft.profileName[0] == '\0') {
                                  snprintf(profileLine, sizeof(profileLine), "%s",
                                           ru ? "Не выбран: выберите в Web UI"
                                              : "Not selected: choose in Web UI");
                                } else {
                                  snprintf(profileLine, sizeof(profileLine), "%s%s",
                                           ui.setupDraft.profileName,
                                           ui.setupDraft.profileCompatible
                                               ? ""
                                               : (ru ? " — другой режим" : " — wrong mode"));
                                }
                                drawCard(cardX, UI_HEADER_H + 38, cardW, 34,
                                         colorCard());
                                drawPanelHeader(cardX, UI_HEADER_H + 38, cardW,
                                                ui.setupDraft.profileRelevant
                                                    ? (ru ? "ПРОФИЛЬ · ВЫБРАТЬ"
                                                          : "PROFILE · SELECT")
                                                    : (ru ? "ПРОФИЛЬ" : "PROFILE"),
                                                profileTone);
                                tft.setTextColor(profileTone);
                                tft.setTextSize(1);
                                tft.setTextDatum(middle_left);
                                char fittedProfile[96];
                                copyFittedText(profileLine, cardW - 24,
                                               fittedProfile, sizeof(fittedProfile));
                                drawDisplayString(fittedProfile, cardX + 10,
                                                  UI_HEADER_H + 62);

                                char params[112];
                                formatSetupParameters(mode, params, sizeof(params));
                                drawCard(cardX, UI_HEADER_H + 76, cardW, 44,
                                         colorCard());
                                drawPanelHeader(cardX, UI_HEADER_H + 76, cardW,
                                                ru ? "ОСНОВНЫЕ ПАРАМЕТРЫ"
                                                   : "CORE PARAMETERS",
                                                COLOR_INFO);
                                tft.setTextColor(colorFg());
                                char fittedParams[112];
                                copyFittedText(params, cardW - 24, fittedParams,
                                               sizeof(fittedParams));
                                drawDisplayString(fittedParams, cardX + 10,
                                                  UI_HEADER_H + 106);

                                char sensors[144];
                                formatSetupRequiredSensors(state, mode, sensors,
                                                           sizeof(sensors));
                                drawCard(cardX, UI_HEADER_H + 124, cardW, 42,
                                         colorCard());
                                drawPanelHeader(cardX, UI_HEADER_H + 124, cardW,
                                                ru ? "ОБЯЗАТЕЛЬНЫЕ ДАТЧИКИ"
                                                   : "REQUIRED SENSORS",
                                                state.safetyOk ? COLOR_SUCCESS
                                                               : COLOR_DANGER);
                                tft.setTextColor(colorFg());
                                char fittedSensors[144];
                                copyFittedText(sensors, cardW - 24, fittedSensors,
                                               sizeof(fittedSensors));
                                drawDisplayString(fittedSensors, cardX + 10,
                                                  UI_HEADER_H + 154);

                                const uint16_t preflightTone =
                                    g_tftPreflight.attempted
                                        ? (g_tftPreflight.ready ? COLOR_SUCCESS
                                                                : COLOR_DANGER)
                                        : COLOR_INFO;
                                drawButton(
                                    cardX, buttonY, cardW, 32,
                                    g_tftPreflight.attempted
                                        ? (g_tftPreflight.ready
                                               ? (ru ? "PREFLIGHT: ГОТОВО"
                                                     : "PREFLIGHT: READY")
                                               : (ru ? "PREFLIGHT: ЕСТЬ БЛОК"
                                                     : "PREFLIGHT: BLOCKED"))
                                        : (ru ? "ПРОВЕРИТЬ ПЕРЕД ЗАПУСКОМ"
                                              : "CHECK BEFORE START"),
                                    preflightTone, TFT_WHITE);
                              }

                              static void renderModeConfig(const SystemState &,
                                                           bool full) {
                                const bool ru = (g_settings.language == 0);
                                const int16_t cardX = 10;
                                const int16_t cardW = TFT_WIDTH - 20;
                                const int16_t buttonY =
                                    TFT_HEIGHT - UI_FOOTER_H - 36;
                                const int16_t buttonW = (TFT_WIDTH - 23) / 2;
                                const bool dirty = hasUnsavedModeSetupDraft();
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(ru ? "ЧЕРНОВИК НАСТРОЙКИ"
                                                : "SETUP DRAFT",
                                             true);
                                  drawTabs(UI_MODE_CONFIG);
                                }

                                drawCard(cardX, UI_HEADER_H + 4, cardW, 30,
                                         colorCard());
                                drawPanelHeader(
                                    cardX, UI_HEADER_H + 4, cardW,
                                    ui.setupDraft.valid
                                        ? getDisplayModeName(ui.setupDraft.mode)
                                        : (ru ? "РЕЖИМ НЕ ВЫБРАН"
                                              : "MODE NOT SELECTED"),
                                    dirty ? COLOR_WARNING : COLOR_SUCCESS);

                                char profileLine[112];
                                if (!ui.setupDraft.profileRelevant) {
                                  snprintf(profileLine, sizeof(profileLine), "%s",
                                           ru ? "Для этого режима профиль необязателен"
                                              : "A profile is optional for this mode");
                                } else if (ui.setupDraft.profileName[0] == '\0') {
                                  snprintf(profileLine, sizeof(profileLine), "%s",
                                           ru ? "Профиль не выбран"
                                              : "No profile selected");
                                } else {
                                  snprintf(profileLine, sizeof(profileLine), "%s%s",
                                           ui.setupDraft.profileName,
                                           dirty ? (ru ? " · НЕ СОХРАНЁН"
                                                       : " · UNSAVED")
                                                 : "");
                                }
                                drawCard(cardX, UI_HEADER_H + 38, cardW, 44,
                                         colorCard());
                                drawPanelHeader(cardX, UI_HEADER_H + 38, cardW,
                                                ru ? "ВЫБРАННЫЙ ПРОФИЛЬ"
                                                   : "SELECTED PROFILE",
                                                dirty ? COLOR_WARNING
                                                      : COLOR_SUCCESS);
                                tft.setTextColor(colorFg());
                                tft.setTextSize(1);
                                tft.setTextDatum(middle_left);
                                char fittedProfile[112];
                                copyFittedText(profileLine, cardW - 24,
                                               fittedProfile,
                                               sizeof(fittedProfile));
                                drawDisplayString(fittedProfile, cardX + 10,
                                                  UI_HEADER_H + 70);

                                char statusLine[144];
                                if (ui.setupDraftMessage[0] != '\0') {
                                  strncpy(statusLine, ui.setupDraftMessage,
                                          sizeof(statusLine) - 1);
                                  statusLine[sizeof(statusLine) - 1] = '\0';
                                } else if (dirty) {
                                  snprintf(statusLine, sizeof(statusLine), "%s",
                                           ru ? "Проверьте профиль и выберите: сохранить или отбросить."
                                              : "Review the profile, then save or discard it.");
                                } else {
                                  snprintf(statusLine, sizeof(statusLine), "%s",
                                           ru ? "Нет несохранённых изменений."
                                              : "There are no unsaved changes.");
                                }
                                drawCard(cardX, UI_HEADER_H + 88, cardW, 50,
                                         colorCard());
                                drawPanelHeader(cardX, UI_HEADER_H + 88, cardW,
                                                ru ? "СТАТУС ЧЕРНОВИКА"
                                                   : "DRAFT STATUS",
                                                dirty ? COLOR_WARNING : COLOR_INFO);
                                tft.setTextColor(colorFg());
                                tft.setTextDatum(middle_left);
                                char fittedStatus[144];
                                copyFittedText(statusLine, cardW - 24,
                                               fittedStatus,
                                               sizeof(fittedStatus));
                                drawDisplayString(fittedStatus, cardX + 10,
                                                  UI_HEADER_H + 122);
                                tft.setTextDatum(top_left);

                                drawButton(10, buttonY, buttonW, 32,
                                           ru ? "ОТБРОСИТЬ" : "DISCARD",
                                           dirty ? COLOR_WARNING
                                                 : dimmedButtonColor(),
                                           TFT_WHITE);
                                drawButton(13 + buttonW, buttonY, buttonW, 32,
                                           ru ? "СОХРАНИТЬ" : "SAVE",
                                           dirty ? COLOR_SUCCESS
                                                 : dimmedButtonColor(),
                                           TFT_WHITE);
                              }

                              static void renderProfilePicker(
                                  const SystemState &, bool full) {
                                const bool ru = (g_settings.language == 0);
                                const int16_t cardX = 10;
                                const int16_t cardW = TFT_WIDTH - 20;
                                const int16_t firstRowY = UI_HEADER_H + 38;
                                const int16_t rowH = 34;
                                const int16_t rowPitch = 39;
                                const int16_t pagerY = TFT_HEIGHT - UI_FOOTER_H - 36;
                                const uint8_t pageCount = getTftProfilePageCount();
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(ru ? "ВЫБОР ПРОФИЛЯ"
                                                : "SELECT PROFILE",
                                             true);
                                  drawTabs(UI_PROFILE_PICKER);
                                }

                                drawCard(cardX, UI_HEADER_H + 4, cardW, 30,
                                         colorCard());
                                char title[96];
                                snprintf(title, sizeof(title), "%u %s",
                                         static_cast<unsigned>(
                                             g_tftProfileOptionCount),
                                         ru ? "совместимых профилей"
                                            : "compatible profiles");
                                drawPanelHeader(cardX, UI_HEADER_H + 4, cardW,
                                                title, colorAccent());

                                for (uint8_t row = 0;
                                     row < TFT_PROFILE_ROWS_PER_PAGE; ++row) {
                                  const uint8_t index =
                                      ui.profilePickerPage *
                                          TFT_PROFILE_ROWS_PER_PAGE +
                                      row;
                                  const int16_t rowY = firstRowY + row * rowPitch;
                                  if (index >= g_tftProfileOptionCount) {
                                    tft.fillRect(cardX, rowY, cardW, rowH,
                                                 colorBg());
                                    continue;
                                  }
                                  const TftProfileOption &option =
                                      g_tftProfileOptions[index];
                                  const bool selected =
                                      strcmp(option.id,
                                             ui.setupDraft.profileId) == 0;
                                  const uint16_t tone = selected
                                                            ? COLOR_SUCCESS
                                                            : colorAccent();
                                  drawCard(cardX, rowY, cardW, rowH,
                                           selected ? colorButtonBody()
                                                    : colorCard());
                                  tft.fillRect(cardX + 2, rowY + 2, 7,
                                               rowH - 4, tone);
                                  tft.setTextColor(colorFg());
                                  tft.setTextSize(1);
                                  tft.setTextDatum(middle_left);
                                  char name[64];
                                  copyFittedText(option.name, cardW - 138, name,
                                                 sizeof(name));
                                  drawDisplayString(name, cardX + 16,
                                                    rowY + rowH / 2);
                                  char meta[40];
                                  snprintf(meta, sizeof(meta), "%s · %u",
                                           option.builtin
                                               ? (ru ? "встр." : "built-in")
                                               : (ru ? "польз." : "user"),
                                           static_cast<unsigned>(option.useCount));
                                  tft.setTextColor(selected ? COLOR_SUCCESS
                                                            : colorMuted());
                                  tft.setTextDatum(middle_right);
                                  drawDisplayString(meta, cardX + cardW - 9,
                                                    rowY + rowH / 2);
                                }
                                tft.setTextDatum(top_left);

                                drawButton(10, pagerY, 132, 32,
                                           ru ? "НАЗАД" : "PREV",
                                           ui.profilePickerPage > 0
                                               ? COLOR_INFO
                                               : dimmedButtonColor(),
                                           TFT_WHITE);
                                drawCard(148, pagerY, TFT_WIDTH - 296, 32,
                                         colorCard());
                                tft.setTextColor(colorMuted());
                                tft.setTextSize(1);
                                tft.setTextDatum(middle_center);
                                char page[20];
                                snprintf(page, sizeof(page), "%u / %u",
                                         static_cast<unsigned>(
                                             ui.profilePickerPage + 1),
                                         static_cast<unsigned>(pageCount));
                                drawDisplayString(page, TFT_WIDTH / 2,
                                                  pagerY + 16);
                                tft.setTextDatum(top_left);
                                drawButton(TFT_WIDTH - 142, pagerY, 132, 32,
                                           ru ? "ДАЛЕЕ" : "NEXT",
                                           ui.profilePickerPage + 1 < pageCount
                                               ? COLOR_INFO
                                               : dimmedButtonColor(),
                                           TFT_WHITE);

                                if (g_tftProfileOptionCount == 0) {
                                  drawCard(cardX, firstRowY, cardW,
                                           rowH * 2 + 5, colorCard());
                                  tft.setTextColor(colorMuted());
                                  tft.setTextSize(1);
                                  tft.setTextDatum(middle_center);
                                  drawDisplayString(
                                      ru ? "Нет сохранённых совместимых профилей"
                                         : "No compatible saved profiles",
                                      TFT_WIDTH / 2, firstRowY + rowH);
                                  tft.setTextDatum(top_left);
                                }
                              }

                              static void renderSafetyView(
                                  const SystemState &state, bool full) {
                                const bool ru = (g_settings.language == 0);
                                const int16_t cardX = 10;
                                const int16_t cardW = TFT_WIDTH - 20;
                                const int16_t actionY =
                                    TFT_HEIGHT - UI_FOOTER_H - 36;
                                const int16_t actionW = (TFT_WIDTH - 23) / 2;
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(ru ? "SAFETY" : "SAFETY", true);
                                  drawTabs(UI_SAFETY_VIEW);
                                }

                                const bool alarmActive =
                                    state.currentAlarm.type != AlarmType::NONE;
                                const bool latched = Safety::isLatched(state);
                                char resetReason[128] = "";
                                const bool resetAvailable =
                                    !latched || Safety::canResetNow(
                                                    state, g_settings,
                                                    resetReason,
                                                    sizeof(resetReason));
                                const TftHmiPolicyLogic::PostAlarmAction
                                    safeAction =
                                        TftHmiPolicyLogic::selectPostAlarmAction(
                                            alarmActive,
                                            state.currentAlarm.acknowledged,
                                            resetAvailable);
                                const uint16_t alarmTone =
                                    latched ? COLOR_DANGER
                                            : (alarmActive ? COLOR_WARNING
                                                           : COLOR_SUCCESS);
                                const char *alarmTitle =
                                    latched ? (ru ? "ЗАЩЁЛКНУТАЯ АВАРИЯ"
                                                  : "LATCHED ALARM")
                                            : (alarmActive
                                                   ? (ru ? "АКТИВНАЯ АВАРИЯ"
                                                         : "ACTIVE ALARM")
                                                   : (ru ? "SAFETY: НОРМА"
                                                         : "SAFETY: NORMAL"));
                                const char *alarmDetail =
                                    alarmActive && state.currentAlarm.message[0] != '\0'
                                        ? state.currentAlarm.message
                                        : (ui.safetyActionMessage[0] != '\0'
                                               ? ui.safetyActionMessage
                                               : (latched
                                                      ? resetReason
                                                      : (ru ? "Критичных условий нет."
                                                            : "No critical conditions.")));
                                drawCard(cardX, UI_HEADER_H + 4, cardW, 44,
                                         colorCard());
                                drawPanelHeader(cardX, UI_HEADER_H + 4, cardW,
                                                alarmTitle, alarmTone);
                                tft.setTextColor(colorFg());
                                tft.setTextSize(1);
                                tft.setTextDatum(middle_left);
                                char alarmText[144];
                                copyFittedText(alarmDetail, cardW - 24,
                                               alarmText, sizeof(alarmText));
                                drawDisplayString(alarmText, cardX + 10,
                                                  UI_HEADER_H + 36);

                                char requiredSensors[144];
                                formatSetupRequiredSensors(state, state.mode,
                                                           requiredSensors,
                                                           sizeof(requiredSensors));
                                drawCard(cardX, UI_HEADER_H + 52, cardW, 36,
                                         colorCard());
                                drawPanelHeader(cardX, UI_HEADER_H + 52, cardW,
                                                ru ? "ОБЯЗАТЕЛЬНЫЕ ДАТЧИКИ"
                                                   : "REQUIRED SENSORS",
                                                state.safetyOk ? COLOR_SUCCESS
                                                               : COLOR_DANGER);
                                tft.setTextColor(colorFg());
                                char sensorText[144];
                                copyFittedText(requiredSensors, cardW - 24,
                                               sensorText, sizeof(sensorText));
                                drawDisplayString(sensorText, cardX + 10,
                                                  UI_HEADER_H + 79);

                                drawCard(cardX, UI_HEADER_H + 92, cardW, 76,
                                         colorCard());
                                drawPanelHeader(cardX, UI_HEADER_H + 92, cardW,
                                                ru ? "ПОРОГИ И КОНТУРЫ"
                                                   : "LIMITS AND CHANNELS",
                                                COLOR_INFO);
                                char pressure[32], tsa[32], water[48], leak[64];
                                snprintf(pressure, sizeof(pressure), "%.0f / %.0f mm",
                                         state.pressure.cube,
                                         g_settings.safety.pressureMaxMmHg);
                                snprintf(tsa, sizeof(tsa), "%.1f / %.1f C",
                                         state.temps.tsa,
                                         g_settings.safety.tsaMaxC);
                                snprintf(water, sizeof(water), "%.1f / %.0f C | in %.1f",
                                         state.temps.waterOut,
                                         g_settings.safety.waterOutMaxC,
                                         state.temps.waterIn);
                                if (g_settings.equipment.leakSensorEnabled) {
                                  snprintf(leak, sizeof(leak), "%.2f / %.2f V%s",
                                           state.leak.voltage,
                                           g_settings.equipment.leakThresholdV,
                                           state.leak.triggered
                                               ? (ru ? " · ТРЕВОГА" : " · ALARM")
                                               : "");
                                } else {
                                  snprintf(leak, sizeof(leak), "%s",
                                           ru ? "не установлен" : "not installed");
                                }
                                drawCompactKeyValueRow(
                                    cardX + 8, UI_HEADER_H + 123, cardW - 16,
                                    ru ? "ДАВЛ." : "PRESS.", pressure,
                                    COLOR_INFO);
                                drawCompactKeyValueRow(
                                    cardX + 8, UI_HEADER_H + 138, cardW - 16,
                                    "TSA", tsa, COLOR_WARNING);
                                drawCompactKeyValueRow(
                                    cardX + 8, UI_HEADER_H + 153, cardW - 16,
                                    ru ? "ВОДА" : "WATER", water, COLOR_INFO);
                                drawCompactKeyValueRow(
                                    cardX + 8, UI_HEADER_H + 168, cardW - 16,
                                    ru ? "ПРОТЕЧКА" : "LEAK", leak,
                                    state.leak.triggered ? COLOR_DANGER
                                                         : COLOR_SUCCESS);
                                tft.setTextDatum(top_left);

                                drawButton(
                                    10, actionY, actionW, 32,
                                    ru ? "ПОДТВЕРДИТЬ" : "ACKNOWLEDGE",
                                    safeAction == TftHmiPolicyLogic::PostAlarmAction::Acknowledge
                                        ? COLOR_WARNING
                                        : dimmedButtonColor(),
                                    TFT_WHITE);
                                drawButton(
                                    13 + actionW, actionY, actionW, 32,
                                    ru ? "СБРОСИТЬ" : "RESET",
                                    safeAction == TftHmiPolicyLogic::PostAlarmAction::Reset
                                        ? COLOR_SUCCESS
                                        : dimmedButtonColor(),
                                    TFT_WHITE);
                              }

                              static uint16_t eventLevelColor(uint8_t level) {
                                return level >= 2 ? COLOR_DANGER
                                                  : (level == 1 ? COLOR_WARNING
                                                                : COLOR_INFO);
                              }

                              static void renderProcessEvents(
                                  const SystemState &, bool full) {
                                const bool ru = (g_settings.language == 0);
                                const int16_t cardX = 10;
                                const int16_t cardW = TFT_WIDTH - 20;
                                const int16_t firstRowY = UI_HEADER_H + 38;
                                const int16_t rowH = 29;
                                const int16_t rowPitch = 33;
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(ru ? "СОБЫТИЯ" : "EVENTS", true);
                                  drawTabs(UI_PROCESS_EVENTS);
                                }

                                char title[64];
                                snprintf(title, sizeof(title), "%u %s",
                                         static_cast<unsigned>(g_tftEvents.count),
                                         ru ? "последних событий"
                                            : "recent events");
                                drawCard(cardX, UI_HEADER_H + 4, cardW, 30,
                                         colorCard());
                                drawPanelHeader(cardX, UI_HEADER_H + 4, cardW,
                                                title, COLOR_INFO);
                                for (uint8_t i = 0; i < 4; ++i) {
                                  const int16_t rowY = firstRowY + i * rowPitch;
                                  const bool present = i < g_tftEvents.count;
                                  drawCard(cardX, rowY, cardW, rowH, colorCard());
                                  const uint16_t tone = present
                                                            ? eventLevelColor(
                                                                  g_tftEvents.level[i])
                                                            : colorNavInactive();
                                  tft.fillRect(cardX + 2, rowY + 2, 6,
                                               rowH - 4, tone);
                                  tft.setTextColor(present ? colorFg()
                                                           : colorMuted());
                                  tft.setTextSize(1);
                                  tft.setTextDatum(middle_left);
                                  char eventText[128];
                                  copyFittedText(
                                      present ? g_tftEvents.message[i]
                                              : (i == 0
                                                     ? (ru ? "Событий пока нет"
                                                           : "No events yet")
                                                     : ""),
                                      cardW - 24, eventText,
                                      sizeof(eventText));
                                  drawDisplayString(eventText, cardX + 15,
                                                    rowY + rowH / 2);
                                }
                                tft.setTextDatum(top_left);
                                drawButton(10, TFT_HEIGHT - UI_FOOTER_H - 36,
                                           TFT_WIDTH - 20, 32,
                                           ru ? "ОБНОВИТЬ СОБЫТИЯ"
                                              : "REFRESH EVENTS",
                                           COLOR_INFO, TFT_WHITE);
                              }

                              static void renderProcessParams(
                                  const SystemState &state, bool full) {
                                const bool ru = (g_settings.language == 0);
                                const int16_t cardX = 10;
                                const int16_t cardW = TFT_WIDTH - 20;
                                const int16_t y = UI_HEADER_H + 86;
                                const bool editable =
                                    isScreenEditable(UI_PROCESS_PARAMS, state);
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(ru ? "ПАРАМЕТРЫ ПРОЦЕССА"
                                                : "PROCESS PARAMETERS",
                                             true);
                                  drawTabs(UI_PROCESS_PARAMS);
                                }

                                drawCard(cardX, UI_HEADER_H + 4, cardW, 34,
                                         colorCard());
                                drawPanelHeader(
                                    cardX, UI_HEADER_H + 4, cardW,
                                    getDisplayModeName(state.mode),
                                    editable ? COLOR_SUCCESS : COLOR_INFO);
                                tft.setTextColor(colorFg());
                                tft.setTextSize(1);
                                tft.setTextDatum(middle_left);
                                char phase[96];
                                snprintf(phase, sizeof(phase), "%s / %s",
                                         getDisplayPhaseName(state),
                                         state.paused
                                             ? (ru ? "ПАУЗА" : "PAUSED")
                                             : (ru ? "РАБОТА" : "RUNNING"));
                                drawDisplayString(phase, cardX + 10,
                                                  UI_HEADER_H + 29);

                                drawCard(cardX, UI_HEADER_H + 42, cardW, 38,
                                         colorCard());
                                drawPanelHeader(
                                    cardX, UI_HEADER_H + 42, cardW,
                                    editable
                                        ? (ru ? "РАЗРЕШЕНО НА ХОДУ"
                                              : "RUNTIME ALLOWLIST")
                                        : (ru ? "ТОЛЬКО ПРОСМОТР"
                                              : "VIEW ONLY"),
                                    editable ? COLOR_SUCCESS
                                             : dimmedButtonColor());
                                tft.setTextColor(colorFg());
                                char policy[128];
                                copyFittedText(getModeMonitorAccessHint(state.mode),
                                               cardW - 24, policy,
                                               sizeof(policy));
                                drawDisplayString(policy, cardX + 10,
                                                  UI_HEADER_H + 71);
                                tft.setTextDatum(top_left);

                                if (!editable) {
                                  drawCard(cardX, y, cardW, 64, colorCard());
                                  tft.setTextColor(colorMuted());
                                  tft.setTextSize(1);
                                  tft.setTextDatum(middle_center);
                                  drawDisplayString(
                                      ru ? "Уставки этого режима не меняются во время процесса"
                                         : "This mode settings cannot change while running",
                                      TFT_WIDTH / 2, y + 32);
                                  tft.setTextDatum(top_left);
                                  return;
                                }

                                if (state.mode == Mode::DISTILLATION) {
                                  char power[48], endTemp[48];
                                  snprintf(power, sizeof(power), "%s %.0f%% / %.0f W",
                                           ru ? "МОЩН." : "POWER",
                                           distUi.powerPercent, state.power.power);
                                  snprintf(endTemp, sizeof(endTemp), "%s %.1f C",
                                           ru ? "КОНЕЦ" : "END",
                                           distUi.endTempC);
                                  drawButton(10, y, 225, 62, power, COLOR_WARNING,
                                             TFT_WHITE);
                                  drawButton(245, y, 225, 62, endTemp, COLOR_INFO,
                                             TFT_WHITE);
                                } else if (state.mode == Mode::MANUAL_RECT) {
                                  char speed[40], power[40], heads[40], body[40],
                                      tails[40];
                                  snprintf(speed, sizeof(speed), "%s %.0f ml/h",
                                           ru ? "ОТБОР" : "TAKEOFF",
                                           manualRectUi.speedMlH);
                                  snprintf(power, sizeof(power), "%s %.0f%%",
                                           ru ? "МОЩН." : "POWER",
                                           manualRectUi.powerPercent);
                                  snprintf(heads, sizeof(heads), "%s %.0f ml",
                                           ru ? "ГОЛОВЫ" : "HEADS",
                                           manualRectUi.headsTargetMl);
                                  snprintf(body, sizeof(body), "%s %.0f ml",
                                           ru ? "ТЕЛО" : "BODY",
                                           manualRectUi.bodyTargetMl);
                                  snprintf(tails, sizeof(tails), "%s %.0f ml",
                                           ru ? "ХВОСТЫ" : "TAILS",
                                           manualRectUi.tailsTargetMl);
                                  drawButton(10, y, 225, 36, speed, COLOR_SUCCESS,
                                             TFT_WHITE);
                                  drawButton(245, y, 225, 36, power, COLOR_WARNING,
                                             TFT_WHITE);
                                  drawButton(10, y + 40, 225, 36, heads, COLOR_INFO,
                                             TFT_WHITE);
                                  drawButton(245, y + 40, 225, 36, body, COLOR_INFO,
                                             TFT_WHITE);
                                  drawButton(10, y + 80, TFT_WIDTH - 20, 36, tails,
                                             COLOR_INFO, TFT_WHITE);
                                } else if (state.mode == Mode::MASHING) {
                                  const uint8_t step = state.mashing.currentStep;
                                  char temp[48], duration[48];
                                  snprintf(temp, sizeof(temp), "%s %u: %.1f C",
                                           ru ? "ШАГ" : "STEP",
                                           static_cast<unsigned>(step + 1),
                                           step < mashProfileDefault.stepCount
                                               ? mashProfileDefault.steps[step]
                                                     .temperature
                                               : 0.0f);
                                  snprintf(duration, sizeof(duration), "%s %u min",
                                           ru ? "ВРЕМЯ" : "TIME",
                                           step < mashProfileDefault.stepCount
                                               ? static_cast<unsigned>(
                                                     mashProfileDefault.steps[step]
                                                         .duration)
                                               : 0);
                                  drawButton(10, y, 225, 62, temp, COLOR_SUCCESS,
                                             TFT_WHITE);
                                  drawButton(245, y, 225, 62, duration, COLOR_INFO,
                                             TFT_WHITE);
                                } else if (state.mode == Mode::HOLD) {
                                  const uint8_t step = state.hold.currentStep;
                                  char temp[48], duration[48];
                                  snprintf(temp, sizeof(temp), "%s %u: %.1f C",
                                           ru ? "ШАГ" : "STEP",
                                           static_cast<unsigned>(step + 1),
                                           step < holdStepsCount
                                               ? holdStepsDefault[step].temperature
                                               : 0.0f);
                                  snprintf(duration, sizeof(duration), "%s %u min",
                                           ru ? "ВРЕМЯ" : "TIME",
                                           step < holdStepsCount
                                               ? static_cast<unsigned>(
                                                     holdStepsDefault[step].duration)
                                               : 0);
                                  drawButton(10, y, 225, 62, temp, COLOR_SUCCESS,
                                             TFT_WHITE);
                                  drawButton(245, y, 225, 62, duration, COLOR_INFO,
                                             TFT_WHITE);
                                }
                              }

                              static void renderPreflight(const SystemState &,
                                                          bool full) {
                                const bool ru = (g_settings.language == 0);
                                const int16_t cardX = 10;
                                const int16_t cardW = TFT_WIDTH - 20;
                                const int16_t buttonY = TFT_HEIGHT - UI_FOOTER_H - 36;
                                const int16_t refreshW = 112;
                                const int16_t pagerButtonW = 42;
                                const int16_t prevX = 10 + refreshW + 6;
                                const int16_t pageX = prevX + pagerButtonW + 4;
                                const int16_t pageW = 44;
                                const int16_t nextX = pageX + pageW + 4;
                                const int16_t startX = nextX + pagerButtonW + 6;
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(ru ? "PREFLIGHT" : "PREFLIGHT", true);
                                  drawTabs(UI_PREFLIGHT);
                                }

                                const uint16_t summaryTone =
                                    !g_tftPreflight.attempted
                                        ? COLOR_INFO
                                        : (g_tftPreflight.ready
                                               ? (g_tftPreflight.warningCount > 0
                                                      ? COLOR_WARNING
                                                      : COLOR_SUCCESS)
                                               : COLOR_DANGER);
                                const uint8_t pageCount =
                                    getTftPreflightPageCount();
                                if (ui.preflightPage >= pageCount) {
                                  ui.preflightPage = pageCount - 1;
                                }
                                char summaryTitle[96];
                                snprintf(
                                    summaryTitle, sizeof(summaryTitle), "%s · %u/%u",
                                    g_tftPreflight.attempted
                                        ? g_tftPreflight.title
                                        : (ru ? "ПРОВЕРКА НЕ ВЫПОЛНЕНА"
                                              : "CHECK NOT RUN"),
                                    static_cast<unsigned>(ui.preflightPage + 1),
                                    static_cast<unsigned>(pageCount));
                                drawCard(cardX, UI_HEADER_H + 4, cardW, 46,
                                         colorCard());
                                drawPanelHeader(
                                    cardX, UI_HEADER_H + 4, cardW, summaryTitle,
                                    summaryTone);
                                tft.setTextColor(colorFg());
                                tft.setTextSize(1);
                                tft.setTextDatum(middle_left);
                                char summary[144];
                                copyFittedText(
                                    g_tftPreflight.attempted
                                        ? g_tftPreflight.detail
                                        : (ru ? "Нажмите «Обновить» для backend-проверки"
                                              : "Tap Refresh for the backend check"),
                                    cardW - 24, summary, sizeof(summary));
                                drawDisplayString(summary, cardX + 10,
                                                  UI_HEADER_H + 37);

                                for (uint8_t row = 0;
                                     row < TFT_PREFLIGHT_ROWS_PER_PAGE; ++row) {
                                  const uint8_t index =
                                      ui.preflightPage *
                                          TFT_PREFLIGHT_ROWS_PER_PAGE +
                                      row;
                                  const int16_t rowY = UI_HEADER_H + 54 + row * 39;
                                  const int16_t rowH = 36;
                                  drawCard(cardX, rowY, cardW, rowH, colorCard());
                                  const bool hasItem =
                                      index < g_tftPreflight.itemCount;
                                  const uint16_t tone =
                                      hasItem ? preflightToneColor(
                                                    g_tftPreflight.itemTone[index])
                                              : colorNavInactive();
                                  tft.fillRect(cardX + 2, rowY + 2, 6, rowH - 4,
                                               tone);
                                  tft.setTextColor(hasItem ? colorFg() : colorMuted());
                                  tft.setTextSize(1);
                                  tft.setTextDatum(top_left);
                                  char itemText[64];
                                  copyFittedText(
                                      hasItem ? g_tftPreflight.itemTitle[index]
                                              : (row == 0
                                                     ? (ru ? "Список появится после проверки"
                                                           : "Checklist appears after refresh")
                                                     : ""),
                                      cardW - 22, itemText, sizeof(itemText),
                                      "...");
                                  drawDisplayString(itemText, cardX + 15,
                                                    rowY + 4);
                                  if (hasItem) {
                                    tft.setTextColor(colorMuted());
                                    char detail[144];
                                    copyFittedText(
                                        g_tftPreflight.itemDetail[index],
                                        cardW - 22, detail, sizeof(detail), "...");
                                    drawDisplayString(detail, cardX + 15,
                                                      rowY + 19);
                                  }
                                }
                                tft.setTextDatum(top_left);

                                drawButton(10, buttonY, refreshW, 32,
                                           ru ? "ОБНОВИТЬ" : "REFRESH",
                                           COLOR_INFO, TFT_WHITE);
                                drawButton(prevX, buttonY, pagerButtonW, 32,
                                           "<",
                                           ui.preflightPage > 0
                                               ? COLOR_INFO
                                               : dimmedButtonColor(),
                                           TFT_WHITE);
                                drawCard(pageX, buttonY, pageW, 32, colorCard());
                                tft.setTextColor(colorMuted());
                                tft.setTextSize(1);
                                tft.setTextDatum(middle_center);
                                char page[20];
                                snprintf(page, sizeof(page), "%u/%u",
                                         static_cast<unsigned>(
                                             ui.preflightPage + 1),
                                         static_cast<unsigned>(pageCount));
                                drawDisplayString(page, pageX + pageW / 2,
                                                  buttonY + 16);
                                tft.setTextDatum(top_left);
                                drawButton(nextX, buttonY, pagerButtonW, 32,
                                           ">",
                                           ui.preflightPage + 1 < pageCount
                                               ? COLOR_INFO
                                               : dimmedButtonColor(),
                                           TFT_WHITE);
                                drawButton(
                                    startX, buttonY, TFT_WIDTH - startX - 10, 32,
                                    ru ? "СТАРТ" : "START",
                                    (g_tftPreflight.attempted &&
                                     g_tftPreflight.ready)
                                        ? COLOR_SUCCESS
                                        : dimmedButtonColor(),
                                    TFT_WHITE);
                              }

                              static void renderSettings(bool full) {
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(msg(Msg::SETTINGS), false);
                                  drawTabs(UI_SETTINGS);
                                }
                                const bool ru = (g_settings.language == 0);

                                // --- КАРТОЧКА 1: Оборудование ---
                                drawCard(SETTINGS_CARD_X1, SETTINGS_CARD_Y1,
                                         SETTINGS_CARD_W, SETTINGS_CARD_H,
                                         colorCard());
                                drawPanelHeader(
                                    SETTINGS_CARD_X1, SETTINGS_CARD_Y1,
                                    SETTINGS_CARD_W, msg(Msg::EQUIPMENT),
                                    colorAccent());
                                {
                                  char b1[16], b2[20];
                                  snprintf(b1, sizeof(b1), "%u %s",
                                           g_settings.equipment.heaterPowerW,
                                           msg(Msg::UNIT_W));
                                  snprintf(b2, sizeof(b2), "%u %s",
                                           g_settings.equipment.columnHeightMm,
                                           msg(Msg::UNIT_MM));
                                  drawCompactKeyValueRow(
                                      SETTINGS_CARD_X1 + 8,
                                      SETTINGS_CARD_Y1 + 34,
                                      SETTINGS_CARD_W - 16,
                                      tftText(TftTextId::PowerShort), b1,
                                      COLOR_WARNING);
                                  drawCompactKeyValueRow(
                                      SETTINGS_CARD_X1 + 8,
                                      SETTINGS_CARD_Y1 + 50,
                                      SETTINGS_CARD_W - 16,
                                      tftText(TftTextId::ColumnShort), b2,
                                      COLOR_INFO);
                                }

                                // --- КАРТОЧКА 2: Параметры ректификации ---
                                drawCard(SETTINGS_CARD_X2, SETTINGS_CARD_Y1,
                                         SETTINGS_CARD_W, SETTINGS_CARD_H,
                                         colorCard());
                                drawPanelHeader(
                                    SETTINGS_CARD_X2, SETTINGS_CARD_Y1,
                                    SETTINGS_CARD_W, msg(Msg::RECT_PARAMS),
                                    COLOR_INFO);
                                {
                                  char b1[24], b2[24];
                                  snprintf(
                                      b1, sizeof(b1), "%s",
                                      rectFeedstockName(
                                          g_settings.rectParams.feedstock, ru));
                                  snprintf(b2, sizeof(b2), "%.0f/%.0f/%.0f%%",
                                           g_settings.rectParams.headsPercent,
                                           g_settings.rectParams.bodyPercent,
                                           g_settings.rectParams.tailsPercent);
                                  drawCompactKeyValueRow(
                                      SETTINGS_CARD_X2 + 8,
                                      SETTINGS_CARD_Y1 + 34,
                                      SETTINGS_CARD_W - 16,
                                      tftText(TftTextId::FeedShort), b1,
                                      colorAccent());
                                  drawCompactKeyValueRow(
                                      SETTINGS_CARD_X2 + 8,
                                      SETTINGS_CARD_Y1 + 50,
                                      SETTINGS_CARD_W - 16,
                                      tftText(TftTextId::CutsShort), b2,
                                      COLOR_INFO);
                                }

                                // --- КАРТОЧКА 3: Параметры дистилляции ---
                                drawCard(SETTINGS_CARD_X1, SETTINGS_CARD_Y2,
                                         SETTINGS_CARD_W, SETTINGS_CARD_H,
                                         colorCard());
                                drawPanelHeader(
                                    SETTINGS_CARD_X1, SETTINGS_CARD_Y2,
                                    SETTINGS_CARD_W, msg(Msg::DIST_PARAMS),
                                    COLOR_WARNING);
                                {
                                  char b1[16], b2[16];
                                  snprintf(b1, sizeof(b1), "%.0f %s",
                                           distUi.speedMlH,
                                           msg(Msg::UNIT_ML_H));
                                  snprintf(b2, sizeof(b2), "%.0f ml",
                                           distUi.targetVolumeMl);
                                  drawCompactKeyValueRow(
                                      SETTINGS_CARD_X1 + 8,
                                      SETTINGS_CARD_Y2 + 34,
                                      SETTINGS_CARD_W - 16,
                                      tftText(TftTextId::SpeedShort), b1,
                                      COLOR_PRIMARY);
                                  drawCompactKeyValueRow(
                                      SETTINGS_CARD_X1 + 8,
                                      SETTINGS_CARD_Y2 + 50,
                                      SETTINGS_CARD_W - 16,
                                      tftText(TftTextId::TargetShort), b2,
                                      COLOR_SUCCESS);
                                }

                                // --- КАРТОЧКА 4: Калибровка ---
                                drawCard(SETTINGS_CARD_X2, SETTINGS_CARD_Y2,
                                         SETTINGS_CARD_W, SETTINGS_CARD_H,
                                         colorCard());
                                drawPanelHeader(
                                    SETTINGS_CARD_X2, SETTINGS_CARD_Y2,
                                    SETTINGS_CARD_W, msg(Msg::CALIBRATION),
                                    COLOR_SUCCESS);
                                {
                                  char b1[20];
                                  snprintf(b1, sizeof(b1), "%.3f %s",
                                           g_settings.pumpCal.mlPerRevolution,
                                           msg(Msg::UNIT_ML_R));
                                  drawCompactKeyValueRow(SETTINGS_CARD_X2 + 8,
                                                         SETTINGS_CARD_Y2 + 34,
                                                         SETTINGS_CARD_W - 16,
                                                         msg(Msg::PUMP), b1,
                                                         COLOR_INFO);
                                  drawCompactKeyValueRow(
                                      SETTINGS_CARD_X2 + 8,
                                      SETTINGS_CARD_Y2 + 50,
                                      SETTINGS_CARD_W - 16,
                                      tftText(TftTextId::TouchShort),
                                      g_settings.touchCal.valid
                                          ? tftText(TftTextId::CalDoneShort)
                                          : tftText(TftTextId::CalRawShort),
                                      g_settings.touchCal.valid
                                          ? COLOR_SUCCESS
                                          : COLOR_WARNING);
                                }

                                // --- СТРОКА БЫСТРЫХ ПЕРЕКЛЮЧАТЕЛЕЙ ---
                                {
                                  const bool dark = (g_settings.theme == 1);
                                  char themeLabel[24];
                                  snprintf(themeLabel, sizeof(themeLabel),
                                           "%s:%s", msg(Msg::THEME),
                                           dark ? (ru ? "Тмн" : "Drk")
                                                : (ru ? "Свт" : "Lgt"));
                                  drawButton(SETTINGS_TOGGLE_X1,
                                             SETTINGS_TOGGLE_Y,
                                             SETTINGS_TOGGLE_W,
                                             SETTINGS_TOGGLE_H, themeLabel,
                                             dark ? tft.color565(58, 64, 72)
                                                  : tft.color565(160, 170, 178),
                                             TFT_WHITE);
                                }
                                {
                                  char soundLabel[20];
                                  snprintf(soundLabel, sizeof(soundLabel),
                                           "%s:%s", msg(Msg::SOUND),
                                           g_settings.soundEnabled
                                               ? (ru ? "ВКЛ" : "ON")
                                               : (ru ? "ВЫКЛ" : "OFF"));
                                  drawButton(
                                      SETTINGS_TOGGLE_X2, SETTINGS_TOGGLE_Y,
                                      SETTINGS_TOGGLE_W, SETTINGS_TOGGLE_H,
                                      soundLabel,
                                      g_settings.soundEnabled ? COLOR_SUCCESS
                                                              : COLOR_DARK_GREY,
                                      TFT_WHITE);
                                }
                                {
                                  char langLabel[20];
                                  snprintf(langLabel, sizeof(langLabel),
                                           "%s:%s", msg(Msg::LANGUAGE),
                                           g_settings.language == 0 ? "RU"
                                                                    : "EN");
                                  drawButton(
                                      SETTINGS_TOGGLE_X3, SETTINGS_TOGGLE_Y,
                                      SETTINGS_TOGGLE_W, SETTINGS_TOGGLE_H,
                                      langLabel, COLOR_INFO, TFT_WHITE);
                                }
                                {
                                  char refreshLabel[24];
                                  snprintf(
                                      refreshLabel, sizeof(refreshLabel),
                                      "%s:%s", ru ? "Экран" : "Display",
                                      getDisplayRefreshProfileCompactName(
                                          g_settings.displaySettings
                                              .refreshProfile));
                                  drawButton(
                                      SETTINGS_PROFILE_X, SETTINGS_PROFILE_Y,
                                      SETTINGS_PROFILE_W, SETTINGS_PROFILE_H,
                                      refreshLabel,
                                      getDisplayRefreshProfileColor(
                                          g_settings.displaySettings
                                              .refreshProfile),
                                      TFT_WHITE);
                                }
                                {
                                  char stirrerLabel[24];
                                  snprintf(stirrerLabel, sizeof(stirrerLabel),
                                           "%s / PWM", ru ? "УЗЛЫ" : "AUX");
                                  drawButton(SETTINGS_STIRRER_X,
                                             SETTINGS_STIRRER_Y,
                                             SETTINGS_STIRRER_W,
                                             SETTINGS_STIRRER_H, stirrerLabel,
                                             colorAccent(),
                                             TFT_WHITE);
                                }

                                drawFooterHint(tftText(TftTextId::SettingsHint),
                                               colorAccent());
                              }

                              static void renderEquipment(bool full) {
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(msg(Msg::EQUIPMENT), true);
                                  drawTabs(UI_SETTINGS);
                                }
                                const int16_t x1 = 10;
                                const int16_t x2 = 245;
                                const int16_t y1 = 48;
                                const int16_t y2 = 138;
                                const int16_t tileW = 225;
                                const int16_t tileH = 78;
                                char buf[32];

                                snprintf(buf, sizeof(buf), "%u",
                                         g_settings.equipment.heaterPowerW);
                                drawValueTile(x1, y1, tileW, tileH,
                                              msg(Msg::HEATER_POWER), buf,
                                              msg(Msg::UNIT_W), COLOR_WARNING);

                                snprintf(buf, sizeof(buf), "%u",
                                         g_settings.equipment.columnHeightMm);
                                drawValueTile(x2, y1, tileW, tileH,
                                              msg(Msg::COLUMN_HEIGHT), buf,
                                              msg(Msg::UNIT_MM), COLOR_INFO);

                                snprintf(buf, sizeof(buf), "%.1f",
                                         g_settings.equipment.cubeVolumeL);
                                drawValueTile(x1, y2, tileW, tileH,
                                              msg(Msg::CUBE_VOLUME), buf,
                                              msg(Msg::UNIT_L), COLOR_SUCCESS);

                                snprintf(buf, sizeof(buf), "%.2f",
                                         g_settings.equipment.packingCoeff);
                                drawValueTile(x2, y2, tileW, tileH,
                                              msg(Msg::PACKING_COEFF), buf, "",
                                              colorAccent());
                                drawScreenAccessFooterHint(
                                    UI_EQUIPMENT, g_state,
                                    msg(Msg::TAP_TO_EDIT), colorAccent());
                              }

                              static void renderRectParams(bool full) {
                                if (full) {
                                  tft.fillScreen(colorBg());
                                }
                                const bool ru = (g_settings.language == 0);
                                if (full) {
                                  drawHeader(tftText(TftTextId::RectTitle), true);
                                  drawTabs(UI_SETTINGS);
                                }

                                const int16_t tileW = 225;
                                const int16_t tileH = 46;
                                const int16_t x1 = 10;
                                const int16_t x2 = 245;
                                const int16_t y1 = 82;
                                const int16_t y2 = 134;
                                const int16_t y3 = 186;
                                char tileBuf[32];
                                char pageBuf[80];

                                snprintf(
                                    pageBuf, sizeof(pageBuf), "%s",
                                    rectParamsPage == 0
                                        ? tftText(TftTextId::RectPageFeedCuts)
                                        : tftText(TftTextId::RectPageFlowTemp));
                                drawButton(10, 48, 460, 26, pageBuf,
                                           rectParamsPage == 0 ? COLOR_INFO
                                                               : colorAccent(),
                                           TFT_WHITE);

                                if (rectParamsPage == 0) {
                                  snprintf(
                                      tileBuf, sizeof(tileBuf), "%s",
                                      rectFeedstockName(
                                          g_settings.rectParams.feedstock, ru));
                                  drawValueTile(x1, y1, tileW, tileH,
                                                msg(Msg::FEEDSTOCK), tileBuf,
                                                "", COLOR_INFO);

                                  snprintf(tileBuf, sizeof(tileBuf), "%.1f",
                                           g_settings.rectParams.feedVolumeL);
                                  drawValueTile(x2, y1, tileW, tileH,
                                                msg(Msg::FEED_VOLUME), tileBuf,
                                                msg(Msg::UNIT_L),
                                                COLOR_PRIMARY);

                                  snprintf(
                                      tileBuf, sizeof(tileBuf), "%.1f",
                                      g_settings.rectParams.feedAbvPercent);
                                  drawValueTile(x1, y2, tileW, tileH,
                                                msg(Msg::FEED_ABV), tileBuf,
                                                "%", COLOR_WARNING);

                                  snprintf(tileBuf, sizeof(tileBuf), "%.1f",
                                           g_settings.rectParams.headsPercent);
                                  drawValueTile(x2, y2, tileW, tileH,
                                                msg(Msg::HEADS_PERCENT),
                                                tileBuf, "%", COLOR_DANGER);

                                  snprintf(tileBuf, sizeof(tileBuf), "%.1f",
                                           g_settings.rectParams.bodyPercent);
                                  drawValueTile(x1, y3, tileW, tileH,
                                                msg(Msg::BODY_PERCENT), tileBuf,
                                                "%", COLOR_SUCCESS);

                                  snprintf(tileBuf, sizeof(tileBuf), "%.1f",
                                           g_settings.rectParams.tailsPercent);
                                  drawValueTile(x2, y3, tileW, tileH,
                                                msg(Msg::TAILS_PERCENT),
                                                tileBuf, "%", COLOR_WARNING);

                                  drawScreenAccessFooterHint(
                                      UI_RECT_PARAMS, g_state,
                                      tftText(TftTextId::RectFeedHint),
                                      COLOR_INFO);
                                  return;
                                }

                                snprintf(tileBuf, sizeof(tileBuf), "%.0f",
                                         g_settings.rectParams.headsSpeedMlHKw);
                                drawValueTile(x1, y1, tileW, tileH,
                                              msg(Msg::HEADS_SPEED), tileBuf,
                                              msg(Msg::UNIT_ML_H_K),
                                              COLOR_DANGER);

                                snprintf(tileBuf, sizeof(tileBuf), "%.0f",
                                         g_settings.rectParams.bodySpeedMlHKw);
                                drawValueTile(x2, y1, tileW, tileH,
                                              msg(Msg::BODY_SPEED), tileBuf,
                                              msg(Msg::UNIT_ML_H_K),
                                              COLOR_SUCCESS);

                                snprintf(
                                    tileBuf, sizeof(tileBuf), "%u",
                                    g_settings.rectParams.stabilizationMin);
                                drawValueTile(x1, y2, tileW, tileH,
                                              msg(Msg::STABILIZATION), tileBuf,
                                              msg(Msg::UNIT_MIN), COLOR_INFO);

                                snprintf(tileBuf, sizeof(tileBuf), "%u",
                                         g_settings.rectParams.purgeMin);
                                drawValueTile(
                                    x2, y2, tileW, tileH, msg(Msg::PURGE_TIME),
                                    tileBuf, msg(Msg::UNIT_MIN), COLOR_WARNING);

                                const float atmHpaComp =
                                    (g_state.pressure.ok &&
                                     g_state.pressure.atmosphere > 850.0f &&
                                     g_state.pressure.atmosphere < 1100.0f)
                                        ? g_state.pressure.atmosphere
                                        : RECT_PRESSURE_STD_HPA;
                                const float bodyToTailsComp =
                                    RECT_CUBE_BODY_TO_TAILS_BASE_C +
                                    (atmHpaComp - RECT_PRESSURE_STD_HPA) *
                                        RECT_TEMP_COMP_C_PER_HPA;
                                const float finishComp =
                                    RECT_CUBE_FINISH_BASE_C +
                                    (atmHpaComp - RECT_PRESSURE_STD_HPA) *
                                        RECT_TEMP_COMP_C_PER_HPA;

                                snprintf(tileBuf, sizeof(tileBuf), "%s",
                                         g_settings.rectParams.baroCorrectionEnabled
                                             ? (ru ? "БАРО: ВКЛ" : "BARO: ON")
                                             : (ru ? "БАРО: ВЫКЛ" : "BARO: OFF"));
                                drawButton(x1, y3, tileW, tileH, tileBuf,
                                           g_settings.rectParams
                                                   .baroCorrectionEnabled
                                               ? COLOR_SUCCESS
                                               : dimmedButtonColor(),
                                           TFT_WHITE);

                                snprintf(tileBuf, sizeof(tileBuf), "%.1f",
                                         finishComp);
                                drawValueTile(x2, y3, tileW, tileH,
                                              msg(Msg::TAILS_FINISH), tileBuf,
                                              "C", colorMuted());

                                 snprintf(tileBuf, sizeof(tileBuf),
                                          ru ? "АТМ %.0f hPa | комп."
                                             : "ATM %.0f hPa | comp",
                                          atmHpaComp);
                                 drawScreenAccessFooterHint(
                                     UI_RECT_PARAMS, g_state, tileBuf,
                                     COLOR_INFO);
                               }

                               static void renderDistParams(bool full) {
                                 if (full) {
                                   tft.fillScreen(colorBg());
                                   drawHeader(tftText(TftTextId::DistTitle), true);
                                   drawTabs(UI_SETTINGS);
                                 }
                                 const bool ru = (g_settings.language == 0);
                                 drawButton(
                                     10, 48, 460, 26,
                                     distParamsPage == 0
                                         ? (ru ? "ОТБОР / ОБЪЁМЫ"
                                               : "TAKEOFF / VOLUMES")
                                         : (ru ? "ПРОГРАММА ФРАКЦИЙ"
                                               : "FRACTION PROGRAM"),
                                     distParamsPage == 0 ? COLOR_INFO
                                                         : colorAccent(),
                                     TFT_WHITE);
                                 if (distParamsPage == 1) {
                                   char target[32];
                                   snprintf(
                                       target, sizeof(target), "%.1f",
                                       g_settings.fractionProgram
                                           .heatingTargetTemperatureC);
                                   drawValueTile(
                                       10, 82, 225, 62,
                                       ru ? "НАГРЕВ ПРОГРАММЫ"
                                          : "PROGRAM HEATING",
                                       target, "C", COLOR_WARNING);
                                   char enabled[64];
                                   snprintf(enabled, sizeof(enabled), "%s",
                                            g_settings.fractionProgram.enabled
                                                ? (ru ? "ПРОГРАММА: ВКЛ"
                                                      : "PROGRAM: ON")
                                                : (ru ? "ПРОГРАММА: ВЫКЛ"
                                                      : "PROGRAM: OFF"));
                                   drawButton(
                                       245, 82, 225, 62, enabled,
                                       g_settings.fractionProgram.enabled
                                           ? COLOR_SUCCESS
                                           : dimmedButtonColor(),
                                       TFT_WHITE);
                                   char summary[96];
                                   snprintf(
                                       summary, sizeof(summary), "%s: %u",
                                       ru ? "ШАГОВ В ПРОФИЛЕ"
                                          : "PROFILE STEPS",
                                       static_cast<unsigned>(
                                           g_settings.fractionProgram.stepCount));
                                   drawCard(10, 150, TFT_WIDTH - 20, 48,
                                            colorCard());
                                   drawPanelHeader(10, 150, TFT_WIDTH - 20,
                                                   summary, COLOR_INFO);
                                   tft.setTextColor(colorMuted());
                                   tft.setTextSize(1);
                                   tft.setTextDatum(middle_left);
                                   drawDisplayString(
                                       ru ? "Состав шагов изменяется в сохранённом профиле"
                                          : "Step composition is edited in the saved profile",
                                       20, 184);
                                   tft.setTextDatum(top_left);
                                   drawScreenAccessFooterHint(
                                       UI_DIST_PARAMS, g_state,
                                       ru ? "Коснитесь уставки или переключателя"
                                          : "Tap the setting or toggle",
                                       COLOR_INFO);
                                   return;
                                 }
                                 const int16_t x1 = 10;
                                 const int16_t x2 = 245;
                                const int16_t y1 = 82;
                                const int16_t y2 = 138;
                                const int16_t tileW = 225;
                                const int16_t tileH = 50;
                                char tileBufDist[32];

                                snprintf(tileBufDist, sizeof(tileBufDist),
                                         "%.0f", distUi.speedMlH);
                                drawValueTile(x1, y1, tileW, tileH,
                                              msg(Msg::DIST_SPEED), tileBufDist,
                                              msg(Msg::UNIT_ML_H),
                                              COLOR_PRIMARY);

                                snprintf(tileBufDist, sizeof(tileBufDist),
                                         "%.0f", distUi.headsVolumeMl);
                                drawValueTile(x2, y1, tileW, tileH,
                                              msg(Msg::HEADS_VOLUME),
                                              tileBufDist, "ml", COLOR_DANGER);

                                snprintf(tileBufDist, sizeof(tileBufDist),
                                         "%.0f", distUi.targetVolumeMl);
                                drawValueTile(x1, y2, tileW, tileH,
                                              msg(Msg::TARGET_VOLUME),
                                              tileBufDist, "ml", COLOR_SUCCESS);

                                snprintf(tileBufDist, sizeof(tileBufDist),
                                         "%.1f", distUi.endTempC);
                                 drawValueTile(x2, y2, tileW, tileH,
                                               msg(Msg::END_TEMP), tileBufDist,
                                               "C", COLOR_WARNING);
                                snprintf(tileBufDist, sizeof(tileBufDist),
                                         "%.0f", distUi.tailsVolumeMl);
                                drawValueTile(10, 194, TFT_WIDTH - 20, 32,
                                              ru ? "ОБЪЁМ ХВОСТОВ" : "TAILS VOLUME",
                                              tileBufDist, "ml", colorMuted());
                                 drawScreenAccessFooterHint(
                                     UI_DIST_PARAMS, g_state,
                                     msg(Msg::TAP_TO_EDIT), colorAccent());
                                 return;
                               }

                               static void renderCalibration(bool full) {
                                 if (full) {
                                   tft.fillScreen(colorBg());
                                   drawHeader(tftText(TftTextId::CalibrationTitle),
                                              true);
                                   drawTabs(UI_SETTINGS);
                                 }
                                 const int16_t tileY = 52;
                                 const int16_t tileW = 225;
                                const int16_t tileH = 86;
                                char buf[32];
                                snprintf(buf, sizeof(buf), "%.3f",
                                         g_settings.pumpCal.mlPerRevolution);
                                drawValueTile(10, tileY, tileW, tileH,
                                              msg(Msg::PUMP_CALIBRATION), buf,
                                              msg(Msg::UNIT_ML_R), COLOR_INFO);

                                 drawButton(245, tileY, tileW, tileH,
                                            msg(Msg::TOUCH_CALIBRATION),
                                            COLOR_WARNING, TFT_WHITE);
                                 drawButton(10, 148, TFT_WIDTH - 20, 64,
                                            (g_settings.language == 0)
                                                ? "ДАТЧИКИ / КАЛИБРОВКА"
                                                : "SENSORS / CALIBRATION",
                                            COLOR_PRIMARY, TFT_WHITE);
                                 drawScreenAccessFooterHint(
                                     UI_CALIBRATION, g_state,
                                     tftText(TftTextId::CalibrationHint),
                                     COLOR_WARNING);
                               }

                               static void renderPumpCalibration(
                                   const SystemState &state, bool full) {
                                 const bool ru = g_settings.language == 0;
                                 const PumpCalibrationSession::Snapshot calibration =
                                     PumpCalibrationSession::getSnapshot();
                                 const int16_t cardX = 10;
                                 const int16_t cardW = TFT_WIDTH - 20;
                                 const int16_t actionY = 178;
                                 if (full) {
                                   tft.fillScreen(colorBg());
                                   drawHeader(ru ? "КАЛИБРОВКА НАСОСА"
                                                 : "PUMP CALIBRATION",
                                              true);
                                   drawTabs(UI_PUMP_CALIBRATION);
                                 }

                                 const char *step = ru ? "1. ПРОКАЧАЙТЕ ЛИНИЮ"
                                                       : "1. PRIME THE LINE";
                                 const char *detail =
                                     ru ? "Запуск не изменяет сохранённую калибровку."
                                        : "Starting does not change saved calibration.";
                                 const char *action = ru ? "НАЧАТЬ ПРОКАЧКУ"
                                                         : "START PRIMING";
                                 uint16_t tone = COLOR_INFO;
                                 switch (calibration.phase) {
                                 case PumpCalibrationSession::Phase::PRIMING:
                                   step = ru ? "1. ЛИНИЯ ЗАПОЛНЯЕТСЯ"
                                             : "1. LINE IS PRIMING";
                                   detail = ru ? "Остановите, когда жидкость выйдет без воздуха."
                                               : "Stop when liquid exits without air.";
                                   action = ru ? "ОСТАНОВИТЬ ПРОКАЧКУ"
                                               : "STOP PRIMING";
                                   tone = COLOR_WARNING;
                                   break;
                                 case PumpCalibrationSession::Phase::READY_TO_MEASURE:
                                   step = ru ? "2. ИЗМЕРИТЕЛЬНАЯ ПРОКАЧКА"
                                             : "2. MEASUREMENT RUN";
                                   detail = ru ? "Поставьте мерную ёмкость и начните прокачку."
                                               : "Place a measuring vessel, then start the run.";
                                   action = ru ? "НАЧАТЬ ИЗМЕРЕНИЕ" : "START MEASUREMENT";
                                   break;
                                 case PumpCalibrationSession::Phase::MEASURING:
                                   step = ru ? "2. ИЗМЕРЕНИЕ ИДЁТ"
                                             : "2. MEASURING";
                                   detail = ru ? "Остановите после достаточного объёма."
                                               : "Stop after a sufficient volume.";
                                   action = ru ? "ОСТАНОВИТЬ ИЗМЕРЕНИЕ"
                                               : "STOP MEASUREMENT";
                                   tone = COLOR_WARNING;
                                   break;
                                 case PumpCalibrationSession::Phase::READY_TO_REVIEW:
                                   step = ru ? "3. ВВЕДИТЕ ФАКТИЧЕСКИЙ ОБЪЁМ"
                                             : "3. ENTER ACTUAL VOLUME";
                                   detail = ru ? "Запишите объём из мерной ёмкости."
                                               : "Enter the volume from the measuring vessel.";
                                   action = ru ? "ВВЕСТИ ОБЪЁМ" : "ENTER VOLUME";
                                   tone = COLOR_INFO;
                                   break;
                                 case PumpCalibrationSession::Phase::REVIEW:
                                   step = ru ? "4. ПРОВЕРЬТЕ РЕЗУЛЬТАТ"
                                             : "4. REVIEW RESULT";
                                   detail = ru ? "Сохранение запишет значение в NVS; повтор не меняет его."
                                               : "Save writes NVS; retry leaves it unchanged.";
                                   action = ru ? "СОХРАНИТЬ" : "SAVE";
                                   tone = COLOR_SUCCESS;
                                   break;
                                 case PumpCalibrationSession::Phase::IDLE:
                                 default:
                                   break;
                                 }

                                 drawCard(cardX, UI_HEADER_H + 4, cardW, 112,
                                          colorCard());
                                 drawPanelHeader(cardX, UI_HEADER_H + 4, cardW,
                                                 step, tone);
                                 tft.setTextColor(colorFg());
                                 tft.setTextDatum(middle_left);
                                 char detailFitted[120];
                                 copyFittedText(detail, cardW - 24, detailFitted,
                                                sizeof(detailFitted));
                                 drawDisplayString(detailFitted, cardX + 10,
                                                   UI_HEADER_H + 48);
                                 char values[112];
                                 if (calibration.phase ==
                                     PumpCalibrationSession::Phase::REVIEW) {
                                   const float previous =
                                       g_settings.pumpCal.mlPerRevolution;
                                   const float error = previous > 0.0f
                                                           ? ((calibration.candidateMlPerRev -
                                                               previous) /
                                                              previous) *
                                                                 100.0f
                                                           : 0.0f;
                                   snprintf(values, sizeof(values),
                                            ru ? "Новый %.3f мл/об, ошибка %+0.1f%%"
                                               : "New %.3f ml/rev, delta %+0.1f%%",
                                            calibration.candidateMlPerRev, error);
                                 } else if (calibration.phase ==
                                                PumpCalibrationSession::Phase::MEASURING ||
                                            calibration.phase ==
                                                PumpCalibrationSession::Phase::READY_TO_REVIEW) {
                                   snprintf(values, sizeof(values),
                                            ru ? "Шагов: %lu" : "Steps: %lu",
                                            static_cast<unsigned long>(
                                                Pump::getTotalSteps()));
                                 } else {
                                   snprintf(values, sizeof(values),
                                            ru ? "Текущее: %.3f мл/об"
                                               : "Current: %.3f ml/rev",
                                            g_settings.pumpCal.mlPerRevolution);
                                 }
                                 char valuesFitted[120];
                                 copyFittedText(values, cardW - 24, valuesFitted,
                                                sizeof(valuesFitted));
                                 drawDisplayString(valuesFitted, cardX + 10,
                                                   UI_HEADER_H + 82);
                                 tft.setTextDatum(top_left);

                                 if (calibration.phase ==
                                     PumpCalibrationSession::Phase::REVIEW) {
                                   const int16_t buttonW = (TFT_WIDTH - 23) / 2;
                                   drawButton(10, actionY, buttonW, 38, action,
                                              tone, TFT_WHITE);
                                   drawButton(13 + buttonW, actionY, buttonW, 38,
                                              ru ? "ПОВТОРИТЬ" : "RETRY",
                                              COLOR_WARNING, TFT_WHITE);
                                 } else {
                                   drawButton(10, actionY, TFT_WIDTH - 20, 38,
                                              action, tone, TFT_WHITE);
                                 }
                                 const char *hint =
                                     ui.pumpCalibrationMessage[0] != '\0'
                                         ? ui.pumpCalibrationMessage
                                         : (ru ? "Выход или авария отменяет сессию без записи NVS."
                                               : "Exit or alarm cancels the session without writing NVS.");
                                 drawFooterHint(hint, tone);
                               }

                              static void renderSensorCalibration(
                                  const SystemState &state, bool full) {
                                const bool ru = g_settings.language == 0;
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(ru ? "ДАТЧИКИ / КАЛИБРОВКА"
                                                : "SENSORS / CALIBRATION",
                                             true);
                                  drawTabs(UI_SENSOR_CALIBRATION);
                                } else {
                                  tft.fillRect(0, UI_HEADER_H, TFT_WIDTH,
                                               TFT_HEIGHT - UI_HEADER_H,
                                               colorBg());
                                }

                                static constexpr int16_t kTabY = 52;
                                static constexpr int16_t kTabH = 28;
                                static constexpr int16_t kTabW = 112;
                                static constexpr int16_t kTabGap = 4;
                                const char *tabLabels[4] = {
                                    ru ? "DS18" : "DS18", ru ? "ДАВЛ" : "PRESS",
                                    ru ? "АРЕО" : "HYDRO", ru ? "УРОВЕНЬ" : "LEVEL"};
                                for (uint8_t tab = 0; tab < 4; ++tab) {
                                  drawButton(10 + tab * (kTabW + kTabGap),
                                             kTabY, kTabW, kTabH, tabLabels[tab],
                                             ui.sensorCalibrationPage == tab
                                                 ? COLOR_PRIMARY
                                                 : colorMuted(),
                                             TFT_WHITE);
                                }

                                const char *defaultHint =
                                    ru ? "Сложные таблицы и экспорт: Web UI → Калибровка."
                                       : "Complex tables and export: Web UI → Calibration.";
                                uint16_t hintTone = COLOR_INFO;

                                switch (ui.sensorCalibrationPage) {
                                case 0: {
                                  const uint8_t role =
                                      ui.sensorCalibrationTempRole < TEMP_COUNT
                                          ? ui.sensorCalibrationTempRole
                                          : TEMP_CUBE;
                                  char title[96];
                                  char reading[128];
                                  char mapping[128];
                                  char assigned[32] = {0};
                                  char scanned[32] = {0};
                                  formatTempAddress(g_settings.tempCal.addresses[role],
                                                    assigned, sizeof(assigned));
                                  if (ui.sensorCalibrationScanCount > 0 &&
                                      ui.sensorCalibrationScanIndex <
                                          ui.sensorCalibrationScanCount) {
                                    formatTempAddress(
                                        ui.sensorCalibrationAddresses
                                            [ui.sensorCalibrationScanIndex],
                                        scanned, sizeof(scanned));
                                  }
                                  snprintf(title, sizeof(title), "DS18B20: %s",
                                           getTempSensorLabel(role));
                                  snprintf(
                                      reading, sizeof(reading),
                                      ru ? "%s  %.2f C | offset %+0.1f C"
                                         : "%s  %.2f C | offset %+0.1f C",
                                      state.temps.valid[role] ? "OK" : "--",
                                      getSensorTemperatureValue(role, state),
                                      g_settings.tempCal.offsets[role]);
                                  snprintf(
                                      mapping, sizeof(mapping),
                                      ru ? "Карта: %s | найден: %s (%u/%u)"
                                         : "Map: %s | found: %s (%u/%u)",
                                      assigned[0] != '\0' ? assigned : "AUTO",
                                      scanned[0] != '\0' ? scanned : "--",
                                      ui.sensorCalibrationScanCount == 0
                                          ? 0
                                          : ui.sensorCalibrationScanIndex + 1,
                                      ui.sensorCalibrationScanCount);
                                  drawCard(10, 86, TFT_WIDTH - 20, 72, colorCard());
                                  drawPanelHeader(10, 86, TFT_WIDTH - 20, title,
                                                  state.temps.valid[role]
                                                      ? COLOR_SUCCESS
                                                      : COLOR_WARNING);
                                  tft.setTextColor(colorFg());
                                  tft.setTextDatum(top_left);
                                  drawDisplayString(reading, 20, 120);
                                  char mappingFitted[128];
                                  copyFittedText(mapping, TFT_WIDTH - 40,
                                                 mappingFitted,
                                                 sizeof(mappingFitted));
                                  drawDisplayString(mappingFitted, 20, 141);
                                  drawButton(10, 166, 100, 38,
                                             ru ? "РОЛЬ <" : "ROLE <",
                                             colorMuted(), TFT_WHITE);
                                  drawButton(115, 166, 100, 38,
                                             ru ? "РОЛЬ >" : "ROLE >",
                                             colorMuted(), TFT_WHITE);
                                  drawButton(220, 166, 250, 38,
                                             ru ? "ПОПРАВКА" : "OFFSET",
                                             COLOR_INFO, TFT_WHITE);
                                  drawButton(10, 210, 90, 38,
                                             ru ? "АДР <" : "ADDR <",
                                             colorMuted(), TFT_WHITE);
                                  drawButton(105, 210, 90, 38,
                                             ru ? "АДР >" : "ADDR >",
                                             colorMuted(), TFT_WHITE);
                                  drawButton(200, 210, 170, 38,
                                             ru ? "НАЗНАЧИТЬ" : "ASSIGN",
                                             COLOR_SUCCESS, TFT_WHITE);
                                  drawButton(375, 210, 95, 38,
                                             ru ? "АВТО" : "AUTO", COLOR_WARNING,
                                             TFT_WHITE);
                                  defaultHint =
                                      ru ? "Назначение берёт адрес из сканирования; АВТО возвращает поиск по порядку."
                                         : "Assign uses scanned address; AUTO restores order-based discovery.";
                                  break;
                                }
                                case 1: {
                                  char signal[128];
                                  char saved[128];
                                  snprintf(signal, sizeof(signal),
                                           ru ? "%s  %.2f mmHg | %.3f V"
                                              : "%s  %.2f mmHg | %.3f V",
                                           state.pressure.ok ? "OK" : "--",
                                           state.pressure.cube,
                                           state.pressure.sensorVoltage);
                                  snprintf(saved, sizeof(saved),
                                           ru ? "Сохранённый ноль: %+0.1f mmHg | точек: %u"
                                              : "Saved zero: %+0.1f mmHg | points: %u",
                                           g_settings.pressureCal.zeroOffsetMmHg,
                                           g_settings.pressureCal.pointCount);
                                  drawCard(10, 86, TFT_WIDTH - 20, 100, colorCard());
                                  drawPanelHeader(10, 86, TFT_WIDTH - 20,
                                                  ru ? "ДАВЛЕНИЕ · ADS1115 A1"
                                                     : "PRESSURE · ADS1115 A1",
                                                  state.pressure.ok ? COLOR_SUCCESS
                                                                    : COLOR_WARNING);
                                  tft.setTextColor(colorFg());
                                  tft.setTextDatum(top_left);
                                  drawDisplayString(signal, 20, 124);
                                  drawDisplayString(saved, 20, 150);
                                  drawButton(10, 194, 225, 38,
                                             ru ? "ТЕКУЩЕЕ = 0" : "SET CURRENT = 0",
                                             COLOR_SUCCESS, TFT_WHITE);
                                  drawButton(245, 194, 225, 38,
                                             ru ? "ИЗМЕНИТЬ НОЛЬ" : "EDIT ZERO",
                                             COLOR_INFO, TFT_WHITE);
                                  defaultHint =
                                      ru ? "Устанавливайте ноль только при безопасном атмосферном давлении."
                                         : "Set zero only at safe atmospheric pressure.";
                                  hintTone = COLOR_WARNING;
                                  break;
                                }
                                case 2: {
                                  char signal[128];
                                  char saved[128];
                                  snprintf(signal, sizeof(signal),
                                           ru ? "%s  %.3f kg/L | %.1f %% ABV"
                                              : "%s  %.3f kg/L | %.1f %% ABV",
                                           state.hydrometer.valid ? "OK" : "--",
                                           state.hydrometer.density,
                                           state.hydrometer.abv);
                                  snprintf(saved, sizeof(saved),
                                           ru ? "Сохранённый offset: %+0.3f | точек: %u"
                                              : "Saved offset: %+0.3f | points: %u",
                                           g_settings.hydroCal.densityOffset,
                                           g_settings.hydroCal.pointCount);
                                  drawCard(10, 86, TFT_WIDTH - 20, 100, colorCard());
                                  drawPanelHeader(10, 86, TFT_WIDTH - 20,
                                                  ru ? "АРЕОМЕТР · ADS1115 A0"
                                                     : "HYDROMETER · ADS1115 A0",
                                                  state.hydrometer.valid
                                                      ? COLOR_SUCCESS
                                                      : COLOR_WARNING);
                                  tft.setTextColor(colorFg());
                                  tft.setTextDatum(top_left);
                                  drawDisplayString(signal, 20, 124);
                                  drawDisplayString(saved, 20, 150);
                                  drawButton(10, 194, 225, 38,
                                             ru ? "ПО ЭТАЛОНУ" : "BY REFERENCE",
                                             COLOR_SUCCESS, TFT_WHITE);
                                  drawButton(245, 194, 225, 38,
                                             ru ? "ИЗМЕНИТЬ OFFSET" : "EDIT OFFSET",
                                             COLOR_INFO, TFT_WHITE);
                                  defaultHint =
                                      ru ? "Эталон рассчитывает offset по текущему валидному сигналу."
                                         : "Reference calculates offset from the current valid signal.";
                                  break;
                                }
                                case 3: {
                                  int16_t bodyAdc = 0;
                                  float bodyVoltage = 0.0f;
                                  const bool bodyValid = Sensors::readAds1115Channel(
                                      ADS_CHANNEL_LEVEL_BODY, bodyAdc, bodyVoltage);
                                  const bool bodyTriggered =
                                      g_settings.equipment.bodyLevelSensorEnabled &&
                                      bodyValid &&
                                      (g_settings.equipment.bodyLevelTriggerAbove
                                           ? bodyVoltage >= g_settings.equipment.bodyLevelThresholdV
                                           : bodyVoltage <= g_settings.equipment.bodyLevelThresholdV);
                                  const char *bodyStatus =
                                      !g_settings.equipment.bodyLevelSensorEnabled
                                          ? "OFF"
                                          : (bodyValid
                                                 ? (bodyTriggered ? "ALARM" : "OK")
                                                 : "--");
                                  const char *leakStatus =
                                      !g_settings.equipment.leakSensorEnabled
                                          ? "OFF"
                                          : (g_state.leak.valid
                                                 ? (g_state.leak.triggered ? "ALARM" : "OK")
                                                 : "--");
                                  char bodyLine[120];
                                  char leakLine[120];
                                  snprintf(
                                      bodyLine, sizeof(bodyLine),
                                      ru ? "%s %.2f V / %.2f V"
                                         : "%s %.2f V / %.2f V",
                                      bodyStatus,
                                      bodyVoltage,
                                      g_settings.equipment.bodyLevelThresholdV);
                                  snprintf(
                                      leakLine, sizeof(leakLine),
                                      ru ? "%s %.2f V / %.2f V"
                                         : "%s %.2f V / %.2f V",
                                      leakStatus,
                                      g_state.leak.voltage,
                                      g_settings.equipment.leakThresholdV);
                                  drawCard(10, 86, TFT_WIDTH - 20, 56, colorCard());
                                  drawPanelHeader(
                                      10, 86, TFT_WIDTH - 20,
                                      ru ? "УРОВЕНЬ ТЕЛА · ADS1115 A2"
                                         : "BODY LEVEL · ADS1115 A2",
                                      bodyTriggered ? COLOR_DANGER : COLOR_INFO);
                                  drawCard(10, 144, TFT_WIDTH - 20, 56, colorCard());
                                  drawPanelHeader(10, 144, TFT_WIDTH - 20,
                                                  ru ? "ПРОТЕЧКА · ADS1115 A3"
                                                     : "LEAK · ADS1115 A3",
                                                  g_state.leak.triggered
                                                      ? COLOR_DANGER
                                                      : COLOR_INFO);
                                  tft.setTextColor(colorFg());
                                  tft.setTextDatum(top_left);
                                  drawDisplayString(bodyLine, 20, 119);
                                  drawDisplayString(leakLine, 20, 177);
                                  drawButton(355, 90, 115, 48,
                                             g_settings.equipment.bodyLevelTriggerAbove
                                                 ? ">= ПОРОГ"
                                                 : "<= ПОРОГ",
                                             colorMuted(), TFT_WHITE);
                                  drawButton(355, 148, 115, 48,
                                             g_settings.equipment.leakTriggerAbove
                                                 ? ">= ПОРОГ"
                                                 : "<= ПОРОГ",
                                             colorMuted(), TFT_WHITE);
                                  drawButton(10, 210, 225, 38,
                                             ru ? "ПОРОГ УРОВНЯ" : "LEVEL THRESHOLD",
                                             COLOR_INFO, TFT_WHITE);
                                  drawButton(245, 210, 225, 38,
                                             ru ? "ПОРОГ ПРОТЕЧКИ" : "LEAK THRESHOLD",
                                             COLOR_INFO, TFT_WHITE);
                                  defaultHint =
                                      ru ? "Коснитесь >=/<=, чтобы сменить полярность; порог меняется отдельно."
                                         : "Tap >=/<= to change polarity; edit threshold separately.";
                                  hintTone = COLOR_WARNING;
                                  break;
                                }
                                default:
                                  ui.sensorCalibrationPage = 0;
                                  break;
                                }

                                drawScreenAccessFooterHint(
                                    UI_SENSOR_CALIBRATION, state,
                                    ui.sensorCalibrationMessage[0] != '\0'
                                        ? ui.sensorCalibrationMessage
                                        : defaultHint,
                                    hintTone);
                              }

                              static void renderManual(
                                  const SystemState &state) {
                                tft.fillScreen(colorBg());
                                const bool ru = (g_settings.language == 0);
                                drawHeader(tftText(TftTextId::ManualTitle),
                                           true);
                                drawTabs(UI_CONTROL);

                                if (!isScreenEditable(UI_MANUAL, state)) {
                                  char message[160];
                                  char footer[160];
                                  snprintf(
                                      message, sizeof(message), "%s",
                                      tftText(TftTextId::ManualLockMessage));
                                  snprintf(
                                      footer, sizeof(footer),
                                      ru ? "Текущий режим: %s\nДоступно только "
                                           "в ожидании и ручном режиме"
                                         : "Current mode: %s\nAvailable only "
                                           "in idle and manual mode",
                                      getDisplayModeName(state.mode));
                                  drawFullscreenOverlay(
                                      tftText(TftTextId::ManualLockTitle),
                                      message, COLOR_WARNING, footer, 1);
                                  return;
                                }

                                char buf[32];
                                const int16_t tileY = 48;
                                const int16_t tileW = 225;
                                const int16_t tileH = 86;
                                snprintf(buf, sizeof(buf), "%u",
                                         Heater::getPower());
                                drawValueTile(10, tileY, tileW, tileH,
                                              msg(Msg::HEATER_POWER), buf, "%",
                                              Heater::getPower() > 0
                                                  ? COLOR_WARNING
                                                  : colorMuted());

                                snprintf(buf, sizeof(buf), "%.0f",
                                         state.pump.speedMlPerHour);
                                drawValueTile(245, tileY, tileW, tileH,
                                              msg(Msg::PUMP), buf,
                                              msg(Msg::UNIT_ML_H),
                                              state.pump.speedMlPerHour > 0.0f
                                                  ? COLOR_SUCCESS
                                                  : colorMuted());

                                drawButton(MANUAL_VALVE_X1, MANUAL_VALVE_Y,
                                           MANUAL_VALVE_W, MANUAL_VALVE_H,
                                           msg(Msg::VALVE_WATER),
                                           Valves::getWater() ? COLOR_SUCCESS
                                                              : COLOR_DARK_GREY,
                                           TFT_WHITE);
                                drawButton(MANUAL_VALVE_X2, MANUAL_VALVE_Y,
                                           MANUAL_VALVE_W, MANUAL_VALVE_H,
                                           msg(Msg::VALVE_HEADS),
                                           Valves::getHeads() ? COLOR_SUCCESS
                                                              : COLOR_DARK_GREY,
                                           TFT_WHITE);
                                drawButton(MANUAL_VALVE_X3, MANUAL_VALVE_Y,
                                           MANUAL_VALVE_W, MANUAL_VALVE_H,
                                           msg(Msg::VALVE_UNO),
                                           Valves::getUno() ? COLOR_SUCCESS
                                                            : COLOR_DARK_GREY,
                                           TFT_WHITE);

                                drawFooterHint(tftText(TftTextId::ManualHint),
                                               colorAccent());
                              }

                              static void renderValueEdit() {
                                if (edit.descriptor == nullptr) {
                                  return;
                                }
                                const EditableFields::Descriptor &descriptor =
                                    *edit.descriptor;
                                tft.fillScreen(colorBg());
                                drawHeader(EditableFields::label(
                                               descriptor.id,
                                               g_settings.language == 0),
                                           true);
                                char buf[32];
                                if (descriptor.decimals == 0)
                                  snprintf(buf, sizeof(buf), "%.0f",
                                           edit.value);
                                else if (descriptor.decimals == 1)
                                  snprintf(buf, sizeof(buf), "%.1f",
                                           edit.value);
                                else
                                  snprintf(buf, sizeof(buf), "%.3f",
                                           edit.value);
                                drawValueTile(10, 48, TFT_WIDTH - 20, 78,
                                              tftText(TftTextId::ValueTitle),
                                              buf, descriptor.unit,
                                              COLOR_PRIMARY);

                                drawButton(VALUE_EDIT_BTN_X1, VALUE_EDIT_BTN_Y,
                                           VALUE_EDIT_BTN_W, VALUE_EDIT_BTN_H,
                                           "--", tft.color565(200, 50, 50),
                                           TFT_WHITE);
                                drawButton(VALUE_EDIT_BTN_X2, VALUE_EDIT_BTN_Y,
                                           VALUE_EDIT_BTN_W, VALUE_EDIT_BTN_H,
                                           "-", tft.color565(220, 100, 100),
                                           TFT_WHITE);
                                drawButton(VALUE_EDIT_BTN_X3, VALUE_EDIT_BTN_Y,
                                           VALUE_EDIT_BTN_W, VALUE_EDIT_BTN_H,
                                           "+", tft.color565(100, 200, 100),
                                           TFT_WHITE);
                                drawButton(VALUE_EDIT_BTN_X4, VALUE_EDIT_BTN_Y,
                                           VALUE_EDIT_BTN_W, VALUE_EDIT_BTN_H,
                                           "++", tft.color565(50, 180, 50),
                                           TFT_WHITE);

                                drawButton(10, 202, TFT_WIDTH - 20, 44,
                                           msg(Msg::SAVE_AND_CLOSE),
                                           COLOR_PRIMARY, TFT_WHITE);
                                drawFooterHint(tftText(TftTextId::ValueHint),
                                               colorAccent());
                                tft.setTextDatum(top_left);
                              }

                              static void renderService(
                                  const SystemState &state, bool full) {
                                const int16_t tileW = 225;
                                const int16_t tileH = 64;
                                const int16_t x1 = 10;
                                const int16_t x2 = 245;
                                const int16_t ipRowY = 63;
                                const int16_t y1 = 92;
                                const int16_t y2 = 162;
                                const int16_t diagY = 242;

                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(msg(Msg::SERVICE), false);
                                  drawTabs(UI_SERVICE);
                                  drawValueTileShell(x1, y1, tileW, tileH,
                                                     msg(Msg::VERSION));
                                  drawValueTileShell(x2, y1, tileW, tileH,
                                                     msg(Msg::UPTIME));
                                  drawValueTileShell(x1, y2, tileW, tileH,
                                                     msg(Msg::FREE_HEAP));
                                  drawButton(
                                      x2, y2, tileW, tileH,
                                      g_settings.language == 0
                                          ? "ПРОВЕРКА ОБОРУДОВАНИЯ"
                                          : "EQUIPMENT TEST",
                                      COLOR_WARNING, TFT_WHITE);
                                }

                                char buf[64];
                                char uptimeBuf[16];
                                char ipBuf[32];

                                getServiceIpSummary(ipBuf, sizeof(ipBuf));
                                drawValueRow(
                                    ipRowY,
                                    g_settings.language == 0 ? "IP Wi-Fi"
                                                             : "Wi-Fi IP",
                                    ipBuf, false);

                                snprintf(buf, sizeof(buf), "%s", FW_VERSION);
                                drawValueTileValue(x1, y1, tileW, tileH, buf,
                                                   "", COLOR_PRIMARY);

                                formatUptimeCompact(state.uptime, uptimeBuf,
                                                    sizeof(uptimeBuf));
                                drawValueTileValue(x2, y1, tileW, tileH,
                                                   uptimeBuf, "",
                                                   colorAccent());

                                snprintf(buf, sizeof(buf), "%u",
                                         ESP.getFreeHeap() / 1024);
                                drawValueTileValue(x1, y2, tileW, tileH, buf,
                                                   "KB", COLOR_SUCCESS);

                                snprintf(
                                    buf, sizeof(buf), "F%u/%u S%lu R%lu H%lu G%u",
                                    g_displayStats.lastFrameMs,
                                    g_displayStats.maxFrameMs,
                                    (unsigned long)g_displayStats.slowFrames,
                                    (unsigned long)
                                        g_displayStats.watchdogRecoveries,
                                    (unsigned long)
                                        g_displayStats.hardWatchdogRecoveries,
                                    (unsigned int)
                                        g_displayStats.lastUpdateGapMs);
                                drawValueRow(diagY,
                                             tftText(TftTextId::ServiceDiag),
                                             buf);
                                uint16_t hintTone = COLOR_INFO;
                                const char *hintText =
                                    tftText(TftTextId::ServiceHintTemps);
                                if (g_displayStats.hardWatchdogRecoveries > 0 ||
                                    g_displayStats.hardWatchdogFailures > 0) {
                                  hintTone = COLOR_DANGER;
                                  hintText =
                                      tftText(TftTextId::ServiceHintHard);
                                } else if (g_displayStats.watchdogRecoveries >
                                               0 ||
                                           g_displayStats.maxFrameMs >=
                                               DISPLAY_SLOW_FRAME_MS) {
                                  hintTone = COLOR_WARNING;
                                  hintText =
                                      tftText(TftTextId::ServiceHintSlow);
                                }
                                drawFooterHint(hintText, hintTone);
                              }

                              static void renderServiceTest(
                                  const SystemState &state, bool full) {
                                const bool ru = (g_settings.language == 0);
                                const int16_t pageY = UI_HEADER_H + 4;
                                const int16_t firstRowY = pageY + 38;
                                const int16_t rowH = 42;
                                const int16_t rowGap = 8;
                                const int16_t x1 = 10;
                                const int16_t x2 = 245;
                                const int16_t w = 225;
                                const auto availability =
                                    getServiceTestAvailability(state);
                                char guardReason[144] = "";
                                const bool allowed = serviceTestAllowed(
                                    state, guardReason, sizeof(guardReason));
                                const ServiceTestSession::Snapshot session =
                                    ServiceTestSession::getSnapshot();

                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(ru ? "ПРОВЕРКА ОБОРУДОВАНИЯ"
                                                : "EQUIPMENT TEST",
                                             true);
                                  drawTabs(UI_SERVICE_TEST);
                                }

                                uint16_t statusTone =
                                    allowed ? COLOR_SUCCESS : COLOR_DANGER;
                                const char *status =
                                    allowed ? (ru ? "ГОТОВО" : "READY")
                                            : (ru ? "БЛОКИРОВАНО" : "BLOCKED");
                                switch (session.phase) {
                                case ServiceTestSession::Phase::REQUESTED:
                                  status = ru ? "ПОДТВЕРДИТЕ ПАУЗУ" : "CONFIRM PAUSE";
                                  statusTone = COLOR_WARNING;
                                  break;
                                case ServiceTestSession::Phase::SAFE_PAUSED:
                                  status = ru ? "СЕРВИСНАЯ ПАУЗА" : "SERVICE PAUSE";
                                  statusTone = COLOR_WARNING;
                                  break;
                                case ServiceTestSession::Phase::TEST_ACTIVE:
                                  status = ru ? "ТЕСТ АКТИВЕН" : "TEST ACTIVE";
                                  statusTone = COLOR_INFO;
                                  break;
                                case ServiceTestSession::Phase::STOPPING:
                                  status = ru ? "ОЖИДАНИЕ STOP" : "WAITING STOP";
                                  statusTone = COLOR_WARNING;
                                  break;
                                case ServiceTestSession::Phase::RESTORE_READY:
                                  status = ru ? "ВОЗВРАТ ГОТОВ" : "RESTORE READY";
                                  statusTone = COLOR_SUCCESS;
                                  break;
                                case ServiceTestSession::Phase::FAULTED:
                                  status = ru ? "SAFETY БЛОК" : "SAFETY BLOCK";
                                  statusTone = COLOR_DANGER;
                                  break;
                                case ServiceTestSession::Phase::CLOSED:
                                default:
                                  break;
                                }
                                drawButton(x1, pageY, 96, 28,
                                           ru ? "< НАЗАД" : "< PREV",
                                           ui.serviceTestPage > 0 ? COLOR_INFO
                                                                  : COLOR_DARK_GREY,
                                           TFT_WHITE);
                                drawButton(TFT_WIDTH - 106, pageY, 96, 28,
                                           ru ? "ДАЛЕЕ >" : "NEXT >",
                                           ui.serviceTestPage < 2 ? COLOR_INFO
                                                                  : COLOR_DARK_GREY,
                                           TFT_WHITE);
                                drawCard(116, pageY, TFT_WIDTH - 232, 28,
                                         colorCard());
                                tft.setTextColor(statusTone);
                                tft.setTextSize(1);
                                tft.setTextDatum(middle_center);
                                char pageText[64];
                                snprintf(pageText, sizeof(pageText), "%s %u/3", status,
                                         static_cast<unsigned>(ui.serviceTestPage + 1));
                                drawDisplayString(pageText, TFT_WIDTH / 2,
                                                  pageY + 15);
                                tft.setTextDatum(top_left);

                                if (ui.serviceTestPage == 0) {
                                  if (TftServiceTestLogic::isAvailable(
                                          TftServiceTestLogic::Device::Pump,
                                          availability)) {
                                    drawButton(
                                        x1, firstRowY, w, rowH,
                                        Pump::isRunning()
                                            ? (ru ? "НАСОС: СТОП" : "PUMP: STOP")
                                            : (ru ? "НАСОС: ТЕСТ"
                                                  : "PUMP: TEST"),
                                        Pump::isRunning() ? COLOR_WARNING
                                                          : COLOR_INFO,
                                        TFT_WHITE);
                                  }
                                  if (TftServiceTestLogic::isAvailable(
                                          TftServiceTestLogic::Device::Stirrer,
                                          availability)) {
                                    drawButton(
                                        x2, firstRowY, w, rowH,
                                        Stirrer::isRunning()
                                            ? (ru ? "МЕШАЛКА: СТОП"
                                                  : "STIRRER: STOP")
                                            : (ru ? "МЕШАЛКА" : "STIRRER"),
                                        Stirrer::isRunning() ? COLOR_WARNING
                                                             : COLOR_INFO,
                                        TFT_WHITE);
                                  }
                                  if (TftServiceTestLogic::isAvailable(
                                          TftServiceTestLogic::Device::Heater,
                                          availability)) {
                                    const bool heaterActive =
                                        Heater::getDiagnostics().active;
                                    drawButton(
                                        x1, firstRowY + rowH + rowGap, w, rowH,
                                        heaterActive
                                            ? (ru ? "ТЭН: СТОП" : "HEATER: STOP")
                                            : (ru ? "ТЭН: ПОДТВЕРДИТЬ"
                                                  : "HEATER: CONFIRM"),
                                        heaterActive ? COLOR_DANGER
                                                     : COLOR_WARNING,
                                        TFT_WHITE);
                                  }
                                  const char *sessionAction =
                                      ru ? "СТОП ВСЕ ВЫХОДЫ" : "STOP ALL OUTPUTS";
                                  uint16_t sessionActionTone = COLOR_DANGER;
                                  if (session.phase ==
                                      ServiceTestSession::Phase::RESTORE_READY) {
                                    sessionAction = session.resumeProcess
                                                        ? (ru ? "ВЕРНУТЬ ПРОЦЕСС"
                                                              : "RESTORE PROCESS")
                                                        : (ru ? "ЗАВЕРШИТЬ СЕРВИС"
                                                              : "CLOSE SERVICE");
                                    sessionActionTone = COLOR_SUCCESS;
                                  } else if (session.phase ==
                                             ServiceTestSession::Phase::FAULTED) {
                                    sessionAction = ru ? "ЗАКРЫТЬ ПОСЛЕ SAFETY"
                                                       : "CLOSE AFTER SAFETY";
                                  }
                                  drawButton(x2, firstRowY + rowH + rowGap, w,
                                             rowH, sessionAction,
                                             sessionActionTone, TFT_WHITE);
                                  if (TftServiceTestLogic::isAvailable(
                                          TftServiceTestLogic::Device::Sensors,
                                          availability)) {
                                    char sensors[96];
                                    snprintf(
                                        sensors, sizeof(sensors),
                                        ru ? "ДАТЧИКИ: T %u/%u  B:%c A:%c P:%c"
                                           : "SENSORS: T %u/%u  B:%c A:%c P:%c",
                                        static_cast<unsigned>(state.health.tempSensorsOk),
                                        static_cast<unsigned>(
                                            state.health.tempSensorsTotal),
                                        state.health.bmp280Ok ? '+' : '-',
                                        state.health.ads1115Ok ? '+' : '-',
                                        state.health.pzemOk ? '+' : '-');
                                    drawButton(
                                        10, firstRowY + (rowH + rowGap) * 2,
                                        TFT_WIDTH - 20, rowH, sensors,
                                        COLOR_INFO, TFT_WHITE);
                                  }
                                } else if (ui.serviceTestPage == 1) {
                                  if (TftServiceTestLogic::isAvailable(
                                          TftServiceTestLogic::Device::WaterValve,
                                          availability)) {
                                    drawButton(x1, firstRowY, w, rowH,
                                               ru ? "ВОДА: ИМПУЛЬС"
                                                  : "WATER: PULSE",
                                               COLOR_INFO, TFT_WHITE);
                                  }
                                  if (TftServiceTestLogic::isAvailable(
                                          TftServiceTestLogic::Device::HeadsValve,
                                          availability)) {
                                    drawButton(x2, firstRowY, w, rowH,
                                               ru ? "ГОЛОВЫ: ИМПУЛЬС"
                                                  : "HEADS: PULSE",
                                               COLOR_INFO, TFT_WHITE);
                                  }
                                  if (TftServiceTestLogic::isAvailable(
                                          TftServiceTestLogic::Device::BodyValve,
                                          availability)) {
                                    drawButton(x1, firstRowY + rowH + rowGap,
                                               w, rowH,
                                               ru ? "ТЕЛО: ИМПУЛЬС"
                                                  : "BODY: PULSE",
                                               COLOR_INFO, TFT_WHITE);
                                  }
                                  if (TftServiceTestLogic::isAvailable(
                                          TftServiceTestLogic::Device::TailsValve,
                                          availability)) {
                                    drawButton(x2, firstRowY + rowH + rowGap,
                                               w, rowH,
                                               ru ? "ХВОСТЫ: ИМПУЛЬС"
                                                  : "TAILS: PULSE",
                                               COLOR_INFO, TFT_WHITE);
                                  }
                                  if (TftServiceTestLogic::isAvailable(
                                          TftServiceTestLogic::Device::UnoValve,
                                          availability)) {
                                    drawButton(x1,
                                               firstRowY + (rowH + rowGap) * 2,
                                               w, rowH,
                                               ru ? "УНО: ИМПУЛЬС"
                                                  : "UNO: PULSE",
                                               COLOR_INFO, TFT_WHITE);
                                  }
                                  if (TftServiceTestLogic::isAvailable(
                                          TftServiceTestLogic::Device::CoolingPwm,
                                          availability)) {
                                    drawButton(x2,
                                               firstRowY + (rowH + rowGap) * 2,
                                               w, rowH,
                                               Valves::getStartStop() > 0
                                                   ? (ru ? "PWM: СТОП"
                                                         : "PWM: STOP")
                                                   : (ru ? "PWM ОХЛАЖДЕНИЯ"
                                                         : "COOLING PWM"),
                                               Valves::getStartStop() > 0
                                                   ? COLOR_WARNING
                                                   : COLOR_INFO,
                                               TFT_WHITE);
                                  }
                                } else if (TftServiceTestLogic::isAvailable(
                                               TftServiceTestLogic::Device::Fractionator,
                                               availability)) {
                                  drawButton(10, firstRowY, TFT_WIDTH - 20,
                                             rowH,
                                             ru ? "ФРАКЦИОННИК: СЛЕДУЮЩАЯ ПОЗИЦИЯ"
                                                : "FRACTIONATOR: NEXT POSITION",
                                             COLOR_INFO, TFT_WHITE);
                                } else {
                                  drawCard(10, firstRowY, TFT_WIDTH - 20, rowH,
                                           colorCard());
                                  tft.setTextColor(colorMuted());
                                  tft.setTextDatum(middle_center);
                                  drawDisplayString(
                                      ru ? "Фракционник отсутствует в конфигурации"
                                         : "Fractionator is absent from configuration",
                                      TFT_WIDTH / 2, firstRowY + rowH / 2);
                                  tft.setTextDatum(top_left);
                                }

                                const char *hint = ui.serviceTestMessage[0] != '\0'
                                                       ? ui.serviceTestMessage
                                                       : (allowed
                                                              ? (ru ? "Ограничения теста задаёт профиль платы; выходы отключаются."
                                                                    : "Board profile limits tests; outputs are stopped safely.")
                                                              : guardReason);
                                drawFooterHint(hint, allowed ? COLOR_INFO
                                                              : COLOR_DANGER);
                              }

                              static void renderAllTemps(
                                  const SystemState &state, bool full) {
                                const bool ru = (g_settings.language == 0);
                                const uint8_t expectedCount =
                                    countExpectedTempSensors();
                                const char *busLabel =
                                    Sensors::getTemperatureBusSourceLabel();
                                const bool useDs2482 =
                                    Sensors::isUsingDs2482ForTemps();
                                const bool busReady =
                                    useDs2482 ? Sensors::isDs2482Available()
                                              : (state.health.tempSensorsOk > 0 ||
                                                 state.health.tempSensorsTotal >
                                                     0);
                                if (full) {
                                  tft.fillScreen(colorBg());
                                  drawHeader(ru ? "ТЕМПЕРАТУРЫ"
                                                : "TEMPERATURES",
                                             true);
                                  drawTabs(ui.rootScreen);
                                  resetAllTempsCache();
                                }

                                const int16_t topY = 48;
                                const int16_t topH = 24;
                                const int16_t xStart = 10;
                                const int16_t yStart = 78;
                                const int16_t tileW = (TFT_WIDTH - 30) / 2;
                                const int16_t tileH = 40;
                                const int16_t gap = 6;

                                const char *labels[TEMP_COUNT] = {
                                    msg(Msg::CUBE_TEMP),
                                    msg(Msg::COLUMN_BOTTOM),
                                    msg(Msg::TOP_T),
                                    msg(Msg::REFLUX_T),
                                    msg(Msg::TSA_T),
                                    g_settings.language == 0 ? "ОХЛ ВХОД"
                                                             : "WATER IN",
                                    g_settings.language == 0 ? "ОХЛ ВЫХОД"
                                                             : "WATER OUT"};

                                float values[TEMP_COUNT] = {
                                    state.temps.cube,
                                    state.temps.columnBottom,
                                    state.temps.columnTop,
                                    state.temps.reflux,
                                    state.temps.tsa,
                                    state.temps.waterIn,
                                    state.temps.waterOut};

                                uint8_t validCount = 0;
                                for (uint8_t i = 0; i < TEMP_COUNT; i++) {
                                  if (state.temps.valid[i]) {
                                    validCount++;
                                  }
                                }

                                char busBuf[48];
                                snprintf(busBuf, sizeof(busBuf), "%s | %s",
                                         busLabel,
                                         busReady ? (ru ? "OK" : "READY")
                                                  : (ru ? "ЖДЁМ" : "WAIT"));
                                char countBuf[24];
                                snprintf(countBuf, sizeof(countBuf), "%u/%u",
                                         validCount,
                                         expectedCount > 0 ? expectedCount
                                                           : TEMP_COUNT);

                                if (full ||
                                    strcmp(g_dashboardCache.infoLine, busBuf) !=
                                        0 ||
                                    strcmp(g_dashboardCache.ioLine, countBuf) !=
                                        0) {
                                  if (!full) {
                                    tft.fillRect(10, topY, TFT_WIDTH - 20, topH,
                                                 colorBg());
                                  }
                                  const int16_t cardW =
                                      (TFT_WIDTH - 26) / 2;
                                  const int16_t countX = 10 + cardW + 6;
                                  drawCard(10, topY, cardW, topH, colorCard());
                                  drawCard(countX, topY, cardW, topH,
                                           colorCard());
                                  tft.setFont(&fonts::efontJA_16);
                                  tft.setTextSize(1);
                                  tft.setTextColor(colorMuted());
                                  tft.setTextDatum(top_left);
                                  drawDisplayString(
                                      ru ? "Термошина" : "Temp bus", 14,
                                      topY + 2);
                                  drawDisplayString(ru ? "Датчики" : "Sensors",
                                                    countX + 4, topY + 2);
                                  tft.setTextColor(busReady ? colorAccent()
                                                            : COLOR_WARNING);
                                  tft.setTextDatum(top_right);
                                  drawDisplayString(busBuf, 10 + cardW - 5,
                                                    topY + 2);
                                  tft.setTextColor(
                                      validCount >= expectedCount &&
                                              expectedCount > 0
                                          ? COLOR_SUCCESS
                                          : (validCount > 0 ? COLOR_WARNING
                                                            : colorMuted()));
                                  drawDisplayString(countBuf,
                                                    countX + cardW - 5,
                                                    topY + 2);
                                  tft.setTextDatum(top_left);
                                  strncpy(g_dashboardCache.infoLine, busBuf,
                                          sizeof(g_dashboardCache.infoLine) -
                                              1);
                                  g_dashboardCache.infoLine
                                      [sizeof(g_dashboardCache.infoLine) - 1] =
                                      '\0';
                                  strncpy(g_dashboardCache.ioLine, countBuf,
                                          sizeof(g_dashboardCache.ioLine) - 1);
                                  g_dashboardCache.ioLine
                                      [sizeof(g_dashboardCache.ioLine) - 1] =
                                      '\0';
                                }

                                for (uint8_t i = 0; i < TEMP_COUNT; i++) {
                                  const int16_t x =
                                      xStart + (i % 2) * (tileW + gap);
                                  const int16_t y =
                                      yStart + (i / 2) * (tileH + gap);
                                  const bool roleEnabled =
                                      isTempRoleEnabledByTopology(i);

                                  char valBuf[20];
                                  char statusBuf[12];
                                  uint16_t valueColor = COLOR_PRIMARY;
                                  if (state.temps.valid[i]) {
                                    snprintf(valBuf, sizeof(valBuf), "%.2f",
                                             values[i]);
                                    snprintf(statusBuf, sizeof(statusBuf), "%s",
                                             ru ? "OK" : "OK");
                                    valueColor = COLOR_PRIMARY;
                                  } else if (roleEnabled) {
                                    snprintf(valBuf, sizeof(valBuf), "%s",
                                             ru ? "---" : "---");
                                    snprintf(statusBuf, sizeof(statusBuf), "%s",
                                             ru ? "НЕТ" : "MISS");
                                    valueColor = COLOR_WARNING;
                                  } else {
                                    snprintf(valBuf, sizeof(valBuf), "%s",
                                             ru ? "ВЫКЛ" : "OFF");
                                    snprintf(statusBuf, sizeof(statusBuf), "%s",
                                             ru ? "LITE" : "LITE");
                                    valueColor = colorMuted();
                                  }

                                  if (full) {
                                    drawValueTileShell(x, y, tileW, tileH,
                                                       labels[i]);
                                  }
                                  if (full ||
                                      strcmp(g_allTempsValueCache[i], valBuf) !=
                                          0 ||
                                      strcmp(g_allTempsStatusCache[i],
                                             statusBuf) != 0) {
                                    if (!full) {
                                      tft.fillRect(x + 3, y + 16, tileW - 6,
                                                   tileH - 18, colorCard());
                                    }
                                    tft.setTextColor(valueColor);
                                    tft.setTextSize(1);
                                    tft.setFont(&fonts::efontJA_16);
                                    tft.setTextDatum(middle_center);
                                    drawDisplayString(valBuf, x + (tileW / 2),
                                                      y + 28);
                                    tft.setTextDatum(top_right);
                                    tft.setTextColor(roleEnabled
                                                         ? (state.temps.valid[i]
                                                                ? COLOR_SUCCESS
                                                                : COLOR_WARNING)
                                                         : colorMuted());
                                    drawDisplayString(statusBuf, x + tileW - 5,
                                                      y + 3);
                                    tft.setTextDatum(top_left);
                                    strncpy(g_allTempsValueCache[i], valBuf,
                                            sizeof(g_allTempsValueCache[i]) - 1);
                                    g_allTempsValueCache[i]
                                                       [sizeof(
                                                            g_allTempsValueCache
                                                                [i]) -
                                                        1] = '\0';
                                    strncpy(g_allTempsStatusCache[i], statusBuf,
                                            sizeof(g_allTempsStatusCache[i]) -
                                                1);
                                    g_allTempsStatusCache[i]
                                                        [sizeof(
                                                             g_allTempsStatusCache
                                                                 [i]) -
                                                         1] = '\0';
                                  }
                                }

                                char footerBuf[96];
                                snprintf(
                                    footerBuf, sizeof(footerBuf),
                                    ru ? "Живых %u/%u | Профиль %s | Δ%lus"
                                       : "Live %u/%u | Profile %s | Δ%lus",
                                    validCount,
                                    expectedCount > 0 ? expectedCount : TEMP_COUNT,
                                    getDisplayRefreshProfileCompactName(
                                        g_settings.displaySettings
                                            .refreshProfile),
                                    state.temps.lastUpdate > 0
                                        ? static_cast<unsigned long>(
                                              (millis() -
                                               state.temps.lastUpdate) /
                                              1000UL)
                                        : 0UL);
                                if (full ||
                                    strcmp(g_allTempsSummaryCache, footerBuf) !=
                                        0) {
                                  drawFooterHint(
                                      footerBuf,
                                      validCount > 0 ? COLOR_INFO
                                                     : COLOR_WARNING);
                                  strncpy(g_allTempsSummaryCache, footerBuf,
                                          sizeof(g_allTempsSummaryCache) - 1);
                                  g_allTempsSummaryCache
                                      [sizeof(g_allTempsSummaryCache) - 1] =
                                      '\0';
                                }
                              }

                              static void renderTouchCalibration() {
                                const bool ru = (g_settings.language == 0);
                                const uint16_t tone = (ui.calSkip > 0)
                                                          ? COLOR_WARNING
                                                          : COLOR_INFO;
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
                                const int16_t targetH =
                                    panelH - (targetY - panelY) - 18;

                                tft.fillScreen(colorNavBg());
                                tft.fillRect(0, 0, TFT_WIDTH, 6, tone);
                                drawCard(panelX, panelY, panelW, panelH,
                                         colorCard());
                                drawPanelHeader(panelX, panelY, panelW,
                                                msg(Msg::TOUCH_CAL_TITLE),
                                                tone);
                                drawCard(infoX, infoY, infoW, infoH, colorBg());
                                tft.fillRect(infoX + 1, infoY + 1, 8, infoH - 2,
                                             tone);

                                for (uint8_t i = 0; i < 4; i++) {
                                  const int16_t sx =
                                      panelX + panelW - 138 + i * 30;
                                  const int16_t sy = panelY + 8;
                                  const bool done = (i < ui.calStep);
                                  const bool current =
                                      (!ui.calSkip && i == ui.calStep);
                                  drawCard(sx, sy, 24, 16,
                                           done ? colorButtonBody()
                                                : colorBg());
                                  tft.fillRect(
                                      sx + 2, sy + 2, 20, 4,
                                      done
                                          ? COLOR_SUCCESS
                                          : (current ? tone : colorSoftFill()));
                                  tft.setTextColor(done ? COLOR_SUCCESS
                                                        : (current
                                                               ? colorFg()
                                                               : colorMuted()));
                                  tft.setTextSize(1);
                                  tft.setTextDatum(middle_center);
                                  char stepBuf[4];
                                  snprintf(stepBuf, sizeof(stepBuf), "%u",
                                           i + 1);
                                  drawDisplayString(stepBuf, sx + 12, sy + 10);
                                  tft.setTextDatum(top_left);
                                }

                                tft.setTextColor(colorFg());
                                tft.setTextSize(1);
                                tft.setTextDatum(top_center);
                                if (ui.calSkip > 0) {
                                  char buf[32];
                                  snprintf(buf, sizeof(buf),
                                           msg(Msg::TOUCH_CAL_TAP_N),
                                           ui.calSkip);
                                  drawDisplayString(buf, TFT_WIDTH / 2, infoY + 9);
                                  tft.setTextColor(colorMuted());
                                  drawDisplayString(
                                      ru ? "Короткие нажатия перед замером"
                                         : "Short taps before sampling",
                                      TFT_WIDTH / 2, infoY + 28);
                                } else {
                                  char buf[32];
                                  snprintf(buf, sizeof(buf), "%s  %d/4",
                                           msg(Msg::TOUCH_CAL_TOUCH_TARGET),
                                           ui.calStep + 1);
                                  drawDisplayString(buf, TFT_WIDTH / 2, infoY + 9);
                                  tft.setTextColor(colorMuted());
                                  drawDisplayString(
                                      ru ? "Отпускайте палец после каждого "
                                           "касания"
                                         : "Release finger after each target",
                                      TFT_WIDTH / 2, infoY + 28);
                                }
                                tft.setTextDatum(top_left);

                                drawCard(targetX, targetY, targetW, targetH,
                                         colorBg());
                                tft.fillRect(targetX + 1, targetY + 1, 8,
                                             targetH - 2, tone);

                                if (ui.calSkip > 0) {
                                  char countBuf[8];
                                  snprintf(countBuf, sizeof(countBuf), "%u",
                                           ui.calSkip);
                                  tft.setTextColor(tone);
                                  tft.setTextSize(5);
                                  tft.setTextDatum(middle_center);
                                  drawDisplayString(countBuf, TFT_WIDTH / 2,
                                                 targetY + (targetH / 2) - 6);
                                  tft.setTextSize(1);
                                  tft.setTextColor(colorMuted());
                                  drawDisplayString(
                                      ru ? "Подготовка тач-контроллера"
                                         : "Preparing touch controller",
                                      TFT_WIDTH / 2, targetY + targetH - 34);
                                  tft.setTextDatum(top_left);
                                } else {
                                  const int16_t points[4][2] = {
                                      {targetX + 34, targetY + 28},
                                      {targetX + targetW - 34, targetY + 28},
                                      {targetX + targetW - 34,
                                       targetY + targetH - 28},
                                      {targetX + 34, targetY + targetH - 28}};
                                  const uint8_t stepIdx =
                                      (ui.calStep < 4) ? ui.calStep : 3;
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
                                  drawDisplayString(
                                      ru ? "Коснитесь прямоугольной мишени"
                                         : "Touch the rectangular target",
                                      TFT_WIDTH / 2,
                                      targetY + (targetH / 2) - 8);
                                  drawDisplayString(
                                      ru ? "Замер берется по среднему удержанию"
                                         : "Sampling uses averaged hold data",
                                      TFT_WIDTH / 2,
                                      targetY + (targetH / 2) + 12);
                                  tft.setTextDatum(top_left);
                                }
                              }

                              typedef void (*UiRenderHandler)(
                                  const SystemState &state, bool full);

                              static void renderSettingsScreen(
                                  const SystemState &, bool full) {
                                renderSettings(full);
                              }

                              static void renderEquipmentScreen(
                                  const SystemState &, bool full) {
                                renderEquipment(full);
                              }

                              static void renderRectParamsScreen(
                                  const SystemState &, bool full) {
                                renderRectParams(full);
                              }

                              static void renderDistParamsScreen(
                                  const SystemState &, bool full) {
                                renderDistParams(full);
                              }

                              static void renderCalibrationScreen(
                                  const SystemState &, bool full) {
                                renderCalibration(full);
                              }

                              static void renderPumpCalibrationScreen(
                                  const SystemState &state, bool full) {
                                renderPumpCalibration(state, full);
                              }

                              static void renderSensorCalibrationScreen(
                                  const SystemState &state, bool full) {
                                renderSensorCalibration(state, full);
                              }

                              static void renderManualScreen(
                                  const SystemState &state, bool) {
                                renderManual(state);
                              }

                              static void renderValueEditScreen(
                                  const SystemState &, bool) {
                                renderValueEdit();
                              }

                                  static constexpr UiRenderHandler
                                  kUiRenderDispatch[] = {
                                      renderHome,            // UI_DASHBOARD / Home
                                      renderControl,         // UI_CONTROL
                                      renderSettingsScreen,  // UI_SETTINGS
                                      renderService,         // UI_SERVICE
                                      renderModeMonitor,     // UI_MODE_MONITOR
                                      renderEquipmentScreen, // UI_EQUIPMENT
                                      renderRectParamsScreen, // UI_RECT_PARAMS
                                      renderDistParamsScreen, // UI_DIST_PARAMS
                                      renderCalibrationScreen, // UI_CALIBRATION
                                      renderManualScreen,      // UI_MANUAL
                                      renderValueEditScreen,   // UI_VALUE_EDIT
                                      renderAllTemps,          // UI_ALL_TEMPS
                                      renderModePicker,        // UI_MODE_PICKER
                                      renderModeSetup,         // UI_MODE_SETUP
                                      renderPreflight,         // UI_PREFLIGHT
                                      renderProfilePicker,     // UI_PROFILE_PICKER
                                      renderSafetyView,        // UI_SAFETY_VIEW
                                      renderProcessEvents,     // UI_PROCESS_EVENTS
                                      renderProcessParams,     // UI_PROCESS_PARAMS
                                      renderModeConfig,        // UI_MODE_CONFIG
                                      renderModeParameters,    // UI_MODE_PARAMETERS
                                      renderServiceTest,       // UI_SERVICE_TEST
                                      renderPumpCalibrationScreen, // UI_PUMP_CALIBRATION
                                      renderSensorCalibrationScreen // UI_SENSOR_CALIBRATION
                                  };

                              static UiRenderHandler
                              getUiRenderHandler(UiScreen screen) {
                                const size_t screenIndex =
                                    static_cast<size_t>(screen);
                                if (screenIndex <
                                    (sizeof(kUiRenderDispatch) /
                                     sizeof(kUiRenderDispatch[0]))) {
                                  return kUiRenderDispatch[screenIndex];
                                }
                                return renderDashboard;
                              }

                              static void applyTouchCalibration() {
                                int16_t xLeft =
                                    (ui.calRawX[0] + ui.calRawX[3]) / 2;
                                int16_t xRight =
                                    (ui.calRawX[1] + ui.calRawX[2]) / 2;
                                int16_t yTop =
                                    (ui.calRawY[0] + ui.calRawY[1]) / 2;
                                int16_t yBottom =
                                    (ui.calRawY[2] + ui.calRawY[3]) / 2;

                                g_settings.touchCal.xMin = xLeft;
                                g_settings.touchCal.xMax = xRight;
                                g_settings.touchCal.yMin = yTop;
                                g_settings.touchCal.yMax = yBottom;
                                g_settings.touchCal.valid = true;

                                NVSManager::saveSettings(g_settings);
                              }

                              static bool initDisplayHardware(
                                  bool showBootSplash) {
                                tft_ok = tft.init();
                                if (!tft_ok) {
                                  touch_ok = false;
                                  return false;
                                }

                                tft.setRotation(1); // Landscape (480x320)
                                tft.setSwapBytes(true);
                                tft.setTextScroll(false);
                                tft.setTextSize(1);
                                tft.setAttribute(lgfx::v1::attribute::UTF8_SWITCH, true);
                                tft.setFont(&fonts::efontJA_16);

                                // Re-init dedicated touch SPI to recover after
                                // bus faults.
                                pinMode(TOUCH_IRQ, INPUT_PULLUP);
                                touchSpi.end();
                                touchSpi.begin(TOUCH_CLK, TOUCH_DO, TOUCH_DIN,
                                               TOUCH_CS);
                                touch_ok = touch.begin(touchSpi);
                                if (touch_ok) {
                                  touch.setRotation(1);
                                }

                                if (showBootSplash) {
                                  char bootBuf[96];
                                  snprintf(
                                      bootBuf, sizeof(bootBuf),
                                      "Smart-Column S3\nILI9488 / XPT2046");
                                  drawFullscreenOverlay(
                                      "SYSTEM START", bootBuf, COLOR_SUCCESS,
                                      touch_ok ? FW_VERSION : "DISPLAY ONLY",
                                      2);
                                  drawWrappedTextBlock(
                                      70, 214, TFT_WIDTH - 140,
                                      touch_ok ? "TFT + TOUCH READY"
                                               : "TFT READY / TOUCH OFFLINE",
                                      touch_ok ? COLOR_SUCCESS : COLOR_WARNING,
                                      1, 2, 4);
                                  delay(1200);
                                }

                                tft.fillScreen(colorNavBg());
                                return true;
                              }

                              static bool attemptHardRecovery(uint32_t nowMs) {
                                if (nowMs -
                                        g_displayStats.lastHardRecoveryAtMs <
                                    DISPLAY_HARD_RECOVERY_COOLDOWN_MS) {
                                  return false;
                                }

                                g_displayStats.lastHardRecoveryAtMs = nowMs;
                                LOG_W("Display: hard watchdog recovery "
                                      "requested");

                                if (!initDisplayHardware(false)) {
                                  g_displayStats.hardWatchdogFailures++;
                                  LOG_E(
                                      "Display: hard watchdog recovery failed");
                                  return false;
                                }

                                g_displayStats.hardWatchdogRecoveries++;
                                g_displayStats.consecutiveSlowFrames = 0;
                                g_displayStats.consecutiveHardFrames = 0;
                                g_displayStats.softRecoveriesInWindow = 0;
                                g_displayStats.softRecoveryWindowStartedMs = 0;

                                requestRedraw(DisplayRedrawReason::RECOVERY_REDRAW,
                                              true);
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
                                  memset(&mashProfileDefault, 0,
                                         sizeof(mashProfileDefault));
                                  strncpy(mashProfileDefault.name,
                                          "Default mash",
                                          sizeof(mashProfileDefault.name) - 1);
                                  mashProfileDefault.stepCount = 5;

                                  mashProfileDefault.steps[0].temperature =
                                      38.0f;
                                  mashProfileDefault.steps[0].duration = 20;
                                  strncpy(
                                      mashProfileDefault.steps[0].name,
                                      "Acid rest",
                                      sizeof(mashProfileDefault.steps[0].name) -
                                          1);

                                  mashProfileDefault.steps[1].temperature =
                                      52.0f;
                                  mashProfileDefault.steps[1].duration = 20;
                                  strncpy(
                                      mashProfileDefault.steps[1].name,
                                      "Protein rest",
                                      sizeof(mashProfileDefault.steps[1].name) -
                                          1);

                                  mashProfileDefault.steps[2].temperature =
                                      63.0f;
                                  mashProfileDefault.steps[2].duration = 40;
                                  strncpy(
                                      mashProfileDefault.steps[2].name,
                                      "Maltose rest",
                                      sizeof(mashProfileDefault.steps[2].name) -
                                          1);

                                  mashProfileDefault.steps[3].temperature =
                                      72.0f;
                                  mashProfileDefault.steps[3].duration = 20;
                                  strncpy(
                                      mashProfileDefault.steps[3].name,
                                      "Saccharification",
                                      sizeof(mashProfileDefault.steps[3].name) -
                                          1);

                                  mashProfileDefault.steps[4].temperature =
                                      78.0f;
                                  mashProfileDefault.steps[4].duration = 10;
                                  strncpy(
                                      mashProfileDefault.steps[4].name,
                                      "Mash out",
                                      sizeof(mashProfileDefault.steps[4].name) -
                                          1);

                                  holdStepsDefault[0].temperature = 65.0f;
                                  holdStepsDefault[0].duration = 60;
                                  holdStepsCount = 1;

                                  // Загружаем уставки дистилляции из
                                  // NVS-профиля настроек.
                                  distUi.speedMlH =
                                      g_settings.distillationUi.speedMlH;
                                  distUi.headsVolumeMl =
                                      g_settings.distillationUi.headsVolumeMl;
                                  distUi.targetVolumeMl =
                                      g_settings.distillationUi.targetVolumeMl;
                                  distUi.endTempC =
                                      g_settings.distillationUi.endTempC;
                                  distUi.powerPercent =
                                      g_settings.distillationUi.powerPercent;
                                  distUi.tailsVolumeMl =
                                      g_settings.distillationUi.tailsVolumeMl;
                                  applyDistUiRuntime();

                                  ui.currentScreen = UI_DASHBOARD;
                                  ui.rootScreen = UI_DASHBOARD;
                                  ui.stackDepth = 0;
                                  requestRedraw(
                                      DisplayRedrawReason::SCREEN_ENTER, true);
                                  const bool forceCal =
                                      touch_ok ? detectCalibrationRequest()
                                               : false;
                                  ui.calibrating =
                                      !g_settings.touchCal.valid || forceCal;
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

                                if (now - lastSparklineUpdateMs >
                                    getSparklineRefreshIntervalMs()) {
                                  if (state.temps.valid[TEMP_CUBE]) {
                                    cubeTempHistory.push(state.temps.cube);
                                  }
                                  if (state.temps.valid[TEMP_COLUMN_TOP]) {
                                    topTempHistory.push(state.temps.columnTop);
                                  }
                                  lastSparklineUpdateMs = now;

                                  // Принудительно инвалидируем кэш интерфейса,
                                  // чтобы графики обновлялись каждые 5 секунд,
                                  // даже если температура (цифры) абсолютно
                                  // стабильна.
                                  g_dashboardCache.cube[0] = '\0';
                                  g_dashboardCache.top[0] = '\0';
                                  memset(g_modeTileCache, 0,
                                         sizeof(g_modeTileCache));
                                  requestRedraw(
                                      DisplayRedrawReason::SPARKLINE_REFRESH);
                                }

                                if (g_displayStats.lastUpdateCallAtMs > 0) {
                                  const uint32_t updateGapMs =
                                      now - g_displayStats.lastUpdateCallAtMs;
                                  const uint16_t clampedGapMs =
                                      static_cast<uint16_t>(updateGapMs > 0xFFFF
                                                                ? 0xFFFF
                                                                : updateGapMs);
                                  g_displayStats.lastUpdateGapMs = clampedGapMs;
                                  if (clampedGapMs >
                                      g_displayStats.maxUpdateGapMs) {
                                    g_displayStats.maxUpdateGapMs =
                                        clampedGapMs;
                                  }
                                  if (updateGapMs >
                                      DISPLAY_UPDATE_GAP_WARN_MS) {
                                    g_displayStats.updateGapOverruns++;
                                  }
                                }
                                g_displayStats.lastUpdateCallAtMs = now;

                                updateServiceTestLifecycle(state);
                                updatePumpCalibrationLifecycle(state);

                                if (!tft_ok) {
                                  attemptHardRecovery(now);
                                  return;
                                }

                                if (ui.calibrating) {
                                  TouchEvent ev;
                                  if (touch_ok) {
                                    const bool wasPressed = ui.touchPressed;
                                    ev = readTouchEvent();

                                    // Accumulate raw samples continuously while
                                    // the finger is held. This gives a stable
                                    // averaged position for each calibration
                                    // point. We do NOT use readTouchRawFiltered
                                    // after ev.tapped because by then the
                                    // finger is already released and the
                                    // controller has no data.
                                    if (ev.pressed) {
                                      int16_t rx = 0, ry = 0;
                                      if (touchReadRaw(&rx, &ry)) {
                                        if (!wasPressed) {
                                          // First frame of this press: start
                                          // fresh accumulator.
                                          ui.calSumRawX = rx;
                                          ui.calSumRawY = ry;
                                          ui.calSampleCount = 1;
                                          ui.calIsCollecting = true;
                                        } else if (ui.calIsCollecting &&
                                                   ui.calSampleCount < 500) {
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
                                        requestRedraw(
                                            DisplayRedrawReason::TAP_ACTION);
                                      } else if (ui.calIsCollecting &&
                                                 ui.calSampleCount > 0) {
                                        if (ui.calStep < 4) {
                                          ui.calRawX[ui.calStep] =
                                              (int16_t)(ui.calSumRawX /
                                                        ui.calSampleCount);
                                          ui.calRawY[ui.calStep] =
                                              (int16_t)(ui.calSumRawY /
                                                        ui.calSampleCount);
                                          ui.calStep++;
                                          ui.calIsCollecting = false;
                                          ui.calSampleCount = 0;
                                          requestRedraw(
                                              DisplayRedrawReason::TAP_ACTION);
                                        }
                                        if (ui.calStep >= 4) {
                                          applyTouchCalibration();
                                          ui.calibrating = false;
                                          requestRedraw(
                                              DisplayRedrawReason::SCREEN_ENTER,
                                              true);
                                          // Calibration is finished; render
                                          // normal UI on next update cycle.
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

                                // Auto-select monitor root window: idle
                                // overview vs active mode window.
                                if (ui.stackDepth == 0 &&
                                    getUiScreenPolicy(ui.rootScreen)
                                        .autoMonitorRoot) {
                                  const UiScreen desiredMonitor =
                                      getMonitorRootScreenForState(state);
                                  if (ui.rootScreen != desiredMonitor ||
                                      ui.currentScreen != desiredMonitor) {
                                    switchRoot(desiredMonitor);
                                  }
                                }

                                syncOperatorOverlay(state);

                                if (!ui.needsRedraw &&
                                    g_displayStats.lastFrameAtMs > 0 &&
                                    (now - g_displayStats.lastFrameAtMs) >
                                        getForceRefreshIntervalMs()) {
                                  requestRedraw(
                                      DisplayRedrawReason::TIMER_KEEPALIVE);
                                }

                                if (!ui.needsRedraw) {
                                  bool changed = false;
                                  const HmiNetworkStatus networkStatus =
                                      getHmiNetworkStatus();
                                  // The global status line is intentionally
                                  // live on every page. Workspace refreshes
                                  // below are restricted by UiScreenPolicy.
                                  if (uiLive.safetyOk != state.safetyOk ||
                                      strcmp(uiLive.networkStatus,
                                             networkStatus.summary) != 0) {
                                    changed = true;
                                  }
                                  if (screenSupportsLiveRefresh(
                                          ui.currentScreen) &&
                                      hasPageLiveDataChanged(ui.currentScreen,
                                                             state, now)) {
                                    changed = true;
                                  }
                                  if (changed &&
                                      (now - uiLive.lastUpdateMs) >
                                          getLiveRedrawMinGapMs()) {
                                    requestRedraw(
                                        DisplayRedrawReason::LIVE_DATA_CHANGED);
                                  }
                                }

                                if (ev.tapped) {
                                  bool handled = false;
                                  // Пробуем прямые координаты
                                  handled =
                                      handleNavigationTap(ev.x, ev.y, state);
                                  if (!handled) {
                                    handled =
                                        handleScreenTap(ev.x, ev.y, state);
                                  }

                                  if (handled) {
                                    requestRedraw(
                                        DisplayRedrawReason::TAP_ACTION);
                                  }
                                }

                                if (ui.needsRedraw) {
                                  const uint32_t frameStartMs = millis();
                                  const uint32_t renderGeneration =
                                      ui.redrawGeneration;
                                  const DisplayRedrawReason redrawReason =
                                      ui.pendingRedrawReason;
                                  const bool full = ui.forceFullRedraw ||
                                                    (ui.currentScreen !=
                                                     ui.lastRenderedScreen);
                                  // A renderer may request a second frame
                                  // (for example after a layout key changes).
                                  // Clear the current request first so that
                                  // the next request survives this frame.
                                  ui.needsRedraw = false;
                                  ui.pendingRedrawReason =
                                      DisplayRedrawReason::NONE;
                                  ui.forceFullRedraw = false;
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
                                  uiLive.tempValidMask =
                                      getUiTempValidMask(state);
                                  uiLive.tempsLastUpdate =
                                      state.temps.lastUpdate;
                                  uiLive.uptime = state.uptime;
                                  uiLive.safetyOk = state.safetyOk;
                                  const HmiNetworkStatus networkStatus =
                                      getHmiNetworkStatus();
                                  strncpy(uiLive.networkStatus,
                                          networkStatus.summary,
                                          sizeof(uiLive.networkStatus) - 1);
                                  uiLive.networkStatus
                                      [sizeof(uiLive.networkStatus) - 1] =
                                      '\0';
                                  uiLive.lastUpdateMs = now;
                                  UiRenderHandler renderHandler =
                                      getUiRenderHandler(ui.currentScreen);
                                  tft.startWrite();
                                  renderHandler(state, full);
                                  drawGlobalStatusBar(state, false);
                                  if (TftOperatorOverlayLogic::isActive(
                                          ui.operatorOverlay)) {
                                    drawOperatorOverlay(state);
                                  }
                                  tft.endWrite();

                                  const uint32_t frameTime =
                                      millis() - frameStartMs;
                                  bool scheduleRecoveryRedraw = false;
                                  bool requestHardRecovery = false;
                                  g_displayStats.framesRendered++;
                                  if (full) {
                                    g_displayStats.fullRedraws++;
                                  } else {
                                    g_displayStats.partialRedraws++;
                                  }
                                  g_displayStats.lastRedrawReason =
                                      redrawReason;
                                  switch (redrawReason) {
                                  case DisplayRedrawReason::SCREEN_ENTER:
                                    g_displayStats.redrawReasonScreenEnter++;
                                    break;
                                  case DisplayRedrawReason::OPERATOR_OVERLAY:
                                    break;
                                  case DisplayRedrawReason::TAP_ACTION:
                                    g_displayStats.redrawReasonTapAction++;
                                    break;
                                  case DisplayRedrawReason::LIVE_DATA_CHANGED:
                                    g_displayStats.redrawReasonLiveDataChanged++;
                                    break;
                                  case DisplayRedrawReason::TIMER_KEEPALIVE:
                                    g_displayStats.redrawReasonTimerKeepalive++;
                                    break;
                                  case DisplayRedrawReason::SPARKLINE_REFRESH:
                                    g_displayStats.redrawReasonSparklineRefresh++;
                                    break;
                                  case DisplayRedrawReason::THEME_CHANGED:
                                    g_displayStats.redrawReasonThemeChanged++;
                                    break;
                                  case DisplayRedrawReason::LANGUAGE_CHANGED:
                                    g_displayStats.redrawReasonLanguageChanged++;
                                    break;
                                  case DisplayRedrawReason::LAYOUT_CHANGED:
                                    g_displayStats.redrawReasonLayoutChanged++;
                                    break;
                                  case DisplayRedrawReason::RECOVERY_REDRAW:
                                    g_displayStats.redrawReasonRecovery++;
                                    break;
                                  case DisplayRedrawReason::NONE:
                                  default:
                                    break;
                                  }
                                  g_displayStats.lastFrameMs =
                                      static_cast<uint16_t>(frameTime > 0xFFFF
                                                                ? 0xFFFF
                                                                : frameTime);
                                  g_displayStats.lastFrameAtMs = millis();
                                  if (g_displayStats.lastFrameMs >
                                      g_displayStats.maxFrameMs) {
                                    g_displayStats.maxFrameMs =
                                        g_displayStats.lastFrameMs;
                                  }

                                  if (frameTime >= DISPLAY_SLOW_FRAME_MS) {
                                    g_displayStats.slowFrames++;
                                    if (!full &&
                                        frameTime >= DISPLAY_HARD_FRAME_MS) {
                                      if (g_displayStats.consecutiveSlowFrames <
                                          255)
                                        g_displayStats.consecutiveSlowFrames++;
                                      if (g_displayStats.consecutiveHardFrames <
                                          255)
                                        g_displayStats.consecutiveHardFrames++;
                                    } else {
                                      g_displayStats.consecutiveSlowFrames = 0;
                                      g_displayStats.consecutiveHardFrames = 0;
                                    }
                                  } else {
                                    g_displayStats.consecutiveSlowFrames = 0;
                                    g_displayStats.consecutiveHardFrames = 0;
                                  }

                                  if (g_displayStats.consecutiveSlowFrames >=
                                      DISPLAY_SOFT_WD_THRESHOLD) {
                                    // Soft watchdog: force a full redraw cycle
                                    // instead of running with a stale frame.
                                    g_displayStats.watchdogRecoveries++;
                                    g_displayStats.consecutiveSlowFrames = 0;
                                    scheduleRecoveryRedraw = true;

                                    if (g_displayStats
                                                .softRecoveryWindowStartedMs ==
                                            0 ||
                                        (now -
                                         g_displayStats
                                             .softRecoveryWindowStartedMs) >
                                            DISPLAY_SOFT_WD_WINDOW_MS) {
                                      g_displayStats
                                          .softRecoveryWindowStartedMs = now;
                                      g_displayStats.softRecoveriesInWindow = 1;
                                    } else if (g_displayStats
                                                   .softRecoveriesInWindow <
                                               255) {
                                      g_displayStats.softRecoveriesInWindow++;
                                    }
                                  }

                                  if (g_displayStats
                                              .softRecoveryWindowStartedMs >
                                          0 &&
                                      (now - g_displayStats
                                                 .softRecoveryWindowStartedMs) >
                                          DISPLAY_SOFT_WD_WINDOW_MS) {
                                    g_displayStats.softRecoveryWindowStartedMs =
                                        now;
                                    g_displayStats.softRecoveriesInWindow = 0;
                                  }

                                  if (g_displayStats.consecutiveHardFrames >=
                                          DISPLAY_HARD_FRAME_BURST_THRESHOLD ||
                                      g_displayStats.softRecoveriesInWindow >=
                                          DISPLAY_SOFT_WD_BURST_FOR_HARD) {
                                    requestHardRecovery = true;
                                  }

                                  if (requestHardRecovery &&
                                      attemptHardRecovery(now)) {
                                    return;
                                  }

                                  const bool redrawQueuedDuringRender =
                                      ui.redrawGeneration != renderGeneration;
                                  if (scheduleRecoveryRedraw) {
                                    ui.needsRedraw = true;
                                    ui.pendingRedrawReason =
                                        DisplayRedrawReason::RECOVERY_REDRAW;
                                    ui.forceFullRedraw = true;
                                  } else if (!redrawQueuedDuringRender) {
                                    ui.needsRedraw = false;
                                    ui.pendingRedrawReason =
                                        DisplayRedrawReason::NONE;
                                    ui.forceFullRedraw = false;
                                  }
                                  ui.lastRenderedScreen = ui.currentScreen;
                                }
#endif
                              }

                              void showMessage(const char *title,
                                               const char *message,
                                               uint8_t type) {
#if TFT_ENABLED
                                if (tft_ok) {
                                  uint16_t color = colorAccent();
                                  if (type == 1) {
                                    color = COLOR_WARNING;
                                  } else if (type == 2) {
                                    color = COLOR_DANGER;
                                  }
                                  tft.startWrite();
                                  drawFullscreenOverlay(title, message, color,
                                                        FW_VERSION,
                                                        (message != nullptr &&
                                                         strlen(message) < 24)
                                                            ? 2
                                                            : 1);
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
                                requestRedraw(DisplayRedrawReason::SCREEN_ENTER,
                                              true);
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
                                stats.framesRendered =
                                    g_displayStats.framesRendered;
                                stats.fullRedraws = g_displayStats.fullRedraws;
                                stats.partialRedraws =
                                    g_displayStats.partialRedraws;
                                stats.slowFrames = g_displayStats.slowFrames;
                                stats.watchdogRecoveries =
                                    g_displayStats.watchdogRecoveries;
                                stats.hardWatchdogRecoveries =
                                    g_displayStats.hardWatchdogRecoveries;
                                stats.hardWatchdogFailures =
                                    g_displayStats.hardWatchdogFailures;
                                stats.lastFrameMs = g_displayStats.lastFrameMs;
                                stats.maxFrameMs = g_displayStats.maxFrameMs;
                                stats.lastFrameAtMs =
                                    g_displayStats.lastFrameAtMs;
                                stats.lastUpdateGapMs =
                                    g_displayStats.lastUpdateGapMs;
                                stats.maxUpdateGapMs =
                                    g_displayStats.maxUpdateGapMs;
                                stats.updateGapOverruns =
                                    g_displayStats.updateGapOverruns;
                                stats.redrawReasonScreenEnter =
                                    g_displayStats.redrawReasonScreenEnter;
                                stats.redrawReasonTapAction =
                                    g_displayStats.redrawReasonTapAction;
                                stats.redrawReasonLiveDataChanged =
                                    g_displayStats.redrawReasonLiveDataChanged;
                                stats.redrawReasonTimerKeepalive =
                                    g_displayStats.redrawReasonTimerKeepalive;
                                stats.redrawReasonSparklineRefresh =
                                    g_displayStats.redrawReasonSparklineRefresh;
                                stats.redrawReasonThemeChanged =
                                    g_displayStats.redrawReasonThemeChanged;
                                stats.redrawReasonLanguageChanged =
                                    g_displayStats.redrawReasonLanguageChanged;
                                stats.redrawReasonLayoutChanged =
                                    g_displayStats.redrawReasonLayoutChanged;
                                stats.redrawReasonRecovery =
                                    g_displayStats.redrawReasonRecovery;
                                snprintf(stats.lastRedrawReason,
                                         sizeof(stats.lastRedrawReason), "%s",
                                         getDisplayRedrawReasonKey(
                                             g_displayStats.lastRedrawReason));
#endif
                                return stats;
                              }

                              void showError(const char *error) {
#if TFT_ENABLED
                                if (tft_ok) {
                                  const bool ru = (g_settings.language == 0);
                                  tft.startWrite();
                                  drawFullscreenOverlay(
                                      ru ? "ОШИБКА" : "ERROR", error,
                                      COLOR_DANGER,
                                      ru ? "Проверьте питание, датчики и логи"
                                         : "Check power, sensors and logs",
                                      1);
                                  tft.endWrite();
                                }
#endif
                              }

                              } // namespace Display
