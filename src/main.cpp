/**
 * Smart-Column S3 - Главный файл
 * 
 * Контроллер автоматизации ректификационной колонны
 * Платформа: ESP32-S3 DevKitC-1 N16R8
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "types.h"

// Драйверы
#include "drivers/sensors.h"
#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/valves.h"
#include "drivers/display.h"

// Управление
#include "control/safety.h"
#include "control/fsm.h"
#include "control/watt_control.h"

// Интерфейсы
#include "interface/webserver.h"
#include "interface/telegram.h"
#include "interface/buttons.h"

// Хранение
#include "storage/nvs_manager.h"
#include "storage/logger.h"

// =============================================================================
// ГЛОБАЛЬНЫЕ ОБЪЕКТЫ
// =============================================================================

SystemState g_state;        // Текущее состояние системы
Settings g_settings;        // Настройки (из NVS)

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
// SETUP
// =============================================================================

void setup() {
    // Serial
    Serial.begin(115200);
    delay(100);

    // WatchDog Timer - защита от зависаний
    esp_task_wdt_init(30, true);  // 30 сек таймаут, паника при срабатывании
    esp_task_wdt_add(NULL);        // Регистрация главной задачи

    LOG_I("=================================");
    LOG_I("%s v%s", FW_NAME, FW_VERSION);
    LOG_I("Build: %s", FW_DATE);
    LOG_I("=================================");
    
    // Инициализация состояния
    memset(&g_state, 0, sizeof(g_state));
    g_state.mode = Mode::IDLE;
    g_state.rectPhase = RectPhase::IDLE;
    g_state.safetyOk = true;
    
    // SPIFFS
    if (!SPIFFS.begin(true)) {
        LOG_E("SPIFFS mount failed!");
    } else {
        LOG_I("SPIFFS: %d KB used / %d KB total", 
              SPIFFS.usedBytes() / 1024, 
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
    if (g_state.mode != Mode::IDLE && now - g_lastLogWrite >= INTERVAL_LOG_WRITE) {
        g_lastLogWrite = now;
        Logger::writeData(g_state);
    }
    
    // Обработка кнопок
    Buttons::update();
    
    // Telegram
    TelegramBot::update();
    
    // Обновление uptime
    g_state.uptime = now / 1000;

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
        LOG_I("AP started: %s, IP: %s", WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
    }
    
    // mDNS
    // TODO: добавить mDNS
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
    g_settings.fractionator.positionsEnabled[0] = true;   // Головы
    g_settings.fractionator.positionsEnabled[1] = false;  // Подголовники - выкл
    g_settings.fractionator.positionsEnabled[2] = true;   // Тело
    g_settings.fractionator.positionsEnabled[3] = false;  // Предхвостье - выкл
    g_settings.fractionator.positionsEnabled[4] = true;   // Хвосты
    
    // Прочее
    g_settings.language = 0;  // RU
    g_settings.theme = 0;     // Light
    g_settings.soundEnabled = true;
    
    // Загрузка из NVS (перезапишет дефолты)
    NVSManager::loadSettings(g_settings);
    
    LOG_I("Settings loaded");
}

// =============================================================================
// BUZZER HELPER
// =============================================================================

namespace Buzzer {
    void beep(uint8_t count, uint16_t duration) {
        for (uint8_t i = 0; i < count; i++) {
            digitalWrite(PIN_BUZZER, HIGH);
            delay(duration);
            digitalWrite(PIN_BUZZER, LOW);
            if (i < count - 1) delay(duration);
        }
    }
    
    void alarm() {
        // Непрерывный сигнал - управляется в safety.cpp
    }
}
