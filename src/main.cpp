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


#include "config.h"
#include "types.h"

// Драйверы
#include "drivers/display.h"
#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/sensors.h"
#include "drivers/valves.h"


// Управление
#include "control/fsm.h"
#include "control/safety.h"
#include "control/watt_control.h"


// Интерфейсы
#include "interface/buttons.h"
#include "interface/mqtt.h"
#include "interface/ota.h"
#include "interface/telegram.h"
#include "interface/webserver.h"


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
  // Serial - увеличиваем задержку для стабильности USB CDC
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10); // Ждём подключения USB CDC до 3 секунд
  }
  delay(500); // Дополнительная задержка для стабильности

  Serial.println("\n\n=============================");
  Serial.println("Smart-Column S3 - Starting...");
  Serial.println("=============================");
  Serial.flush();

  // WatchDog Timer - защита от зависаний
  esp_task_wdt_init(30, true); // 30 сек таймаут, паника при срабатывании
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

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    LOG_E("SPIFFS mount failed!");
  } else {
    LOG_I("SPIFFS: %d KB used / %d KB total", SPIFFS.usedBytes() / 1024,
          SPIFFS.totalBytes() / 1024);
  }

  // NVS - загрузка настроек
  LOG_I("Loading settings...");
  NVSManager::init();
  loadSettings();

  // Инициализация железа
  LOG_I("Initializing hardware...");
  initHardware();

  // Сеть
  LOG_I("Initializing network...");
  initNetwork();

  // Веб-сервер
  LOG_I("Starting web server...");
  WebServer::init();

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

  // Логгер
  Logger::init();
  Logger::log(LogEvent{millis(), 0, "System started"});

  // Готово
  LOG_I("=================================");
  LOG_I("System ready!");
  LOG_I("IP: %s", WiFi.localIP().toString().c_str());
  LOG_I("=================================");

  // Звуковой сигнал готовности
  if (g_settings.soundEnabled) {
    Buzzer::beep(2, BUZZER_DURATION_SHORT);
  }
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  uint32_t now = millis();

  // OTA Updates (наивысший приоритет)
  OTA::handle();

  // Если идёт обновление OTA - пропустить всё остальное
  if (OTA::isUpdating()) {
    return;
  }

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
  if (now - g_lastWebBroadcast >= INTERVAL_WEB_BROADCAST) {
    g_lastWebBroadcast = now;
    WebServer::broadcastState(g_state);
  }

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

  // Обработка кнопок
  Buttons::update();

  // Telegram
  TelegramBot::update();

  // MQTT
  if (g_settings.mqtt.enabled) {
    MQTT::handle();
  }

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

  // Клапаны
  Valves::init();

  // Фракционник
  if (g_settings.fractionator.enabled) {
    Valves::initFractionator();
  }

  // Дисплей
  Display::init();

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
  // Попытка подключения к WiFi
  if (strlen(g_settings.wifi.ssid) > 0) {
    LOG_I("Connecting to WiFi: %s", g_settings.wifi.ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(g_settings.wifi.ssid, g_settings.wifi.password);

    uint32_t startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS) {
      delay(100);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      LOG_I("WiFi connected! IP: %s", WiFi.localIP().toString().c_str());
    } else {
      LOG_E("WiFi connection failed, starting AP mode");
      g_settings.wifi.apMode = true;
    }
  } else {
    g_settings.wifi.apMode = true;
  }

  // AP режим если нет подключения
  if (g_settings.wifi.apMode) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    LOG_I("AP started: %s, IP: %s", WIFI_AP_SSID,
          WiFi.softAPIP().toString().c_str());
  }

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

  // Насос
  g_settings.pumpCal.mlPerRevolution = DEFAULT_PUMP_ML_PER_REV;
  g_settings.pumpCal.stepsPerRevolution = PUMP_STEPS_PER_REV;
  g_settings.pumpCal.microsteps = PUMP_MICROSTEPS;

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
