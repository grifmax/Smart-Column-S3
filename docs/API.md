# Smart-Column S3 — API документация

**Версия прошивки:** `2.2.1`
**Последнее обновление:** 2026-04-17

---

## Базовая информация

- **Базовый URL:** `http://<device-ip>`
- **REST префикс:** `/api/*`
- **WebSocket:** `ws://<device-ip>/ws`
- **Формат данных:** `application/json`
- **Аутентификация:** HTTP Basic Auth (если включена в настройках)

---

## Режимы процессов

### `mode` (`/api/process/start`, `/api/status`, WebSocket)

| Значение | Режим |
|----------|-------|
| `idle` | Простой |
| `rectification` | Авто-ректификация |
| `distillation` | Дистилляция |
| `manual` | Ручная ректификация (также `manual_rect`) |
| `mashing` | Затирание солода |
| `hold` | Пастеризация / температурные ступени |
| `nbk` | НБК (непрерывная бражная колонна) |
| `fermentation` | Ферментация |

### `phaseStr` — фазы авто-ректификации

`idle` → `heating` → `stabilization` → `heads` → `post_heads` → `body` → `tails` → `purge` → `finish` → `completed`

### `phaseStr` — фазы НБК

`idle` → `heating` → `stabilization` → `working` → `finish` → `completed`

### `phaseStr` — фазы затирания

`idle` → `acid_rest` → `protein_rest` → `beta_amylase` → `alpha_amylase` → `mash_out` → `finish`

### `phaseStr` — фазы ферментации

`idle` → `running` → `completed`

---

## Состояние и диагностика

### `GET /api/status`

Полное состояние системы. Обновляется по REST и WebSocket.

**Ключевые поля:**

```json
{
  "mode": "rectification",
  "modeStr": "Ректификация",
  "phase": 3,
  "phaseStr": "heads",
  "paused": false,
  "safetyOk": true,
  "uptime": 3600,
  "temps": {
    "cube": 78.5,
    "columnBottom": 77.2,
    "columnTop": 76.1,
    "reflux": 74.0,
    "tsa": 25.0,
    "waterIn": 18.0,
    "waterOut": 32.0
  },
  "pressure": {
    "cube": 12.3,
    "atmosphere": 760.0,
    "ok": true
  },
  "power": {
    "voltage": 225.0,
    "current": 8.5,
    "power": 1900.0,
    "energy": 1.25,
    "frequency": 50.0,
    "powerFactor": 0.99
  },
  "pump": {
    "running": true,
    "speedMlPerHour": 900.0,
    "totalVolumeMl": 280.0
  },
  "stirrer": {
    "running": false,
    "speed": 0,
    "available": true,
    "autoMode": false
  },
  "hydrometer": {
    "abv": 73.5,
    "density": 0.851,
    "valid": true,
    "ok": true
  },
  "alarm": {
    "active": false,
    "latched": false,
    "type": "none",
    "level": "none",
    "message": "",
    "resetAvailable": true
  },
  "v2": {
    "available": true,
    "lifecycle": "running",
    "phaseToken": "heads",
    "paused": false,
    "lastReasonCode": "RC_HEADS_VOLUME_REACHED",
    "operatorMessage": "Heads fraction collected"
  },
  "mashing": {
    "active": false,
    "phase": 0,
    "phaseStr": "idle",
    "stepCount": 0,
    "currentStep": 0,
    "targetTemp": 0.0,
    "stepDurationSec": 0,
    "tempInRange": false,
    "stepName": "",
    "elapsedSec": 0,
    "remainingSec": 0
  },
  "hold": {
    "active": false,
    "stepCount": 0,
    "currentStep": 0,
    "targetTemp": 0.0,
    "tempInRange": false,
    "stepDurationSec": 0,
    "elapsedSec": 0,
    "remainingSec": 0
  }
}
```

---

### `GET /api/health`

Сводное состояние датчиков и подсистем.

