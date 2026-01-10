#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// ВЕРСИЯ ПРОШИВКИ
// =============================================================================

#define FIRMWARE_VERSION "1.4.10"
#define FW_NAME "Smart-Column-S3"
#define FW_VERSION FIRMWARE_VERSION
#define FW_DATE __DATE__

// Включение функций
#define DISPLAY_ENABLED // Включить поддержку дисплея
#define BUTTONS_ENABLED // Включить поддержку кнопок управления

// =============================================================================
// КОНФИГУРАЦИЯ ПИНОВ (ESP32-S3 DevKitC-1 N16R8)
// =============================================================================

// --- Основные пины управления ---
#define PIN_HEATER 5                 // SSR нагреватель (через PC817)
#define PIN_SSR_HEATER PIN_HEATER    // Алиас для совместимости
#define PIN_PUMP 6                   // Шаговый насос (STEP) - соответствует SPEC.md
#define PIN_PUMP_STEP PIN_PUMP       // Алиас для совместимости
#define PIN_PUMP_DIR 7               // Шаговый насос (DIR) - соответствует SPEC.md
#define PIN_PUMP_EN 15               // Шаговый насос (ENABLE) - соответствует SPEC.md (не GPIO33 - занят PSRAM!)
#define PIN_VALVE 16                 // Клапан охлаждения
#define PIN_BUZZER 38                // Пьезоизлучатель
#define PIN_TEMP_SENSORS 4           // DS18B20 (OneWire)
#define PIN_ONEWIRE PIN_TEMP_SENSORS // Алиас

// --- PZEM-004T (аппаратный UART0) ---
#define PZEM_RX_PIN 44          // RX (подключается к TX PZEM)
#define PZEM_TX_PIN 43          // TX (подключается к RX PZEM)
#define PIN_PZEM_RX PZEM_RX_PIN // Алиас для совместимости
#define PIN_PZEM_TX PZEM_TX_PIN // Алиас для совместимости
#define PZEM_UART_NUM 0         // UART0 для PZEM-004T
#define PZEM_BAUD_RATE 9600     // Скорость PZEM-004T

// --- I2C шина (BMP280, ADS1115, OLED) ---
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 9                // GPIO9 (GPIO22 не существует на ESP32-S3, используется GPIO9 вместо GPIO22)
#define DISPLAY_SDA_PIN PIN_I2C_SDA
#define DISPLAY_SCL_PIN PIN_I2C_SCL

// --- Клапаны (MOSFET) ---
#define PIN_VALVE_WATER 16 // Охлаждение (то же что PIN_VALVE)
#define PIN_VALVE_HEADS 17 // Отбор голов
#define PIN_VALVE_UNO 18   // Непрерывный отбор

// --- Опциональные выходы ---
#define PIN_SERVO_FRACTION 8   // Фракционник (PWM 50Hz) - соответствует SPEC.md
#define PIN_VALVE_STARTSTOP 14 // Клапан старт-стоп (ШИМ) - GPIO14 (SPEC.md указывает GPIO9, но GPIO9 используется для I2C SCL на ESP32-S3)

// --- Датчики ---
#define PIN_FLOW_SENSOR 3   // YF-S201 (счётчик импульсов) - соответствует SPEC.md
#define PIN_LEVEL_SENSOR 1  // Оптический датчик уровня (опция) - перенесён с GPIO13 (был конфликт с кнопкой)

// --- Кнопки управления ---
#define PIN_BUTTON_UP 13   // Кнопка "Вверх" (SPEC.md указывает GPIO0 для Boot, но код использует GPIO13)
#define PIN_BUTTON_DOWN 10 // Кнопка "Вниз"
#define PIN_BUTTON_OK 11   // Кнопка "ОК/Выбор"
#define PIN_BUTTON_BACK 12 // Кнопка "Назад/Отмена"

// =============================================================================
// OLED ДИСПЛЕЙ
// =============================================================================

#define DISPLAY_RESET_PIN -1         // Сброс (-1 если не используется)
#define DISPLAY_ADDRESS 0x3C         // I2C адрес (0x3C для 128x64)
#define OLED_ADDRESS DISPLAY_ADDRESS // Алиас для совместимости
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

// =============================================================================
// I2C АДРЕСА
// =============================================================================

#define I2C_ADDR_BMP280_1 0x76 // Атмосферное давление
#define I2C_ADDR_BMP280_2 0x77 // Резерв
#define I2C_ADDR_ADS1115 0x48  // АЦП 16-бит

