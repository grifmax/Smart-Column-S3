/**
 * Smart-Column S3 - Главный файл
 *
 * Контроллер автоматизации ректификационной колонны
 * Платформа: ESP32-S3 DevKitC-1 N16R8
 */

#include "fs_compat.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include <Preferences.h>

#include "config.h"
#include "types.h"

// Драйверы
#include "drivers/display.h"
#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/sensors.h"
#include "drivers/valves.h"

// Управление
#include "control/demo_simulator.h"
#include "control/fsm.h"
#include "control/safety.h"
#include "control/watt_control.h"

// Интерфейсы
#include "interface/buttons.h"
#include "interface/mqtt.h"
#include "interface/ota.h"
#include "interface/telegram.h"
#include "interface/webserver.h"
#include "interface/cloud_tunnel.h"

// Хранение
#include "storage/logger.h"
#include "storage/nvs_manager.h"

// =============================================================================
// ГЛОБАЛЬНЫЕ ОБЪЕКТЫ
// =============================================================================

SystemState g_state;           // Текущее состояние системы
Settings g_settings;           // Настройки (из NVS)
EnergyHistory g_energyHistory; // История энергопотребления

// Таймеры задач
uint32_t g_lastTempRead = 0;
uint32_t g_lastPressureRead = 0;
uint32_t g_lastPowerRead = 0;
uint32_t g_lastDisplayUpdate = 0;
uint32_t g_lastWebBroadcast = 0;
uint32_t g_lastLogWrite = 0;
uint32_t g_lastSafetyCheck = 0;

// =============================================================================
// ПРОТОТИПЫ
// =============================================================================

void initHardware();
void initNetwork();
void loadSettings();
void runTasks();
void resetWiFiAndRestart(); // Сброс WiFi настроек и перезагрузка
static void showBootStage(const char* message);

// =============================================================================
// BUZZER HELPER
// =============================================================================

namespace Buzzer {
void beep(uint8_t count, uint16_t duration) {
  for (uint8_t i = 0; i < count; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(duration);
    digitalWrite(PIN_BUZZER, LOW);
    if (i < count - 1)
      delay(duration);
  }
}

void alarm() {
  // Непрерывный сигнал - управляется в safety.cpp
}
} // namespace Buzzer

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  // Serial - инициализация USB CDC для ESP32-S3
  Serial.begin(115200);
  
  // Для ESP32-S3 USB CDC нужно подождать инициализацию USB
  // На ESP32-S3 Serial всегда возвращает true, даже если USB не подключен
  // Уменьшена задержка для более быстрого вывода
  delay(500); // Минимальная задержка для инициализации USB CDC
  
  // Выводим стартовое сообщение сразу
  Serial.println("\n\n=============================");
  Serial.println("Smart-Column S3 - Starting...");
  Serial.println("=============================");
  Serial.flush();

  // Проверка причины перезагрузки
  esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_WDT || resetReason == ESP_RST_TASK_WDT || 
      resetReason == ESP_RST_INT_WDT) {
    Serial.println("WARNING: Previous reset was due to Watchdog!");
    Serial.println("This may indicate a problem during initialization.");
    delay(2000); // Дать время прочитать сообщение
  }

  // WatchDog Timer - защита от зависаний
  // Увеличен таймаут до 60 сек для длительной инициализации
  esp_task_wdt_init(60, true); // 60 сек таймаут, паника при срабатывании
  esp_task_wdt_add(NULL);      // Регистрация главной задачи

  LOG_I("=================================");
  LOG_I("%s v%s", FW_NAME, FW_VERSION);
  LOG_I("Build: %s", FW_DATE);
  LOG_I("=================================");

  // Инициализация состояния
  memset(&g_state, 0, sizeof(g_state));
  g_state.mode = Mode::IDLE;
  g_state.rectPhase = RectPhase::IDLE;
  g_state.safetyOk = true;

  // Инициализация истории энергопотребления
  memset(&g_energyHistory, 0, sizeof(g_energyHistory));

  // LittleFS
  if (!LittleFS.begin(true)) {
    LOG_E("LittleFS mount failed!");
  } else {
    LOG_I("LittleFS: %d KB used / %d KB total", LittleFS.usedBytes() / 1024,
          LittleFS.totalBytes() / 1024);
  }

  // NVS - загрузка настроек
  LOG_I("Loading settings...");
  NVSManager::init();
  
  // Проверка сброса WiFi - если зажата кнопка BACK при загрузке
  if (PIN_BUTTON_BACK >= 0) {
    pinMode(PIN_BUTTON_BACK, INPUT_PULLUP);
    delay(50); // Дать время на стабилизацию
    if (!digitalRead(PIN_BUTTON_BACK)) {
      // Кнопка зажата - сброс WiFi настроек
      LOG_W("RESET: Button BACK pressed - resetting WiFi settings!");
      resetWiFiAndRestart();
    }
  }
  
  loadSettings();

  // Инициализация железа
  LOG_I("Initializing hardware...");
  initHardware();
  showBootStage("Hardware initialized");
  
  // ВРЕМЕННО ОТКЛЮЧЕНО: Насос отключен для диагностики перегрева
  // Pump::init(); вызывается в initHardware(), но можно отключить здесь
  // Если ESP не греется без насоса - проблема в подключении двигателя
  
  esp_task_wdt_reset(); // Сброс watchdog после инициализации железа

  // Сеть
  LOG_I("Initializing network...");
  showBootStage("Network init...");
  initNetwork();
  showBootStage("Network ready");
  esp_task_wdt_reset(); // Сброс watchdog после инициализации сети