```json
{
  "overall": 95,
  "tempSensorsOk": 7,
  "tempSensorsTotal": 7,
  "bmp280": true,
  "ads1115": true,
  "pzem": true,
  "wifiRSSI": -55,
  "cpuTemp": 42.0
}
```

---

### `GET /api/version`

Версия прошивки + метаданные.

```json
{
  "firmware": "2.2.1",
  "board": "esp32-s3-devkitc-1-n16r8",
  "buildDate": "Apr 15 2026",
  "deviceId": "abc123"
}
```

---

### `POST /api/reboot`

Перезагрузка контроллера.

---

### `GET /api/reboot/status`

История перезагрузок: причина, тип (WDT / Brownout / panic / SW reset).

---

## Управление процессом

### `POST /api/process/start`

Запуск режима.

**Минимальный запрос:**
```json
{ "mode": "rectification" }
```

**Параметры для разных режимов:**

```json
// Дистилляция
{
  "mode": "distillation",
  "params": {
    "speed": 1500,
    "headsVolume": 100,
    "targetVolume": 3000,
    "endTemp": 98.0
  }
}

// Затирание
{
  "mode": "mashing",
  "params": {
    "profile": {
      "name": "Классический",
      "steps": [
        {"temperature": 52, "duration": 15, "name": "Белковая пауза"},
        {"temperature": 63, "duration": 60, "name": "Бета-амилаза"},
        {"temperature": 72, "duration": 30, "name": "Альфа-амилаза"},
        {"temperature": 78, "duration": 5,  "name": "Мэш-аут"}
      ]
    }
  }
}

// Пастеризация / Hold
{
  "mode": "hold",
  "params": {
    "steps": [
      {"temperature": 63, "duration": 30},
      {"temperature": 72, "duration": 15}
    ]
  }
}

// НБК
{
  "mode": "nbk",
  "params": {
    "targetVolumeMl": 5000,
    "powerPercent": 80
  }
}

// Ферментация
{
  "mode": "fermentation",
  "params": {
    "durationHours": 72,
    "targetTemp": 20.0
  }
}
```

---

### `POST /api/process/stop`

Остановить текущий процесс.

### `POST /api/process/pause`

Поставить на паузу.

### `POST /api/process/resume`

Возобновить после паузы.

---

## Авто-ректификация

### `GET /api/settings/rect`

Получить параметры авто-ректификации.

### `POST /api/settings/rect`

Сохранить параметры. Поддерживаемые поля:

| Поле | Тип | Диапазон | Описание |
|------|-----|----------|----------|
| `feedstock` | int | 0..7 | Вид сырья |
| `feedVolumeL` | float | 1..250 | Объём браги (л) |
| `feedAbvPercent` | float | 1..96 | Крепость браги (%) |
| `headsPercent` | float | 0..40 | Доля голов (%) |
| `bodyPercent` | float | 0..100 | Доля тела (%) |
| `tailsPercent` | float | 0..100 | Доля хвостов (%) |
| `headsSpeedMlHKw` | float | 10..2000 | Скорость голов (мл/ч/кВт) |
| `bodySpeedMlHKw` | float | 50..3000 | Скорость тела (мл/ч/кВт) |
| `stabilizationMin` | int | 1..180 | Время стабилизации (мин) |
| `purgeMin` | int | 1..120 | Время продувки (мин) |
| `applyFeedstockDefaults` | bool | — | Применить дефолты по сырью |

### Сырьё и дефолтные фракции

| feedstock | Сырьё | Головы % | Тело % | Хвосты % |
|-----------|----|---:|---:|---:|
| 0 | Сахар | 6 | 84 | 10 |
| 1 | Мука/зерно | 8 | 80 | 12 |
| 2 | Солод | 7 | 81 | 12 |
| 3 | Фрукты | 5 | 75 | 20 |
| 4 | Меласса | 8 | 74 | 18 |
| 5 | Виноград/вино | 6 | 78 | 16 |
| 6 | Мёд | 7 | 79 | 14 |
| 7 | Другое | — | — | — |

---

## Ручное управление (runtime)

### `POST /api/manual/heater`

