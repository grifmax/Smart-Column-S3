#include "buttons.h"
#include "display.h"
#include "utils.h"
#include "menu.h"

// Состояния кнопок
static ButtonState upButtonState;
static ButtonState downButtonState;
static ButtonState okButtonState;
static ButtonState backButtonState;

// Действия кнопок
static ButtonAction upButtonAction = BUTTON_NONE;
static ButtonAction downButtonAction = BUTTON_NONE;
static ButtonAction okButtonAction = BUTTON_NONE;
static ButtonAction backButtonAction = BUTTON_NONE;

// Инициализация кнопок
void initButtons() {
    // Настраиваем пины кнопок на вход с подтяжкой
    pinMode(PIN_BUTTON_UP, INPUT_PULLUP);
    pinMode(PIN_BUTTON_DOWN, INPUT_PULLUP);
    pinMode(PIN_BUTTON_OK, INPUT_PULLUP);
    pinMode(PIN_BUTTON_BACK, INPUT_PULLUP);
    
    // Сбрасываем состояния кнопок
    upButtonState = {false, false, 0, 0, false};
    downButtonState = {false, false, 0, 0, false};
    okButtonState = {false, false, 0, 0, false};
    backButtonState = {false, false, 0, 0, false};
    
    Serial.println("Кнопки управления инициализированы");
}

// Обновление состояния кнопок
void updateButtons() {
    unsigned long currentTime = millis();
    
    // Считываем текущее состояние кнопок (инвертируем, т.к. используем подтяжку)
    bool upPressed = !digitalRead(PIN_BUTTON_UP);
    bool downPressed = !digitalRead(PIN_BUTTON_DOWN);
    bool okPressed = !digitalRead(PIN_BUTTON_OK);
    bool backPressed = !digitalRead(PIN_BUTTON_BACK);
    
    // Обработка кнопки "Вверх"
    if (upPressed != upButtonState.isPressed) {
        // Учитываем дребезг контактов
        if (currentTime - upButtonState.pressTime > BUTTON_DEBOUNCE_MS) {
            upButtonState.wasPressed = upButtonState.isPressed;
            upButtonState.isPressed = upPressed;
            upButtonState.pressTime = currentTime;
            
            if (upPressed) {
                // Кнопка нажата
                upButtonAction = BUTTON_PRESS;
                registerUserInteraction();
            } else {
                // Кнопка отпущена
                upButtonAction = BUTTON_RELEASE;
                upButtonState.repeatEnabled = false;
            }
        }
    } else if (upPressed && upButtonState.isPressed) {
        // Обработка удержания кнопки
        if (currentTime - upButtonState.pressTime > BUTTON_HOLD_TIME_MS) {
            upButtonAction = BUTTON_HOLD;
            upButtonState.repeatEnabled = true;
        }
        
        // Автоповтор при удержании
        if (upButtonState.repeatEnabled && 
            currentTime - upButtonState.lastActionTime > BUTTON_REPEAT_DELAY_MS) {
            upButtonAction = BUTTON_PRESS;
            upButtonState.lastActionTime = currentTime;
            registerUserInteraction();
        }
    }
    
    // Обработка кнопки "Вниз"
    if (downPressed != downButtonState.isPressed) {
        // Учитываем дребезг контактов
        if (currentTime - downButtonState.pressTime > BUTTON_DEBOUNCE_MS) {
            downButtonState.wasPressed = downButtonState.isPressed;
            downButtonState.isPressed = downPressed;
            downButtonState.pressTime = currentTime;
            
            if (downPressed) {
                // Кнопка нажата
                downButtonAction = BUTTON_PRESS;
                registerUserInteraction();
            } else {
                // Кнопка отпущена
                downButtonAction = BUTTON_RELEASE;
                downButtonState.repeatEnabled = false;
            }
        }
    } else if (downPressed && downButtonState.isPressed) {
        // Обработка удержания кнопки
        if (currentTime - downButtonState.pressTime > BUTTON_HOLD_TIME_MS) {
            downButtonAction = BUTTON_HOLD;
            downButtonState.repeatEnabled = true;
        }
        
        // Автоповтор при удержании
        if (downButtonState.repeatEnabled && 
            currentTime - downButtonState.lastActionTime > BUTTON_REPEAT_DELAY_MS) {
            downButtonAction = BUTTON_PRESS;
            downButtonState.lastActionTime = currentTime;
            registerUserInteraction();
        }
    }
    
    // Обработка кнопки "OK"
    if (okPressed != okButtonState.isPressed) {
        // Учитываем дребезг контактов
        if (currentTime - okButtonState.pressTime > BUTTON_DEBOUNCE_MS) {
            okButtonState.wasPressed = okButtonState.isPressed;
            okButtonState.isPressed = okPressed;
            okButtonState.pressTime = currentTime;
            
            if (okPressed) {
                // Кнопка нажата
                okButtonAction = BUTTON_PRESS;
                registerUserInteraction();
            } else {
                // Кнопка отпущена
                okButtonAction = BUTTON_RELEASE;
            }
        }
    } else if (okPressed && okButtonState.isPressed) {
        // Обработка удержания кнопки
        if (currentTime - okButtonState.pressTime > BUTTON_HOLD_TIME_MS) {
            okButtonAction = BUTTON_HOLD;
        }
    }
    
    // Обработка кнопки "Назад"
    if (backPressed != backButtonState.isPressed) {
        // Учитываем дребезг контактов
        if (currentTime - backButtonState.pressTime > BUTTON_DEBOUNCE_MS) {
            backButtonState.wasPressed = backButtonState.isPressed;
            backButtonState.isPressed = backPressed;
            backButtonState.pressTime = currentTime;
            
            if (backPressed) {
                // Кнопка нажата
                backButtonAction = BUTTON_PRESS;
                registerUserInteraction();
            } else {
                // Кнопка отпущена
                backButtonAction = BUTTON_RELEASE;
            }
        }
    } else if (backPressed && backButtonState.isPressed) {
        // Обработка удержания кнопки
        if (currentTime - backButtonState.pressTime > BUTTON_HOLD_TIME_MS) {
            backButtonAction = BUTTON_HOLD;
        }
    }
}

