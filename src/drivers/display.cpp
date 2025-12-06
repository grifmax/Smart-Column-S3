/**
 * Smart-Column S3 - Драйвер дисплея
 *
 * Поддержка TFT дисплея и OLED резервного
 */

#include "display.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED дисплей 0.96" (резервный)
static Adafruit_SSD1306 oled(128, 64, &Wire, -1);
static bool oled_ok = false;

namespace Display {

void init() {
    LOG_I("Display: Initializing...");

    // Попытка инициализации OLED
    oled_ok = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);

    if (oled_ok) {
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(0, 0);
        oled.println("Smart-Column S3");
        oled.println("Starting...");
        oled.display();
        LOG_I("Display: OLED OK");
    } else {
        LOG_E("Display: OLED not found");
    }

    // TODO: Инициализация TFT дисплея

    LOG_I("Display: Init complete");
}

void update(const SystemState& state) {
    if (!oled_ok) return;

    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);

    // Заголовок
    oled.println("Smart-Column S3");
    oled.println("----------------");

    // Режим
    const char* modes[] = {"IDLE", "RECT", "MANUAL", "DIST", "MASH", "HOLD"};
    oled.print("Mode: ");
    oled.println(modes[static_cast<int>(state.mode)]);

    // Температуры
    oled.print("T_cube: ");
    oled.print(state.temps.cube, 1);
    oled.println(" C");

    oled.print("T_col: ");
    oled.print(state.temps.columnTop, 1);
    oled.println(" C");

    // Мощность
    oled.print("Power: ");
    oled.print(state.power.power, 0);
    oled.println(" W");

    // Насос
    if (state.pump.running) {
        oled.print("Pump: ");
        oled.print(state.pump.speedMlPerHour, 0);
        oled.println(" ml/h");
    }

    oled.display();
}

void showMessage(const char* message) {
    if (!oled_ok) return;

    oled.clearDisplay();
    oled.setTextSize(2);
    oled.setCursor(0, 20);
    oled.println(message);
    oled.display();
}

void showError(const char* error) {
    if (!oled_ok) return;

    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("ERROR!");
    oled.println("");
    oled.setTextSize(1);
    oled.println(error);
    oled.display();
}

} // namespace Display