```json
{ "power": 55 }
```

### `POST /api/rect/heater`

Override мощности ТЭНа в авто-ректификации.  
`power = -1` — снять override, вернуть управление Watt-Control.

### `POST /api/manual/pump`

```json
{ "speed": 900 }
```

`speed <= 0` — стоп. `speed > 0` — мл/ч.

### `POST /api/manual/valves`

```json
{ "water": true, "heads": false, "uno": true }
```

### `POST /api/manual/volumes`

Ручная корректировка объёмов фракций.

```json
{
  "heads": 120,
  "body": 2500,
  "tails": 150,
  "syncTotal": true
}
```

---

## Мешалка куба (0-10В, MCP4725)

### `POST /api/stirrer/start`

Запустить мешалку.

```json
{ "speed": 70 }
```

`speed` — 0..100%. Если не указан или 0, используется `defaultSpeedPercent` из настроек.

Ручное управление мешалкой разрешено только в `IDLE`. Если любой процесс активен или поставлен на паузу, backend вернёт `409`.

### `POST /api/stirrer/stop`

Остановить мешалку.

### `POST /api/stirrer/set`

Установить скорость у уже запущенной мешалки без повторного `start`.

```json
{ "speed": 50 }
```

`speed` — 1..100%. Для полной остановки используйте `POST /api/stirrer/stop`.

### Поля в WebSocket / `/api/status`

```json
"stirrer": {
  "running": true,
  "speed": 70,
  "available": true,
  "autoMode": false
}
```

| Поле | Описание |
|------|----------|
| `running` | Мешалка работает |
| `speed` | Текущая скорость 0–100% |
| `available` | MCP4725 обнаружен (I2C OK) |
| `autoMode` | Запущена автоматически из FSM |

---

## Настройки мешалки

### `GET /api/settings/stirrer`

```json
{
  "enabled": true,
  "defaultSpeedPercent": 60,
  "autoMashing": true,
  "autoFermentation": false,
  "autoNbk": false
}
```

### `POST /api/settings/stirrer`

Сохранить настройки мешалки в NVS.

---

## Калибровка и оборудование

- `GET /api/calibration`
- `POST /api/calibration/pump`
- `POST /api/calibration/temp`
- `POST /api/calibration/hydrometer`
- `GET /api/calibration/scan`
- `POST /api/pump/calibrate/start`
- `POST /api/pump/calibrate/stop`
- `POST /api/pump/calibrate/cancel`
- `POST /api/pump/calibrate/finish`
- `POST /api/pump/start`
- `POST /api/pump/stop`
- `GET /api/pump/status`
- `GET /api/pump/diag` — расширенная диагностика FreeRTOS pump-task
- `GET /api/energy`

---

## Тестирование оборудования

### `GET /api/testing/status`

Полный статус тестирования: блокировки, активные тесты, насос, мешалка, ТЭН, клапаны, серво, сенсоры, питание.

### `POST /api/testing/stop-all`

Принудительно остановить все активные тесты оборудования.

### `POST /api/testing/pump`

```json
{ "action": "start", "speed": 800 }
```

### `POST /api/testing/stirrer`

```json
{ "action": "start", "speedPercent": 60 }
```

```json
{ "action": "set", "speedPercent": 45 }
```

```json
{ "action": "stop" }
```

### `POST /api/testing/heater`

```json
{ "action": "start", "power": 10 }
```

### `POST /api/testing/valves`

```json
{ "valve": "water", "action": "open" }
// или
{ "valve": "heads", "action": "pulse", "durationMs": 2000 }
```

### `POST /api/testing/servo`

```json
{ "fraction": "body" }
```

---

## Безопасность

### `POST /api/safety/ack`

Подтвердить (acknowledge) активную аварию.

**Ответ:**
```json
{
  "success": true,
  "v2": {
    "safetyLatched": true,
    "severity": "latched_trip",
    "resetAvailable": false,
    "resetBlockedReason": "Температура всё ещё высокая"
  }
}
```

### `POST /api/safety/reset`

