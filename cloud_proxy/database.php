<?php
/**
 * Smart-Column S3 - Database Functions
 * 
 * Функции для работы с MySQL базой данных
 */

require_once __DIR__ . '/config.php';

/**
 * Получить соединение с базой данных
 * 
 * @return PDO|null Соединение с БД или null при ошибке
 */
function getDB() {
    static $pdo = null;
    
    if ($pdo !== null) {
        return $pdo;
    }
    
    $host = DB_HOST;
    $dbname = DB_NAME;
    $username = DB_USER;
    $password = DB_PASS;
    
    // Проверяем, что все параметры заданы
    if (empty($dbname) || empty($username)) {
        error_log('Database configuration is incomplete. DB_NAME and DB_USER must be set.');
        return null;
    }
    
    try {
        $dsn = "mysql:host={$host};dbname={$dbname};charset=utf8mb4";
        $options = [
            PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            PDO::ATTR_EMULATE_PREPARES => false,
            PDO::MYSQL_ATTR_INIT_COMMAND => "SET NAMES utf8mb4 COLLATE utf8mb4_unicode_ci"
        ];
        
        $pdo = new PDO($dsn, $username, $password, $options);
        return $pdo;
    } catch (PDOException $e) {
        error_log('Database connection error: ' . $e->getMessage());
        return null;
    }
}

/**
 * Создать SQL для создания таблиц
 * 
 * @return array Массив SQL команд для создания таблиц
 */
function createTables() {
    return [
        // Таблица пользователей
        "CREATE TABLE IF NOT EXISTS `users` (
            `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
            `username` VARCHAR(100) NOT NULL,
            `password_hash` VARCHAR(255) NOT NULL,
            `email` VARCHAR(255) DEFAULT NULL,
            `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            `last_login` TIMESTAMP NULL DEFAULT NULL,
            PRIMARY KEY (`id`),
            UNIQUE KEY `idx_username` (`username`)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",
        
        // Таблица устройств ESP32
        "CREATE TABLE IF NOT EXISTS `esp32_devices` (
            `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
            `user_id` INT UNSIGNED NOT NULL,
            `name` VARCHAR(100) NOT NULL,
            `host` VARCHAR(255) NOT NULL,
            `port` INT UNSIGNED DEFAULT 80,
            `use_https` BOOLEAN DEFAULT FALSE,
            `username` VARCHAR(100) DEFAULT NULL,
            `password_hash` VARCHAR(255) DEFAULT NULL,
            `timeout` INT UNSIGNED DEFAULT 5,
            `is_active` BOOLEAN DEFAULT FALSE,
            `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY (`id`),
            KEY `idx_user_id` (`user_id`),
            CONSTRAINT `fk_esp32_devices_user_id` 
                FOREIGN KEY (`user_id`) 
                REFERENCES `users` (`id`) 
                ON DELETE CASCADE
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",

        // Версия схемы (миграции) — задел на будущее
        "CREATE TABLE IF NOT EXISTS `schema_version` (
            `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
            `version` INT UNSIGNED NOT NULL,
            `applied_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY (`id`),
            KEY `idx_version` (`version`)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",

        // Права/подписки пользователя — задел под платные услуги
        "CREATE TABLE IF NOT EXISTS `user_entitlements` (
            `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
            `user_id` INT UNSIGNED NOT NULL,
            `plan_tier` VARCHAR(32) NOT NULL DEFAULT 'free',
            `expires_at` TIMESTAMP NULL DEFAULT NULL,
            `features_json` LONGTEXT DEFAULT NULL,
            `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY (`id`),
            KEY `idx_user_entitlements_user_id` (`user_id`),
            CONSTRAINT `fk_user_entitlements_user_id`
                FOREIGN KEY (`user_id`)
                REFERENCES `users` (`id`)
                ON DELETE CASCADE
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
    ];
}

/**
 * Инициализировать базу данных (создать таблицы если не существуют)
 * 
 * @return array ['success' => bool, 'errors' => array, 'messages' => array]
 */
function initDatabase() {
    $pdo = getDB();
    
    if ($pdo === null) {
        return [
            'success' => false,
            'errors' => ['Cannot initialize database: connection failed'],
            'messages' => []
        ];
    }
    
    $errors = [];
    $messages = [];
    $tables = createTables();
    $tableNames = ['users', 'esp32_devices', 'schema_version', 'user_entitlements'];
    
    try {
        foreach ($tables as $index => $sql) {
            try {
                $pdo->exec($sql);
                $tableName = $tableNames[$index] ?? ('table_' . $index);
                $messages[] = "Таблица {$tableName} создана/проверена";
            } catch (PDOException $e) {
                $tableName = $tableNames[$index] ?? ('table_' . $index);
                $errorMsg = "Ошибка при создании таблицы {$tableName}: " . $e->getMessage();
                $errors[] = $errorMsg;
                error_log($errorMsg);
            }
        }
        
        return [
            'success' => empty($errors),
            'errors' => $errors,
            'messages' => $messages
        ];
    } catch (Exception $e) {
        $errorMsg = 'Database initialization error: ' . $e->getMessage();
        error_log($errorMsg);
        return [
            'success' => false,
            'errors' => [$errorMsg],
            'messages' => $messages
        ];
    }
}

/**
 * Проверить существование таблицы
 * 
 * @param string $tableName Имя таблицы
 * @return bool true если таблица существует
 */
function tableExists($tableName) {
    $pdo = getDB();
    
    if ($pdo === null) {
        return false;
    }
    
    try {
        // Используем INFORMATION_SCHEMA для более надежной проверки
        $stmt = $pdo->prepare("SELECT COUNT(*) as count FROM INFORMATION_SCHEMA.TABLES 
                               WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ?");
        $stmt->execute([$tableName]);
        $result = $stmt->fetch();
        return $result && (int)$result['count'] > 0;
    } catch (PDOException $e) {
        error_log('Error checking table existence: ' . $e->getMessage());
        // Fallback на старый метод
        try {
            $dbName = DB_NAME;
            $stmt = $pdo->prepare("SHOW TABLES FROM `{$dbName}` LIKE ?");
            $stmt->execute([$tableName]);
            return $stmt->rowCount() > 0;
        } catch (PDOException $e2) {
            error_log('Error checking table existence (fallback): ' . $e2->getMessage());
            return false;
        }
    }
}

/**
 * Получить версию схемы базы данных (для миграций)
 * 
 * @return int|null Версия схемы или null при ошибке
 */
function getSchemaVersion() {
    $pdo = getDB();
    
    if ($pdo === null) {
        return null;
    }
    
    try {
        // Проверяем существование таблицы версий (может быть создана в будущем)
        if (tableExists('schema_version')) {
            $stmt = $pdo->query("SELECT version FROM schema_version ORDER BY id DESC LIMIT 1");
            $result = $stmt->fetch();
            return $result ? (int)$result['version'] : 0;
        }
        
        // Если таблицы версий нет, проверяем наличие основных таблиц
        if (tableExists('users') && tableExists('esp32_devices')) {
            return 1; // Базовая версия схемы
        }
        
        return 0; // Схема не инициализирована
    } catch (PDOException $e) {
        error_log('Error getting schema version: ' . $e->getMessage());
        return null;
    }
}