// =============================================================================
// ADS1115 КАНАЛЫ
// =============================================================================

#define ADS_CHANNEL_PRESSURE 0 // MPX5010DP (давление куба)
#define ADS_CHANNEL_RESERVE_1 1
#define ADS_CHANNEL_RESERVE_2 2
#define ADS_CHANNEL_RESERVE_3 3

// =============================================================================
// ТЕРМОМЕТРЫ DS18B20 (7 датчиков)
// =============================================================================

#define TEMP_CUBE 0          // Куб
#define TEMP_COLUMN_BOTTOM 1 // Царга низ (T_base)
#define TEMP_COLUMN_TOP 2    // Царга верх
#define TEMP_REFLUX 3        // Дефлегматор
#define TEMP_TSA 4           // Выход ТСА
#define TEMP_WATER_IN 5      // Вода вход
#define TEMP_WATER_OUT 6     // Вода выход
#define TEMP_COUNT 7
#define TEMP_PRODUCT TEMP_TSA // Алиас для совместимости

// Для обратной совместимости
#define MAX_TEMP_SENSORS TEMP_COUNT

// =============================================================================
// ПОРОГИ БЕЗОПАСНОСТИ
// =============================================================================

#define SAFETY_TEMP_TSA_MAX 55.0f       // °C - прорыв паров
#define SAFETY_TEMP_WATER_OUT_MAX 70.0f // °C - перегрев воды
#define SAFETY_VOLTAGE_MIN 190.0f       // V - низкое напряжение
#define SAFETY_VOLTAGE_MAX 250.0f       // V - высокое напряжение
#define SAFETY_SENSOR_TIMEOUT_MS 5000   // мс - таймаут датчика

// Давление (множители от P_захлёб)
#define PRESSURE_WORK_MULT 0.75f // Рабочее
#define PRESSURE_WARN_MULT 0.90f // Предупреждение
#define PRESSURE_CRIT_MULT 1.05f // Аварийное

// =============================================================================
// ПИД РЕГУЛЯТОР
// =============================================================================

#define PID_KP_DEFAULT 2.0f
#define PID_KI_DEFAULT 0.5f
#define PID_KD_DEFAULT 1.0f
#define PID_OUTPUT_MIN 0
#define PID_OUTPUT_MAX 100

// =============================================================================
// ШИМ (PWM)
// =============================================================================

#define PWM_FREQ_HEATER 1   // Гц (медленный для SSR)
#define PWM_FREQ_VALVE 1000 // Гц
#define PWM_RESOLUTION 8    // бит (0-255)

// Каналы LEDC
#define LEDC_CHANNEL_HEATER 0
#define LEDC_CHANNEL_VALVE 1

// =============================================================================
// НАСТРОЙКИ КНОПОК
// =============================================================================

#define BUTTON_DEBOUNCE_MS 50      // Время дребезга (мс)
#define BUTTON_HOLD_TIME_MS 500    // Время удержания (мс)
#define BUTTON_REPEAT_DELAY_MS 200 // Интервал автоповтора (мс)

// =============================================================================
// НАСТРОЙКИ ИНТЕРФЕЙСА
// =============================================================================

#define MENU_TIMEOUT_MS 30000 // Таймаут возврата к главному экрану
#define DISPLAY_TIMEOUT_MS 0  // Таймаут отключения (0 = не отключать)
#define MAX_MENU_ITEMS 10     // Максимум элементов в меню

// Wi-Fi настройки
#define WIFI_SSID "DistillerAP"        // Имя Wi-Fi точки доступа
#define WIFI_PASSWORD "distiller12345" // Пароль Wi-Fi точки доступа

// Настройки регулирования мощности
#define POWER_CONTROL_INTERVAL                                                 \
  1000                      // Интервал обновления регулировки мощности (мс)
#define SSR_PWM_FREQUENCY 5 // Частота ШИМ для симисторного регулятора (Гц)
#define SSR_CONTROL_INTERVAL 200 // Интервал обновления ШИМ (мс)

// Режимы работы системы
enum OperationMode {
  MODE_NONE = 0,          // Система не активна
  MODE_RECTIFICATION = 1, // Режим ректификации
  MODE_DISTILLATION = 2   // Режим дистилляции
};

