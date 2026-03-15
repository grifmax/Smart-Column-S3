# Журнал изменений

Все значимые изменения в этом проекте будут документированы в этом файле.

Формат основан на [Keep a Changelog](https://keepachangelog.com/ru/1.0.0/),
и этот проект придерживается [Semantic Versioning](https://semver.org/lang/ru/).

---

---

## [2.0.17] - 2026-03-15

### Изменено
- `src/control/safety.cpp` теперь ставит централизованные v2 operator actions для `ack` и успешного `reset`, а `src/control/v2/status_adapter.cpp` записывает их в persistent history как отдельные события `RC_SAFETY_ACKNOWLEDGED` и `RC_SAFETY_RESET_COMPLETED`, не размазывая логику по `webserver` и `cloud_tunnel`. (codex)
- Запись operator actions проходит через pending hook внутри `status_adapter`, поэтому порядок safety timeline остаётся согласованным с runtime-переходами `recovery/trip`, а `ack/reset` не спорят с `RC_SAFETY_RECOVERY_EXITED` в тот же цикл обновления. (codex)
- Frontend history и runtime labels обновлены для новых reason codes: `src/web/history/details.js`, `src/web/styles/_modal.css` и `src/web/runtime/process-notifications.js` теперь показывают подтверждение и сброс аварии как отдельные info-события. (codex)
- Версия прошивки поднята до `2.0.17` как отдельный шаг миграции safety history к полному сценарию `trip -> ack -> recovery -> reset`. (codex)

## [2.0.16] - 2026-03-15

### Изменено
- `src/control/v2/status_adapter.cpp` теперь пишет в persistent history не только финальный `safety stop`, но и live safety transitions `limited`, `recovery` и `latched trip`, когда меняются v2 `severity/reasonCode`, так что history получает более полную safety-хронологию процесса. (codex)
- Для recovery добавлен отдельный history-event `RC_SAFETY_RECOVERY_EXITED`, а финальная запись safety stop теперь дедуплицируется относительно уже сохранённого transition event, чтобы post-mortem timeline не засорялся одинаковыми аварийными сообщениями. (codex)
- `ProcessRecorder::addWarning()` в `src/history.cpp` теперь защищён проверкой `recording`, чтобы warning/error события не могли попасть в history вне активной process-сессии. (codex)
- Версия прошивки поднята до `2.0.16` как отдельный шаг миграции persistent history к полноценной v2 safety timeline. (codex)

## [2.0.15] - 2026-03-15

### Изменено
- History details modal в `src/web/history/details.js` теперь собирает отдельный `Safety timeline` из сохранённых `results.errors` и `results.warnings`, сортирует события по времени и показывает в post-mortem виде аварии, recovery и safety-ограничения процесса. (codex)
- Safety timeline использует существующие v2 `reasonCode` (`RC_SAFETY_TRIP_*`, `RC_SAFETY_RECOVERY_*`, `RC_SAFETY_LIMIT_*`) для человекочитаемых заголовков событий, так что оператор видит не просто raw message, а структуру safety-сценария процесса. (codex)
- В `src/web/styles/_modal.css` добавлены визуальные состояния для safety timeline card/badge (`error`, `limited`, `recovery`), чтобы критические trip-события, ограничения и восстановление условий безопасности визуально различались в истории. (codex)
- Версия прошивки поднята до `2.0.15` как отдельный шаг миграции history UI в сторону полноценного safety post-mortem экрана. (codex)

## [2.0.14] - 2026-03-15

### Изменено
- History details modal в `src/web/history/details.js` расширен блоками `Ошибки и аварии`, `Предупреждения` и `Заметки`, которые показывают сохранённые `results.errors`, `results.warnings` и `notes`, включая `reasonCode` и `operatorMessage` для post-mortem анализа процесса. (codex)
- Отрисовка фаз и новых history event-блоков переведена на безопасное создание DOM-узлов через `textContent` вместо прямой подстановки произвольных строк в `innerHTML`, чтобы `operatorMessage` и заметки не расширяли XSS-поверхность UI. (codex)
- В `src/web/styles/_modal.css` добавлены лёгкие стили для history events и full-width history sections, чтобы safety warnings/errors читались как отдельные карточки внутри модального окна. (codex)
- Версия прошивки поднята до `2.0.14` как отдельный шаг миграции, который делает history UI полезным для разбора ошибок и safety-сценариев после завершения процесса. (codex)

## [2.0.13] - 2026-03-15

### Изменено
- В актуальный `src/interface/webserver.cpp` возвращены рабочие history-endpoints: `/api/history`, `/api/history/{id}`, `/api/history/{id}/export`, а также удаление одного процесса и очистка всей истории, чтобы Web UI снова работал с текущим сервером, а не зависел от устаревшего `src/old/web.cpp`. (codex)
- `exportProcessToJSON()` в `src/history.cpp` больше не является заглушкой: JSON-export теперь включает полную историю процесса, включая `timeseries`, `results`, `phases` и новые v2-поля `reasonCode/operatorMessage`. (codex)
- History modal на фронтенде (`src/web/history/details.js`) теперь показывает причину завершения фазы и операторский комментарий, если они были сохранены в history, так что v2 причинная модель стала видна не только в live runtime, но и в UI истории. (codex)
- Версия прошивки поднята до `2.0.13` как отдельный шаг миграции, который возвращает history API в актуальный runtime и открывает доступ к новым v2 history-данным через Web UI. (codex)

## [2.0.12] - 2026-03-15

### Изменено
- История процессов в `src/history.*` расширена v2-полями `reasonCode` и `operatorMessage` для фаз и предупреждений, а загрузка старых JSON-файлов остаётся обратносуместимой за счёт optional-deserialization новых полей. (codex)
- `ProcessRecorder` больше не пишет устаревшую версию схемы `1.3.0`: новые history-сессии теперь помечаются текущей версией прошивки, а warning-записи тоже могут хранить v2 reason context. (codex)
- `src/control/v2/status_adapter.cpp` подключён к persistent history recorder: при старте режима автоматически открывается history-сессия, при фазовых переходах сохраняются завершённые фазы с `reasonCode/operatorMessage`, а при остановке процесса history закрывается с учётом natural finish vs stop/safety stop. (codex)
- Версия прошивки поднята до `2.0.12` как отдельный шаг миграции, который переносит v2 причинную модель из live/event log слоя в постоянную process history. (codex)

## [2.0.11] - 2026-03-15

### Изменено
- Экспорт последних событий в `src/storage/logger.cpp` расширен structured-полями для v2 phase transition log: JSON `/api/logs/events` теперь, помимо исходного `message`, отдаёт `kind`, `mode`, `fromPhase`, `toPhase`, `reasonCode` и `operatorMessage`, если запись распознана как `phase_transition`. (codex)
- CSV-выгрузка recent events дополнена колонками `kind`, `mode`, `from_phase`, `to_phase`, `reason_code` и `operator_message`, поэтому transition log можно анализировать как структурированный журнал причин переходов, а не только как плоский текст. (codex)
- Версия прошивки поднята до `2.0.11` как отдельный шаг миграции, который протягивает v2 причинную модель из live runtime в event log export слой. (codex)

## [2.0.10] - 2026-03-15

### Изменено
- Browser notifications переведены на отдельный v2-aware helper `runtime/process-notifications.js`, который сравнивает текущее runtime-состояние и дедуплицирует события, поэтому уведомления о запуске, завершении и смене этапа теперь одинаково работают и при polling, и при WebSocket-обновлениях. (codex)
- Уведомления больше не ограничиваются только mode/phase: в body теперь подставляются `v2.lastReasonCode` и `v2.operatorMessage`, так что оператор видит не только факт перехода, но и его причину в терминах новой v2 модели. (codex)
- `status.js` и `update-ui.js` очищены от разрозненной ad-hoc логики уведомлений и переведены на общий runtime helper, а версия прошивки поднята до `2.0.10` как отдельный шаг миграции process notifications на v2 contracts. (codex)

## [2.0.9] - 2026-03-15

### Изменено
- WebSocket live-status в `webserver.cpp` расширен блоком `v2` safety summary, а cloud tunnel `/api/status` в `cloud_tunnel.cpp` теперь тоже возвращает `v2`-статус, чтобы frontend получал одну и ту же safety-модель и при локальном streaming, и при cloud/polling работе. (codex)
- На frontend добавлен единый helper `runtime/safety-state.js`, который нормализует `alarm + v2.safety` в общую UI-модель; на него переведены `safety-modal.js` и landing safety chip, поэтому интерфейс теперь одинаково интерпретирует `alert`, `acknowledged`, `limited` и `recovery/reset ready`. (codex)
- Landing screen теперь показывает более точный safety status (`SAFETY OK`, `SAFETY WARN`, `SAFETY LIMITED`, `SAFETY ACKED`, `RESET READY`) вместо одного только `safetyOk`, а версия прошивки поднята до `2.0.9` как отдельный шаг унификации live safety contract. (codex)

## [2.0.8] - 2026-03-15

### Изменено
- Frontend runtime в `globals.js` и `state.js` расширен хранением `v2` safety-состояния, чтобы UI мог принимать решение по `severity`, `reasonCode`, `resetAvailable` и `resetBlockedReason` напрямую из новых v2 API-ответов. (codex)
- `safety-modal.js` больше не ждёт следующий общий status tick после `ack/reset`: модалка сразу применяет JSON-ответ action endpoint, корректно различает подтверждение и реальный reset и не переоткрывает окно для уже acknowledged alarm без доступного сброса. (codex)
- Основная кнопка safety modal теперь переключается между действиями `ack` и `reset` по текущему recovery/reset состоянию, а версия прошивки поднята до `2.0.8` как отдельный шаг интеграции нового v2 frontend contract. (codex)

## [2.0.7] - 2026-03-15

### Изменено
- `/api/safety/ack` и `/api/safety/reset` в `webserver.cpp` и `cloud_tunnel.cpp` теперь возвращают не только legacy `success/alarm`, но и отдельный блок `v2` с `severity`, `reasonCode`, `requiresAcknowledge`, `resetAvailable`, `resetBlockedReason`, `safetyLatched` и `lastReasonCode`. (codex)
- После `ack/reset` runtime v2 принудительно обновляется через `ControlV2::updateRuntime(...)`, чтобы ответ API отражал уже актуальное safety/recovery состояние, а не предыдущий snapshot. (codex)
- Версия прошивки поднята до `2.0.7` как отдельный шаг миграции safety action endpoints на более явный v2 response contract. (codex)

## [2.0.6] - 2026-03-15

### Изменено
- `alarm` JSON в `webserver.cpp` и `cloud_tunnel.cpp` расширен полями `resetAvailable` и `resetBlockedReason`, чтобы UI и внешние клиенты могли понимать, можно ли уже выполнить `safety reset` без повторного угадывания локально. (codex)
- `v2.safety` в `/api/status` теперь тоже публикует доступность сброса аварии и причину блокировки reset, что связывает новый `recovery`-статус с практическим действием оператора. (codex)
- Frontend runtime приведён к единому формату safety alarm: `state.js` теперь нормализует `alarm/currentAlarm`, `globals.js` хранит их в общем runtime state, а `safety-modal.js` показывает, восстановились ли условия безопасности или почему reset ещё недоступен. (codex)
- Версия прошивки поднята до `2.0.6` как отдельный шаг интеграции recovery/reset readiness в API и UI слой. (codex)

## [2.0.5] - 2026-03-15

### Добавлено
- Добавлен отдельный read-only `SafetySupervisorV2` в `src/control/v2/safety_supervisor.*`, который стал единой точкой расчёта `activeLimits` и `SafetyDecisionV2` для v2 runtime/export слоя. (codex)
- В `Safety` открыт helper `canResetNow(...)`, использующий ту же внутреннюю проверку, что и реальный `reset`, чтобы recovery-статус в v2 опирался на ту же safety-логику, а не на дублированные эвристики. (codex)

### Изменено
- `status_adapter` больше не держит локальную safety-эвристику: расчёт `severity`, `reasonCode`, `message`, `requiresAcknowledge` и `activeLimits` делегирован в `SafetySupervisorV2`. (codex)
- `v2` теперь различает `latched_trip` и `recovery`: когда авария уже может быть снята, `metrics.safety.severity` переходит в `recovery`, а `indicators.recoveryActive` поднимается в `true`. (codex)
- Версия прошивки поднята до `2.0.5` как отдельный шаг миграции к единому v2 safety supervisor слою. (codex)

## [2.0.4] - 2026-03-15

### Изменено
- `MANUAL_RECT` переведён на explicit `ReasonCodeV2` для ручных фазовых переключений и пользовательской остановки: `manual_rect_handler.cpp` теперь сам отправляет `notePhaseTransition(...)` с понятными причинами и operator message вместо одной общей эвристики в адаптере. (codex)
- `FSM::startMode()` и `FSM::stopMode()` скорректированы так, чтобы старт ручной ректификации и её ручная остановка попадали в v2 transition log как явные события, а не только как постфактум-угаданный `mode change`. (codex)
- Fallback-логика `status_adapter` для `MANUAL_RECT` уточнена: если explicit transition по какой-то причине не пришёл, адаптер теперь различает старт режима и операторскую остановку вместо одного универсального `RC_MANUAL_OPERATOR_SWITCH`. (codex)
- Версия прошивки поднята до `2.0.4` как отдельный шаг миграции ручной ректификации на явные v2 transition contracts. (codex)

## [2.0.3] - 2026-03-15

### Добавлено
- `SafetyPolicyV2` расширен общим policy-блоком для `MANUAL_RECT`: вынесены критерий захлёба, расчёт критического порога, cooldown между step-down событиями и расчёт новой мощности ТЭНа в `src/control/v2/safety_policy.*`. (codex)

### Изменено
- `manual_rect_handler.cpp` больше не содержит собственную anti-flood формулу: handler теперь получает готовое policy-решение из `SafetyPolicyV2`, применяет его и сохраняет прежнее поведение по уведомлению и пошаговому снижению мощности. (codex)
- `ProcessIndicatorsV2` и `status_adapter` подключены к той же manual rect policy, поэтому `powerLimited` и `activeLimits.maxHeaterPowerPercent` для `MANUAL_RECT` теперь рассчитываются по той же логике, что и реальное anti-flood ограничение в runtime. (codex)
- Версия прошивки поднята до `2.0.3` как отдельный шаг миграции, который переносит manual rect anti-flood / derating из mode handler в общий v2 policy слой. (codex)

## [2.0.2] - 2026-03-15

### Добавлено
- Добавлен общий v2 helper `SafetyPolicyV2` в `src/control/v2/safety_policy.*`, который централизует формулу NBK pressure-derating и делает её переиспользуемой для handler/runtime/status слоя. (codex)

### Изменено
- `NBK` больше не держит собственную формулу снижения мощности в `nbk_handler.cpp`: теперь handler запрашивает решение у общего policy helper и применяет уже готовый `appliedPowerPercent`. (codex)
- `ProcessIndicatorsV2` и `status_adapter` переведены на ту же общую политику, чтобы флаг `powerLimited`, `activeLimits.maxHeaterPowerPercent` и реальное ограничение мощности считались по одной и той же логике. (codex)
- Ветка v2 продолжена до версии `2.0.2` как отдельный шаг миграции безопасности из mode handlers в общий policy/runtime слой. (codex)

## [2.0.1] - 2026-03-15

### Добавлено
- В read-only v2 runtime добавлен explicit transition bridge `ControlV2::notePhaseTransition(...)`, чтобы handlers могли передавать точные причины фазовых переходов без угадывания их в адаптере постфактум. (codex)

### Изменено
- `RECTIFICATION` переведён на явные `ReasonCodeV2` в точках переходов `HEATING -> STABILIZATION`, `STABILIZATION -> HEADS`, `HEADS -> POST_HEADS_STABILIZATION`, `POST_HEADS_STABILIZATION -> PURGE`, `PURGE -> BODY`, `BODY -> TAILS`, `TAILS -> FINISH` и `FINISH -> IDLE`. (codex)
- `NBK` переведён на явные `ReasonCodeV2` для переходов `HEATING -> STABILIZATION`, `STABILIZATION -> WORKING` и `FINISH -> COMPLETED`, чтобы v2 status/logging больше не зависели от одной общей эвристики. (codex)
- `status_adapter` обновлён так, чтобы приоритетно использовать explicit phase transition reason codes, корректно логировать финальные переходы при выходе режима в `IDLE` и только затем падать обратно на старую эвристику как fallback. (codex)

## [2.0.0] - 2026-03-15

### Добавлено
- Добавлен изолированный v2 groundwork: документы миграции `docs/v2/*` и новый каркас `src/control/v2/*` для `reason codes`, `mode contracts`, `process indicators` и `transition logger`. (codex)
- Введён read-only runtime adapter v2, который собирает `ProcessIndicatorsV2`, `MetricsSnapshotV2` и `ModeStatusV2` поверх текущего `SystemState` без изменения поведения режимов. (codex)

### Изменено
- `/api/status` расширен новым блоком `v2` с lifecycle, phase token, active limits, command targets, safety state и process indicators для безопасного старта миграции на архитектуру 2.x. (codex)
- Версия прошивки переведена на ветку `2.0.0`, чтобы дальше все v2-изменения шли уже в новой мажорной линии. (codex)

## [1.13.11] - 2026-03-14

### Исправлено
- Исправлена проблема калибровки насоса ("перестало идти время при калибровке и всегда молотит на полных оборотах"). Устранено состояние гонки и двойного запроса к API, вызывавшее сброс UI; теперь скорость отбора передается напрямую в `/api/pump/calibrate/start` в параметре `speed`. (claude)

---

## [1.13.10] - 2026-03-14

### Исправлено
- Исправлено отсутствие окна ввода фактического объёма при автоматическом завершении калибровки насоса. (gemini)
- Исправлена ошибка "Calibration already active" при попытке повторного запуска калибровки после отмены (добавлен эндпоинт `/api/pump/calibrate/cancel`). (gemini)
- Улучшен сброс интерфейса калибровки (очистка сообщений и скрытие полей). (gemini)

## [1.13.9] - 2026-03-14
...

---

## [1.13.8] - 2026-03-14


### Добавлено
- Управление насосом вынесено в отдельную высокоприоритетную задачу FreeRTOS (Core 1, Priority: Max-1) для устранения микро-джиттера и подергиваний (gemini).
- Реализована полноценная поддержка калибровки насоса при отсутствии датчиков температуры (gemini).

### Исправлено
- Исправлено "подергивание" насоса, вызванное блокирующими операциями в основном цикле `loop()` (gemini).
- Исправлен конфликт DemoSimulator с реальным драйвером насоса: теперь симулятор управляет физическим устройством через `setSpeed` (gemini).
- Улучшена отзывчивость UI при работе насоса за счет разгрузки основного цикла (gemini).

---

## [1.13.7] - 2026-03-14

### Исправлено
- Исправлена критическая ошибка компиляции в `src/types.h` (синтаксическая ошибка инициализации массива `healthScores`) (gemini).
- Исправлена ошибка линковки (undefined reference) для статических членов `SystemHealth` (gemini).
- Исправлено отсутствие объявлений `extern` для глобальных объектов в модулях Telegram и WebServer (gemini).
- Устранено дублирование логики самодиагностики в `src/main.cpp` (gemini).

### Добавлено
- Реализована полноценная взвешенная матрица здоровья (Health Matrix) с 6 подсистемами: SENSORS (40%), SAFETY (20%), POWER (20%), WIFI (10%), STORAGE (5%), OTA (5%) (gemini).
- Внедрена система `RebootTracker` для глубокого анализа причин перезагрузки ESP32-S3 (WDT, Brownout, Exception, SW Reset) (gemini).
- Расширено Telegram-меню: добавлена кнопка "📊 ДЕТАЛИ" и команда `/diag` для получения подробного отчета по каждой подсистеме и ошибкам датчиков (gemini).
- Добавлен REST API эндпоинт `/api/reboot/status` для интеграции со сторонними сервисами мониторинга (gemini).
- Информация о причине перезагрузки интегрирована в API `/api/health` для отображения в Web UI (gemini).

---

## [1.13.6] - 2026-03-15

### Исправлено
- **CRITICAL-FIX** (`watt_control.cpp`, `watt_control.h`): Устранена причина периодических перезагрузок (WDT Reset) при использовании симисторного регулятора. Логика управления мощностью, включая расчеты и логирование, вынесена из прерывания (ISR) в выделенную FreeRTOS задачу с высоким приоритетом. Это предотвращает блокировку системы и обеспечивает стабильную работу. (gemini)
- **Архитектура**: Реализован инкапсулированный класс `WattControl` для управления симистором, следующий паттерну "ISR -> FreeRTOS Task", что соответствует лучшим практикам для real-time систем. (gemini)

### Добавлено
- **Health Matrix**: Расширен `SystemHealth` структурой с взвешенным скорингом по подсистемам (датчики, память, связь, питание, температура, стабильность) для более точной оценки состояния системы (gemini)
- **Unified Self-Check**: Объединена дублирующаяся логика самоконтроля в функцию `performSystemHealthCheck()` с расширенным событированным логированием и селективными уведомлениями в Telegram (gemini)

---

## [1.13.5] - 2026-03-14

### Добавлено
- Интегрирована поддержка **симисторного регулятора мощности** (Phase Control) с детектором перехода через ноль (Zero-Cross). (gemini)
- В `config.h` добавлен параметр `HEATER_MODE_TRIAC` для аппаратного фазового управления, использующий прерывания `gptimer` на ESP32-S3. (gemini)
- В `watt_control.cpp` реализован аппаратный расчет угла отсечки (задержки включения симистора) на основе требуемой мощности и текущего Vrms напряжения сети для компенсации просадок (закон Джоуля-Ленца). (gemini)

## [1.13.4] - 2026-03-14

### Добавлено
- Добавлено детальное логирование смены фаз в режиме ручной ректификации в системный лог событий. (gemini)

### Исправлено
- Исправлена логика работы Demo Mode: теперь при включении симуляции аварии отсутствующих физических датчиков сбрасываются автоматически, позволяя FSM запускать процессы. (gemini)

## [1.13.2] - 2026-03-14

### Добавлено
- Реализовано интерактивное модальное окно безопасности в Web UI. (gemini)
- При возникновении Soft Failure (отказ некритичных датчиков) пользователь теперь может выбрать: остановить процесс или продолжить работу, приняв риски. (gemini)
- Добавлена интеграция нового модуля `safety-modal.js` в основной цикл обновления интерфейса. (gemini)

## [1.13.1] - 2026-03-14

### Добавлено
- Реализована система периодического Self-Check: раз в 30 минут выполняется диагностика памяти, аптайма и стабильности датчиков. (gemini)
- Внедрена взвешенная оценка здоровья (Weighted Health Score): критические датчики (Куб, Царга) теперь имеют приоритет 80% при расчете статуса. (gemini)
- Реализован детальный учет CRC-ошибок для каждого из 7 температурных датчиков индивидуально. (gemini)
- Реализована поддержка интерактивных кнопок (Inline Keyboard) в Telegram боте для управления процессом (/status). (gemini)

### Изменено
- Умная логика безопасности: отказ вспомогательных датчиков (ТСА, Охлаждение, Давление) больше не вызывает немедленную остановку процесса, а требует подтверждения пользователя в UI (Soft Failure). (gemini)

## [1.13.0] - 2026-03-14

### Добавлено
- **#4** (`fsm.cpp`): `getPhaseProgressPercent()` и `getPhaseTargetSec()` теперь возвращают реальный прогресс для всех режимов — Mashing (по выдержке шага), Hold (по длительности шага), NBK (по темп./времени/объёму), Fermentation (по времени). Ранее возвращали 0.
- **#4** (`types.h`): В `NbkSettings` добавлено поле `targetVolumeMl` (0=неизвестно), в `FermentationSettings` — `durationHours` (0=бессрочно). Через Web UI эти параметры можно задать для отображения прогресса.
- **#14** (`main.cpp`): Self-check лог каждые 30 минут — записывает свободный heap, uptime, причину перезагрузки и счётчики ошибок датчиков в `Logger::logf()` и Serial.

### Без изменений (подтверждено)
- **#7** `buzzerTask`: WDT не требуется — задача спит на `portMAX_DELAY` (правильный паттерн FreeRTOS).
- **#12** `/health` команда Telegram: уже реализована ранее.
- **#9** Взвешенный Health Score: уже реализован в `sensors.cpp::updateHealth()`.

## [1.12.0] - 2026-03-14

### Исправлено
- **BUG-1** (`sensors.cpp`, `types.h`): `SystemHealth::tempSensorsOk` исправлен с `bool` на `uint8_t` — Health Score теперь правильно считает количество рабочих датчиков, а не просто `true/false`.
- **BUG-2** (`heater.cpp`): Плавный разгон ТЭНа (ramp) теперь линейный — добавлена переменная `rampStartPower` для корректной интерполяции. Ранее `currentPower` перезаписывался на каждом шаге, давая экспоненциальную кривую.
- **BUG-3** (`webserver.cpp`): Удалён дупликат `return "finish"` в `getMashPhaseString()` (dead code).
- **BUG-4** (`sensors.cpp`): Исправлена race condition при чтении `flowPulseCount` из ISR — счётчик теперь атомарно копируется за одну критическую секцию (`noInterrupts`), а не читается 4 раза с промежуточными изменениями от прерываний.
- **BUG-5** (`pump.cpp`): `totalSteps` изменён с `uint32_t` на `int32_t` — устраняло потенциальное переполнение объёма при неявном преобразовании signed → unsigned.
- **ARCH-2** (`cloud_tunnel.cpp`): В `getModeToken()` добавлены case'ы для `Mode::NBK` и `Mode::FERMENTATION` — ранее они возвращали `"unknown"` из облачного тоннеля.
- **ARCH-3** (`valves.cpp`, `valves.h`): Убраны блокирующие `delay(15)×N + delay(2000)` в `Valves::setFraction()` и `initFractionator()`. Плавное движение сервопривода переведено на неблокирующий автомат через `millis()` с вызовом `Valves::update()` из loop.
- **PERF-3** (`main.cpp`): `Heater::update()` и `Valves::update()` теперь вызываются в основном loop — ранее функция плавного разгона ТЭНа (`rampTo`) была реализована, но никогда не вызывалась.

## [1.11.23] - 2026-03-14

### Добавлено
- Реализована поддержка профилей плат через `BOARD_REV` и новый файл `src/pins_config.h`. (gemini)
- Добавлен новый экран TFT "Все температуры" для мониторинга всех 7 датчиков одновременно. (gemini)
- Реализован выбор профиля обновления дисплея (Safe/Normal/Fast). (gemini)
- Добавлена автоматическая ротация логов (лимит 10 файлов). (gemini)

## [1.11.20] - 2026-03-14

### Добавлено
- Реализован неблокирующий зуммер через FreeRTOS задачу. (gemini)
- Добавлена поддержка `secrets.h` для хранения Wi‑Fi паролей. (gemini)

## [1.11.18] - 2026-03-13

### Изменено
- Выполнена полная миграция на **ArduinoJson 7**. (gemini)

## [1.11.15] - 2026-03-13

### Добавлено
- Реализована модульная архитектура FSM (вынос режимов в `src/control/modes/`). (gemini)
- Добавлена система **Reboot Reason Tracking**. (gemini)

[1.13.1]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.13.1
[1.13.0]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.13.0
[1.12.0]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.12.0
[1.11.23]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.11.23
[1.11.20]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.11.20
[1.11.18]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.11.18
[1.11.15]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.11.15
