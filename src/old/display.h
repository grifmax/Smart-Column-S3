#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "config.h"

// Инициализация дисплея
void initDisplay();

// Обновление дисплея
void updateDisplay();

// Отображение начального экрана
void showSplashScreen();

// Отображение логотипа
void showLogo();

// Отображение уведомления
void showNotification(const char* message, NotificationType type, int durationMs = 3000);

// Переход на определенный экран
void goToScreen(DisplayScreen screen);

// Получение текущего экрана
DisplayScreen getCurrentScreen();

// Обработка кнопок на текущем экране
bool handleDisplayButton(ButtonType button);

// Отрисовка главного экрана
void drawMainScreen();

// Отрисовка экрана процесса
void drawProcessScreen();

// Отрисовка экрана меню
void drawMenuScreen();

// Отрисовка экрана настроек
void drawSettingsScreen();

// Отрисовка экрана параметров
void drawParametersScreen();

// Обновление инверсии цветов дисплея
void updateDisplayInversion(bool inverted);

// Обновление яркости дисплея
void updateDisplayBrightness(int brightness);

// Обновление контрастности дисплея
void updateDisplayContrast(int contrast);

// Обновление поворота дисплея
void updateDisplayRotation(int rotation);

#endif // DISPLAY_H