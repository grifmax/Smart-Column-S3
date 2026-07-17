# Smart-Column S3 — API

**Версия прошивки:** 2.3.8  
**Актуальность документа:** 2026-06-14

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
- `v2.reasonInsight`
- `v2.indicators`
- `pressure.cube` / `pressure.atm` и `v2.indicators.pressureRateMmHgPerMin`, `v2.indicators.distPressureMargin` — live-данные для операторского мониторинга давления
- `v2.activeLimits.antiOscillationActive` / `v2.activeLimits.antiOscillationHoldSec`
- `v2.indicators.telemetryCoverage`, `decisionTrust`, `degradedModeActive`, `adaptiveControlAllowed`
- `activeProfile` — активный профиль процесса, его validation context и preview адаптации рецепта под текущее атмосферное давление
- `power.backend`, `power.boosterEnabled`, `power.zeroCrossSeen`, `power.zeroCrossCount`, `power.triacFireCount`, `power.triacDelayUs` — live-диагностика схемы нагрева `TRIAC main + SSR booster`
- `power.safetyRelayAvailable` / `power.safetyRelayEnergized` — состояние опционального независимого cutoff-реле нагрева; по умолчанию канал отключён до назначения отдельного GPIO в pin-профиле

Это главный endpoint для web UI и fallback для внешних клиентов.

Поле `activeProfile` в `/api/status` содержит:

- `id`, `loaded`, `name`, `category`
- `mashing.steps[]` — для активного профиля `mashing` backend возвращает шаги рецепта, чтобы Web UI мог восстановить панель `Затирка` после reload
- `validation.validatedAt`, `validation.sourceProcessId`, `validation.atmosphereMmHg`
- `baseTemperatures.*`
- `baroPreview.*`
- `effectiveTemperaturesPreview.*`

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

Возвращает `lastReason`, `lastReasonStr`, `lastReasonKind`, накопительные счётчики `totalReboots`, `wdtReboots`, `crashReboots`, `userReboots`, `otherReboots`, а также `uptimeSec`, `healthOverall` и `freeHeap`.

#### `GET /api/settings/equipment`

Помимо параметров оборудования и списка модулей теперь возвращает `bootGpio`:
- `completed`, `overallOk`, `checkedCount`, `timestampMs`, `boardRev`
- `items[]` с полями `label`, `pin`, `mode`, `expectedLevel`, `actualLevel`, `ok`

Также возвращает `boardProfile`:
- `rev`, `name`, `code`
- capability-флаги `hasTft`, `hasTouch`, `hasTriac`, `hasZeroCross`, `hasFractionatorServo`, `hasStartStopPwm`

