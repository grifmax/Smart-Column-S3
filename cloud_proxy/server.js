/**
 * Smart-Column S3 - Cloud Proxy Server
 * 
 * Промежуточный сервер для подключения ESP32 и Android приложения
 * без необходимости настройки роутера
 */

require('dotenv').config();
const express = require('express');
const WebSocket = require('ws');
const http = require('http');
const cors = require('cors');

const app = express();
const server = http.createServer(app);

// Конфигурация
const PORT = process.env.PORT || 3000;
const ESP32_TOKEN = process.env.ESP32_TOKEN || 'change_me_in_production';
const CLIENT_TOKEN = process.env.CLIENT_TOKEN || 'change_me_in_production';

// Middleware
app.use(cors());
app.use(express.json());

// Хранилище подключений ESP32
const devices = new Map();

// WebSocket сервер для ESP32
const wssESP32 = new WebSocket.Server({ 
  server, 
  path: '/esp32',
  verifyClient: (info) => {
    try {
      const url = new URL(info.req.url, 'http://localhost');
      const token = url.searchParams.get('token');
      const deviceId = url.searchParams.get('device');
      
      if (!token || !deviceId) {
        console.log('ESP32 connection rejected: missing token or device ID');
        return false;
      }
      
      if (token !== ESP32_TOKEN) {
        console.log('ESP32 connection rejected: invalid token');
        return false;
      }
      
      return true;
    } catch (e) {
      console.error('ESP32 verification error:', e);
      return false;
    }
  }
});

wssESP32.on('connection', (ws, req) => {
  const url = new URL(req.url, 'http://localhost');
  const deviceId = url.searchParams.get('device');
  
  console.log(`[ESP32] Device connected: ${deviceId}`);
  
  devices.set(deviceId, {
    ws,
    deviceId,
    lastSeen: Date.now(),
    clients: new Set(),
  });
  
  // Отправка heartbeat каждые 30 секунд
  const heartbeat = setInterval(() => {
    if (ws.readyState === WebSocket.OPEN) {
      ws.ping();
    } else {
      clearInterval(heartbeat);
    }
  }, 30000);
  
  ws.on('message', (data) => {
    const device = devices.get(deviceId);
    if (!device) return;
    
    device.lastSeen = Date.now();
    
    // Пересылка данных всем подключенным клиентам
    device.clients.forEach((clientWs) => {
      if (clientWs.readyState === WebSocket.OPEN) {
        try {
          clientWs.send(data);
        } catch (e) {
          console.error(`[ESP32] Error sending to client:`, e);
        }
      }
    });
  });
  
  ws.on('close', () => {
    clearInterval(heartbeat);
    const device = devices.get(deviceId);
    if (device) {
      // Уведомляем всех клиентов об отключении
      device.clients.forEach((clientWs) => {
        if (clientWs.readyState === WebSocket.OPEN) {
          clientWs.send(JSON.stringify({
            type: 'device_offline',
            deviceId,
          }));
        }
      });
      devices.delete(deviceId);
    }
    console.log(`[ESP32] Device disconnected: ${deviceId}`);
  });
  
  ws.on('error', (error) => {
    console.error(`[ESP32] Error for device ${deviceId}:`, error);
  });
});

// WebSocket сервер для клиентов (Android приложение)
const wssClients = new WebSocket.Server({ 
  server, 
  path: '/client',
  verifyClient: (info) => {
    try {
      const url = new URL(info.req.url, 'http://localhost');
      const token = url.searchParams.get('token');
      const deviceId = url.searchParams.get('device');
      
      if (!token || !deviceId) {
        return false;
      }
      
      if (token !== CLIENT_TOKEN) {
        return false;
      }
      
      return true;
    } catch (e) {
      return false;
    }
  }
});

wssClients.on('connection', (ws, req) => {
  const url = new URL(req.url, 'http://localhost');
  const deviceId = url.searchParams.get('device');
  
  const device = devices.get(deviceId);
  if (!device) {
    ws.close(1008, 'Device not found');
    return;
  }
  
  console.log(`[Client] Connected to device: ${deviceId}`);
  device.clients.add(ws);
  
  // Отправка уведомления о подключении
  ws.send(JSON.stringify({
    type: 'connected',
    deviceId,
    status: 'online',
  }));
  
  ws.on('message', (data) => {
    if (device.ws.readyState === WebSocket.OPEN) {
      device.ws.send(data);
    } else {
      ws.send(JSON.stringify({
        type: 'error',
        message: 'Device offline',
      }));
    }
  });
  
  ws.on('close', () => {
    device.clients.delete(ws);
    console.log(`[Client] Disconnected from device: ${deviceId}`);
  });
  
  ws.on('error', (error) => {
    console.error(`[Client] Error for device ${deviceId}:`, error);
  });
});

// HTTP API для REST запросов
app.get('/api/device/:deviceId/status', (req, res) => {
  const device = devices.get(req.params.deviceId);
  
  if (!device) {
    return res.status(503).json({ 
      error: 'Device offline',
      deviceId: req.params.deviceId,
    });
  }
  
  res.json({
    status: 'online',
    deviceId: device.deviceId,
    lastSeen: device.lastSeen,
    clientsCount: device.clients.size,
  });
});

app.post('/api/device/:deviceId/command', (req, res) => {
  const device = devices.get(req.params.deviceId);
  
  if (!device) {
    return res.status(503).json({ 
      error: 'Device offline',
      deviceId: req.params.deviceId,
    });
  }
  
  if (device.ws.readyState !== WebSocket.OPEN) {
    return res.status(503).json({ 
      error: 'Device connection lost',
    });
  }
  
  const command = {
    type: 'command',
    command: req.body.command,
    data: req.body.data || {},
    timestamp: Date.now(),
  };
  
  device.ws.send(JSON.stringify(command));
  
  res.json({ 
    success: true,
    message: 'Command sent',
  });
});

// Список всех устройств
app.get('/api/devices', (req, res) => {
  const deviceList = Array.from(devices.values()).map(device => ({
    deviceId: device.deviceId,
    lastSeen: device.lastSeen,
    clientsCount: device.clients.size,
    online: device.ws.readyState === WebSocket.OPEN,
  }));
  
  res.json({
    devices: deviceList,
    total: deviceList.length,
  });
});

// Health check
app.get('/health', (req, res) => {
  res.json({
    status: 'ok',
    timestamp: Date.now(),
    devicesCount: devices.size,
  });
});

// Запуск сервера
server.listen(PORT, () => {
  console.log('=================================');
  console.log('Smart-Column S3 Cloud Proxy');
  console.log('=================================');
  console.log(`Server running on port ${PORT}`);
  console.log(`ESP32 endpoint: ws://localhost:${PORT}/esp32`);
  console.log(`Client endpoint: ws://localhost:${PORT}/client`);
  console.log(`HTTP API: http://localhost:${PORT}/api`);
  console.log('=================================');
});

// Graceful shutdown
process.on('SIGTERM', () => {
  console.log('SIGTERM received, closing server...');
  server.close(() => {
    console.log('Server closed');
    process.exit(0);
  });
});

