#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Версия прошивки
#define FIRMWARE_VERSION "1.4.1"

// Включение функций
#define DISPLAY_ENABLED          // Включить поддержку дисплея
#define BUTTONS_ENABLED          // Включить поддержку кнопок управления

// Конфигурация пинов (ESP32-S3 DevKitC-1 N16R8)
#define PIN_HEATER       5     // Пин для управления нагревателем (SSR через PC817)
#define PIN_PUMP         6     // Пин для управления насосом (STEP)
#define PIN_VALVE        16    // Пин для управления клапаном (охлаждение)
#define PIN_BUZZER       38    // Пин для пьезоизлучателя
#define PIN_TEMP_SENSORS 4     // Пин для датчиков температуры DS18B20

// Пины для PZEM-004T (аппаратный UART0)
#define PZEM_RX_PIN      44    // RX пин для PZEM (подключается к TX PZEM)
#define PZEM_TX_PIN      43    // TX пин для PZEM (подключается к RX PZEM)

// Настройки OLED дисплея
#define DISPLAY_SDA_PIN 21     // SDA пин для I2C дисплея
#define DISPLAY_SCL_PIN 9      // SCL пин для I2C дисплея (GPIO22 нет на ESP32-S3!)
#define DISPLAY_RESET_PIN -1   // Сброс дисплея (-1 если не используется)
#define DISPLAY_ADDRESS 0x3C   // I2C адрес дисплея (0x3C для 128x64, 0x3D для 128x32)
#define DISPLAY_WIDTH 128      // Ширина дисплея
#define DISPLAY_HEIGHT 64      // Высота дисплея

// Пины кнопок управления (ESP32-S3)
#define PIN_BUTTON_UP    13    // Кнопка "Вверх"
#define PIN_BUTTON_DOWN  10    // Кнопка "Вниз" (был 14 - конфликт!)
#define PIN_BUTTON_OK    11    // Кнопка "ОК/Выбор" (был 27 - НЕТ на S3!)
#define PIN_BUTTON_BACK  12    // Кнопка "Назад/Отмена"

// Настройки кнопок
#define BUTTON_DEBOUNCE_MS 50  // Время дребезга контактов (мс)
#define BUTTON_HOLD_TIME_MS 500 // Время удержания для долгого нажатия (мс)
#define BUTTON_REPEAT_DELAY_MS 200 // Интервал автоповтора при удержании (мс)

// Настройки интерфейса
#define MENU_TIMEOUT_MS 30000  // Таймаут возврата к главному экрану (мс)
#define DISPLAY_TIMEOUT_MS 0   // Таймаут отключения дисплея (0 = не отключать)
#define MAX_MENU_ITEMS 10      // Максимальное количество элементов в одном меню

// Wi-Fi настройки
#define WIFI_SSID "DistillerAP"        // Имя Wi-Fi точки доступа
#define WIFI_PASSWORD "distiller12345" // Пароль Wi-Fi точки доступа

// Настройки регулирования мощности
#define POWER_CONTROL_INTERVAL 1000    // Интервал обновления регулировки мощности (мс)
#define SSR_PWM_FREQUENCY 5            // Частота ШИМ для симисторного регулятора (Гц)
#define SSR_CONTROL_INTERVAL 200       // Интервал обновления ШИМ (мс)

// Режимы работы системы
enum OperationMode {
    MODE_NONE = 0,            // Система не активна
    MODE_RECTIFICATION = 1,   // Режим ректификации
    MODE_DISTILLATION = 2     // Режим дистилляции
};

// Режимы управления мощностью
enum PowerControlMode {
    POWER_CONTROL_MANUAL = 0,     // Ручное управление (процент мощности)
    POWER_CONTROL_PI = 1,         // PI-регулирование мощности
    POWER_CONTROL_PZEM = 2        // Точное управление по показаниям PZEM
};

// Модели ректификации
enum RectificationModel {
    MODEL_CLASSIC = 0,        // Классическая модель
    MODEL_ALTERNATIVE = 1     // Альтернативная модель
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
    SOUND_NONE,              // Без звука
    SOUND_START,             // Запуск процесса
    SOUND_STOP,              // Остановка процесса
    SOUND_PHASE_CHANGE,      // Смена фазы
    SOUND_ALARM,             // Тревога
    SOUND_PROCESS_COMPLETE,  // Процесс завершен
    SOUND_BUTTON_PRESS,      // Нажатие кнопки
    SOUND_BUTTON_MENU        // Навигация по меню
};