// Обработать действия кнопок
void handleButtonActions() {
    // Обрабатываем действия кнопок
    if (upButtonAction != BUTTON_NONE) {
        handleUpButton(upButtonAction);
        upButtonAction = BUTTON_NONE;
    }
    
    if (downButtonAction != BUTTON_NONE) {
        handleDownButton(downButtonAction);
        downButtonAction = BUTTON_NONE;
    }
    
    if (okButtonAction != BUTTON_NONE) {
        handleOkButton(okButtonAction);
        okButtonAction = BUTTON_NONE;
    }
    
    if (backButtonAction != BUTTON_NONE) {
        handleBackButton(backButtonAction);
        backButtonAction = BUTTON_NONE;
    }
}

// Обработка нажатия кнопки "Вверх"
void handleUpButton(ButtonAction action) {
    if (action == BUTTON_PRESS || action == BUTTON_HOLD) {
        playSound(SOUND_BUTTON_PRESS);
        
        MenuScreen currentScreen = getCurrentScreen();
        
        // Обработка в зависимости от текущего экрана
        switch (currentScreen) {
            case MENU_MAIN:
            case MENU_PROCESS:
            case MENU_RECT_SETTINGS:
            case MENU_DIST_SETTINGS:
            case MENU_POWER_SETTINGS:
            case MENU_SYSTEM_SETTINGS:
            case MENU_TEMP_SENSORS:
            case MENU_CALIBRATION:
            case MENU_INFO:
                // Перемещение вверх по меню
                menuNavigateUp(currentScreen);
                break;
                
            case SCREEN_PROCESS:
                // На экране процесса кнопка вверх может увеличивать мощность
                if (systemRunning) {
                    int currentPower = getCurrentPowerPercent();
                    setPowerPercent(currentPower + 5); // Увеличиваем на 5%
                    updateDisplay();
                }
                break;
                
            default:
                break;
        }
    }
}

// Обработка нажатия кнопки "Вниз"
void handleDownButton(ButtonAction action) {
    if (action == BUTTON_PRESS || action == BUTTON_HOLD) {
        playSound(SOUND_BUTTON_PRESS);
        
        MenuScreen currentScreen = getCurrentScreen();
        
        // Обработка в зависимости от текущего экрана
        switch (currentScreen) {
            case MENU_MAIN:
            case MENU_PROCESS:
            case MENU_RECT_SETTINGS:
            case MENU_DIST_SETTINGS:
            case MENU_POWER_SETTINGS:
            case MENU_SYSTEM_SETTINGS:
            case MENU_TEMP_SENSORS:
            case MENU_CALIBRATION:
            case MENU_INFO:
                // Перемещение вниз по меню
                menuNavigateDown(currentScreen);
                break;
                
            case SCREEN_PROCESS:
                // На экране процесса кнопка вниз может уменьшать мощность
                if (systemRunning) {
                    int currentPower = getCurrentPowerPercent();
                    setPowerPercent(currentPower - 5); // Уменьшаем на 5%
                    updateDisplay();
                }
                break;
                
            default:
                break;
        }
    }
}

