#ifndef WATT_CONTROL_H
#define WATT_CONTROL_H

#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <Arduino.h>


class WattControl {
public:
  WattControl();
  ~WattControl();

  /**
   * @brief Инициализирует контроллер мощности симистора.
   * Должен быть вызван один раз в setup().
   * @param zeroCrossPin Пин, подключенный к детектору перехода через ноль.
   * @param triacGatePin Пин, подключенный к управляющему электроду симистора
   * (gate).
   * @return true в случае успеха, false в случае ошибки.
   */
  bool init(int zeroCrossPin, int triacGatePin);

  /**
   * @brief Устанавливает требуемую мощность. Потокобезопасен.
   * @param percentage Мощность от 0 (выключено) до 100 (полностью включено).
   */
  void setPower(uint8_t percentage);

private:
  // Компоненты FreeRTOS для синхронизации
  TaskHandle_t _taskHandle;
  QueueHandle_t _eventQueue;
  portMUX_TYPE _spinlock;

  // Аппаратные компоненты
  gptimer_handle_t _gptimer;
  int _triacGatePin;

  // Состояние
  volatile uint8_t _powerPercentage;

  // Внутренние методы и статические обертки
  void _controlTask();
  static void _taskRunner(void *pvParameters);
  static void IRAM_ATTR _zeroCrossISR(void *arg);
  static bool IRAM_ATTR _triacFireISR(gptimer_handle_t timer,
                                      const gptimer_alarm_event_data_t *edata,
                                      void *user_ctx);
};

#endif // WATT_CONTROL_H