// Типы уведомлений
enum NotificationType {
    NOTIFY_INFO,             // Информация
    NOTIFY_SUCCESS,          // Успешное действие
    NOTIFY_WARNING,          // Предупреждение
    NOTIFY_ERROR             // Ошибка
};

// Индексы для массива температур
#define MAX_TEMP_SENSORS 3   // Максимальное количество датчиков
#define TEMP_CUBE 0          // Индекс датчика в кубе
#define TEMP_REFLUX 1        // Индекс датчика в колонне/дефлегматоре
#define TEMP_PRODUCT 2       // Индекс датчика в продукте

// Действия кнопок
enum ButtonAction {
    BUTTON_NONE,             // Нет действия
    BUTTON_PRESS,            // Короткое нажатие
    BUTTON_HOLD,             // Длительное нажатие
    BUTTON_RELEASE           // Отпускание кнопки
};

// Идентификаторы экранов меню
enum MenuScreen {
    MENU_MAIN,               // Главное меню
    MENU_PROCESS,            // Выбор процесса
    MENU_RECT_SETTINGS,      // Настройки ректификации
    MENU_DIST_SETTINGS,      // Настройки дистилляции
    MENU_POWER_SETTINGS,     // Настройки мощности
    MENU_SYSTEM_SETTINGS,    // Системные настройки
    MENU_TEMP_SENSORS,       // Настройки датчиков температуры
    MENU_CALIBRATION,        // Калибровка
    MENU_INFO,               // Информация о системе
    MENU_CONFIRM,            // Экран подтверждения
    SCREEN_PROCESS,          // Экран активного процесса
    SCREEN_TEMPERATURES,     // Экран температур
    SCREEN_POWER,            // Экран мощности
    SCREEN_START_RECT,       // Экран запуска ректификации
    SCREEN_START_DIST        // Экран запуска дистилляции
};

// Состояние кнопок
struct ButtonState {
    bool isPressed;              // Кнопка нажата в данный момент
    bool wasPressed;             // Кнопка была нажата в предыдущем цикле
    unsigned long pressTime;     // Время последнего нажатия
    unsigned long lastActionTime; // Время последнего действия для автоповтора
    bool repeatEnabled;          // Включен ли автоповтор для кнопки
};

// Структура для хранения настроек насоса
struct PumpSettings {
    float calibrationFactor;         // Калибровочный коэффициент (мл/с на 100% мощности)
    float headsFlowRate;             // Скорость отбора голов (мл/час)
    float bodyFlowRate;              // Скорость отбора тела (мл/час)
    float tailsFlowRate;             // Скорость отбора хвостов (мл/час)
    float minFlowRate;               // Минимальная скорость отбора (мл/час)
    float maxFlowRate;               // Максимальная скорость отбора (мл/час)
    int pumpPeriodMs;                // Период цикла насоса (мс)
};

// Настройки PI-регулятора
struct PIControllerSettings {
    float kp;                        // Коэффициент пропорциональности
    float ki;                        // Коэффициент интегрирования
    float outputMin;                 // Минимальное значение выхода (0%)
    float outputMax;                 // Максимальное значение выхода (100%)
    float integralLimit;             // Ограничение интегральной составляющей
};

// Настройки дисплея
struct DisplaySettings {
    bool enabled;                    // Включен ли дисплей
    int brightness;                  // Яркость (0-255)
    int rotation;                    // Поворот дисплея (0, 1, 2, 3)
    bool invertColors;               // Инвертировать цвета
    int contrast;                    // Контрастность (0-255)
    int timeout;                     // Таймаут отключения (0 = не отключать)
    bool showLogo;                   // Показывать логотип при запуске
};

