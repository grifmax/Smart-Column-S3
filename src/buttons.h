#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>
#include "config.h"

// Инициализация кнопок
void initButtons();

// Обновление состояния кнопок
void updateButtons();

// Обработка действий кнопок
void handleButtonActions();

// Проверка, нажата ли кнопка
bool isButtonPressed(ButtonType button);

// Проверка, была ли нажата и отпущена кнопка
bool isButtonClicked(ButtonType button);

// Проверка, удерживается ли кнопка
bool isButtonHeld(ButtonType button);

// Проверка, удерживается ли кнопка длительное время
bool isButtonLongPressed(ButtonType button);

// Инициализация меню
void initMenu();

// Обработка нажатия кнопки в меню
void handleMenuButton(ButtonType button);

// Обработка нажатия кнопки "ОК" в меню
void handleMenuOkButton();

// Обработка нажатия кнопки "Назад" в меню
void handleMenuBackButton();

// Обновление пунктов меню
void updateMenuItems();

// Получение текущего пункта меню
const MenuItem* getCurrentMenuItem();

#endif // BUTTONS_H