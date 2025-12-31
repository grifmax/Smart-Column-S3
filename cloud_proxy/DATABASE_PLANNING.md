# Планирование базы данных для прокси

## Текущее состояние

Сейчас данные хранятся в файлах:
- `devices.json` - информация об устройствах
- `commands.json` - очередь команд

**Ограничения файлового хранилища:**
- Не подходит для множества устройств
- Нет транзакций
- Проблемы с конкурентным доступом
- Нет истории и аналитики

## Варианты баз данных для Timeweb

### Вариант 1: MySQL/MariaDB (рекомендуется)

**Преимущества:**
- ✅ Обычно доступна на Timeweb хостинге
- ✅ Надежная и проверенная
- ✅ Хорошая производительность
- ✅ Поддержка транзакций

**Настройка:**
1. В панели Timeweb: **"Базы данных"** → **"Создать БД"**
2. Создайте базу данных MySQL
3. Запишите данные подключения:
   - Хост: обычно `localhost`
   - Имя БД: `co111685_proxy` (пример)
   - Пользователь: `co111685_proxy`
   - Пароль: (сгенерируйте)

**Структура таблиц:**
```sql
-- Устройства
CREATE TABLE devices (
    device_id VARCHAR(50) PRIMARY KEY,
    last_seen INT NOT NULL,
    status ENUM('online', 'offline') DEFAULT 'offline',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_last_seen (last_seen),
    INDEX idx_status (status)
);

-- Клиенты (подключенные приложения)
CREATE TABLE clients (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    connected_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    disconnected_at TIMESTAMP NULL,
    FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE,
    INDEX idx_device_id (device_id)
);

-- Команды (очередь)
CREATE TABLE commands (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    command_type VARCHAR(50) NOT NULL,
    command_data TEXT,
    status ENUM('pending', 'sent', 'failed') DEFAULT 'pending',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    sent_at TIMESTAMP NULL,
    FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE,
    INDEX idx_device_status (device_id, status),
    INDEX idx_status (status)
);

-- История сообщений (опционально)
CREATE TABLE messages (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    direction ENUM('to_device', 'from_device') NOT NULL,
    message_data TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE,
    INDEX idx_device_created (device_id, created_at)
);
```

### Вариант 2: SQLite (проще для начала)

**Преимущества:**
- ✅ Не требует настройки сервера
- ✅ Файловая БД (один файл)
- ✅ Легко мигрировать на MySQL позже
- ✅ Подходит для небольшого количества устройств

**Недостатки:**
- ⚠️ Медленнее при большом количестве записей
- ⚠️ Проблемы с конкурентным доступом

**Использование:**
```php
// Простое подключение
$db = new PDO('sqlite:' . __DIR__ . '/proxy.db');
```

### Вариант 3: Redis (для кэширования)

**Преимущества:**
- ✅ Очень быстрая
- ✅ Отлично для временных данных
- ✅ Поддержка pub/sub для WebSocket

**Недостатки:**
- ⚠️ Может быть недоступен на виртуальном хостинге
- ⚠️ Нужен дополнительный сервер

**Использование:**
- Кэширование состояния устройств
- Очередь команд
- Pub/sub для WebSocket уведомлений

## Рекомендуемый план миграции

### Этап 1: Подготовка (сейчас)

1. ✅ Создать .env файл с токенами
2. ✅ Запустить прокси с файловым хранилищем
3. ✅ Протестировать работу

### Этап 2: Создание БД (когда будет нужно)

1. Создать MySQL базу данных в панели Timeweb
2. Создать таблицы (см. структуру выше)
3. Добавить данные подключения в .env:
   ```env
   DB_HOST=localhost
   DB_NAME=co111685_proxy
   DB_USER=co111685_proxy
   DB_PASS=your_password
   ```

### Этап 3: Миграция кода

1. Создать класс `Database` для работы с БД
2. Обновить `server.php` для сохранения в БД
3. Обновить `api.php` для чтения из БД
4. Добавить миграцию данных из файлов в БД

### Этап 4: Оптимизация

1. Добавить индексы для быстрого поиска
2. Настроить кэширование (если нужно)
3. Добавить аналитику и логирование

## Структура файлов для БД

```
cloud_proxy/
├── database/
│   ├── Database.php          # Класс для работы с БД
│   ├── migrations/
│   │   └── 001_create_tables.sql
│   └── models/
│       ├── Device.php
│       ├── Command.php
│       └── Message.php
├── config.php                # Добавить DB конфигурацию
├── server.php                # Использовать Database вместо файлов
└── api.php                   # Использовать Database вместо файлов
```

## Пример класса Database

```php
<?php
class Database {
    private $pdo;
    
    public function __construct() {
        $dsn = sprintf(
            "mysql:host=%s;dbname=%s;charset=utf8mb4",
            DB_HOST,
            DB_NAME
        );
        $this->pdo = new PDO($dsn, DB_USER, DB_PASS);
        $this->pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    }
    
    public function saveDevice($deviceId, $lastSeen, $status) {
        $stmt = $this->pdo->prepare("
            INSERT INTO devices (device_id, last_seen, status)
            VALUES (?, ?, ?)
            ON DUPLICATE KEY UPDATE
                last_seen = VALUES(last_seen),
                status = VALUES(status)
        ");
        return $stmt->execute([$deviceId, $lastSeen, $status]);
    }
    
    public function getDevice($deviceId) {
        $stmt = $this->pdo->prepare("SELECT * FROM devices WHERE device_id = ?");
        $stmt->execute([$deviceId]);
        return $stmt->fetch(PDO::FETCH_ASSOC);
    }
    
    public function queueCommand($deviceId, $commandType, $commandData) {
        $stmt = $this->pdo->prepare("
            INSERT INTO commands (device_id, command_type, command_data)
            VALUES (?, ?, ?)
        ");
        return $stmt->execute([$deviceId, $commandType, json_encode($commandData)]);
    }
    
    public function getPendingCommands($deviceId) {
        $stmt = $this->pdo->prepare("
            SELECT * FROM commands
            WHERE device_id = ? AND status = 'pending'
            ORDER BY created_at ASC
        ");
        $stmt->execute([$deviceId]);
        return $stmt->fetchAll(PDO::FETCH_ASSOC);
    }
}
```

## Когда переходить на БД?

**Переходите на БД, когда:**
- ✅ Нужна история команд и сообщений
- ✅ Более 10 устройств одновременно
- ✅ Нужна аналитика и отчеты
- ✅ Требуется высокая надежность
- ✅ Нужны транзакции

**Пока можно использовать файлы, если:**
- ✅ 1-5 устройств
- ✅ Простое тестирование
- ✅ Не нужна история

## Следующие шаги

1. **Сейчас:** Продолжайте с файловым хранилищем
2. **Когда будет нужно:** Создайте MySQL БД в панели Timeweb
3. **Миграция:** Используйте структуру таблиц из этого документа
4. **Код:** Я помогу создать класс Database когда будете готовы

## Полезные ссылки

- Документация MySQL: https://dev.mysql.com/doc/
- PDO документация: https://www.php.net/manual/ru/book.pdo.php
- Timeweb БД: Панель → "Базы данных"

