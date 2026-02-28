# Быстрый старт - Развертывание

## 🎯 Структура на сервере

- **Основной домен:** `smartcolumn/public_html/` 
- **Прокси сервер:** `smart-column-proxy/public_html/`

## 📋 Что делать

### 1. Подготовка файлов

Сначала модифицируйте файлы в `data/`:
- Добавьте настройки ESP32 в `data/index.html` (код из `ESP32_SETTINGS_HTML.txt`)
- Добавьте JavaScript в `data/app.js` (код из `web_esp32_settings.js`)

Затем скопируйте все из `data/` в `cloud_proxy/web/`

### 2. Загрузка на поддомен прокси

Загрузите в `smart-column-proxy/public_html/`:

**PHP файлы:**
- `proxy.php`
- `login.php`
- `auth_web.php`
- `web_settings_api.php`
- `esp32_config.php`
- `.htaccess`

**Директория:**
- `web/` (вся директория)

### 3. Настройка основного домена

В `smartcolumn/public_html/` создайте файл `.htaccess` с редиректом:

```apache
Redirect permanent / https://smart-column-proxy.spiritcontrol.ru/
```

Или создайте `index.php`:

```php
<?php
header('Location: https://smart-column-proxy.spiritcontrol.ru/');
exit;
```

### 4. Готово!

Откройте `https://smart-column-proxy.spiritcontrol.ru/login.php`
- Логин: `admin`
- Пароль: `admin`

## 📚 Подробная документация

- `FILES_FOR_PROXY.md` - полный список файлов для прокси
- `FILES_FOR_MAIN.md` - файлы для основного домена
- `DEPLOYMENT_STRUCTURE.md` - структура директорий
- `ESP32_SETTINGS_INSTRUCTIONS.md` - как модифицировать файлы

