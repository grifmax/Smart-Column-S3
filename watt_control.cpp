#include "control/watt_control.h"
#include "config.h" // Для макросов логирования LOG_D, LOG_E, LOG_I
#include "esp_rom_sys.h"

// Период полуволны для сети 50 Гц = 10000 микросекунд.
// Вычитаем небольшое "мертвое время", чтобы не пытаться зажечь симистор в самом
// конце полуволны.
#define HALF_WAVE_PERIOD_US 10000
#define TRIAC_MAX_DELAY_US (HALF_WAVE_PERIOD_US - 200) // 9800 мкс

WattControl::WattControl()
    : _taskHandle(NULL), _eventQueue(NULL), _gptimer(NULL), _triacGatePin(-1),
      _powerPercentage(0) {
  _spinlock = portMUX_INITIALIZER_UNLOCKED;
}

WattControl::~WattControl() {
  if (_taskHandle)
    vTaskDelete(_taskHandle);
  if (_eventQueue)
    vQueueDelete(_eventQueue);
  if (_gptimer) {
    gptimer_stop(_gptimer);
    gptimer_disable(_gptimer);
    gptimer_del(_gptimer);
  }
}

bool WattControl::init(int zeroCrossPin, int triacGatePin) {
  _triacGatePin = triacGatePin;

  pinMode(_triacGatePin, OUTPUT);
  digitalWrite(_triacGatePin, LOW);

  pinMode(zeroCrossPin, INPUT_PULLUP);

  // Создаем очередь для передачи событий из ISR в задачу
  _eventQueue = xQueueCreate(10, sizeof(uint8_t));
  if (_eventQueue == NULL) {
    LOG_E("Не удалось создать очередь событий WattControl");
    return false;
  }

  // Создаем gptimer для точной задержки зажигания симистора
  gptimer_config_t timer_config = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = 1000000, // 1МГц, 1 тик = 1мкс
  };
  ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &_gptimer));

  gptimer_event_callbacks_t cbs = {
      .on_alarm = _triacFireISR,
  };
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(_gptimer, &cbs, this));
  ESP_ERROR_CHECK(gptimer_enable(_gptimer));

  // Создаем задачу управления
  xTaskCreate(_taskRunner, "WattCtrlTask",
              2560, // Размер стека
              this,
              configMAX_PRIORITIES -
                  1, // Высокий приоритет для real-time управления
              &_taskHandle);
  if (_taskHandle == NULL) {
    LOG_E("Не удалось создать задачу WattControl");
    return false;
  }

  // Привязываем прерывание к пину детектора нуля
  attachInterruptArg(digitalPinToInterrupt(zeroCrossPin), _zeroCrossISR, this,
                     FALLING);

  LOG_I("WattControl успешно инициализирован");
  return true;
}

void WattControl::setPower(uint8_t percentage) {
  if (percentage > 100) {
    percentage = 100;
  }
  // Используем критическую секцию для потокобезопасного обновления
  taskENTER_CRITICAL(&_spinlock);
  _powerPercentage = percentage;
  taskEXIT_CRITICAL(&_spinlock);
}

// Статическая обертка для запуска метода класса в задаче
void WattControl::_taskRunner(void *pvParameters) {
  static_cast<WattControl *>(pvParameters)->_controlTask();
}

void WattControl::_controlTask() {
  uint8_t dummy;
  for (;;) {
    // Ждем события о переходе через ноль из ISR
    if (xQueueReceive(_eventQueue, &dummy, portMAX_DELAY)) {

      // Теперь мы в контексте задачи, можно безопасно выполнять "тяжелые"
      // операции
      uint8_t power;
      taskENTER_CRITICAL(&_spinlock);
      power = _powerPercentage;
      taskEXIT_CRITICAL(&_spinlock);

      if (power == 0) {
        digitalWrite(_triacGatePin, LOW); // Полностью выключаем
        continue;
      }
      if (power >= 100) {
        digitalWrite(_triacGatePin, HIGH); // Полностью включаем
        continue;
      }

      // "Тяжелые" вычисления, которые вызывали WDT в ISR
      // Используем нелинейную кривую для более плавного управления на малых
      // мощностях
      float power_map = (float)power / 100.0f;
      uint32_t delay_us =
          (uint32_t)(TRIAC_MAX_DELAY_US * (1.0f - (power_map * power_map)));

      // Безопасное логирование, которое больше не вызывает перезагрузку
      LOG_D("WattControl: Мощность=%d%%, Задержка=%d мкс", power, delay_us);

      // Настраиваем и запускаем одноразовый таймер для зажигания симистора
      gptimer_alarm_config_t alarm_config = {
          .alarm_count = delay_us,
          .reload_count = 0,
          .flags = {.auto_reload_on_alarm = false}};

      gptimer_set_alarm_action(_gptimer, &alarm_config);
      gptimer_start(_gptimer);
    }
  }
}

// ISR: Детектор нуля. ДОЛЖЕН БЫТЬ МАКСИМАЛЬНО БЫСТРЫМ.
void IRAM_ATTR WattControl::_zeroCrossISR(void *arg) {
  BaseType_t high_task_woken = pdFALSE;
  uint8_t dummy = 1;
  xQueueSendFromISR(static_cast<WattControl *>(arg)->_eventQueue, &dummy,
                    &high_task_woken);
  if (high_task_woken)
    portYIELD_FROM_ISR();
}

// ISR: Срабатывание таймера. ДОЛЖЕН БЫТЬ МАКСИМАЛЬНО БЫСТРЫМ.
bool IRAM_ATTR WattControl::_triacFireISR(
    gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata,
    void *user_ctx) {
  digitalWrite(static_cast<WattControl *>(user_ctx)->_triacGatePin, HIGH);
  esp_rom_delay_us(
      20); // Импульс ~20мкс, достаточный для большинства симисторов
  digitalWrite(static_cast<WattControl *>(user_ctx)->_triacGatePin, LOW);
  return false;
}