<?php
/**
 * Smart-Column S3 - Web Settings API
 * 
 * API для работы с настройками ESP32 из веб-интерфейса
 */

require_once __DIR__ . '/auth_web.php';
require_once __DIR__ . '/esp32_config.php';
require_once __DIR__ . '/database.php';

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

/**
 * Вызов tunnel service (VPS) из cloud_proxy.
 */
function callTunnelService($path, $payload) {
    $base = rtrim(TUNNEL_SERVICE_URL, '/');
    $url = $base . $path;

    $ch = curl_init($url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_TIMEOUT, 10);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 5);
    curl_setopt($ch, CURLOPT_POST, true);
    curl_setopt($ch, CURLOPT_HTTPHEADER, [
        'Content-Type: application/json; charset=utf-8',
        'x-service-key: ' . TUNNEL_SERVICE_KEY
    ]);
    curl_setopt($ch, CURLOPT_POSTFIELDS, json_encode($payload, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES));

    $resp = curl_exec($ch);
    $code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $err = curl_error($ch);
    curl_close($ch);

    return [$code, $resp, $err];
}

// GET /api/web/devices/discovered - устройства, которые недавно «засветились» (есть активный claim)
if ($path === '/api/web/devices/discovered' && $method === 'GET') {
    $pdo = getDB();
    if ($pdo === null) {
        http_response_code(500);
        echo json_encode(['error' => 'DB unavailable'], JSON_UNESCAPED_UNICODE);
        exit;
    }

    try {
        $stmt = $pdo->query(
            "SELECT c.device_uid,
                    MAX(c.expires_at) AS expires_at,
                    MAX(s.last_seen_at) AS last_seen_at
             FROM esp32_device_claims c
             LEFT JOIN esp32_device_sessions s ON s.device_uid = c.device_uid
             WHERE c.expires_at > NOW()
             GROUP BY c.device_uid
             ORDER BY last_seen_at DESC, expires_at DESC
             LIMIT 50"
        );
        $rows = $stmt->fetchAll();

        $devices = [];
        foreach ($rows as $r) {
            $devices[] = [
                'deviceId' => $r['device_uid'],
                'expiresAt' => $r['expires_at'],
                'lastSeenAt' => $r['last_seen_at']
            ];
        }

        echo json_encode(['devices' => $devices], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
        exit;
    } catch (PDOException $e) {
        http_response_code(500);
        echo json_encode(['error' => 'DB error'], JSON_UNESCAPED_UNICODE);
        exit;
    }
}

// POST /api/web/devices/claim - привязать устройство по Device ID + PIN
if ($path === '/api/web/devices/claim' && $method === 'POST') {
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

    $deviceId = strtoupper(trim($input['deviceId'] ?? ''));
    $claimCode = trim($input['claimCode'] ?? '');

    if ($deviceId === '' || $claimCode === '') {
        http_response_code(400);
        echo json_encode(['error' => 'deviceId and claimCode required'], JSON_UNESCAPED_UNICODE);
        exit;
    }

    $pdo = getDB();
    if ($pdo === null) {
        http_response_code(500);
        echo json_encode(['error' => 'DB unavailable'], JSON_UNESCAPED_UNICODE);
        exit;
    }

    try {
        // Берём самый свежий активный claim
        $stmt = $pdo->prepare(
            "SELECT id, claim_salt, claim_hash, expires_at
             FROM esp32_device_claims
             WHERE device_uid = ? AND expires_at > NOW()
             ORDER BY issued_at DESC, id DESC
             LIMIT 1"
        );
        $stmt->execute([$deviceId]);
        $claim = $stmt->fetch();

        if (!$claim) {
            http_response_code(404);
            echo json_encode(['error' => 'Claim not found or expired'], JSON_UNESCAPED_UNICODE);
            exit;
        }

        $expected = hash('sha256', $claim['claim_salt'] . $claimCode);
        if (!hash_equals($claim['claim_hash'], $expected)) {
            http_response_code(403);
            echo json_encode(['error' => 'Invalid claim code'], JSON_UNESCAPED_UNICODE);
            exit;
        }

        // Проверим, не привязано ли устройство к другому пользователю
        $stmt = $pdo->prepare("SELECT id, user_id FROM esp32_devices WHERE device_uid = ? LIMIT 1");
        $stmt->execute([$deviceId]);
        $existing = $stmt->fetch();
        if ($existing && (int)$existing['user_id'] !== (int)$user['id']) {
            http_response_code(409);
            echo json_encode(['error' => 'Device already claimed by another user'], JSON_UNESCAPED_UNICODE);
            exit;
        }

        // Деактивировать остальные устройства пользователя
        $stmt = $pdo->prepare("UPDATE esp32_devices SET is_active = 0 WHERE user_id = ?");
        $stmt->execute([(int)$user['id']]);

        if ($existing) {
            $stmt = $pdo->prepare(
                "UPDATE esp32_devices
                 SET tunnel_enabled = 1,
                     tunnel_status = 'online',
                     is_active = 1,
                     claimed_at = COALESCE(claimed_at, NOW()),
                     name = COALESCE(NULLIF(name,''), 'ESP32 Device')
                 WHERE id = ? AND user_id = ?"
            );
            $stmt->execute([(int)$existing['id'], (int)$user['id']]);
        } else {
            $stmt = $pdo->prepare(
                "INSERT INTO esp32_devices (user_id, device_uid, name, host, port, use_https, username, password_hash, timeout, is_active, tunnel_enabled, tunnel_status, claimed_at)
                 VALUES (?, ?, ?, '', 80, 0, '', NULL, 5, 1, 1, 'online', NOW())"
            );
            $stmt->execute([(int)$user['id'], $deviceId, 'ESP32 Device']);
        }

        // Удалить claim (одноразовый)
        $stmt = $pdo->prepare("DELETE FROM esp32_device_claims WHERE id = ?");
        $stmt->execute([(int)$claim['id']]);

        // Попросить tunnel service выдать токен устройству
        [$code, $resp, $err] = callTunnelService('/api/tunnel/claim/commit', [
            'userId' => (int)$user['id'],
            'deviceId' => $deviceId
        ]);

        if ($err) {
            http_response_code(502);
            echo json_encode(['error' => 'Tunnel service error: ' . $err], JSON_UNESCAPED_UNICODE);
            exit;
        }

        if ($code < 200 || $code >= 300) {
            http_response_code(502);
            echo json_encode(['error' => 'Tunnel service returned HTTP ' . $code, 'details' => $resp], JSON_UNESCAPED_UNICODE);
            exit;
        }

        echo json_encode(['success' => true, 'deviceId' => $deviceId], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
        exit;
    } catch (PDOException $e) {
        http_response_code(500);
        echo json_encode(['error' => 'DB error'], JSON_UNESCAPED_UNICODE);
        exit;
    }
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

