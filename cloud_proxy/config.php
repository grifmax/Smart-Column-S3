<?php
/**
 * Конфигурация прокси-сервера
 */

// Загрузка переменных из .env файла
$envFile = __DIR__ . '/.env';
if (file_exists($envFile)) {
    $lines = file($envFile, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
    foreach ($lines as $line) {
        if (strpos(trim($line), '#') === 0) {
            continue; // Пропускаем комментарии
        }
        list($key, $value) = explode('=', $line, 2);
        $key = trim($key);
        $value = trim($value);
        if (!empty($key)) {
            define($key, $value);
        }
    }
}

// Константы по умолчанию
if (!defined('PORT')) {
    define('PORT', 3000);
}

if (!defined('ESP32_TOKEN')) {
    define('ESP32_TOKEN', 'change_me_in_production');
}

if (!defined('CLIENT_TOKEN')) {
    define('CLIENT_TOKEN', 'change_me_in_production');
}

// Database configuration (для будущего использования)
if (!defined('DB_HOST')) {
    define('DB_HOST', getenv('DB_HOST') ?: 'localhost');
}

if (!defined('DB_NAME')) {
    define('DB_NAME', getenv('DB_NAME') ?: '');
}

if (!defined('DB_USER')) {
    define('DB_USER', getenv('DB_USER') ?: '');
}

if (!defined('DB_PASS')) {
    define('DB_PASS', getenv('DB_PASS') ?: '');
}

