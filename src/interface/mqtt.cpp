/**
 * Smart-Column S3 - MQTT Client
 *
 * PubSubClient для публикации данных в MQTT
 */

#include "mqtt.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"

static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static String baseTopic = "smartcolumn";
static String deviceId;
static uint32_t lastReconnectAttempt = 0;

namespace MQTT {

void init(const char* server, uint16_t port, const char* username, const char* password) {
    LOG_I("MQTT: Initializing...");

    // Генерация уникального ID устройства из MAC адреса
    uint8_t mac[6];
    WiFi.macAddress(mac);
    deviceId = String(mac[3], HEX) + String(mac[4], HEX) + String(mac[5], HEX);
    deviceId.toUpperCase();

    mqttClient.setServer(server, port);
    mqttClient.setBufferSize(1024);  // Увеличенный буфер для Discovery

    // Установка callback для входящих сообщений
    mqttClient.setCallback([](char* topic, byte* payload, unsigned int length) {
        LOG_D("MQTT: Message received [%s]", topic);
        // TODO: Обработка команд управления
    });

    LOG_I("MQTT: Device ID: %s", deviceId.c_str());
    LOG_I("MQTT: Server: %s:%d", server, port);
}

bool reconnect() {
    String clientId = "SmartColumn-" + deviceId;
    String willTopic = baseTopic + "/" + deviceId + "/status";

    LOG_I("MQTT: Connecting as %s...", clientId.c_str());

    // Last Will and Testament (LWT) для индикации доступности
    if (mqttClient.connect(clientId.c_str(), willTopic.c_str(), 1, true, "offline")) {
        LOG_I("MQTT: Connected!");

        // Публикация online статуса
        mqttClient.publish(willTopic.c_str(), "online", true);

        // Подписка на топики команд
        String cmdTopic = baseTopic + "/" + deviceId + "/cmd/#";
        mqttClient.subscribe(cmdTopic.c_str());

        // Публикация Discovery при подключении
        publishDiscovery();

        return true;
    }

    LOG_E("MQTT: Connection failed, rc=%d", mqttClient.state());
    return false;
}

void handle() {
    if (!mqttClient.connected()) {
        uint32_t now = millis();
        // Попытка переподключения раз в 5 секунд
        if (now - lastReconnectAttempt > 5000) {
            lastReconnectAttempt = now;
            reconnect();
        }
    } else {
        mqttClient.loop();
    }
}

void publishState(const SystemState& state) {
    if (!mqttClient.connected()) return;

    String topic = baseTopic + "/" + deviceId + "/state";
    StaticJsonDocument<512> doc;

    // Основные параметры
    doc["mode"] = static_cast<int>(state.mode);
    doc["phase"] = static_cast<int>(state.rectPhase);

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

    LOG_I("MQTT: Discovery published");
}

bool isConnected() {
    return mqttClient.connected();
}

void setBaseTopic(const char* topic) {
    baseTopic = String(topic);
}

} // namespace MQTT
