# Smart-Column S3 — Документация API

**Версия:** 1.2+
**Последнее обновление:** 2025-12-06

## Содержание

1. [REST API](#rest-api)
2. [WebSocket API](#websocket-api)
3. [MQTT интеграция](#mqtt-интеграция)
4. [Аутентификация](#аутентификация)
5. [Ограничение запросов](#ограничение-запросов)
6. [Коды ошибок](#коды-ошибок)
7. [Примеры использования](#примеры-использования)

---

## REST API

Все REST API эндпоинты доступны по адресу `http://<ip-устройства>/api/`

### Аутентификация

Большинство эндпоинтов требуют HTTP Basic Authentication (если включено в настройках).

```http
Authorization: Basic <base64(username:password)>
```

### Эндпоинты

#### GET /api/status

Получить текущее состояние системы.

**Ответ:**
```json
{
  "mode": 0,
  "phase": 1,
  "temperatures": {
    "cube": 78.5,
    "column_bottom": 75.2,
    "column_top": 82.1,
    "reflux": 80.5,
    "tsa": 45.2,
    "water_in": 18.5,
    "water_out": 25.3
  },
  "power": {
    "voltage": 220.5,
    "current": 4.25,
    "power": 937,
    "energy": 2.456
  },
  "heater_power": 85,
  "pump_speed": 120,
  "uptime": 145620
}
```

#### GET /api/health

Получить статус здоровья системы (0-100%).

**Ответ:**
```json
{
  "health": 95,
  "checks": {
    "temperatures": true,
    "power": true,
    "water_flow": true,
    "pressure": true
  },
  "warnings": [],
  "errors": []
}
```

#### GET /api/sensors

Получить показания всех датчиков.

**Ответ:**
```json
{
  "ds18b20": [78.5, 75.2, 82.1, 80.5, 45.2, 18.5, 25.3],
  "bmp280_1": {"temperature": 22.5, "pressure": 101325},
  "bmp280_2": {"temperature": 78.2, "pressure": 102100},
  "mpx5010": 850,
  "zmpt101b": 220.5,
  "acs712": 4.25,
  "yf_s201": 12.5
}
```

#### POST /api/mode/start

Запустить выбранный режим работы.

**Запрос:**
```json
{
  "mode": 0,
  "params": {
    "heads_percent": 8,
    "body_speed": 100,
    "target_temp": 78.5
  }
}
```

**Ответ:**
```json
{
  "success": true,
  "message": "Режим ректификации запущен"
}
```

#### POST /api/mode/stop

Остановить текущую операцию.

**Ответ:**
```json
{
  "success": true,
  "message": "Операция остановлена"
}
```

#### POST /api/heater/power

Установить мощность нагревателя (0-100%).

**Запрос:**
```json
{
  "power": 85
}
```

**Ответ:**
```json
{
  "success": true,
  "power": 85
}
```

#### POST /api/pump/speed

Установить скорость насоса (мл/час).

**Запрос:**
```json
{
  "speed": 120
}
```

**Ответ:**
```json
{
  "success": true,
  "speed": 120
}
```

#### GET /api/calibration

Получить данные калибровки.

**Ответ:**
```json
{
  "ds18b20_offsets": [0.0, -0.2, 0.1, 0.0, -0.1, 0.0, 0.2],
  "pump_ml_per_rev": 2.5,
  "voltage_calibration": 1.0,
  "current_calibration": 1.0,
  "pressure_calibration": 1.0
}
```

#### POST /api/calibration

Обновить данные калибровки.

**Запрос:**
```json
{
  "ds18b20_offsets": [0.0, -0.2, 0.1, 0.0, -0.1, 0.0, 0.2],
  "pump_ml_per_rev": 2.5
}
```

**Ответ:**
```json
{
  "success": true,
  "message": "Калибровка обновлена"
}
```

#### GET /api/system/info

Получить информацию о системе.

**Ответ:**
```json
{
  "firmware_version": "1.2.1",
  "hardware": "ESP32-S3 DevKitC-1 N16R8",
  "uptime": 145620,
  "free_heap": 245760,
  "wifi_rssi": -45,
  "ip_address": "192.168.1.100"
}
```

#### POST /api/system/reboot

Перезагрузить устройство.

**Ответ:**
```json
{
  "success": true,
  "message": "Перезагрузка через 3 секунды"
}
```

---

## WebSocket API

Подключение по адресу `ws://<ip-устройства>/ws`

### Формат сообщений

Все сообщения в формате JSON.

#### Сервер → Клиент (Обновления статуса)

Отправляется каждую 1 секунду во время работы.

```json
{
  "type": "status",
  "data": {
    "mode": 0,
    "phase": 1,
    "temperatures": { ... },
    "power": { ... },
    "heater_power": 85,
    "pump_speed": 120
  }
}
```

#### Сервер → Клиент (События)

```json
{
  "type": "event",
  "event": "phase_change",
  "data": {
    "from": 1,
    "to": 2,
    "phase_name": "HEADS"
  }
}
```

#### Клиент → Сервер (Команды)

```json
{
  "command": "set_power",
  "value": 85
}
```

```json
{
  "command": "set_speed",
  "value": 120
}
```

```json
{
  "command": "start_mode",
  "mode": 0,
  "params": { ... }
}
```

---

## MQTT интеграция

### Подключение

```
Сервер: <mqtt_broker>
Порт: 1883 (по умолчанию)
Имя пользователя: <mqtt_username> (опционально)
Пароль: <mqtt_password> (опционально)
```

### Структура топиков

#### Топик состояния
```
smartcolumn/<device_id>/state
```

**Пример полезной нагрузки:**
```json
{
  "mode": 0,
  "phase": 1,
  "temperatures": {
    "cube": 78.5,
    "column_top": 82.1
  },
  "power": {
    "voltage": 220.5,
    "current": 4.25,
    "power": 937,
    "energy": 2.456
  }
}
```

**Публикуется:** Каждые 10 секунд (настраивается)

#### Топик здоровья
```
smartcolumn/<device_id>/health
```

**Полезная нагрузка:** `95` (процент 0-100)
**Публикуется:** Каждые 5 секунд

#### Топик доступности
```
smartcolumn/<device_id>/status
```

**Полезная нагрузка:** `online` или `offline`
**Retained:** Да
**LWT:** Да

### Home Assistant MQTT Discovery

Устройство автоматически публикует сообщения обнаружения для Home Assistant.

#### Формат топика обнаружения
```
homeassistant/sensor/<device_id>_<entity>/config
```

#### Пример сообщения обнаружения (Датчик температуры)

**Топик:** `homeassistant/sensor/abc123_cube_temp/config`

**Полезная нагрузка:**
```json
{
  "name": "Температура куба",
  "uniq_id": "abc123_cube_temp",
  "stat_t": "smartcolumn/abc123/state",
  "val_tpl": "{{ value_json.temperatures.cube }}",
  "unit_of_meas": "°C",
  "dev_cla": "temperature",
  "avty_t": "smartcolumn/abc123/status",
  "dev": {
    "ids": ["smartcolumn_abc123"],
    "name": "Smart Column abc123",
    "mdl": "ESP32-S3 DevKitC-1",
    "mf": "DIY",
    "sw": "1.2.1"
  }
}
```

#### Доступные сущности

| Сущность | Класс устройства | Единица | Класс состояния |
|----------|------------------|---------|-----------------|
| Температура куба | temperature | °C | measurement |
| Температура царги верх | temperature | °C | measurement |
| Напряжение | voltage | V | measurement |
| Ток | current | A | measurement |
| Мощность | power | W | measurement |
| Энергия | energy | kWh | total_increasing |
| Здоровье системы | None | % | measurement |

---

## Аутентификация

### HTTP Basic Authentication

Когда аутентификация включена, все HTTP запросы должны содержать:

```http
Authorization: Basic <base64(username:password)>
```

### Пример (Python)
```python
import requests
from requests.auth import HTTPBasicAuth

response = requests.get(
    'http://192.168.1.100/api/status',
    auth=HTTPBasicAuth('admin', 'password')
)
```

### Пример (cURL)
```bash
curl -u admin:password http://192.168.1.100/api/status
```

### Пример (JavaScript)
```javascript
fetch('http://192.168.1.100/api/status', {
  headers: {
    'Authorization': 'Basic ' + btoa('admin:password')
  }
})
```

---

## Ограничение запросов

Система реализует ограничение частоты запросов для защиты от злоупотреблений.

**Лимиты:**
- 60 запросов в минуту с одного IP-адреса
- Применяется ко всем HTTP эндпоинтам

**Ответ при превышении:**
```http
HTTP/1.1 429 Too Many Requests
Content-Type: application/json

{
  "error": "Превышен лимит запросов",
  "retry_after": 45
}
```

---

## Коды ошибок

| HTTP код | Значение |
|----------|----------|
| 200 | Успешно |
| 400 | Неверный запрос - Некорректные параметры |
| 401 | Не авторизован - Требуется аутентификация или неверные данные |
| 404 | Не найдено - Эндпоинт не существует |
| 429 | Слишком много запросов - Превышен лимит |
| 500 | Внутренняя ошибка сервера |

### Формат ответа с ошибкой

```json
{
  "error": "Сообщение об ошибке",
  "code": 400,
  "details": "Дополнительная информация"
}
```

---

## Примеры использования

### Python - Получение статуса и управление

```python
import requests
from requests.auth import HTTPBasicAuth
import time

BASE_URL = 'http://192.168.1.100'
auth = HTTPBasicAuth('admin', 'password')

# Получить текущий статус
status = requests.get(f'{BASE_URL}/api/status', auth=auth).json()
print(f"Текущая температура: {status['temperatures']['cube']}°C")

# Установить мощность нагревателя
requests.post(
    f'{BASE_URL}/api/heater/power',
    json={'power': 75},
    auth=auth
)

# Запустить режим ректификации
requests.post(
    f'{BASE_URL}/api/mode/start',
    json={
        'mode': 0,
        'params': {
            'heads_percent': 8,
            'body_speed': 100
        }
    },
    auth=auth
)

# Мониторинг статуса
while True:
    status = requests.get(f'{BASE_URL}/api/status', auth=auth).json()
    print(f"Куб: {status['temperatures']['cube']}°C, Мощность: {status['power']['power']}Вт")
    time.sleep(5)
```

### Node.js - Подписка на MQTT

```javascript
const mqtt = require('mqtt');

const client = mqtt.connect('mqtt://localhost:1883', {
  username: 'mqtt_user',
  password: 'mqtt_pass'
});

client.on('connect', () => {
  // Подписаться на все топики устройства
  client.subscribe('smartcolumn/abc123/#');
});

client.on('message', (topic, message) => {
  console.log(`${topic}: ${message.toString()}`);

  if (topic.endsWith('/state')) {
    const state = JSON.parse(message.toString());
    console.log(`Температура куба: ${state.temperatures.cube}°C`);
    console.log(`Мощность: ${state.power.power}Вт`);
  }

  if (topic.endsWith('/health')) {
    const health = parseInt(message.toString());
    console.log(`Здоровье системы: ${health}%`);
    if (health < 80) {
      console.warn('Внимание: Низкое здоровье системы!');
    }
  }
});
```

### Home Assistant - Energy Dashboard

Добавьте в `configuration.yaml`:

```yaml
mqtt:
  sensor:
    - name: "Smart Column Энергия"
      state_topic: "smartcolumn/abc123/state"
      value_template: "{{ value_json.power.energy }}"
      unit_of_measurement: "kWh"
      device_class: energy
      state_class: total_increasing
```

Затем добавьте в Energy Dashboard:
1. Настройки → Панели управления → Энергия
2. Добавить источник энергии
3. Выбрать "Smart Column Энергия"

### JavaScript - WebSocket подключение

```javascript
const ws = new WebSocket('ws://192.168.1.100/ws');

ws.onopen = () => {
  console.log('WebSocket подключен');

  // Отправить команду
  ws.send(JSON.stringify({
    command: 'set_power',
    value: 85
  }));
};

ws.onmessage = (event) => {
  const data = JSON.parse(event.data);

  if (data.type === 'status') {
    console.log(`Температура: ${data.data.temperatures.cube}°C`);
    console.log(`Мощность: ${data.data.power.power}Вт`);
  }

  if (data.type === 'event') {
    console.log(`Событие: ${data.event}`);
    console.log(`Данные:`, data.data);
  }
};

ws.onerror = (error) => {
  console.error('WebSocket ошибка:', error);
};

ws.onclose = () => {
  console.log('WebSocket отключен');
};
```

### cURL - Примеры команд

```bash
# Получить статус
curl -u admin:password http://192.168.1.100/api/status

# Получить здоровье системы
curl -u admin:password http://192.168.1.100/api/health

# Установить мощность нагревателя
curl -u admin:password \
  -X POST \
  -H "Content-Type: application/json" \
  -d '{"power": 75}' \
  http://192.168.1.100/api/heater/power

# Запустить ректификацию
curl -u admin:password \
  -X POST \
  -H "Content-Type: application/json" \
  -d '{"mode": 0, "params": {"heads_percent": 8, "body_speed": 100}}' \
  http://192.168.1.100/api/mode/start

# Остановить процесс
curl -u admin:password \
  -X POST \
  http://192.168.1.100/api/mode/stop

# Получить информацию о системе
curl -u admin:password http://192.168.1.100/api/system/info
```

### Python - MQTT публикация и подписка

```python
import paho.mqtt.client as mqtt
import json

def on_connect(client, userdata, flags, rc):
    print(f"Подключено с кодом результата {rc}")
    # Подписаться на все топики устройства
    client.subscribe("smartcolumn/abc123/#")

def on_message(client, userdata, msg):
    print(f"{msg.topic}: {msg.payload.decode()}")

    if msg.topic.endswith('/state'):
        state = json.loads(msg.payload.decode())
        temp = state['temperatures']['cube']
        power = state['power']['power']
        print(f"Куб: {temp}°C, Мощность: {power}Вт")

        # Проверка на превышение температуры
        if temp > 95:
            print("⚠️ ВНИМАНИЕ: Высокая температура!")

client = mqtt.Client()
client.username_pw_set("mqtt_user", "mqtt_pass")
client.on_connect = on_connect
client.on_message = on_message

client.connect("192.168.1.100", 1883, 60)
client.loop_forever()
```

---

## Заголовки безопасности

Все HTTP ответы включают заголовки безопасности:

```http
Content-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'
X-Frame-Options: DENY
X-Content-Type-Options: nosniff
X-XSS-Protection: 1; mode=block
Referrer-Policy: strict-origin-when-cross-origin
Permissions-Policy: geolocation=(), microphone=(), camera=()
```

---

## История изменений

### v1.2.1 (2025-12-06)
- Добавлена MQTT интеграция
- Добавлено Home Assistant MQTT Discovery
- Добавлена HTTP Basic Authentication
- Добавлено ограничение запросов (60 req/min)
- Добавлены заголовки безопасности
- Добавлен эндпоинт `/api/health`

### v1.2.0 (2025-12-05)
- Добавлен мониторинг мощности PZEM-004T
- Добавлено отслеживание энергии
- Добавлены обновления здоровья через WebSocket
- Добавлены Telegram оповещения

### v1.1.0 (2025-12-02)
- Первоначальная документация API
- Основные REST эндпоинты
- Поддержка WebSocket

---

*Последнее обновление: 2025-12-06*