// Режимы управления мощностью
enum PowerControlMode {
  POWER_CONTROL_MANUAL = 0, // Ручное управление (процент мощности)
  POWER_CONTROL_PI = 1,     // PI-регулирование мощности
  POWER_CONTROL_PZEM = 2    // Точное управление по показаниям PZEM
};

// Модели ректификации
enum RectificationModel {
  MODEL_CLASSIC = 0,    // Классическая модель
  MODEL_ALTERNATIVE = 1 // Альтернативная модель
};

// Фазы процесса ректификации
enum RectificationPhase {
  PHASE_NONE,                     // Процесс не запущен
  PHASE_HEATING,                  // Фаза нагрева до кипения
  PHASE_STABILIZATION,            // Фаза стабилизации колонны
  PHASE_HEADS,                    // Отбор голов
  PHASE_POST_HEADS_STABILIZATION, // Фаза стабилизации после отбора голов
  PHASE_BODY,                     // Отбор тела
  PHASE_TAILS,                    // Отбор хвостов
  PHASE_COMPLETED                 // Процесс завершен
};

// Фазы процесса дистилляции
enum DistillationPhase {
  DIST_PHASE_NONE,         // Процесс не запущен
  DIST_PHASE_HEATING,      // Фаза нагрева
  DIST_PHASE_DISTILLATION, // Фаза дистилляции
  DIST_PHASE_COMPLETED     // Процесс завершен
};

// Типы звуковых оповещений
enum SoundType {
  SOUND_NONE,             // Без звука
  SOUND_START,            // Запуск процесса
  SOUND_STOP,             // Остановка процесса
  SOUND_PHASE_CHANGE,     // Смена фазы
  SOUND_ALARM,            // Тревога
  SOUND_PROCESS_COMPLETE, // Процесс завершен
  SOUND_BUTTON_PRESS,     // Нажатие кнопки
  SOUND_BUTTON_MENU       // Навигация по меню
};

// Типы уведомлений
enum NotificationType {
  NOTIFY_INFO,    // Информация
  NOTIFY_SUCCESS, // Успешное действие
  NOTIFY_WARNING, // Предупреждение
  NOTIFY_ERROR    // Ошибка
};

// Примечание: Индексы датчиков определены выше (TEMP_CUBE, TEMP_REFLUX и др.)

// Действия кнопок
enum ButtonAction {
  BUTTON_NONE,   // Нет действия
  BUTTON_PRESS,  // Короткое нажатие
  BUTTON_HOLD,   // Длительное нажатие
  BUTTON_RELEASE // Отпускание кнопки
};

// Идентификаторы экранов меню
enum MenuScreen {
  MENU_MAIN,            // Главное меню
  MENU_PROCESS,         // Выбор процесса
  MENU_RECT_SETTINGS,   // Настройки ректификации
  MENU_DIST_SETTINGS,   // Настройки дистилляции
  MENU_POWER_SETTINGS,  // Настройки мощности
  MENU_SYSTEM_SETTINGS, // Системные настройки
  MENU_TEMP_SENSORS,    // Настройки датчиков температуры
  MENU_CALIBRATION,     // Калибровка
  MENU_INFO,            // Информация о системе
  MENU_CONFIRM,         // Экран подтверждения
  SCREEN_PROCESS,       // Экран активного процесса
  SCREEN_TEMPERATURES,  // Экран температур
  SCREEN_POWER,         // Экран мощности
  SCREEN_START_RECT,    // Экран запуска ректификации
  SCREEN_START_DIST     // Экран запуска дистилляции
};

// Состояние кнопок
struct ButtonState {
  bool isPressed;               // Кнопка нажата в данный момент
  bool wasPressed;              // Кнопка была нажата в предыдущем цикле
  unsigned long pressTime;      // Время последнего нажатия
  unsigned long lastActionTime; // Время последнего действия для автоповтора
  bool repeatEnabled;           // Включен ли автоповтор для кнопки
};

// Структура для хранения настроек насоса
struct PumpSettings {
  float calibrationFactor; // Калибровочный коэффициент (мл/с на 100% мощности)
  float headsFlowRate;     // Скорость отбора голов (мл/час)
  float bodyFlowRate;      // Скорость отбора тела (мл/час)
  float tailsFlowRate;     // Скорость отбора хвостов (мл/час)
  float minFlowRate;       // Минимальная скорость отбора (мл/час)
  float maxFlowRate;       // Максимальная скорость отбора (мл/час)
  int pumpPeriodMs;        // Период цикла насоса (мс)
};

