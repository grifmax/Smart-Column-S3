/**
 * Smart-Column S3 - Р”СЂР°Р№РІРµСЂ РґРёСЃРїР»РµСЏ
 *
 * TFT 3.5" ILI9488 (РѕСЃРЅРѕРІРЅРѕР№)
 * РСЃРїРѕР»СЊР·СѓРµС‚ LovyanGFX РґР»СЏ TFT
 */

#include "display.h"
#include <LovyanGFX.hpp>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <esp_task_wdt.h>
#include "storage/nvs_manager.h"
#include "control/fsm.h"
#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/valves.h"
#include "interface/localization.h"

#if TFT_ENABLED
#define LGFX_USE_V1
// =============================================================================
// LovyanGFX РєРѕРЅС„РёРіСѓСЂР°С†РёСЏ РґР»СЏ ILI9488 (С‚РѕР»СЊРєРѕ РґРёСЃРїР»РµР№)
// =============================================================================

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9488 _panel_instance;
    lgfx::Bus_SPI _bus_instance;

public:
    LGFX(void) {
        // SPI bus configuration
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;     // HSPI
            cfg.spi_mode = 0;
            // 40MHz РЅР° ILI9488 С‡Р°СЃС‚Рѕ РЅРµСЃС‚Р°Р±РёР»РµРЅ РЅР° РґР»РёРЅРЅС‹С… РїСЂРѕРІРѕРґР°С…/РєР»РѕРЅР°С….
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

bool touchReadRaw(int16_t* x, int16_t* y) {
#ifndef TOUCH_IGNORE_IRQ
    if (!touch.touched()) return false;
#else
    if (!touch.tirqTouched() && !touch.touched()) return false;
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

static void getTouchCalibration(int16_t* xMin, int16_t* xMax, int16_t* yMin, int16_t* yMax) {
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
bool touchRead(int16_t* sx, int16_t* sy) {
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

static bool readTouchRawFiltered(int16_t* x, int16_t* y) {
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
    if (samples == 0) return false;
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
static const int16_t UI_HEADER_H = 40;  // РќРµРјРЅРѕРіРѕ СѓРјРµРЅСЊС€РёР»
static const int16_t UI_FOOTER_H = 65;  // РќРµРјРЅРѕРіРѕ СѓРІРµР»РёС‡РёР» РґР»СЏ С€СЂРёС„С‚Р°
static const int16_t UI_CONTENT_Y = 10; // РќР°С‡РёРЅР°РµРј РїРѕС‡С‚Рё СЃРІРµСЂС…Сѓ
static const int16_t UI_CONTENT_H = TFT_HEIGHT - UI_FOOTER_H - 10;

// Colors matching web UI
#define COLOR_PRIMARY     tft.color565(0, 123, 255)  // #007bff
#define COLOR_SUCCESS     tft.color565(40, 167, 69)  // #28a745
#define COLOR_DANGER      tft.color565(220, 53, 69)  // #dc3545
#define COLOR_WARNING     tft.color565(255, 193, 7)  // #ffc107
#define COLOR_INFO        tft.color565(23, 162, 184) // #17a2b8
#define COLOR_DARK_GREY   tft.color565(52, 58, 64)   // #343a40
#define COLOR_LIGHT_GREY  tft.color565(233, 236, 239)// #e9ecef

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
    UI_VALUE_EDIT
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
        const int32_t dx = static_cast<int32_t>(ui.touchLastX) - static_cast<int32_t>(ui.touchDownX);
        const int32_t dy = static_cast<int32_t>(ui.touchLastY) - static_cast<int32_t>(ui.touchDownY);
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
    if (ui.currentScreen == screen) return;
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
    if (ui.currentScreen == target) return;
    ui.currentScreen = target;
    ui.needsRedraw = true;
    armScreenSwitchGuard();
}

static void switchRoot(UiScreen screen) {
    const bool changed = (ui.rootScreen != screen) || (ui.currentScreen != screen) || (ui.stackDepth != 0);
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
};
static ValueEditState edit;

static void openValueEdit(const char* label, float val, float min, float max, float step, float fastStep, ValueSaveCallback cb, const char* unit = "", uint8_t decimals = 1) {
    strncpy(edit.label, label, sizeof(edit.label)-1);
    edit.label[sizeof(edit.label) - 1] = '\0';
    edit.value = val;
    edit.min = min;
    edit.max = max;
    edit.step = step;
    edit.fastStep = fastStep;
    edit.onSave = cb;
    strncpy(edit.unit, unit, sizeof(edit.unit)-1);
    edit.unit[sizeof(edit.unit) - 1] = '\0';
    edit.decimals = decimals;
    pushScreen(UI_VALUE_EDIT);
}

struct DistUiParams {
    float speedMlH = 500.0f;
    float headsVolumeMl = 0.0f;
    float targetVolumeMl = 3000.0f;
    float endTempC = 96.0f;
};

static DistUiParams distUi;

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

static const uint16_t DISPLAY_SLOW_FRAME_MS = 120;
static const uint16_t DISPLAY_HARD_FRAME_MS = 250;
static const uint32_t DISPLAY_FORCE_REFRESH_MS = 5000;
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
static const int16_t CTRL_Y1 = 44;
static const int16_t CTRL_Y2 = 96;
static const int16_t CTRL_Y3 = 148;
static const int16_t CTRL_Y4 = 200;

static bool hit(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    return (x >= rx && x <= (rx + rw) && y >= ry && y <= (ry + rh));
}

static bool hitPad(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh, int16_t pad) {
    return hit(x, y, rx - pad, ry - pad, rw + (pad * 2), rh + (pad * 2));
}

static bool isModeRunning(const SystemState& state) {
    return state.mode != Mode::IDLE;
}

static bool isMonitorRootScreen(UiScreen screen) {
    return (screen == UI_DASHBOARD || screen == UI_MODE_MONITOR);
}

static uint16_t dimmedButtonColor() {
    return tft.color565(140, 140, 140);
}

static uint16_t modeButtonColor(const SystemState& state, Mode target, uint16_t idleColor) {
    if (state.mode == target) return COLOR_SUCCESS;
    if (isModeRunning(state)) return dimmedButtonColor();
    return idleColor;
}

static void startModeFromControl(Mode mode) {
    switch (mode) {
        case Mode::RECTIFICATION:
        case Mode::DISTILLATION:
        case Mode::MANUAL_RECT:
            if (mode == Mode::DISTILLATION) {
                FSM::Distillation::setParams(distUi.speedMlH,
                                             distUi.headsVolumeMl,
                                             distUi.targetVolumeMl,
                                             distUi.endTempC);
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

static void startOrRequestMode(const SystemState& state, Mode target) {
    if (state.mode == target) return;
    if (isModeRunning(state)) {
        requestModeSwitch(target);
        return;
    }
    startModeFromControl(target);
}

// =============================================================================
// Settings Callbacks
// =============================================================================
static void saveHeaterPower(float val) { g_settings.equipment.heaterPowerW = (uint16_t)val; NVSManager::saveSettings(g_settings); }
static void saveColumnHeight(float val) { g_settings.equipment.columnHeightMm = (uint16_t)val; NVSManager::saveSettings(g_settings); }
static void saveCubeVolume(float val) { g_settings.equipment.cubeVolumeL = val; NVSManager::saveSettings(g_settings); }
static void savePackingCoeff(float val) { g_settings.equipment.packingCoeff = val; NVSManager::saveSettings(g_settings); }

static void saveHeadsPercent(float val) { g_settings.rectParams.headsPercent = val; NVSManager::saveSettings(g_settings); }
static void saveHeadsSpeed(float val) { g_settings.rectParams.headsSpeedMlHKw = val; NVSManager::saveSettings(g_settings); }
static void saveBodySpeed(float val) { g_settings.rectParams.bodySpeedMlHKw = val; NVSManager::saveSettings(g_settings); }
static void saveStabMin(float val) { g_settings.rectParams.stabilizationMin = (uint16_t)val; NVSManager::saveSettings(g_settings); }
static void savePurgeMin(float val) { g_settings.rectParams.purgeMin = (uint16_t)val; NVSManager::saveSettings(g_settings); }

static void saveDistSpeed(float val) { distUi.speedMlH = val; }
static void saveDistHeads(float val) { distUi.headsVolumeMl = val; }
static void saveDistTarget(float val) { distUi.targetVolumeMl = val; }
static void saveDistEndTemp(float val) { distUi.endTempC = val; }

static void savePumpCal(float val) { g_settings.pumpCal.mlPerRevolution = val; NVSManager::saveSettings(g_settings); Pump::setCalibration(val); }
static void saveManualHeater(float val) { Heater::setPower((uint8_t)val); }
static void saveManualPump(float val) { if (val <= 0) Pump::stop(); else Pump::start(val); }

static bool handleNavigationTap(int16_t tx, int16_t ty, const SystemState& state) {
    if (ui.modeSwitchConfirm) {
        return false;
    }

    // РљРЅРѕРїРєР° РќРђР—РђР” (С‚РµРїРµСЂСЊ РІ РІРµСЂС…РЅРµРј РїСЂР°РІРѕРј СѓРіР»Сѓ РЅР° РїРѕРґ-СЌРєСЂР°РЅР°С…)
    bool isRoot = (isMonitorRootScreen(ui.currentScreen) || ui.currentScreen == UI_CONTROL ||
                   ui.currentScreen == UI_SETTINGS || ui.currentScreen == UI_SERVICE);
    
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

static bool handleScreenTap(int16_t tx, int16_t ty, const SystemState& state) {
    switch (ui.currentScreen) {
        case UI_DASHBOARD:
            // Tap on dashboard goes to Control
            if (ty > UI_HEADER_H && ty < (TFT_HEIGHT - UI_FOOTER_H)) {
                switchRoot(UI_CONTROL);
                return true;
            }
            break;
        case UI_MODE_MONITOR:
            // Tap on active mode monitor also opens Control for quick actions.
            if (ty > UI_HEADER_H && ty < (TFT_HEIGHT - UI_FOOTER_H)) {
                switchRoot(UI_CONTROL);
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
                if (state.mode == Mode::IDLE || state.mode == Mode::MANUAL_RECT) {
                    pushScreen(UI_MANUAL);
                }
                return true;
            } else if (hit(tx, ty, CTRL_X1, CTRL_Y4, CTRL_BW, CTRL_BH)) {
                if (state.mode != Mode::IDLE) {
                    if (state.paused) FSM::resume(g_state);
                    else FSM::pause(g_state);
                }
                return true;
            } else if (hit(tx, ty, CTRL_X2, CTRL_Y4, CTRL_BW, CTRL_BH)) {
                if (state.mode != Mode::IDLE) {
                    FSM::stopMode(g_state);
                }
                return true;
            }
            break;

        case UI_VALUE_EDIT:
            if (hit(tx, ty, 10, 175, 100, 70)) {
                edit.value -= edit.fastStep;
                if (edit.value < edit.min) edit.value = edit.min;
                return true;
            } else if (hit(tx, ty, 125, 175, 100, 70)) {
                edit.value -= edit.step;
                if (edit.value < edit.min) edit.value = edit.min;
                return true;
            } else if (hit(tx, ty, 255, 175, 100, 70)) {
                edit.value += edit.step;
                if (edit.value > edit.max) edit.value = edit.max;
                return true;
            } else if (hit(tx, ty, 370, 175, 100, 70)) {
                edit.value += edit.fastStep;
                if (edit.value > edit.max) edit.value = edit.max;
                return true;
            } else if (hit(tx, ty, 10, 255, TFT_WIDTH - 20, 55)) {
                if (edit.onSave) edit.onSave(edit.value);
                popScreen();
                return true;
            }
            break;

        case UI_SETTINGS:
            if (hit(tx, ty, 10, 65, 225, 50)) {
                pushScreen(UI_EQUIPMENT);
                return true;
            } else if (hit(tx, ty, 245, 65, 225, 50)) {
                pushScreen(UI_RECT_PARAMS);
                return true;
            } else if (hit(tx, ty, 10, 125, 225, 50)) {
                pushScreen(UI_DIST_PARAMS);
                return true;
            } else if (hit(tx, ty, 245, 125, 225, 50)) {
                pushScreen(UI_CALIBRATION);
                return true;
            } else if (hit(tx, ty, 10, 185, 145, 50)) {
                g_settings.theme = (g_settings.theme == 0) ? 1 : 0;
                NVSManager::saveSettings(g_settings);
                return true;
            } else if (hit(tx, ty, 165, 185, 145, 50)) {
                g_settings.soundEnabled = !g_settings.soundEnabled;
                NVSManager::saveSettings(g_settings);
                return true;
            } else if (hit(tx, ty, 320, 185, 145, 50)) {
                g_settings.language = (g_settings.language == 0) ? 1 : 0;
                NVSManager::saveSettings(g_settings);
                return true;
            }
            break;
            
        case UI_EQUIPMENT:
            if (ty >= 65 && ty < 245) {
                if (ty >= 65 && ty < 110) {
                    openValueEdit(msg(Msg::HEATER_POWER), g_settings.equipment.heaterPowerW, 1000, 10000, 100, 500, saveHeaterPower, "W", 0);
                    return true;
                } else if (ty >= 110 && ty < 155) {
                    openValueEdit(msg(Msg::COLUMN_HEIGHT), g_settings.equipment.columnHeightMm, 500, 3000, 50, 200, saveColumnHeight, "mm", 0);
                    return true;
                } else if (ty >= 155 && ty < 200) {
                    openValueEdit(msg(Msg::CUBE_VOLUME), g_settings.equipment.cubeVolumeL, 5, 200, 1, 10, saveCubeVolume, "L", 1);
                    return true;
                } else if (ty >= 200 && ty < 245) {
                    openValueEdit(msg(Msg::PACKING_COEFF), g_settings.equipment.packingCoeff, 1, 15, 0.1, 1, savePackingCoeff, "", 2);
                    return true;
                }
            }
            break;
            
        case UI_RECT_PARAMS:
            if (ty >= 65 && ty < 265) {
                if (ty >= 65 && ty < 105) {
                    openValueEdit(msg(Msg::HEADS_PERCENT), g_settings.rectParams.headsPercent, 0, 20, 0.5, 2, saveHeadsPercent, "%", 1);
                    return true;
                } else if (ty >= 105 && ty < 145) {
                    openValueEdit(msg(Msg::HEADS_SPEED), g_settings.rectParams.headsSpeedMlHKw, 10, 1000, 10, 100, saveHeadsSpeed, "ml/h/k", 0);
                    return true;
                } else if (ty >= 145 && ty < 185) {
                    openValueEdit(msg(Msg::BODY_SPEED), g_settings.rectParams.bodySpeedMlHKw, 50, 3000, 50, 200, saveBodySpeed, "ml/h/k", 0);
                    return true;
                } else if (ty >= 185 && ty < 225) {
                    openValueEdit(msg(Msg::STABILIZATION), g_settings.rectParams.stabilizationMin, 1, 120, 1, 10, saveStabMin, "min", 0);
                    return true;
                } else if (ty >= 225 && ty < 265) {
                    openValueEdit(msg(Msg::PURGE_TIME), g_settings.rectParams.purgeMin, 1, 60, 1, 5, savePurgeMin, "min", 0);
                    return true;
                }
            }
            break;
            
        case UI_DIST_PARAMS:
            if (ty >= 65 && ty < 245) {
                if (ty >= 65 && ty < 110) {
                    openValueEdit(msg(Msg::DIST_SPEED), distUi.speedMlH, 50, 5000, 50, 500, saveDistSpeed, "ml/h", 0);
                    return true;
                } else if (ty >= 110 && ty < 155) {
                    openValueEdit(msg(Msg::HEADS_VOLUME), distUi.headsVolumeMl, 0, 5000, 10, 100, saveDistHeads, "ml", 0);
                    return true;
                } else if (ty >= 155 && ty < 200) {
                    openValueEdit(msg(Msg::TARGET_VOLUME), distUi.targetVolumeMl, 0, 50000, 100, 1000, saveDistTarget, "ml", 0);
                    return true;
                } else if (ty >= 200 && ty < 245) {
                    openValueEdit(msg(Msg::END_TEMP), distUi.endTempC, 80, 100, 0.1, 1, saveDistEndTemp, "C", 1);
                    return true;
                }
            }
            break;
            
        case UI_CALIBRATION:
            if (tx > 240 && ty >= 65 && ty < 120) {
                openValueEdit(msg(Msg::PUMP_CALIBRATION), g_settings.pumpCal.mlPerRevolution, 0.001, 5.0, 0.001, 0.05, savePumpCal, "ml/r", 3);
                return true;
            } else if (hit(tx, ty, 20, 150, 440, 55)) {
                Display::startTouchCalibration();
                return true;
            }
            break;
        case UI_MANUAL:
            if (ty >= 65 && ty < 185) {
                if (ty >= 65 && ty < 125) {
                    openValueEdit(msg(Msg::HEATER_POWER), Heater::getPower(), 0, 100, 1, 10, saveManualHeater, "%", 0);
                    return true;
                } else if (ty >= 125 && ty < 185) {
                    openValueEdit(msg(Msg::PUMP), state.pump.speedMlPerHour, 0, 5000, 10, 100, saveManualPump, "ml/h", 0);
                    return true;
                }
            }
            if (ty >= 185 && ty < 240) {
                if (hit(tx, ty, 10, 185, 145, 55)) {
                    Valves::setWater(!Valves::getWater());
                    return true;
                } else if (hit(tx, ty, 167, 185, 145, 55)) {
                    Valves::setHeads(!Valves::getHeads());
                    return true;
                } else if (hit(tx, ty, 325, 185, 145, 55)) {
                    Valves::setUno(!Valves::getUno());
                    return true;
                }
            }
            break;
        default:
            break;
    }

    return false;
}

static uint16_t colorBg() {
    return (g_settings.theme == 1) ? TFT_BLACK : tft.color565(248, 249, 250);
}

static uint16_t colorFg() {
    return (g_settings.theme == 1) ? TFT_WHITE : COLOR_DARK_GREY;
}

static uint16_t colorAccent() {
    return COLOR_PRIMARY;
}

static uint16_t colorCard() {
    return (g_settings.theme == 1) ? tft.color565(33, 37, 41) : TFT_WHITE;
}

static void clearRow(int16_t y, int16_t h = 24) {
    tft.fillRect(10, y - 2, TFT_WIDTH - 20, h + 4, colorBg());
}

static void drawHeader(const char* title, bool showBack) {
    if (!showBack) return; // РЈР±РёСЂР°РµРј РґСѓР±Р»РёСЂСѓСЋС‰РёР№ С‚СѓР»Р±Р°СЂ РЅР° РіР»Р°РІРЅС‹С… СЌРєСЂР°РЅР°С…

    // РћС‚СЂРёСЃРѕРІС‹РІР°РµРј С‚РѕР»СЊРєРѕ РЅР° РїРѕРґ-СЌРєСЂР°РЅР°С…
    tft.fillRect(0, 0, TFT_WIDTH, UI_HEADER_H, colorCard());
    tft.drawFastHLine(0, UI_HEADER_H - 1, TFT_WIDTH, tft.color565(200, 200, 200));
    
    tft.setTextColor(colorFg());
    tft.setTextSize(1); 
    tft.setTextDatum(middle_left);
    tft.drawString(title, 15, UI_HEADER_H / 2);

    int16_t bw = 110;
    int16_t bh = UI_HEADER_H - 10;
    tft.fillRoundRect(TFT_WIDTH - bw - 5, (UI_HEADER_H - bh) / 2, bw, bh, 8, COLOR_PRIMARY);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(middle_center);
    tft.drawString(msg(Msg::BTN_BACK), TFT_WIDTH - bw / 2 - 5, UI_HEADER_H / 2);
    
    tft.setTextDatum(top_left);
}

static void drawTabs(UiScreen current) {
    const bool ru = (g_settings.language == 0);
    const char* labels[4] = {
        ru ? "МОНИТОР" : "DASH",
        ru ? "УПРАВЛ" : "CTRL",
        ru ? "НАСТРОЙ" : "SET",
        ru ? "СЕРВИС" : "INFO"
    };

    const int16_t navY = TFT_HEIGHT - UI_FOOTER_H;
    const int16_t gap = 8;
    const int16_t bw = (TFT_WIDTH - (gap * 5)) / 4;
    const int16_t bh = UI_FOOTER_H - 14;
    const uint16_t navBg = (g_settings.theme == 1) ? tft.color565(20, 22, 24) : tft.color565(236, 240, 244);
    const uint16_t navInactive = (g_settings.theme == 1) ? tft.color565(52, 58, 64) : tft.color565(222, 228, 234);

    tft.fillRect(0, navY, TFT_WIDTH, UI_FOOTER_H, navBg);
    tft.drawFastHLine(0, navY, TFT_WIDTH, tft.color565(160, 170, 180));

    for (int i = 0; i < 4; i++) {
        const int16_t x = gap + i * (bw + gap);
        const int16_t y = navY + 7;
        bool active = false;
        if (i == 0) active = isMonitorRootScreen(current);
        else if (i == 1) active = (current == UI_CONTROL);
        else if (i == 2) active = (current == UI_SETTINGS);
        else active = (current == UI_SERVICE);
        const uint16_t bg = active ? COLOR_PRIMARY : navInactive;
        const uint16_t border = active ? tft.color565(240, 245, 250) : tft.color565(145, 155, 165);
        const uint16_t fg = active ? TFT_WHITE : colorFg();

        tft.fillRoundRect(x, y, bw, bh, 12, bg);
        tft.drawRoundRect(x, y, bw, bh, 12, border);
        if (active) {
            tft.fillRoundRect(x + 16, y + 4, bw - 32, 3, 2, tft.color565(210, 236, 255));
        }

        tft.setTextColor(fg);
        tft.setTextSize(1);
        tft.setFont(&fonts::efontJA_16);
        tft.setTextDatum(middle_center);
        tft.drawString(labels[i], x + bw / 2, y + bh / 2 + 1);

        // Quick status dots to improve at-a-glance readability.
        if (i == 1 && g_state.mode != Mode::IDLE) {
            const uint16_t dot = g_state.paused ? COLOR_WARNING : COLOR_SUCCESS;
            tft.fillCircle(x + bw - 10, y + 10, 4, dot);
        }
        if (i == 3 && !g_state.safetyOk) {
            tft.fillCircle(x + bw - 10, y + 10, 4, COLOR_DANGER);
        }
    }

    tft.setFont(&fonts::efontJA_16);
    tft.setTextDatum(top_left);
}

static void drawValueRow(int16_t y, const char* label, const char* value, bool highlighted = true) {
    tft.setTextColor(colorFg());
    tft.setTextSize(1);
    tft.setCursor(20, y);
    tft.print(label);
    
    uint16_t tw = tft.textWidth(value);
    int16_t boxW = (tw < 80) ? 80 : tw + 20;
    int16_t boxX = TFT_WIDTH - boxW - 15;
    
    if (highlighted) {
        // Р”РµР»Р°РµРј Р·РЅР°С‡РµРЅРёРµ РїРѕС…РѕР¶РёРј РЅР° РєРЅРѕРїРєСѓ
        tft.fillRoundRect(boxX, y - 7, boxW, 32, 8, COLOR_PRIMARY);
        tft.drawRoundRect(boxX, y - 7, boxW, 32, 8, tft.color565(255, 255, 255));
        tft.setTextColor(TFT_WHITE);
    } else {
        tft.fillRoundRect(boxX, y - 7, boxW, 32, 8, colorCard());
        tft.drawRoundRect(boxX, y - 7, boxW, 32, 8, tft.color565(200, 200, 200));
        tft.setTextColor(COLOR_PRIMARY);
    }
    
    tft.setTextDatum(middle_center);
    tft.drawString(value, boxX + boxW / 2, y + 9);
    tft.setTextDatum(top_left);
    tft.setTextColor(colorFg());
}

static void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, uint16_t bg, uint16_t fg) {
    tft.fillRoundRect(x, y, w, h, 10, bg);
    // Darker border
    tft.drawRoundRect(x, y, w, h, 10, tft.color565(100, 100, 100));
    
    tft.setTextColor(fg);
    tft.setTextSize(2);
    tft.setTextDatum(middle_center);
    tft.drawString(label, x + w / 2, y + h / 2);
    tft.setTextDatum(top_left);
    tft.setTextSize(1);
}

static void drawCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t bg) {
    tft.fillRoundRect(x, y, w, h, 12, bg);
    tft.drawRoundRect(x, y, w, h, 12, tft.color565(200, 200, 200));
}

static void drawValueTileShell(int16_t x, int16_t y, int16_t w, int16_t h, const char* label) {
    drawCard(x, y, w, h, colorCard());
    
    tft.setTextColor(tft.color565(96, 104, 116));
    tft.setTextSize(1);
    tft.setTextDatum(top_left);
    tft.drawString(label, x + 10, y + 6);
    tft.setTextDatum(top_left);
}

static void drawValueTileValue(int16_t x, int16_t y, int16_t w, int16_t h, const char* value, const char* unit, uint16_t color) {
    // Clear only value area to avoid visible full-tile flicker on periodic updates.
    const int16_t valueAreaY = y + 20;
    const int16_t valueAreaH = h - 22;
    if (valueAreaH > 0) {
        tft.fillRoundRect(x + 2, valueAreaY, w - 4, valueAreaH, 8, colorCard());
    }

    uint8_t valueSize = (w >= 135 && h >= 64) ? 3 : 2;
    tft.setTextSize(valueSize);
    if (tft.textWidth(value) > (w - 18) && valueSize > 1) {
        valueSize--;
    }

    tft.setTextColor(color);
    tft.setTextSize(valueSize);
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
    
    tft.setTextDatum(top_left);
}

static void drawValueTile(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, const char* value, const char* unit, uint16_t color) {
    drawValueTileShell(x, y, w, h, label);
    drawValueTileValue(x, y, w, h, value, unit, color);
}

static void formatUptimeCompact(uint32_t uptimeSec, char* out, size_t outSize) {
    const uint32_t h = uptimeSec / 3600UL;
    const uint32_t m = (uptimeSec % 3600UL) / 60UL;
    const uint32_t s = uptimeSec % 60UL;
    snprintf(out, outSize, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
}

static void formatDurationCompact(uint32_t sec, char* out, size_t outSize) {
    const uint32_t h = sec / 3600UL;
    const uint32_t m = (sec % 3600UL) / 60UL;
    const uint32_t s = sec % 60UL;
    if (h > 0) {
        snprintf(out, outSize, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    } else {
        snprintf(out, outSize, "%02lu:%02lu", (unsigned long)m, (unsigned long)s);
    }
}

static void renderDashboard(const SystemState& state, bool full) {
    const bool ru = (g_settings.language == 0);
    const int16_t barY = 8;
    const int16_t statusX = 20;
    const int16_t statusY = barY + 5;
    const int16_t statusW = 300;
    const int16_t statusH = 34;
    const int16_t badgeX = 332;
    const int16_t badgeW = 128;
    const int16_t badgeH = 14;

    const int16_t tileGap = 8;
    const int16_t tileW = (TFT_WIDTH - 20 - tileGap * 2) / 3; // 3 columns, 10px outer margins
    const int16_t tileH = 70;
    const int16_t row1Y = 56;
    const int16_t row2Y = row1Y + tileH + 6;
    const int16_t x1 = 10;
    const int16_t x2 = x1 + tileW + tileGap;
    const int16_t x3 = x2 + tileW + tileGap;
    const int16_t infoY = row2Y + tileH + 8;
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

    const uint8_t layoutKey = static_cast<uint8_t>((static_cast<uint8_t>(profile) << 2) |
                                                   (hasWaterIn ? 0x01 : 0x00) |
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
             FSM::getModeName(state.mode), 
             FSM::getPhaseName(state.rectPhase));

    const char* procState = (state.mode == Mode::IDLE)
                                ? (ru ? "ОЖИД." : "IDLE")
                                : (state.paused ? (ru ? "ПАУЗА" : "PAUSE")
                                                : (ru ? "РАБОТА" : "RUN"));
    const uint16_t procColor = (state.mode == Mode::IDLE) ? COLOR_INFO : (state.paused ? COLOR_WARNING : COLOR_SUCCESS);
    const char* safetyState = state.safetyOk ? (ru ? "БЕЗОП." : "SAFE") : (ru ? "ТРЕВОГА" : "ALARM");
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
        snprintf(timerBuf, sizeof(timerBuf), "%s %s/%s", ru ? "Фаза" : "Phase", elapsedBuf, targetBuf);
    } else {
        snprintf(timerBuf, sizeof(timerBuf), "%s %s", ru ? "Фаза" : "Phase", elapsedBuf);
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
        tft.fillRoundRect(pbX, pbY, pbW, pbH, 3, tft.color565(210, 216, 224));
        if (phaseProgress > 0) {
            const int16_t fillW = (pbW * phaseProgress) / 100;
            tft.fillRoundRect(pbX, pbY, fillW, pbH, 3, COLOR_PRIMARY);
        }
        tft.setFont(&fonts::efontJA_16);
        tft.setTextDatum(top_left);
        strncpy(g_dashboardCache.status, statusBuf, sizeof(g_dashboardCache.status));
        g_dashboardCache.status[sizeof(g_dashboardCache.status) - 1] = '\0';
        strncpy(g_dashboardCache.phaseTimer, timerBuf, sizeof(g_dashboardCache.phaseTimer));
        g_dashboardCache.phaseTimer[sizeof(g_dashboardCache.phaseTimer) - 1] = '\0';
        g_dashboardCache.phaseProgress = phaseProgress;
    }

    if (full || strcmp(g_dashboardCache.processState, procState) != 0 ||
        strcmp(g_dashboardCache.safetyState, safetyState) != 0) {
        if (!full) {
            tft.fillRect(badgeX - 4, barY + 4, badgeW + 8, 34, colorCard());
        }

        tft.fillRoundRect(badgeX, barY + 6, badgeW, badgeH, 7, procColor);
        tft.drawRoundRect(badgeX, barY + 6, badgeW, badgeH, 7, tft.color565(220, 230, 240));
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        tft.setTextDatum(middle_center);
        tft.drawString(procState, badgeX + badgeW / 2, barY + 6 + badgeH / 2);

        tft.fillRoundRect(badgeX, barY + 24, badgeW, badgeH, 7, safetyColor);
        tft.drawRoundRect(badgeX, barY + 24, badgeW, badgeH, 7, tft.color565(220, 230, 240));
        tft.setTextColor(TFT_WHITE);
        tft.setTextDatum(middle_center);
        tft.drawString(safetyState, badgeX + badgeW / 2, barY + 24 + badgeH / 2);

        strncpy(g_dashboardCache.processState, procState, sizeof(g_dashboardCache.processState));
        g_dashboardCache.processState[sizeof(g_dashboardCache.processState) - 1] = '\0';
        strncpy(g_dashboardCache.safetyState, safetyState, sizeof(g_dashboardCache.safetyState));
        g_dashboardCache.safetyState[sizeof(g_dashboardCache.safetyState) - 1] = '\0';
    }

    const bool layoutChanged = full || (g_dashboardCache.layoutKey != layoutKey);
    const int16_t tileX[6] = {x1, x2, x3, x1, x2, x3};
    const int16_t tileY[6] = {row1Y, row1Y, row1Y, row2Y, row2Y, row2Y};
    const char* tileLabels[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    const char* tileUnits[6] = {"В°C", "В°C", "В°C", "В°C", msg(Msg::UNIT_W), msg(Msg::UNIT_ML_H)};
    uint16_t tileColors[6] = {COLOR_DANGER, colorAccent(), COLOR_INFO, COLOR_WARNING, COLOR_WARNING, COLOR_SUCCESS};
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
        tileLabels[4] = hasWaterIn ? (ru ? "ОХЛ ВХ" : "WATER IN") : (ru ? "СЕТЬ" : "MAINS");
        tileLabels[5] = hasWaterOut ? (ru ? "ОХЛ ВЫХ" : "WATER OUT") : (ru ? "МОЩНОСТЬ" : "POWER");
        if (hasWaterIn) {
            snprintf(tileValues[4], sizeof(tileValues[4]), "%.1f", state.temps.waterIn);
            tileUnits[4] = "В°C";
            tileColors[4] = COLOR_INFO;
        } else {
            snprintf(tileValues[4], sizeof(tileValues[4]), "%.0f", state.power.voltage);
            tileUnits[4] = "V";
            tileColors[4] = COLOR_PRIMARY;
        }
        if (hasWaterOut) {
            snprintf(tileValues[5], sizeof(tileValues[5]), "%.1f", state.temps.waterOut);
            tileUnits[5] = "В°C";
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
            snprintf(tileValues[5], sizeof(tileValues[5]), "%.1f", state.temps.waterOut);
            tileUnits[5] = "В°C";
            tileColors[5] = COLOR_INFO;
        } else {
            tileLabels[5] = ru ? "ОТБОР" : "TAKEOFF";
            snprintf(tileValues[5], sizeof(tileValues[5]), "%.0f", state.pump.speedMlPerHour);
            tileUnits[5] = msg(Msg::UNIT_ML_H);
            tileColors[5] = COLOR_SUCCESS;
        }
    } else {
        tileLabels[4] = msg(Msg::HEATER_POWER);
        tileLabels[5] = hasWaterOut ? (ru ? "ОХЛ ВЫХ" : "WATER OUT") : msg(Msg::PUMP);
        snprintf(tileValues[4], sizeof(tileValues[4]), "%.0f", state.power.power);
        if (hasWaterOut) {
            snprintf(tileValues[5], sizeof(tileValues[5]), "%.1f", state.temps.waterOut);
            tileUnits[5] = "В°C";
            tileColors[5] = COLOR_INFO;
        } else {
            snprintf(tileValues[5], sizeof(tileValues[5]), "%.0f", state.pump.speedMlPerHour);
            tileUnits[5] = msg(Msg::UNIT_ML_H);
            tileColors[5] = COLOR_SUCCESS;
        }
    }

    if (layoutChanged) {
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

    char* tileCache[6] = {
        g_dashboardCache.cube,
        g_dashboardCache.top,
        g_dashboardCache.reflux,
        g_dashboardCache.tsa,
        g_dashboardCache.power,
        g_dashboardCache.pump
    };

    for (uint8_t i = 0; i < 6; i++) {
        if (full || strcmp(tileCache[i], tileValues[i]) != 0) {
            drawValueTileValue(tileX[i], tileY[i], tileW, tileH, tileValues[i], tileUnits[i], tileColors[i]);
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
                 state.stats.headsVolume,
                 state.stats.bodyVolume,
                 state.stats.tailsVolume);
    } else {
        snprintf(infoBuf, sizeof(infoBuf), "Heads %.0f | Body %.0f | Tails %.0f ml",
                 state.stats.headsVolume,
                 state.stats.bodyVolume,
                 state.stats.tailsVolume);
    }

    const char* k1 = Valves::getWater() ? "ON" : "--";
    const char* k2 = Valves::getHeads() ? "ON" : "--";
    const char* k3 = (Heater::getPower() > 0) ? "ON" : "--";
    char waterBuf[24];
    if (state.temps.valid[TEMP_WATER_IN] && state.temps.valid[TEMP_WATER_OUT]) {
        snprintf(waterBuf, sizeof(waterBuf), "%.1f/%.1f", state.temps.waterIn, state.temps.waterOut);
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
        snprintf(ioBuf, sizeof(ioBuf), "W %s | V %.0f | P %.0f", waterBuf, state.power.voltage, state.pressure.cube);
    } else {
        snprintf(ioBuf, sizeof(ioBuf), "W %s | V %.0f | P %.0f | K1%s K2%s K3%s",
                 waterBuf,
                 state.power.voltage,
                 state.pressure.cube,
                 k1, k2, k3);
    }

    char upBuf[16];
    formatUptimeCompact(state.uptime, upBuf, sizeof(upBuf));

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

        strncpy(g_dashboardCache.infoLine, infoBuf, sizeof(g_dashboardCache.infoLine));
        g_dashboardCache.infoLine[sizeof(g_dashboardCache.infoLine) - 1] = '\0';
        strncpy(g_dashboardCache.ioLine, ioBuf, sizeof(g_dashboardCache.ioLine));
        g_dashboardCache.ioLine[sizeof(g_dashboardCache.ioLine) - 1] = '\0';
        strncpy(g_dashboardCache.uptime, upBuf, sizeof(g_dashboardCache.uptime));
        g_dashboardCache.uptime[sizeof(g_dashboardCache.uptime) - 1] = '\0';
    }

    tft.setFont(&fonts::efontJA_16);
    tft.setTextDatum(top_left);
}

static void renderModeMonitor(const SystemState& state, bool full) {
    const bool ru = (g_settings.language == 0);
    const int16_t barY = 8;
    const int16_t statusX = 20;
    const int16_t statusY = barY + 5;
    const int16_t statusW = 300;
    const int16_t statusH = 34;
    const int16_t badgeX = 332;
    const int16_t badgeW = 128;
    const int16_t badgeH = 14;

    const int16_t panelY = 56;
    const int16_t panelH = 156;
    const int16_t leftX = 10;
    const int16_t leftW = 218;
    const int16_t rightX = 236;
    const int16_t rightW = TFT_WIDTH - rightX - 10;
    const int16_t colGap = 6;
    const int16_t rowGap = 6;
    const int16_t tileW = (rightW - colGap) / 2;
    const int16_t tileH = (panelH - rowGap * 2) / 3;
    const int16_t infoY = panelY + panelH + 8;

    if (full) {
        tft.fillScreen(colorBg());
        drawHeader(msg(Msg::MONITOR), false);
        drawTabs(UI_MODE_MONITOR);
        drawCard(10, barY, TFT_WIDTH - 20, 44, colorCard());
        drawCard(leftX, panelY, leftW, panelH, colorCard());

        drawValueTileShell(rightX, panelY, tileW, tileH, msg(Msg::CUBE_TEMP));
        drawValueTileShell(rightX + tileW + colGap, panelY, tileW, tileH, msg(Msg::TOP_T));
        drawValueTileShell(rightX, panelY + tileH + rowGap, tileW, tileH, msg(Msg::REFLUX_T));
        drawValueTileShell(rightX + tileW + colGap, panelY + tileH + rowGap, tileW, tileH, msg(Msg::TSA_T));
        drawValueTileShell(rightX, panelY + (tileH + rowGap) * 2, tileW, tileH, msg(Msg::HEATER_POWER));
        drawValueTileShell(rightX + tileW + colGap, panelY + (tileH + rowGap) * 2, tileW, tileH,
                           state.temps.valid[TEMP_WATER_OUT] ? (ru ? "ОХЛ ВЫХ" : "WATER OUT") : msg(Msg::PUMP));

        drawCard(10, infoY, TFT_WIDTH - 20, 40, colorCard());
        memset(&g_dashboardCache, 0, sizeof(g_dashboardCache));
        g_dashboardCache.layoutKey = 0xEE;
    }

    char statusBuf[64];
    snprintf(statusBuf, sizeof(statusBuf), "%s / %s",
             FSM::getModeName(state.mode),
             FSM::getPhaseName(state.rectPhase));

    const char* procState = state.paused ? (ru ? "ПАУЗА" : "PAUSE") : (ru ? "РАБОТА" : "RUN");
    const uint16_t procColor = state.paused ? COLOR_WARNING : COLOR_SUCCESS;
    const char* safetyState = state.safetyOk ? (ru ? "БЕЗОП." : "SAFE") : (ru ? "ТРЕВОГА" : "ALARM");
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
        snprintf(timerBuf, sizeof(timerBuf), "%s %s/%s", ru ? "Фаза" : "Phase", elapsedBuf, targetBuf);
    } else {
        snprintf(timerBuf, sizeof(timerBuf), "%s %s", ru ? "Фаза" : "Phase", elapsedBuf);
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
        tft.fillRoundRect(pbX, pbY, pbW, pbH, 3, tft.color565(210, 216, 224));
        if (phaseProgress > 0) {
            const int16_t fillW = (pbW * phaseProgress) / 100;
            tft.fillRoundRect(pbX, pbY, fillW, pbH, 3, COLOR_PRIMARY);
        }

        strncpy(g_dashboardCache.status, statusBuf, sizeof(g_dashboardCache.status));
        g_dashboardCache.status[sizeof(g_dashboardCache.status) - 1] = '\0';
        strncpy(g_dashboardCache.phaseTimer, timerBuf, sizeof(g_dashboardCache.phaseTimer));
        g_dashboardCache.phaseTimer[sizeof(g_dashboardCache.phaseTimer) - 1] = '\0';
        g_dashboardCache.phaseProgress = phaseProgress;
    }

    if (full || strcmp(g_dashboardCache.processState, procState) != 0 ||
        strcmp(g_dashboardCache.safetyState, safetyState) != 0) {
        if (!full) {
            tft.fillRect(badgeX - 4, barY + 4, badgeW + 8, 34, colorCard());
        }
        tft.fillRoundRect(badgeX, barY + 6, badgeW, badgeH, 7, procColor);
        tft.drawRoundRect(badgeX, barY + 6, badgeW, badgeH, 7, tft.color565(220, 230, 240));
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        tft.setTextDatum(middle_center);
        tft.drawString(procState, badgeX + badgeW / 2, barY + 6 + badgeH / 2);

        tft.fillRoundRect(badgeX, barY + 24, badgeW, badgeH, 7, safetyColor);
        tft.drawRoundRect(badgeX, barY + 24, badgeW, badgeH, 7, tft.color565(220, 230, 240));
        tft.drawString(safetyState, badgeX + badgeW / 2, barY + 24 + badgeH / 2);

        strncpy(g_dashboardCache.processState, procState, sizeof(g_dashboardCache.processState));
        g_dashboardCache.processState[sizeof(g_dashboardCache.processState) - 1] = '\0';
        strncpy(g_dashboardCache.safetyState, safetyState, sizeof(g_dashboardCache.safetyState));
        g_dashboardCache.safetyState[sizeof(g_dashboardCache.safetyState) - 1] = '\0';
    }

    char summaryBuf[96];
    if (ru) {
        snprintf(summaryBuf, sizeof(summaryBuf), "ОКНО РЕЖИМА\nГол %.0f  Тело %.0f  Хв %.0f",
                 state.stats.headsVolume, state.stats.bodyVolume, state.stats.tailsVolume);
    } else {
        snprintf(summaryBuf, sizeof(summaryBuf), "MODE WINDOW\nH %.0f  B %.0f  T %.0f",
                 state.stats.headsVolume, state.stats.bodyVolume, state.stats.tailsVolume);
    }

    char val[16];
    char topLine[32];
    snprintf(topLine, sizeof(topLine), "V %.0f  P %.0f", state.power.voltage, state.pressure.cube);
    if (full || strcmp(g_dashboardCache.infoLine, summaryBuf) != 0 || strcmp(g_dashboardCache.ioLine, topLine) != 0) {
        if (!full) {
            tft.fillRect(leftX + 6, panelY + 8, leftW - 12, panelH - 16, colorCard());
        }
        tft.setTextColor(colorFg());
        tft.setTextSize(1);
        tft.setTextDatum(top_left);
        tft.drawString(ru ? "РЕЖИМНОЕ ОКНО" : "MODE WINDOW", leftX + 10, panelY + 10);
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

        const char* k1 = Valves::getWater() ? "ON" : "--";
        const char* k2 = Valves::getHeads() ? "ON" : "--";
        const char* k3 = (Heater::getPower() > 0) ? "ON" : "--";
        snprintf(val, sizeof(val), "K1%s K2%s K3%s", k1, k2, k3);
        tft.setTextColor(tft.color565(120, 130, 140));
        tft.drawString(val, leftX + 10, panelY + 120);

        strncpy(g_dashboardCache.infoLine, summaryBuf, sizeof(g_dashboardCache.infoLine));
        g_dashboardCache.infoLine[sizeof(g_dashboardCache.infoLine) - 1] = '\0';
        strncpy(g_dashboardCache.ioLine, topLine, sizeof(g_dashboardCache.ioLine));
        g_dashboardCache.ioLine[sizeof(g_dashboardCache.ioLine) - 1] = '\0';
    }

    snprintf(val, sizeof(val), "%.1f", state.temps.cube);
    if (full || strcmp(g_dashboardCache.cube, val) != 0) {
        drawValueTileValue(rightX, panelY, tileW, tileH, val, "В°C", COLOR_DANGER);
        strncpy(g_dashboardCache.cube, val, sizeof(g_dashboardCache.cube));
        g_dashboardCache.cube[sizeof(g_dashboardCache.cube) - 1] = '\0';
    }
    snprintf(val, sizeof(val), "%.1f", state.temps.columnTop);
    if (full || strcmp(g_dashboardCache.top, val) != 0) {
        drawValueTileValue(rightX + tileW + colGap, panelY, tileW, tileH, val, "В°C", colorAccent());
        strncpy(g_dashboardCache.top, val, sizeof(g_dashboardCache.top));
        g_dashboardCache.top[sizeof(g_dashboardCache.top) - 1] = '\0';
    }
    snprintf(val, sizeof(val), "%.1f", state.temps.reflux);
    if (full || strcmp(g_dashboardCache.reflux, val) != 0) {
        drawValueTileValue(rightX, panelY + tileH + rowGap, tileW, tileH, val, "В°C", COLOR_INFO);
        strncpy(g_dashboardCache.reflux, val, sizeof(g_dashboardCache.reflux));
        g_dashboardCache.reflux[sizeof(g_dashboardCache.reflux) - 1] = '\0';
    }
    snprintf(val, sizeof(val), "%.1f", state.temps.tsa);
    if (full || strcmp(g_dashboardCache.tsa, val) != 0) {
        drawValueTileValue(rightX + tileW + colGap, panelY + tileH + rowGap, tileW, tileH, val, "В°C", COLOR_WARNING);
        strncpy(g_dashboardCache.tsa, val, sizeof(g_dashboardCache.tsa));
        g_dashboardCache.tsa[sizeof(g_dashboardCache.tsa) - 1] = '\0';
    }
    snprintf(val, sizeof(val), "%.0f", state.power.power);
    if (full || strcmp(g_dashboardCache.power, val) != 0) {
        drawValueTileValue(rightX, panelY + (tileH + rowGap) * 2, tileW, tileH, val, msg(Msg::UNIT_W), COLOR_WARNING);
        strncpy(g_dashboardCache.power, val, sizeof(g_dashboardCache.power));
        g_dashboardCache.power[sizeof(g_dashboardCache.power) - 1] = '\0';
    }
    if (state.temps.valid[TEMP_WATER_OUT]) {
        snprintf(val, sizeof(val), "%.1f", state.temps.waterOut);
    } else {
        snprintf(val, sizeof(val), "%.0f", state.pump.speedMlPerHour);
    }
    if (full || strcmp(g_dashboardCache.pump, val) != 0) {
        drawValueTileValue(rightX + tileW + colGap, panelY + (tileH + rowGap) * 2, tileW, tileH,
                           val,
                           state.temps.valid[TEMP_WATER_OUT] ? "В°C" : msg(Msg::UNIT_ML_H),
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
        tft.drawString(ru ? "Окно режима активно" : "Mode window active", 20, infoY + 20);
        tft.setTextColor(COLOR_PRIMARY);
        tft.setTextDatum(middle_right);
        tft.drawString(upBuf, TFT_WIDTH - 18, infoY + 20);
        strncpy(g_dashboardCache.uptime, upBuf, sizeof(g_dashboardCache.uptime));
        g_dashboardCache.uptime[sizeof(g_dashboardCache.uptime) - 1] = '\0';
    }

    tft.setFont(&fonts::efontJA_16);
    tft.setTextDatum(top_left);
}

static void renderControl(const SystemState& state, bool full) {
    if (full) {
        tft.fillScreen(colorBg());
        drawHeader(msg(Msg::CONTROL), false);
        drawTabs(UI_CONTROL);
    }

    char modeBuf[64];
    const bool ru = (g_settings.language == 0);
    snprintf(modeBuf, sizeof(modeBuf),
             (state.mode == Mode::IDLE)
                 ? (ru ? "Режим: %s" : "Mode: %s")
                 : (ru ? "Активен: %s" : "Active: %s"),
             FSM::getModeName(state.mode));

    drawCard(10, 8, TFT_WIDTH - 20, 30, colorCard());
    tft.setTextColor((state.mode == Mode::IDLE) ? COLOR_INFO : COLOR_SUCCESS);
    tft.setTextDatum(middle_center);
    tft.setTextSize(1);
    tft.drawString(modeBuf, TFT_WIDTH / 2, 23);
    tft.setTextDatum(top_left);

    drawButton(CTRL_X1, CTRL_Y1, CTRL_BW, CTRL_BH, msg(Msg::AUTO_RECTIFY),
               modeButtonColor(state, Mode::RECTIFICATION, COLOR_SUCCESS), TFT_WHITE);
    drawButton(CTRL_X2, CTRL_Y1, CTRL_BW, CTRL_BH, msg(Msg::DISTILLATION),
               modeButtonColor(state, Mode::DISTILLATION, COLOR_SUCCESS), TFT_WHITE);

    drawButton(CTRL_X1, CTRL_Y2, CTRL_BW, CTRL_BH, msg(Msg::MANUAL_RECT),
               modeButtonColor(state, Mode::MANUAL_RECT, COLOR_PRIMARY), TFT_WHITE);
    drawButton(CTRL_X2, CTRL_Y2, CTRL_BW, CTRL_BH, msg(Msg::MASHING),
               modeButtonColor(state, Mode::MASHING, COLOR_PRIMARY), TFT_WHITE);

    drawButton(CTRL_X1, CTRL_Y3, CTRL_BW, CTRL_BH, msg(Msg::HOLD_MODE),
               modeButtonColor(state, Mode::HOLD, COLOR_PRIMARY), TFT_WHITE);

    const bool manualScreenAllowed = (state.mode == Mode::IDLE || state.mode == Mode::MANUAL_RECT);
    drawButton(CTRL_X2, CTRL_Y3, CTRL_BW, CTRL_BH, msg(Msg::MANUAL_PUMP),
               manualScreenAllowed ? COLOR_INFO : dimmedButtonColor(), TFT_WHITE);

    drawButton(CTRL_X1, CTRL_Y4, CTRL_BW, CTRL_BH, state.paused ? msg(Msg::RESUME) : msg(Msg::PAUSE),
               (state.mode == Mode::IDLE) ? dimmedButtonColor() : COLOR_WARNING, TFT_WHITE);
    drawButton(CTRL_X2, CTRL_Y4, CTRL_BW, CTRL_BH, msg(Msg::STOP),
               (state.mode == Mode::IDLE) ? dimmedButtonColor() : COLOR_DANGER, TFT_WHITE);

    if (ui.modeSwitchConfirm) {
        const int16_t mx = 30;
        const int16_t my = 78;
        const int16_t mw = TFT_WIDTH - 60;
        const int16_t mh = 150;
        const int16_t by = my + 102;
        drawCard(mx, my, mw, mh, colorCard());
        tft.setTextColor(COLOR_WARNING);
        tft.setTextDatum(middle_center);
        tft.setTextSize(2);
        tft.drawString(ru ? "СМЕНА РЕЖИМА" : "SWITCH MODE", TFT_WIDTH / 2, my + 20);
        tft.setTextSize(1);

        char cur[64];
        char next[64];
        snprintf(cur, sizeof(cur), ru ? "Сейчас: %s" : "Current: %s", FSM::getModeName(state.mode));
        snprintf(next, sizeof(next), ru ? "Перейти: %s ?" : "Switch to: %s ?", FSM::getModeName(ui.modeSwitchTarget));
        tft.setTextColor(colorFg());
        tft.drawString(cur, TFT_WIDTH / 2, my + 56);
        tft.drawString(next, TFT_WIDTH / 2, my + 78);

        drawButton(mx + 20, by, 150, 42, ru ? "ОТМЕНА" : "CANCEL", COLOR_DARK_GREY, TFT_WHITE);
        drawButton(mx + mw - 170, by, 150, 42, ru ? "ПЕРЕЙТИ" : "SWITCH", COLOR_DANGER, TFT_WHITE);
        tft.setTextDatum(top_left);
    }
}

static void renderSettings() {
    tft.fillScreen(colorBg());
    drawHeader(msg(Msg::SETTINGS), false);
    drawTabs(UI_SETTINGS);

    int16_t bw = 225;
    int16_t bh = 50;
    int16_t x2 = 245;

    drawButton(10, 65, bw, bh, msg(Msg::EQUIPMENT), COLOR_PRIMARY, TFT_WHITE);
    drawButton(x2, 65, bw, bh, msg(Msg::RECT_PARAMS), COLOR_PRIMARY, TFT_WHITE);
    
    drawButton(10, 125, bw, bh, msg(Msg::DIST_PARAMS), COLOR_PRIMARY, TFT_WHITE);
    drawButton(x2, 125, bw, bh, msg(Msg::CALIBRATION), COLOR_PRIMARY, TFT_WHITE);
    
    int16_t bw3 = 145;
    drawButton(10, 185, bw3, bh, msg(Msg::THEME), COLOR_DARK_GREY, TFT_WHITE);
    drawButton(165, 185, bw3, bh, msg(Msg::SOUND), g_settings.soundEnabled ? COLOR_SUCCESS : COLOR_DANGER, TFT_WHITE);
    drawButton(320, 185, bw3, bh, g_settings.language == 0 ? "RU" : "EN", COLOR_INFO, TFT_WHITE);
}

static void renderEquipment() {
    tft.fillScreen(colorBg());
    drawHeader(msg(Msg::EQUIPMENT), true);
    drawTabs(UI_SETTINGS);

    int16_t y = 65;
    int16_t step = 45;
    char buf[32];

    snprintf(buf, sizeof(buf), "%u %s", g_settings.equipment.heaterPowerW, msg(Msg::UNIT_W));
    drawValueRow(y, msg(Msg::HEATER_POWER), buf); y += step;
    
    snprintf(buf, sizeof(buf), "%u %s", g_settings.equipment.columnHeightMm, msg(Msg::UNIT_MM));
    drawValueRow(y, msg(Msg::COLUMN_HEIGHT), buf); y += step;
    
    snprintf(buf, sizeof(buf), "%.1f %s", g_settings.equipment.cubeVolumeL, msg(Msg::UNIT_L));
    drawValueRow(y, msg(Msg::CUBE_VOLUME), buf); y += step;
    
    snprintf(buf, sizeof(buf), "%.2f", g_settings.equipment.packingCoeff);
    drawValueRow(y, msg(Msg::PACKING_COEFF), buf);
    
    tft.setTextColor(tft.color565(150, 150, 150));
    tft.setTextDatum(bottom_center);
    tft.drawString(msg(Msg::TAP_TO_EDIT), TFT_WIDTH / 2, TFT_HEIGHT - UI_FOOTER_H - 10);
    tft.setTextDatum(top_left);
}

static void renderRectParams() {
    tft.fillScreen(colorBg());
    drawHeader(msg(Msg::RECT_PARAMS), true);
    drawTabs(UI_SETTINGS);

    int16_t y = 65;
    int16_t step = 40;
    char buf[32];

    snprintf(buf, sizeof(buf), "%.1f %%", g_settings.rectParams.headsPercent);
    drawValueRow(y, msg(Msg::HEADS_PERCENT), buf); y += step;
    
    snprintf(buf, sizeof(buf), "%.0f %s", g_settings.rectParams.headsSpeedMlHKw, msg(Msg::UNIT_ML_H_K));
    drawValueRow(y, msg(Msg::HEADS_SPEED), buf); y += step;
    
    snprintf(buf, sizeof(buf), "%.0f %s", g_settings.rectParams.bodySpeedMlHKw, msg(Msg::UNIT_ML_H_K));
    drawValueRow(y, msg(Msg::BODY_SPEED), buf); y += step;
    
    snprintf(buf, sizeof(buf), "%u %s", g_settings.rectParams.stabilizationMin, msg(Msg::UNIT_MIN));
    drawValueRow(y, msg(Msg::STABILIZATION), buf); y += step;
    
    snprintf(buf, sizeof(buf), "%u %s", g_settings.rectParams.purgeMin, msg(Msg::UNIT_MIN));
    drawValueRow(y, msg(Msg::PURGE_TIME), buf);
}

static void renderDistParams() {
    tft.fillScreen(colorBg());
    drawHeader(msg(Msg::DIST_PARAMS), true);
    drawTabs(UI_SETTINGS);

    int16_t y = 65;
    int16_t step = 45;
    char buf[32];

    snprintf(buf, sizeof(buf), "%.0f %s", distUi.speedMlH, msg(Msg::UNIT_ML_H));
    drawValueRow(y, msg(Msg::DIST_SPEED), buf); y += step;
    
    snprintf(buf, sizeof(buf), "%.0f ml", distUi.headsVolumeMl);
    drawValueRow(y, msg(Msg::HEADS_VOLUME), buf); y += step;
    
    snprintf(buf, sizeof(buf), "%.0f ml", distUi.targetVolumeMl);
    drawValueRow(y, msg(Msg::TARGET_VOLUME), buf); y += step;
    
    snprintf(buf, sizeof(buf), "%.1f C", distUi.endTempC);
    drawValueRow(y, msg(Msg::END_TEMP), buf);
}

static void renderCalibration() {
    tft.fillScreen(colorBg());
    drawHeader(msg(Msg::CALIBRATION), true);
    drawTabs(UI_SETTINGS);

    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f %s", g_settings.pumpCal.mlPerRevolution, msg(Msg::UNIT_ML_R));
    drawValueRow(80, msg(Msg::PUMP_CALIBRATION), buf);

    drawButton(20, 160, 440, 55, msg(Msg::TOUCH_CALIBRATION), COLOR_PRIMARY, TFT_WHITE);
}

static void renderManual(const SystemState& state) {
    tft.fillScreen(colorBg());
    drawHeader(msg(Msg::MANUAL_MODE), true);
    drawTabs(UI_CONTROL);

    int16_t y = 65;
    char buf[32];
    snprintf(buf, sizeof(buf), "%u %%", Heater::getPower());
    drawValueRow(y, msg(Msg::HEATER_POWER), buf); y += 60;

    snprintf(buf, sizeof(buf), "%.0f %s", state.pump.speedMlPerHour, msg(Msg::UNIT_ML_H));
    drawValueRow(y, msg(Msg::PUMP), buf); y += 60;

    int16_t bw = 145;
    int16_t bh = 55;
    drawButton(10, y, bw, bh, msg(Msg::VALVE_WATER), Valves::getWater() ? COLOR_SUCCESS : COLOR_DARK_GREY, TFT_WHITE);
    drawButton(165, y, bw, bh, msg(Msg::VALVE_HEADS), Valves::getHeads() ? COLOR_SUCCESS : COLOR_DARK_GREY, TFT_WHITE);
    drawButton(325, y, bw, bh, msg(Msg::VALVE_UNO), Valves::getUno() ? COLOR_SUCCESS : COLOR_DARK_GREY, TFT_WHITE);
}

static void renderValueEdit() {
    tft.fillScreen(colorBg());
    drawHeader(edit.label, true);
    
    // Large value display in a card
    drawCard(20, 70, TFT_WIDTH - 40, 90, colorCard());
    tft.setTextColor(COLOR_PRIMARY);
    tft.setTextSize(4);
    tft.setTextDatum(middle_center);
    char buf[32];
    if (edit.decimals == 0) snprintf(buf, sizeof(buf), "%.0f %s", edit.value, edit.unit);
    else if (edit.decimals == 1) snprintf(buf, sizeof(buf), "%.1f %s", edit.value, edit.unit);
    else snprintf(buf, sizeof(buf), "%.3f %s", edit.value, edit.unit);
    tft.drawString(buf, TFT_WIDTH / 2, 115);
    tft.setTextSize(1);

    // Large +/- buttons
    int16_t bw = 100;
    int16_t bh = 70;
    int16_t y = 175;
    drawButton(10, y, bw, bh, "--", tft.color565(200, 50, 50), TFT_WHITE);
    drawButton(125, y, bw, bh, "-", tft.color565(220, 100, 100), TFT_WHITE);
    drawButton(255, y, bw, bh, "+", tft.color565(100, 200, 100), TFT_WHITE);
    drawButton(370, y, bw, bh, "++", tft.color565(50, 180, 50), TFT_WHITE);
    
    // Save button
    drawButton(10, 255, TFT_WIDTH - 20, 55, msg(Msg::SAVE_AND_CLOSE), COLOR_PRIMARY, TFT_WHITE);
    
    tft.setTextDatum(top_left);
}

static void renderService(const SystemState& state, bool full) {
    if (full) {
        tft.fillScreen(colorBg());
        drawHeader(msg(Msg::SERVICE), false);
        drawTabs(UI_SERVICE);
        char buf[40];
        snprintf(buf, sizeof(buf), "%s", FW_VERSION);
        drawValueRow(30, msg(Msg::VERSION), buf);
    }

    char buf[48];
    clearRow(80, 30);
    snprintf(buf, sizeof(buf), "%lus", state.uptime);
    drawValueRow(80, msg(Msg::UPTIME), buf);
    
    clearRow(130, 30);
    snprintf(buf, sizeof(buf), "%u KB", ESP.getFreeHeap() / 1024);
    drawValueRow(130, msg(Msg::FREE_HEAP), buf);

    const bool ru = (g_settings.language == 0);
    clearRow(180, 30);
    snprintf(buf, sizeof(buf), "%ums (%ums max)", g_displayStats.lastFrameMs,
             g_displayStats.maxFrameMs);
    drawValueRow(180, ru ? "Кадр TFT" : "TFT frame", buf);

    clearRow(230, 30);
    snprintf(buf, sizeof(buf), "S%lu R%lu H%lu G%u",
             (unsigned long)g_displayStats.slowFrames,
             (unsigned long)g_displayStats.watchdogRecoveries,
             (unsigned long)g_displayStats.hardWatchdogRecoveries,
             (unsigned int)g_displayStats.lastUpdateGapMs);
    drawValueRow(230, ru ? "Диагн. TFT" : "TFT diag", buf);
}

static void renderTouchCalibration() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 20);
    tft.print(msg(Msg::TOUCH_CAL_TITLE));
    tft.setTextSize(1);
    tft.setCursor(20, 60);
    if (ui.calSkip > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), msg(Msg::TOUCH_CAL_TAP_N), ui.calSkip);
        tft.print(buf);
    } else {
        tft.print(msg(Msg::TOUCH_CAL_TOUCH_TARGET));
    }

    const int16_t points[4][2] = {
        {30, 30},
        {TFT_WIDTH - 30, 30},
        {TFT_WIDTH - 30, TFT_HEIGHT - 30},
        {30, TFT_HEIGHT - 30}
    };
    int16_t px = points[ui.calStep][0];
    int16_t py = points[ui.calStep][1];
    tft.drawCircle(px, py, 10, TFT_GREEN);
    tft.drawLine(px - 15, py, px + 15, py, TFT_GREEN);
    tft.drawLine(px, py - 15, px, py + 15, TFT_GREEN);
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

    tft.setRotation(1);  // Landscape (480x320)
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
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(3);
        tft.setCursor(100, 120);
        tft.println("Smart-Column S3");
        tft.setTextSize(2);
        tft.setCursor(140, 170);
        tft.setTextColor(TFT_GREEN);
        tft.println("TFT + Touch OK");
        delay(1200);
    }

    tft.fillScreen(TFT_BLACK);
    return true;
}

static bool attemptHardRecovery(uint32_t nowMs) {
    if (nowMs - g_displayStats.lastHardRecoveryAtMs < DISPLAY_HARD_RECOVERY_COOLDOWN_MS) {
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
    // РРЅРёС†РёР°Р»РёР·Р°С†РёСЏ TFT
    LOG_I("Display: Init TFT (LovyanGFX)...");
    
    if (initDisplayHardware(true)) {
        LOG_I("Display: TFT + Touch initialized");

        // РџСЂРѕС„РёР»СЊ Р·Р°С‚РёСЂРєРё РїРѕ СѓРјРѕР»С‡Р°РЅРёСЋ
        memset(&mashProfileDefault, 0, sizeof(mashProfileDefault));
        strncpy(mashProfileDefault.name, "Default mash", sizeof(mashProfileDefault.name) - 1);
        mashProfileDefault.stepCount = 5;

        mashProfileDefault.steps[0].temperature = 38.0f;
        mashProfileDefault.steps[0].duration = 20;
        strncpy(mashProfileDefault.steps[0].name, "Acid rest", sizeof(mashProfileDefault.steps[0].name) - 1);

        mashProfileDefault.steps[1].temperature = 52.0f;
        mashProfileDefault.steps[1].duration = 20;
        strncpy(mashProfileDefault.steps[1].name, "Protein rest", sizeof(mashProfileDefault.steps[1].name) - 1);

        mashProfileDefault.steps[2].temperature = 63.0f;
        mashProfileDefault.steps[2].duration = 40;
        strncpy(mashProfileDefault.steps[2].name, "Maltose rest", sizeof(mashProfileDefault.steps[2].name) - 1);

        mashProfileDefault.steps[3].temperature = 72.0f;
        mashProfileDefault.steps[3].duration = 20;
        strncpy(mashProfileDefault.steps[3].name, "Saccharification", sizeof(mashProfileDefault.steps[3].name) - 1);

        mashProfileDefault.steps[4].temperature = 78.0f;
        mashProfileDefault.steps[4].duration = 10;
        strncpy(mashProfileDefault.steps[4].name, "Mash out", sizeof(mashProfileDefault.steps[4].name) - 1);

        holdStepsDefault[0].temperature = 65.0f;
        holdStepsDefault[0].duration = 60;
        holdStepsCount = 1;

        ui.currentScreen = UI_DASHBOARD;
        ui.rootScreen = UI_DASHBOARD;
        ui.stackDepth = 0;
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

void update(const SystemState& state) {
#if TFT_ENABLED
    const uint32_t now = millis();

    if (g_displayStats.lastUpdateCallAtMs > 0) {
        const uint32_t updateGapMs = now - g_displayStats.lastUpdateCallAtMs;
        const uint16_t clampedGapMs = static_cast<uint16_t>(updateGapMs > 0xFFFF ? 0xFFFF : updateGapMs);
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
            ev = readTouchEvent();
            if (ev.tapped) {
                if (ui.calSkip > 0) {
                    ui.calSkip--;
                    ui.needsRedraw = true;
                } else {
                int16_t rx = 0;
                int16_t ry = 0;
                if (readTouchRawFiltered(&rx, &ry)) {
                    if (ui.calStep < 4) {
                        ui.calRawX[ui.calStep] = rx;
                        ui.calRawY[ui.calStep] = ry;
                        ui.calStep++;
                        ui.needsRedraw = true;
                    }
                    if (ui.calStep >= 4) {
                        applyTouchCalibration();
                        ui.calibrating = false;
                        ui.needsRedraw = true;
                    }
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
        const UiScreen desiredMonitor = isModeRunning(state) ? UI_MODE_MONITOR : UI_DASHBOARD;
        if (ui.rootScreen != desiredMonitor || ui.currentScreen != desiredMonitor) {
            switchRoot(desiredMonitor);
        }
    }

    if (!ui.needsRedraw && g_displayStats.lastFrameAtMs > 0 &&
        (now - g_displayStats.lastFrameAtMs) > DISPLAY_FORCE_REFRESH_MS) {
        ui.needsRedraw = true;
    }

    if (!ui.needsRedraw) {
        bool changed = false;
        if (ui.currentScreen == UI_DASHBOARD ||
            ui.currentScreen == UI_MODE_MONITOR ||
            ui.currentScreen == UI_CONTROL ||
            ui.currentScreen == UI_SERVICE ||
            ui.currentScreen == UI_MANUAL) {
            if (uiLive.mode != state.mode ||
                uiLive.phase != state.rectPhase ||
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
                if (ui.currentScreen == UI_SERVICE &&
                    state.uptime != uiLive.uptime &&
                    (now - uiLive.lastUpdateMs) > 1000) {
                    changed = true;
                }
            }
            if ((ui.currentScreen == UI_DASHBOARD || ui.currentScreen == UI_MODE_MONITOR) &&
                (now - uiLive.lastUpdateMs) > 1200) {
                // Keep phase timer/progress and service values moving even when temperatures are stable.
                changed = true;
            }
        }
        if (changed && (now - uiLive.lastUpdateMs) > 300) {
            ui.needsRedraw = true;
        }
    }

    if (ev.tapped) {
        bool handled = false;
        // РџСЂРѕР±СѓРµРј РїСЂСЏРјС‹Рµ РєРѕРѕСЂРґРёРЅР°С‚С‹
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
                if (full) renderSettings();
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
            default:
                renderDashboard(state, full);
                break;
        }
        tft.endWrite();

        const uint32_t frameTime = millis() - frameStartMs;
        bool scheduleRecoveryRedraw = false;
        bool requestHardRecovery = false;
        g_displayStats.framesRendered++;
        g_displayStats.lastFrameMs = static_cast<uint16_t>(frameTime > 0xFFFF ? 0xFFFF : frameTime);
        g_displayStats.lastFrameAtMs = millis();
        if (g_displayStats.lastFrameMs > g_displayStats.maxFrameMs) {
            g_displayStats.maxFrameMs = g_displayStats.lastFrameMs;
        }

        if (frameTime >= DISPLAY_SLOW_FRAME_MS) {
            g_displayStats.slowFrames++;
            if (!full && frameTime >= DISPLAY_HARD_FRAME_MS) {
                if (g_displayStats.consecutiveSlowFrames < 255) g_displayStats.consecutiveSlowFrames++;
                if (g_displayStats.consecutiveHardFrames < 255) g_displayStats.consecutiveHardFrames++;
            } else {
                g_displayStats.consecutiveSlowFrames = 0;
                g_displayStats.consecutiveHardFrames = 0;
            }
        } else {
            g_displayStats.consecutiveSlowFrames = 0;
            g_displayStats.consecutiveHardFrames = 0;
        }

        if (g_displayStats.consecutiveSlowFrames >= DISPLAY_SOFT_WD_THRESHOLD) {
            // Soft watchdog: force a full redraw cycle instead of running with a stale frame.
            g_displayStats.watchdogRecoveries++;
            g_displayStats.consecutiveSlowFrames = 0;
            ui.lastRenderedScreen = static_cast<UiScreen>(255);
            scheduleRecoveryRedraw = true;

            if (g_displayStats.softRecoveryWindowStartedMs == 0 ||
                (now - g_displayStats.softRecoveryWindowStartedMs) > DISPLAY_SOFT_WD_WINDOW_MS) {
                g_displayStats.softRecoveryWindowStartedMs = now;
                g_displayStats.softRecoveriesInWindow = 1;
            } else if (g_displayStats.softRecoveriesInWindow < 255) {
                g_displayStats.softRecoveriesInWindow++;
            }
        }

        if (g_displayStats.softRecoveryWindowStartedMs > 0 &&
            (now - g_displayStats.softRecoveryWindowStartedMs) > DISPLAY_SOFT_WD_WINDOW_MS) {
            g_displayStats.softRecoveryWindowStartedMs = now;
            g_displayStats.softRecoveriesInWindow = 0;
        }

        if (g_displayStats.consecutiveHardFrames >= DISPLAY_HARD_FRAME_BURST_THRESHOLD ||
            g_displayStats.softRecoveriesInWindow >= DISPLAY_SOFT_WD_BURST_FOR_HARD) {
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

void showMessage(const char* title, const char* message, uint8_t type) {
#if TFT_ENABLED
    if (tft_ok) {
        uint16_t color = TFT_WHITE;
        if (type == 1) {
            color = TFT_YELLOW;
        } else if (type == 2) {
            color = TFT_RED;
        }
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(color);
        tft.setCursor(20, 40);
        tft.println(title);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(20, 120);
        tft.println(message);
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
    if (!tft_ok || !touch_ok) return;
    ui.calibrating = true;
    ui.calStep = 0;
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

void showError(const char* error) {
#if TFT_ENABLED
    if (tft_ok) {
        tft.fillScreen(TFT_RED);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(20, 20);
        tft.println("РћРЁРР‘РљРђ!"); // localized manually for simplicity or add to Msg
        tft.setCursor(20, 80);
        tft.println(error);
    }
#endif
}

} // namespace Display

