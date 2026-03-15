# Mode Contracts v2 Spec

## 1. Цель

Сделать единый контракт режима, который:

- совместим с ESP32/Arduino C++;
- не требует переписать текущие handlers сразу;
- позволяет добавлять process indicators и safety supervisor без разлома существующих сценариев.

## 2. Основной принцип

Режим больше не должен напрямую быть единственным владельцем:

- raw sensor interpretation;
- safety policy;
- финальной схемы логирования;
- UI/export status.

Режим владеет только:

- фазовой логикой;
- режимными целями;
- правилами переходов;
- режимно-специфичными reason codes и target behavior.

## 3. Обязательные сущности

### 3.1. ModeStatusV2

Должен содержать:

- `mode`
- `lifecycle`
- `phaseId`
- `phaseToken`
- `phaseStartMs`
- `phaseElapsedSec`
- `modeStartMs`
- `modeElapsedSec`
- `paused`
- `safetyLatched`
- `activeLimits`
- `lastReason`
- `indicators`
- `commandTargets`
- `operatorMessage`

### 3.2. MetricsSnapshotV2

Должен содержать:

- timestamp
- temperatures
- pressure
- power
- pump state
- process indicators
- safety decision

### 3.3. SafetyDecisionV2

Должен содержать:

- severity
- primary event
- active limits
- reason code
- acknowledge/reset requirement
- operator-facing message

## 4. Интерфейс режима

Обязательные методы:

- `init(...)`
- `start(...)`
- `update(...)`
- `pause(...)`
- `resume(...)`
- `stop(...)`
- `getStatus()`
- `getMetricsSnapshot()`
- `getCurrentPhaseToken()`
- `getLastReasonCode()`

## 5. Семантика методов

### `init(...)`

Используется для подготовки mode runtime state без запуска процесса.

### `start(...)`

Переводит режим в активное состояние, но не должен сам bypass-ить safety/precheck policy.

### `update(...)`

Получает уже собранный runtime context:

- `SystemState`
- `Settings`
- `MetricsSnapshotV2`
- `SafetyDecisionV2`
- `nowMs`

Внутри `update(...)` режим должен решать только:

- оставаться в текущей фазе;
- перейти в другую фазу;
- обновить command targets;
- записать reason code.

### `pause(...)` / `resume(...)`

Не должны переизобретать общие shutdown/restore сценарии вне контракта.

### `stop(...)`

Должен завершать режим штатно, не подменяя safety trip.

## 6. Адаптерная модель для текущего проекта

Чтобы не переписывать текущий код, вводится промежуточная схема:

1. Существующие handlers остаются источником phase logic.
2. Поверх них появляется adapter, который строит:
   - `ModeStatusV2`
   - `MetricsSnapshotV2`
   - `ReasonCodeV2`
3. Только после этого логика переносится из namespace-functions в реальные mode controllers.

## 7. Mapping для текущих режимов

### Rectification / Distillation / Manual rectification

Пока используют `RectPhase`, поэтому контракт должен хранить:

- `phaseId`
- `phaseToken`

а не только enum одного типа.

### NBK

Имеет отдельный `NbkPhase`, значит unified contract обязан поддерживать mode-specific phase enum без копипасты.

### Hold / Mashing

Их "фаза" уже фактически temperature-step lifecycle, а не тот же enum, что у rectification. Контракт должен поддерживать режимы, где phase token задаётся профилем или step type.

### Fermentation

Сейчас почти бинарный режим. Контракт должен позволять и простую реализацию, и последующее расширение до day-profile.

## 8. Backward compatibility

До реальной миграции обязательно сохранять:

- текущие JSON поля `/api/status`;
- текущие MQTT/status payloads;
- текущие поля истории;
- существующие enum в `src/types.h`.

Новые v2-сущности должны сначала работать как дополнительный слой, а не замена.

## 9. Acceptance criteria для первой интеграции

- хотя бы один режим умеет собирать `ModeStatusV2` без изменения поведения;
- `lastReasonCode` появляется в runtime export;
- `activeLimits` публикуются как отдельный блок;
- phase transition можно логировать без ad-hoc строк в handler’е.