Также возвращает topology-aware блоки для температурной комплектации:
- `temperatureTopology.cube`, `columnBottom`, `columnTop`, `reflux`, `tsa`, `waterIn`, `waterOut`
- `temperatureTopology.installedCount`
- `supportedModes.rectification|manualRect|distillation|nbk|mashing|hold|fermentation`
- для каждого `supportedModes.*`: `supported`, `reason`

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
    "tailsPercent": 8,
    "baroCorrectionEnabled": true,
    "boosterEnabled": true,
    "boosterStopCubeTempC": 78.0
  }
}
```

Для `rectification`, `distillation` и `nbk` в `params` можно дополнительно передавать:

- `boosterEnabled` — участвует ли `SSR booster` в разогреве именно этого запуска
- `boosterStopCubeTempC` — температура куба, при которой booster должен отключиться

Эти поля участвуют и в `preflight`, и в фактическом `start`, но не требуют отдельного сохранения в постоянные настройки оборудования.

Ответ содержит:

- `ready`
- `blockingCount`
- `warningCount`
- `checks`
- `items`
- `advisor`

Для ректификации `advisor.baroCorrection` может дополнительно содержать:

- `enabled` — включена ли мягкая барокоррекция для текущего запуска
- `applicable` — хватает ли baseline профиля и текущего давления для расчёта
- `applied` — будет ли реально применён сдвиг порогов
- `baselinePressureMmHg`, `currentPressureMmHg`, `pressureDeltaMmHg`
- `appliedShiftC`
- `effectiveTemperatures.headsEnd`, `effectiveTemperatures.bodyStart`, `effectiveTemperatures.bodyEnd`

Для ректификации `advisor.dryRun` дополнительно содержит прогноз запуска:

- `supported` — backend смог собрать прогноз для текущего сценария
- `usesLearning` — прогноз опирается не только на модель уставок, но и на learning snapshot профиля
- `profileAligned` — активный профиль подходит выбранному режиму и может выступать baseline
- `summary` — короткая текстовая сводка по длительности и энергии
- `totalMin`, `heatingMin`, `preparationMin`, `takeoffMin`
- `energyKwh`
- `charge.feedVolumeL`, `charge.feedAbvPercent`, `charge.absoluteAlcoholMl`
- `volumes.headsMl`, `volumes.bodyMl`, `volumes.tailsMl`
- `speeds.headsMlH`, `speeds.bodyMlH`, `speeds.tailsMlH`
- `baselineDurationMin`, `baselineEnergyKwh`, `baselineProcessId`
- `riskTone`, `riskTitle`, `riskDetail`

#### `POST /api/process/start`

Запуск процесса.

Для `rectification`, `distillation` и `nbk` можно использовать те же `params.boosterEnabled` и `params.boosterStopCubeTempC`, что и в `preflight`: backend применит их как runtime-override для текущего запуска без записи в NVS.

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

`GET /api/history` also publishes a compact `indicatorsSummary` block for each run:

- `available`, `samples`
- `avgProcessHealth`, `minProcessHealth`
- `avgStabilityIndex`
- `minCoolingMarginC`
- `maxFloodRisk`
- `takeoffShare`
- `freshnessShare`

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

#### `POST /api/history/demo`

Р—Р°РіСЂСѓР¶Р°РµС‚ РІСЃС‚СЂРѕРµРЅРЅС‹Р№ `public demo dataset` РІ РёСЃС‚РѕСЂРёСЋ.

- РўРµР»Рѕ Р·Р°РїСЂРѕСЃР°: `{ "replace": true|false }`
- РџСЂРё `replace=true` СЃС‚Р°СЂС‹Рµ demo-Р·Р°РїСѓСЃРєРё СЃРЅР°С‡Р°Р»Р° СѓРґР°Р»СЏСЋС‚СЃСЏ, Р·Р°С‚РµРј СЃРѕР·РґР°СЋС‚СЃСЏ Р·Р°РЅРѕРІРѕ.
- Р РµР°Р»СЊРЅС‹Рµ РїРѕР»СЊР·РѕРІР°С‚РµР»СЊСЃРєРёРµ РїСЂРѕРіРѕРЅС‹ РЅРµ Р·Р°С‚СЂР°РіРёРІР°СЋС‚СЃСЏ.

#### `DELETE /api/history/demo`

РЈРґР°Р»СЏРµС‚ С‚РѕР»СЊРєРѕ demo-Р·Р°РїСѓСЃРєРё, СЃРѕР·РґР°РЅРЅС‹Рµ РІСЃС‚СЂРѕРµРЅРЅС‹Рј `public demo dataset`.

#### `DELETE /api/history`

Полная очистка истории.

---

## Профили

#### `GET /api/profiles`

Список профилей.

Для каждого элемента списка дополнительно возвращаются `description`, `tags`, `author` и `updated`, чтобы Web UI мог делать поиск и быстрый обзор без отдельной загрузки каждой карточки.

#### `GET /api/profiles/{id}`

Полные данные профиля, включая learning summary, validation context последнего успешного baseline, вычисленные `effectiveTemperatures` и блок `baroCorrection` с текущим мягким смещением порогов.

- для профилей категории `mashing` в `parameters.mashing.steps[]` возвращаются шаги рецепта:
  - `temperature`
  - `duration`
  - `name`

#### `POST /api/profiles`

Создание нового профиля.

#### `PUT /api/profiles/{id}`

Обновление существующего пользовательского профиля.

- builtin-профили редактировать нельзя, backend вернёт `403`
- обновляются `metadata` и ключевые `parameters`
- статистика профиля, learning summary и validation context сохраняются

#### `POST /api/profiles/{id}/load`

Загрузка профиля в текущие настройки.

- для `rectification` и `distillation` профиль применяет рабочие уставки в контроллер
- для `mashing` профиль также помечается активным, а Web UI может сразу подставить его шаги в панель запуска режима `Затирка` и затем восстановить их после обновления страницы через `GET /api/status`

#### `DELETE /api/profiles/{id}`

Удаление профиля.

#### `DELETE /api/profiles`

Удаление всех пользовательских профилей. Встроенные рецепты сохраняются.

#### `GET /api/profiles/{id}/export`

Экспорт одного профиля в JSON.

#### `GET /api/profiles/export`

Экспорт всех профилей.

#### `POST /api/profiles/import`

Импорт профилей.

- принимает массив профилей
- принимает один профиль
- принимает snapshot-объект с полем `profiles`
- принимает snapshot-объект с полем `profile`

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

Для `GET/POST /api/settings/equipment` дополнительно используются:

- `packingType`
- `packingCoeff`
- `coolingPwmEnabled`
- `coolingPwmMinDuty`
- `coolingPwmMaxDuty`
- `coolingPwmStartupDuty`
- `coolingPwmCurrentDuty` (`GET`, live state)
- `useDs2482ForTemps`
- `ds2482Address`
- `tempBusGpioPin` (`GET`, справочно)
- `temperatureBusSource` (`GET`, live state)
- `temperatureBusSourceLabel` (`GET`, live state)
- `bodyLevelSensorEnabled`
- `bodyLevelThresholdV`
- `bodyLevelTriggerAbove`
- `leakSensorEnabled`
- `leakThresholdV`
- `leakTriggerAbove`
- `leak.enabled`, `leak.valid`, `leak.triggered`, `leak.voltage`, `leak.lastUpdate`
- `vaporSensors.enabled`, `vaporSensors.primaryValid`, `vaporSensors.primaryTriggered`, `vaporSensors.secondaryValid`, `vaporSensors.secondaryTriggered`
- `pzem.*`
- `modules.*`

Для `GET /api/settings/equipment` блок `pzem` дополнительно содержит:

- `available` — обнаружен ли `PZEM-004T`
- `uartNum`, `baudRate`
- `rxPin`, `txPin`
- `voltage`, `current`, `power`, `energy`, `frequency`, `powerFactor`

Для `GET /api/settings/equipment` блок `modules` дополнительно содержит ожидаемые аппаратные узлы:

- `bmp280Primary` — `BMP280 #1`, `I2C 0x76`, основной атмосферный датчик
- `bmp280Secondary` — `BMP280 #2`, `I2C 0x77`, резервный атмосферный датчик
- `ads1115` — `ADS1115`, `I2C 0x48`, `A1` давление куба / `A0` ареометр / `A2-A3` резервные safety-каналы
- `ads1115Secondary` — `ADS1115`, `I2C 0x49`, пока отдан в резерв под датчики уровня приёмных ёмкостей
- `ds2482` — `DS2482S-100`, `I2C 0x18..0x1B`, альтернативный мастер `1-Wire` для `DS18B20`
- `mcp4725` — `MCP4725`, `I2C 0x60`, DAC мешалки `0-10V`
- `pzem004t` — `PZEM-004T`, `UART1`, монитор питания и нагрева

