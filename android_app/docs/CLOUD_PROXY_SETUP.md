# Настройка облачного прокси (без настройки роутера)

## Что это?

Облачный прокси - это промежуточный сервер в интернете, который:
1. Принимает подключения от ESP32 (ESP32 сам подключается к серверу)
2. Принимает подключения от Android приложения
3. Проксирует запросы между ними

**Преимущества:**
- ✅ Не требует настройки роутера
- ✅ Работает из коробки
- ✅ ESP32 сам подключается к серверу
- ✅ Безопасно (HTTPS/TLS)

## Архитектура

```
ESP32 (локальная сеть)
  ↓ WebSocket/HTTP
Облачный сервер (VPS/Cloud)
  ↓ HTTPS
Android приложение
```

## Варианты реализации

### Вариант 1: Готовый сервис (ngrok, Cloudflare Tunnel)

См. `PRODUCTION_SOLUTION.md` - Cloudflare Tunnel

### Вариант 2: Собственный сервер

#### Требования:
- VPS сервер (DigitalOcean, AWS, Hetzner - от $5/месяц)
- Домен (опционально, можно использовать IP)
- Node.js/Python сервер

#### Быстрый старт (Node.js)

1. **Создайте сервер:**

```bash
mkdir smart-column-proxy
cd smart-column-proxy
npm init -y
npm install express ws http-proxy-middleware
```

2. **Создайте server.js:**

```javascript
const express = require('express');
const WebSocket = require('ws');
const http = require('http');

const app = express();
const server = http.createServer(app);

// Хранилище подключений ESP32
const devices = new Map();

// WebSocket для ESP32
const wssESP32 = new WebSocket.Server({ 
  server, 
  path: '/esp32',
  verifyClient: (info) => {
    const url = new URL(info.req.url, 'http://localhost');
    const token = url.searchParams.get('token');
    const deviceId = url.searchParams.get('device');
    return token === process.env.ESP32_TOKEN && deviceId;
  }
});

wssESP32.on('connection', (ws, req) => {
  const url = new URL(req.url, 'http://localhost');
  const deviceId = url.searchParams.get('device');
  
  devices.set(deviceId, {
    ws,
    lastSeen: Date.now(),
  });
  
  console.log(`ESP32 connected: ${deviceId}`);
  
  ws.on('message', (data) => {
    // Пересылка данных клиентам
    broadcastToClients(deviceId, data);
  });
  
  ws.on('close', () => {
    devices.delete(deviceId);
    console.log(`ESP32 disconnected: ${deviceId}`);
  });
});

// WebSocket для клиентов
const wssClients = new WebSocket.Server({ 
  server, 
  path: '/client' 
});

function broadcastToClients(deviceId, data) {
  wssClients.clients.forEach((client) => {
    if (client.deviceId === deviceId && client.readyState === WebSocket.OPEN) {
      client.send(data);
    }
  });
}

wssClients.on('connection', (ws, req) => {
  const url = new URL(req.url, 'http://localhost');
  const deviceId = url.searchParams.get('device');
  ws.deviceId = deviceId;
  
  ws.on('message', (data) => {
    const device = devices.get(deviceId);
    if (device && device.ws.readyState === WebSocket.OPEN) {
      device.ws.send(data);
    }
  });
});

// HTTP API
app.use(express.json());

app.get('/api/device/:deviceId/status', async (req, res) => {
  const device = devices.get(req.params.deviceId);
  if (!device) {
    return res.status(503).json({ error: 'Device offline' });
  }
  
  // Запрос статуса через WebSocket
  // (требует реализации протокола запрос-ответ)
  res.json({ status: 'online' });
});

app.post('/api/device/:deviceId/command', async (req, res) => {
  const device = devices.get(req.params.deviceId);
  if (!device) {
    return res.status(503).json({ error: 'Device offline' });
  }
  
  device.ws.send(JSON.stringify({
    type: 'command',
    command: req.body.command,
    data: req.body.data,
  }));
  
  res.json({ success: true });
});

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
  console.log(`Proxy server running on port ${PORT}`);
});
```

3. **Запуск:**

```bash
ESP32_TOKEN=your_secret_token node server.js
```

4. **Настройка ESP32:**

ESP32 должен подключаться к серверу:

```cpp
// В коде ESP32
WiFiClientSecure client;
client.setInsecure(); // Для разработки

if (client.connect("your-server.com", 443)) {
  // Подключение установлено
  // Отправка данных
}
```

5. **Использование в приложении:**

- Хост: `your-server.com`
- Порт: `443`
- HTTPS: Включено
- Device ID: ID вашего ESP32

---

## Готовое решение

Я могу создать:
1. ✅ Облачный прокси-сервер (Node.js)
2. ✅ Код для ESP32 (подключение к прокси)
3. ✅ Интеграцию в Android приложение

Нужно ли создать полное решение?

