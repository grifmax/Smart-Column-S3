# Smart-Column-S3 — Architecture v2 Outline

Этот документ — **каркас будущей архитектуры v2** для проекта **Smart-Column-S3**.  
Он не заменяет код и не требует немедленного полного рефакторинга.  
Его задача — дать **единый инженерный шаблон**, в который можно последовательно переносить решения из текущей кодовой базы.

---

## 1. Цель документа

Использовать этот outline как основу для:

- проектирования следующей версии автоматики;
- обсуждения архитектуры с Codex / Claude / GPT;
- декомпозиции на задачи;
- постепенного рефакторинга без переписывания проекта с нуля;
- выравнивания подходов между режимами:
  - rectification,
  - distillation,
  - manual rectification,
  - NBK,
  - mashing,
  - hold,
  - fermentation.

---

## 2. Главная инженерная цель v2

Перейти от модели:

> «каждый режим сам по себе как умеет читает датчики, принимает решения и локально защищается»

к модели:

> «все режимы работают в единой архитектуре: sensors → process indicators → mode FSM → safety supervisor → actuators → logging/UI»

---

## 3. Архитектурные принципы

### 3.1. Эволюционное развитие
Ничего не переписывать с нуля.  
Сохранять сильные стороны текущего проекта:
- модульность,
- отдельные handlers,
- существующий FSM,
- safety,
- web/UI,
- process history,
- telemetry.

### 3.2. Разделение ответственности
Нужно чётко отделить:
- raw sensor values,
- derived process indicators,
- mode logic,
- safety decisions,
- actuator commands,
- logging/observability.

### 3.3. Объяснимость автоматики
Любое решение автоматики должно быть объяснимо:
- почему произошёл переход фазы;
- почему ограничили мощность;
- почему уменьшили отбор;
- почему случился trip.

### 3.4. Единый контракт режима
Каждый режим должен подчиняться единой модели интеграции.

### 3.5. Safety выше FSM
Safety supervisor всегда имеет приоритет над режимным handler.

---

## 4. Целевая слоистая архитектура v2

### 4.1. Sensor Layer
Ответственность:
- чтение сырых данных;
- валидация;
- timestamp свежести;
- базовая диагностика датчиков.

Примеры:
- температуры;
- давление;
- мощность;
- расход/объём;
- состояния исполнительных устройств.

Не должен:
- принимать технологические решения;
- выполнять safety policy;
- решать, когда переходить фазу.

---

### 4.2. Process Indicators Layer
Ответственность:
- вычисление производных признаков процесса;
- сглаживание;
- тренды;
- score-модели;
- оценка устойчивости и качества процесса.

Примеры индикаторов:
- `stabilityIndex`
- `floodRisk`
- `coolingMargin`
- `heatingRate`
- `boilingDetected`
- `targetReached`
- `headsCompletionScore`
- `bodyEndScore`
- `processHealth`

Не должен:
- напрямую управлять actuators;
- переключать фазы;
- выполнять trip.

---

### 4.3. Mode FSM Layer
Ответственность:
- фазовая логика режима;
- цели режима;
- правила переходов;
- режимные reason codes.

FSM должна работать в первую очередь через:
- settings,
- mode context,
- process indicators,
- safety decisions.

Не должна:
- самостоятельно реализовывать глобальную safety policy;
- читать все датчики напрямую, если есть соответствующие indicators;
- дублировать logging rules.

---

### 4.4. Safety Supervisor
Ответственность:
- глобальный контроль рисков;
- классификация событий;
- ограничение режима;
- recovery;
- trip;
- latched trip.

Уровни:
- info
- warning
- limited
- recovery
- trip
- latched_trip

Не должен:
- содержать полную фазовую логику всех режимов;
- заменять режимный FSM.

---

### 4.5. Actuator / Control Layer
Ответственность:
- применение команд:
  - heater power,
  - pump rate,
  - valve routing,
  - water control,
  - auxiliary devices.
- rate limit;
- bounds;
- sanity checks;
- safe state fallback.

Не должен:
- сам решать технологические цели режима.

---

### 4.6. Logging / Telemetry / Observability Layer
Ответственность:
- единый лог событий;
- phase transition log;
- safety log;
- snapshots;
- history;
- telemetry export;
- UI status export.

---

