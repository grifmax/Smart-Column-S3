# ⚠️ ВАЖНО: Правильный путь для файлов на Timeweb

## Критически важно!

**Все файлы прокси должны быть в папке `public_html` внутри `smart-column-proxy`!**

**Полный путь:** `co111685/smart-column-proxy/public_html`

## Структура директорий

```
~/smart-column-proxy/
├── public_html/          ← ВСЕ ФАЙЛЫ ЗДЕСЬ!
│   ├── api.php
│   ├── server.php
│   ├── config.php
│   ├── start_websocket.php
│   ├── composer.json
│   ├── .htaccess
│   ├── .env
│   └── vendor/          (после composer install)
└── (другие файлы, если нужны)
```

## Правильные команды

### Создание структуры

```bash
# Создайте папку public_html
cd ~/smart-column-proxy
mkdir -p public_html
```

### Загрузка файлов

```bash
# Все файлы копируйте в public_html/
cd ~/smart-column-proxy/public_html

# Файлы должны быть здесь:
ls -la
# Должны видеть: api.php, server.php, config.php и т.д.
```

### Установка зависимостей

```bash
cd ~/smart-column-proxy/public_html
composer install --no-dev
# Или: php ../composer.phar install --no-dev
```

### Создание .env

```bash
cd ~/smart-column-proxy/public_html
nano .env
```

### Запуск WebSocket сервера

```bash
cd ~/smart-column-proxy/public_html
screen -S smart-column-proxy
php start_websocket.php
```

### Настройка поддомена

В панели Timeweb при создании поддомена:
- **Директория:** `/smart-column-proxy/public_html`

## Если файлы уже в неправильном месте

Если файлы в `~/smart-column-proxy` (не в public_html):

```bash
cd ~/smart-column-proxy

# Создайте public_html если нет
mkdir -p public_html

# Переместите файлы
mv *.php public_html/
mv .htaccess public_html/
mv .env public_html/ 2>/dev/null || true
mv composer.json public_html/ 2>/dev/null || true
mv vendor public_html/ 2>/dev/null || true

# Проверьте
ls -la public_html/
```

## Почему это важно?

На Timeweb веб-сервер (Apache/Nginx) ищет файлы в папке `public_html`. 
Без этой папки файлы не будут доступны через HTTP/HTTPS.

## Проверка

```bash
# Должны быть видны файлы
cd ~/smart-column-proxy/public_html
ls -la

# Должны быть:
# - api.php
# - server.php
# - config.php
# - start_websocket.php
# - composer.json
# - .htaccess
# - .env
# - vendor/ (после установки зависимостей)
```

