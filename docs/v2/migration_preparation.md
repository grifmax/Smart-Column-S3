# Smart-Column-S3 v2 Migration Preparation

Этот документ фиксирует стартовую инженерную базу для миграции к архитектуре v2 без переписывания проекта с нуля.

## Current status after Wave 4 audit

As of firmware `2.0.35`, the migration is effectively in finalization mode rather than active architectural rewrite.

- `Wave 1` is complete in practice: mode transitions, reason codes, unified runtime status and transition logging are already wired into the real runtime.
- `Wave 2` is complete in practice: `ProcessIndicatorsV2`, `activeLimits`, `lastReasonCode` and unified `v2` snapshots are exported through local API, cloud tunnel, WebSocket/live UI and history.
- `Wave 3` is complete in practice: NBK pressure derating, manual anti-flood derating, recovery/readiness semantics and safety summaries were lifted into shared `v2` policy/supervisor layers.
- `Wave 4` is functionally complete: `RECTIFICATION`, `NBK`, `MANUAL_RECT`, `DISTILLATION`, `HOLD`, `MASHING` and `FERMENTATION` now emit explicit start/phase/finish contracts, and terminal transitions survive mode-to-idle completion in the same loop pass.

Current realistic migration estimate: `98-99%`.

### What is still intentionally open

- `RC_PHASE_TRANSITION_INFERRED` still exists, but now mainly as an honest adapter fallback for any future missing explicit contract; it is no longer used as a normal business reason for known recovery paths.
- `RC_UNSPECIFIED` still exists in the enum and UI compatibility layer, but primary runtime happy-path and safety-path logic has been reduced to near-zero usage.
- The remaining high-value work is verification, not redesign: run through real process scenarios and confirm that `RC_PHASE_TRANSITION_INFERRED` does not appear on normal start/phase/finish flows.
- If inferred transitions still show up during real device runs, the next change should be surgical: add the missing explicit `notePhaseTransition(...)` in that exact handler path instead of expanding fallback logic in the adapter.

### Practical definition of final done

The migration can be treated as operationally complete when all of the following are true:

- normal scenario runs for `rectification`, `distillation`, `nbk`, `manual rect`, `hold`, `mashing` and `fermentation` complete without unexpected `RC_PHASE_TRANSITION_INFERRED`;
- history, live `/api/status`, safety action endpoints and UI notifications show the same explicit terminal reasons for the same run;
- no primary runtime path emits `RC_UNSPECIFIED`;
- any remaining fallback reason is clearly intentional and documented as compatibility reserve rather than active behavior.

## 1. Что уже есть и должно быть сохранено

- `src/control/fsm.*` уже выступает центральным оркестратором режимов.
- `src/control/modes/*.cpp` уже отделяют режимную логику по handlers.
- `src/control/safety.*` уже содержит общий safety-контур и лэтчинг аварий.
- `src/interface/webserver.cpp`, `src/interface/mqtt.cpp`, `src/interface/cloud_tunnel.cpp` уже экспортируют богатый runtime status.
- `src/storage/logger.*` и `src/history.*` уже создают основу observability и process history.
- `src/drivers/*` уже отделяют sensor/actuator слой от UI и storage.

Главный принцип подготовки: не ломать текущий `FSM + handlers`, а навесить поверх них единые v2-контракты и постепенно переносить ответственность наверх.

## 2. Карта текущего кода по слоям v2

| V2 layer | Текущее состояние | Основные файлы |
| --- | --- | --- |
| Sensor layer | Уже есть, но без отдельного unified snapshot API | `src/drivers/sensors.*`, `src/types.h` |
| Process indicators | Почти отсутствует, вычисления живут в handlers и UI-эвристиках | `src/control/modes/*.cpp`, `src/control/fsm_utils.*` |
| Mode FSM | Есть, но с разной глубиной зрелости по режимам | `src/control/fsm.*`, `src/control/modes/*.cpp` |
| Safety supervisor | Есть, но часть limited/recovery логики сидит внутри handlers | `src/control/safety.*`, `src/control/modes/manual_rect_handler.cpp`, `src/control/modes/nbk_handler.cpp` |
| Actuator layer | Есть, но команды задаются прямо из handlers | `src/drivers/heater.*`, `src/drivers/pump.*`, `src/drivers/valves.*` |
| Logging / observability | Есть, но без единого transition contract и reason codes | `src/storage/logger.*`, `src/history.*` |
| Mode API / status export | Есть, но схема статуса не нормализована под v2 | `src/interface/webserver.cpp`, `src/interface/mqtt.cpp`, `src/interface/cloud_tunnel.cpp` |