### 4.7. Mode API / Status Export Layer
Ответственность:
- единое представление состояния режима для UI / API / history;
- публикация phase status;
- публикация active limits;
- публикация reason codes;
- публикация текущих индикаторов.

---

## 5. Unified Mode Contract v2

Каждый режим должен вписываться в общий контракт.

### 5.1. Обязательные элементы контракта
- `init(...)`
- `start(...)`
- `update(...)`
- `pause(...)`
- `resume(...)`
- `stop(...)`
- `getStatus()`
- `getMetricsSnapshot()`
- `getCurrentPhase()`
- `getLastReasonCode()`

### 5.2. Что должен содержать status
- mode
- current phase
- phase start time
- phase elapsed
- mode start time
- mode elapsed
- active limits
- active warnings
- active recovery flags
- key indicators
- current targets
- last transition reason

### 5.3. Что должен содержать metrics snapshot
- temperatures
- pressure
- power
- volume / rate
- process indicators
- safety state
- mode-specific metrics

---

## 6. Unified Phase Transition Contract

Каждый переход фазы должен логироваться одинаково.

### Обязательные поля transition event
- `mode`
- `from_phase`
- `to_phase`
- `reason_code`
- `operator_message`
- `timestamp`
- `phase_elapsed`
- `mode_elapsed`
- `key_indicators_snapshot`
- `active_safety_limits`
- `mode_specific_metadata`

### Примеры reason codes
- `RC_PRECHECK_OK`
- `RC_PRECHECK_FAIL_SENSOR`
- `RC_HEATING_COMPLETE`
- `RC_STABILIZATION_TIMEOUT_OK`
- `RC_STABILITY_WINDOW_REACHED`
- `RC_HEADS_VOLUME_REACHED`
- `RC_BODY_END_DETECTED`
- `RC_RECOVERY_ENTERED`
- `RC_SAFETY_TRIP_PRESSURE`
- `RC_MANUAL_OPERATOR_SWITCH`

---

## 7. Safety Hierarchy v2

### 7.1. Info
События без ограничений.  
Примеры:
- старт режима;
- переход фазы;
- восстановление датчика;
- успешная стабилизация.

### 7.2. Warning
Процесс продолжается, но есть отклонение.  
Примеры:
- повышенный дрейф температуры;
- ухудшение охлаждения;
- шум датчика.

### 7.3. Limited
Режим ограничивается автоматически.  
Примеры:
- рост flood risk;
- нестабильное давление;
- недостаточный cooling margin.

Действия:
- cap power;
- cap takeoff;
- запрет перехода в следующую фазу;
- предупреждение оператору.

### 7.4. Recovery
Временное деградированное поведение с попыткой вернуться в рабочий режим.

Действия:
- уменьшить мощность;
- уменьшить/остановить отбор;
- дождаться окна устойчивости;
- либо вернуться;
- либо trip.

### 7.5. Trip
Безопасный останов процесса.

### 7.6. Latched Trip
Trip с обязательным acknowledge/reset перед новым стартом.

---

## 8. Process Indicators Framework v2

### 8.1. Общие индикаторы
- `processHealth`
- `sensorFreshnessOk`
- `pressureStable`
- `heatingRate`
- `targetReached`
- `powerLimited`
- `recoveryActive`

### 8.2. Rectification indicators
- `boilingDetected`
- `columnStable`
- `stabilityIndex`
- `headsCompletionScore`
- `bodyEndScore`
- `floodRisk`
- `coolingMargin`
- `takeoffAllowed`

### 8.3. Distillation indicators
- `distHeatingComplete`
- `distHeadsOptionalComplete`
- `distBodyNearEnd`
- `distPressureMargin`

### 8.4. NBK indicators
- `feedEnergyBalance`
- `nbkPressureMargin`
- `nbkColumnLoad`
- `nbkWorkingStable`
- `nbkFeedAllowed`

### 8.5. Hold / Mashing indicators
- `tempInBand`
- `stepReady`
- `stepHoldStable`
- `heatingTooSlow`
- `overshootRisk`

### 8.6. Fermentation indicators
- `fermTempInBand`
- `longDeviation`
- `heatingDemand`
- `coolingDemand`

---

## 9. Logging and Observability Contract

### 9.1. Что логировать обязательно
- старт режима;
- стоп режима;
- переход фазы;
- вход в limited/recovery/trip;
- изменения active limits;
- восстановление из recovery;
- ошибки датчиков;
- operator actions.

