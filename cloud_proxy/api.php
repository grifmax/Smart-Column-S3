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
    file_put_contents($devicesFile, json_encode($devices));
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
        $deviceList[] = [
            'deviceId' => $deviceId,
            'lastSeen' => $device['lastSeen'] ?? 0,
            'clientsCount' => count($device['clients'] ?? []),
            'online' => (time() - ($device['lastSeen'] ?? 0)) < 60, // Онлайн если был активен за последнюю минуту
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
    echo json_encode([
        'status' => 'online',
        'deviceId' => $deviceId,
        'lastSeen' => $device['lastSeen'] ?? 0,
        'clientsCount' => count($device['clients'] ?? [])
    ]);
    exit;
}

// Отправка команды
if (preg_match('#^/api/device/([^/]+)/command$#', $path, $matches) && $method === 'POST') {
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
    
    $input = json_decode(file_get_contents('php://input'), true);
    $command = [
        'type' => 'command',
        'command' => $input['command'] ?? '',
        'data' => $input['data'] ?? [],
        'timestamp' => time()
    ];
    
    // В реальной реализации здесь нужно отправить команду через WebSocket
    // Для упрощения просто возвращаем успех
    echo json_encode([
        'success' => true,
        'message' => 'Command sent'
    ]);
    exit;
}

// 404
http_response_code(404);
echo json_encode(['error' => 'Not found']);