Сбросить аварию (если условия позволяют).

---

## История процессов

### `GET /api/history`

Список всех процессов. Каждый элемент содержит:
- `id`, `mode`, `startTime`, `endTime`, `duration`
- `completionState` — `completed`, `stopped`, `safety_stop`
- `completionReasonCode` — RC-код завершения
- `safetySummary` — краткая safety-сводка
- `safetyState` — `ok`, `trip`, `ack`, `recovery`

### `GET /api/history/{id}`

Полные детали процесса: timeseries, фазы, предупреждения, ошибки, reason codes.

### `DELETE /api/history/{id}`

Удалить запись процесса.

### `GET /api/history/{id}/export`

Экспорт в JSON.

---

## WiFi и облако

- `GET /api/wifi/scan`
- `GET /api/wifi/status`
- `POST /api/wifi/connect`
- `GET /api/wifi/profiles`
- `POST /api/wifi/profiles`
- `DELETE /api/wifi/profiles/{index}`
- `POST /api/cloud/claim`
- `POST /api/cloud/config`

---

## Профили

- `GET /api/profiles`
- `GET /api/profiles/{id}`
- `POST /api/profiles/{id}/load`
- `DELETE /api/profiles/{id}`

---

## Логи

- `GET /api/logs/events` — последние события в JSON/CSV
- `GET /api/logs/list` — список лог-файлов
- `GET /api/logs/{filename}` — скачать лог-файл

---

## WebSocket API

Подключение: `ws://<device-ip>/ws`

### Fast packet (каждые 2 сек)

Основные поля:
- `mode`, `modeStr`, `phase`, `phaseStr`, `paused`, `uptime`
- `temps.*` — все температуры
- `pressure.*` — давление куба и атмосферное
- `power.*` — напряжение, ток, мощность, энергия
- `pump.*` — состояние насоса
- `stirrer.*` — состояние мешалки ← **NEW**
- `alarm.*` — текущая авария
- `phase_elapsed_sec`, `phase_target_sec`, `phase_percent`

### Full packet (каждые 10 сек)

Дополнительно:
- `progress` — детальный прогресс фазы
- `rectification` — параметры ректификации
- `distillation` — параметры дистилляции
- `mashing` — статус затирания
- `hold` — статус пастеризации
- `health` — состояние подсистем
- `memory` — heap/flash статистика
- `v2` — lifecycle, reason codes, process indicators

---

## HTTP коды ошибок

| Код | Причина |
|-----|---------|
| 400 | Некорректный JSON / параметры |
| 401 | Требуется авторизация |
| 404 | Endpoint не найден |
| 429 | Превышен лимит запросов |
| 500 | Внутренняя ошибка / ошибка сохранения |
| 503 | Внешняя зависимость недоступна |

---

## Quick cURL Examples

```bash
# Статус системы
curl -u admin:admin http://192.168.4.1/api/status

# Запуск ректификации
curl -u admin:admin -X POST http://192.168.4.1/api/process/start \
  -H "Content-Type: application/json" \
  -d '{"mode":"rectification"}'

# Старт мешалки на 70%
curl -u admin:admin -X POST http://192.168.4.1/api/stirrer/start \
  -H "Content-Type: application/json" \
  -d '{"speed":70}'

# Стоп мешалки
curl -u admin:admin -X POST http://192.168.4.1/api/stirrer/stop

# Параметры ректификации по сахарному сырью
curl -u admin:admin -X POST http://192.168.4.1/api/settings/rect \
  -H "Content-Type: application/json" \
  -d '{"feedstock":0,"feedVolumeL":25,"feedAbvPercent":35,"applyFeedstockDefaults":true}'

# Затирание (кастомный профиль)
curl -u admin:admin -X POST http://192.168.4.1/api/process/start \
  -H "Content-Type: application/json" \
  -d '{"mode":"mashing","params":{"profile":{"name":"Тест","steps":[{"temperature":63,"duration":60,"name":"Бета"}]}}}'

# История процессов
curl -u admin:admin http://192.168.4.1/api/history
```
