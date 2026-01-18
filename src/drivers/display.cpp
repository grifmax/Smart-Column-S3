/**
 * Smart-Column S3 - Драйвер дисплея
 *
 * TFT 3.5" ILI9488 (основной)
 * Использует LovyanGFX для TFT
 */

#include "display.h"
#include <LovyanGFX.hpp>
#include <esp_task_wdt.h>

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

// Read calibrated touch (screen coordinates)
bool touchRead(int16_t* sx, int16_t* sy) {
    int16_t rawX, rawY;
    if (!touchReadRaw(&rawX, &rawY)) {
        return false;
    }
    
    // Map to screen coordinates using calibration (inverted axes)
    *sx = map(rawX, TOUCH_CAL_X_MAX, TOUCH_CAL_X_MIN, 0, TFT_WIDTH);
    *sy = map(rawY, TOUCH_CAL_Y_MAX, TOUCH_CAL_Y_MIN, 0, TFT_HEIGHT);
    
    // Clamp to screen bounds
    *sx = constrain(*sx, 0, TFT_WIDTH - 1);
    *sy = constrain(*sy, 0, TFT_HEIGHT - 1);
    
    return true;
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
    } else {
        LOG_E("Display: TFT init failed");
    }
#endif

    LOG_I("Display: Init complete");
}

void update(const SystemState& state) {
#if TFT_ENABLED
    if (!tft_ok) return;
    
    // Header
    tft.fillRect(0, 0, 480, 45, TFT_NAVY);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 15);
    tft.print("Smart-Column S3");
    
    // Mode
    const char* modes[] = {"IDLE", "RECT", "DIST", "M.RECT", "MASH", "HOLD"};
    tft.setCursor(350, 15);
    tft.print(modes[static_cast<int>(state.mode)]);
    
    // Data area
    tft.fillRect(0, 50, 480, 220, TFT_BLACK);
    tft.setTextSize(2);
    
    // Temperatures
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(20, 60);
    tft.print("Cube:   ");
    tft.setTextColor(TFT_WHITE);
    tft.print(state.temps.cube, 1);
    tft.println(" C");
    
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(20, 90);
    tft.print("Column: ");
    tft.setTextColor(TFT_WHITE);
    tft.print(state.temps.columnTop, 1);
    tft.println(" C");
    
    // Power
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(20, 130);
    tft.print("Power:  ");
    tft.setTextColor(TFT_WHITE);
    tft.print(state.power.power, 0);
    tft.println(" W");
    
    // Touch (calibrated screen coordinates)
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(20, 180);
#ifdef TOUCH_DEBUG_RAW
    if (touch_ok) {
        int irqState = digitalRead(TOUCH_IRQ);
        int16_t rx = touchReadChannel(0x90);
        int16_t ry = touchReadChannel(0xD0);
        tft.print("IRQ:");
        tft.print(irqState ? "H " : "L ");
        tft.print("Raw:");
        tft.print(rx);
        tft.print(",");
        tft.println(ry);
    } else {
        tft.println("Touch: no init");
    }
#else
    int16_t sx, sy;
    if (touch_ok && touchRead(&sx, &sy)) {
        tft.print("Touch: ");
        tft.print(sx);
        tft.print(",");
        tft.println(sy);
        // Draw calibrated point
        tft.fillCircle(sx, sy, 8, TFT_RED);
    } else {
        tft.println("Touch: waiting...");
    }
#endif
    
    // Status bar
    tft.fillRect(0, 280, 480, 40, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 295);
    tft.print("Uptime: ");
    tft.print(state.uptime);
    tft.print("s");
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
        tft.setTextSize(3);
        tft.setCursor(20, 40);
        tft.println(title);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(2);
        tft.setCursor(20, 120);
        tft.println(message);
    }
#endif
}

void showError(const char* error) {
#if TFT_ENABLED
    if (tft_ok) {
        tft.fillScreen(TFT_RED);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(3);
        tft.setCursor(20, 20);
        tft.println("ERROR!");
        tft.setTextSize(2);
        tft.setCursor(20, 80);
        tft.println(error);
    }
#endif
}

} // namespace Display
