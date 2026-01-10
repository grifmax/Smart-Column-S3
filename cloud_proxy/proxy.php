<?php
/**
 * Smart-Column S3 - Web Proxy
 * 
 * Прокси для веб-интерфейса ESP32 через spiritcontrol.ru
 * Отдает статические файлы и проксирует API запросы к ESP32
 */

require_once __DIR__ . '/config.php';
require_once __DIR__ . '/esp32_config.php';
require_once __DIR__ . '/auth_web.php';

// Запускаем сессию для авторизации (если еще не запущена)
if (session_status() === PHP_SESSION_NONE) {
    session_start();
}

// Получаем путь запроса
$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);
$method = $_SERVER['REQUEST_METHOD'];

// Убираем начальный слэш для обработки
$path = ltrim($path, '/');

// Исключаем запросы к самому proxy.php/proxy из обработки статических файлов
if ($path === 'proxy' || $path === 'proxy.php') {
    // Если это прямой запрос к proxy без параметров - редирект на главную
    if (empty($_SERVER['QUERY_STRING'])) {
        header('Location: /');
        exit;
    }
    // Иначе продолжаем обработку (может быть запрос с параметрами)
    $path = ''; // Обрабатываем как корневой путь
}

// Если это login (с расширением или без) - обрабатываем отдельно
if ($path === 'login' || $path === 'login.php' || basename($path) === 'login' || basename($path) === 'login.php') {
    require_once __DIR__ . '/login.php';
    exit;
}

// Для корневого пути (пустой путь) проверяем авторизацию сразу
// И для всех HTML файлов проверяем авторизацию
if (empty($path)) {
    // Для корневого пути всегда проверяем авторизацию
    if (!isAuthenticated()) {
        header('Location: /login');
        exit;
    }
} elseif (preg_match('/\.html?$/i', $path)) {
    // Для всех HTML файлов проверяем авторизацию (кроме login.html)
    if (basename($path) !== 'login.html' && !isAuthenticated()) {
        header('Location: /login');
        exit;
    }
}

/**
 * Проксировать запрос к ESP32
 */
function proxyToESP32($path, $method = 'GET', $data = null, $headers = []) {
    $config = loadESP32Config();
    
    if (!$config['enabled'] || empty($config['host'])) {
        http_response_code(503);
        header('Content-Type: application/json; charset=utf-8');
        echo json_encode(['error' => 'ESP32 not configured'], JSON_UNESCAPED_UNICODE);
        exit;
    }
    
    $protocol = $config['useHttps'] ? 'https' : 'http';
    $port = $config['port'];
    $host = $config['host'];
    
    // Формируем URL
    $url = "{$protocol}://{$host}";
    if ($port != 80 && $port != 443) {
        $url .= ":{$port}";
    }
    $url .= $path;
    
    // Подготовка запроса
    $ch = curl_init($url);
    
    // Базовые настройки
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_FOLLOWLOCATION, true);
    curl_setopt($ch, CURLOPT_TIMEOUT, $config['timeout']);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, $config['timeout']);
    
    // HTTP Basic Auth если указан
    if (!empty($config['username'])) {
        curl_setopt($ch, CURLOPT_USERPWD, "{$config['username']}:{$config['password']}");
    }
    
    // Метод запроса
    if ($method === 'POST') {
        curl_setopt($ch, CURLOPT_POST, true);
        if ($data !== null) {
            curl_setopt($ch, CURLOPT_POSTFIELDS, is_string($data) ? $data : json_encode($data, JSON_UNESCAPED_UNICODE));
        }
    } elseif ($method === 'PUT') {
        curl_setopt($ch, CURLOPT_CUSTOMREQUEST, 'PUT');
        if ($data !== null) {
            curl_setopt($ch, CURLOPT_POSTFIELDS, is_string($data) ? $data : json_encode($data, JSON_UNESCAPED_UNICODE));
        }
    } elseif ($method === 'DELETE') {
        curl_setopt($ch, CURLOPT_CUSTOMREQUEST, 'DELETE');
    }
    
    // Заголовки
    $requestHeaders = ['Content-Type: application/json; charset=utf-8'];
    if (isset($_SERVER['HTTP_ACCEPT'])) {
        $requestHeaders[] = 'Accept: ' . $_SERVER['HTTP_ACCEPT'];
    }
    foreach ($headers as $key => $value) {
        $requestHeaders[] = "$key: $value";
    }
    curl_setopt($ch, CURLOPT_HTTPHEADER, $requestHeaders);
    
    // Выполнение запроса
    $response = curl_exec($ch);
    $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $contentType = curl_getinfo($ch, CURLINFO_CONTENT_TYPE);
    $error = curl_error($ch);
    
    curl_close($ch);
    
    if ($error) {
        http_response_code(502);
        header('Content-Type: application/json; charset=utf-8');
        echo json_encode(['error' => 'ESP32 connection error: ' . $error], JSON_UNESCAPED_UNICODE);
        exit;
    }
    
    // Передаем код ответа и заголовки
    http_response_code($httpCode);
    if ($contentType) {
        header('Content-Type: ' . $contentType);
    }
    
    echo $response;
    exit;
}