#if WEB_SERVER_ENABLED
  // Веб-сервер
  LOG_I("Starting web server...");
  showBootStage("Web server...");
  WebServer::init();
  showBootStage("Web server ready");
  esp_task_wdt_reset(); // Сброс watchdog после инициализации веб-сервера
#endif

#if NETWORK_SERVICES_ENABLED
  // Cloud tunnel (IoT, исходящее соединение)
  CloudTunnel::init();

  // Telegram
  if (g_settings.telegram.enabled) {
    LOG_I("Starting Telegram bot...");
    TelegramBot::init(g_settings.telegram.token, g_settings.telegram.chatId);
  }

  // OTA Updates (только если WiFi подключён)
  if (WiFi.status() == WL_CONNECTED || g_settings.wifi.apMode) {
    LOG_I("Starting OTA...");
    OTA::init();
    // Опционально: установить пароль для защиты
    // OTA::setPassword("your_password_here");
  }

  // MQTT (только если включён и WiFi подключён)
  if (g_settings.mqtt.enabled && WiFi.status() == WL_CONNECTED) {
    LOG_I("Starting MQTT...");
    MQTT::init(g_settings.mqtt.server, g_settings.mqtt.port,
               g_settings.mqtt.username[0] ? g_settings.mqtt.username : nullptr,
               g_settings.mqtt.password[0] ? g_settings.mqtt.password
                                           : nullptr);
    if (g_settings.mqtt.baseTopic[0]) {
      MQTT::setBaseTopic(g_settings.mqtt.baseTopic);
    }
  }
#endif

  // Логгер
  Logger::init();
  Logger::log(LogEvent{millis(), 0, "System started"});
  esp_task_wdt_reset(); // Сброс watchdog перед завершением setup()

  // Готово
  LOG_I("=================================");
  LOG_I("System ready!");
  showBootStage("System ready");
  LOG_I("IP: %s", WiFi.localIP().toString().c_str());
  LOG_I("=================================");

  // Switch from BOOT status to the main UI immediately after setup.
  Display::update(g_state);
  g_lastDisplayUpdate = millis();

  // Звуковой сигнал готовности
  if (g_settings.soundEnabled) {
    Buzzer::beep(2, BUZZER_DURATION_SHORT);
  }
}

static void showBootStage(const char* message) {
#ifdef DISPLAY_ENABLED
  Display::showMessage("BOOT", message, 0);
#endif
}

// =============================================================================
// LOOP
// =============================================================================

#if PUMP_TASK_ENABLED
static TaskHandle_t g_pumpTaskHandle = nullptr;

static void pumpTask(void *param) {
  (void)param;
  for (;;) {
    Pump::update();
    vTaskDelay(pdMS_TO_TICKS(PUMP_TASK_DELAY_MS));
  }
}
#endif

