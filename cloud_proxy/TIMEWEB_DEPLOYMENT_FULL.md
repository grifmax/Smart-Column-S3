# Полное руководство: Развертывание PHP-прокси на Timeweb

## Обзор

Это руководство описывает развертывание полного PHP-прокси с WebSocket поддержкой на хостинге Timeweb. Прокси позволяет подключаться к ESP32 устройствам из Android приложения через интернет без настройки роутера.

## Архитектура

```
Android App <--HTTPS/WSS--> Timeweb Hosting
                              ├── api.php (REST API)
                              ├── start_websocket.php (WebSocket Server)
                              └── .env (Конфигурация)
                                    ↓
                              ESP32 Device
```

## Требования

- ✅ PHP 7.4+ (у вас PHP 8.2.28)
- ✅ Composer (требуется обновление до версии 2.x)
- ✅ SSH доступ
- ✅ FTP доступ (опционально)
- ✅ Домен или поддомен

## ⚠️ Важная информация

**Node.js на виртуальном хостинге Timeweb:**
- Node.js работает только как консольная утилита
- Запуск Node.js серверов **не поддерживается** на виртуальном хостинге
- Для Node.js серверов нужен VDS или другой хостинг (например, Railway.app)

**Поэтому используем PHP версию прокси** - она работает на виртуальном хостинге!

См. подробности: [TIMEWEB_NODEJS_LIMITATION.md](TIMEWEB_NODEJS_LIMITATION.md)

## Этап 1: Подготовка файлов

### Вариант A: Через SSH + Git (рекомендуется)

**Преимущества:**
- Быстро и автоматически
- Всегда актуальная версия
- Легко обновлять

**Команды:**

```bash
# Перейдите в директорию прокси
cd ~/smart-column-proxy

# Удалите старые файлы если есть
rm -rf temp 2>/dev/null || true

# Клонируйте репозиторий с правильной веткой
git clone -b claude/smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7 https://github.com/grifmax/Smart-Column-S3.git temp

# Проверьте наличие cloud_proxy
ls -la temp/cloud_proxy/

# Скопируйте PHP файлы
mv temp/cloud_proxy/*.php .

# Скопируйте конфигурационные файлы
mv temp/cloud_proxy/composer.json .
mv temp/cloud_proxy/.htaccess .
mv temp/cloud_proxy/.env.example . 2>/dev/null || true

# Удалите временную папку
rm -rf temp

# Проверьте файлы
ls -la
```

**Должны увидеть:**
- `server.php` - WebSocket сервер
- `api.php` - REST API
- `config.php` - Загрузка конфигурации
- `start_websocket.php` - Скрипт запуска
- `composer.json` - Зависимости
- `.htaccess` - Настройки Apache

### Вариант B: Через FTP

**Преимущества:**
- Визуальный контроль
- Удобно для разовых загрузок
- Не требует Git

**Инструкция:**

1. **Установите FTP клиент:**
   - FileZilla (бесплатно): https://filezilla-project.org/
   - WinSCP (бесплатно): https://winscp.net/
   - Или используйте встроенный FTP клиент в Windows

2. **Подключитесь к серверу:**
   - **Хост:** `vh456.timeweb.ru` или `92.53.96.141`
   - **Порт:** `21` (FTP) или `22` (SFTP)
   - **Логин:** `co111685`
   - **Пароль:** пароль от панели управления Timeweb
   - **Протокол:** FTP или SFTP (рекомендуется SFTP)

3. **Найдите директорию:**
   - Перейдите в `~/smart-column-proxy` или `/home/c/co111685/smart-column-proxy`
   - Если папки нет - создайте её через панель управления или SSH

4. **Загрузите файлы:**
   Из локальной папки `cloud_proxy/` загрузите:
   - `server.php`
   - `api.php`
   - `config.php`
   - `start_websocket.php`
   - `composer.json`
   - `.htaccess`

5. **Проверьте загрузку:**
   - Убедитесь, что все файлы загружены
   - Проверьте права доступа (должны быть 644 для файлов, 755 для директорий)

**Настройка прав через SSH (после загрузки):**
```bash
cd ~/smart-column-proxy
chmod 644 *.php *.json .htaccess
chmod 755 .
```

