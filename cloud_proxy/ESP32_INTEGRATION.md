# Интеграция ESP32 с облачным прокси

## Обзор

ESP32 должен подключаться к облачному прокси-серверу через WebSocket, отправляя данные состояния и получая команды.

## Архитектура

```
ESP32 (локальная сеть)
  ↓ WebSocket подключение
Облачный прокси-сервер
  ↓ HTTPS/WebSocket
Android приложение
```

## Реализация на ESP32

### 1. Добавьте WebSocket клиент

Создайте файл `src/interface/cloud_proxy.cpp`:

```cpp
#include "cloud_proxy.h"
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "../types.h"

extern SystemState g_state;

WebSocketsClient webSocket;
bool cloudProxyConnected = false;
String cloudProxyUrl = "";
String deviceId = "";
String authToken = "";

void cloudProxyWebSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      LOG_I("CloudProxy: Disconnected");
      cloudProxyConnected = false;
      break;
      
    case WStype_CONNECTED:
      LOG_I("CloudProxy: Connected to %s", payload);
      cloudProxyConnected = true;
      break;
      
    case WStype_TEXT:
      // Обработка команд от сервера
      {
        StaticJsonDocument<512> doc;
        deserializeJson(doc, payload, length);
        
        if (doc["type"] == "command") {
          String command = doc["command"];
          // Обработка команды
          // Например: start, stop, pause, set_power и т.д.
        }
      }
      break;
      
    default:
      break;
  }
}

void CloudProxy::init(const char* url, const char* id, const char* token) {
  cloudProxyUrl = String(url);
  deviceId = String(id);
  authToken = String(token);
  
  // Формируем URL подключения
  String wsUrl = cloudProxyUrl + "/esp32?token=" + authToken + "&device=" + deviceId;
  
  webSocket.beginSSL(cloudProxyUrl.c_str(), 443, "/esp32");
  webSocket.setAuthorization("token", authToken.c_str());
  webSocket.onEvent(cloudProxyWebSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void CloudProxy::loop() {
  webSocket.loop();
  
  // Отправка состояния каждые 5 секунд
  static unsigned long lastSend = 0;
  if (cloudProxyConnected && millis() - lastSend > 5000) {
    sendState();
    lastSend = millis();
  }
}

void CloudProxy::sendState() {
  if (!cloudProxyConnected) return;
  
  StaticJsonDocument<2048> doc;
  
  doc["type"] = "state";
  doc["deviceId"] = deviceId;
  doc["mode"] = static_cast<int>(g_state.mode);
  doc["phase"] = static_cast<int>(g_state.rectPhase);
  doc["uptime"] = g_state.uptime;
  
  // Температуры
  JsonObject temps = doc.createNestedObject("temps");
  temps["cube"] = g_state.temps.cube;
  temps["columnBottom"] = g_state.temps.columnBottom;
  temps["columnTop"] = g_state.temps.columnTop;
  temps["reflux"] = g_state.temps.reflux;
  
  // Мощность
  JsonObject power = doc.createNestedObject("power");
  power["voltage"] = g_state.power.voltage;
  power["current"] = g_state.power.current;
  power["power"] = g_state.power.power;
  power["energy"] = g_state.power.energy;
  
  // Насос
  JsonObject pump = doc.createNestedObject("pump");
  pump["speed"] = g_state.pump.speedMlPerHour;
  pump["volume"] = g_state.pump.totalVolumeMl;
  pump["running"] = g_state.pump.running;
  
  String json;
  serializeJson(doc, json);
  webSocket.sendTXT(json);
}
```

### 2. Добавьте заголовочный файл

`src/interface/cloud_proxy.h`:

```cpp
#ifndef CLOUD_PROXY_H
#define CLOUD_PROXY_H

namespace CloudProxy {
  void init(const char* url, const char* deviceId, const char* token);
  void loop();
  void sendState();
  bool isConnected();
}

#endif
```

### 3. Интеграция в main.cpp

```cpp
#include "interface/cloud_proxy.h"

void setup() {
  // ... существующий код ...
  
  // Настройка облачного прокси (если включено)
  if (g_settings.cloudProxy.enabled) {
    CloudProxy::init(
      g_settings.cloudProxy.url,
      g_settings.cloudProxy.deviceId,
      g_settings.cloudProxy.token
    );
  }
}

void loop() {
  // ... существующий код ...
  
  // Облачный прокси
  if (g_settings.cloudProxy.enabled) {
    CloudProxy::loop();
  }
}
```

### 4. Добавьте настройки в NVS

В `src/types.h` или `src/storage/nvs_manager.cpp`:

```cpp
struct CloudProxySettings {
  bool enabled = false;
  char url[128] = "";
  char deviceId[32] = "";
  char token[64] = "";
};
```

### 5. Настройка через Web UI

Добавьте в веб-интерфейс раздел для настройки облачного прокси:

```javascript
// В настройках системы
{
  cloudProxy: {
    enabled: false,
    url: "https://your-server.com",
    deviceId: "esp32-001",
    token: "your_esp32_token"
  }
}
```

## Получение Device ID

Device ID должен быть уникальным для каждого ESP32. Варианты:

1. **MAC адрес:**
```cpp
String deviceId = WiFi.macAddress();
deviceId.replace(":", "");
```

2. **Chip ID:**
```cpp
String deviceId = "esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
```

3. **Ручной ввод** через Web UI

## Тестирование

1. Запустите облачный прокси-сервер
2. Настройте ESP32 с URL сервера
3. Проверьте подключение в логах сервера
4. Проверьте доступность через Android приложение

## Безопасность

1. ✅ Используйте HTTPS (wss://) для подключения
2. ✅ Храните токен в NVS (не в коде)
3. ✅ Используйте уникальный Device ID
4. ✅ Регулярно обновляйте токены