void loop() {
  uint32_t now = millis();

#if PUMP_TEST_MODE
  // Минимальный тест насоса: постоянные импульсы STEP без остальной логики
  static bool pumpStarted = false;
  if (!pumpStarted) {
    Pump::start(Pump::getMaxSpeedMlH() * 0.5f);
    pumpStarted = true;
  }
#if TEST_ENABLE_OTA
  OTA::handle();
  if (OTA::isUpdating()) {
    return;
  }
#endif
#if TEST_ENABLE_SAFETY
  if (now - g_lastSafetyCheck >= INTERVAL_SAFETY_CHECK) {
    g_lastSafetyCheck = now;
    Safety::check(g_state, g_settings);
  }
#endif
#if TEST_ENABLE_SENSORS
#if TEST_ENABLE_TEMP_SENSORS
  if (now - g_lastTempRead >= INTERVAL_TEMP_READ) {
    g_lastTempRead = now;
    Sensors::readTemperatures(g_state.temps);
  }
#endif
#if TEST_ENABLE_PRESSURE
  if (now - g_lastPressureRead >= INTERVAL_PRESSURE_READ) {
    g_lastPressureRead = now;
    Sensors::readPressure(g_state.pressure);
  }
#endif
#if TEST_ENABLE_HYDROMETER
  if (now - g_lastPressureRead >= INTERVAL_PRESSURE_READ) {
    g_lastPressureRead = now;
    Sensors::readHydrometer(g_state.hydrometer, g_state.temps.columnTop);
  }
#endif
#if TEST_ENABLE_POWER
  if (now - g_lastPowerRead >= INTERVAL_POWER_READ) {
    g_lastPowerRead = now;
    Sensors::readPower(g_state.power);
  }
#endif
#endif
#if TEST_ENABLE_DISPLAY
  if (now - g_lastDisplayUpdate >= INTERVAL_DISPLAY_UPDATE) {
    g_lastDisplayUpdate = now;
    Display::update(g_state);
  }
#endif
#if TEST_ENABLE_WEBSOCKET
  if (now - g_lastWebBroadcast >= INTERVAL_WEB_BROADCAST) {
    g_lastWebBroadcast = now;
    WebServer::broadcastState(g_state);
  }
#endif
#if TEST_ENABLE_CLOUD
  CloudTunnel::loop();
#endif
#if TEST_ENABLE_LOGGER
  if (g_state.mode != Mode::IDLE &&
      now - g_lastLogWrite >= INTERVAL_LOG_WRITE) {
    g_lastLogWrite = now;
    Logger::writeData(g_state);
  }
#endif
  Pump::update();
#if TEST_ENABLE_BUTTONS
  Buttons::update();
#endif
#if TEST_ENABLE_TELEGRAM
  TelegramBot::update();
#endif
#if TEST_ENABLE_MQTT
  if (g_settings.mqtt.enabled) {
    MQTT::handle();
  }
#endif
  g_state.uptime = now / 1000;
  esp_task_wdt_reset();
  delay(1);
  return;
#endif

  // Обработка команд через Serial (для сброса WiFi)
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    
    if (cmd == "RESETWIFI" || cmd == "RESET WIFI") {
      resetWiFiAndRestart();
    } else if (cmd == "HELP" || cmd == "?") {
      Serial.println("\n=== Available Commands ===");
      Serial.println("RESETWIFI - Reset WiFi settings and restart");
      Serial.println("HELP or ? - Show this help");
    }
  }

  // OTA Updates (наивысший приоритет)
#if NETWORK_SERVICES_ENABLED
  OTA::handle();

  // Если идёт обновление OTA - пропустить всё остальное
  if (OTA::isUpdating()) {
    return;
  }
#endif

  // Проверка безопасности (высший приоритет)
  if (now - g_lastSafetyCheck >= INTERVAL_SAFETY_CHECK) {
    g_lastSafetyCheck = now;
    Safety::check(g_state, g_settings);
  }

  // Чтение температур
  if (now - g_lastTempRead >= INTERVAL_TEMP_READ) {
    g_lastTempRead = now;
    Sensors::readTemperatures(g_state.temps);
  }

  // Чтение давления
  if (now - g_lastPressureRead >= INTERVAL_PRESSURE_READ) {
    g_lastPressureRead = now;
    Sensors::readPressure(g_state.pressure);
    Sensors::readHydrometer(g_state.hydrometer, g_state.temps.columnTop);
  }

  // Чтение мощности
  if (now - g_lastPowerRead >= INTERVAL_POWER_READ) {
    g_lastPowerRead = now;
    Sensors::readPower(g_state.power);
  }

  // Демо-симулятор (перезаписывает данные сенсоров если demoMode=true)
  if (g_settings.demoMode) {
    DemoSimulator::update(g_state, g_settings);
  }

  // FSM - конечный автомат режимов
  if (g_state.safetyOk && !g_state.paused) {
    FSM::update(g_state, g_settings);
  }

  // Обновление дисплея
  if (now - g_lastDisplayUpdate >= INTERVAL_DISPLAY_UPDATE) {
    g_lastDisplayUpdate = now;
    Display::update(g_state);
  }

  // WebSocket broadcast