## Этап 2: Установка зависимостей

### Проверка Composer

```bash
cd ~/smart-column-proxy
composer --version
```

Должно показать: `Composer version 1.9.0` (или новее)

### Установка зависимостей

**Важно:** Если у вас Composer 1.9.0, сначала обновите его:

```bash
# Обновление Composer до версии 2
composer self-update --2

# Проверка версии
composer --version
```

**Установка зависимостей:**

```bash
cd ~/smart-column-proxy
composer install --no-dev
```

**Ожидаемый результат:**
```
Loading composer repositories with package information
Installing dependencies from lock file
Package operations: X installs, 0 updates, 0 removals
  - Installing ratchet/ratchet (v0.4.4)
  - Installing react/socket (v1.15.0)
  - Installing react/stream (v1.3.0)
  - Installing react/event-loop (v1.4.0)
  ...
Writing lock file
Generating autoload files
```

**Если возникли ошибки:**

1. **Composer устарел (версия 1.9.0):**
   ```bash
   # Обновление до версии 2
   composer self-update --2
   
   # Проверка версии
   composer --version
   ```

2. **Пакеты не найдены:**
   - Убедитесь, что используете обновленный `composer.json`
   - Очистите кэш: `composer clear-cache`
   - Попробуйте: `composer install --no-dev`

3. **Проблемы с правами:**
   ```bash
   chmod 755 .
   chmod 644 composer.json
   ```

4. **Подробная диагностика:**
   ```bash
   composer install -vvv
   ```

5. **Если Composer не обновляется:**
   - Обратитесь в поддержку Timeweb для обновления Composer
   - Или используйте Railway.app для Node.js версии

**См. также:** 
- [TIMEWEB_COMPOSER_FIX.md](TIMEWEB_COMPOSER_FIX.md) - Решение проблем с Composer
- [TIMEWEB_NODEJS_LIMITATION.md](TIMEWEB_NODEJS_LIMITATION.md) - Ограничения Node.js на виртуальном хостинге

## Этап 3: Настройка конфигурации

### Создание .env файла

```bash
cd ~/smart-column-proxy
nano .env
```

### Содержимое .env

```env
PORT=3000
ESP32_TOKEN=сгенерированный_токен_32_символа
CLIENT_TOKEN=другой_сгенерированный_токен_32_символа
```

### Генерация токенов

```bash
# Первый токен (для ESP32)
php -r "echo bin2hex(random_bytes(16));"

# Второй токен (для клиентов) - выполните еще раз
php -r "echo bin2hex(random_bytes(16));"
```

**Пример результата:**
```
a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6
z9y8x7w6v5u4t3s2r1q0p9o8n7m6l5k4
```

**Пример .env файла:**
```env
PORT=3000
ESP32_TOKEN=a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6
CLIENT_TOKEN=z9y8x7w6v5u4t3s2r1q0p9o8n7m6l5k4
```

### Сохранение файла

В nano:
- `Ctrl+O` - сохранить
- `Enter` - подтвердить
- `Ctrl+X` - выйти

### Проверка .env

```bash
cat .env
```

Убедитесь, что файл содержит все три переменные.

## Этап 4: Настройка поддомена

### Создание поддомена

1. Войдите в панель управления Timeweb
2. Перейдите в **"Домены"** → **"Мои домены"**
3. Найдите ваш основной домен (например: `co111685.tw1.ru`)
4. Нажмите **"Поддомены"** или **"Создать поддомен"**
5. Заполните:
   - **Имя поддомена:** `smartcolumn` (или другое)
   - **Домен:** выберите основной домен
   - **Директория:** `/smart-column-proxy` или `/public_html/smart-column-proxy`
6. Нажмите **"Создать"**

**Результат:** Получите URL вида `smartcolumn.spiritcontrol.ru`

### Установка SSL сертификата

1. Перейдите в **"Домены"** → **"SSL-сертификаты"**
2. Найдите ваш поддомен
3. Нажмите **"Установить"** или **"Let's Encrypt"** (бесплатный)
4. Дождитесь установки (обычно 1-2 минуты)

**Проверка HTTPS:**
Откройте в браузере: `https://smartcolumn.spiritcontrol.ru/health`

