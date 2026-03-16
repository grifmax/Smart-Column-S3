
# Smart-Column-S3 — Codex / Architecture Analysis Prompt Pack

Этот файл содержит:
- **Основной промпт (простыня)** для архитектурного анализа проекта Smart-Column-S3.
- **Наволочка 1** — запрос структуры файлов/модулей.
- **Наволочка 2** — запрос псевдокода C++ для ключевых архитектурных элементов.

Его можно целиком скармливать Codex / LLM для получения архитектурного плана развития проекта.

---

# ОСНОВНОЙ ПРОМПТ (ПРОСТЫНЯ)

Сначала внимательно прочитай весь документ целиком, не делая преждевременных выводов.

Ты выступаешь как **principal embedded engineer, systems architect и technical lead** для проекта **Smart-Column-S3**.

## Контекст проекта

Smart-Column-S3 — это **многорежимная платформа автоматики на ESP32-S3** для технологических процессов.

В проекте есть режимы:

- rectification
- distillation
- manual rectification
- NBK
- mashing
- hold
- fermentation

У проекта уже есть:

- модульная архитектура
- FSM
- отдельные handlers режимов
- safety
- web UI
- process history
- storage/settings
- telemetry/notifications
- фазовые сценарии режимов

Я передаю тебе **таблицу инженерного анализа режимов проекта**.

Этот документ **не финальная спецификация**, а **аналитическая база** для проектирования следующей версии автоматики.

Твоя задача:

**Не пересказать документ, а переработать его в практический план архитектурного и кодового рефакторинга.**

Главная цель:

Спроектировать **эволюционную v2 архитектуру автоматики** без переписывания проекта с нуля.

---

# РЕЗУЛЬТАТ ДОЛЖЕН БЫТЬ ПРИГОДЕН ДЛЯ

- дальнейшей работы в Codex
- декомпозиции на coding tasks
- рефакторинга существующих модулей
- создания новых .h/.cpp файлов
- внедрения unified contracts
- внедрения process indicators layer
- внедрения safety hierarchy
- стандартизации phase transitions
- стандартизации logging и telemetry

---

# ЧТО НУЖНО СДЕЛАТЬ

## 1. Определи сильные стороны текущей архитектуры

Какие решения правильные и должны быть сохранены:

- разделение режимов
- структура handlers
- существующий FSM
- safety
- telemetry
- logging
- history

Определи, **что нельзя ломать**.

---

## 2. Найди системные проблемы

Например:

- дублирование логики между режимами
- смешивание sensor logic / FSM / safety
- отсутствие process indicators layer
- отсутствие единого mode contract
- различия в phase transitions
- различия в logging
- safety-логика внутри handlers

---

## 3. Спроектируй целевую архитектуру v2

Раздели систему на слои:

- Sensor layer
- Process indicators layer
- Mode FSM layer
- Safety supervisor
- Actuator / control layer
- Logging / telemetry layer
- Mode API / status export layer

---

## 4. Проанализируй каждый режим

Для каждого режима:

- purpose
- phases
- maturity level
- current strengths
- current weaknesses
- target behaviour
- required process indicators
- required safety reactions
- refactor priority

---

## 5. Сформируй Unified Mode Contract v2

Контракт режима должен быть пригоден для **C++ / ESP32**.

Нужно определить:

- интерфейс режима
- обязательные методы
- формат status
- reason codes
- metrics snapshot
- start/update/stop/pause/resume

---

## 6. Сформируй Phase Transition Contract

Каждый переход фазы должен содержать:

- from
- to
- reason_code
- operator_message
- timestamp
- phase_elapsed
- key indicators snapshot
- active safety limits
- metadata

---

## 7. Спроектируй Safety Hierarchy v2

Уровни:

- info
- warning
- limited
- recovery
- trip
- latched_trip

Определи:

- события
- реакции
- UI поведение
- взаимодействие с FSM

---

## 8. Создай Process Indicators Framework

Определи:

### Общие индикаторы

- stabilityIndex
- floodRisk
- coolingMargin
- heatingRate
- targetReached
- completionScore
- processHealth

### Специфические

- rectification indicators
- NBK indicators
- hold/mashing indicators
- fermentation indicators

Укажи:

- derived indicators
- какие логировать
- какие отображать в UI

---

## 9. Logging + Observability Contract

Определи:

Что логируется:

- при старте режима
- при переходе фазы
- при safety событиях
- при recovery
- при trip

Определи структуру:

- process history
- metrics snapshot
- events timeline

---

## 10. Практический план рефакторинга

Не абстрактно.

Определи:

- какие файлы создать
- какие изменить
- какую логику переместить
- какие enum добавить
- какие struct создать
- какие helper modules нужны

---

## 11. Naming Scheme

Определи naming для:

- enums
- structs
- reason codes
- indicators
- safety events
- transitions

---

## 12. Implementation Roadmap

Фазы:

1. Architecture cleanup
2. Process indicators
3. Rectification improvements
4. NBK improvements
5. Safety unification
6. Hold/Mashing/Fermentation normalization
7. Observability + UI integration

---

## 13. Coding Tasks

Разбей на задачи.

Для каждой:

- цель
- файлы
- результат
- зависимости
- priority

---

# ОГРАНИЧЕНИЯ

- не переписывать проект с нуля
- сохранять сильные решения
- эволюционная архитектура
- практические предложения
- ориентироваться на ESP32 / C++

---

# СТИЛЬ ОТВЕТА

- principal engineer
- конкретно
- без воды
- инженерная аргументация
- практически применимо

---

# ФОРМАТ ОТВЕТА

1. Architecture strengths to preserve
2. Systemic weaknesses
3. Target layered architecture
4. Per‑mode analysis
5. Unified mode contract
6. Phase transition contract
7. Safety hierarchy
8. Process indicators framework
9. Logging model
10. Codebase refactor plan
11. File/module structure
12. Naming scheme
13. Implementation roadmap
14. Coding tasks

---

# НАВОЛОЧКА 1 — СТРУКТУРА ФАЙЛОВ

После основного анализа предложи **целевую структуру файлов/модулей**.

Например:

control/

process_indicators.h / .cpp  
mode_contracts.h  
transition_logger.h / .cpp  
safety_supervisor.h / .cpp  
safety_events.h  
reason_codes.h  
mode_status.h  
metrics_snapshot.h  
recovery_controller.h / .cpp  

rectification/  
nbk/  
common/  

Для каждого файла укажи:

- ответственность
- публичный интерфейс
- зависимости
- что в нём **не должно находиться**

---

# НАВОЛОЧКА 2 — ПСЕВДОКОД C++

После архитектуры выдай **пример псевдокода C++**:

Покажи:

- enum Phase
- enum ReasonCode
- struct ProcessIndicators
- struct MetricsSnapshot
- struct PhaseTransitionEvent
- struct SafetyDecision

Интерфейсы:

IModeController

ModeContext

SafetySupervisor

ProcessIndicatorsEngine

TransitionLogger

Код должен быть **C++‑style pseudocode**, без конкретных include, но достаточно детализированным для переноса в .h/.cpp.
