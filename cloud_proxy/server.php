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
    protected $devicesFile;
    protected $commandsFile;
    
    public function __construct() {
        $this->devicesFile = __DIR__ . '/devices.json';
        $this->commandsFile = __DIR__ . '/commands.json';
        // Загружаем существующие устройства из файла
        $this->loadDevices();
    }
    
    protected function loadDevices() {
        if (file_exists($this->devicesFile)) {
            $data = json_decode(file_get_contents($this->devicesFile), true);
            if ($data) {
                // Загружаем только метаданные, соединения будут установлены при подключении
                foreach ($data as $deviceId => $device) {
                    $this->devices[$deviceId] = [
                        'conn' => null, // Будет установлено при подключении
                        'deviceId' => $deviceId,
                        'lastSeen' => $device['lastSeen'] ?? 0,
                        'clients' => []
                    ];
                }
            }
        }
    }
    
    protected function saveDevices() {
        $data = [];
        foreach ($this->devices as $deviceId => $device) {
            $data[$deviceId] = [
                'deviceId' => $deviceId,
                'lastSeen' => $device['lastSeen'] ?? time(),
                'clientsCount' => count($device['clients'] ?? []),
                'online' => $device['conn'] !== null
            ];
        }
        file_put_contents($this->devicesFile, json_encode($data, JSON_PRETTY_PRINT));
    }
    
    protected function saveCommand($deviceId, $command) {
        $commands = [];
        if (file_exists($this->commandsFile)) {
            $commands = json_decode(file_get_contents($this->commandsFile), true) ?: [];
        }
        
        if (!isset($commands[$deviceId])) {
            $commands[$deviceId] = [];
        }
        
        $commands[$deviceId][] = $command;
        // Оставляем только последние 10 команд
        $commands[$deviceId] = array_slice($commands[$deviceId], -10);
        
        file_put_contents($this->commandsFile, json_encode($commands, JSON_PRETTY_PRINT));
    }
    
    protected function getCommands($deviceId) {
        if (!file_exists($this->commandsFile)) {
            return [];
        }
        $commands = json_decode(file_get_contents($this->commandsFile), true) ?: [];
        return $commands[$deviceId] ?? [];
    }
    
    protected function clearCommands($deviceId) {
        if (!file_exists($this->commandsFile)) {
            return;
        }
        $commands = json_decode(file_get_contents($this->commandsFile), true) ?: [];
        if (isset($commands[$deviceId])) {
            unset($commands[$deviceId]);
            file_put_contents($this->commandsFile, json_encode($commands, JSON_PRETTY_PRINT));
        }
    }
    
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
            
            // Сохраняем в файл
            $this->saveDevices();
            
            // Отправляем накопленные команды (если есть)
            $pendingCommands = $this->getCommands($deviceId);
            foreach ($pendingCommands as $cmd) {
                try {
                    $conn->send(json_encode($cmd));
                } catch (Exception $e) {
                    error_log("[ESP32] Error sending pending command: " . $e->getMessage());
                }
            }
            $this->clearCommands($deviceId);
            
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
            $this->devices[$deviceId]['lastSeen'] = time();
            $this->saveDevices(); // Сохраняем обновление lastSeen
            
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
                $this->saveDevices(); // Сохраняем удаление устройства
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
                    $this->saveDevices(); // Сохраняем обновление списка клиентов
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