// Настройки PI-регулятора
struct PIControllerSettings {
  float kp;            // Коэффициент пропорциональности
  float ki;            // Коэффициент интегрирования
  float outputMin;     // Минимальное значение выхода (0%)
  float outputMax;     // Максимальное значение выхода (100%)
  float integralLimit; // Ограничение интегральной составляющей
};

// Настройки дисплея
struct DisplaySettings {
  bool enabled;      // Включен ли дисплей
  int brightness;    // Яркость (0-255)
  int rotation;      // Поворот дисплея (0, 1, 2, 3)
  bool invertColors; // Инвертировать цвета
  int contrast;      // Контрастность (0-255)
  int timeout;       // Таймаут отключения (0 = не отключать)
  bool showLogo;     // Показывать логотип при запуске
};

// Системные настройки
struct SystemSettings {
  int maxHeaterPowerWatts;           // Максимальная мощность нагревателя (Вт)
  PowerControlMode powerControlMode; // Режим управления мощностью
  PIControllerSettings piSettings;   // Настройки PI-регулятора
  bool pzemEnabled;                  // Включение/выключение PZEM
  bool soundEnabled;                 // Включение/выключение звука
  int soundVolume;                   // Громкость звука (0-100%)
  DisplaySettings displaySettings;   // Настройки дисплея
  int tempUpdateInterval;            // Интервал обновления температуры (мс)
  int tempReportInterval; // Интервал отправки температуры клиентам (мс)
  // Настройки адресов датчиков DS18B20
  uint8_t tempSensorAddresses[MAX_TEMP_SENSORS][8]; // Адреса датчиков
  bool tempSensorEnabled[MAX_TEMP_SENSORS];         // Активация датчиков
  float tempSensorCalibration[MAX_TEMP_SENSORS];    // Калибровка датчиков
};

// Структура параметров ректификации
struct RectificationParams {
  // Общие параметры
  RectificationModel model; // Выбранная модель ректификации

  // Температурные параметры
  float maxCubeTemp; // Максимальная температура в кубе (°C)
  float headsTemp;   // Температура отбора голов (°C)
  float bodyTemp;    // Температура начала отбора тела (°C)
  float tailsTemp;   // Температура начала отбора хвостов (°C)
  float endTemp;     // Температура завершения процесса (°C)

  // Параметры мощности
  int heatingPowerWatts;       // Мощность нагрева (Вт)
  int stabilizationPowerWatts; // Мощность на стабилизации (Вт)
  int bodyPowerWatts;          // Мощность на отборе тела (Вт)
  int tailsPowerWatts;         // Мощность на отборе хвостов (Вт)

  // Проценты мощности (для обратной совместимости)
  int heatingPower;       // Мощность нагрева (0-100%)
  int stabilizationPower; // Мощность на стабилизации (0-100%)
  int bodyPower;          // Мощность на отборе тела (0-100%)
  int tailsPower;         // Мощность на отборе хвостов (0-100%)

  // Параметры классической модели
  int stabilizationTime; // Время стабилизации колонны (минуты)
  float headsVolume;     // Расчетный объем голов (мл)
  float bodyVolume;      // Расчетный объем тела (мл)

  // Параметры альтернативной модели
  int headsTargetTimeMinutes;     // Целевое время отбора голов (минуты)
  int postHeadsStabilizationTime; // Время стабилизации после голов (минуты)
  float bodyFlowRateMlPerHour;    // Скорость отбора тела (мл/час)
  float
      tempDeltaEndBody; // Изменение температуры для окончания отбора тела (°C)
  float tailsCubeTemp;  // Температура куба для окончания отбора хвостов (°C)
  float tailsFlowRateMlPerHour; // Скорость отбора хвостов (мл/час)
  bool
      useSameFlowRateForTails; // Использовать ту же скорость отбора для хвостов

  // Настройки орошения
  float refluxRatio; // Соотношение орошения для тела (например, 5/1)
  int refluxPeriod;  // Период цикла орошения (секунды)
};

// Структура параметров дистилляции
struct DistillationParams {
  float maxCubeTemp;          // Максимальная температура в кубе (°C)
  float startCollectingTemp;  // Температура начала отбора (°C)
  float endTemp;              // Температура завершения процесса (°C)
  int heatingPowerWatts;      // Мощность нагрева (Вт)
  int distillationPowerWatts; // Мощность дистилляции (Вт)
  float flowRate;             // Скорость отбора (мл/час)