#if WEB_SERVER_ENABLED
  if (now - g_lastWebBroadcast >= INTERVAL_WEB_BROADCAST) {
    g_lastWebBroadcast = now;
    WebServer::broadcastState(g_state);
  }
#endif

  // Cloud tunnel loop
#if NETWORK_SERVICES_ENABLED
  CloudTunnel::loop();
#endif

  // Логирование
  if (g_state.mode != Mode::IDLE &&
      now - g_lastLogWrite >= INTERVAL_LOG_WRITE) {
    g_lastLogWrite = now;
    Logger::writeData(g_state);
  }

  // Запись истории энергопотребления (каждые 5 минут)
  static uint32_t lastEnergyLog = 0;
  if (now - lastEnergyLog >= 300000) { // 5 минут
    lastEnergyLog = now;

    // Добавить точку данных в циклический буфер
    EnergyDataPoint &point = g_energyHistory.points[g_energyHistory.writeIndex];
    point.timestamp = now / 1000; // Секунды с запуска
    point.power = g_state.power.power;
    point.energy = g_state.power.energy;
    point.voltage = g_state.power.voltage;
    point.current = g_state.power.current;

    // Обновить индексы
    g_energyHistory.writeIndex =
        (g_energyHistory.writeIndex + 1) % EnergyHistory::MAX_POINTS;
    if (g_energyHistory.count < EnergyHistory::MAX_POINTS) {
      g_energyHistory.count++;
    }
    g_energyHistory.lastUpdate = now;

    LOG_D("Energy: %.1fW, %.3fkWh logged", point.power, point.energy);
  }

  // Обновление насоса (генерация шагов - ОБЯЗАТЕЛЬНО каждый цикл!)
#if !PUMP_TASK_ENABLED
  Pump::update();
#endif

  // Синхронизация состояния насоса в SystemState (для Web/API/UI)
  g_state.pump.running = Pump::isRunning();
  g_state.pump.speedMlPerHour = Pump::getSpeed();
  g_state.pump.totalVolumeMl = Pump::getTotalVolume();

  // Обработка кнопок
  Buttons::update();

  // Telegram
#if NETWORK_SERVICES_ENABLED
  TelegramBot::update();

  // MQTT
  if (g_settings.mqtt.enabled) {
    MQTT::handle();
  }
#endif

  // Обновление uptime
  g_state.uptime = now / 1000;

  // Обновление здоровья системы (раз в 5 секунд)
  static uint32_t lastHealthUpdate = 0;
  static uint8_t lastHealthStatus = 100;
  static bool healthAlertSent = false;

  if (now - lastHealthUpdate >= 5000) {
    lastHealthUpdate = now;
    Sensors::updateHealth(g_state.health);

    // Telegram уведомление при падении здоровья ниже 80%
    if (g_settings.telegram.enabled) {
      if (g_state.health.overallHealth < 80 && !healthAlertSent) {
        TelegramBot::notifyHealthAlert(g_state.health);
        healthAlertSent = true;
        LOG_W("Health alert sent to Telegram: %d%%",
              g_state.health.overallHealth);
      }
      // Сброс флага если здоровье восстановилось выше 90%
      else if (g_state.health.overallHealth >= 90 && healthAlertSent) {
        healthAlertSent = false;
        TelegramBot::sendMessage("✅ System health restored");
        LOG_I("Health restored: %d%%", g_state.health.overallHealth);
      }
    }

    lastHealthStatus = g_state.health.overallHealth;

    // MQTT публикация здоровья (раз в 5 секунд)
    if (g_settings.mqtt.enabled && MQTT::isConnected()) {
      MQTT::publishHealth(g_state.health);
    }
  }

  // MQTT публикация состояния (интервал из настроек)
  if (g_settings.mqtt.enabled && MQTT::isConnected()) {
    static uint32_t lastMqttPublish = 0;
    uint32_t interval = g_settings.mqtt.publishInterval > 0
                            ? g_settings.mqtt.publishInterval
                            : 10000;

    if (now - lastMqttPublish >= interval) {
      lastMqttPublish = now;
      MQTT::publishState(g_state);
    }
  }

  // Сброс WatchDog Timer (подтверждение работы)
  esp_task_wdt_reset();

  // Yield для WiFi
  yield();

  // При генерации шагов насоса в основном loop нельзя спать долго:
  // AccelStepper::runSpeed() требует частых вызовов.
