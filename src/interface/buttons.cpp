/**
 * Smart-Column S3 - Обработка кнопок
 *
 * Чтение и обработка физических кнопок с debounce
 */

#include "buttons.h"

static uint32_t lastPress[4] = {0};
static bool lastState[4] = {false};

namespace Buttons {

void init() {
  LOG_I("Buttons: Initializing...");

  pinMode(PIN_BUTTON_UP, INPUT_PULLUP);
  pinMode(PIN_BUTTON_DOWN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_OK, INPUT_PULLUP);
  pinMode(PIN_BUTTON_BACK, INPUT_PULLUP);

  LOG_I("Buttons: Ready");
}

void update() {
  uint32_t now = millis();
  uint8_t pins[] = {PIN_BUTTON_UP, PIN_BUTTON_DOWN, PIN_BUTTON_OK,
                    PIN_BUTTON_BACK};

  for (uint8_t i = 0; i < 4; i++) {
    bool current = !digitalRead(pins[i]); // Инвертируем (pullup)

    // Debounce
    if (current != lastState[i] && now - lastPress[i] > BUTTON_DEBOUNCE_MS) {
      lastState[i] = current;
      lastPress[i] = now;

      if (current) {
        LOG_D("Buttons: Button %d pressed", i + 1);
        // TODO: Отправить событие
      }
    }
  }
}

bool isPressed(uint8_t button) {
  if (button > 4)
    return false;
  uint8_t pins[] = {PIN_BUTTON_UP, PIN_BUTTON_DOWN, PIN_BUTTON_OK,
                    PIN_BUTTON_BACK};
  return !digitalRead(pins[button - 1]);
}

} // namespace Buttons
