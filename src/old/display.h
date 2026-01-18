#ifndef OLD_DISPLAY_H
#define OLD_DISPLAY_H

#include <Arduino.h>
#include "config.h"

// Инициализация дисплея (легаси-модуль, SSD1306 удален)
void initDisplay();

// Обновление дисплея (без вывода)
void updateDisplay();

// Отображение начального экрана (заглушка)
void showSplashScreen();

// Отображение уведомления (логика без вывода)
void showNotification(const char* message, NotificationType type, int durationMs = 3000);

// Переход на определенный экран
void goToScreen(MenuScreen screen);

// Получение текущего экрана
MenuScreen getCurrentScreen();

// Управление (заглушки)
void setDisplayBrightness(int brightness);
void setDisplayEnabled(bool enabled);

#endif // OLD_DISPLAY_H