// API для настроек веб-интерфейса (требует авторизации)
if (strpos($path, 'api/web/') === 0) {
    require_once __DIR__ . '/web_settings_api.php';
    exit;
}

// API запросы проксируем к ESP32
if (strpos($path, 'api/') === 0) {
    $apiPath = '/' . $path;
    
    // Получаем тело запроса если есть
    $input = null;
    if (in_array($method, ['POST', 'PUT', 'PATCH'])) {
        $input = file_get_contents('php://input');
    }
    
    proxyToESP32($apiPath, $method, $input);
}

// WebSocket проксирование (требует специальной настройки)
if ($path === 'ws' || strpos($path, 'ws/') === 0) {
    http_response_code(501);
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode([
        'error' => 'WebSocket proxy not implemented',
        'note' => 'WebSocket requires special server configuration (nginx/apache proxy)'
    ], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

// Статические файлы
$webDir = __DIR__ . '/web';
$filePath = $webDir . '/' . $path;

// Если путь пустой или это директория, ищем index.html
if (empty($path) || is_dir($filePath)) {
    $filePath = $webDir . '/index.html';
}

// Проверяем существование файла
if (file_exists($filePath) && is_file($filePath)) {
    // Для HTML файлов проверяем авторизацию (кроме login.php)
    $ext = strtolower(pathinfo($filePath, PATHINFO_EXTENSION));
    $fileName = basename($filePath);
    
    // login.php обрабатывается отдельно, не проверяем авторизацию для него
    // Для остальных HTML файлов требуем авторизацию
    if ($ext === 'html' && $fileName !== 'login.html') {
        if (!isAuthenticated()) {
            header('Location: /login');
            exit;
        }
    }
    // Определяем MIME тип
    $mimeTypes = [
        'html' => 'text/html',
        'css' => 'text/css',
        'js' => 'application/javascript',
        'json' => 'application/json',
        'png' => 'image/png',
        'jpg' => 'image/jpeg',
        'jpeg' => 'image/jpeg',
        'gif' => 'image/gif',
        'svg' => 'image/svg+xml',
        'ico' => 'image/x-icon',
        'woff' => 'font/woff',
        'woff2' => 'font/woff2',
        'ttf' => 'font/ttf'
    ];
    
    $ext = strtolower(pathinfo($filePath, PATHINFO_EXTENSION));
    $mime = $mimeTypes[$ext] ?? 'application/octet-stream';
    
    // Добавляем charset для текстовых типов
    if (in_array($ext, ['html', 'css', 'js', 'json', 'svg'])) {
        header('Content-Type: ' . $mime . '; charset=utf-8');
    } else {
        header('Content-Type: ' . $mime);
    }
    header('Content-Length: ' . filesize($filePath));
    
    // Кэширование для статических файлов (1 час)
    if (in_array($ext, ['css', 'js', 'png', 'jpg', 'jpeg', 'gif', 'svg', 'woff', 'woff2'])) {
        header('Cache-Control: public, max-age=3600');
    }
    
    readfile($filePath);
    exit;
}

// 404 - файл не найден
http_response_code(404);
header('Content-Type: text/html; charset=utf-8');
echo '<!DOCTYPE html><html><head><meta charset="UTF-8"><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>File not found: ' . htmlspecialchars($path, ENT_QUOTES, 'UTF-8') . '</p></body></html>';

