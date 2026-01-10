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
// Используем путь из REQUEST_URI_API если передан из proxy.php, иначе парсим из REQUEST_URI
if (isset($_SERVER['REQUEST_URI_API'])) {
    $path = $_SERVER['REQUEST_URI_API'];
} else {
    $path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);
}

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
        // Права/подписка (задел под платные услуги)
        $user['entitlements'] = getUserEntitlements($user['id']);
        echo json_encode($user, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    } else {
        http_response_code(401);
        echo json_encode(['error' => 'Unauthorized'], JSON_UNESCAPED_UNICODE);
    }
    exit;
}

// GET /api/web/user/logout - выход из системы
if ($path === '/api/web/user/logout' && $method === 'GET') {
    logout();
    echo json_encode(['success' => true, 'message' => 'Выполнен выход'], JSON_UNESCAPED_UNICODE);
    exit;
}

// GET /api/web/user/account - информация об аккаунте
if ($path === '/api/web/user/account' && $method === 'GET') {
    $user = getCurrentUser();
    if ($user) {
        echo json_encode([
            'id' => $user['id'],
            'username' => $user['username'],
            'email' => $user['email'] ?? '',
            'created' => $user['created'] ?? 0,
            'lastLogin' => $user['lastLogin'] ?? 0
        ], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    } else {
        http_response_code(401);
        echo json_encode(['error' => 'Unauthorized'], JSON_UNESCAPED_UNICODE);
    }
    exit;
}

// GET /api/web/esp32/devices - список всех устройств пользователя
if ($path === '/api/web/esp32/devices' && $method === 'GET') {
    $user = getCurrentUser();
    if (!$user) {
        http_response_code(401);
        echo json_encode(['error' => 'Unauthorized'], JSON_UNESCAPED_UNICODE);
        exit;
    }
    
    $devices = listUserDevices($user['id']);
    echo json_encode(['devices' => $devices], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

// GET /api/web/esp32/devices/{id} - получить устройство по ID
if (preg_match('#^/api/web/esp32/devices/(\d+)$#', $path, $matches) && $method === 'GET') {
    $user = getCurrentUser();
    if (!$user) {
        http_response_code(401);
        echo json_encode(['error' => 'Unauthorized'], JSON_UNESCAPED_UNICODE);
        exit;
    }
    
    $deviceId = (int)$matches[1];
    $device = getDeviceById($deviceId, $user['id']);
    
    if ($device) {
        echo json_encode(['device' => $device], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    } else {
        http_response_code(404);
        echo json_encode(['error' => 'Устройство не найдено'], JSON_UNESCAPED_UNICODE);
    }
    exit;
}

// POST /api/web/esp32/devices - создать новое устройство
if ($path === '/api/web/esp32/devices' && $method === 'POST') {
    $user = getCurrentUser();
    if (!$user) {
        http_response_code(401);
        echo json_encode(['error' => 'Unauthorized'], JSON_UNESCAPED_UNICODE);
        exit;
    }
    
    $input = json_decode(file_get_contents('php://input'), true);
    if (!$input) {
        http_response_code(400);
        echo json_encode(['error' => 'Invalid JSON'], JSON_UNESCAPED_UNICODE);
        exit;
    }
    
    $data = [
        'name' => $input['name'] ?? 'ESP32 Device',
        'host' => $input['host'] ?? '',
        'port' => isset($input['port']) ? (int)$input['port'] : 80,
        'useHttps' => isset($input['useHttps']) ? (bool)$input['useHttps'] : false,
        'username' => $input['username'] ?? '',
        'password' => $input['password'] ?? '',
        'timeout' => isset($input['timeout']) ? (int)$input['timeout'] : 5,
        'is_active' => isset($input['is_active']) ? (bool)$input['is_active'] : false
    ];
    
    $deviceId = createDevice($user['id'], $data);
    
    if ($deviceId) {
        if ($data['is_active']) {
            setActiveDevice($deviceId, $user['id']);
        }
        echo json_encode(['success' => true, 'id' => $deviceId, 'message' => 'Устройство создано'], JSON_UNESCAPED_UNICODE);
    } else {
        http_response_code(500);
        echo json_encode(['error' => 'Ошибка при создании устройства'], JSON_UNESCAPED_UNICODE);
    }
    exit;
}

// PUT /api/web/esp32/devices/{id} - обновить устройство
if (preg_match('#^/api/web/esp32/devices/(\d+)$#', $path, $matches) && $method === 'PUT') {
    $user = getCurrentUser();
    if (!$user) {
        http_response_code(401);
        echo json_encode(['error' => 'Unauthorized'], JSON_UNESCAPED_UNICODE);
        exit;
    }
    
    $deviceId = (int)$matches[1];
    $input = json_decode(file_get_contents('php://input'), true);
    if (!$input) {
        http_response_code(400);
        echo json_encode(['error' => 'Invalid JSON'], JSON_UNESCAPED_UNICODE);
        exit;
    }
    
    $result = updateDevice($deviceId, $user['id'], $input);
    
    if ($result) {
        echo json_encode(['success' => true, 'message' => 'Устройство обновлено'], JSON_UNESCAPED_UNICODE);
    } else {
        http_response_code(404);
        echo json_encode(['error' => 'Устройство не найдено'], JSON_UNESCAPED_UNICODE);
    }
    exit;
}

// DELETE /api/web/esp32/devices/{id} - удалить устройство
if (preg_match('#^/api/web/esp32/devices/(\d+)$#', $path, $matches) && $method === 'DELETE') {
    $user = getCurrentUser();
    if (!$user) {
        http_response_code(401);
        echo json_encode(['error' => 'Unauthorized'], JSON_UNESCAPED_UNICODE);
        exit;
    }
    
    $deviceId = (int)$matches[1];
    $result = deleteDevice($deviceId, $user['id']);
    
    if ($result) {
        echo json_encode(['success' => true, 'message' => 'Устройство удалено'], JSON_UNESCAPED_UNICODE);
    } else {
        http_response_code(404);
        echo json_encode(['error' => 'Устройство не найдено'], JSON_UNESCAPED_UNICODE);
    }
    exit;
}

// POST /api/web/esp32/devices/{id}/activate - установить активное устройство
if (preg_match('#^/api/web/esp32/devices/(\d+)/activate$#', $path, $matches) && $method === 'POST') {
    $user = getCurrentUser();
    if (!$user) {
        http_response_code(401);
        echo json_encode(['error' => 'Unauthorized'], JSON_UNESCAPED_UNICODE);
        exit;
    }
    
    $deviceId = (int)$matches[1];
    $result = setActiveDevice($deviceId, $user['id']);
    
    if ($result) {
        echo json_encode(['success' => true, 'message' => 'Устройство активировано'], JSON_UNESCAPED_UNICODE);
    } else {
        http_response_code(404);
        echo json_encode(['error' => 'Устройство не найдено'], JSON_UNESCAPED_UNICODE);
    }
    exit;
}

http_response_code(404);
echo json_encode(['error' => 'Not found'], JSON_UNESCAPED_UNICODE);