// Обработка нажатия кнопки "ОК"
void handleOkButton(ButtonAction action) {
    if (action == BUTTON_PRESS) {
        playSound(SOUND_BUTTON_MENU);
        
        MenuScreen currentScreen = getCurrentScreen();
        
        // Обработка в зависимости от текущего экрана
        switch (currentScreen) {
            case MENU_MAIN:
            case MENU_PROCESS:
            case MENU_RECT_SETTINGS:
            case MENU_DIST_SETTINGS:
            case MENU_POWER_SETTINGS:
            case MENU_SYSTEM_SETTINGS:
            case MENU_TEMP_SENSORS:
            case MENU_CALIBRATION:
            case MENU_INFO:
                // Выбор текущего пункта меню
                menuSelectItem(currentScreen);
                break;
                
            case SCREEN_PROCESS:
                // На экране процесса кнопка ОК может приостанавливать/возобновлять процесс
                if (systemRunning) {
                    if (systemPaused) {
                        resumeProcess();
                    } else {
                        pauseProcess();
                    }
                    showNotification(systemPaused ? "Процесс приостановлен" : "Процесс возобновлен", NOTIFY_INFO);
                    updateDisplay();
                }
                break;
                
            case SCREEN_START_RECT:
                // Запуск процесса ректификации
                currentMode = MODE_RECTIFICATION;
                startProcess();
                goToScreen(SCREEN_PROCESS);
                break;
                
            case SCREEN_START_DIST:
                // Запуск процесса дистилляции
                currentMode = MODE_DISTILLATION;
                startProcess();
                goToScreen(SCREEN_PROCESS);
                break;
                
            case MENU_CONFIRM:
                // Подтверждение действия
                executeConfirmAction(true);
                break;
                
            default:
                break;
        }
    } else if (action == BUTTON_HOLD) {
        MenuScreen currentScreen = getCurrentScreen();
        
        // Специальные действия при удержании кнопки ОК
        switch (currentScreen) {
            case SCREEN_PROCESS:
                // Долгое нажатие на экране процесса может остановить процесс
                if (systemRunning) {
                    goToScreen(MENU_CONFIRM);
                    setConfirmAction("Остановить процесс?", stopProcess);
                }
                break;
                
            default:
                // Для других экранов долгое нажатие может возвращать на главный экран
                goToScreen(MENU_MAIN);
                break;
        }
    }
}

// Обработка нажатия кнопки "Назад"
void handleBackButton(ButtonAction action) {
    if (action == BUTTON_PRESS) {
        playSound(SOUND_BUTTON_MENU);
        
        MenuScreen currentScreen = getCurrentScreen();
        
        // Обработка в зависимости от текущего экрана
        switch (currentScreen) {
            case MENU_MAIN:
                // На главном экране кнопка назад может переключать на экран температур
                goToScreen(SCREEN_TEMPERATURES);
                break;
                
            case SCREEN_TEMPERATURES:
                // Возврат с экрана температур на главный
                goToScreen(MENU_MAIN);
                break;
                
            case SCREEN_PROCESS:
                // С экрана процесса переходим на экран температур
                goToScreen(SCREEN_TEMPERATURES);
                break;
                
            case MENU_PROCESS:
            case MENU_RECT_SETTINGS:
            case MENU_DIST_SETTINGS:
            case MENU_POWER_SETTINGS:
            case MENU_SYSTEM_SETTINGS:
            case MENU_TEMP_SENSORS:
            case MENU_CALIBRATION:
            case MENU_INFO:
                // Возврат в родительское меню
                menuGoBack(currentScreen);
                break;
                
            case SCREEN_START_RECT:
            case SCREEN_START_DIST:
                // Возврат в меню выбора процесса
                goToScreen(MENU_PROCESS);
                break;
                
            case MENU_CONFIRM:
                // Отмена действия
                executeConfirmAction(false);
                break;
                
            default:
                // По умолчанию возвращаемся на главный экран
                goToScreen(MENU_MAIN);
                break;
        }
    } else if (action == BUTTON_HOLD) {
        // Долгое нажатие кнопки "Назад" всегда возвращает на главный экран
        goToScreen(MENU_MAIN);
    }
}

// Получить действие кнопки "Вверх"
ButtonAction getUpButtonAction() {
    ButtonAction action = upButtonAction;
    upButtonAction = BUTTON_NONE;
    return action;
}

// Получить действие кнопки "Вниз"
ButtonAction getDownButtonAction() {
    ButtonAction action = downButtonAction;
    downButtonAction = BUTTON_NONE;
    return action;
}

// Получить действие кнопки "ОК"
ButtonAction getOkButtonAction() {
    ButtonAction action = okButtonAction;
    okButtonAction = BUTTON_NONE;
    return action;
}

// Получить действие кнопки "Назад"
ButtonAction getBackButtonAction() {
    ButtonAction action = backButtonAction;
    backButtonAction = BUTTON_NONE;
    return action;
}

// Проверить, нажата ли кнопка "Вверх"
bool isUpButtonPressed() {
    return upButtonState.isPressed;
}

// Проверить, нажата ли кнопка "Вниз"
bool isDownButtonPressed() {
    return downButtonState.isPressed;
}

// Проверить, нажата ли кнопка "ОК"
bool isOkButtonPressed() {
    return okButtonState.isPressed;
}

// Проверить, нажата ли кнопка "Назад"
bool isBackButtonPressed() {
    return backButtonState.isPressed;
}

// Зарегистрировать взаимодействие с пользователем (для сброса таймаута меню)
void registerUserInteraction() {
    extern unsigned long lastUserInteractionTime;
    lastUserInteractionTime = millis();
}