Должен вернуться JSON (даже если сервер еще не запущен, Apache должен ответить).

### Проверка .htaccess

Убедитесь, что файл `.htaccess` загружен и mod_rewrite включен:

```bash
cd ~/smart-column-proxy
ls -la .htaccess
```

Если файла нет - создайте его (см. содержимое в репозитории).

## Этап 5: Запуск WebSocket сервера

### Вариант A: Через screen (рекомендуется)

**Создание screen сессии:**
```bash
cd ~/smart-column-proxy
screen -S smart-column-proxy
```

**Запуск сервера:**
```bash
php start_websocket.php
```

**Отключение от screen:**
- Нажмите `Ctrl+A`, затем `D`

**Подключение обратно:**
```bash
screen -r smart-column-proxy
```

### Вариант B: Через nohup (альтернатива)

```bash
cd ~/smart-column-proxy
nohup php start_websocket.php > websocket.log 2>&1 &
```

**Просмотр логов:**
```bash
tail -f websocket.log
```

### Проверка работы

**Проверка процесса:**
```bash
ps aux | grep php
```

Должен быть виден процесс `php start_websocket.php`

**Проверка порта:**
```bash
netstat -tuln | grep 3000
```

Должен показать, что порт 3000 слушается.

**Ожидаемый вывод сервера:**
```
=================================
Smart-Column S3 Cloud Proxy (PHP)
=================================
Server running on port 3000
ESP32 endpoint: ws://localhost:3000/esp32
Client endpoint: ws://localhost:3000/client
HTTP API: http://localhost:3000/api
=================================
```

## Этап 6: Настройка автозапуска

### Через cron

```bash
crontab -e
```

**Добавьте строку:**
```cron
@reboot cd ~/smart-column-proxy && screen -dmS smart-column-proxy php start_websocket.php
```

**Сохранение:**
- В nano: `Ctrl+O`, `Enter`, `Ctrl+X`
- В vi: `:wq`

**Проверка cron:**
```bash
crontab -l
```

Должна быть видна добавленная строка.

## Этап 7: Проверка работы

### REST API

**Health check:**
```bash
curl https://smartcolumn.co111685.tw1.ru/health
```

**Ожидаемый результат:**
```json
{"status":"ok","timestamp":1234567890,"devicesCount":0}
```

**Список устройств:**
```bash
curl https://smartcolumn.spiritcontrol.ru/api/devices
```

**Ожидаемый результат:**
```json
{"devices":[],"total":0}
```

### WebSocket

**Проверка через screen:**
```bash
screen -r smart-column-proxy
```

Должны быть видны логи подключений.

**Проверка через онлайн инструмент:**
1. Откройте https://www.websocket.org/echo.html
2. Подключитесь к: `wss://smartcolumn.spiritcontrol.ru:3000/client?token=YOUR_CLIENT_TOKEN&device=TEST`

**Примечание:** WebSocket через HTTPS требует проксирования или использования порта напрямую.

## Этап 8: Настройка Android приложения

1. Откройте Android приложение
2. Перейдите в **"Настройки устройства"**
3. Нажмите **"Добавить устройство"** или **"+"**
4. Заполните форму:
   - **Название:** `Моя колонна (Timeweb)`
   - **Хост:** `smartcolumn.co111685.tw1.ru` (ваш поддомен)
   - **Порт:** `443` (для HTTPS) или `3000` (для WebSocket напрямую)
   - **Включите:** "Облачный прокси"
   - **Device ID:** `esp32-001` (или другой уникальный ID)
   - **Токен прокси:** ваш `CLIENT_TOKEN` из .env файла
   - **HTTPS:** Автоматически включено
5. Нажмите **"Проверить подключение"**
6. Если успешно - нажмите **"Сохранить"**

## Этап 9: Настройка ESP32

В коде ESP32 добавьте настройки подключения к прокси:

```cpp
CloudProxySettings proxy;
proxy.enabled = true;
proxy.url = "smartcolumn.spiritcontrol.ru";  // Ваш поддомен
proxy.deviceId = "esp32-001";  // Должен совпадать с приложением!
proxy.token = "a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6";  // ESP32_TOKEN из .env
```

См. подробную инструкцию: [ESP32_INTEGRATION.md](ESP32_INTEGRATION.md)

