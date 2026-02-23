# Smart-Column S3 - Backend на spiritcontrol.ru

## 🎯 Что это?

Полноценный PHP бэкенд, который работает как ESP32, но на сервере spiritcontrol.ru. Все API endpoints идентичны ESP32 версии.

## ✅ Функции

- ✅ HTTP Basic Authentication (логин/пароль)
- ✅ Все API endpoints как на ESP32
- ✅ Хранение состояния в JSON файлах
- ✅ Совместимость с Android приложением
- ✅ Работает на стандартном PHP 7.4+

## 🚀 Быстрая установка

### 1. Загрузите файлы на сервер

```
backend.php    - основной API файл
auth.php       - система аутентификации  
.htaccess      - правила Apache
```

### 2. Установите права доступа

```bash
chmod 644 backend.php auth.php .htaccess
chmod 755 cloud_proxy/
```

### 3. Готово!

Файлы `state.json`, `settings.json`, `calibration.json`, `users.json` создадутся автоматически.

## 🔐 Вход по умолчанию

**ВАЖНО! Измените сразу после установки!**

- **Логин:** `admin`
- **Пароль:** `admin`

## 📡 Использование

### Проверка работоспособности (без авторизации)

```bash
curl https://spiritcontrol.ru/health
```

### Получение статуса (требуется авторизация)

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

## 📱 Настройка Android приложения

В настройках Android приложения:

```
URL: spiritcontrol.ru
Порт: 443 (HTTPS) или 80 (HTTP)
HTTPS: ✅ (если используете SSL)
Логин: admin
Пароль: [ваш пароль]
```

## 🔧 Доступные API endpoints

- `GET /api/status` - Полное состояние системы
- `GET /api/health` - Здоровье системы  
- `POST /api/process/start` - Запуск процесса
- `POST /api/process/stop` - Остановка процесса
- `POST /api/process/pause` - Пауза
- `POST /api/process/resume` - Возобновление
- `GET /api/calibration` - Получить калибровку
- `POST /api/calibration/pump` - Калибровка насоса
- `POST /api/pump/start` - Запуск насоса
- `POST /api/pump/stop` - Остановка насоса
- `GET /api/pump/status` - Статус насоса
- `POST /api/manual/heater` - Установить мощность нагревателя
- И другие...

Полный список в `BACKEND_SETUP.md`

## 🔒 Безопасность

1. **Обязательно измените пароль по умолчанию!**
2. Используйте HTTPS
3. Делайте бэкапы файлов `*.json`
4. Не публикуйте `users.json` в открытый доступ

## 📂 Структура файлов

```
cloud_proxy/
├── backend.php          # Основной API
├── auth.php             # Аутентификация
├── .htaccess            # Правила Apache
├── state.json           # Состояние (создается автоматически)
├── settings.json        # Настройки (создается автоматически)
├── calibration.json     # Калибровка (создается автоматически)
└── users.json           # Пользователи (создается автоматически)
```

## ❓ Решение проблем

### Ошибка 401 Unauthorized
- Проверьте логин/пароль (по умолчанию `admin:admin`)

### Ошибка 500 Internal Server Error
- Проверьте права доступа: `chmod 666 *.json`

### Файлы не создаются
- Убедитесь, что есть права на запись: `chmod 777 cloud_proxy/`

---

**Готово к использованию!** 🎉

Подробная документация: `BACKEND_SETUP.md`

