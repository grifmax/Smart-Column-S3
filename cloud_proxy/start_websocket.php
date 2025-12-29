<?php
/**
 * Запуск WebSocket сервера
 * 
 * Запускается через командную строку:
 * php start_websocket.php
 * 
 * Или через screen/tmux для фонового режима
 */

require_once __DIR__ . '/vendor/autoload.php';
require_once __DIR__ . '/server.php';

// Проверка, что Ratchet установлен
if (!class_exists('Ratchet\Server\IoServer')) {
    die("ERROR: Ratchet не установлен. Выполните: composer install\n");
}

// Запуск сервера
$port = defined('PORT') ? PORT : 3000;
$server = IoServer::factory(
    new HttpServer(
        new WsServer(
            new DeviceProxy()
        )
    ),
    $port
);

echo "=================================\n";
echo "Smart-Column S3 Cloud Proxy (PHP)\n";
echo "=================================\n";
echo "Server running on port $port\n";
echo "ESP32 endpoint: ws://localhost:$port/esp32\n";
echo "Client endpoint: ws://localhost:$port/client\n";
echo "HTTP API: http://localhost:$port/api\n";
echo "=================================\n";

$server->run();

