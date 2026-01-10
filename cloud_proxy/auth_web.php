<?php
/**
 * Smart-Column S3 - Web Authentication
 * 
 * Авторизация для веб-интерфейса (сессии)
 */

session_start();

$usersFile = __DIR__ . '/users_web.json';

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
 * Загрузить пользователей
 */
function loadWebUsers() {
    global $usersFile;
    
    if (file_exists($usersFile)) {
        $content = @file_get_contents($usersFile);
        if ($content !== false) {
            $data = @json_decode($content, true);
            if (json_last_error() === JSON_ERROR_NONE) {
                // Исправляем кодировку данных
                return fixEncoding($data ?: []);
            }
        }
    }
    
    return [];
}

/**
 * Сохранить пользователей
 */
function saveWebUsers($users) {
    global $usersFile;
    
    // Нормализуем все данные перед сохранением
    $normalizedUsers = [];
    foreach ($users as $username => $userData) {
        $normalizedUsername = normalizeToUtf8($username);
        $normalizedUsers[$normalizedUsername] = fixEncoding($userData);
        // Убеждаемся, что username есть в данных
        if (!isset($normalizedUsers[$normalizedUsername]['username'])) {
            $normalizedUsers[$normalizedUsername]['username'] = $normalizedUsername;
        }
    }
    
    // Кодируем в JSON с правильной кодировкой
    $json = json_encode($normalizedUsers, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    
    if ($json === false) {
        error_log('JSON encode error: ' . json_last_error_msg());
        return false;
    }
    
    // Сохраняем файл
    $result = @file_put_contents($usersFile, $json, LOCK_EX);
    
    return $result !== false;
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
    
    $users = loadWebUsers();
    $userId = normalizeToUtf8($_SESSION['user_id'] ?? '');
    
    if (empty($userId) || !isset($users[$userId])) {
        return null;
    }
    
    $user = $users[$userId];
    // Убеждаемся, что username есть в данных пользователя
    if (!isset($user['username'])) {
        $user['username'] = $userId;
    }
    
    return $user;
}

/**
 * Войти
 */
function login($username, $password) {
    // Нормализуем username в UTF-8
    $username = normalizeToUtf8(trim($username));
    
    $users = loadWebUsers();
    
    if (!isset($users[$username])) {
        return ['success' => false, 'message' => 'Неверный логин или пароль'];
    }
    
    if (!password_verify($password, $users[$username]['password'])) {
        return ['success' => false, 'message' => 'Неверный логин или пароль'];
    }
    
    $_SESSION['user_id'] = $username;
    $users[$username]['lastLogin'] = time();
    saveWebUsers($users);
    
    return ['success' => true];
}

/**
 * Зарегистрировать
 */
function register($username, $password, $email = '') {
    // Нормализуем username и email в UTF-8
    $username = normalizeToUtf8(trim($username));
    $email = normalizeToUtf8(trim($email));
    
    $users = loadWebUsers();
    
    if (isset($users[$username])) {
        return ['success' => false, 'message' => 'Пользователь с таким логином уже существует'];
    }
    
    if (mb_strlen($username, 'UTF-8') < 3) {
        return ['success' => false, 'message' => 'Логин должен содержать минимум 3 символа'];
    }
    
    if (strlen($password) < 6) {
        return ['success' => false, 'message' => 'Пароль должен содержать минимум 6 символов'];
    }
    
    $users[$username] = [
        'username' => $username,  // Сохраняем username в данных для API
        'password' => password_hash($password, PASSWORD_DEFAULT),
        'email' => $email,
        'created' => time(),
        'lastLogin' => 0
    ];
    
    saveWebUsers($users);
    
    $_SESSION['user_id'] = $username;
    
    return ['success' => true];
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
$users = loadWebUsers();
if (empty($users)) {
    $users['admin'] = [
        'username' => 'admin',  // Сохраняем username в данных
        'password' => password_hash('admin', PASSWORD_DEFAULT),
        'email' => '',
        'created' => time(),
        'lastLogin' => 0
    ];
    saveWebUsers($users);
}

