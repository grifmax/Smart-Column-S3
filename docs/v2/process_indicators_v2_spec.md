# Process Indicators v2 Spec

## 1. Цель слоя indicators

Process indicators нужны, чтобы режимы и safety принимали решения не по сырым температурам и давлениям напрямую, а по более устойчивым инженерным признакам процесса.

Слой indicators:

- агрегирует raw sensor values;
- считает тренды;
- даёт стабильные score и flags;
- не управляет actuator’ами сам;
- не делает phase transitions сам.

## 2. Общие требования

Каждый indicator должен иметь:

- имя;
- единицы или диапазон;
- источник данных;
- допустимую деградацию при missing sensors;
- правило публикации в UI/history;
- объяснимость для оператора.

## 3. Common indicators

### `processHealth`

- Диапазон: `0..1`
- Источник: `SystemHealth.overallHealth`
- Назначение: общая оценка пригодности текущего runtime к автоматике

### `sensorFreshnessOk`

- Тип: bool
- Основа: `temps.lastUpdate`, `pressure.lastUpdate`, `power.lastUpdate`
- Логика: false, если данные устарели относительно safety timeout

### `pressureStable`

- Тип: bool
- Основа: `dP/dt`
- Логика: true, если модуль скорости изменения давления находится в допустимом окне

### `heatingRate`

- Единицы: `°C/min`
- Основа: `dT_cube/dt`
- Использование:
  - rectification/distillation preheat
  - hold/mashing slow-heating detection

### `targetReached`

- Тип: bool
- Основа: active mode target
- Использование:
  - hold/mashing step entry
  - fermentation temperature band
  - distillation finish heuristics

### `powerLimited`

- Тип: bool
- Источник: supervisor limits
- Использование: UI и history

### `recoveryActive`

- Тип: bool
- Источник: supervisor state
- Использование: observability и anti-oscillation logic

## 4. Rectification indicators

### `boilingDetected`

- Тип: bool
- Базовый эвристический минимум:
  - `T_cube` вышла в рабочую зону
  - либо `T_column_bottom` и `T_column_top` подтверждают паровой фронт

### `columnStable`

- Тип: bool
- Основа:
  - низкий `dT_top/dt`
  - низкий `dP/dt`
  - нормальный cooling margin

### `stabilityIndex`

- Диапазон: `0..1`
- Основа:
  - drift top temp
  - drift pressure
  - запас охлаждения

### `headsCompletionScore`

- Диапазон: `0..1`
- Базовая стартовая реализация:
  - объёмная часть цели
  - плюс penalization или bonus по stability

### `bodyEndScore`

- Диапазон: `0..1`
- Базовая стартовая реализация:
  - рост `T_cube`
  - деградация стабильности
  - приближение к целевому объёму или условию окончания

### `floodRisk`

- Диапазон: `0..1`
- Основа:
  - отношение текущего давления к safety limit
  - тренд давления
  - запас по охлаждению

### `coolingMargin`

- Единицы: `°C`
- Формула: `waterOutMaxC - T_water_out`

### `takeoffAllowed`

- Тип: bool
- Источник:
  - `sensorFreshnessOk`
  - `floodRisk`
  - `coolingMargin`
  - supervisor limits

## 5. Distillation indicators

- `distHeatingComplete`
- `distHeadsOptionalComplete`
- `distBodyNearEnd`
- `distPressureMargin`

Минимальная первая версия может строиться на:

- `T_cube`
- collected volume
- pressure margin

## 6. NBK indicators

- `steamReady`
- `nbkPressureMargin`
- `nbkColumnLoad`
- `feedEnergyBalance`
- `nbkWorkingStable`
- `nbkFeedAllowed`
- `finishLikely`

Принцип для v2:

- NBK нельзя вести только по heater limit.
- Нужен баланс `heater power <-> feed rate <-> pressure/load`.

## 7. Thermal mode indicators

### Hold / Mashing

- `tempInBand`
- `stepReady`
- `stepHoldStable`
- `heatingTooSlow`
- `overshootRisk`

Использование:

- переход к выдержке только по `tempInBand`;
- тревога и UI warning при `heatingTooSlow`;
- ограничение мощности при `overshootRisk`.

### Fermentation

- `fermTempInBand`
- `longDeviation`
- `heatingDemand`
- `coolingDemand`

## 8. Что обязательно логировать

В history и event log нужно отдавать минимум:

- `processHealth`
- `sensorFreshnessOk`
- `heatingRate`
- `pressureStable`
- `floodRisk`
- `coolingMargin`
- `stabilityIndex`
- `bodyEndScore`
- `active limits`
- `last reason code`

## 9. Что обязательно показывать в UI

### Всегда

- `processHealth`
- `last reason code`
- `active limits`
- `sensor freshness`

### Для rectification/NBK

- `stabilityIndex`
- `floodRisk`
- `coolingMargin`
- `takeoffAllowed`

### Для thermal modes

- `tempInBand`
- `heatingTooSlow`
- `overshootRisk`

## 10. Технические правила первой реализации

- sampling: опираться на текущий основной loop и хранить lightweight runtime state;
- не использовать тяжёлые буферы на старте;
- все score приводить к диапазону `0..1`;
- при недоступных датчиках indicators должны деградировать предсказуемо, а не притворяться valid.

## 11. Acceptance criteria

- common indicators можно собрать из текущего `SystemState`;
- они не меняют поведение существующих режимов, пока явно не подключены;
- их можно публиковать в UI и history без изменения существующего sensor layer.