  // Проценты мощности (для обратной совместимости)
  int heatingPower;      // Мощность нагрева (0-100%)
  int distillationPower; // Мощность дистилляции (0-100%)

  // Дополнительные параметры
  bool separateHeads;  // Выделение голов отдельно
  float headsVolume;   // Объем голов (мл)
  float headsFlowRate; // Скорость отбора голов (мл/час)
};

// Структура элемента меню
struct MenuItem {
  const char *text;        // Текст элемента меню
  MenuScreen targetScreen; // Экран, на который переходит элемент
  void (*action)();        // Функция, выполняемая при выборе элемента
};

// Примечание: FW_NAME, FW_VERSION, FW_DATE и PIN_I2C_* определены выше

// Интервалы обновления (мс)
#define INTERVAL_SAFETY_CHECK 1000  // Проверка безопасности
#define INTERVAL_TEMP_READ 1000     // Чтение температур
#define INTERVAL_PRESSURE_READ 2000 // Чтение давления
#define INTERVAL_POWER_READ 1000    // Чтение мощности
#define INTERVAL_DISPLAY_UPDATE 500 // Обновление дисплея
#define INTERVAL_WEB_BROADCAST 1000 // Отправка данных по WebSocket
#define INTERVAL_LOG_WRITE 60000    // Запись в лог (1 мин)

// WiFi настройки
#define WIFI_CONNECT_TIMEOUT_MS 10000  // Таймаут подключения к WiFi
#define WIFI_AP_SSID "Smart-Column-S3" // SSID точки доступа
#define WIFI_AP_PASS "12345678"        // Пароль точки доступа

// Значения по умолчанию для оборудования
#define DEFAULT_COLUMN_HEIGHT_MM 1500 // Высота колонны (мм)
#define DEFAULT_PACKING_COEFF 15.0f   // СПН коэффициент: мм рт.ст./м
#define DEFAULT_HEATER_POWER_W 3000   // Мощность нагревателя (Вт)
#define DEFAULT_CUBE_VOLUME_L 50.0    // Объем куба (л)

// Параметры насоса по умолчанию
#define DEFAULT_PUMP_ML_PER_REV 0.5f // мл на оборот
#define PUMP_STEPS_PER_REV 200       // Шагов на оборот
#define PUMP_MICROSTEPS 16           // Микрошаги TMC2209
#define PUMP_MAX_SPEED 1000          // Макс. шагов/сек
#define PUMP_ACCELERATION 500        // Ускорение

// Серво фракционник
#define SERVO_MIN_PULSE 500      // мкс
#define SERVO_MAX_PULSE 2500     // мкс
#define SERVO_MOVE_DELAY_MS 2000 // Пауза после поворота

// =============================================================================
// ФРАКЦИОННИК (5 позиций)
// =============================================================================

#define FRACTION_COUNT 5

// Углы фракционатора (градусы)
#define FRACTION_ANGLE_HEADS 0      // Головы
#define FRACTION_ANGLE_SUBHEADS 36  // Подголовники
#define FRACTION_ANGLE_BODY 72      // Тело
#define FRACTION_ANGLE_PRETAILS 108 // Предхвостье
#define FRACTION_ANGLE_TAILS 144    // Хвосты

// Имена фракций
#define FRACTION_NAME_HEADS "Головы"
#define FRACTION_NAME_SUBHEADS "Подголовники"
#define FRACTION_NAME_BODY "Тело"
#define FRACTION_NAME_PRETAILS "Предхвостье"
#define FRACTION_NAME_TAILS "Хвосты"

// =============================================================================
// ПАРАМЕТРЫ РЕЖИМОВ
// =============================================================================

// Авто-ректификация
#define RECT_STABILIZATION_TIME_MIN 20 // Стабилизация "на себя", мин
#define RECT_STABILIZATION_DELTA 0.1f  // °C за 5 мин
#define RECT_HEADS_PERCENT_DEFAULT 8   // % голов от АС
#define RECT_HEADS_SPEED_ML_H_KW 50    // мл/час на кВт
#define RECT_PURGE_TIME_MIN 10         // Продувка между фракциями

// Smart Decrement (умное снижение скорости)
#define DECREMENT_TRIGGER_DELTA 0.15f  // °C выше T_base → стоп
#define DECREMENT_RESUME_DELTA 0.10f   // °C выше T_base → старт
#define DECREMENT_WAIT_MAX_SEC 300     // Макс. ожидание, сек
#define DECREMENT_SPEED_MULT 0.85f     // Множитель снижения
#define DECREMENT_MIN_SPEED_ML_H_KW 50 // Минимум → хвосты