// Системные настройки
struct SystemSettings {
    int maxHeaterPowerWatts;         // Максимальная мощность нагревателя (Вт)
    PowerControlMode powerControlMode; // Режим управления мощностью
    PIControllerSettings piSettings; // Настройки PI-регулятора
    bool pzemEnabled;                // Включение/выключение PZEM
    bool soundEnabled;               // Включение/выключение звука
    int soundVolume;                 // Громкость звука (0-100%)
    DisplaySettings displaySettings;  // Настройки дисплея
    int tempUpdateInterval;          // Интервал обновления температуры (мс)
    int tempReportInterval;          // Интервал отправки температуры клиентам (мс)
    // Настройки адресов датчиков DS18B20
    uint8_t tempSensorAddresses[MAX_TEMP_SENSORS][8];  // Адреса датчиков
    bool tempSensorEnabled[MAX_TEMP_SENSORS];          // Активация датчиков
    float tempSensorCalibration[MAX_TEMP_SENSORS];     // Калибровка датчиков
};

// Структура параметров ректификации
struct RectificationParams {
    // Общие параметры
    RectificationModel model;        // Выбранная модель ректификации
    
    // Температурные параметры
    float maxCubeTemp;              // Максимальная температура в кубе (°C)
    float headsTemp;                // Температура отбора голов (°C)
    float bodyTemp;                 // Температура начала отбора тела (°C)
    float tailsTemp;                // Температура начала отбора хвостов (°C)
    float endTemp;                  // Температура завершения процесса (°C)
    
    // Параметры мощности
    int heatingPowerWatts;          // Мощность нагрева (Вт)
    int stabilizationPowerWatts;    // Мощность на стабилизации (Вт)
    int bodyPowerWatts;             // Мощность на отборе тела (Вт)
    int tailsPowerWatts;            // Мощность на отборе хвостов (Вт)
    
    // Проценты мощности (для обратной совместимости)
    int heatingPower;               // Мощность нагрева (0-100%)
    int stabilizationPower;         // Мощность на стабилизации (0-100%)
    int bodyPower;                  // Мощность на отборе тела (0-100%)
    int tailsPower;                 // Мощность на отборе хвостов (0-100%)
    
    // Параметры классической модели
    int stabilizationTime;          // Время стабилизации колонны (минуты)
    float headsVolume;              // Расчетный объем голов (мл)
    float bodyVolume;               // Расчетный объем тела (мл)
    
    // Параметры альтернативной модели
    int headsTargetTimeMinutes;     // Целевое время отбора голов (минуты)
    int postHeadsStabilizationTime; // Время стабилизации после голов (минуты)
    float bodyFlowRateMlPerHour;    // Скорость отбора тела (мл/час)
    float tempDeltaEndBody;         // Изменение температуры для окончания отбора тела (°C)
    float tailsCubeTemp;            // Температура куба для окончания отбора хвостов (°C)
    float tailsFlowRateMlPerHour;   // Скорость отбора хвостов (мл/час)
    bool useSameFlowRateForTails;   // Использовать ту же скорость отбора для хвостов
    
    // Настройки орошения
    float refluxRatio;              // Соотношение орошения для тела (например, 5/1)
    int refluxPeriod;               // Период цикла орошения (секунды)
};

// Структура параметров дистилляции
struct DistillationParams {
    float maxCubeTemp;              // Максимальная температура в кубе (°C)
    float startCollectingTemp;      // Температура начала отбора (°C)
    float endTemp;                  // Температура завершения процесса (°C)
    int heatingPowerWatts;          // Мощность нагрева (Вт)
    int distillationPowerWatts;     // Мощность дистилляции (Вт)
    float flowRate;                 // Скорость отбора (мл/час)
    
    // Проценты мощности (для обратной совместимости)
    int heatingPower;               // Мощность нагрева (0-100%)
    int distillationPower;          // Мощность дистилляции (0-100%)
    
    // Дополнительные параметры
    bool separateHeads;             // Выделение голов отдельно
    float headsVolume;              // Объем голов (мл)
    float headsFlowRate;            // Скорость отбора голов (мл/час)
};

// Структура элемента меню
struct MenuItem {
    const char* text;               // Текст элемента меню
    MenuScreen targetScreen;        // Экран, на который переходит элемент
    void (*action)();               // Функция, выполняемая при выборе элемента
};

#endif // CONFIG_H