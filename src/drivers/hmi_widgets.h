#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Потокобезопасный кольцевой буфер для отрисовки искрографиков (Sparklines)
// Использует статическую память (без String и malloc) согласно GEMINI.md
template <size_t SIZE> class SparklineBuffer {
private:
  float data[SIZE];
  size_t head = 0;
  size_t count = 0;
  SemaphoreHandle_t mutex;

public:
  SparklineBuffer() {
    memset(data, 0, sizeof(data));
    mutex = xSemaphoreCreateMutex();
  }

  ~SparklineBuffer() {
    if (mutex)
      vSemaphoreDelete(mutex);
  }

  void push(float value) {
    if (!mutex)
      return;
    // Неблокирующее ожидание мьютекса (10 тиков)
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      data[head] = value;
      head = (head + 1) % SIZE;
      if (count < SIZE)
        count++;
      xSemaphoreGive(mutex);
    }
  }

  template <typename Display>
  void draw(Display &lcd, int32_t x, int32_t y, int32_t w, int32_t h,
            uint16_t color) {
    if (!mutex || count < 2)
      return;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(20)) == pdTRUE) {

      float min_val = data[0];
      float max_val = data[0];
      for (size_t i = 1; i < count; i++) {
        if (data[i] < min_val)
          min_val = data[i];
        if (data[i] > max_val)
          max_val = data[i];
      }

      // Защита от деления на ноль, если график плоский
      if (max_val - min_val < 0.1f) {
        max_val += 0.5f;
        min_val -= 0.5f;
      }

      float scale_x = (float)(w - 1) / (count - 1);
      float scale_y = (float)(h - 1) / (max_val - min_val);

      int32_t prev_px = -1, prev_py = -1;

      for (size_t i = 0; i < count; i++) {
        size_t index = (head - count + i + SIZE) % SIZE;
        int32_t px = x + (int32_t)(i * scale_x);
        int32_t py = y + h - 1 - (int32_t)((data[index] - min_val) * scale_y);

        if (prev_px != -1) {
          lcd.drawLine(prev_px, prev_py, px, py, color);
        }
        prev_px = px;
        prev_py = py;
      }
      xSemaphoreGive(mutex);
    }
  }
};

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
