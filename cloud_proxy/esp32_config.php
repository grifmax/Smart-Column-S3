<?php
/**
 * Smart-Column S3 - ESP32 Configuration
 * 
 * Функции для работы с конфигурацией ESP32 (поддержка нескольких устройств)
 */

require_once __DIR__ . '/database.php';
require_once __DIR__ . '/auth_web.php';

/**
 * Загрузить конфигурацию ESP32 (активное устройство текущего пользователя)
 */
function loadESP32Config() {
    $user = getCurrentUser();
    if (!$user) {
        return [
            'enabled' => false,
            'host' => '',
            'port' => 80,
            'useHttps' => false,
            'username' => '',
            'password' => '',
            'timeout' => 5
        ];
    }
    
    $device = getActiveDevice($user['id']);
    if ($device) {
        return $device;
    }
    
    return [
        'enabled' => false,
        'host' => '',
        'port' => 80,
        'useHttps' => false,
        'username' => '',
        'password' => '',
        'timeout' => 5
    ];
}

/**
 * Сохранить конфигурацию ESP32 (обновить активное устройство или создать новое)
 */
function saveESP32Config($config) {
    $user = getCurrentUser();
    if (!$user) {
        return false;
    }
    
    $deviceId = $config['id'] ?? null;
    
    if ($deviceId) {
        // Обновить существующее устройство
        return updateDevice($deviceId, $user['id'], $config);
    } else {
        // Создать новое устройство и сделать его активным
        $data = $config;
        $data['is_active'] = true;
        $deviceId = createDevice($user['id'], $data);
        
        if ($deviceId) {
            // Деактивируем остальные устройства этого пользователя
            setActiveDevice($deviceId, $user['id']);
            return true;
        }
        
        return false;
    }
}

/**
 * Получить устройство по ID
 */
function getDeviceById($deviceId, $userId) {
    $pdo = getDB();
    if ($pdo === null) {
        return null;
    }
    
    try {
        $stmt = $pdo->prepare("SELECT * FROM esp32_devices WHERE id = ? AND user_id = ?");
        $stmt->execute([$deviceId, $userId]);
        $device = $stmt->fetch();
        
        if ($device) {
            return [
                'id' => (int)$device['id'],
                'name' => $device['name'],
                'host' => $device['host'],
                'port' => (int)$device['port'],
                'useHttps' => (bool)$device['use_https'],
                'username' => $device['username'],
                'password' => $device['password_hash'] ? base64_decode($device['password_hash']) : '',
                'timeout' => (int)$device['timeout'],
                'is_active' => (bool)$device['is_active']
            ];
        }
        
        return null;
    } catch (PDOException $e) {
        error_log('Error getting device by ID: ' . $e->getMessage());
        return null;
    }
}

/**
 * Создать новое устройство
 */
