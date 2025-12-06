/**
 * @file buttons_debounce.cpp
 * @brief Улучшенная обработка кнопок с защитой от дребезга
 */

#include "buttons.h"
#include "config.h"
#include "logger.h"

// Параметры debouncing
#define DEBOUNCE_DELAY 50      // мс задержки для устранения дребезга
#define LONG_PRESS_TIME 1000   // мс для длинного нажатия
#define REPEAT_DELAY 500       // мс начальная задержка повтора
#define REPEAT_RATE 100        // мс между повторами

// Структура состояния кнопки с debouncing
struct ButtonState {
    int pin;
    bool currentState;
    bool lastStableState;
    bool lastReading;
    unsigned long lastDebounceTime;
    unsigned long pressStartTime;
    unsigned long lastRepeatTime;
    bool isPressed;
    bool wasClicked;
    bool wasLongPressed;
    bool isRepeating;
};

// Массив состояний кнопок
static ButtonState buttonStates[4];

/**
 * @brief Инициализация системы кнопок с debouncing
 */
void initButtonsDebounce() {
    LOG_INFO(LOG_CAT_SYSTEM, "Инициализация кнопок с защитой от дребезга...");
    
    // Настройка пинов
    buttonStates[0].pin = PIN_BUTTON_UP;
    buttonStates[1].pin = PIN_BUTTON_DOWN;
    buttonStates[2].pin = PIN_BUTTON_OK;
    buttonStates[3].pin = PIN_BUTTON_BACK;
    
    for (int i = 0; i < 4; i++) {
        pinMode(buttonStates[i].pin, INPUT_PULLUP);
        
        // Инициализация состояния
        buttonStates[i].currentState = HIGH;
        buttonStates[i].lastStableState = HIGH;
        buttonStates[i].lastReading = HIGH;
        buttonStates[i].lastDebounceTime = 0;
        buttonStates[i].pressStartTime = 0;
        buttonStates[i].lastRepeatTime = 0;
        buttonStates[i].isPressed = false;
        buttonStates[i].wasClicked = false;
        buttonStates[i].wasLongPressed = false;
        buttonStates[i].isRepeating = false;
    }
    
    LOG_INFO(LOG_CAT_SYSTEM, "Кнопки инициализированы");
}

/**
 * @brief Обновление состояния одной кнопки
 * 
 * @param btn Указатель на состояние кнопки
 */
static void updateButton(ButtonState* btn) {
    // Чтение текущего состояния
    bool reading = digitalRead(btn->pin);
    unsigned long currentTime = millis();
    
    // Если состояние изменилось, сбрасываем таймер debounce
    if (reading != btn->lastReading) {
        btn->lastDebounceTime = currentTime;
    }
    
    // Если прошло достаточно времени с последнего изменения
    if ((currentTime - btn->lastDebounceTime) > DEBOUNCE_DELAY) {
        // Если стабильное состояние отличается от текущего
        if (reading != btn->lastStableState) {
            btn->lastStableState = reading;
            
            // Кнопка нажата (LOW из-за INPUT_PULLUP)
            if (reading == LOW) {
                btn->isPressed = true;
                btn->pressStartTime = currentTime;
                btn->lastRepeatTime = currentTime;
                btn->isRepeating = false;
                btn->wasClicked = false;
                btn->wasLongPressed = false;
                
                LOG_DEBUGF(LOG_CAT_SYSTEM, "Кнопка на пине %d нажата", btn->pin);
            }
            // Кнопка отпущена
            else {
                unsigned long pressDuration = currentTime - btn->pressStartTime;
                
                // Определяем тип нажатия
                if (pressDuration >= LONG_PRESS_TIME) {
                    btn->wasLongPressed = true;
                    LOG_DEBUGF(LOG_CAT_SYSTEM, "Кнопка на пине %d: длинное нажатие (%lu мс)", 
                              btn->pin, pressDuration);
                } else {
                    btn->wasClicked = true;
                    LOG_DEBUGF(LOG_CAT_SYSTEM, "Кнопка на пине %d: короткое нажатие (%lu мс)", 
                              btn->pin, pressDuration);
                }
                
                btn->isPressed = false;
                btn->isRepeating = false;
            }
        }
    }
    
    // Обработка длинного нажатия и повторов
    if (btn->isPressed && !btn->wasLongPressed) {
        unsigned long pressDuration = currentTime - btn->pressStartTime;
        
        // Достигнуто время длинного нажатия
        if (pressDuration >= LONG_PRESS_TIME && !btn->wasLongPressed) {
            btn->wasLongPressed = true;
            LOG_DEBUGF(LOG_CAT_SYSTEM, "Кнопка на пине %d: переход в режим длинного нажатия", btn->pin);
        }
        
        // Обработка повторов
        if (!btn->isRepeating) {
            // Начало повторов после задержки
            if (pressDuration >= REPEAT_DELAY) {
                btn->isRepeating = true;
                btn->lastRepeatTime = currentTime;
            }
        } else {
            // Генерация повторных событий
            if ((currentTime - btn->lastRepeatTime) >= REPEAT_RATE) {
                btn->wasClicked = true; // Генерируем клик для повтора
                btn->lastRepeatTime = currentTime;
                LOG_TRACEF(LOG_CAT_SYSTEM, "Кнопка на пине %d: повтор", btn->pin);
            }
        }
    }
    
    btn->lastReading = reading;
}

