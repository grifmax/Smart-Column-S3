<?php
/**
 * Smart-Column S3 - Web Authentication
 * 
 * Авторизация для веб-интерфейса (сессии)
 */

session_start();

require_once __DIR__ . '/database.php';

/**
 * Нормализовать строку в UTF-8
 */
function normalizeToUtf8($str) {
    if (!is_string($str)) {
        return $str;
    }
    
    // Если строка пустая, возвращаем как есть
    if (strlen($str) === 0) {
        return $str;
    }
    
    // Проверяем, что строка уже в UTF-8
    if (function_exists('mb_check_encoding')) {
        if (mb_check_encoding($str, 'UTF-8')) {
            return $str;
        }
    } else {
        // Если mbstring недоступен, проверяем по-другому
        if (preg_match('//u', $str)) {
            return $str;
        }
    }
    
    // Пытаемся конвертировать в UTF-8
    if (function_exists('mb_convert_encoding')) {
        // Пробуем определить исходную кодировку
        $detected = @mb_detect_encoding($str, ['UTF-8', 'Windows-1251', 'ISO-8859-1', 'Windows-1252', 'CP1251'], true);
        if ($detected && $detected !== 'UTF-8') {
            $converted = @mb_convert_encoding($str, 'UTF-8', $detected);
            if ($converted !== false) {
                return $converted;
            }
        }
        // Если не удалось определить, пробуем принудительно конвертировать
        $converted = @mb_convert_encoding($str, 'UTF-8', 'auto');
        if ($converted !== false && mb_check_encoding($converted, 'UTF-8')) {
            return $converted;
        }
    }
    
    // Последняя попытка: используем iconv если доступен
    if (function_exists('iconv')) {
        $converted = @iconv('UTF-8', 'UTF-8//IGNORE', $str);
        if ($converted !== false) {
            return $converted;
        }
    }
    
    // Если ничего не помогло, возвращаем как есть (может быть уже UTF-8, но с проблемными символами)
    return $str;
}

/**
 * Исправить кодировку массива рекурсивно
 */
function fixEncoding($data) {
    if (is_array($data)) {
        $result = [];
        foreach ($data as $key => $value) {
            $normalizedKey = normalizeToUtf8($key);
            $result[$normalizedKey] = fixEncoding($value);
        }
        return $result;
    } elseif (is_string($data)) {
        return normalizeToUtf8($data);
    }
    return $data;
}

/**
 * Получить пользователя по имени
 */
function getUserByUsername($username) {
    $pdo = getDB();
    if ($pdo === null) {
        return null;
    }
    
    try {
        $stmt = $pdo->prepare("SELECT * FROM users WHERE username = ?");
        $stmt->execute([normalizeToUtf8(trim($username))]);
        $user = $stmt->fetch();
        
        if ($user) {
            return [
                'id' => (int)$user['id'],
                'username' => $user['username'],
                'password' => $user['password_hash'],
                'email' => $user['email'],
                'created' => strtotime($user['created_at']),
                'lastLogin' => $user['last_login'] ? strtotime($user['last_login']) : 0
            ];
        }
        
        return null;
    } catch (PDOException $e) {
        error_log('Error getting user by username: ' . $e->getMessage());
        return null;
    }
}

/**
 * Создать пользователя
 */
function createUser($username, $passwordHash, $email = '') {
    $pdo = getDB();
    if ($pdo === null) {
        return false;
    }
    
    try {
        $stmt = $pdo->prepare("INSERT INTO users (username, password_hash, email) VALUES (?, ?, ?)");
        return $stmt->execute([
            normalizeToUtf8(trim($username)),
            $passwordHash,
            normalizeToUtf8(trim($email))
        ]);
    } catch (PDOException $e) {
        error_log('Error creating user: ' . $e->getMessage());
        return false;
    }
}

/**
 * Обновить пользователя
 */
