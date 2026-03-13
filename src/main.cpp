/**
 * Smart-Column S3 - Главный файл
 *
 * Контроллер автоматизации ректификационной колонны
 * Платформа: ESP32-S3 DevKitC-1 N16R8
 */

#include "fs_compat.h"
#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

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
#include "interface/wifi_profiles.h"

// Хранение
#include "storage/logger.h"
#include "storage/nvs_manager.h"

// =============================================================================
// ГЛОБАЛЬНЫЕ ОБЪЕКТЫ
// =============================================================================

SystemState g_state;           // Текущее состояние системы
Settings g_settings;           // Настройки (из NVS)
EnergyHistory g_energyHistory; // История энергопотребления

// Очередь для неблокирующего зуммера (Analysis Step 1)
struct BuzzerCmd {
  uint8_t count;
  uint16_t duration;
};
static QueueHandle_t g_buzzerQueue = nullptr;

// Captive portal DNS (активен только при первом запуске без WiFi)
static DNSServer g_dnsServer;
static bool g_captivePortalActive = false;

// Таймеры задач
uint32_t g_lastTempRead = 0;
uint32_t g_lastPressureRead = 0;
uint32_t g_lastPowerRead = 0;
uint32_t g_lastDisplayUpdate = 0;
uint32_t g_lastWebBroadcast = 0;
uint32_t g_lastLogWrite = 0;
uint32_t g_lastSafetyCheck = 0;
uint32_t g_lastSelfCheck = 0;   // #14: self-check лог каждые 30 мин

// =============================================================================
// ПРОТОТИПЫ
// =============================================================================

void initHardware();
void initNetwork();
void loadSettings();
void runTasks();
void resetWiFiAndRestart(); 
static void showBootStage(const char* message);
static void handleLoggerLifecycle(uint32_t now);
void buzzerTask(void* pvParameters);

// =============================================================================
// BUZZER HELPER
// =============================================================================

namespace Buzzer {
void beep(uint8_t count, uint16_t duration) {
  if (g_buzzerQueue == nullptr) return;
  BuzzerCmd cmd = {count, duration};
  xQueueSend(g_buzzerQueue, &cmd, 0);
}
} // namespace Buzzer

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(500); 
  
  Serial.println("\n\n=============================");
  Serial.println("Smart-Column S3 - Starting...");
  Serial.println("=============================");
  Serial.flush();

  // 1. Инициализация состояния (ОБЯЗАТЕЛЬНО ПЕРЕД ВСЕМ ОСТАЛЬНЫМ)
  memset(&g_state, 0, sizeof(g_state));
  g_state.mode = Mode::IDLE;
  g_state.rectPhase = RectPhase::IDLE;
  g_state.safetyOk = true;

  // 2. Проверка причины перезагрузки
  esp_reset_reason_t resetReason = esp_reset_reason();
  g_state.health.lastRebootReason = (uint8_t)resetReason;
  
  if (resetReason == ESP_RST_WDT || resetReason == ESP_RST_TASK_WDT || 
      resetReason == ESP_RST_INT_WDT) {
    Serial.println("WARNING: Previous reset was due to Watchdog!");
  }

  // 3. WatchDog Timer
  esp_task_wdt_init(60, true); 
  esp_task_wdt_add(NULL);

  // 4. Очередь зуммера (Analysis Step 1)
  g_buzzerQueue = xQueueCreate(10, sizeof(BuzzerCmd));
  xTaskCreate(buzzerTask, "buzzer", 2048, NULL, 1, NULL);

  // 5. Инициализация модуля безопасности (Analysis Step 2)
  Safety::init();

  // 6. Файловая система
  if (!LittleFS.begin(true)) {
    LOG_E("LittleFS mount failed!");
  }

  // 7. NVS и Настройки
  NVSManager::init();
  loadSettings();

  // Сохранить причину перезагрузки, если изменилась
  if (g_settings.lastRebootReason != g_state.health.lastRebootReason) {
    g_settings.lastRebootReason = g_state.health.lastRebootReason;
    NVSManager::saveSettings(g_settings);
    LOG_I("Reset reason updated in NVS: %d", g_settings.lastRebootReason);
  }

  // 8. Инициализация железа
  initHardware();
  showBootStage("Hardware initialized");
  esp_task_wdt_reset();

  // 9. Сеть
  initNetwork();
  showBootStage("Network ready");
  esp_task_wdt_reset();

  // 10. Сервисы
