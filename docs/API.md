# Smart-Column S3 — Документация API

**Версия прошивки:** `1.5.16`  
**Последнее обновление:** 2026-02-23

## Базовая информация

- Базовый URL: `http://<device-ip>`
- REST префикс: `/api/*`
- WebSocket: `ws://<device-ip>/ws`
- Формат данных: `application/json`
- Аутентификация: HTTP Basic Auth (если включена в настройках)

## Справочник режимов

### `mode` (`/api/process/start`, `/api/status`, WebSocket)

- `idle`
- `rectification`
- `distillation`
- `manual` (допустим также `manual_rect` при старте)
- `mashing`
- `hold`

### `phaseStr` (ректификация)

- `idle`, `heating`, `stabilization`, `heads`, `post_heads`, `body`, `tails`, `purge`, `finish`, `completed`

## REST API

### Ключевые endpoints

## Состояние и диагностика

### `GET /api/status`
Полное состояние системы, включая режим, температуры, давление, мощность, прогресс, параметры режимов, метрики дисплея, cloud, mashing/hold.

Ключевые поля:

- `mode`, `modeStr`, `phase`, `phaseStr`, `paused`, `safetyOk`, `uptime`
- `temps.{cube,columnBottom,columnTop,reflux,deflegmator,product,tsa,waterIn,waterOut}`
- `pressure.{cube,atm,kpa}`
- `power.{voltage,current,power,energy,frequency,pf}`
- `pump.{speedMlH,totalMl,running}`
- `hydrometer.{abv,density,valid}`
- `volumes.{heads,body,tails}`
- `rectification.{feedVolumeL,feedAbvPercent,headsPercent,bodyPercent,tailsPercent,headsSpeedMlHKw,bodySpeedMlHKw,headsTargetMl,bodyTargetMl,tailsTargetMl}`
- `distillation.{speedMlH,headsVolumeMl,targetVolumeMl,endTempC,powerPercent}`
- `progress.{phaseElapsedSec,phaseTargetSec,phaseRemainingSec,phasePercent}`
- `display.{frames,slowFrames,recoveries,hardRecoveries,hardFailures,lastFrameMs,maxFrameMs,lastGapMs,maxGapMs,gapOverruns}`
- `cloud.{enabled,tunnelUrl,connected,authenticated,claimActive,claimCode?,claimExpiresAt?}`
- `mashing.{active,phase,phaseStr,stepCount,currentStep,targetTemp,stepDurationSec,tempInRange,stepName,elapsedSec,remainingSec}`
- `hold.{active,stepCount,currentStep,targetTemp,tempInRange,stepDurationSec,elapsedSec,remainingSec}`

### `GET /api/health`
Сводное состояние датчиков и системы.

### `GET /api/version`
Версия прошивки + метаданные сборки + board/deviceId + frontend version (из `/version.json`, если есть).

### `POST /api/reboot`
Перезагрузка контроллера.

## Управление процессом

### `POST /api/process/start`
Запуск режима.

Минимальный запрос:

```json
{
  "mode": "rectification"
}
```

Примеры `params`:

- `distillation`: `speed`, `headsVolume`, `targetVolume`, `endTemp`
- `mashing`: `profile: { name, steps: [{temperature,duration,name}] }`
- `hold`: `steps: [{temperature,duration}]`

### `POST /api/process/stop`
Остановить текущий процесс.

### `POST /api/process/pause`
Поставить на паузу.

### `POST /api/process/resume`
Возобновить после паузы.

## Авто-ректификация

### `GET /api/settings/rect`
Получить параметры старта авто-ректификации.

### `POST /api/settings/rect`
Сохранить параметры старта авто-ректификации.

Поддерживаемые поля:

- `feedstock` (`0..7`)
- `feedVolumeL` (`1..250`)
- `feedAbvPercent` (`1..96`)
- `headsPercent` (`0..40`)
- `bodyPercent` (`0..100`)
- `tailsPercent` (`0..100`)
- `headsSpeedMlHKw` (`10..2000`)
- `bodySpeedMlHKw` (`50..3000`)
- `stabilizationMin` (`1..180`)
- `purgeMin` (`1..120`)
- `applyFeedstockDefaults` (`true/false`)

Можно передавать как плоский JSON, так и через вложенный объект `params`.

### Feedstock (`feedstock`) и дефолтные фракции

