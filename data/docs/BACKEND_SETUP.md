# Smart-Column S3 - Backend Setup

## 🎯 Описание

Полноценный PHP бэкенд для Smart-Column S3, который работает как ESP32, но на сервере spiritcontrol.ru.

## ✨ Возможности

- ✅ HTTP Basic Authentication (логин/пароль)
- ✅ Все API endpoints как на ESP32
- ✅ Хранение состояния в JSON файлах
- ✅ Полная совместимость с Android приложением
- ✅ Работает на стандартном PHP (7.4+)

## 📋 Требования

- PHP 7.4 или выше
- Apache с mod_rewrite (или Nginx)
- Права на запись в директорию (для JSON файлов)

## 🚀 Установка на spiritcontrol.ru

### Шаг 1: Загрузите файлы

Загрузите следующие файлы на сервер:
- `backend.php` - основной API файл
- `auth.php` - система аутентификации
- `config.php` - конфигурация (если нужно)
- `.htaccess` - правила Apache

### Шаг 2: Настройте права доступа

```bash
chmod 644 backend.php auth.php config.php .htaccess
chmod 755 cloud_proxy/
chmod 666 state.json settings.json calibration.json users.json  # После создания
```

### Шаг 3: Создайте директорию для данных (если нужно)

Файлы создадутся автоматически при первом запуске:
- `state.json` - состояние системы
- `settings.json` - настройки
- `calibration.json` - данные калибровки
- `users.json` - пользователи (создается автоматически)

### Шаг 4: Настройте домен

Убедитесь, что домен `spiritcontrol.ru` указывает на директорию с файлами.

### Шаг 5: Измените пароль по умолчанию!

**ВАЖНО!** По умолчанию создается пользователь:
- **Логин:** `admin`
- **Пароль:** `admin`

Измените пароль сразу после установки!

## 🔐 Аутентификация

### Стандартные учетные данные

- **Логин:** `admin`
- **Пароль:** `admin` (измените!)

### HTTP Basic Authentication

Все API запросы (кроме `/health`) требуют HTTP Basic Authentication:

```bash
curl -u admin:admin https://spiritcontrol.ru/api/status
```

Или в коде:

```javascript
fetch('https://spiritcontrol.ru/api/status', {
  headers: {
    'Authorization': 'Basic ' + btoa('admin:admin')
  }
})
```

## 📡 API Endpoints

### Статус

- `GET /api/status` - Полное состояние системы
- `GET /api/health` - Здоровье системы
- `GET /api/version` - Информация о версии

### Процессы

- `POST /api/process/start` - Запуск процесса
- `POST /api/process/stop` - Остановка процесса
- `POST /api/process/pause` - Пауза
- `POST /api/process/resume` - Возобновление

### Настройки

- `GET /api/settings/demo` - Получить демо-режим
- `POST /api/settings/demo` - Включить/выключить демо-режим

### Калибровка

- `GET /api/calibration` - Получить данные калибровки
- `POST /api/calibration/pump` - Калибровка насоса
- `POST /api/calibration/temp` - Калибровка термометров
- `POST /api/calibration/hydrometer` - Калибровка ареометра
- `GET /api/calibration/scan` - Сканирование датчиков

### Насос

- `GET /api/pump/status` - Статус насоса
- `POST /api/pump/start` - Запуск насоса
- `POST /api/pump/stop` - Остановка насоса

### Нагреватель

- `POST /api/manual/heater` - Установить мощность (0-100%)

### Система

- `POST /api/reboot` - Перезагрузка (заглушка для бэкенда)

## 🔧 Настройка Android приложения

В Android приложении настройте подключение:

```
URL: spiritcontrol.ru
Порт: 443 (для HTTPS) или 80 (для HTTP)
HTTPS: ✅ Включено (если используется SSL)
Логин: admin
Пароль: [ваш пароль]
```

**Важно:** Android приложение должно поддерживать HTTP Basic Auth для API запросов.

## 📝 Примеры использования

### Проверка работоспособности

```bash
curl https://spiritcontrol.ru/health
```

Ответ:
```json
{
  "status": "ok",
  "timestamp": 1702123456,
  "server": "Smart-Column S3 Backend",
  "version": "1.0.0"
}
```

### Получение статуса

```bash
curl -u admin:admin https://spiritcontrol.ru/api/status
```

### Запуск процесса

```bash
curl -u admin:admin -X POST \
  -H "Content-Type: application/json" \
  -d '{"mode": "rectification"}' \
  https://spiritcontrol.ru/api/process/start
```

### Установка мощности нагревателя

```bash
curl -u admin:admin -X POST \
  -H "Content-Type: application/json" \
  -d '{"power": 75}' \
  https://spiritcontrol.ru/api/manual/heater
```

## 🔒 Безопасность

### Рекомендации

1. **Измените пароль по умолчанию** сразу после установки
2. **Используйте HTTPS** для защиты передаваемых данных
3. **Ограничьте доступ** к директории через .htaccess если возможно
4. **Регулярно делайте бэкапы** файлов `*.json`
5. **Не публикуйте** файл `users.json` в публичный доступ

### Изменение пароля

Пароль хранится в зашифрованном виде в `users.json`. Для изменения пароля:

1. Удалите файл `users.json`
2. Перезагрузите страницу - создастся новый пользователь `admin:admin`
3. Или измените через код (будущее обновление)

## 📊 Структура файлов

```
cloud_proxy/
├── backend.php          # Основной API файл
├── auth.php             # Система аутентификации
├── config.php           # Конфигурация
├── .htaccess            # Правила Apache
├── state.json           # Состояние системы (создается автоматически)
├── settings.json        # Настройки (создается автоматически)
├── calibration.json     # Калибровка (создается автоматически)
└── users.json           # Пользователи (создается автоматически)
```

## 🐛 Решение проблем

### Ошибка 401 Unauthorized

**Проблема:** Неправильные логин/пароль

**Решение:** Проверьте учетные данные. По умолчанию `admin:admin`

### Ошибка 500 Internal Server Error

**Проблема:** Нет прав на запись файлов

**Решение:** 
```bash
chmod 666 state.json settings.json calibration.json users.json
chmod 755 cloud_proxy/
```

### Файлы не создаются

**Проблема:** Нет прав на создание файлов в директории

**Решение:**
```bash
chmod 777 cloud_proxy/  # Временно для теста
# Или
touch state.json settings.json calibration.json users.json
chmod 666 *.json
```

### mod_rewrite не работает

**Проблема:** Запросы не перенаправляются на backend.php

**Решение:** 
1. Проверьте, что mod_rewrite включен: `a2enmod rewrite`
2. Убедитесь, что `.htaccess` загружается (AllowOverride All)
3. Попробуйте использовать `backend.php?path=/api/status` напрямую

## 🔄 Обновление

Для обновления бэкенда:

1. Загрузите новые версии `backend.php` и `auth.php`
2. Сделайте бэкап файлов `*.json`
3. Проверьте работоспособность через `/health`

## 📞 Поддержка

При возникновении проблем проверьте:
1. Логи PHP (если доступны)
2. Логи Apache/Nginx
3. Права доступа к файлам
4. Версию PHP (должна быть 7.4+)

---

**Готово!** Теперь у вас есть полноценный бэкенд на spiritcontrol.ru! 🎉

