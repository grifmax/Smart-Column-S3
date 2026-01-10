<?php
/**
 * Smart-Column S3 - ESP32 Configuration
 * 
 * Функции для работы с конфигурацией ESP32
 */

/**
 * Загрузить конфигурацию ESP32
 */
function loadESP32Config() {
    $esp32ConfigFile = __DIR__ . '/esp32_config.json';
    
    $default = [
        'enabled' => false,
        'host' => '',
        'port' => 80,
        'useHttps' => false,
        'username' => '',
        'password' => '',
        'timeout' => 5
    ];
    
    if (file_exists($esp32ConfigFile)) {
        $content = @file_get_contents($esp32ConfigFile);
        if ($content !== false) {
            $config = @json_decode($content, true);
            if ($config && is_array($config)) {
                return array_merge($default, $config);
            }
        }
    }
    
    return $default;
}

/**
 * Сохранить конфигурацию ESP32
 */
function saveESP32Config($config) {
    $esp32ConfigFile = __DIR__ . '/esp32_config.json';
    @file_put_contents($esp32ConfigFile, json_encode($config, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE));
}
