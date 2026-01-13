<?php
/**
 * Smart-Column S3 - Database Migration Script
 *
 * Идемпотентные миграции схемы БД для cloud-proxy.
 *
 * Использование:
 * - В браузере: https://your-domain.com/database_migrate.php?confirm=yes
 * - CLI: php database_migrate.php
 */
require_once __DIR__ . '/config.php';
require_once __DIR__ . '/database.php';

$isCli = php_sapi_name() === 'cli';
$isWebRequest = !$isCli && isset($_SERVER['REQUEST_METHOD']);

if ($isWebRequest && (!isset($_GET['confirm']) || $_GET['confirm'] !== 'yes')) {
    header('Content-Type: text/html; charset=utf-8');
    ?>
    <!DOCTYPE html>
    <html lang="ru">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Миграции БД</title>
        <style>
            body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; max-width: 800px; margin: 50px auto; padding: 20px; background: #f5f5f5; }
            .container { background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
            .warning { background: #fff3cd; border: 1px solid #ffc107; padding: 15px; border-radius: 4px; margin: 20px 0; }
            .btn { display: inline-block; padding: 12px 24px; background: #dc3545; color: white; text-decoration: none; border-radius: 4px; }
        </style>
    </head>
    <body>
        <div class="container">
            <h1>Миграции базы данных</h1>
            <div class="warning">
                Этот скрипт изменит схему БД (добавит колонки/таблицы для tunnel/claim).\n
                Запускайте только если уверены.
            </div>
            <p><a class="btn" href="?confirm=yes">Запустить миграции</a></p>
        </div>
    </body>
    </html>
    <?php
    exit;
}

header('Content-Type: text/plain; charset=utf-8');

$pdo = getDB();
if ($pdo === null) {
    echo "ERROR: DB connection failed\n";
    exit(1);
}

function columnExists(PDO $pdo, string $table, string $column): bool {
    $stmt = $pdo->prepare(
        "SELECT COUNT(*) AS c
         FROM INFORMATION_SCHEMA.COLUMNS
         WHERE TABLE_SCHEMA = DATABASE()
           AND TABLE_NAME = ?
           AND COLUMN_NAME = ?"
    );
    $stmt->execute([$table, $column]);
    $row = $stmt->fetch();
    return $row && (int)$row['c'] > 0;
}

function indexExists(PDO $pdo, string $table, string $indexName): bool {
    $stmt = $pdo->prepare(
        "SELECT COUNT(*) AS c
         FROM INFORMATION_SCHEMA.STATISTICS
         WHERE TABLE_SCHEMA = DATABASE()
           AND TABLE_NAME = ?
           AND INDEX_NAME = ?"
    );
    $stmt->execute([$table, $indexName]);
    $row = $stmt->fetch();
    return $row && (int)$row['c'] > 0;
}

function applyVersion(PDO $pdo, int $version, callable $fn): void {
    $pdo->beginTransaction();
    try {
        $fn();
        // записать версию
        $stmt = $pdo->prepare("INSERT INTO schema_version (version) VALUES (?)");
        $stmt->execute([$version]);
        $pdo->commit();
        echo "OK: applied schema version {$version}\n";
    } catch (Throwable $e) {
        $pdo->rollBack();
        echo "ERROR: migration {$version} failed: " . $e->getMessage() . "\n";
        throw $e;
    }
}

$current = getSchemaVersion();
if ($current === null) {
    echo "ERROR: cannot read schema version\n";
    exit(1);
}

echo "Current schema version: {$current}\n";

// Миграции должны быть идемпотентными: проверяем наличие колонок/таблиц перед ALTER.
// Версия 2: поля для tunnel/claim + таблицы claims/sessions.
if ($current < 2) {
    applyVersion($pdo, 2, function () use ($pdo) {
        // Обеспечить базовые таблицы
        $res = initDatabase();
        if (!$res['success']) {
            throw new RuntimeException('initDatabase failed: ' . implode('; ', $res['errors']));
        }

        // esp32_devices: добавить колонки если их нет
        $alter = [];
        if (!columnExists($pdo, 'esp32_devices', 'device_uid')) {
            $alter[] = "ADD COLUMN device_uid VARCHAR(32) DEFAULT NULL";
        }
        if (!columnExists($pdo, 'esp32_devices', 'tunnel_enabled')) {
            $alter[] = "ADD COLUMN tunnel_enabled BOOLEAN DEFAULT FALSE";
        }
        if (!columnExists($pdo, 'esp32_devices', 'tunnel_status')) {
            $alter[] = "ADD COLUMN tunnel_status VARCHAR(32) DEFAULT 'offline'";
        }
        if (!columnExists($pdo, 'esp32_devices', 'claimed_at')) {
            $alter[] = "ADD COLUMN claimed_at TIMESTAMP NULL DEFAULT NULL";
        }
        if (!columnExists($pdo, 'esp32_devices', 'last_seen_at')) {
            $alter[] = "ADD COLUMN last_seen_at TIMESTAMP NULL DEFAULT NULL";
        }
        if (!columnExists($pdo, 'esp32_devices', 'firmware_version')) {
            $alter[] = "ADD COLUMN firmware_version VARCHAR(64) DEFAULT NULL";
        }
        if (!columnExists($pdo, 'esp32_devices', 'device_token_hash')) {
            $alter[] = "ADD COLUMN device_token_hash VARCHAR(128) DEFAULT NULL";
        }
        if (!columnExists($pdo, 'esp32_devices', 'device_token_id')) {
            $alter[] = "ADD COLUMN device_token_id VARCHAR(64) DEFAULT NULL";
        }

        // host: сделать не обязательным (если было NOT NULL)
        // Безопасно: просто поставим DEFAULT '' и разрешим NULL через MODIFY, если возможно.
        try {
            $pdo->exec("ALTER TABLE esp32_devices MODIFY COLUMN host VARCHAR(255) DEFAULT ''");
            echo "OK: esp32_devices.host relaxed\n";
        } catch (Throwable $e) {
            // Игнорируем если нет прав или уже так
            echo "WARN: cannot modify esp32_devices.host: " . $e->getMessage() . "\n";
        }

        if (!empty($alter)) {
            $sql = "ALTER TABLE esp32_devices " . implode(", ", $alter);
            $pdo->exec($sql);
            echo "OK: altered esp32_devices (" . count($alter) . " changes)\n";
        }

        if (!indexExists($pdo, 'esp32_devices', 'idx_device_uid')) {
            $pdo->exec("ALTER TABLE esp32_devices ADD UNIQUE KEY idx_device_uid (device_uid)");
            echo "OK: added unique index idx_device_uid\n";
        }

        // Таблицы claims/sessions создаются initDatabase(), но на старых инсталляциях их может не быть:
        if (!tableExists('esp32_device_claims')) {
            $pdo->exec("CREATE TABLE IF NOT EXISTS `esp32_device_claims` (
                `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
                `device_uid` VARCHAR(32) NOT NULL,
                `claim_salt` VARCHAR(64) NOT NULL,
                `claim_hash` VARCHAR(128) NOT NULL,
                `issued_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                `expires_at` TIMESTAMP NOT NULL,
                `ws_session_id` VARCHAR(64) DEFAULT NULL,
                PRIMARY KEY (`id`),
                KEY `idx_claim_device_uid` (`device_uid`),
                KEY `idx_claim_expires_at` (`expires_at`)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");
            echo "OK: created esp32_device_claims\n";
        }

        if (!tableExists('esp32_device_sessions')) {
            $pdo->exec("CREATE TABLE IF NOT EXISTS `esp32_device_sessions` (
                `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
                `device_uid` VARCHAR(32) NOT NULL,
                `ws_session_id` VARCHAR(64) NOT NULL,
                `connected_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                `last_seen_at` TIMESTAMP NULL DEFAULT NULL,
                `fw_version` VARCHAR(64) DEFAULT NULL,
                `ip_info` VARCHAR(255) DEFAULT NULL,
                PRIMARY KEY (`id`),
                KEY `idx_session_device_uid` (`device_uid`),
                KEY `idx_session_ws_session_id` (`ws_session_id`),
                KEY `idx_session_last_seen_at` (`last_seen_at`)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");
            echo "OK: created esp32_device_sessions\n";
        }
    });
}

echo "Done.\n";