Каждый элемент `modules.*` содержит минимум:

- `label`
- `available`
- `expected`
- `bus`
- `address`
- `role`

Для `GET /api/settings/equipment` блок `safetyChannels` дополнительно описывает резервные safety-каналы и их статус:

- `bodyLevel` — `ADS1115 A2`, канал под датчик уровня тела
- `leak` — `ADS1115 A3`, канал под протечку/поддон
- `vaporPrimary` — `GPIO1`, резерв под первичный датчик паров/газа
- `vaporSecondary` — `GPIO3`, резерв под вторичный ADC-канал для газа/паров

Каждый объект `safetyChannels.*` содержит минимум:

- `label`
- `status` — `ready | armed | triggered | no_signal | offline | reserved`
- `available`
- `expected`
- `planned`
- `liveReadable`
- `bus`
- `address`
- `role`
- `source`
- `enabled`
- `triggered`

Для каналов `ADS1115 A2/A3` дополнительно публикуются:

- `channel`
- `adc`
- `voltage`
- `thresholdV`
- `triggerAbove`
- `triggerMode`
- `maxVoltage`

Для `GET/POST /api/settings/rect` дополнительно используется флаг:

- `baroCorrectionEnabled` — включает или отключает мягкую барокоррекцию температурных порогов профиля.

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