#if WEB_SERVER_ENABLED
  WebServer::init();
#endif

  CloudTunnel::init();

  if (g_settings.telegram.enabled) {
    LOG_I("Starting Telegram bot...");
    TelegramBot::init(g_settings.telegram.token, g_settings.telegram.chatId);
  }

  if (WiFi.status() == WL_CONNECTED || g_settings.wifi.apMode) {
    OTA::init();
  }

  if (g_settings.mqtt.enabled && g_settings.mqtt.server[0] != '\0') {
    MQTT::setBaseTopic(g_settings.mqtt.baseTopic);
    MQTT::init(g_settings.mqtt.server, g_settings.mqtt.port,
               g_settings.mqtt.username[0] ? g_settings.mqtt.username : nullptr,
               g_settings.mqtt.password[0] ? g_settings.mqtt.password : nullptr);
  }

  Logger::init();
  esp_task_wdt_reset();

  LOG_I("System ready!");
  Logger::logf(0, "System ready: firmware %s", FW_VERSION);
  
  Display::update(g_state);
  g_lastDisplayUpdate = millis();

  if (g_settings.soundEnabled) {
    Buzzer::beep(2, 50);
  }
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  if (g_captivePortalActive) {
    g_dnsServer.processNextRequest();
  }

  uint32_t now = millis();

  // OTA Updates
  OTA::handle();
  if (OTA::isUpdating()) return;

  // Демо-симулятор
  if (g_settings.demoMode) {
    DemoSimulator::update(g_state, g_settings);
  }

  // Безопасность
  if (now - g_lastSafetyCheck >= INTERVAL_SAFETY_CHECK) {
    g_lastSafetyCheck = now;
    Safety::check(g_state, g_settings);
  }

  // Чтение сенсоров (только если не в демо-режиме)
  if (!g_settings.demoMode) {
    if (now - g_lastTempRead >= INTERVAL_TEMP_READ) {
      g_lastTempRead = now;
      Sensors::readTemperatures(g_state.temps);
    }
    if (now - g_lastPressureRead >= INTERVAL_PRESSURE_READ) {
      g_lastPressureRead = now;
      Sensors::readPressure(g_state.pressure);
      Sensors::readHydrometer(g_state.hydrometer, g_state.temps.columnTop);
    }
    if (now - g_lastPowerRead >= INTERVAL_POWER_READ) {
      g_lastPowerRead = now;
      Sensors::readPower(g_state.power);
    }
  }

  // FSM - Логика режимов
  if (g_state.safetyOk && !g_state.paused) {
    FSM::update(g_state, g_settings);
  }

  // Интерфейсы
  if (now - g_lastDisplayUpdate >= INTERVAL_DISPLAY_UPDATE) {
    g_lastDisplayUpdate = now;
    Display::update(g_state);
  }

#if WEB_SERVER_ENABLED
  if (now - g_lastWebBroadcast >= INTERVAL_WEB_BROADCAST) {
    g_lastWebBroadcast = now;
    WebServer::broadcastState(g_state);
  }