#if !PUMP_TASK_ENABLED
  const uint32_t loopDelayMs = Pump::isRunning() ? 1 : 10;
#else
  const uint32_t loopDelayMs = 10;
#endif
  delay(loopDelayMs);
}

// =============================================================================
// ИНИЦИАЛИЗАЦИЯ ЖЕЛЕЗА
// =============================================================================

void initHardware() {
  // I2C
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  // Датчики
  Sensors::init();

  // Нагреватель
  Heater::init();

  // Насос
  Pump::init();
  Pump::setCalibration(g_settings.pumpCal.mlPerRevolution);

#if PUMP_TASK_ENABLED
  if (g_pumpTaskHandle == nullptr) {
    xTaskCreatePinnedToCore(pumpTask, "PumpTask", 4096, nullptr, 3,
                            &g_pumpTaskHandle, PUMP_TASK_CORE);
  }
#endif

  // Клапаны
  Valves::init();

  // Фракционник
  if (g_settings.fractionator.enabled) {
    Valves::initFractionator();
  }

  // Дисплей
  Display::init();
  if (Display::needsTouchCalibration()) {
    Display::startTouchCalibration();
  }
  // Показать первый экран сразу после инициализации,
  // чтобы не оставаться на пустом чёрном экране во время загрузки.

  // Кнопки
  Buttons::init();

  // Зуммер
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
}

// =============================================================================
// ИНИЦИАЛИЗАЦИЯ СЕТИ
// =============================================================================

void initNetwork() {
#if FORCE_AP_MODE
  g_settings.wifi.apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  LOG_I("AP started (diag): %s, IP: %s", WIFI_AP_SSID,
        WiFi.softAPIP().toString().c_str());
#else
  // AP поднимаем всегда для гарантированного локального доступа по 192.168.4.1.
  // STA подключаем параллельно, если есть сохранённые креды.
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);

  IPAddress apIp(192, 168, 4, 1);
  IPAddress apGw(192, 168, 4, 1);
  IPAddress apMask(255, 255, 255, 0);
  if (!WiFi.softAPConfig(apIp, apGw, apMask)) {
    LOG_W("AP config failed, using default AP network");
  }

  const bool apStarted = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  g_settings.wifi.apMode = apStarted;
  if (apStarted) {
    LOG_I("AP started: %s, IP: %s", WIFI_AP_SSID,
          WiFi.softAPIP().toString().c_str());
  } else {
    LOG_E("AP start failed for SSID: %s", WIFI_AP_SSID);
  }

  // Попытка подключения к внешнему WiFi (STA)
  if (strlen(g_settings.wifi.ssid) > 0) {
    LOG_I("Connecting to WiFi: %s", g_settings.wifi.ssid);
    WiFi.begin(g_settings.wifi.ssid, g_settings.wifi.password);

    uint32_t startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS) {
      delay(100);
      Serial.print(".");
      esp_task_wdt_reset(); // Сброс watchdog во время ожидания WiFi
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      LOG_I("WiFi connected! STA IP: %s", WiFi.localIP().toString().c_str());
    } else {
      LOG_W("WiFi connection failed, AP-only access remains available");
    }
  }
#endif

  // mDNS - доступ по smart-column.local
  if (MDNS.begin("smart-column")) {
    LOG_I("mDNS responder started: smart-column.local");

    // Объявление HTTP сервиса
    MDNS.addService("http", "tcp", 80);

    // Дополнительная информация
    MDNS.addServiceTxt("http", "tcp", "version", FW_VERSION);
    MDNS.addServiceTxt("http", "tcp", "board", "ESP32-S3");
  } else {
    LOG_E("mDNS initialization failed");
  }
}

