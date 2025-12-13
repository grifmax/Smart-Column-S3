#ifndef MENU_H
#define MENU_H

#include <Arduino.h>
#include "config.h"

// Максимальное количество пунктов в истории навигации
#define MAX_MENU_HISTORY 10

// Инициализация системы меню
void initMenu();

// Получить заголовок для текущего экрана меню
const char* getMenuTitle(MenuScreen screen);

// Получить количество элементов в меню
int getMenuItemsCount(MenuScreen screen);

// Получить текст элемента меню
const char* getMenuItemText(MenuScreen screen, int index);

// Получить текущий выбранный элемент меню
int getSelectedMenuItem(MenuScreen screen);

// Получить позицию прокрутки меню
int getMenuScrollPosition(MenuScreen screen);

// Перемещение вверх по меню
void menuNavigateUp(MenuScreen screen);

// Перемещение вниз по меню
void menuNavigateDown(MenuScreen screen);

// Выбор текущего элемента меню
void menuSelectItem(MenuScreen screen);

// Возврат в родительское меню
void menuGoBack(MenuScreen screen);

// Получить родительский экран для текущего экрана
MenuScreen getParentScreen(MenuScreen screen);

// Установить действие подтверждения
void setConfirmAction(const char* message, void (*action)());

// Выполнить действие подтверждения
void executeConfirmAction(bool confirmed);

#endif // MENU_H