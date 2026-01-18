/**
 * Smart-Column S3 - Драйвер дисплея
 *
 * TFT 3.5" ILI9488 (основной)
 * Использует LovyanGFX для TFT
 */

#include "display.h"
#include <LovyanGFX.hpp>
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
// LovyanGFX конфигурация для ILI9488 (только дисплей)
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
            cfg.freq_write = 40000000;    // 40 MHz
            cfg.freq_read = 16000000;
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
            cfg.readable = true;
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);
    }
};

static LGFX tft;
static bool tft_ok = false;

// XPT2046 touch - manual SPI read
static bool touch_ok = false;

// Software SPI (bit-bang) for XPT2046 - completely separate pins from TFT
static inline void touchSpiWrite(uint8_t data) {
    for (int i = 7; i >= 0; i--) {
        digitalWrite(TOUCH_DIN, (data >> i) & 1);
        delayMicroseconds(2);
        digitalWrite(TOUCH_CLK, HIGH);
        delayMicroseconds(2);
        digitalWrite(TOUCH_CLK, LOW);
        delayMicroseconds(2);
    }
}

static inline uint8_t touchSpiRead() {
    uint8_t data = 0;
    for (int i = 7; i >= 0; i--) {
        digitalWrite(TOUCH_CLK, HIGH);
        delayMicroseconds(2);
        if (digitalRead(TOUCH_DO)) {
            data |= (1 << i);
        }
        digitalWrite(TOUCH_CLK, LOW);
        delayMicroseconds(2);
    }
    return data;
}

uint16_t touchReadChannel(uint8_t channel) {
    // Select touch
    digitalWrite(TOUCH_CS, LOW);
    delayMicroseconds(10);
    
    // Send command
    touchSpiWrite(channel);
    delayMicroseconds(10);
    
    // Read 16 bits
    uint8_t hi = touchSpiRead();
    uint8_t lo = touchSpiRead();
    
    digitalWrite(TOUCH_CS, HIGH);
    
    return ((hi << 8) | lo) >> 3;  // 12-bit result
}

// Read raw touch (swapped X/Y commands)
bool touchReadRaw(int16_t* x, int16_t* y) {
#ifndef TOUCH_IGNORE_IRQ
    if (digitalRead(TOUCH_IRQ) == HIGH) {
        return false;  // Not touched
    }
#endif
    
    // Read X and Y (swapped commands)
    *x = touchReadChannel(0x90);
    *y = touchReadChannel(0xD0);
    
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
        delay(5);
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
static const int16_t UI_HEADER_H = 40;  // Немного уменьшил
static const int16_t UI_FOOTER_H = 65;  // Немного увеличил для шрифта
static const int16_t UI_CONTENT_Y = 10; // Начинаем почти сверху
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

    bool calibrating = false;
    uint8_t calStep = 0;
    uint8_t calSkip = 0;
    int16_t calRawX[4] = {0};
    int16_t calRawY[4] = {0};
};

static UiState ui;

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
    bool pressed = touch_ok && touchRead(&sx, &sy);
    ev.pressed = pressed;
    ev.x = sx;
    ev.y = sy;

    if (pressed && !ui.touchPressed) {
        uint32_t now = millis();
        if (now - ui.lastTapMs > 160) {
            ev.tapped = true;
            ui.lastTapMs = now;
        }
    }

    if (!pressed && ui.touchPressed) {
        ev.released = true;
    }

    ui.touchPressed = pressed;
    return ev;
}

static void pushScreen(UiScreen screen) {
    if (ui.stackDepth < 6) {
        ui.stack[ui.stackDepth++] = ui.currentScreen;
    }
    ui.currentScreen = screen;
    ui.needsRedraw = true;
}

static void popScreen() {
    if (ui.stackDepth > 0) {
        ui.currentScreen = ui.stack[--ui.stackDepth];
    } else {
        ui.currentScreen = ui.rootScreen;
    }
    ui.needsRedraw = true;
}

static void switchRoot(UiScreen screen) {
    ui.rootScreen = screen;
    ui.currentScreen = screen;
    ui.stackDepth = 0;
    ui.needsRedraw = true;
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
    edit.value = val;
    edit.min = min;
    edit.max = max;
    edit.step = step;
    edit.fastStep = fastStep;
    edit.onSave = cb;
    strncpy(edit.unit, unit, sizeof(edit.unit)-1);
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
    float power = 0.0f;
    float pumpSpeed = 0.0f;
    uint32_t uptime = 0;
    uint32_t lastUpdateMs = 0;
};

static UiLiveCache uiLive;

static bool hit(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    return (x >= rx && x <= (rx + rw) && y >= ry && y <= (ry + rh));
}

