<?php
/**
 * Smart-Column S3 - Database Initialization Script
 * 
 * Скрипт для инициализации базы данных
 * Создает необходимые таблицы если они не существуют
 * 
 * ВАЖНО: Этот файл можно удалить после инициализации БД,
 * или использовать для миграций в будущем
 * 
 * Использование:
 * - Через веб-браузер: http://your-domain.com/database_init.php
 * - Через командную строку: php database_init.php
 */

require_once __DIR__ . '/config.php';
require_once __DIR__ . '/database.php';

// Проверка, что скрипт запущен из CLI или через веб с подтверждением
$isCli = php_sapi_name() === 'cli';
$isWebRequest = !$isCli && isset($_SERVER['REQUEST_METHOD']);

// Для веб-запроса требуем подтверждение через параметр
if ($isWebRequest && (!isset($_GET['confirm']) || $_GET['confirm'] !== 'yes')) {
    header('Content-Type: text/html; charset=utf-8');
    ?>
    <!DOCTYPE html>
    <html lang="ru">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Инициализация базы данных</title>
        <style>
            body {
                font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
                max-width: 600px;
                margin: 50px auto;
                padding: 20px;
                background: #f5f5f5;
            }
            .container {
                background: white;
                padding: 30px;
                border-radius: 8px;
                box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            }
            h1 {
                color: #333;
                margin-top: 0;
            }
            .warning {
                background: #fff3cd;
                border: 1px solid #ffc107;
                padding: 15px;
                border-radius: 4px;
                margin: 20px 0;
            }
            .info {
                background: #d1ecf1;
                border: 1px solid #0c5460;
                padding: 15px;
                border-radius: 4px;
                margin: 20px 0;
            }
            .btn {
                display: inline-block;
                padding: 12px 24px;
                background: #007bff;
                color: white;
                text-decoration: none;
                border-radius: 4px;
                margin: 10px 5px 0 0;
            }
            .btn:hover {
                background: #0056b3;
            }
            .btn-danger {
                background: #dc3545;
            }
            .btn-danger:hover {
                background: #c82333;
            }
            pre {
                background: #f4f4f4;
                padding: 15px;
                border-radius: 4px;
                overflow-x: auto;
            }
        </style>
    </head>
    <body>
        <div class="container">
            <h1>Инициализация базы данных</h1>
            
            <div class="warning">
                <strong>⚠️ Внимание!</strong><br>
                Этот скрипт создаст таблицы в базе данных MySQL. 
                Убедитесь, что:
                <ul>
                    <li>База данных создана</li>
                    <li>Параметры подключения настроены в <code>config.php</code></li>
                    <li>У пользователя БД есть права на создание таблиц</li>
                </ul>
            </div>
            
            <div class="info">
                <strong>📋 Что будет создано:</strong>
                <ul>
                    <li>Таблица <code>users</code> - пользователи веб-интерфейса</li>
                    <li>Таблица <code>esp32_devices</code> - устройства ESP32</li>
                </ul>
            </div>
            
            <div class="warning">
                <strong>⚠️ Важно:</strong> Существующие данные не будут удалены, 
                но таблицы будут созданы заново если они не существуют.
            </div>
            
            <p>
                <a href="?confirm=yes" class="btn btn-danger">Продолжить инициализацию</a>
                <a href="/" class="btn">Отмена</a>
            </p>
        </div>
    </body>
    </html>
    <?php
    exit;
}

// Основной код инициализации
header('Content-Type: text/html; charset=utf-8');