// =============================================================================
// ЗАГРУЗКА НАСТРОЕК
// =============================================================================

void loadSettings() {
  // Настройки по умолчанию
  memset(&g_settings, 0, sizeof(g_settings));

  // Оборудование
  g_settings.equipment.columnHeightMm = DEFAULT_COLUMN_HEIGHT_MM;
  g_settings.equipment.packingType = PackingType::SPN_3_5;
  g_settings.equipment.packingCoeff = DEFAULT_PACKING_COEFF;
  g_settings.equipment.heaterPowerW = DEFAULT_HEATER_POWER_W;
  g_settings.equipment.cubeVolumeL = DEFAULT_CUBE_VOLUME_L;
  g_settings.equipment.minHeaterSubmergeL = DEFAULT_MIN_HEATER_SUBMERGE_L;
  g_settings.equipment.waterAutoStartCubeTempC = DEFAULT_WATER_AUTOSTART_CUBE_TEMP_C;

  // Насос
  g_settings.pumpCal.mlPerRevolution = DEFAULT_PUMP_ML_PER_REV;
  g_settings.pumpCal.stepsPerRevolution = PUMP_STEPS_PER_REV;
  g_settings.pumpCal.microsteps = PUMP_MICROSTEPS;

  // Тач (по умолчанию считаем неоткалиброванным)
  g_settings.touchCal.xMin = TOUCH_CAL_X_MIN;
  g_settings.touchCal.xMax = TOUCH_CAL_X_MAX;
  g_settings.touchCal.yMin = TOUCH_CAL_Y_MIN;
  g_settings.touchCal.yMax = TOUCH_CAL_Y_MAX;
  g_settings.touchCal.valid = false;

  // PZEM-004T не требует калибровки - уже откалиброван на заводе

  // Ректификация
  g_settings.rectParams.headsPercent = RECT_HEADS_PERCENT_DEFAULT;
  g_settings.rectParams.headsSpeedMlHKw = RECT_HEADS_SPEED_ML_H_KW;
  g_settings.rectParams.bodySpeedMlHKw = RECT_HEADS_SPEED_ML_H_KW * 2;
  g_settings.rectParams.stabilizationMin = RECT_STABILIZATION_TIME_MIN;
  g_settings.rectParams.purgeMin = RECT_PURGE_TIME_MIN;

  // Фракционник - все позиции по умолчанию
  g_settings.fractionator.enabled = false;
  g_settings.fractionator.angles[0] = FRACTION_ANGLE_HEADS;
  g_settings.fractionator.angles[1] = FRACTION_ANGLE_SUBHEADS;
  g_settings.fractionator.angles[2] = FRACTION_ANGLE_BODY;
  g_settings.fractionator.angles[3] = FRACTION_ANGLE_PRETAILS;
  g_settings.fractionator.angles[4] = FRACTION_ANGLE_TAILS;
  // Только основные фракции включены по умолчанию
  g_settings.fractionator.positionsEnabled[0] = true;  // Головы
  g_settings.fractionator.positionsEnabled[1] = false; // Подголовники - выкл
  g_settings.fractionator.positionsEnabled[2] = true;  // Тело
  g_settings.fractionator.positionsEnabled[3] = false; // Предхвостье - выкл
  g_settings.fractionator.positionsEnabled[4] = true;  // Хвосты

  // Прочее
  g_settings.language = 0; // RU
  g_settings.theme = 0;    // Light
  g_settings.soundEnabled = true;

  // Загрузка из NVS (перезапишет дефолты)
  NVSManager::loadSettings(g_settings);

  LOG_I("Settings loaded");
}

// =============================================================================
// СБРОС WiFi И ПЕРЕЗАГРУЗКА
// =============================================================================

void resetWiFiAndRestart() {
  Serial.println("\n========================================");
  Serial.println("WiFi RESET: Clearing WiFi settings...");
  Serial.println("========================================");

  // Очистить только WiFi настройки из NVS
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.remove(NVS_KEY_WIFI_SSID);
  prefs.remove(NVS_KEY_WIFI_PASS);
  prefs.end();

  Serial.println("WiFi settings cleared! Restarting...");
  delay(1000);
  ESP.restart();
}