// УНО цикл (клапан непрерывного отбора)
#define UNO_ON_SEC_DEFAULT 3   // Клапан открыт, сек
#define UNO_OFF_SEC_DEFAULT 60 // Клапан закрыт, сек

// Переход в хвосты
#define TAILS_TEMP_CUBE_MIN 99.0f // °C - окончание погона
#define TAILS_TEMP_DELTA_MAX 0.3f // °C - нестабильность

// =============================================================================
// КАЛИБРОВКА
// =============================================================================

// PZEM-004T
#define PZEM_VOLTAGE_ALARM_MIN 190.0f // V - мин. напряжение
#define PZEM_VOLTAGE_ALARM_MAX 250.0f // V - макс. напряжение
#define PZEM_CURRENT_MAX 30.0f        // A - максимальный ток

// MPX5010DP (датчик давления)
#define MPX5010_OFFSET 0.2f       // В при 0 кПа
#define MPX5010_SENSITIVITY 0.45f // В/кПа

// =============================================================================
// СЕТЬ
// =============================================================================

#define MDNS_HOSTNAME "smart-column"
#define WEB_SERVER_PORT 80
#define WEBSOCKET_PORT 81

// =============================================================================
// SPIFFS / LittleFS
// =============================================================================

#define LOG_FILE_PREFIX "/logs/"
#define LOG_FILE_EXT ".csv"
#define LOG_MAX_SIZE_BYTES 1048576 // 1 МБ на файл
#define LOG_MAX_FILES 10

// =============================================================================
// NVS NAMESPACE
// =============================================================================

#define NVS_NAMESPACE "smartcol"

// Ключи NVS
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"
#define NVS_KEY_TG_TOKEN "tg_token"
#define NVS_KEY_TG_CHAT "tg_chat"
#define NVS_KEY_LANGUAGE "lang"
#define NVS_KEY_THEME "theme"
#define NVS_KEY_SOUND "sound"
#define NVS_KEY_COLUMN_HEIGHT "col_height"
#define NVS_KEY_PACKING_TYPE "pack_type"
#define NVS_KEY_PACKING_COEFF "pack_coeff"
#define NVS_KEY_HEATER_POWER "heater_pwr"
#define NVS_KEY_CUBE_VOLUME "cube_vol"
#define NVS_KEY_TEMP_OFFSETS "temp_offs"
#define NVS_KEY_PUMP_ML_REV "pump_mlrev"
#define NVS_KEY_PRESSURE_FLOOD "p_flood"
#define NVS_KEY_HYDRO_POINTS "hydro_pts"
#define NVS_KEY_FRACTION_ANGLES "frac_ang"
#define NVS_KEY_FRACTION_ENABLED "frac_en"

// =============================================================================
// ЗУММЕР
// =============================================================================

#define BUZZER_FREQ_LOW 1000      // Гц
#define BUZZER_FREQ_HIGH 2000     // Гц
#define BUZZER_DURATION_SHORT 100 // мс
#define BUZZER_DURATION_LONG 500  // мс

// =============================================================================
// МАКРОСЫ ЛОГИРОВАНИЯ
// =============================================================================

#define DEBUG_SERIAL 1
#define DEBUG_LEVEL 2 // 0=OFF, 1=ERROR, 2=INFO, 3=DEBUG

#if DEBUG_SERIAL
#define LOG_E(fmt, ...)                                                        \
  if (DEBUG_LEVEL >= 1)                                                        \
  Serial.printf("[E] " fmt "\n", ##__VA_ARGS__)
#define LOG_W(fmt, ...)                                                        \
  if (DEBUG_LEVEL >= 2)                                                        \
  Serial.printf("[W] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN LOG_W // Алиас для совместимости
#define LOG_I(fmt, ...)                                                        \
  if (DEBUG_LEVEL >= 2)                                                        \
  Serial.printf("[I] " fmt "\n", ##__VA_ARGS__)
#define LOG_D(fmt, ...)                                                        \
  if (DEBUG_LEVEL >= 3)                                                        \
  Serial.printf("[D] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_E(fmt, ...)
#define LOG_W(fmt, ...)
#define LOG_WARN(fmt, ...)
#define LOG_I(fmt, ...)
#define LOG_D(fmt, ...)
#endif

#endif // CONFIG_H