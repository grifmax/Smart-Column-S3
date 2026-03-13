/**
 * Smart-Column S3 - MQTT Client
 *
 * PubSubClient для публикации данных в MQTT
 */

#include "mqtt.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <limits.h>
#include "config.h"
#include "storage/logger.h"

// Для управления железом
#include "control/fsm.h"
#include "control/watt_control.h"
#include "drivers/heater.h"
#include "drivers/pump.h"
#include "drivers/valves.h"

extern SystemState g_state;
extern Settings g_settings;

static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static String baseTopic = "smart-column";
static String deviceId;
static String mqttUsername;
static String mqttPassword;
static uint32_t lastReconnectAttempt = 0;
static bool wasConnected = false;
static int lastConnectErrorCode = INT_MIN;
static uint32_t lastConnectErrorLogMs = 0;

namespace {

void logConnectFailureThrottled(int errorCode) {
    const uint32_t now = millis();
    if (errorCode != lastConnectErrorCode ||
        now - lastConnectErrorLogMs >= 30000UL) {
        Logger::logf(1, "MQTT connect failed: rc=%d, server=%s:%u",
                     errorCode, g_settings.mqtt.server, g_settings.mqtt.port);
        lastConnectErrorCode = errorCode;
        lastConnectErrorLogMs = now;
    }
}

} // namespace

