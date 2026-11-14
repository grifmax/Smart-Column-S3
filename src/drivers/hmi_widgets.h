#pragma once

#include <Arduino.h>

// Статические методы для отрисовки HMI индикаторов по стандарту ISA-101
class HMIIndicators {
public:
  template <typename Display>
  static void drawProgressBar(Display &lcd, int32_t x, int32_t y, int32_t w,
                              int32_t h, float percent, uint16_t bgColor,
                              uint16_t fillColor, uint16_t alertColor) {
    if (percent < 0.0f)
      percent = 0.0f;
    if (percent > 100.0f)
      percent = 100.0f;

    int32_t fillWidth = (int32_t)((w * percent) / 100.0f);

    // Цветовая дисциплина ISA-101: яркие цвета только для привлечения внимания
    uint16_t color = (percent > 80.0f) ? alertColor : fillColor;

    if (fillWidth > 0) {
      lcd.fillRect(x, y, fillWidth, h, color);
    }
    if (w - fillWidth > 0) {
      lcd.fillRect(x + fillWidth, y, w - fillWidth, h, bgColor);
    }

    lcd.drawRect(x, y, w, h, lcd.color565(80, 80, 80)); // Тонкая HMI-рамка
  }
};
