<?php
/**
 * HTTP API для REST запросов
 */

require_once __DIR__ . '/config.php';

// Заголовки CORS
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');
header('Content-Type: application/json');

// Обработка OPTIONS запроса
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

// Простое хранилище устройств (в продакшене использовать Redis или БД)
$devicesFile = __DIR__ . '/devices.json';

function getDevices() {
    global $devicesFile;
    if (file_exists($devicesFile)) {
        return json_decode(file_get_contents($devicesFile), true) ?: [];
    }
    return [];
}

function saveDevices($devices) {
    global $devicesFile;
    file_put_contents($devicesFile, json_encode($devices, JSON_PRETTY_PRINT));
}

function getCommands($deviceId) {
    $commandsFile = __DIR__ . '/commands.json';
    if (file_exists($commandsFile)) {
        $commands = json_decode(file_get_contents($commandsFile), true) ?: [];
        return $commands[$deviceId] ?? [];
    }
    return [];
}

function saveCommand($deviceId, $command) {
    $commandsFile = __DIR__ . '/commands.json';
    $commands = [];
    if (file_exists($commandsFile)) {
        $commands = json_decode(file_get_contents($commandsFile), true) ?: [];
    }
    
    if (!isset($commands[$deviceId])) {
        $commands[$deviceId] = [];
    }
    
    $commands[$deviceId][] = $command;
    // Оставляем только последние 10 команд
    $commands[$deviceId] = array_slice($commands[$deviceId], -10);
    
    file_put_contents($commandsFile, json_encode($commands, JSON_PRETTY_PRINT));
    return true;
}

// Маршрутизация
$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);
$method = $_SERVER['REQUEST_METHOD'];

// Health check
if ($path === '/health' && $method === 'GET') {
    $devices = getDevices();
    echo json_encode([
        'status' => 'ok',
        'timestamp' => time(),
        'devicesCount' => count($devices)
    ]);
    exit;
}

// Список устройств
if ($path === '/api/devices' && $method === 'GET') {
    $devices = getDevices();
    $deviceList = [];
    foreach ($devices as $deviceId => $device) {
        $isOnline = ($device['online'] ?? false) && ((time() - ($device['lastSeen'] ?? 0)) < 60);
        $deviceList[] = [
            'deviceId' => $deviceId,
            'lastSeen' => $device['lastSeen'] ?? 0,
            'clientsCount' => $device['clientsCount'] ?? 0,
            'online' => $isOnline,
        ];
    }
    echo json_encode([
        'devices' => $deviceList,
        'total' => count($deviceList)
    ]);
    exit;
}

// Статус устройства
if (preg_match('#^/api/device/([^/]+)/status$#', $path, $matches) && $method === 'GET') {
    $deviceId = $matches[1];
    $devices = getDevices();
    
    if (!isset($devices[$deviceId])) {
        http_response_code(503);
        echo json_encode([
            'error' => 'Device offline',
            'deviceId' => $deviceId
        ]);
        exit;
    }
    
    $device = $devices[$deviceId];
    $isOnline = ($device['online'] ?? false) && ((time() - ($device['lastSeen'] ?? 0)) < 60);
    echo json_encode([
        'status' => $isOnline ? 'online' : 'offline',
        'deviceId' => $deviceId,
        'lastSeen' => $device['lastSeen'] ?? 0,
        'clientsCount' => $device['clientsCount'] ?? 0,
        'online' => $isOnline
    ]);
    exit;
}

// Отправка команды
if (preg_match('#^/api/device/([^/]+)/command$#', $path, $matches) && $method === 'POST') {
    $deviceId = $matches[1];
    $devices = getDevices();
    
    // Проверяем, есть ли устройство (даже если offline, команда будет в очереди)
    $device = $devices[$deviceId] ?? null;
    $isOnline = $device && ($device['online'] ?? false) && ((time() - ($device['lastSeen'] ?? 0)) < 60);
    
    $input = json_decode(file_get_contents('php://input'), true);
    $command = [
        'type' => 'command',
        'command' => $input['command'] ?? '',
        'data' => $input['data'] ?? [],
        'timestamp' => time()
    ];
    
    // Сохраняем команду в очередь
    saveCommand($deviceId, $command);
    
    if (!$isOnline) {
        // Устройство offline - команда в очереди
        echo json_encode([
            'success' => true,
            'message' => 'Command queued (device offline)',
            'queued' => true
        ]);
    } else {
        // Устройство online - команда будет отправлена при следующем подключении или сразу если WebSocket активен
        echo json_encode([
            'success' => true,
            'message' => 'Command sent',
            'queued' => false
        ]);
    }
    exit;
}

// 404
http_response_code(404);
echo json_encode(['error' => 'Not found']);

