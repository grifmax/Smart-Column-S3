<?php
/**
 * Smart-Column S3 - Web Settings API
 * 
 * API для работы с настройками ESP32 из веб-интерфейса
 */

require_once __DIR__ . '/auth_web.php';
require_once __DIR__ . '/esp32_config.php';

// Устанавливаем кодировку UTF-8 для ответа
header('Content-Type: application/json; charset=utf-8');

// Требуем авторизацию
requireAuth();

$method = $_SERVER['REQUEST_METHOD'];
$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);

// GET /api/web/esp32/config - получить настройки ESP32
if ($path === '/api/web/esp32/config' && $method === 'GET') {
    $config = loadESP32Config();
    // Не возвращаем пароль
    unset($config['password']);
    echo json_encode($config, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

// POST /api/web/esp32/config - сохранить настройки ESP32
if ($path === '/api/web/esp32/config' && $method === 'POST') {
    $input = json_decode(file_get_contents('php://input'), true);
    
    if (!$input) {
        http_response_code(400);
        echo json_encode(['error' => 'Invalid JSON'], JSON_UNESCAPED_UNICODE);
        exit;
    }
    
    $currentConfig = loadESP32Config();
    
    $config = [
        'enabled' => isset($input['enabled']) ? (bool)$input['enabled'] : $currentConfig['enabled'],
        'host' => trim($input['host'] ?? $currentConfig['host']),
        'port' => (int)($input['port'] ?? $currentConfig['port']),
        'useHttps' => isset($input['useHttps']) ? (bool)$input['useHttps'] : $currentConfig['useHttps'],
        'username' => trim($input['username'] ?? $currentConfig['username']),
        'password' => isset($input['password']) && !empty($input['password']) 
            ? trim($input['password']) 
            : $currentConfig['password'], // Сохраняем старый если новый не указан
        'timeout' => (int)($input['timeout'] ?? $currentConfig['timeout'])
    ];
    
    saveESP32Config($config);
    
        echo json_encode(['success' => true, 'message' => 'Настройки сохранены'], JSON_UNESCAPED_UNICODE);
    exit;
}

// POST /api/web/esp32/test - проверить подключение
if ($path === '/api/web/esp32/test' && $method === 'POST') {
    $config = loadESP32Config();
    
    if (empty($config['host'])) {
        http_response_code(400);
        echo json_encode(['success' => false, 'error' => 'Укажите адрес ESP32'], JSON_UNESCAPED_UNICODE);
        exit;
    }
    
    $protocol = $config['useHttps'] ? 'https' : 'http';
    $port = $config['port'];
    $host = $config['host'];
    $url = "{$protocol}://{$host}";
    if ($port != 80 && $port != 443) {
        $url .= ":{$port}";
    }
    $url .= '/health';
    
    $ch = curl_init($url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_TIMEOUT, $config['timeout']);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, $config['timeout']);
    
    if (!empty($config['username'])) {
        curl_setopt($ch, CURLOPT_USERPWD, "{$config['username']}:{$config['password']}");
    }
    
    $response = curl_exec($ch);
    $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $error = curl_error($ch);
    curl_close($ch);
    
    if ($error) {
        echo json_encode(['success' => false, 'error' => "Ошибка подключения: {$error}"], JSON_UNESCAPED_UNICODE);
    } elseif ($httpCode === 200) {
        echo json_encode(['success' => true, 'message' => "Подключение успешно! HTTP {$httpCode}"], JSON_UNESCAPED_UNICODE);
    } else {
        echo json_encode(['success' => false, 'error' => "ESP32 отвечает, но ошибка HTTP {$httpCode}"], JSON_UNESCAPED_UNICODE);
    }
    exit;
}

// GET /api/web/user - получить информацию о текущем пользователе
if ($path === '/api/web/user' && $method === 'GET') {
    $user = getCurrentUser();
    if ($user) {
        // Не возвращаем пароль
        unset($user['password']);
        echo json_encode($user, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    } else {
        http_response_code(401);
        echo json_encode(['error' => 'Unauthorized'], JSON_UNESCAPED_UNICODE);
    }
    exit;
}

http_response_code(404);
echo json_encode(['error' => 'Not found'], JSON_UNESCAPED_UNICODE);

