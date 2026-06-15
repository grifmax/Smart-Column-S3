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
#include "live_chart_history.h"
#include "drivers/stirrer.h"
#include "drivers/valves.h"

// Управление
#include "control/demo_simulator.h"
#include "control/fsm.h"
#include "control/safety.h"
#include "control/v2/status_adapter.h"
#include "control/watt_control.h"

// Интерфейсы
#include "interface/buttons.h"
#include "interface/mqtt.h"
#include "interface/ota.h"
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
RebootTracker g_rebootTracker; // Отслеживание перезагрузок (Analysis Step 1)

// Очередь для неблокирующего зуммера (Analysis Step 1)
BootGpioSelfTest g_bootGpioSelfTest; // Safe bring-up опасных GPIO до старта драйверов
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
static bool isWatchdogResetReason(esp_reset_reason_t reason);
static bool isCrashResetReason(esp_reset_reason_t reason);
static bool isUserResetReason(esp_reset_reason_t reason);
static const char* resetReasonToString(esp_reset_reason_t reason);
static void runBootGpioSelfTest();
static void registerBootOutputCheck(const char* label, int16_t pin, bool highLevel);
static void registerBootInputCheck(const char* label, int16_t pin, bool pullup,
                                   int8_t expectedLevel);

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

static bool isWatchdogResetReason(esp_reset_reason_t reason) {
  return reason == ESP_RST_WDT || reason == ESP_RST_TASK_WDT ||
         reason == ESP_RST_INT_WDT;
}

static bool isCrashResetReason(esp_reset_reason_t reason) {
  return reason == ESP_RST_PANIC;
}

static bool isUserResetReason(esp_reset_reason_t reason) {
  return reason == ESP_RST_SW || reason == ESP_RST_EXT;
}

static const char* resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "Power On";
    case ESP_RST_EXT: return "External Pin";
    case ESP_RST_SW: return "Software Reset";
    case ESP_RST_PANIC: return "Exception/Panic";
    case ESP_RST_INT_WDT: return "Interrupt WDT";
    case ESP_RST_TASK_WDT: return "Task WDT";
    case ESP_RST_WDT: return "Other WDT";
    case ESP_RST_DEEPSLEEP: return "Deep Sleep";
    case ESP_RST_BROWNOUT: return "Brownout";
    case ESP_RST_SDIO: return "SDIO Reset";
    default: return "Other";
  }
}

static void registerBootOutputCheck(const char* label, int16_t pin,
                                    bool highLevel) {
  if (g_bootGpioSelfTest.checkedCount >= BOOT_GPIO_CHECK_MAX) return;
  BootGpioCheckItem& item =
      g_bootGpioSelfTest.items[g_bootGpioSelfTest.checkedCount++];
  item.pin = pin;
  item.mode = highLevel ? 1 : 0;
  item.expectedLevel = highLevel ? HIGH : LOW;
  strlcpy(item.label, label, sizeof(item.label));

  if (pin < 0) {
    item.actualLevel = -1;
    item.ok = false;
    return;
  }

  pinMode(pin, OUTPUT);
  digitalWrite(pin, highLevel ? HIGH : LOW);
  delayMicroseconds(30);
  item.actualLevel = digitalRead(pin);
  item.ok = item.actualLevel == item.expectedLevel;
}

static void registerBootInputCheck(const char* label, int16_t pin, bool pullup,
                                   int8_t expectedLevel) {
  if (g_bootGpioSelfTest.checkedCount >= BOOT_GPIO_CHECK_MAX) return;
  BootGpioCheckItem& item =
      g_bootGpioSelfTest.items[g_bootGpioSelfTest.checkedCount++];
  item.pin = pin;
  item.mode = pullup ? 3 : 2;
  item.expectedLevel = expectedLevel;
  strlcpy(item.label, label, sizeof(item.label));

  if (pin < 0) {
    item.actualLevel = -1;
    item.ok = false;
    return;
  }

  pinMode(pin, pullup ? INPUT_PULLUP : INPUT);
  delayMicroseconds(30);
  item.actualLevel = digitalRead(pin);
  item.ok = expectedLevel < 0 ? true : item.actualLevel == expectedLevel;
}