| feedstock | Сырьё | Головы % | Тело % | Хвосты % |
|---|---|---:|---:|---:|
| 0 | Сахар | 6 | 84 | 10 |
| 1 | Мука/зерно | 8 | 80 | 12 |
| 2 | Солод | 7 | 81 | 12 |
| 3 | Фрукты | 5 | 75 | 20 |
| 4 | Меласса | 8 | 74 | 18 |
| 5 | Виноград/вино | 6 | 78 | 16 |
| 6 | Мёд | 7 | 79 | 14 |
| 7 | Другое | значения по умолчанию проекта |

## Ручное управление (runtime)

### `POST /api/manual/heater`
Установить мощность ТЭНа в ручном режиме, `%` (`0..100`).

```json
{ "power": 55 }
```

### `POST /api/rect/heater`
Override мощности ТЭНа в авто-ректификации.

- `power = -1` — снять override и вернуть управление `WattControl`
- `power = 0..100` — принудительный %

### `POST /api/manual/pump`
Управление насосом.

- `speed <= 0` — стоп
- `speed > 0` — запуск с указанной скоростью мл/ч

```json
{ "speed": 900 }
```

### `POST /api/manual/valves`
Управление клапанами: `water`, `heads`, `uno`, либо `allOff: true`.

```json
{ "water": true, "heads": false, "uno": true }
```

### `POST /api/manual/volumes`
Ручная корректировка отображаемых объёмов фракций.

```json
{
  "heads": 120,
  "body": 2500,
  "tails": 150,
  "syncTotal": true
}
```

- Обязателен минимум один из: `heads`, `body`, `tails`
- При `syncTotal=true` суммарный `pump.totalVolumeMl` пересчитывается автоматически

## Калибровка и оборудование

- `GET /api/calibration`
- `POST /api/calibration/pump`
- `POST /api/calibration/temp`
- `POST /api/calibration/hydrometer`
- `GET /api/calibration/scan`
- `POST /api/pump/calibrate/start`
- `POST /api/pump/calibrate/stop`
- `POST /api/pump/calibrate/finish`
- `POST /api/pump/start`
- `POST /api/pump/stop`
- `GET /api/pump/status`
- `GET /api/energy`

## Wi‑Fi и облако

- `GET /api/wifi/scan`
- `GET /api/wifi/status`
- `POST /api/wifi/connect`
- `POST /api/cloud/claim`
- `POST /api/cloud/config`

## Профили

- `GET /api/profiles`
- `GET /api/profiles/{id}`
- `POST /api/profiles/{id}/load`
- `DELETE /api/profiles/{id}`

## WebSocket API

Подключение:

```text
ws://<device-ip>/ws
```

### Fast packet (частый)
Отправляется каждые `INTERVAL_WEB_BROADCAST` (по умолчанию 2000 мс).

Основные поля:

- `mode`, `modeStr`, `phase`, `phaseStr`, `paused`, `uptime`
- `t_cube`, `t_column_bottom`, `t_column_top`, `t_reflux`, `t_tsa`, `t_water_in`, `t_water_out`
- `p_cube`, `p_atm`
- `voltage`, `current`, `power`, `energy`, `frequency`, `pf`
- `pump_speed`, `pump_volume`, `speed`, `volume`
- `volume_heads`, `volume_body`, `volume_tails`
- `abv`, `abv_valid`
- `phase_elapsed_sec`, `phase_target_sec`, `phase_remaining_sec`, `phase_percent`
- `display_last_ms`, `display_slow`, `display_hard`, `display_gap_ms`

### Full packet (редкий)
Отправляется каждые `INTERVAL_WEB_BROADCAST_FULL` (по умолчанию 10000 мс), содержит расширенные объекты:

- `progress`
- `rectification`
- `distillation`
- `display`

## API Errors

Типовые ответы:

- `400` — некорректный JSON/параметры
- `401` — требуется авторизация
- `404` — endpoint/ресурс не найден
- `500` — ошибка сохранения/внутренняя ошибка
- `503` — внешняя зависимость недоступна

## Quick cURL Examples

```bash
# Статус
curl -u admin:admin http://192.168.4.1/api/status

# Старт ректификации
curl -u admin:admin -X POST http://192.168.4.1/api/process/start \
  -H "Content-Type: application/json" \
  -d '{"mode":"rectification"}'

# Обновить параметры авто-ректификации
curl -u admin:admin -X POST http://192.168.4.1/api/settings/rect \
  -H "Content-Type: application/json" \
  -d '{"feedstock":0,"feedVolumeL":25,"feedAbvPercent":35,"applyFeedstockDefaults":true}'

# Ручная коррекция объёмов
curl -u admin:admin -X POST http://192.168.4.1/api/manual/volumes \
  -H "Content-Type: application/json" \
  -d '{"heads":80,"body":1800,"tails":100,"syncTotal":true}'
```
