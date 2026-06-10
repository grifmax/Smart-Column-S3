# Smart-Column S3 — API

**Версия прошивки:** `2.2.51`  
**Актуальность документа:** 2026-06-10

---

## Общая схема

- Base URL: `http://<device-ip>`
- REST API: `/api/*`
- WebSocket: `ws://<device-ip>/ws`
- Формат обмена: `application/json`
- Защита: HTTP Basic Auth, если включена в настройках безопасности

## Ключевые endpoint-ы

### Состояние системы

#### `GET /api/status`

Возвращает полное текущее состояние:

- режим и фазу
- температуры
- давление
- мощность
- насос
- мешалку
- safety / alarm
- `v2.guidance`
- `v2.indicators`

Это главный endpoint для web UI и fallback для внешних клиентов.

#### `GET /api/health`

Сводное здоровье подсистем:

- датчики
- BMP280 / ADS1115 / PZEM
- Wi‑Fi
- heap / uptime / CPU temp

#### `GET /api/version`

Версия прошивки и сведения о сборке.

#### `GET /api/reboot/status`

Информация о последней перезагрузке контроллера.

#### `POST /api/reboot`

Перезагрузка контроллера.

---

## Управление процессами

#### `POST /api/process/preflight`

Проверка готовности перед запуском режима.

Используется модалкой `Pre-flight мастер запуска`.

Пример:

```json
{
  "mode": "rectification",
  "params": {
    "feedVolumeL": 25,
    "feedAbvPercent": 40,
    "headsPercent": 8,
    "bodyPercent": 84,
    "tailsPercent": 8
  }
}
```

Ответ содержит:

- `ready`
- `blockingCount`
- `warningCount`
- `checks`
- `items`
- `advisor`

#### `POST /api/process/start`

Запуск процесса.

#### `POST /api/process/stop`

Остановка процесса.

#### `POST /api/process/pause`

Пауза текущего режима, если режим её поддерживает.

#### `POST /api/process/resume`

Возобновление после паузы.

---

## История процессов

#### `GET /api/history`

Список сохранённых прогонов.

Используется таблицей истории и подбором baseline для `Run Advisor`.

#### `GET /api/history/{id}`

Полные детали конкретного прогона:

- метаданные
- параметры старта
- метрики
- фазы
- события safety
- warnings / errors
- `advisorSnapshot`

#### `POST /api/history/{id}/advisor`

Сохранение snapshot рекомендаций `Run Advisor` для конкретного прогона.

#### `GET /api/history/{id}/export?format=csv`

Экспорт одного прогона.

#### `DELETE /api/history/{id}`

Удаление одного прогона.

#### `DELETE /api/history`

Полная очистка истории.

---

## Профили

#### `GET /api/profiles`

Список профилей.

#### `GET /api/profiles/{id}`

Полные данные профиля, включая learning summary, validation context последнего успешного baseline и последние advisor snapshot-ы.

#### `POST /api/profiles`

Создание нового профиля.

#### `POST /api/profiles/{id}/load`

Загрузка профиля в текущие настройки.

#### `DELETE /api/profiles/{id}`

Удаление профиля.

#### `GET /api/profiles/export`

Экспорт всех профилей.

#### `POST /api/profiles/import`

Импорт профилей.

---

## Настройки

Проект использует несколько групп настроек. Основные:

- `GET/POST /api/settings/equipment`
- `GET/POST /api/settings/safety`
- `GET/POST /api/settings/security`
- `GET/POST /api/settings/rect`
- `GET/POST /api/settings/nbk`
- `GET/POST /api/settings/fermentation`
- `GET/POST /api/settings/stirrer`
- `GET/POST /api/settings/mqtt`
- `GET/POST /api/settings/demo`

Точный набор полей лучше смотреть по текущему payload, который возвращает контроллер.

---

## Исполнительные механизмы и сервис

### Мешалка

- `POST /api/stirrer/start`
- `POST /api/stirrer/stop`
- `POST /api/stirrer/set`

### Насос

- `POST /api/pump/calibrate/start`
- `POST /api/pump/calibrate/stop`
- `POST /api/pump/calibrate/cancel`
- `POST /api/pump/stop`
- `GET /api/pump/status`
- `GET /api/pump/diag`

### Калибровка

- `GET /api/calibration`
- `POST /api/calibration`
- `GET /api/calibration/scan`

### Safety

- `POST /api/safety/ack`
- `POST /api/safety/reset`

### Логи

- `GET /api/logs/events`
- `POST /api/logs/events/clear`

---

## WebSocket `/ws`

WebSocket нужен для живого UI и потоковых обновлений без постоянного опроса REST.

Через него фронтенд получает:

- текущее состояние
- indicators / guidance
- статусы оборудования
- быстрые обновления для схемы и показометров

Если вы пишете внешний клиент, начинайте с REST `/api/status`, а WebSocket подключайте как слой live-обновления.

---

## Что важно для интеграций

### 1. Не полагаться только на один флаг

Для инженерных сценариев полезно смотреть комбинацию:

- `mode`
- `phaseStr`
- `alarm`
- `v2.guidance`
- `v2.indicators`

### 2. `Pre-flight` и `Run Advisor` — разные уровни

- `Pre-flight` отвечает на вопрос: “можно ли стартовать сейчас?”
- `Run Advisor` отвечает на вопрос: “что показал уже завершённый прогон и что улучшать дальше?”

### 3. История — это основа аналитики

Если интеграции хотят строить сравнение запусков, нужно опираться на:

- `/api/history`
- `/api/history/{id}`
- `profileId`
- `advisorSnapshot`

---

## Смежные документы

- [PROJECT_GUIDE.md](PROJECT_GUIDE.md)
- [HISTORY_SCHEMA.md](HISTORY_SCHEMA.md)
- [HOME_ASSISTANT.md](HOME_ASSISTANT.md)
- [../SPEC.md](../SPEC.md)