## Решение проблем

### Проблема: "Composer не найден"

**Решение:**
```bash
# Проверьте путь
which composer

# Если не найден, используйте полный путь
/usr/local/bin/composer install
```

### Проблема: "Ratchet не установлен"

**Решение:**
```bash
cd ~/smart-column-proxy
composer install --no-dev
```

### Проблема: "Порт 3000 уже занят"

**Решение:**
1. Проверьте, что заняло порт:
   ```bash
   netstat -tuln | grep 3000
   ```

2. Остановите старый процесс:
   ```bash
   pkill -f start_websocket.php
   ```

3. Или измените порт в .env:
   ```env
   PORT=3001
   ```

### Проблема: "WebSocket не работает через HTTPS"

**Решение:**
- Используйте порт напрямую: `wss://smartcolumn.spiritcontrol.ru:3000`
- Или настройте Nginx проксирование (обратитесь в поддержку Timeweb)

### Проблема: "REST API возвращает 404"

**Решение:**
1. Проверьте .htaccess:
   ```bash
   ls -la ~/smart-column-proxy/.htaccess
   ```

2. Проверьте mod_rewrite:
   - Обратитесь в поддержку Timeweb для включения mod_rewrite

3. Проверьте путь в настройках поддомена

### Проблема: "Данные не синхронизируются"

**Решение:**
1. Проверьте права на запись:
   ```bash
   chmod 666 ~/smart-column-proxy/devices.json
   chmod 666 ~/smart-column-proxy/commands.json
   ```

2. Проверьте, что файлы создаются:
   ```bash
   ls -la ~/smart-column-proxy/*.json
   ```

## Обновление прокси

### Через Git

```bash
cd ~/smart-column-proxy
git pull
composer install  # Если изменились зависимости
screen -r smart-column-proxy
# Остановите сервер: Ctrl+C
php start_websocket.php
# Отключитесь: Ctrl+A, D
```

### Через FTP

1. Загрузите новые файлы через FTP
2. Перезапустите WebSocket сервер:
   ```bash
   screen -r smart-column-proxy
   # Ctrl+C для остановки
   php start_websocket.php
   # Ctrl+A, D для отключения
   ```

## Мониторинг

### Просмотр логов

**Через screen:**
```bash
screen -r smart-column-proxy
```

**Через файл (если использовали nohup):**
```bash
tail -f ~/smart-column-proxy/websocket.log
```

### Проверка статуса

```bash
# Процесс
ps aux | grep start_websocket.php

# Порт
netstat -tuln | grep 3000

# Файлы данных
ls -lh ~/smart-column-proxy/*.json
```

## Безопасность

### Рекомендации:

1. ✅ Используйте сильные токены (32+ символов)
2. ✅ Регулярно обновляйте зависимости: `composer update`
3. ✅ Используйте HTTPS (SSL сертификат)
4. ✅ Ограничьте доступ по IP (если возможно в панели Timeweb)
5. ✅ Регулярно проверяйте логи на подозрительную активность
6. ✅ Не коммитьте .env файл в Git

## Готово! ✅

После выполнения всех этапов у вас будет:
- ✅ Работающий PHP-прокси на Timeweb
- ✅ REST API через HTTPS
- ✅ WebSocket сервер на порту 3000
- ✅ Готов к использованию из Android приложения
- ✅ Готов к подключению ESP32 устройств

**URL для использования:** `https://smartcolumn.spiritcontrol.ru`

## Полезные команды

```bash
# Статус сервера
screen -r smart-column-proxy

# Перезапуск
screen -r smart-column-proxy
# Ctrl+C, затем:
php start_websocket.php
# Ctrl+A, D

# Остановка
pkill -f start_websocket.php

# Логи
tail -f websocket.log

# Проверка конфигурации
cat .env
```

## Дополнительная документация

- [ESP32_INTEGRATION.md](ESP32_INTEGRATION.md) - Интеграция с ESP32
- [TIMEWEB_PHP_SETUP.md](TIMEWEB_PHP_SETUP.md) - Краткая инструкция
- [TIMEWEB_STEP_BY_STEP.md](TIMEWEB_STEP_BY_STEP.md) - Пошаговые указания

