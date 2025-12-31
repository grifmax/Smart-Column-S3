# Настройка поддомена для spiritcontrol.ru

## Ваш домен

**Основной домен:** `spiritcontrol.ru`

## Создание поддомена

### ШАГ 1: Создание поддомена в панели Timeweb

1. Войдите в панель управления Timeweb
2. Перейдите в **"Домены"** → **"Мои домены"**
3. Найдите домен `spiritcontrol.ru`
4. Нажмите на него или найдите кнопку **"Поддомены"**
5. Нажмите **"Создать поддомен"** или **"Добавить поддомен"**

### ШАГ 2: Настройка поддомена

**Рекомендуемые варианты имени поддомена:**

- `smartcolumn.spiritcontrol.ru` ⭐ (рекомендуется)
- `proxy.spiritcontrol.ru`
- `api.spiritcontrol.ru`
- `sc-proxy.spiritcontrol.ru`

**Заполните форму:**
- **Имя поддомена:** `smartcolumn` (или другое)
- **Домен:** `spiritcontrol.ru`
- **Директория:** `/smart-column-proxy` или `/public_html/smart-column-proxy`

**Важно:** Убедитесь, что указали правильную директорию, где находятся файлы прокси!

### ШАГ 3: Установка SSL сертификата

1. Перейдите в **"Домены"** → **"SSL-сертификаты"**
2. Найдите поддомен `smartcolumn.spiritcontrol.ru`
3. Нажмите **"Установить"** или **"Let's Encrypt"** (бесплатный)
4. Дождитесь установки (обычно 1-2 минуты)

**Проверка HTTPS:**
Откройте в браузере: `https://smartcolumn.spiritcontrol.ru/health`

Должен вернуться JSON (даже если сервер еще не запущен, Apache должен ответить).

## Проверка настройки

### Проверка DNS

```bash
# Проверка поддомена
nslookup smartcolumn.spiritcontrol.ru

# Или
dig smartcolumn.spiritcontrol.ru
```

### Проверка директории

В SSH-консоли:

```bash
# Проверьте, что файлы в правильной директории
cd ~/smart-column-proxy
ls -la

# Должны быть видны:
# - api.php
# - server.php
# - config.php
# - .htaccess
# - .env
```

### Проверка .htaccess

```bash
cat ~/smart-column-proxy/.htaccess
```

Должен содержать правила для перенаправления API запросов.

## Настройка Android приложения

После создания поддомена и установки SSL:

1. Откройте Android приложение
2. Перейдите в **"Настройки устройства"**
3. Включите **"Облачный прокси"**
4. Заполните:
   - **Хост:** `smartcolumn.spiritcontrol.ru`
   - **Порт:** `443` (для HTTPS) или `3000` (для WebSocket напрямую)
   - **Device ID:** `esp32-001` (или другой уникальный)
   - **Токен прокси:** ваш `CLIENT_TOKEN` из .env файла
   - **HTTPS:** Автоматически включено

## Настройка ESP32

В коде ESP32:

```cpp
CloudProxySettings proxy;
proxy.enabled = true;
proxy.url = "smartcolumn.spiritcontrol.ru";  // Ваш поддомен
proxy.deviceId = "esp32-001";  // Должен совпадать с приложением!
proxy.token = "ваш_ESP32_TOKEN_из_файла_.env";
```

## URL для использования

После настройки поддомена:

- **REST API:** `https://smartcolumn.spiritcontrol.ru/api/`
- **Health Check:** `https://smartcolumn.spiritcontrol.ru/health`
- **WebSocket (напрямую):** `wss://smartcolumn.spiritcontrol.ru:3000/`
- **WebSocket (через прокси):** `wss://smartcolumn.spiritcontrol.ru/client?token=...&device=...`

## Решение проблем

### Проблема: "Поддомен не работает"

**Решение:**
1. Проверьте DNS записи (может потребоваться время на распространение)
2. Проверьте, что указана правильная директория
3. Проверьте права на файлы: `chmod 755 ~/smart-column-proxy`

### Проблема: "SSL не установлен"

**Решение:**
1. Подождите 5-10 минут после установки
2. Проверьте в панели статус сертификата
3. Попробуйте установить заново

### Проблема: "404 Not Found"

**Решение:**
1. Проверьте .htaccess файл
2. Проверьте, что mod_rewrite включен (обратитесь в поддержку)
3. Проверьте путь в настройках поддомена

## Готово! ✅

После настройки поддомена:
- ✅ Поддомен создан: `smartcolumn.spiritcontrol.ru`
- ✅ SSL сертификат установлен
- ✅ HTTPS работает
- ✅ Готов к использованию

**Следующий шаг:** Запустите WebSocket сервер и протестируйте подключение!