static void runBootGpioSelfTest() {
  memset(&g_bootGpioSelfTest, 0, sizeof(g_bootGpioSelfTest));
  strlcpy(g_bootGpioSelfTest.boardRev, BOARD_REV_LABEL,
          sizeof(g_bootGpioSelfTest.boardRev));

  registerBootOutputCheck("Main heater SSR", PIN_SSR_HEATER, false);
  registerBootOutputCheck("TRIAC gate", PIN_TRIAC, false);
  registerBootOutputCheck("Pump step", PIN_PUMP_STEP, false);
  registerBootOutputCheck("Pump dir", PIN_PUMP_DIR, false);
  registerBootOutputCheck("Pump enable", PIN_PUMP_EN, true);
  registerBootOutputCheck("Valve water", PIN_VALVE_WATER, false);
  registerBootOutputCheck("Valve heads", PIN_VALVE_HEADS, false);
  registerBootOutputCheck("Valve UNO", PIN_VALVE_UNO, false);
  registerBootOutputCheck("Valve PWM", PIN_VALVE_STARTSTOP, false);
  registerBootOutputCheck("Buzzer", PIN_BUZZER, false);
  registerBootInputCheck("Zero-cross", PIN_ZERO_CROSS, true, -1);
  registerBootInputCheck("1-Wire", PIN_ONEWIRE, true, HIGH);

  g_bootGpioSelfTest.completed = true;
  g_bootGpioSelfTest.timestampMs = millis();
  g_bootGpioSelfTest.overallOk = true;
  for (uint8_t i = 0; i < g_bootGpioSelfTest.checkedCount; ++i) {
    if (!g_bootGpioSelfTest.items[i].ok) {
      g_bootGpioSelfTest.overallOk = false;
      break;
    }
  }

  LOG_I("Boot GPIO self-test: %s (%u checks, board %s)",
        g_bootGpioSelfTest.overallOk ? "OK" : "WARN",
        g_bootGpioSelfTest.checkedCount, g_bootGpioSelfTest.boardRev);
}

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
  runBootGpioSelfTest();

  // 2. Проверка причины перезагрузки
  esp_reset_reason_t resetReason = esp_reset_reason();
  g_state.health.lastRebootReason = (uint8_t)resetReason;
  g_rebootTracker.lastReason = (uint8_t)resetReason;
  strncpy(g_rebootTracker.lastReasonStr, resetReasonToString(resetReason),
          sizeof(g_rebootTracker.lastReasonStr) - 1);
  g_rebootTracker.lastReasonStr[sizeof(g_rebootTracker.lastReasonStr) - 1] = '\0';

  if (isWatchdogResetReason(resetReason)) {
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
  LiveChartHistory::init();

  // Сохранить причину перезагрузки, если изменилась
  g_rebootTracker.totalReboots = g_settings.rebootCountTotal + 1;
  g_rebootTracker.wdtReboots =
      g_settings.rebootCountWdt + (isWatchdogResetReason(resetReason) ? 1U : 0U);
  g_rebootTracker.crashReboots =
      g_settings.rebootCountCrash + (isCrashResetReason(resetReason) ? 1U : 0U);
  g_rebootTracker.userReboots =
      g_settings.rebootCountUser + (isUserResetReason(resetReason) ? 1U : 0U);

  const bool rebootStatsChanged =
      g_settings.lastRebootReason != g_state.health.lastRebootReason ||
      g_settings.rebootCountTotal != g_rebootTracker.totalReboots ||
      g_settings.rebootCountWdt != g_rebootTracker.wdtReboots ||
      g_settings.rebootCountCrash != g_rebootTracker.crashReboots ||
      g_settings.rebootCountUser != g_rebootTracker.userReboots;

  if (rebootStatsChanged) {
    g_settings.lastRebootReason = g_state.health.lastRebootReason;
    g_settings.rebootCountTotal = g_rebootTracker.totalReboots;
    g_settings.rebootCountWdt = g_rebootTracker.wdtReboots;
    g_settings.rebootCountCrash = g_rebootTracker.crashReboots;
    g_settings.rebootCountUser = g_rebootTracker.userReboots;
    NVSManager::saveSettings(g_settings);
    LOG_I("Reboot stats updated in NVS: reason=%d total=%u wdt=%u crash=%u user=%u",
          g_settings.lastRebootReason, g_settings.rebootCountTotal,
          g_settings.rebootCountWdt, g_settings.rebootCountCrash,
          g_settings.rebootCountUser);
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

// Улучшенная функция самоконтроля системы с event logging
static void performSystemHealthCheck(uint32_t now) {
  static uint32_t lastCheck = 0;
  if (now - lastCheck < 1800000) return; // Раз в 30 минут
  lastCheck = now;

  LOG_I("Health Check: Starting comprehensive system diagnostic...");
  
  // 1. Основные системные метрики
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t uptimeSec = now / 1000;
  
  // 2. Проверка датчиков температуры на стабильность
  bool sensorsStable = true;
  for (int i = 0; i < 7; i++) {
    if (g_state.health.tempErrors[i] > 50) {
      sensorsStable = false;
      LOG_W("Health Check: Sensor %d unstable (%u errors)", i, g_state.health.tempErrors[i]);
    }
  }

  // 3. Проверка критических систем
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  int32_t wifiRssi = wifiOk ? WiFi.RSSI() : 0;
  bool criticalSensorsOk = g_state.health.tempSensorsOk >= 2; // Минимум куб и царга низ
  
  // 4. Формирование расширенного сообщения для логов
  char logMsg[256];
  const char* resetStr = "Other";
  switch (g_state.health.lastRebootReason) {
    case 1: resetStr = "PowerOn"; break;
    case 3: resetStr = "SWD WDT"; break;
    case 4: resetStr = "HWD WDT"; break;
    case 5: resetStr = "DeepSleep"; break;
    case 6: resetStr = "SW Reset"; break;
    case 7: resetStr = "Panic"; break;
    default: resetStr = "Unknown"; break;
  }
  
  snprintf(logMsg, sizeof(logMsg),
           "[HEALTHCHECK] H:%uKB U:%us Rb:%s T:%u/%u PzemSp:%u Wifi:%s(%d) Healh:%u%%",
           freeHeap / 1024,
           uptimeSec,
           resetStr,
           g_state.health.tempSensorsOk,
           g_state.health.tempSensorsTotal,
           g_state.health.pzemSpikeCount,
           wifiOk ? "OK" : "FAIL",
           wifiRssi,
           g_state.health.overallHealth);

  // 5. Запись в системный лог
  Logger::logf(0, "%s", logMsg);
  LOG_I("%s", logMsg);

  // 6. Анализ состояния и принятие решений
  bool needsAttention = false;
  String alertMessage = "🩺 *Системная диагностика*\n";
  
  if (!criticalSensorsOk) {
    needsAttention = true;
    alertMessage += "- ❌ Критические датчики недоступны\n";
  }
  
  if (!wifiOk && !g_settings.wifi.apMode) {
    needsAttention = true;
    alertMessage += "- ❌ Потеря соединения WiFi\n";
  } else if (wifiOk && wifiRssi < -85) {
    needsAttention = true;
    alertMessage += "- ⚠️ Слабый сигнал WiFi\n";
  }
  
  if (freeHeap < 32768) {
    needsAttention = true;
    alertMessage += "- ⚠️ Критически низкая память\n";
  } else if (freeHeap < 65536) {
    needsAttention = true;
    alertMessage += "- ⚠️ Низкая память\n";
  }
  
  if (g_state.health.cpuTemp > 85) {
    needsAttention = true;
    alertMessage += "- ⚠️ Перегрев процессора\n";
  }
  
  // Проверка на нестабильные датчики
  bool hasUnstableSensors = false;
  for (int i = 0; i < 7; i++) {
    if (g_state.health.tempErrors[i] > 50) {
      hasUnstableSensors = true;
      break;
    }
  }
  if (hasUnstableSensors) {
    needsAttention = true;
    alertMessage += "- ⚠️ Нестабильные датчики температуры\n";
  }
  
  if (needsAttention) {
    Logger::logf(1, "%s", alertMessage.c_str());
  } else {
    // В нормальном режиме просто логируем OK статус раз в 4 проверки (2 часа)
    static uint32_t lastOkLog = 0;
    if (now - lastOkLog >= 7200000) { // 2 часа
      lastOkLog = now;
      Logger::logf(0, "[HEALTHCHECK] System OK - all parameters nominal");
      LOG_I("[HEALTHCHECK] System OK - all parameters nominal");
    }
  }
}

void loop() {
  if (g_captivePortalActive) {
    g_dnsServer.processNextRequest();
  }

  uint32_t now = millis();
  
  // Периодическая диагностика и самоконтроль
  performSystemHealthCheck(now);

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
      Sensors::readHydrometer(g_state.hydrometer, g_state.temps.columnTop,
                              g_settings.hydroCal);
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
  if (g_settings.demoMode) {
    DemoSimulator::syncHardware(g_state, g_settings);
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

  // Pump::update() теперь вызывается в отдельной высокоприоритетной задаче (src/drivers/pump.cpp)
  Heater::update();     // PERF-3 fix: плавный разгон ТЭНа (ramp) был реализован, но не вызывался
  Valves::update();     // ARCH-3 fix: неблокирующий движок сервопривода фракционника
  if (!g_settings.demoMode) {
    g_state.pump.running = Pump::isRunning();
    g_state.pump.speedMlPerHour = Pump::getSpeed();
    g_state.pump.totalVolumeMl = Pump::getTotalVolume();
    Stirrer::syncState(g_state); // обновить g_state.stirrer
  }

  ControlV2::updateRuntime(g_state, g_settings);
  Buttons::update();

  if (g_settings.mqtt.enabled) {
    MQTT::handle();
  }

  g_state.uptime = now / 1000;
  
// Здоровье системы (обновление каждые 5 секунд для MQTT и базового мониторинга)
static uint32_t lastHealthUpdate = 0;
if (now - lastHealthUpdate >= 5000) {
  lastHealthUpdate = now;
  Sensors::updateHealth(g_state.health);
  if (g_settings.mqtt.enabled) MQTT::publishHealth(g_state.health);
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
  Sensors::applyCalibration(g_settings.tempCal);
  Sensors::refreshTemperatureInventory();
  Heater::init();
  Pump::init();
  Pump::setCalibration(g_settings.pumpCal.mlPerRevolution);
  Valves::init();
  if (g_settings.fractionator.enabled) Valves::initFractionator();
  Stirrer::init(); // MCP4725 DAC мешалки
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
  g_settings = Settings{};
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
