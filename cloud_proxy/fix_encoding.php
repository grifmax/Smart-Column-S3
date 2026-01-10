<?php
/**
 * Исправление кодировки users_web.json
 * 
 * Запустите этот файл один раз через браузер для исправления кодировки
 * После исправления удалите этот файл
 */

header('Content-Type: text/html; charset=utf-8');

require_once __DIR__ . '/auth_web.php';

$usersFile = __DIR__ . '/users_web.json';

echo "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Исправление кодировки</title></head><body>";
echo "<h1>Исправление кодировки users_web.json</h1>";

if (file_exists($usersFile)) {
    // Загружаем пользователей через функцию, которая нормализует кодировку
    $users = loadWebUsers();
    
    if (!empty($users)) {
        echo "<p>Найдено пользователей: " . count($users) . "</p>";
        
        // Пересохраняем через функцию saveWebUsers (она нормализует кодировку)
        if (saveWebUsers($users)) {
            echo "<p style='color: green;'><strong>✓ Файл успешно пересохранен в UTF-8</strong></p>";
            
            // Показываем данные для проверки
            echo "<h2>Данные пользователей:</h2>";
            echo "<pre>" . htmlspecialchars(json_encode($users, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE), ENT_QUOTES, 'UTF-8') . "</pre>";
            
            // Проверяем каждого пользователя
            echo "<h2>Проверка кодировки:</h2>";
            foreach ($users as $username => $user) {
                $displayUsername = isset($user['username']) ? $user['username'] : $username;
                $isValid = mb_check_encoding($displayUsername, 'UTF-8');
                $status = $isValid ? "✓ OK" : "✗ ОШИБКА";
                $color = $isValid ? "green" : "red";
                echo "<p style='color: $color;'>Пользователь: " . htmlspecialchars($displayUsername, ENT_QUOTES, 'UTF-8') . " - $status</p>";
            }
        } else {
            echo "<p style='color: red;'><strong>✗ Ошибка при сохранении файла</strong></p>";
        }
    } else {
        echo "<p>Файл пуст или не содержит пользователей</p>";
    }
} else {
    echo "<p style='color: orange;'>Файл users_web.json не найден. Создайте первого пользователя через форму регистрации.</p>";
}

echo "<p><a href='/login.php'>Вернуться на страницу входа</a></p>";
echo "</body></html>";

