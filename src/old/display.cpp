#include "display.h"
#include <string.h>

// Легаси-модуль дисплея: SSD1306 удален, чтобы не конфликтовать с TFT.
// Функции оставлены как заглушки для старого кода.

static MenuScreen currentScreen = MENU_MAIN;
static unsigned long lastScreenUpdateTime = 0;
static unsigned long lastUserInteractionTime = 0;
static bool displayPoweredOn = true;

static bool notificationActive = false;
static unsigned long notificationEndTime = 0;
static char notificationMessage[64] = "";
static NotificationType notificationType = NOTIFY_INFO;

// Частота вызовов обновления (мс)
#define DISPLAY_UPDATE_INTERVAL 200

static void handleMenuTimeout();

void initDisplay() {
    lastScreenUpdateTime = millis();
    lastUserInteractionTime = millis();
    displayPoweredOn = true;
    notificationActive = false;
    notificationEndTime = 0;
    notificationMessage[0] = '\0';
    notificationType = NOTIFY_INFO;

    Serial.println("Display: legacy SSD1306 disabled, no UI output.");
}

void updateDisplay() {
    unsigned long currentMillis = millis();

    // Таймаут отключения дисплея (логика сохранена, вывода нет)
    if (sysSettings.displaySettings.timeout > 0 &&
        currentMillis - lastUserInteractionTime > sysSettings.displaySettings.timeout) {
        displayPoweredOn = false;
        return;
    }

    if (!displayPoweredOn) {
        displayPoweredOn = true;
    }

    if (currentMillis - lastScreenUpdateTime < DISPLAY_UPDATE_INTERVAL) {
        return;
    }

    lastScreenUpdateTime = currentMillis;

    if (notificationActive && currentMillis >= notificationEndTime) {
        notificationActive = false;
    }

    handleMenuTimeout();
}

void showSplashScreen() {
    Serial.println("Display: splash skipped (legacy SSD1306 removed).");
}

void setDisplayBrightness(int brightness) {
    (void)brightness;
}

void setDisplayEnabled(bool enabled) {
    displayPoweredOn = enabled;
}

void showNotification(const char* message, NotificationType type, int durationMs) {
    strncpy(notificationMessage, message, sizeof(notificationMessage) - 1);
    notificationMessage[sizeof(notificationMessage) - 1] = '\0';

    notificationType = type;
    notificationActive = true;
    notificationEndTime = millis() + durationMs;

    updateDisplay();
}

void goToScreen(MenuScreen screen) {
    currentScreen = screen;
    lastUserInteractionTime = millis();
    updateDisplay();
}

MenuScreen getCurrentScreen() {
    return currentScreen;
}

static void handleMenuTimeout() {
    if (MENU_TIMEOUT_MS > 0 &&
        (millis() - lastUserInteractionTime > MENU_TIMEOUT_MS)) {
        if (currentScreen != MENU_MAIN && currentScreen != SCREEN_PROCESS &&
            currentScreen != SCREEN_TEMPERATURES) {
            if (systemRunning) {
                goToScreen(SCREEN_PROCESS);
            } else {
                goToScreen(MENU_MAIN);
            }
        }
    }
}