namespace MQTT {

void init(const char* server, uint16_t port, const char* username, const char* password) {
    LOG_I("MQTT: Initializing...");

    // Генерация уникального ID устройства из MAC адреса
    uint8_t mac[6];
    WiFi.macAddress(mac);
    deviceId = String(mac[3], HEX) + String(mac[4], HEX) + String(mac[5], HEX);
    deviceId.toUpperCase();
    mqttUsername = username ? String(username) : String();
    mqttPassword = password ? String(password) : String();

    mqttClient.setServer(server, port);
    mqttClient.setBufferSize(1024);  // Увеличенный буфер для Discovery

    // Установка callback для входящих сообщений
    mqttClient.setCallback([](char* topic, byte* payload, unsigned int length) {
        LOG_D("MQTT: Message received [%s]", topic);
        
        // Преобразуем payload в String
        String payloadStr = "";
        for (unsigned int i = 0; i < length; i++) {
            payloadStr += (char)payload[i];
        }
        payloadStr.trim();
        
        String topicStr = String(topic);
        String cmdPrefix = baseTopic + "/" + deviceId + "/cmd/";
        
        if (!topicStr.startsWith(cmdPrefix)) {
            return;
        }
        
        String cmd = topicStr.substring(cmdPrefix.length());
        
        LOG_I("MQTT Command: %s = %s", cmd.c_str(), payloadStr.c_str());
        Logger::logf(0, "MQTT command received: %s=%s",
                     cmd.c_str(), payloadStr.c_str());

        // Обработка команд FSM
        if (cmd == "start") {
            Mode m = Mode::IDLE;
            int reqMode = payloadStr.toInt();
            if (reqMode > 0 && reqMode <= static_cast<int>(Mode::FERMENTATION)) {
                m = static_cast<Mode>(reqMode);
            } else {
                // Если не указан - попробовать использовать текущий сохранённый (в UI это обычно не делается)
                // Оставим IDLE, значит FSM::startMode просто включит IDLE. В идеале UI передает число.
                LOG_W("MQTT: start without valid mode. Using IDLE.");
            }
            FSM::startMode(g_state, g_settings, m);
        } else if (cmd == "stop") {
            FSM::stopMode(g_state);
        } else if (cmd == "pause") {
            FSM::pause(g_state);
        } else if (cmd == "resume") {
            FSM::resume(g_state);
        } else if (cmd == "next") {
            FSM::nextFraction(g_state, g_settings);
        }
        
        // Обработка управления железом (работает если FSM не заблокировал)
        // ТЭН
        else if (cmd == "heater") {
            int pwr = payloadStr.toInt();
            if (pwr < 0) pwr = 0;
            if (pwr > 100) pwr = 100;
            if (g_state.mode == Mode::RECTIFICATION || g_state.mode == Mode::DISTILLATION || g_state.mode == Mode::MASHING) {
                // В автоматическом режиме - override (от -1: снять перехват, 0-100: установить)
                int overridePwr = payloadStr.toInt();
                WattControl::setOverride(overridePwr);
            } else {
                Heater::setPower((uint8_t)pwr);
            }
        }
        // Насос
        else if (cmd == "pump") {
            float speed = payloadStr.toFloat();
            if (speed <= 0.0f) {
                Pump::stop();
            } else {
                Pump::start(speed);
            }
        }
        // Клапаны
        else if (cmd == "valves/water") {
            Valves::setWater(payloadStr == "1" || payloadStr == "true" || payloadStr == "on");
        }
        else if (cmd == "valves/heads") {
            Valves::setHeads(payloadStr == "1" || payloadStr == "true" || payloadStr == "on");
        }
        else if (cmd == "valves/uno") {
            Valves::setUno(payloadStr == "1" || payloadStr == "true" || payloadStr == "on");
        }
        else if (cmd == "valves/closeAll" || cmd == "valves/stop") {
            Valves::closeAll();
        }
        else {
            LOG_W("MQTT: Unknown command: %s", cmd.c_str());
            Logger::logf(1, "MQTT command rejected: unknown command '%s'",
                         cmd.c_str());
        }
    });

    LOG_I("MQTT: Device ID: %s", deviceId.c_str());
    LOG_I("MQTT: Server: %s:%d", server, port);
    LOG_I("MQTT: Auth: %s",
          mqttUsername.length() ? "username/password" : "anonymous");
}

bool reconnect() {
    String clientId = "SmartColumn-" + deviceId;
    String willTopic = baseTopic + "/" + deviceId + "/status";
    const char* username = mqttUsername.length() ? mqttUsername.c_str() : nullptr;
    const char* password = mqttUsername.length() ? mqttPassword.c_str() : nullptr;

    LOG_I("MQTT: Connecting as %s...", clientId.c_str());

    // Last Will and Testament (LWT) для индикации доступности
    const bool connected = username
        ? mqttClient.connect(clientId.c_str(), username, password,
                             willTopic.c_str(), 1, true, "offline")
        : mqttClient.connect(clientId.c_str(), willTopic.c_str(), 1, true, "offline");

    if (connected) {
        LOG_I("MQTT: Connected!");
        wasConnected = true;
        lastConnectErrorCode = INT_MIN;
        lastConnectErrorLogMs = 0;

        // Публикация online статуса
        mqttClient.publish(willTopic.c_str(), "online", true);

        // Подписка на топики команд
        String cmdTopic = baseTopic + "/" + deviceId + "/cmd/#";
        mqttClient.subscribe(cmdTopic.c_str());

        // Публикация Discovery при подключении
        publishDiscovery();
        Logger::logf(0, "MQTT connected: %s:%u, topic=%s",
                     g_settings.mqtt.server, g_settings.mqtt.port,
                     baseTopic.c_str());

        return true;
    }

    LOG_E("MQTT: Connection failed, rc=%d", mqttClient.state());
    wasConnected = false;
    logConnectFailureThrottled(mqttClient.state());
    return false;
}

void handle() {
    if (!mqttClient.connected()) {
        if (wasConnected) {
            wasConnected = false;
            Logger::logf(1, "MQTT disconnected: rc=%d", mqttClient.state());
        }
        uint32_t now = millis();
        // Попытка переподключения раз в 5 секунд
        if (now - lastReconnectAttempt > 5000) {
            lastReconnectAttempt = now;
            reconnect();
        }
    } else {
        wasConnected = true;
        mqttClient.loop();
    }
}

void publishState(const SystemState& state) {
    if (!mqttClient.connected()) return;

    String topic = baseTopic + "/" + deviceId + "/state";
    StaticJsonDocument<512> doc;

    // Основные параметры
    doc["mode"] = static_cast<int>(state.mode);
    doc["phase"] = static_cast<int>(state.rectPhase); // для совместимости
    doc["nbk_phase"] = static_cast<int>(state.nbkPhase);
    doc["ferm_phase"] = static_cast<int>(state.fermPhase);

    // Температуры
    JsonObject temps = doc.createNestedObject("temperatures");
    temps["cube"] = round(state.temps.cube * 10) / 10;
    temps["column_top"] = round(state.temps.columnTop * 10) / 10;
    temps["column_bottom"] = round(state.temps.columnBottom * 10) / 10;
    temps["reflux"] = round(state.temps.reflux * 10) / 10;
    temps["tsa"] = round(state.temps.tsa * 10) / 10;

    // Мощность
    JsonObject power = doc.createNestedObject("power");
    power["voltage"] = round(state.power.voltage * 10) / 10;
    power["current"] = round(state.power.current * 100) / 100;
    power["power"] = round(state.power.power);
    power["energy"] = round(state.power.energy * 1000) / 1000;

    // Насос
    doc["pump_speed"] = round(state.pump.speedMlPerHour);
    doc["pump_volume"] = round(state.pump.totalVolumeMl);

    String json;
    serializeJson(doc, json);
    mqttClient.publish(topic.c_str(), json.c_str(), true);
}

void publishHealth(const SystemHealth& health) {
    if (!mqttClient.connected()) return;

    String topic = baseTopic + "/" + deviceId + "/health";
    StaticJsonDocument<384> doc;

    doc["overall"] = health.overallHealth;
    doc["wifi_rssi"] = health.wifiRSSI;
    doc["uptime"] = health.uptime;
    doc["free_heap"] = health.freeHeap;
    doc["cpu_temp"] = health.cpuTemp;
    doc["last_reboot_reason"] = health.lastRebootReason;

    // Статусы датчиков
    doc["pzem_ok"] = health.pzemOk;
    doc["ads1115_ok"] = health.ads1115Ok;
    doc["bmp280_ok"] = health.bmp280Ok;
    doc["temp_sensors_ok"] = health.tempSensorsOk;
    doc["temp_sensors_total"] = health.tempSensorsTotal;

    // Счётчики ошибок
    doc["pzem_spikes"] = health.pzemSpikeCount;
    doc["temp_errors"] = health.tempReadErrors;

    String json;
    serializeJson(doc, json);
    mqttClient.publish(topic.c_str(), json.c_str(), true);
}

void publishDiscovery() {
    if (!mqttClient.connected()) return;

    LOG_I("MQTT: Publishing Home Assistant Discovery...");

    String deviceName = "Smart Column " + deviceId;
    String availTopic = baseTopic + "/" + deviceId + "/status";
    String stateTopic = baseTopic + "/" + deviceId + "/state";

    // Device info (общая для всех сущностей)
    String deviceInfo = String("{") +
        "\"ids\":[\"" + deviceId + "\"]," +
        "\"name\":\"" + deviceName + "\"," +
        "\"mdl\":\"Smart-Column S3\"," +
        "\"mf\":\"Custom\"," +
        "\"sw\":\"" + FW_VERSION + "\"" +
        "}";

    // 1. Температура куба
    {
        String topic = "homeassistant/sensor/" + deviceId + "_cube_temp/config";
        String payload = String("{") +
            "\"name\":\"Cube Temperature\"," +
            "\"uniq_id\":\"" + deviceId + "_cube_temp\"," +
            "\"stat_t\":\"" + stateTopic + "\"," +
            "\"val_tpl\":\"{{ value_json.temperatures.cube }}\"," +
            "\"unit_of_meas\":\"°C\"," +
            "\"dev_cla\":\"temperature\"," +
            "\"avty_t\":\"" + availTopic + "\"," +
            "\"dev\":" + deviceInfo +
            "}";
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    // 2. Мощность
    {
        String topic = "homeassistant/sensor/" + deviceId + "_power/config";
        String payload = String("{") +
            "\"name\":\"Power\"," +
            "\"uniq_id\":\"" + deviceId + "_power\"," +
            "\"stat_t\":\"" + stateTopic + "\"," +
            "\"val_tpl\":\"{{ value_json.power.power }}\"," +
            "\"unit_of_meas\":\"W\"," +
            "\"dev_cla\":\"power\"," +
            "\"avty_t\":\"" + availTopic + "\"," +
            "\"dev\":" + deviceInfo +
            "}";
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    // 3. Энергия
    {
        String topic = "homeassistant/sensor/" + deviceId + "_energy/config";
        String payload = String("{") +
            "\"name\":\"Energy\"," +
            "\"uniq_id\":\"" + deviceId + "_energy\"," +
            "\"stat_t\":\"" + stateTopic + "\"," +
            "\"val_tpl\":\"{{ value_json.power.energy }}\"," +
            "\"unit_of_meas\":\"kWh\"," +
            "\"dev_cla\":\"energy\"," +
            "\"stat_cla\":\"total_increasing\"," +
            "\"avty_t\":\"" + availTopic + "\"," +
            "\"dev\":" + deviceInfo +
            "}";
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    // 4. System Health (как sensor)
    {
        String healthTopic = baseTopic + "/" + deviceId + "/health";
        String topic = "homeassistant/sensor/" + deviceId + "_health/config";
        String payload = String("{") +
            "\"name\":\"System Health\"," +
            "\"uniq_id\":\"" + deviceId + "_health\"," +
            "\"stat_t\":\"" + healthTopic + "\"," +
            "\"val_tpl\":\"{{ value_json.overall }}\"," +
            "\"unit_of_meas\":\"%\"," +
            "\"avty_t\":\"" + availTopic + "\"," +
            "\"dev\":" + deviceInfo +
            "}";
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    // 5. Последнее уведомление (как sensor для автоматизаций)
    {
        String notifTopic = baseTopic + "/" + deviceId + "/notification";
        String topic = "homeassistant/sensor/" + deviceId + "_notification/config";
        String payload = String("{") +
            "\"name\":\"Last Notification\"," +
            "\"uniq_id\":\"" + deviceId + "_notification\"," +
            "\"stat_t\":\"" + notifTopic + "\"," +
            "\"val_tpl\":\"{{ value_json.title }}\"," +
            "\"json_attr_t\":\"" + notifTopic + "\"," +
            "\"json_attr_tpl\":\"{{ value_json | tojson }}\"," +
            "\"icon\":\"mdi:bell-alert\"," +
            "\"avty_t\":\"" + availTopic + "\"," +
            "\"dev\":" + deviceInfo +
            "}";
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    LOG_I("MQTT: Discovery published (5 entities)");
}

bool isConnected() {
    return mqttClient.connected();
}

void disconnect() {
    if (mqttClient.connected()) {
        LOG_I("MQTT: Disconnecting");
        Logger::logf(0, "MQTT disconnected");
        mqttClient.disconnect();
    }
    wasConnected = false;
    lastConnectErrorCode = INT_MIN;
    lastConnectErrorLogMs = 0;
    lastReconnectAttempt = 0;
}

void setBaseTopic(const char* topic) {
    baseTopic = (topic && topic[0]) ? String(topic) : String("smart-column");
}

void publishNotification(const char* title, const char* message, const char* level) {
    if (!mqttClient.connected()) {
        LOG_W("MQTT: Cannot send notification - not connected");
        return;
    }

    // Публикация в топик уведомлений для sensor
    String notifTopic = baseTopic + "/" + deviceId + "/notification";
    StaticJsonDocument<512> doc;

    doc["title"] = title;
    doc["message"] = message;
    doc["level"] = level;
    doc["timestamp"] = millis() / 1000;  // Время в секундах

    String json;
    serializeJson(doc, json);
    mqttClient.publish(notifTopic.c_str(), json.c_str(), false);

    // Публикация в топик для Home Assistant notify service
    // Формат для MQTT notify: {"title": "...", "message": "..."}
    String notifyTopic = baseTopic + "/" + deviceId + "/notify";
    StaticJsonDocument<384> notifyDoc;

    // Добавляем эмодзи в зависимости от уровня
    String titleWithIcon = String(title);
    if (strcmp(level, "error") == 0) {
        titleWithIcon = "❌ " + titleWithIcon;
    } else if (strcmp(level, "warning") == 0) {
        titleWithIcon = "⚠️ " + titleWithIcon;
    } else if (strcmp(level, "success") == 0) {
        titleWithIcon = "✅ " + titleWithIcon;
    } else {
        titleWithIcon = "ℹ️ " + titleWithIcon;
    }

    notifyDoc["title"] = titleWithIcon;
    notifyDoc["message"] = message;

    String notifyJson;
    serializeJson(notifyDoc, notifyJson);
    mqttClient.publish(notifyTopic.c_str(), notifyJson.c_str(), false);

    LOG_I("MQTT: Notification sent - %s: %s", title, message);
}

} // namespace MQTT