function createDevice($userId, $data) {
    $pdo = getDB();
    if ($pdo === null) {
        return false;
    }
    
    try {
        $name = $data['name'] ?? 'ESP32 Device';
        $host = $data['host'] ?? '';
        $port = isset($data['port']) ? (int)$data['port'] : 80;
        $useHttps = isset($data['useHttps']) ? (bool)$data['useHttps'] : false;
        $username = $data['username'] ?? '';
        $password = $data['password'] ?? '';
        $timeout = isset($data['timeout']) ? (int)$data['timeout'] : 5;
        $isActive = isset($data['is_active']) ? (bool)$data['is_active'] : false;
        
        // Шифруем пароль (используем base64 для простоты, можно использовать openssl_encrypt)
        $passwordHash = $password ? base64_encode($password) : null;
        
        // Если это активное устройство, сначала деактивируем остальные
        if ($isActive) {
            $stmt = $pdo->prepare("UPDATE esp32_devices SET is_active = 0 WHERE user_id = ?");
            $stmt->execute([$userId]);
        }
        
        $stmt = $pdo->prepare("INSERT INTO esp32_devices (user_id, name, host, port, use_https, username, password_hash, timeout, is_active) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        $stmt->execute([$userId, $name, $host, $port, $useHttps ? 1 : 0, $username, $passwordHash, $timeout, $isActive ? 1 : 0]);
        
        return $pdo->lastInsertId();
    } catch (PDOException $e) {
        error_log('Error creating device: ' . $e->getMessage());
        return false;
    }
}

/**
 * Обновить устройство
 */
function updateDevice($deviceId, $userId, $data) {
    $pdo = getDB();
    if ($pdo === null) {
        return false;
    }
    
    try {
        // Проверяем, что устройство принадлежит пользователю
        $device = getDeviceById($deviceId, $userId);
        if (!$device) {
            return false;
        }
        
        $fields = [];
        $values = [];
        
        if (isset($data['name'])) {
            $fields[] = 'name = ?';
            $values[] = $data['name'];
        }
        
        if (isset($data['host'])) {
            $fields[] = 'host = ?';
            $values[] = $data['host'];
        }
        
        if (isset($data['port'])) {
            $fields[] = 'port = ?';
            $values[] = (int)$data['port'];
        }
        
        if (isset($data['useHttps'])) {
            $fields[] = 'use_https = ?';
            $values[] = (bool)$data['useHttps'] ? 1 : 0;
        }
        
        if (isset($data['username'])) {
            $fields[] = 'username = ?';
            $values[] = $data['username'];
        }
        
        if (isset($data['password']) && $data['password'] !== '') {
            $fields[] = 'password_hash = ?';
            $values[] = base64_encode($data['password']);
        }
        
        if (isset($data['timeout'])) {
            $fields[] = 'timeout = ?';
            $values[] = (int)$data['timeout'];
        }
        
        if (isset($data['is_active'])) {
            $fields[] = 'is_active = ?';
            $values[] = (bool)$data['is_active'] ? 1 : 0;
            
            // Если делаем устройство активным, деактивируем остальные
            if ($data['is_active']) {
                $stmt = $pdo->prepare("UPDATE esp32_devices SET is_active = 0 WHERE user_id = ? AND id != ?");
                $stmt->execute([$userId, $deviceId]);
            }
        }
        
        if (empty($fields)) {
            return false;
        }
        
        $values[] = $deviceId;
        $values[] = $userId;
        $sql = "UPDATE esp32_devices SET " . implode(', ', $fields) . " WHERE id = ? AND user_id = ?";
        $stmt = $pdo->prepare($sql);
        return $stmt->execute($values);
    } catch (PDOException $e) {
        error_log('Error updating device: ' . $e->getMessage());
        return false;
    }
}

/**
 * Удалить устройство
 */
function deleteDevice($deviceId, $userId) {
    $pdo = getDB();
    if ($pdo === null) {
        return false;
    }
    
    try {
        $stmt = $pdo->prepare("DELETE FROM esp32_devices WHERE id = ? AND user_id = ?");
        return $stmt->execute([$deviceId, $userId]);
    } catch (PDOException $e) {
        error_log('Error deleting device: ' . $e->getMessage());
        return false;
    }
}

/**
 * Установить активное устройство
 */
function setActiveDevice($deviceId, $userId) {
    $pdo = getDB();
    if ($pdo === null) {
        return false;
    }
    
    try {
        // Проверяем, что устройство принадлежит пользователю
        $device = getDeviceById($deviceId, $userId);
        if (!$device) {
            return false;
        }
        
        // Деактивируем все устройства пользователя
        $stmt = $pdo->prepare("UPDATE esp32_devices SET is_active = 0 WHERE user_id = ?");
        $stmt->execute([$userId]);
        
        // Активируем выбранное устройство
        $stmt = $pdo->prepare("UPDATE esp32_devices SET is_active = 1 WHERE id = ? AND user_id = ?");
        return $stmt->execute([$deviceId, $userId]);
    } catch (PDOException $e) {
        error_log('Error setting active device: ' . $e->getMessage());
        return false;
    }
}

/**
 * Получить список всех устройств пользователя
 */
function listUserDevices($userId) {
    $pdo = getDB();
    if ($pdo === null) {
        return [];
    }
    
    try {
        $stmt = $pdo->prepare("SELECT * FROM esp32_devices WHERE user_id = ? ORDER BY is_active DESC, created_at DESC");
        $stmt->execute([$userId]);
        $devices = $stmt->fetchAll();
        
        $result = [];
        foreach ($devices as $device) {
            $result[] = [
                'id' => (int)$device['id'],
                'name' => $device['name'],
                'host' => $device['host'],
                'port' => (int)$device['port'],
                'useHttps' => (bool)$device['use_https'],
                'username' => $device['username'],
                'timeout' => (int)$device['timeout'],
                'is_active' => (bool)$device['is_active'],
                'created_at' => $device['created_at']
            ];
        }
        
        return $result;
    } catch (PDOException $e) {
        error_log('Error listing user devices: ' . $e->getMessage());
        return [];
    }
}