/**
 * @brief Обновление всех кнопок
 * 
 * Вызывается в loop() для обработки состояния кнопок
 */
void updateButtons() {
    for (int i = 0; i < 4; i++) {
        updateButton(&buttonStates[i]);
    }
}

/**
 * @brief Проверка нажатия кнопки
 * 
 * @param buttonIndex Индекс кнопки (0-3)
 * @return true если кнопка нажата
 */
bool isButtonPressed(int buttonIndex) {
    if (buttonIndex < 0 || buttonIndex >= 4) return false;
    return buttonStates[buttonIndex].isPressed;
}

/**
 * @brief Проверка клика кнопки (нажатие и отпускание)
 * 
 * @param buttonIndex Индекс кнопки (0-3)
 * @return true если был клик (флаг автоматически сбрасывается)
 */
bool wasButtonClicked(int buttonIndex) {
    if (buttonIndex < 0 || buttonIndex >= 4) return false;
    
    if (buttonStates[buttonIndex].wasClicked) {
        buttonStates[buttonIndex].wasClicked = false; // Сброс флага
        return true;
    }
    return false;
}

/**
 * @brief Проверка длинного нажатия
 * 
 * @param buttonIndex Индекс кнопки (0-3)
 * @return true если было длинное нажатие (флаг автоматически сбрасывается)
 */
bool wasButtonLongPressed(int buttonIndex) {
    if (buttonIndex < 0 || buttonIndex >= 4) return false;
    
    if (buttonStates[buttonIndex].wasLongPressed) {
        buttonStates[buttonIndex].wasLongPressed = false; // Сброс флага
        return true;
    }
    return false;
}

/**
 * @brief Получение длительности нажатия
 * 
 * @param buttonIndex Индекс кнопки (0-3)
 * @return Длительность в миллисекундах (0 если не нажата)
 */
unsigned long getButtonPressDuration(int buttonIndex) {
    if (buttonIndex < 0 || buttonIndex >= 4) return 0;
    
    if (buttonStates[buttonIndex].isPressed) {
        return millis() - buttonStates[buttonIndex].pressStartTime;
    }
    return 0;
}

/**
 * @brief Проверка повтора кнопки
 * 
 * @param buttonIndex Индекс кнопки (0-3)
 * @return true если кнопка в режиме повтора
 */
bool isButtonRepeating(int buttonIndex) {
    if (buttonIndex < 0 || buttonIndex >= 4) return false;
    return buttonStates[buttonIndex].isRepeating;
}

// Удобные функции для конкретных кнопок
bool isUpPressed() { return isButtonPressed(0); }
bool isDownPressed() { return isButtonPressed(1); }
bool isOkPressed() { return isButtonPressed(2); }
bool isBackPressed() { return isButtonPressed(3); }

bool wasUpClicked() { return wasButtonClicked(0); }
bool wasDownClicked() { return wasButtonClicked(1); }
bool wasOkClicked() { return wasButtonClicked(2); }
bool wasBackClicked() { return wasButtonClicked(3); }

bool wasUpLongPressed() { return wasButtonLongPressed(0); }
bool wasDownLongPressed() { return wasButtonLongPressed(1); }
bool wasOkLongPressed() { return wasButtonLongPressed(2); }
bool wasBackLongPressed() { return wasButtonLongPressed(3); }