### 9.2. Что должно быть в snapshot
- timestamp
- mode
- phase
- temperatures
- pressure
- power
- flow/volume
- key indicators
- active limits
- active alarms
- last reason code

### 9.3. Что должно попасть в history
- phase timeline
- safety events timeline
- summary KPIs
- per-phase duration
- per-phase average metrics
- stop reason
- operator interventions

---

## 10. Целевая структура модулей v2

### Возможный каркас
- `control/process_indicators.h/.cpp`
- `control/mode_contracts.h`
- `control/mode_status.h`
- `control/reason_codes.h`
- `control/metrics_snapshot.h`
- `control/transition_logger.h/.cpp`
- `control/safety_supervisor.h/.cpp`
- `control/safety_events.h`
- `control/recovery_controller.h/.cpp`
- `control/common/...`
- `control/rectification/...`
- `control/distillation/...`
- `control/nbk/...`
- `control/thermal/...`

### Принцип
Общее — наверх.  
Режимно-специфичное — оставлять в mode handlers.

---

## 11. Приоритетный roadmap v2

### Phase 1 — Architecture cleanup
- унифицировать mode status;
- унифицировать transition events;
- ввести reason codes;
- выровнять logging contract.

### Phase 2 — Process indicators
- сделать базовый engine indicators;
- внедрить общие тренды и scores;
- минимально интегрировать в UI/history.

### Phase 3 — Rectification improvements
- улучшить heating/stabilization criteria;
- ввести stability-based logic;
- доработать body controller;
- formalize recovery.

### Phase 4 — NBK improvements
- выделить feed-energy indicators;
- улучшить working-phase control;
- усилить degradation/recovery logic.

### Phase 5 — Safety unification
- поднять safety из handlers в supervisor;
- ввести limited/recovery/trip policy.

### Phase 6 — Hold/Mashing/Fermentation normalization
- унифицировать temp-control patterns;
- добавить timeouts / overshoot / slow heating signals.

### Phase 7 — Observability/UI integration
- отдать в UI indicators, active limits, transition reasons;
- улучшить history и post-analysis.

---

## 12. Открытые вопросы для v2

Ниже секция, которую можно заполнять по мере работы с Codex или вручную.

### 12.1. Общие
- Какие indicators должны храниться в history полностью, а какие только как summary?
- Где проходит граница между mode-specific logic и supervisor logic?
- Какие safety events должны быть latched?

### 12.2. Rectification
- Какие критерии heads/body/end будут основными?
- Как именно формировать `stabilityIndex`?
- Какой алгоритм Smart Decrement оставить/переделать?

### 12.3. NBK
- Управлять в первую очередь heater или feed?
- Какие признаки перегрузки колонны считать главными?
- Какой recovery сценарий для NBK считать эталонным?

### 12.4. Thermal modes
- Хватит ли PI/P-регулирования?
- Нужна ли отдельная стратегия anti-overshoot?
- Какие таймауты выхода на шаг обязательны?

---

## 13. Шаблон задач для дальнейшей декомпозиции

### Task template
- **Title**
- **Goal**
- **Why**
- **Touched modules**
- **Inputs**
- **Outputs**
- **Dependencies**
- **Acceptance criteria**
- **Priority**

---

## 14. Минимальные deliverables для первой итерации

Чтобы v2 не расползлась, достаточно начать с четырёх артефактов:

1. `mode_contracts.h`
2. `reason_codes.h`
3. `transition_logger.*`
4. `process_indicators.*`

Если эти 4 вещи будут сделаны хорошо, дальше уже можно спокойно переносить режимы по одному.

---

## 15. Как использовать этот документ

Этот outline лучше использовать так:

1. Прикладывать его вместе с таблицей анализа режимов.
2. Давать Codex задачу не “пиши всё”, а “закрой конкретный раздел”.
3. После каждого шага обновлять этот файл.
4. Поддерживать его как **живой архитектурный документ**, а не как статичную теорию.

---

## 16. Следующий практический шаг

Наиболее полезный следующий документ после этого outline:

- `process_indicators_v2_spec.md`

Либо:
- `mode_contracts_v2_spec.md`

Именно с них удобнее всего начинать реальную миграцию архитектуры.