#endif

  CloudTunnel::loop();
  
  handleLoggerLifecycle(now);
  if (Logger::isSessionActive() && now - g_lastLogWrite >= INTERVAL_LOG_WRITE) {
    g_lastLogWrite = now;
    Logger::writeData(g_state);
  }

  Pump::update();
  Heater::update();     // PERF-3 fix: плавный разгон ТЭНа (ramp) был реализован, но не вызывался
  Valves::update();     // ARCH-3 fix: неблокирующий движок сервопривода фракционника
  g_state.pump.running = Pump::isRunning();
  g_state.pump.speedMlPerHour = Pump::getSpeed();
  g_state.pump.totalVolumeMl = Pump::getTotalVolume();

  Buttons::update();
  TelegramBot::update();

  if (g_settings.mqtt.enabled) {
    MQTT::handle();
  }

  g_state.uptime = now / 1000;
  
  // Здоровье системы
  static uint32_t lastHealthUpdate = 0;
  if (now - lastHealthUpdate >= 5000) {
    lastHealthUpdate = now;
    Sensors::updateHealth(g_state.health);
    if (g_settings.mqtt.enabled) MQTT::publishHealth(g_state.health);
  }

  // #14: Self-check лог: heap, uptime, ошибки — каждые 30 минут
  static const uint32_t SELF_CHECK_INTERVAL_MS = 30UL * 60UL * 1000UL;
  if (now - g_lastSelfCheck >= SELF_CHECK_INTERVAL_MS) {
    g_lastSelfCheck = now;
    const char* resetStr = "Other";
    switch (g_state.health.lastRebootReason) {
      case 1: resetStr = "PowerOn"; break;
      case 3: resetStr = "SWD WDT"; break;
      case 4: resetStr = "HWD WDT"; break;
      case 5: resetStr = "DeepSleep"; break;
      case 6: resetStr = "SW Reset"; break;
      case 7: resetStr = "Panic"; break;
    }
    Logger::logf(0, "[SELFCHECK] Heap:%uKB Uptime:%us Reboot:%s TempErr:%u PzemSpk:%u",
      g_state.health.freeHeap / 1024,
      (unsigned)g_state.uptime,
      resetStr,
      g_state.health.tempReadErrors,
      g_state.health.pzemSpikeCount);
    LOG_I("[SELFCHECK] Heap:%uKB Uptime:%us Reboot:%s TempErr:%u PzemSpk:%u",
      g_state.health.freeHeap / 1024,
      (unsigned)g_state.uptime,
      resetStr,
      g_state.health.tempReadErrors,
      g_state.health.pzemSpikeCount);
  }

  esp_task_wdt_reset();
  yield();
  delay(Pump::isRunning() ? 1 : 10);
}

// =============================================================================
// РЕАЛИЗАЦИЯ ИНИЦИАЛИЗАЦИИ
// =============================================================================

void initHardware() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  Sensors::init();
  Heater::init();
  Pump::init();
  Pump::setCalibration(g_settings.pumpCal.mlPerRevolution);
  Valves::init();
  if (g_settings.fractionator.enabled) Valves::initFractionator();
  Display::init();
  Buttons::init();
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
}

void initNetwork() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  
  if (WiFiProfiles::hasConfiguredProfiles(g_settings.wifi)) {
    WiFiProfiles::connectBestAvailable(g_settings.wifi, 10000);
  } else {
    g_dnsServer.start(53, "*", WiFi.softAPIP());
    g_captivePortalActive = true;
  }
  
  if (MDNS.begin("smart-column")) {
    MDNS.addService("http", "tcp", 80);
  }
}

void loadSettings() {
  memset(&g_settings, 0, sizeof(g_settings));
  g_settings.equipment.waterAutoStartCubeTempC = 45.0f;
  g_settings.safety.pressureMaxMmHg = 50.0f;
  NVSManager::loadSettings(g_settings);
}

void resetWiFiAndRestart() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.remove(NVS_KEY_WIFI_SSID);
  prefs.remove(NVS_KEY_WIFI_PASS);
  prefs.end();
  delay(1000);
  ESP.restart();
}

static void showBootStage(const char* message) {
#ifdef DISPLAY_ENABLED
  Display::showMessage("BOOT", message, 0);
#endif
}

static bool isProcessModeActive(Mode mode) {
  return mode != Mode::IDLE;
}

static void handleLoggerLifecycle(uint32_t now) {
  static Mode loggedMode = Mode::IDLE;
  if (g_state.mode == loggedMode) return;
  if (isProcessModeActive(loggedMode)) Logger::stopSession();
  if (isProcessModeActive(g_state.mode)) Logger::startSession();
  loggedMode = g_state.mode;
}

void buzzerTask(void* pvParameters) {
  BuzzerCmd cmd;
  for (;;) {
    if (xQueueReceive(g_buzzerQueue, &cmd, portMAX_DELAY) == pdTRUE) {
      for (uint8_t i = 0; i < cmd.count; i++) {
        digitalWrite(PIN_BUZZER, HIGH);
        vTaskDelay(pdMS_TO_TICKS(cmd.duration));
        digitalWrite(PIN_BUZZER, LOW);
        if (i < cmd.count - 1) {
          vTaskDelay(pdMS_TO_TICKS(cmd.duration));
        }
      }
    }
  }
}