### Клапаны и сервисный тест

- `POST /api/manual/valves`
- `GET /api/testing/status`
- `POST /api/testing/valves`

Для `POST /api/manual/valves` дополнительно поддерживается:

- `startStopDuty` — прямое задание `0..255` для PWM-канала охлаждения

Для `POST /api/testing/valves` доступны:

- `target: "water" | "heads" | "uno"` + `open`
- `target: "water" | "heads" | "uno"` + `action: "pulse"` + `durationMs`
- `target: "startStop"` + `duty` — сервисная установка PWM-канала охлаждения

`GET /api/testing/status` дополнительно возвращает:

- `valves.startStopDuty`
- `coolingSettings.enabled`
- `coolingSettings.minDuty`
- `coolingSettings.maxDuty`
- `coolingSettings.startupDuty`
- `activeTests.startStopDuty`
- `modules.*` — тот же снимок обязательных и опциональных модулей, что и в `GET /api/settings/equipment`, чтобы сервисный экран мог без второго запроса показать `BMP280`, оба `ADS1115`, `MCP4725` и `PZEM-004T`
- `temperatureTopology.*` — та же схема физических ролей термодатчиков, что и в `GET /api/settings/equipment`
- `supportedModes.*` — доступность режимов для текущей температурной комплектации
- `health.overall`
- `health.tempSensorsOk`
- `health.tempSensorsTotal`
- `health.bmp280`
- `health.ads1115`
- `health.pzem`
- `health.wifiConnected`
- `health.wifiRSSI`
- `health.freeHeap`
- `health.cpuTemp`
- `health.pzemSpikes`
- `health.tempErrors`
- `health.rebootReason`
- `health.rebootReasonStr`
- `health.lastUpdate`

### Калибровка

- `GET /api/calibration`
- `POST /api/calibration`
- `GET /api/calibration/scan`
- `POST /api/calibration/temp`
- `POST /api/calibration/pump`
- `POST /api/calibration/pressure`
- `POST /api/calibration/hydrometer`

`GET /api/calibration` for `temperatures[]` now also includes:

- `name`
- `address`
- `assignedAddress`
- `detectedAddress`
- `mappingMode` (`auto` or `manual`)

`GET /api/calibration` now also includes a `hydrometer` block with:

- `densityOffset`
- `pointCount`
- `abvPoints[]`
- `pressurePoints[]`
- `currentPressure`
- `currentDensity`
- `currentABV`
- `valid`

It also includes a `pressureSensor` block with:

- `pointCount`
- `zeroOffsetMmHg`
- `ads1115Available`
- `source`
- `voltagePoints[]`
- `pressurePoints[]`
- `currentVoltage`
- `currentAdc`
- `currentPressure`
- `valid`
- `calibrated`

`POST /api/calibration/pressure` accepts:

- `voltagePoints[]` + `pressurePoints[]` together to replace the pressure table
- `zeroOffsetMmHg` alone to update only the zero trim
- or both in the same request

Testing/service pressure status also includes:

- `ads1115Available`
- `source`
- `sensorVoltage`
- `sensorAdc`

`GET /api/calibration/scan` now returns `sensors[]` with:

- `index`
- `address`
- `mappedRole`
- `mappedRoleName`
- `valid`

`POST /api/calibration/temp` now accepts one of:

- `{ "index": N, "offset": X }`
- `{ "index": N, "reference": X }`
- `{ "index": N, "address": "28:FF:..." }` to pin a role to a concrete `DS18B20`
- `{ "index": N, "address": "" }` to clear manual binding and return the role to auto-mapping

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