static bool hitPad(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh, int16_t pad) {
    return hit(x, y, rx - pad, ry - pad, rw + (pad * 2), rh + (pad * 2));
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

static bool handleNavigationTap(int16_t tx, int16_t ty) {
    // Кнопка НАЗАД (теперь в верхнем правом углу на под-экранах)
    bool isRoot = (ui.currentScreen == UI_DASHBOARD || ui.currentScreen == UI_CONTROL || 
                   ui.currentScreen == UI_SETTINGS || ui.currentScreen == UI_SERVICE);
    
    if (!isRoot && hit(tx, ty, TFT_WIDTH - 110, 0, 110, 50)) {
        popScreen();
        return true;
    }

    // Tabs
    if (ty >= (TFT_HEIGHT - UI_FOOTER_H)) {
        int tab = tx / (TFT_WIDTH / 4);
        if (tab >= 0 && tab < 4) {
            switchRoot(static_cast<UiScreen>(tab));
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
            
        case UI_CONTROL:
            // Новые координаты кнопок управления (с учетом отсутствия хедера)
            if (hit(tx, ty, 10, 15, 225, 55)) {
                FSM::startMode(g_state, g_settings, Mode::RECTIFICATION);
                return true;
            } else if (hit(tx, ty, 245, 15, 225, 55)) {
                FSM::Distillation::setParams(distUi.speedMlH,
                                             distUi.headsVolumeMl,
                                             distUi.targetVolumeMl,
                                             distUi.endTempC);
                FSM::startMode(g_state, g_settings, Mode::DISTILLATION);
                return true;
            } else if (hit(tx, ty, 10, 75, 225, 55)) {
                FSM::startMode(g_state, g_settings, Mode::MANUAL_RECT);
                return true;
            } else if (hit(tx, ty, 245, 75, 225, 55)) {
                FSM::Mashing::start(g_state, &mashProfileDefault);
                return true;
            } else if (hit(tx, ty, 10, 135, 225, 55)) {
                FSM::Hold::start(g_state, holdStepsDefault, holdStepsCount);
                return true;
            } else if (hit(tx, ty, 245, 135, 225, 55)) {
                pushScreen(UI_MANUAL);
                return true;
            } else if (hit(tx, ty, 10, 195, 225, 55)) {
                if (state.paused) {
                    FSM::resume(g_state);
                } else {
                    FSM::pause(g_state);
                }
                return true;
            } else if (hit(tx, ty, 245, 195, 225, 55)) {
                FSM::stopMode(g_state);
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
            if (tx > 200 || ty > 200) { // Учитываем возможный свап или широкую область
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
            if (tx > 200 || ty > 200) {
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
            if (tx > 200 || ty > 200) {
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
            if (tx > 240) {
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
    if (!showBack) return; // Убираем дублирующий тулбар на главных экранах

    // Отрисовываем только на под-экранах
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
    const char* labels[4] = {
        msg(Msg::MONITOR), 
        msg(Msg::CONTROL), 
        msg(Msg::SETTINGS), 
        msg(Msg::SERVICE)
    };
    int16_t tw = TFT_WIDTH / 4;
    
    tft.fillRect(0, TFT_HEIGHT - UI_FOOTER_H, TFT_WIDTH, UI_FOOTER_H, colorCard());
    tft.drawFastHLine(0, TFT_HEIGHT - UI_FOOTER_H, TFT_WIDTH, tft.color565(200, 200, 200));

    for (int i = 0; i < 4; i++) {
        int16_t x = i * tw;
        bool active = (current == static_cast<UiScreen>(i));
        
        if (active) {
            tft.fillRect(x + 5, TFT_HEIGHT - UI_FOOTER_H + 5, tw - 10, UI_FOOTER_H - 10, COLOR_PRIMARY);
            tft.setTextColor(TFT_WHITE);
        } else {
            tft.setTextColor(colorFg());
        }
        
        // Увеличиваем шрифт для вкладок
        tft.setTextSize(1);
        tft.setFont(&fonts::efontJA_24); 
        tft.setTextDatum(middle_center);
        tft.drawString(labels[i], x + tw / 2, TFT_HEIGHT - UI_FOOTER_H / 2);
        tft.setFont(&fonts::efontJA_16); // Возвращаем основной шрифт
    }
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
        // Делаем значение похожим на кнопку
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

static void drawValueTile(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, const char* value, const char* unit, uint16_t color) {
    drawCard(x, y, w, h, colorCard());
    
    tft.setTextColor(tft.color565(120, 120, 120));
    tft.setTextSize(1);
    tft.setTextDatum(top_left);
    tft.drawString(label, x + 10, y + 8);
    
    tft.setTextColor(color);
    tft.setTextSize(w > 150 ? 4 : 2); // Большие значения для крупных плиток
    tft.setTextDatum(middle_center);
    tft.drawString(value, x + w / 2, y + h / 2 + 5);
    
    tft.setTextColor(tft.color565(100, 100, 100));
    tft.setTextSize(1);
    tft.setTextDatum(bottom_right);
    tft.drawString(unit, x + w - 8, y + h - 8);
    
    tft.setTextDatum(top_left);
}

static void renderDashboard(const SystemState& state, bool full) {
    if (full) {
        tft.fillScreen(colorBg());
        drawHeader(msg(Msg::MONITOR), false);
        drawTabs(UI_DASHBOARD);
    }

    // Header bar with Mode & Phase (перенес выше, так как хедера нет)
    int16_t barY = 10; 
    if (full) {
        drawCard(10, barY, TFT_WIDTH - 20, 40, colorCard());
    }
    
    static char lastStatus[64] = "";
    char statusBuf[64];
    snprintf(statusBuf, sizeof(statusBuf), "%s : %s", 
             FSM::getModeName(state.mode), 
             FSM::getPhaseName(state.rectPhase));
    
    if (full || strcmp(lastStatus, statusBuf) != 0) {
        if (!full) {
            tft.fillRect(20, barY + 5, TFT_WIDTH - 40, 30, colorCard());
        }
        tft.setTextColor(colorAccent());
        tft.setTextSize(2);
        tft.setTextDatum(middle_center);
        tft.drawString(statusBuf, TFT_WIDTH / 2, barY + 20);
        tft.setTextSize(1);
        strncpy(lastStatus, statusBuf, sizeof(lastStatus));
    }

    // Main Row: Cube and Power
    char val[16];
    snprintf(val, sizeof(val), "%.1f", state.temps.cube);
    drawValueTile(10, barY + 50, 225, 90, msg(Msg::CUBE_TEMP), val, "°C", COLOR_DANGER);
    
    snprintf(val, sizeof(val), "%.0f", state.power.power);
    drawValueTile(245, barY + 50, 225, 90, msg(Msg::HEATER_POWER), val, msg(Msg::UNIT_W), COLOR_WARNING);

    // Secondary Row: Top, Reflux, Pump, TSA
    snprintf(val, sizeof(val), "%.1f", state.temps.columnTop);
    drawValueTile(10, barY + 150, 107, 55, msg(Msg::TOP_T), val, "°C", colorAccent());
    
    snprintf(val, sizeof(val), "%.1f", state.temps.reflux);
    drawValueTile(127, barY + 150, 107, 55, msg(Msg::REFLUX_T), val, "°C", COLOR_INFO);
    
    snprintf(val, sizeof(val), "%.0f", state.pump.speedMlPerHour);
    drawValueTile(245, barY + 150, 107, 55, msg(Msg::PUMP), val, msg(Msg::UNIT_ML_H), COLOR_SUCCESS);
    
    snprintf(val, sizeof(val), "%.1f", state.temps.tsa);
    drawValueTile(362, barY + 150, 107, 55, msg(Msg::TSA_T), val, "°C", COLOR_DANGER);
    
    tft.setTextDatum(top_left);
}

static void renderControl(const SystemState& state, bool full) {
    if (full) {
        tft.fillScreen(colorBg());
        drawHeader(msg(Msg::CONTROL), false);
        drawTabs(UI_CONTROL);

        int16_t bw = 225;
        int16_t bh = 55;
        int16_t x2 = 245;

        drawButton(10, 15, bw, bh, msg(Msg::AUTO_RECTIFY), COLOR_SUCCESS, TFT_WHITE);
        drawButton(x2, 15, bw, bh, msg(Msg::DISTILLATION), COLOR_SUCCESS, TFT_WHITE);
        
        drawButton(10, 75, bw, bh, msg(Msg::MANUAL_RECT), COLOR_PRIMARY, TFT_WHITE);
        drawButton(x2, 75, bw, bh, msg(Msg::MASHING), COLOR_PRIMARY, TFT_WHITE);
        
        drawButton(10, 135, bw, bh, msg(Msg::HOLD_MODE), COLOR_PRIMARY, TFT_WHITE);
        drawButton(x2, 135, bw, bh, msg(Msg::MANUAL_PUMP), COLOR_INFO, TFT_WHITE);

        drawButton(x2, 195, bw, bh, msg(Msg::STOP), COLOR_DANGER, TFT_WHITE);
    }

    // Dynamic Pause button
    drawButton(10, 195, 225, 55, state.paused ? msg(Msg::RESUME) : msg(Msg::PAUSE), COLOR_WARNING, TFT_WHITE);
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
        char buf[32];
        snprintf(buf, sizeof(buf), "%s", FW_VERSION);
        drawValueRow(30, msg(Msg::VERSION), buf);
    }

    char buf[32];
    clearRow(80, 30);
    snprintf(buf, sizeof(buf), "%lus", state.uptime);
    drawValueRow(80, msg(Msg::UPTIME), buf);
    
    clearRow(130, 30);
    snprintf(buf, sizeof(buf), "%u KB", ESP.getFreeHeap() / 1024);
    drawValueRow(130, msg(Msg::FREE_HEAP), buf);
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

#endif // TFT_ENABLED

namespace Display {

void init() {
    LOG_I("Display: Initializing...");

#if TFT_ENABLED
    // Инициализация TFT
    LOG_I("Display: Init TFT (LovyanGFX)...");
    tft_ok = tft.init();
    
    if (tft_ok) {
        tft.setRotation(1);  // Ландшафтная ориентация (480x320)
        tft.setSwapBytes(true);
        tft.setTextScroll(true);
        tft.setTextSize(1);
        tft.setFont(&fonts::efontJA_16); // Встроенный шрифт с поддержкой кириллицы
        
        // Setup touch pins (separate SPI from TFT)
        pinMode(TOUCH_CLK, OUTPUT);
        pinMode(TOUCH_DIN, OUTPUT);
        pinMode(TOUCH_DO, INPUT);
        pinMode(TOUCH_CS, OUTPUT);
        pinMode(TOUCH_IRQ, INPUT_PULLUP);
        digitalWrite(TOUCH_CLK, LOW);
        digitalWrite(TOUCH_CS, HIGH);
        touch_ok = true;
        
        // Welcome screen
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(3);
        tft.setCursor(100, 120);
        tft.println("Smart-Column S3");
        tft.setTextSize(2);
        tft.setCursor(140, 170);
        tft.setTextColor(TFT_GREEN);
        tft.println("TFT + Touch OK");
        
        LOG_I("Display: TFT + Touch initialized");
        delay(1500);
        
        // Очистить экран для основного UI
        tft.fillScreen(TFT_BLACK);

        // Профиль затирки по умолчанию
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
    if (!tft_ok) return;

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
            renderTouchCalibration();
            ui.needsRedraw = false;
        }
        return;
    }

    TouchEvent ev = readTouchEvent();
    const uint32_t now = millis();
    if (!ui.needsRedraw) {
        bool changed = false;
        if (ui.currentScreen == UI_DASHBOARD ||
            ui.currentScreen == UI_CONTROL ||
            ui.currentScreen == UI_SERVICE ||
            ui.currentScreen == UI_MANUAL) {
            if (uiLive.mode != state.mode ||
                uiLive.phase != state.rectPhase ||
                uiLive.paused != state.paused) {
                changed = true;
            }
            if (fabsf(uiLive.tCube - state.temps.cube) > 0.1f ||
                fabsf(uiLive.tTop - state.temps.columnTop) > 0.1f ||
                fabsf(uiLive.tReflux - state.temps.reflux) > 0.1f ||
                fabsf(uiLive.tTsa - state.temps.tsa) > 0.1f) {
                changed = true;
            }
            if (fabsf(uiLive.power - state.power.power) > 1.0f ||
                fabsf(uiLive.pumpSpeed - state.pump.speedMlPerHour) > 1.0f) {
                changed = true;
            }
            if (state.uptime != uiLive.uptime && (now - uiLive.lastUpdateMs) > 1000) {
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
        handled = handleNavigationTap(ev.x, ev.y);
        if (!handled) {
            handled = handleScreenTap(ev.x, ev.y, state);
        }
        
        // Если не сработало, пробуем инвертированные (для некоторых тач-панелей)
        if (!handled) {
            handled = handleNavigationTap(ev.y, ev.x);
            if (!handled) {
                handled = handleScreenTap(ev.y, ev.x, state);
            }
        }
        
        if (handled) {
            ui.needsRedraw = true;
        }
    }

    if (ui.needsRedraw) {
        uiLive.mode = state.mode;
        uiLive.phase = state.rectPhase;
        uiLive.paused = state.paused;
        uiLive.tCube = state.temps.cube;
        uiLive.tTop = state.temps.columnTop;
        uiLive.tReflux = state.temps.reflux;
        uiLive.tTsa = state.temps.tsa;
        uiLive.power = state.power.power;
        uiLive.pumpSpeed = state.pump.speedMlPerHour;
        uiLive.uptime = state.uptime;
        uiLive.lastUpdateMs = now;
        const bool full = (ui.currentScreen != ui.lastRenderedScreen);
        switch (ui.currentScreen) {
            case UI_DASHBOARD:
                renderDashboard(state, full);
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
        ui.needsRedraw = false;
        ui.lastRenderedScreen = ui.currentScreen;
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

void showError(const char* error) {
#if TFT_ENABLED
    if (tft_ok) {
        tft.fillScreen(TFT_RED);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(20, 20);
        tft.println("ОШИБКА!"); // localized manually for simplicity or add to Msg
        tft.setCursor(20, 80);
        tft.println(error);
    }
#endif
}

} // namespace Display