?>
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Инициализация базы данных - Результат</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            max-width: 800px;
            margin: 50px auto;
            padding: 20px;
            background: #f5f5f5;
        }
        .container {
            background: white;
            padding: 30px;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h1 {
            color: #333;
            margin-top: 0;
        }
        .success {
            background: #d4edda;
            border: 1px solid #28a745;
            color: #155724;
            padding: 15px;
            border-radius: 4px;
            margin: 20px 0;
        }
        .error {
            background: #f8d7da;
            border: 1px solid #dc3545;
            color: #721c24;
            padding: 15px;
            border-radius: 4px;
            margin: 20px 0;
        }
        .info {
            background: #d1ecf1;
            border: 1px solid #0c5460;
            color: #0c5460;
            padding: 15px;
            border-radius: 4px;
            margin: 20px 0;
        }
        pre {
            background: #f4f4f4;
            padding: 15px;
            border-radius: 4px;
            overflow-x: auto;
            font-size: 14px;
        }
        .btn {
            display: inline-block;
            padding: 12px 24px;
            background: #007bff;
            color: white;
            text-decoration: none;
            border-radius: 4px;
            margin: 10px 5px 0 0;
        }
        .btn:hover {
            background: #0056b3;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Инициализация базы данных</h1>
        
        <?php
        // Проверяем конфигурацию
        $configOk = true;
        $messages = [];
        
        if (empty(DB_NAME)) {
            $configOk = false;
            $messages[] = '❌ DB_NAME не установлен в config.php';
        } else {
            $messages[] = '✓ DB_NAME: ' . DB_NAME;
        }
        
        if (empty(DB_USER)) {
            $configOk = false;
            $messages[] = '❌ DB_USER не установлен в config.php';
        } else {
            $messages[] = '✓ DB_USER: ' . DB_USER;
        }
        
        $messages[] = '✓ DB_HOST: ' . DB_HOST;
        
        if (!$configOk) {
            echo '<div class="error">';
            echo '<strong>Ошибка конфигурации:</strong><br>';
            echo implode('<br>', $messages);
            echo '</div>';
            echo '<p><a href="/" class="btn">Вернуться на главную</a></p>';
            exit;
        }
        
        echo '<div class="info">';
        echo '<strong>Параметры подключения:</strong><br>';
        echo implode('<br>', $messages);
        echo '</div>';
        
        // Пытаемся подключиться к БД
        $pdo = getDB();
        
        if ($pdo === null) {
            echo '<div class="error">';
            echo '<strong>Ошибка подключения к базе данных</strong><br>';
            echo 'Проверьте параметры подключения в config.php и убедитесь, что:<br>';
            echo '<ul>';
            echo '<li>База данных ' . htmlspecialchars(DB_NAME) . ' существует</li>';
            echo '<li>Пользователь ' . htmlspecialchars(DB_USER) . ' имеет права доступа</li>';
            echo '<li>Сервер MySQL доступен по адресу ' . htmlspecialchars(DB_HOST) . '</li>';
            echo '</ul>';
            echo '</div>';
            echo '<p><a href="/" class="btn">Вернуться на главную</a></p>';
            exit;
        }
        
        echo '<div class="success">✓ Подключение к базе данных успешно</div>';
        
        // Инициализируем таблицы
        echo '<div class="info">';
        echo '<strong>Создание таблиц...</strong><br>';
        echo '</div>';
        
        $result = initDatabase();
        
        // Показываем сообщения
        if (!empty($result['messages'])) {
            echo '<div class="info">';
            echo '<strong>Процесс создания:</strong><br>';
            echo '<ul>';
            foreach ($result['messages'] as $msg) {
                echo '<li>' . htmlspecialchars($msg) . '</li>';
            }
            echo '</ul>';
            echo '</div>';
        }
        
        // Показываем ошибки
        if (!empty($result['errors'])) {
            echo '<div class="error">';
            echo '<strong>Ошибки при создании таблиц:</strong><br>';
            echo '<ul>';
            foreach ($result['errors'] as $error) {
                echo '<li>' . htmlspecialchars($error) . '</li>';
            }
            echo '</ul>';
            echo '</div>';
        }
        
        if ($result['success']) {
            echo '<div class="success">';
            echo '<strong>✓ Инициализация базы данных завершена успешно!</strong><br><br>';
            echo 'Созданные таблицы:<br>';
            echo '<ul>';
            
            // Проверяем существование таблиц
            $tables = ['users', 'esp32_devices', 'schema_version', 'user_entitlements'];
            foreach ($tables as $table) {
                if (tableExists($table)) {
                    echo '<li>✓ Таблица <code>' . htmlspecialchars($table) . '</code> существует</li>';
                } else {
                    echo '<li>❌ Таблица <code>' . htmlspecialchars($table) . '</code> не найдена</li>';
                }
            }
            
            echo '</ul>';
            echo '</div>';
            
            echo '<div class="info">';
            echo '<strong>Следующие шаги:</strong><br>';
            echo '<ul>';
            echo '<li>Убедитесь, что auth_web.php обновлен для работы с MySQL</li>';
            echo '<li>Убедитесь, что esp32_config.php обновлен для работы с MySQL</li>';
            echo '<li>Протестируйте регистрацию и вход пользователей</li>';
            echo '<li>Протестируйте добавление устройств ESP32</li>';
            echo '</ul>';
            echo '</div>';
            
            echo '<p>';
            echo '<strong>⚠️ Важно:</strong> После успешной инициализации вы можете удалить этот файл (database_init.php) или оставить его для будущих миграций.';
            echo '</p>';
            
        } else {
            echo '<div class="error">';
            echo '<strong>❌ Ошибка при инициализации базы данных</strong><br>';
            if (!empty($result['errors'])) {
                echo '<ul>';
                foreach ($result['errors'] as $error) {
                    echo '<li>' . htmlspecialchars($error) . '</li>';
                }
                echo '</ul>';
            } else {
                echo 'Проверьте логи сервера для получения подробной информации об ошибке.';
            }
            echo '</div>';
        }
        ?>
        
        <p>
            <a href="/" class="btn">Вернуться на главную</a>
            <a href="database_init.php" class="btn">Повторить инициализацию</a>
        </p>
    </div>
</body>
</html>