function updateUser($userId, $data) {
    $pdo = getDB();
    if ($pdo === null) {
        return false;
    }
    
    try {
        $fields = [];
        $values = [];
        
        if (isset($data['password_hash'])) {
            $fields[] = 'password_hash = ?';
            $values[] = $data['password_hash'];
        }
        
        if (isset($data['email'])) {
            $fields[] = 'email = ?';
            $values[] = normalizeToUtf8(trim($data['email']));
        }
        
        if (isset($data['last_login'])) {
            $fields[] = 'last_login = ?';
            $values[] = $data['last_login'] ? date('Y-m-d H:i:s', $data['last_login']) : null;
        }
        
        if (empty($fields)) {
            return false;
        }
        
        $values[] = $userId;
        $sql = "UPDATE users SET " . implode(', ', $fields) . " WHERE id = ?";
        $stmt = $pdo->prepare($sql);
        return $stmt->execute($values);
    } catch (PDOException $e) {
        error_log('Error updating user: ' . $e->getMessage());
        return false;
    }
}

/**
 * Проверить авторизацию
 */
function isAuthenticated() {
    return isset($_SESSION['user_id']) && !empty($_SESSION['user_id']);
}

/**
 * Получить текущего пользователя
 */
function getCurrentUser() {
    if (!isAuthenticated()) {
        return null;
    }
    
    $pdo = getDB();
    if ($pdo === null) {
        return null;
    }
    
    $userId = (int)($_SESSION['user_id'] ?? 0);
    if ($userId <= 0) {
        return null;
    }
    
    try {
        $stmt = $pdo->prepare("SELECT * FROM users WHERE id = ?");
        $stmt->execute([$userId]);
        $user = $stmt->fetch();
        
        if ($user) {
            return [
                'id' => (int)$user['id'],
                'username' => $user['username'],
                'email' => $user['email'],
                'created' => strtotime($user['created_at']),
                'lastLogin' => $user['last_login'] ? strtotime($user['last_login']) : 0
            ];
        }
        
        return null;
    } catch (PDOException $e) {
        error_log('Error getting current user: ' . $e->getMessage());
        return null;
    }
}

/**
 * Получить права/подписку пользователя (задел под платные услуги)
 *
 * Важно: user_id берется из сессии, поэтому доверенный.
 * deviceId НЕ является секретом и не используется как токен.
 */
function getUserEntitlements($userId) {
    $default = [
        'planTier' => 'free',
        'expiresAt' => null,
        'features' => []
    ];

    $pdo = getDB();
    if ($pdo === null) {
        return $default;
    }

    // Если таблица еще не создана (старые установки) — возвращаем дефолт.
    if (!function_exists('tableExists') || !tableExists('user_entitlements')) {
        return $default;
    }

    try {
        $stmt = $pdo->prepare(
            "SELECT plan_tier, expires_at, features_json
             FROM user_entitlements
             WHERE user_id = ?
             ORDER BY created_at DESC, id DESC
             LIMIT 1"
        );
        $stmt->execute([(int)$userId]);
        $row = $stmt->fetch();

        if (!$row) {
            return $default;
        }

        $planTier = $row['plan_tier'] ?: 'free';
        $expiresAt = !empty($row['expires_at']) ? strtotime($row['expires_at']) : null;

        // Если подписка истекла — считаем free (данные оставляем как подсказку).
        if ($expiresAt !== null && $expiresAt > 0 && $expiresAt < time()) {
            $planTier = 'free';
        }

        $features = [];
        if (!empty($row['features_json'])) {
            $decoded = json_decode($row['features_json'], true);
            if (is_array($decoded)) {
                $features = $decoded;
            }
        }

        return [
            'planTier' => $planTier,
            'expiresAt' => $expiresAt,
            'features' => $features
        ];
    } catch (PDOException $e) {
        error_log('Error getting user entitlements: ' . $e->getMessage());
        return $default;
    }
}

/**
 * Получить устройства пользователя
 */
function getUserDevices($userId) {
    $pdo = getDB();
    if ($pdo === null) {
        return [];
    }
    
    try {
        $stmt = $pdo->prepare("SELECT * FROM esp32_devices WHERE user_id = ? ORDER BY created_at DESC");
        $stmt->execute([$userId]);
        return $stmt->fetchAll();
    } catch (PDOException $e) {
        error_log('Error getting user devices: ' . $e->getMessage());
        return [];
    }
}