## 3. Главные разрывы между текущим кодом и v2

### 3.1. Нет единого mode contract

Сейчас режимы обновляются через namespace-функции и общие поля `SystemState`. Это работает, но не даёт:

- единый `status`;
- единый `metrics snapshot`;
- единый `reason code`;
- единую точку для внедрения supervisor/indicators.

### 3.2. Нет process indicators layer

Сейчас решения принимаются напрямую по raw sensor values:

- `rect_handler.cpp` завершает нагрев и стабилизацию по фиксированным порогам и таймерам;
- `distillation_handler.cpp` использует одномерные критерии старта и финиша;
- `nbk_handler.cpp` ограничивает heater по давлению, но без formalized feed-energy model;
- `hold` и `mashing` опираются на простой proportional loop без индикаторов качества.

### 3.3. Safety не отделена от degradation/recovery логики

`Safety::check()` хорошо закрывает hard-stop сценарии, но не покрывает целиком:

- `warning`;
- `limited`;
- `recovery`;
- policy-блокировку переходов фаз;
- общие active limits для UI.

Часть реакций живёт локально в handlers:

- anti-flood logic в manual rectification;
- pressure-based derating в NBK.

### 3.4. Phase transitions и logging не нормализованы

Сейчас:

- часть переходов логируется через `LOG_I`;
- часть через `Logger::logf`;
- часть через `MQTT::publishNotification`;
- причины переходов и снапшоты параметров не стандартизованы.

Это мешает истории, UI и последующей аналитике.

## 4. Что добавлено в этой подготовке

### 4.1. Документы

- `docs/v2/mode_contracts_v2_spec.md`
- `docs/v2/process_indicators_v2_spec.md`
- `docs/v2/migration_preparation.md`

### 4.2. Стартовый кодовый каркас

Новый изолированный namespace `ControlV2` в `src/control/v2/`:

- `reason_codes.h`
- `mode_contracts.h`
- `process_indicators.h/.cpp`
- `transition_logger.h/.cpp`

Этот каркас не подключён в текущий runtime и не меняет существующее поведение. Его задача: дать стабильные типы и точки интеграции для следующих итераций.

## 5. Рекомендуемый порядок реальной миграции

### Wave 1. Contracts first

1. Вынести reason codes и transition events в реальные mode handlers.
2. Начать логировать фазовые переходы через `TransitionLoggerV2`.
3. Собрать `ModeStatusV2` поверх существующего `SystemState`, не меняя бизнес-логику.

### Wave 2. Indicators before behavior rewrite

1. Подключить `ProcessIndicatorsEngineV2` в `FSM::update()`.
2. Публиковать indicators в `/api/status`, MQTT и cloud tunnel.
3. Сохранять key indicators в history snapshots.

### Wave 3. Lift safety out of handlers

1. Перенести NBK pressure-derating из handler в supervisor/policy слой.
2. Перенести anti-flood/manual derating из manual rectification в общий limited/recovery policy.
3. Начать публиковать `active limits` и `last reason code`.

### Wave 4. Migrate one mode at a time

Порядок из mode matrix:

1. Rectification
2. NBK
3. Manual rectification
4. Distillation
5. Hold / Mashing
6. Fermentation

## 6. Где интегрировать дальше

### 6.1. Точки сборки snapshot/status

- `src/interface/webserver.cpp`
- `src/interface/mqtt.cpp`
- `src/interface/cloud_tunnel.cpp`

Именно здесь нужно будет заменить ad-hoc JSON на unified export layer.

### 6.2. Точки принятия решений

- `src/control/fsm.cpp`
- `src/control/modes/rect_handler.cpp`
- `src/control/modes/distillation_handler.cpp`
- `src/control/modes/manual_rect_handler.cpp`
- `src/control/modes/nbk_handler.cpp`
- `src/control/modes/mashing_handler.cpp`
- `src/control/modes/hold_handler.cpp`
- `src/control/modes/fermentation_handler.cpp`

### 6.3. Точки observability

- `src/storage/logger.cpp`
- `src/history.cpp`
- `src/interface/telegram.cpp`

## 7. Definition of done для следующей практической итерации

Следующий шаг можно считать успешным, если будут выполнены все пункты:

- хотя бы rectification и NBK начнут отдавать `ModeStatusV2`;
- phase changes будут логироваться единообразно с `ReasonCodeV2`;
- `/api/status` начнёт публиковать `indicators`, `activeLimits`, `lastReasonCode`;
- history snapshots начнут сохранять минимум common indicators;
- текущие UI и мобильный клиент не сломаются по backward compatibility.
