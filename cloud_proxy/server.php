<?php
/**
 * Smart-Column S3 - Cloud Proxy Server (PHP Version)
 * 
 * Промежуточный сервер для подключения ESP32 и Android приложения
 * без необходимости настройки роутера
 * 
 * Требования: PHP 7.4+ с поддержкой WebSocket (Ratchet)
 */

// Загрузка конфигурации
require_once __DIR__ . '/config.php';

// Используем Ratchet для WebSocket
use Ratchet\MessageComponentInterface;
use Ratchet\ConnectionInterface;
use Ratchet\Server\IoServer;
use Ratchet\Http\HttpServer;
use Ratchet\WebSocket\WsServer;

// Хранилище подключений
class DeviceProxy implements MessageComponentInterface {
    protected $devices = [];
    protected $clients = [];
    
    public function onOpen(ConnectionInterface $conn) {
        $path = $conn->httpRequest->getUri()->getPath();
        $query = $conn->httpRequest->getUri()->getQuery();
        parse_str($query, $params);
        
        // Проверка токена и device ID
        $token = $params['token'] ?? '';
        $deviceId = $params['device'] ?? '';
        
        if (empty($token) || empty($deviceId)) {
            $conn->close();
            return;
        }
        
        // Определяем тип подключения по пути
        if ($path === '/esp32') {
            // Подключение ESP32
            if ($token !== ESP32_TOKEN) {
                $conn->close();
                return;
            }
            
            $this->devices[$deviceId] = [
                'conn' => $conn,
                'deviceId' => $deviceId,
                'lastSeen' => time(),
                'clients' => []
            ];
            
            error_log("[ESP32] Device connected: $deviceId");
        } elseif ($path === '/client') {
            // Подключение клиента (Android приложение)
            if ($token !== CLIENT_TOKEN) {
                $conn->close();
                return;
            }
            
            if (!isset($this->devices[$deviceId])) {
                $conn->close();
                return;
            }
            
            $this->clients[$conn->resourceId] = [
                'conn' => $conn,
                'deviceId' => $deviceId
            ];
            
            $this->devices[$deviceId]['clients'][] = $conn;
            
            // Отправка уведомления о подключении
            $conn->send(json_encode([
                'type' => 'connected',
                'deviceId' => $deviceId,
                'status' => 'online'
            ]));
            
            error_log("[Client] Connected to device: $deviceId");
        } else {
            $conn->close();
        }
    }
    
    public function onMessage(ConnectionInterface $from, $msg) {
        // Находим устройство или клиента
        $deviceId = null;
        $isDevice = false;
        
        foreach ($this->devices as $id => $device) {
            if ($device['conn'] === $from) {
                $deviceId = $id;
                $isDevice = true;
                break;
            }
        }
        
        if (!$isDevice) {
            // Это клиент - находим его deviceId
            if (isset($this->clients[$from->resourceId])) {
                $deviceId = $this->clients[$from->resourceId]['deviceId'];
            }
        }
        
        if (!$deviceId || !isset($this->devices[$deviceId])) {
            return;
        }
        
        $device = $this->devices[$deviceId];
        
        if ($isDevice) {
            // Сообщение от ESP32 - пересылаем всем клиентам
            $device['lastSeen'] = time();
            foreach ($device['clients'] as $client) {
                if ($client !== $from) {
                    try {
                        $client->send($msg);
                    } catch (Exception $e) {
                        error_log("[ESP32] Error sending to client: " . $e->getMessage());
                    }
                }
            }
        } else {
            // Сообщение от клиента - пересылаем ESP32
            if (isset($device['conn'])) {
                try {
                    $device['conn']->send($msg);
                } catch (Exception $e) {
                    $from->send(json_encode([
                        'type' => 'error',
                        'message' => 'Device offline'
                    ]));
                }
            }
        }
    }
    
    public function onClose(ConnectionInterface $conn) {
        // Удаляем из устройств
        foreach ($this->devices as $deviceId => $device) {
            if ($device['conn'] === $conn) {
                // Уведомляем всех клиентов об отключении
                foreach ($device['clients'] as $client) {
                    try {
                        $client->send(json_encode([
                            'type' => 'device_offline',
                            'deviceId' => $deviceId
                        ]));
                    } catch (Exception $e) {
                        // Игнорируем ошибки
                    }
                }
                unset($this->devices[$deviceId]);
                error_log("[ESP32] Device disconnected: $deviceId");
                break;
            }
        }
        
        // Удаляем из клиентов
        if (isset($this->clients[$conn->resourceId])) {
            $deviceId = $this->clients[$conn->resourceId]['deviceId'];
            if (isset($this->devices[$deviceId])) {
                $key = array_search($conn, $this->devices[$deviceId]['clients']);
                if ($key !== false) {
                    unset($this->devices[$deviceId]['clients'][$key]);
                }
            }
            unset($this->clients[$conn->resourceId]);
            error_log("[Client] Disconnected from device: $deviceId");
        }
    }
    
    public function onError(ConnectionInterface $conn, \Exception $e) {
        error_log("Connection error: " . $e->getMessage());
        $conn->close();
    }
}

// Запуск сервера (только если запускается из командной строки)
if (php_sapi_name() === 'cli') {
    $port = defined('PORT') ? PORT : 3000;
    $server = IoServer::factory(
        new HttpServer(
            new WsServer(
                new DeviceProxy()
            )
        ),
        $port
    );
    
    echo "=================================\n";
    echo "Smart-Column S3 Cloud Proxy (PHP)\n";
    echo "=================================\n";
    echo "Server running on port $port\n";
    echo "ESP32 endpoint: ws://localhost:$port/esp32\n";
    echo "Client endpoint: ws://localhost:$port/client\n";
    echo "=================================\n";
    
    $server->run();
}