/**
 * Получить активное устройство пользователя
 */
function getActiveDevice($userId) {
    $pdo = getDB();
    if ($pdo === null) {
        return null;
    }
    
    try {
        $stmt = $pdo->prepare("SELECT * FROM esp32_devices WHERE user_id = ? AND is_active = 1 LIMIT 1");
        $stmt->execute([$userId]);
        $device = $stmt->fetch();
        
        if ($device) {
            return [
                'id' => (int)$device['id'],
                'name' => $device['name'],
                'host' => $device['host'],
                'port' => (int)$device['port'],
                'useHttps' => (bool)$device['use_https'],
                'username' => $device['username'],
                'password' => $device['password_hash'] ? base64_decode($device['password_hash']) : '', // Раскодируем пароль
                'timeout' => (int)$device['timeout'],
                'enabled' => true
            ];
        }
        
        return null;
    } catch (PDOException $e) {
        error_log('Error getting active device: ' . $e->getMessage());
        return null;
    }
}

/**
 * Войти
 */
function login($username, $password) {
    // Нормализуем username в UTF-8
    $username = normalizeToUtf8(trim($username));
    
    $user = getUserByUsername($username);
    
    if (!$user) {
        return ['success' => false, 'message' => 'Неверный логин или пароль'];
    }
    
    if (!password_verify($password, $user['password'])) {
        return ['success' => false, 'message' => 'Неверный логин или пароль'];
    }
    
    $_SESSION['user_id'] = $user['id'];
    updateUser($user['id'], ['last_login' => time()]);
    
    return ['success' => true];
}

/**
 * Зарегистрировать
 */
function register($username, $password, $email = '') {
    // Нормализуем username и email в UTF-8
    $username = normalizeToUtf8(trim($username));
    $email = normalizeToUtf8(trim($email));
    
    // Проверяем существование пользователя
    if (getUserByUsername($username)) {
        return ['success' => false, 'message' => 'Пользователь с таким логином уже существует'];
    }
    
    if (mb_strlen($username, 'UTF-8') < 3) {
        return ['success' => false, 'message' => 'Логин должен содержать минимум 3 символа'];
    }
    
    if (strlen($password) < 6) {
        return ['success' => false, 'message' => 'Пароль должен содержать минимум 6 символов'];
    }
    
    $passwordHash = password_hash($password, PASSWORD_DEFAULT);
    
    if (!createUser($username, $passwordHash, $email)) {
        return ['success' => false, 'message' => 'Ошибка при создании пользователя'];
    }
    
    // Получаем созданного пользователя
    $user = getUserByUsername($username);
    if ($user) {
        $_SESSION['user_id'] = $user['id'];
        return ['success' => true];
    }
    
    return ['success' => false, 'message' => 'Ошибка при создании пользователя'];
}

/**
 * Выйти
 */
function logout() {
    unset($_SESSION['user_id']);
    session_destroy();
}

/**
 * Требовать авторизацию (редирект на login если не авторизован)
 */
function requireAuth() {
    if (!isAuthenticated()) {
        if (strpos($_SERVER['REQUEST_URI'], '/api/') === false) {
            // Для веб-страниц - редирект на login
            header('Location: /login.php');
            exit;
        } else {
            // Для API - JSON ответ
            http_response_code(401);
            header('Content-Type: application/json; charset=utf-8');
            echo json_encode(['error' => 'Unauthorized', 'login_required' => true], JSON_UNESCAPED_UNICODE);
            exit;
        }
    }
}

// Инициализация: создать первого пользователя если нет пользователей
$pdo = getDB();
if ($pdo !== null) {
    try {
        $stmt = $pdo->query("SELECT COUNT(*) as count FROM users");
        $result = $stmt->fetch();
        
        if ($result && (int)$result['count'] === 0) {
            // Создаем администратора по умолчанию
            $adminPassword = password_hash('admin', PASSWORD_DEFAULT);
            createUser('admin', $adminPassword, '');
        }
    } catch (PDOException $e) {
        // Игнорируем ошибки при инициализации - возможно таблицы еще не созданы
        error_log('Error during initial user check: ' . $e->getMessage());
    }
}

