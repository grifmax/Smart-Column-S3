# Развертывание PHP-версии прокси на Timeweb

## ✅ Преимущества PHP-версии

- ✅ Работает сразу (PHP 8.2.28 уже установлен)
- ✅ Не нужно ждать установки Node.js
- ✅ Проще настроить на shared хостинге

## ⚠️ Ограничения

- ⚠️ WebSocket требует Ratchet (нужно установить через Composer)
- ⚠️ Для WebSocket нужен отдельный процесс (через screen/tmux)
- ⚠️ REST API работает через обычный PHP

---

## ШАГ 1: Загрузка файлов

### Вариант A: Через Git (исправленная команда)

В SSH-консоли:

```bash
cd ~/smart-column-proxy

# Клонируйте с указанием ветки
git clone -b claude/smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7 https://github.com/grifmax/Smart-Column-S3.git temp

# Или попробуйте main/master
# git clone -b main https://github.com/grifmax/Smart-Column-S3.git temp

# Проверьте наличие cloud_proxy
ls -la temp/

# Если есть cloud_proxy:
mv temp/cloud_proxy/* .
mv temp/cloud_proxy/.* . 2>/dev/null || true
rm -rf temp

# Проверьте файлы
ls -la
```

### Вариант B: Через файловый менеджер

1. В панели Timeweb: **"Файл. менеджер"**
2. Перейдите в `smart-column-proxy`
3. Загрузите файлы:
   - `server.php`
   - `config.php`
   - `api.php`
   - `.htaccess`
   - `composer.json`
   - `start_websocket.php`

---

## ШАГ 2: Установка Composer

### Проверка Composer

```bash
composer --version
```

### Если Composer не установлен:

```bash
# Скачать Composer
curl -sS https://getcomposer.org/installer | php

# Переместить в глобальную директорию
mv composer.phar ~/bin/composer
# Или использовать локально: php composer.phar
```

### Если Composer недоступен:

Обратитесь в поддержку Timeweb для установки Composer.

---

## ШАГ 3: Установка зависимостей

```bash
cd ~/smart-column-proxy

# Установка через Composer
composer install

# Или если composer локально:
php composer.phar install
```

**Ожидаемый результат:** Установка Ratchet и зависимостей

---

## ШАГ 4: Создание .env файла

```bash
cd ~/smart-column-proxy
nano .env
```

Вставьте:

```env
PORT=3000
ESP32_TOKEN=сгенерируйте_токен_32_символа
CLIENT_TOKEN=сгенерируйте_другой_токен_32_символа
```

**Генерация токенов:**
```bash
php -r "echo bin2hex(random_bytes(16));"
```

Выполните дважды для двух токенов.

Сохраните: `Ctrl+O`, `Enter`, `Ctrl+X`

---

## ШАГ 5: Настройка поддомена

1. В панели Timeweb: **"Домены"** → **"Мои домены"**
2. Создайте поддомен: `smartcolumn` (или другое)
3. Укажите директорию: `/smart-column-proxy`
4. Установите SSL сертификат (Let's Encrypt)

---

## ШАГ 6: Запуск WebSocket сервера

### Через screen (рекомендуется)

```bash
cd ~/smart-column-proxy

# Создать screen сессию
screen -S smart-column-proxy

# Запустить WebSocket сервер
php start_websocket.php

# Отключиться: Ctrl+A, затем D
# Подключиться обратно: screen -r smart-column-proxy
```

### Через nohup (альтернатива)

```bash
cd ~/smart-column-proxy
nohup php start_websocket.php > websocket.log 2>&1 &
```

---

## ШАГ 7: Проверка работы

### REST API

Откройте в браузере:
```
https://smartcolumn.yourdomain.com/health
```

Должен вернуться JSON:
```json
{"status":"ok","timestamp":1234567890,"devicesCount":0}
```

### WebSocket

Проверьте логи:
```bash
tail -f websocket.log
```

---

## ШАГ 8: Настройка Android приложения

1. Откройте приложение → **Настройки устройства**
2. Включите **"Облачный прокси"**
3. Заполните:
   - **Хост:** `smartcolumn.yourdomain.com`
   - **Порт:** `443` (для HTTPS) или `3000` (для WebSocket)
   - **Device ID:** `esp32-001`
   - **Токен прокси:** ваш `CLIENT_TOKEN`

---

## Автозапуск WebSocket

### Через cron

```bash
crontab -e
```

Добавьте:
```cron
@reboot cd ~/smart-column-proxy && screen -dmS smart-column-proxy php start_websocket.php
```

---

## Решение проблем

### Проблема: "Composer не найден"

**Решение:**
- Обратитесь в поддержку Timeweb
- Или используйте локальный composer.phar

### Проблема: "Ratchet не установлен"

**Решение:**
```bash
composer install
# Или
php composer.phar install
```

### Проблема: "WebSocket не работает"

**Решение:**
1. Проверьте, что процесс запущен: `ps aux | grep php`
2. Проверьте порт: `netstat -tuln | grep 3000`
3. Проверьте логи: `tail -f websocket.log`

---

## Готово! ✅

После выполнения всех шагов:
- ✅ REST API работает через PHP
- ✅ WebSocket работает через отдельный процесс
- ✅ Готов к использованию из Android приложения

