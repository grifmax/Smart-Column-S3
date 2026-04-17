# Журнал изменений

Все значимые изменения в этом проекте будут документированы в этом файле.

Формат основан на [Keep a Changelog](https://keepachangelog.com/ru/1.0.0/),
и этот проект придерживается [Semantic Versioning](https://semver.org/lang/ru/).

---

---

## [2.2.8] - 2026-04-17

### Изменено
- Для встроенного TFT добавлен общий UTF-8-safe text-fit слой: длинные строки теперь аккуратно ужимаются/подрезаются в `header`, `buttons`, `tabs`, `value rows`, `panel headers`, `value tiles`, `badges`, `footer hints` и root footer вместо налезания друг на друга. (codex)
- Основные сервисные и режимные подписи на IPS теперь лучше переносят длинные русские названия и статусы без ручного подбора каждого текста по месту. (codex)
- Версия прошивки поднята до `2.2.8`; frontend bundle будет пересобран и version-stamps синхронизированы с новым релизом. (codex)

## [2.2.7] - 2026-04-17

### Изменено
- На TFT добит modal/runtime слой: подтверждение смены режима переведено на общий panel-overlay, а locked-state ручного режима больше не рисуется старым кастомным блоком. (codex)
- Экран `SERVICE` теперь явнее подсвечивает проблемы рендера TFT: при slow/watchdog/hard recovery нижний статус меняет тон и говорит не «что-то не так», а что именно смотреть. (codex)
- Версия прошивки поднята до `2.2.7`; frontend bundle будет пересобран и version-stamps синхронизированы с новым релизом. (codex)

## [2.2.6] - 2026-04-17

### Изменено
- Вторичные TFT-экраны переведены на тот же HMI-язык: touch-calibration получил панельную компоновку с прямоугольной мишенью, boot splash больше не выглядит как сырой debug-экран, а `showMessage`/`showError` используют общий fullscreen-overlay вместо старых чёрных заливок. (codex)
- Системные подсказки и короткие сервисные сообщения на встроенном IPS теперь рисуются через общий overlay/helper-слой, чтобы аварийные и служебные экраны не выпадали из нового прямоугольного стиля. (codex)
- Версия прошивки поднята до `2.2.6`; frontend bundle будет пересобран и version-stamps синхронизированы с новым релизом. (codex)

## [2.2.5] - 2026-04-17

### Изменено
- Вложенные TFT-экраны `SETTINGS` и `SERVICE` переведены на тот же прямоугольный HMI-язык: строковые панели параметров, компактные статусные подсказки, новый переключатель страницы ректификации и более строгий экран редактирования значения. (codex)
- Экран `ALL TEMPS` ужат под реальную высоту `480x320` с нижней навигацией, чтобы температурная сетка перестала залезать на footer встроенного IPS. (codex)
- Версия прошивки поднята до `2.2.5`; frontend bundle будет пересобран и version-stamps синхронизированы с новым релизом. (codex)

## [2.2.4] - 2026-04-17

### Изменено
- Базовый TFT-стиль переписан в более жёсткий HMI-язык: обновлены палитра, фоновые панели, табы, header, карточки, value-tiles и общие прямоугольные кнопки, чтобы экран перестал выглядеть как уменьшенная web-страница. (codex)
- Экран управления `CONTROL` и корневой экран `SETTINGS` теперь используют более дисциплинированную цветовую кодировку режимов и секций вместо старого набора случайных оттенков. (codex)
- Общие примитивы встроенного IPS теперь строятся вокруг нейтрального тела кнопки/панели и цветовых полос состояния, чтобы статус режима читался как промышленный пульт, а не как декоративные карточки. (codex)
- Версия прошивки поднята до `2.2.4`; frontend bundle будет пересобран и version-stamps синхронизированы с новым релизом. (codex)

## [2.2.3] - 2026-04-17

### Изменено
- Продолжен HMI-редизайн встроенного IPS UI для оставшихся custom-режимов: `MANUAL_RECT`, `MASHING` и `HOLD` переведены на тот же строгий root-monitor каркас с прямоугольной summary-панелью и общей status/footer схемой. (codex)
- На TFT для ручной ректификации добавлена отдельная операторская сводка по скорости, мощности и фракциям, а живая телеметрия вынесена в правую сетку температур, давления и подачи. (codex)
- Для затирки и пастеризации старый полноширинный step-list переработан в HMI-компоновку с левой панелью текущего шага, цели и таймера и правым списком ступеней с прогрессом. (codex)
- Версия прошивки поднята до `2.2.3`; frontend bundle будет пересобран и version-stamps синхронизированы с новым релизом. (codex)

## [2.2.2] - 2026-04-17

### Изменено
- Продолжен HMI-редизайн встроенного IPS UI для `custom monitor`: режимы `DISTILLATION`, `NBK` и `FERMENTATION` переведены на тот же операторский шаблон с левой summary-панелью и правой 2x3 сеткой ключевых параметров. (codex)
- Общий root-monitor каркас для нестандартных режимов теперь использует единые status/footer панели, чтобы `dashboard`, `rect monitor` и остальные runtime-экраны читались как одна HMI-система. (codex)
- Для дистилляции, НБК и ферментации на TFT добавлены отдельные summary-блоки по целям, объему, давлению, нагреву и времени работы, без возврата к старому карточному layout. (codex)
- Версия прошивки поднята до `2.2.2`; frontend bundle будет пересобран и version-stamps синхронизированы с новым релизом. (codex)

## [2.2.1] - 2026-04-17

### Изменено
- Продолжен HMI-редизайн встроенного IPS UI: `dashboard` и основной `mode monitor` для ректификации переведены с широких карточек на более плотную операторскую компоновку с левой summary-панелью и правой 2x3 сеткой ключевых параметров. (codex)
- На главном экране IPS добавлена отдельная summary-зона состояния процесса, где отдельно показываются режим, фракции, давление/охлаждение и safety-state без потери места под основные метрики. (codex)
- Экран активной ректификации на TFT теперь использует тот же прямоугольный HMI-каркас, что и главный экран: короткий stage-header, компактные строки по головам/телу/хвостам и более плотную правую сетку температур и мощности. (codex)
- Версия прошивки поднята до `2.2.1`; frontend bundle будет пересобран и version-stamps синхронизированы с новым релизом. (codex)

## [2.2.0] - 2026-04-16

### Изменено
- Встроенный IPS UI переработан в первый HMI-срез: базовые панели, кнопки, value-tiles, progress bar и статусные бейджи переведены на строгий прямоугольный стиль без скруглений и декоративных карточек. (codex)
- Экран выбора режимов на TFT теперь учитывает весь фактический набор режимов проекта: ректификация, дистилляция, ручной режим, затирка, пастеризация, НБК и ферментация; отдельные action-кнопки паузы и стопа вынесены в верхнюю строку. (codex)
- Для встроенного экрана добавлены отдельные monitor-layouts для `NBK` и `FERMENTATION`, а phase/status строка теперь использует корректные имена фаз для всех режимов, а не только `rectPhase`. (codex)
- Ручное управление на IPS теперь визуально и функционально блокируется вне `IDLE`/`MANUAL_RECT`, чтобы не было обхода текущей policy через локальный тач-интерфейс. (codex)
- Версия прошивки поднята до `2.2.0`; frontend bundle будет пересобран и version-stamps синхронизированы с новым релизом. (codex)

## [2.1.21] - 2026-04-16

### Изменено
- Для `POST /api/stirrer/start`, `POST /api/stirrer/set` и `POST /api/stirrer/stop` зафиксирована единая policy ручного управления: команды доступны только в `IDLE`, а при активном или поставленном на паузу процессе backend возвращает `409` с явной причиной. (codex)
- Главный виджет мешалки теперь заранее показывает ownership FSM в активных режимах, отключает ручные кнопки и выводит причину блокировки без лишнего запроса к API. (codex)
- Добавлен UI smoke-сценарий, который проверяет блокировку ручного управления мешалкой во время активного процесса. (codex)
- Версия прошивки поднята до `2.1.21`; frontend bundle пересобран, `data/version.json` и HTML-asset stamps синхронизированы с новым релизом. (codex)

## [2.1.20] - 2026-04-15

### Добавлено
- На главной странице добавлен live-виджет мешалки куба: текущая скорость, режим работы, статус MCP4725 и ручные команды `start/set/stop` через существующий REST API. (codex)
- Во вкладке `Оборудование -> Параметры` добавлена отдельная карточка настроек мешалки с флагом `enabled`, скоростью по умолчанию и auto-start для затирки, НБК и ферментации. (codex)
- В `Оборудование -> Тестирование` добавлен сервисный сценарий мешалки с ручным запуском, сменой скорости и отдельным backend endpoint `POST /api/testing/stirrer`. (codex)
- Добавлены UI smoke-сценарии для мешалки: покрыт главный виджет, сохранение настроек и сервисный тест мешалки в workspace оборудования. (codex)

### Изменено
- `GET /api/testing/status` и общий `POST /api/testing/stop-all` теперь учитывают мешалку в активных сервисных тестах и live-статусе оборудования. (codex)
- Версия прошивки поднята до `2.1.20`; frontend bundle пересобран, `data/version.json` и HTML-asset stamps синхронизированы с новым релизом. (codex)

## [2.1.19] - 2026-04-15

### Добавлено
- В `src/interface/webserver.cpp` реализованы REST endpoints мешалки: `POST /api/stirrer/start`, `POST /api/stirrer/stop`, `POST /api/stirrer/set`, а также `GET/POST /api/settings/stirrer` для чтения и сохранения конфигурации. (codex)
- Ответы `GET /api/status` и WebSocket broadcast теперь включают актуальный блок `stirrer`, синхронизированный прямо перед сериализацией ответа, чтобы ручные команды не ждали следующего тика `loop()`. (codex)

### Изменено
- В `src/storage/nvs_manager.cpp` завершена интеграция настроек мешалки с NVS: `enabled`, `defaultSpeedPercent`, `autoMashing`, `autoFermentation`, `autoNbk` теперь загружаются и сохраняются вместе с остальными настройками. (codex)
- Ручные API-команды мешалки теперь переводят её в manual override, сбрасывая `autoMode`, а отключение мешалки в настройках сразу вызывает `Stirrer::stop()` для предсказуемого состояния выхода. (codex)
- Версия прошивки поднята до `2.1.19`, пересобран frontend bundle и обновлены version-stamps в `data/version.json` и статических HTML-ассетах. (codex)

## [2.1.18] - 2026-04-14

### Добавлено
- Реализован драйвер управления мешалкой куба через аналоговый сигнал 0-10В на базе **MCP4725** (12-бит DAC, I2C) и операционного усилителя **MCP6001** (усиление ×3, 0–3.3В → 0–10В). (gemini)
- Добавлены структуры `StirrerState` и `StirrerSettings` в `types.h` для хранения состояния и настроек мешалки. (gemini)
- Новый драйвер `src/drivers/stirrer.h/.cpp`: инициализация MCP4725 по I2C, установка скорости (0–100%), старт/стоп, синхронизация с `g_state.stirrer`. (gemini)
- Добавлена библиотека `adafruit/Adafruit MCP4725@^2.0.2` в зависимости `platformio.ini`. (gemini)
- Добавлены NVS-ключи для настроек мешалки (`stir_en`, `stir_spd`, `stir_amash`, `stir_aferm`, `stir_anbk`) в `config.h`. (gemini)
- Добавлена константа `I2C_ADDR_MCP4725 0x60` в `config.h`. (gemini)
- Авто-запуск мешалки в FSM (`fsm.cpp`) при активации режимов: Затирание (`autoMashing`), Ферментация (`autoFermentation`), НБК (`autoNbk`). (gemini)
- Мешалка останавливается через `Stirrer::stop()` при выходе из любого режима (`finalizeModeStop`) и при срабатывании аварийной защиты (`forceSafeOutputs` в `safety.cpp`). (gemini)
- Телеметрия мешалки (`running`, `speed`, `available`, `autoMode`) добавлена в WebSocket broadcastState. (gemini)

## [2.1.17] - 2026-03-18

### Изменено
- Во вкладке `Настройки` локальная навигация окончательно переведена в настоящий левый sidebar на desktop: кнопки разделов теперь занимают всю ширину боковой колонки и больше не выглядят как верхняя лента. (codex)
- Во вкладке `Оборудование -> Тестирование` правая колонка теперь действительно показывает только выбранный сервисный узел, а общий статус тестирования и журнал последних действий перенесены в левую боковую колонку на desktop, чтобы не раздувать рабочую область длинной простынёй. (codex)
- Smoke-сценарий `equipment-testing` обновлён под новую механику бокового меню и теперь проверяет переключение между насосом, сервоприводом и клапанами через sidebar, а не через старую полную ленту карточек. (codex)

## [2.1.16] - 2026-03-17

### Изменено
- Во вкладке `Оборудование` переключение верхних подпунктов (`Параметры / Калибровка / Тестирование`) теперь жёстко активирует только один связанный рабочий блок, вместо длинной простыни карточек; при смене подпункта сразу выбирается первый релевантный узел внутри этого раздела. (codex)
- Во вкладке `Настройки` внедрён тот же workbench-паттерн, что уже используется в `Оборудовании`: появился локальный submenu по разделам `Подключение / Интеграции / Доступ / Интерфейс / Система`, а внутри каждого раздела на desktop открыт только один компактный блок с боковой навигацией, вместо большой ленты всех карточек сразу. (codex)
- Навигация `Настроек` и `Оборудования` унифицирована на одном UI-каркасе, поэтому поведение desktop/mobile теперь ближе к `Инструментам`: на узких экранах блоки работают как аккордеоны, а на широких — как service workbench с компактным меню. (codex)

## [2.1.15] - 2026-03-17

### Изменено
- Исправлен пакет регрессий в мобильной навигации и экранах `Графики/Логи`: заголовок `Главная` на узких экранах скрывается, а для `charts.html` и `logs.html` добавлен локальный fallback `toggleTopMenu`, чтобы кнопка гамбургера снова стабильно открывала меню. (codex)
- Во фронтенде режимов выполнен UI cleanup: кнопки выбора режимов приведены к стандартному стилю проекта без цветных названий и emoji, `Hold` переименован в `Пастеризацию`, в истории и progress-баре тоже заменены старые подписи, а для шагов пастеризации добавлены пауза без температуры и чекбокс охлаждения с поддержкой backend-логики. (codex)
- Из ручной ректификации убраны ручные клапаны, блок скорости насоса перенесён в контекст отбора тела, а из дистилляции удалены стартовые поля скорости отбора, голов и целевого объёма; HTML и JS очищены так, чтобы эти элементы больше не всплывали даже до инициализации скриптов. (codex)
- `Оборудование -> Тестирование` уплотнено и структурно доведено: журнал последних действий оператора вынесен вниз в accordion, desktop-sidebar больше не смешивается со статусным блоком, а локальные стили сервиса лучше держат компактную компоновку. (codex)
- Во вкладках `Настройки`, `WiFi` и `Безопасность` уменьшены oversized checkbox и лишние вертикальные разрывы; дополнительно исправлено сохранение `Home Assistant Discovery` через `/api/settings/mqtt` и NVS, чтобы чекбокс больше не возвращался сам после reload/save. (codex)
- Во вкладке `История процессов` фильтры приведены к типичному стилю проекта, статус-фильтр снова участвует в отборе, а битые подписи `НБК/Ферментация/Пастеризация` заменены на нормальные UTF-8 значения и в HTML, и в runtime-инициализации. (codex)

## [2.1.14] - 2026-03-17

### Изменено
- В мобильном верхнем меню сокращён пункт `Главная`: для `index/charts/logs` он теперь отображается как компактная кнопка `📊`, чтобы навигация не ломала ряд и остальные разделы помещались на узких экранах. (codex)

## [2.1.13] - 2026-03-17

### Изменено
- Сервисные действия из `Оборудование -> Тестирование` теперь идут не только в локальный live-журнал, но и в общий event-log, а при активной записи процесса дополнительно сохраняются в `history.results.warnings` как операторские сервисные события с reason code `RC_OPERATOR_SERVICE_ACTION`. (codex)
- Общий журнал событий научился различать `equipment_test`-события и отдавать их структурно через `/api/logs/events` и CSV-экспорт, а фронтенд [src/web/core/logs.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\core\logs.js) теперь показывает их как отдельные сервисные записи вместо сырого технического текста. (codex)
- В тексты сервисного экрана внесён UI-polish: в [src/web/settings/equipment-testing.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\settings\equipment-testing.js) укорочены описания карточек, `interlock` заменён на `блокировки`, статусы `SIMULATED/RUNNING/IDLE/ON/OFF` переведены на нормальные русские подписи, а журнал последних действий стал показывать человекочитаемые метки времени. (codex)
- В деталях истории [src/web/history/details.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\history\details.js) добавлена человекочитаемая подпись для `RC_OPERATOR_SERVICE_ACTION`, а `Safety timeline` переведён на русский как `Хронология безопасности`. (codex)

## [2.1.12] - 2026-03-17

### Изменено
- В проекте выполнен cleanup битой кодировки: восстановлен нормальный UTF-8 в [CHANGELOG.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\CHANGELOG.md), [GEMINI.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\GEMINI.md), [SPEC.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\SPEC.md), [TODO2.0.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\TODO2.0.md), [docs/HOME_ASSISTANT.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\docs\HOME_ASSISTANT.md), [analysis_smart_column_s3.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\analysis_smart_column_s3.md), [plan_analysis.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\plan_analysis.md), [src/drivers/display.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\drivers\display.cpp) и [cloud_proxy/web/app.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\cloud_proxy\web\app.js). (codex)
- Дополнительно вручную исправлены остаточные поломки единиц измерения температуры и давления, подписей `Порт`/`HTTP коды` и стрелок переходов в спецификациях, чтобы в репозитории не оставалось полубитых фрагментов после автоматического восстановления. (codex)

## [2.1.11] - 2026-03-17

### Изменено
- Для вкладки `Настройки` добавлен отдельный компактный слой стилей в [src/web/styles/_settings.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_settings.css), подключённый через [src/web/styles/main.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\main.css): карточки, form-group и action-строки теперь плотнее и визуально ближе к `Инструментам` и `Оборудованию`. (codex)
- Кнопки внутри `Настроек` больше не растягиваются без необходимости на всю ширину карточек: локальные переопределения задают им рабочий диапазон ширины на desktop и аккуратную сетку на mobile, даже если старый HTML содержал inline `width: 100%` или `flex: 1`. (codex)
- Информационные блоки во вкладке `Настройки` тоже уплотнены: уменьшены лишние `margin/padding` у вложенных секций со статусами и версиями, поэтому экран воспринимается как компактный сервисный dashboard, а не как длинная анкета. (codex)

## [2.1.10] - 2026-03-17

### Изменено
- В [src/web/styles/_equipment.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_equipment.css) раздел `Оборудование` переведён на более плотный сервисный ритм: ужаты отступы между блоками, заголовками и action-рядом, чтобы экран больше не выглядел как растянутая форма с пустым воздухом. (codex)
- Карточки `Параметров` теперь ограничивают ширину одиночных input/stepper-контролов и больше не растягивают один показатель на всю строку: числовые поля, inline-строки и кнопки `Сохранить/Добавить` получили фиксированные рабочие размеры и более собранную компоновку. (codex)
- Метрики и сервисные действия в `Тестировании` тоже уплотнены: [src/web/styles/_equipment.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_equipment.css) теперь не растягивает одиночные KPI-карточки и preset-кнопки на всю ширину контейнера, а mobile-кнопки стали ниже и компактнее. (codex)

## [2.1.9] - 2026-03-17

### Изменено
- В [src/web/styles/_equipment.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_equipment.css) исправлено отображение активного подпункта `Оборудования` в светлой теме: вместо невалидного градиента с `var(--primary)` теперь используется корректная связка `var(--accent) -> var(--accent-hover)`, поэтому выбранный пункт больше не превращается в белый текст на светлой кнопке. (codex)
- Backend [src/interface/webserver.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\interface\webserver.cpp) теперь хранит кольцевой буфер последних сервисных действий оператора из раздела `Оборудование -> Тестирование`: старт/стоп насоса, ТЭН, клапаны, импульсы, позиции сервопривода и общий `stop-all` попадают в `recentActions` внутри `/api/testing/status`. (codex)
- Во фронтенде [src/web/settings/equipment-testing.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\settings\equipment-testing.js) добавлен компактный живой журнал сервисных действий прямо в статусной карточке `Тестирования`, а smoke-сценарий [tools/ui-smoke/tests/equipment-testing.smoke.spec.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\tools\ui-smoke\tests\equipment-testing.smoke.spec.js) расширен проверкой этого журнала. (codex)

## [2.1.8] - 2026-03-17

### Изменено
- Раздел `Оборудование` доведён до единого компактного паттерна: `Параметры` и `Калибровка` теперь, как и `Тестирование`, открываются через workbench-слой с sidebar-навигацией на desktop и мобильными аккордеонами, без длинных растянутых полотен из карточек. (codex)
- Во фронтенде [src/web/settings/equipment-testing.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\settings\equipment-testing.js) `Параметры` рационально разбиты на группы `Насос и дозирование`, `Куб, ТЭН и колонна`, `Охлаждение и автостарт`, а `Калибровка` получила компактное переключение между насосом и термометрами с сохранением активной карточки. (codex)
- В стилях [src/web/styles/_equipment.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_equipment.css) добавлена общая плотная компоновка для всех внутренних карточек `Оборудования`: меньше разрывов, уже mobile-формы и одинаковое поведение action-рядов во всех трёх подразделах. (codex)
- Smoke-сценарий [tools/ui-smoke/tests/equipment-testing.smoke.spec.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\tools\ui-smoke\tests\equipment-testing.smoke.spec.js) расширен и теперь проверяет не только сервисные тесты, но и новую навигацию `Параметры/Калибровка`, чтобы единый workbench не деградировал незаметно. (codex)

## [2.1.7] - 2026-03-17

### Изменено
- Экран `Оборудование -> Тестирование` визуально перестроен под тот же паттерн, что и `Инструменты`: появился компактный sidebar-menu на desktop и mobile-аккордеоны с одной рабочей карточкой вместо длинной растянутой простыни из всех сервисных блоков сразу. (codex)
- Во фронтенде [src/web/settings/equipment-testing.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\settings\equipment-testing.js) добавлен workbench-слой для сервисных групп `Насос / Клапаны / Сервопривод / ТЭН / Термометры / Давление / Ареометр / Питание`, с сохранением активной карточки и единым меню навигации. (codex)
- В стилях [src/web/styles/_equipment.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_equipment.css) уменьшены разрывы, ужаты action-строки и мобильные кнопки, а карточки тестирования переведены на более плотную компоновку без лишнего вертикального растягивания экрана. (codex)

## [2.1.6] - 2026-03-17

### Изменено
- В сервисном экране `Оборудование -> Тестирование` для клапанов добавлены импульсные тесты с настраиваемой длительностью: оператор может давать короткий щелчок на `Воду`, `Головы` и `УНО`, не переводя клапан в постоянное открытое состояние. (codex)
- Драйвер [src/drivers/valves.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\drivers\valves.cpp) получил неблокирующий timer для импульсов клапанов, а backend [src/interface/webserver.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\interface\webserver.cpp) теперь экспортирует живой статус `waterPulse/headsPulse/unoPulse` и принимает `POST /api/testing/valves` с `action: "pulse"`. (codex)
- Во фронтенде [src/web/settings/equipment-testing.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\settings\equipment-testing.js) и [src/web/styles/_equipment.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_equipment.css) клапанные карточки получили поле длительности импульса, отдельные кнопки `Импульс` и живую подсказку с оставшимся временем, а smoke-сценарий `tools/ui-smoke/tests/equipment-testing.smoke.spec.js` теперь проверяет и этот сервисный путь. (codex)

## [2.1.5] - 2026-03-17

### Изменено
- Для нового workspace `Оборудование -> Тестирование` добавлен отдельный UI smoke-сценарий `tools/ui-smoke/tests/equipment-testing.smoke.spec.js`, который проверяет сервисную навигацию, загрузку `/api/testing/status`, запуск теста насоса, движение сервопривода, общий `stop-all` и переход в калибровку. (codex)
- Расширены `tools/ui-smoke/tests/helpers/smoke-helpers.js`: добавлены fixtures и трассировка для `/api/testing/*` и `/api/calibration`, чтобы новый сервисный экран стабильно тестировался в headless-среде без ручных моков в каждом сценарии. (codex)

## [2.1.4] - 2026-03-16

### Изменено
- В раздел `Оборудование` добавлена внутренняя сервисная навигация `Параметры / Калибровка / Тестирование`, а новый экран `Тестирование` собран как отдельная диагностическая рабочая зона с группами для насоса, клапанов, сервопривода фракционника, ТЭНа, термометров, датчика давления, ареометра-заглушки и силовой телеметрии. (codex)
- Добавлен backend API `GET /api/testing/status`, `POST /api/testing/stop-all`, `POST /api/testing/pump`, `POST /api/testing/heater`, `POST /api/testing/valves`, `POST /api/testing/servo`: он агрегирует live-статус тестов, interlock-ограничения, состояние помпы/клапанов/сервопривода/датчиков и выполняет сервисные действия только в безопасных условиях. (codex)
- Конфигурация фракционника теперь сохраняется в общих настройках через NVS: добавлены загрузка и сохранение `enabled`, углов и флагов активных позиций, а драйвер клапанов получил live API для текущего угла, флага движения и доступности сервопривода. (codex)
- Во фронтенде добавлен новый модуль `src/web/settings/equipment-testing.js` и расширены стили `src/web/styles/_equipment.css`: тестовые карты получили единую компоновку, сводку активных тестов, защитное модальное подтверждение запуска ТЭНа, быстрые pump-пресеты, ручное управление сервоприводом и встроенные переходы к калибровке. (codex)

## [2.1.3] - 2026-03-16

### Изменено
- В demo-режиме полностью отключено физическое управление исполнительными устройствами: драйверы насоса, ТЭНа и клапанов теперь не подают реальные команды на железо при `demoMode=true`, сохраняя только логическое состояние для интерфейса и симуляции. (codex)
- [src/main.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\main.cpp) больше не перетирает demo-состояние помпы данными физического драйвера, поэтому UI показывает смоделированную скорость без попыток реально крутить мотор. (codex)

## [2.1.2] - 2026-03-16

### Изменено
- В [src/control/demo_simulator.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\control\demo_simulator.cpp) демо-режим перестал дёргать насос на каждом цикле с рандомным `setSpeed()`: модель теперь обновляет целевую скорость реже и сглаживает её. (codex)
- Добавлена отдельная синхронизация демо-помпы после FSM через [src/main.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\main.cpp) и [src/control/demo_simulator.h](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\control\demo_simulator.h), чтобы в demo насос держал реальную смоделированную скорость, но не гудел волнами от частых команд. (codex)

## [2.1.1] - 2026-03-16

### Изменено
- Исправлен скрипт [scripts/build_web.py](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\scripts\build_web.py): из логов убраны Unicode-символы, из-за которых `uploadfs` падал в Windows-консоли и не доливал LittleFS на устройство. (codex)
- После фикса LittleFS перезалита на контроллер, так что Web UI на устройстве теперь соответствует версии без Telegram-настроек. (codex)

## [2.1.0] - 2026-03-16

### Изменено
- Из прошивки полностью удалён Telegram-модуль: вырезаны `src/interface/telegram.cpp`, `src/interface/telegram.h`, backend API `/api/settings/telegram*`, runtime вызовы из `src/main.cpp`, связанные поля настроек и NVS-ключи. (codex)
- Из Web UI и сборки удалены все Telegram-настройки и привязки: убраны `src/web/settings/telegram.js`, блок Telegram из `data/index.html`, импорты из `src/web/main.js` и `src/web/_main-init.js`, а также ссылки в `scripts/wire-modules.mjs` и `scripts/split-app.mjs`. (codex)
- Из проекта удалены зависимость `FastBot2`, vendored каталог `lib/FastBot2`, продуктовые упоминания в `README.md`, `SPEC.md`, `docs/API.md`, `docs/HOME_ASSISTANT.md`, `data/landing/index.html` и связанных рабочих markdown-файлах, чтобы Telegram больше не фигурировал ни как функция, ни как интеграция. (codex)
- Версия прошивки поднята до `2.1.0` как первый релиз после полного удаления Telegram из firmware, Web UI, документации и зависимостей проекта. (codex)

## [2.0.44] - 2026-03-15

### Изменено
- В `src/drivers/pump.cpp` развёрнуто направление вращения насоса: базовый уровень на `PIN_PUMP_DIR` и направление при `start()` теперь выставляются в противоположное состояние, чтобы мотор крутился в обратную сторону относительно прошивки `2.0.43`. (codex)
- Версия прошивки поднята до `2.0.44` как отдельный hotfix по направлению вращения насоса, чтобы это изменение было зафиксировано отдельно от предыдущего перевода STEP на аппаратный генератор. (codex)

## [2.0.43] - 2026-03-15

### Изменено
- В `src/drivers/pump.cpp` генерация STEP-импульсов для насоса переведена с софтового `AccelStepper::runSpeed()` на аппаратный PWM-генератор ESP32 (`LEDC`), чтобы частота шагов больше не зависела от джиттера задач и не вызывала волнообразное гудение и краткие затыки на больших скоростях. (codex)
- В `src/drivers/pump.cpp` pump-task превращён в лёгкий supervisory-слой: он теперь только плавно подводит частоту к целевой, синхронизирует счётчик шагов и объёма по времени и больше не отвечает за саму подачу STEP-пульсов. (codex)
- В `src/interface/webserver.cpp` сохранена расширенная диагностика `GET /api/pump/diag`, включая `appliedSpeedMlH`, чтобы на живом устройстве можно было сравнивать командную и фактическую аппаратно-подведённую скорость насоса. (codex)
- Версия прошивки поднята до `2.0.43` как отдельный hotfix по реальной проблеме рывков и кратких остановок насоса на высоких скоростях. (codex)

## [2.0.42] - 2026-03-15

### Изменено
- В `src/drivers/pump.cpp` убрана регулярная `vTaskDelay(1 ms)` пауза во время работы насоса: pump-task теперь больше не вносит предсказуемые 10-мс "дыры" в поток шагов, которые на высоких скоростях давали волнообразное гудение и подзатыки stepper-подачи. (codex)
- В `src/drivers/pump.cpp` добавлен мягкий ramp целевой скорости для режима `runSpeed()`, потому что `AccelStepper` на этом пути не использует acceleration сам по себе: насос теперь выходит на высокую скорость плавнее и без мгновенного скачка step-rate при `start/setSpeed`. (codex)
- В `src/drivers/pump.cpp` для STEP-сигнала задан `setMinPulseWidth(4)`, а в `src/interface/webserver.cpp` диагностика `GET /api/pump/diag` расширена полем `appliedSpeedMlH`, чтобы отдельно видеть целевую и фактически подведённую скорость насоса. (codex)
- Версия прошивки поднята до `2.0.42` как отдельный шаг live-fix по плавности и устойчивости насоса на высоких скоростях после ручной проверки на устройстве. (codex)

## [2.0.41] - 2026-03-15

### Изменено
- В `src/interface/webserver.cpp` локальный `GET /api/status` теперь принудительно обновляет `ControlV2` перед сериализацией ответа, чтобы `v2.lifecycle` и `v2.paused` не отставали от обычных полей `mode/paused` между тиками FSM. (codex)
- В `src/interface/webserver.cpp` и `src/interface/cloud_tunnel.cpp` `POST /api/process/pause` и `POST /api/process/resume` теперь сразу вызывают `ControlV2::updateRuntime(...)`, поэтому после операторской паузы или возобновления `v2`-снимок обновляется в той же API-транзакции, а не ждёт следующего цикла. (codex)
- В `src/interface/cloud_tunnel.cpp` `GET /api/status` выровнен с локальным сервером и тоже пересобирает свежий `v2` runtime прямо перед ответом, чтобы локальный и облачный status path не расходились по pause/resume семантике. (codex)
- Версия прошивки поднята до `2.0.41` как отдельный шаг live runtime consistency fix после ручного API-smoke по режимам и диагностике насоса. (codex)

## [2.0.40] - 2026-03-15

### Изменено
- В `src/drivers/pump.h` и `src/drivers/pump.cpp` добавлена лёгкая runtime-телеметрия worker-task насоса: теперь можно снять `taskAlive`, `taskLoopCount`, `cooperativeSleepCount`, `fastYieldCount`, `lockTimeoutCount`, `lastLoopAtMs` и текущие step/volume метрики без подключения отладчика. (codex)
- В `src/interface/webserver.cpp` добавлен новый диагностический endpoint `GET /api/pump/diag`, чтобы проверять живучесть pump-task и признаки starvation/lock-problem прямо по сети с устройства. (codex)
- В `src/main.cpp` улучшено наполнение `RebootTracker` на текущем boot: `totalReboots`, `wdtReboots`, `crashReboots` и `userReboots` теперь хотя бы отражают тип последнего рестарта, а не оставались нулями при любых причинах перезагрузки. (codex)
- Версия прошивки поднята до `2.0.40` как отдельный шаг диагностической стабилизации и post-fix observability после hotfix насоса. (codex)

## [2.0.39] - 2026-03-15

### Изменено
- `src/drivers/pump.cpp` переработан так, чтобы pump-task больше не держала `core 1` в бесконечном spin-loop на максимальном приоритете: вместо этого добавлен кооперативный yield-slice с `vTaskDelay(...)`, чтобы не душить `idle task` и не провоцировать `Task WDT` reset при работающем насосе. (codex)
- В том же `src/drivers/pump.cpp` добавлена mutex-защита вокруг `AccelStepper`, потому что раньше `runSpeed()` в отдельной задаче и вызовы `start/stop/setSpeed/currentPosition` из основного потока обращались к одному объекту без синхронизации, что создавало race condition во время работы насоса. (codex)
- Живой runtime-check на устройстве показал симптом до фикса: `http://192.168.3.138/api/status` отдавал очень маленький `uptime`, а `http://192.168.3.138/api/reboot/status` показывал `Task WDT`, что хорошо совпало с поведением старой pump-task и стало основанием для точечного драйверного исправления. (codex)
- Версия прошивки поднята до `2.0.39` как отдельный hotfix-шаг по стабилизации насоса после post-migration этапа. (codex)

## [2.0.38] - 2026-03-15

### Изменено
- `tools/ui-smoke/tests/helpers/smoke-helpers.js` расширен поддержкой `GET /api/history` и `GET /api/history/{id}`: общий smoke harness теперь умеет мокать список истории и детали процесса так же удобно, как уже мокал `status` и `logs`. (codex)
- Добавлен новый smoke-тест `tools/ui-smoke/tests/history-v2.smoke.spec.js`, который проходит путь `history list -> details modal` и проверяет `v2` safety summary, completion badge, outcome summary, phase `reasonCode/operatorMessage` и `Safety timeline`. (codex)
- Это закрывает важный post-migration verification gap: после завершения v2 migration история процессов теперь закреплена не только кодом и manual UI-проверками, но и автоматическим smoke-сценарием на ключевые `v2`-поля. (codex)
- Версия прошивки поднята до `2.0.38` как отдельный шаг финальной verification и post-migration polish перед загрузкой на устройство. (codex)

## [2.0.37] - 2026-03-15

### Изменено
- `src/types.h` и `src/drivers/sensors.cpp` очищены от старого `inline static constexpr` warning: `SystemHealth::healthWeights` вынесен в обычное `static const` определение, совместимое с текущим стандартом сборки Arduino/PlatformIO. (codex)
- Это убирает основной шум из firmware build после завершения v2 migration и делает post-migration сборку заметно чище без изменения поведения health scoring. (codex)
- Дополнительно прогнан `npm run test:ui-smoke`: все 5 smoke-тестов Web UI прошли успешно на текущем post-migration состоянии проекта. (codex)
- Версия прошивки поднята до `2.0.37` как отдельный шаг post-migration cleanup и verification. (codex)

## [2.0.36] - 2026-03-15

### Изменено
- `docs/v2/migration_preparation.md` обновлён по итогам финального `Wave 4` audit: теперь в основном migration-доке явно зафиксировано, что runtime/contracts/history/API/UI слой практически завершён и проект находится на стадии финальной верификации, а не активной архитектурной перестройки. (codex)
- В документ добавлены текущая оценка прогресса (`98-99%`), оставшиеся осознанные хвосты (`RC_PHASE_TRANSITION_INFERRED` как честный adapter fallback, `RC_UNSPECIFIED` как резерв совместимости) и практический definition of done для закрытия миграции. (codex)
- Это снижает риск путаницы на следующем этапе: теперь основной migration-док отражает не стартовый план, а фактическое состояние после всех реализованных шагов `2.0.x`. (codex)
- Версия прошивки поднята до `2.0.36` как отдельный шаг фиксации финального migration-review и оставшихся verification goals. (codex)

## [2.0.35] - 2026-03-15

### Изменено
- `src/control/v2/status_adapter.cpp` теперь умеет потреблять explicit terminal transition даже если handler в той же итерации уже перевёл верхнеуровневый `mode` в `IDLE`. (codex)
- Это закрывает важный semantic gap для `NBK`, `FERMENTATION`, `HOLD` и `MASHING`: их финальные `notePhaseTransition(...)` больше не должны теряться и сваливаться в adapter fallback только из-за порядка обновления state внутри одного loop-pass. (codex)
- В результате `lastReasonCode`, transition log, history completion и live `v2` status получают именно explicit финальную причину перехода, а не post-factum inferred mode-exit reason. (codex)
- Версия прошивки поднята до `2.0.35` как отдельный шаг финального runtime-audit по сохранению terminal transitions на happy-path completion сценариях. (codex)

## [2.0.34] - 2026-03-15

### Изменено
- `src/control/modes/distillation_handler.cpp` больше не возвращает `RC_UNSPECIFIED` во внутреннем helper для завершения body-phase: даже защитный fallback теперь использует осмысленный `RC_BODY_END_DETECTED`. (codex)
- Fallback message для завершения body-phase в `DISTILLATION` тоже выровнен до явного `"Distillation body end detected"`, чтобы history, transition log и live status не теряли смысл даже в защитной ветке. (codex)
- Это закрывает ещё один остаточный источник runtime `RC_UNSPECIFIED` и делает `v2` reason contracts практически полным источником правды на реальных mode paths. (codex)
- Версия прошивки поднята до `2.0.34` как отдельный шаг финальной зачистки legacy fallback reasons после runtime-audit inferred paths. (codex)

## [2.0.33] - 2026-03-15

### Изменено
- `src/control/modes/distillation_handler.cpp` больше не использует `RC_PHASE_TRANSITION_INFERRED` в собственной recovery-ветке: восстановление некорректной distillation phase в `BODY` теперь идёт через отдельный explicit reason `RC_PHASE_RECOVERY_APPLIED`. (codex)
- `src/control/v2/reason_codes.h` расширен новым служебным reason code для mode-level phase recovery, чтобы `RC_PHASE_TRANSITION_INFERRED` оставался маркером именно adapter fallback, а не штатных recovery-решений внутри handler. (codex)
- `src/web/runtime/process-notifications.js` научен показывать новый reason code по-человечески, поэтому live уведомления больше не смешивают намеренно восстановленную phase recovery с adapter inference. (codex)
- Версия прошивки поднята до `2.0.33` как отдельный шаг финальной зачистки источников `RC_PHASE_TRANSITION_INFERRED` перед точечным аудитом последних missing contracts. (codex)

## [2.0.32] - 2026-03-15

### Изменено
- `src/control/v2/status_adapter.cpp` перестал затирать `v2.operatorMessage` текущим safety message на каждом цикле: теперь `operatorMessage` остаётся сообщением, связанным именно с `lastReasonCode`, как и задумывалось для live status, API и уведомлений. (codex)
- Fallback safety message теперь подставляется только тогда, когда у статуса ещё нет собственного `lastReasonCode`, поэтому `v2` contract снова корректно различает "последнюю причину перехода" и "текущее safety-состояние". (codex)
- Для неявных mode-change fallback веток `status_adapter` теперь использует общий `setStatusReason(...)`, чтобы `lastReasonCode` и `operatorMessage` обновлялись согласованно даже там, где explicit transition ещё не пришёл. (codex)
- Версия прошивки поднята до `2.0.32` как отдельный шаг финального runtime-audit по выравниванию semantics в `status/status-history/API` перед добивкой последних inferred contracts. (codex)

## [2.0.31] - 2026-03-15

### Изменено
- `src/control/fsm.cpp` выровнен с `src/control/v2/safety_supervisor.cpp`: `abortMode()` теперь маппит `POWER_FAILURE` в явный `RC_SAFETY_TRIP_POWER`, а не теряет эту причину в старом fallback-маршруте. (codex)
- Generic safety abort в `FSM` больше не уходит в `RC_UNSPECIFIED`: для нераспознанных alarm типов теперь выставляется `RC_SAFETY_TRIP_GENERIC`, чтобы history, transition log и live status сохраняли осмысленный `v2` reason code. (codex)
- Это закрывает ещё одно расхождение между runtime stop path и новым `SafetySupervisorV2`, уменьшая остаточную legacy-semantics в `Wave 4` audit. (codex)
- Версия прошивки поднята до `2.0.31` как отдельный шаг точечной зачистки safety reason mappings после перевода phase fallback в explicit inferred transitions. (codex)

## [2.0.30] - 2026-03-15

### Изменено
- `src/control/v2/status_adapter.cpp` больше не пытается угадывать доменные причины phase changes по косвенным признакам, если explicit `notePhaseTransition(...)` не был получен: такой случай теперь честно маркируется как `RC_PHASE_TRANSITION_INFERRED`. (codex)
- Для inferred phase fallback добавлено явное операторское сообщение `from -> to`, поэтому history, transition log и live status теперь показывают, что переход был восстановлен адаптером, а не пришёл из настоящего mode contract. (codex)
- Это дополнительно уменьшает роль `inferPhaseReason(...)` как источника "магических" причин и делает оставшиеся дыры в explicit contracts наблюдаемыми вместо молчаливой подмены правдоподобными reason codes. (codex)
- Версия прошивки поднята до `2.0.30` как отдельный шаг финальной зачистки semantic fallback-логики в `status_adapter`. (codex)

## [2.0.29] - 2026-03-15

### Изменено
- `src/control/v2/safety_supervisor.cpp` получил явные `v2` reason mappings для `POWER_FAILURE` и generic fallback safety trip, поэтому latched safety state больше не скатывается в `RC_UNSPECIFIED` при неполном alarm mapping. (codex)
- `src/control/v2/reason_codes.h` расширен кодами `RC_SAFETY_TRIP_POWER` и `RC_SAFETY_TRIP_GENERIC`, а также новым `power_failure` safety event token для статуса и API. (codex)
- `src/history.cpp`, `src/web/history/details.js` и `src/web/runtime/process-notifications.js` научены распознавать и человекочитаемо показывать новые safety trip reasons, чтобы post-mortem и live UI не теряли смысл при power/generic авариях. (codex)
- Версия прошивки поднята до `2.0.29` как отдельный шаг почти финальной зачистки fallback reason codes в safety-v2 слое. (codex)

## [2.0.28] - 2026-03-15

### Изменено
- `src/control/v2/reason_codes.h` добавлен технический `RC_PHASE_TRANSITION_INFERRED`, чтобы остаточные fallback-переходы больше не попадали в runtime/history как безликий `RC_UNSPECIFIED`. (codex)
- `src/control/v2/status_adapter.cpp` переведён на новый fallback reason для неявных phase changes, так что даже если адаптеру всё ещё приходится восстанавливать причину перехода, он теперь делает это явно и трассируемо. (codex)
- `src/control/modes/distillation_handler.cpp` получил explicit recovery transition в аварийной `default`-ветке: восстановление distillation phase в `BODY` теперь тоже идёт через `notePhaseTransition(...)`, а не через немой phase reset. (codex)
- `src/web/runtime/process-notifications.js` научен человекочитаемо показывать `RC_PHASE_TRANSITION_INFERRED`, а версия прошивки поднята до `2.0.28` как отдельный шаг зачистки остаточных fallback semantics. (codex)

## [2.0.27] - 2026-03-15

### Изменено
- `src/control/v2/status_adapter.cpp` теперь на `mode -> IDLE` сначала доверяет уже зафиксированному terminal `v2` reason code, а не пытается заново выводить завершение только по `previousPhaseId` и legacy mode-change эвристикам. (codex)
- Для normal completion классификация success вынесена в `isSuccessfulCompletionReason(...)`, поэтому `mashing`, `hold` и `fermentation` корректно сохраняют свой explicit completion reason при выходе в `IDLE` без лишнего пересчёта. (codex)
- Fallback для mode exit сжат в отдельный `inferModeExitReason(...)`: safety stop по-прежнему берётся из latched alarm, а фазовые допущения остаются только как запасной путь для legacy завершений `RECTIFICATION/DISTILLATION/NBK`. (codex)
- Версия прошивки поднята до `2.0.27` как отдельный шаг уменьшения inference-layer в `status_adapter` перед дальнейшей зачисткой оставшихся `RC_UNSPECIFIED` fallback paths. (codex)

## [2.0.26] - 2026-03-15

### Изменено
- `src/control/fsm.cpp` получил общий `noteModeExitTransition(...)` для всех режимов, поэтому explicit `v2` exit contracts теперь выставляются централизованно и больше не размазаны частично по `stopMode()` fallback-веткам. (codex)
- Для `RECTIFICATION` и `NBK` теперь тоже ставятся явные stop transitions при операторской остановке, а `abortMode()` больше не проходит через user-stop path и сохраняет safety reason code из текущей аварии. (codex)
- Общий helper `finalizeModeStop(...)` выровнял shutdown/reset mode state без дублирования, а `status_adapter` после этого получает меньше mode-change inference и больше реальных причин завершения. (codex)
- Версия прошивки поднята до `2.0.26` как отдельный шаг миграции stop/abort semantics к единому `v2` lifecycle contract. (codex)

## [2.0.25] - 2026-03-15

### Изменено
- `src/control/v2/mode_contracts.h` добавлен общий `kNoPhaseIdV2`, чтобы `v2`-контракты могли явно обозначать отсутствие активной фазы и не подменять этот случай локальными эвристиками разных режимов. (codex)
- `src/control/v2/status_adapter.cpp` теперь трактует `kNoPhaseIdV2` как `idle` phase token, считает его idle-like для history и разрешает explicit start transition из `IDLE`, если handler передал sentinel вместо legacy phase id. (codex)
- `src/control/modes/hold_handler.cpp` и `src/control/fsm.cpp` переведены на новый контракт для `HOLD`: старт логируется как `idle -> hold_step`, а операторская остановка как `hold_step -> idle`, без двусмысленного `hold_step -> hold_step`. (codex)
- Версия прошивки поднята до `2.0.25` как отдельный шаг выравнивания phase semantics для `HOLD` перед дальнейшей зачисткой остаточных fallback-эвристик `Wave 4`. (codex)

## [2.0.24] - 2026-03-15

### Изменено
- `src/control/v2/status_adapter.cpp` расширен поддержкой explicit start transitions: `notePhaseTransition(...)` теперь корректно подхватывается не только для фаз внутри режима и остановок, но и для старта режима из `IDLE`, если handler явно задал стартовый контракт. (codex)
- `src/control/fsm.cpp` теперь ставит явный `RC_MODE_START_REQUEST` для старта `RECTIFICATION`, `DISTILLATION`, `NBK` и `FERMENTATION`, а `src/control/modes/mashing_handler.cpp` и `src/control/modes/hold_handler.cpp` делают то же для прямых `Mashing::start(...)` и `Hold::start(...)`. (codex)
- Это убирает ещё один слой fallback-эвристики из `Wave 4`: history, transition log и live `lastReasonCode` получают реальный стартовый reason contract вместо молчаливого восстановления из mode-change fallback. (codex)
- Версия прошивки поднята до `2.0.24` как отдельный шаг миграции стартовых контрактов к полноценному v2 mode lifecycle. (codex)

## [2.0.23] - 2026-03-15

### Изменено
- `src/control/modes/fermentation_handler.cpp` теперь завершает ферментацию по `settings.fermentation.durationHours`, ставит явный `v2` transition `RUNNING -> COMPLETED` с `RC_FERM_TARGET_REACHED` и корректно выключает нагреватель при финале режима. (codex)
- Для `FERMENTATION` в `src/control/fsm.cpp` добавлена явная операторская остановка через `RC_MODE_STOP_REQUEST`, чтобы ручной stop этого режима не терялся в history и transition log. (codex)
- `src/control/v2/status_adapter.cpp` теперь считает `FERMENTATION` успешным завершением при `RC_FERM_TARGET_REACHED`, поэтому completion summary/history больше не выглядят как простой `stopped`, если режим закончил работу штатно по времени. (codex)
- Версия прошивки поднята до `2.0.23` как отдельный шаг `Wave 4` для перевода `fermentation` на явные v2 completion transitions. (codex)

## [2.0.22] - 2026-03-15

### Изменено
- `src/control/modes/hold_handler.cpp` и `src/control/modes/mashing_handler.cpp` переведены на явные `v2` phase contracts для температурных шагов: завершение шага и финал программы теперь ставят `ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE` с операторским сообщением прямо в месте перехода. (codex)
- Для `MASHING` и `HOLD` в `src/control/fsm.cpp` добавлена явная операторская остановка через `RC_MODE_STOP_REQUEST`, чтобы manual stop этих режимов не терялся в history и transition log. (codex)
- `src/control/v2/status_adapter.cpp` теперь считает `MASHING` и `HOLD` успешным завершением при `RC_TEMP_STEP_HOLD_COMPLETE`, так что history и completion summary для этих температурных программ больше не выглядят как обычный `stopped`. (codex)
- Версия прошивки поднята до `2.0.22` как отдельный шаг `Wave 4` для перевода `hold/mashing` на явные v2 phase transitions и корректный completion outcome. (codex)

## [2.0.21] - 2026-03-15

### Изменено
- `src/control/modes/distillation_handler.cpp` переведён на явные `v2` phase contracts: переходы `HEATING -> HEADS/BODY`, `HEADS -> BODY`, `BODY -> FINISH` и `FINISH -> IDLE` теперь ставят `ReasonCodeV2` и операторские сообщения прямо в месте смены фазы. (codex)
- Для `DISTILLATION` в `src/control/fsm.cpp` добавлена явная фиксация операторской остановки через `RC_MODE_STOP_REQUEST`, чтобы history и transition log не падали обратно в fallback-эвристику при ручном stop режима. (codex)
- Это закрывает следующий кусок `Wave 4` из `docs/v2/migration_preparation.md`: distillation стал ещё одним режимом, который уже живёт с реальными v2 transition reasons, а не только экспортируется через v2 status/read-only слой. (codex)
- Версия прошивки поднята до `2.0.21` как отдельный шаг миграции `DISTILLATION` к явным v2 phase transitions. (codex)

## [2.0.20] - 2026-03-15

### Изменено
- `src/history.h`, `src/history.cpp` и `src/interface/webserver.cpp` расширены compact итогом завершения процесса: history list теперь получает `completionState`, `completionReasonCode` и `completionOperatorMessage`, чтобы различать normal finish, operator stop и safety stop без полной загрузки details. (codex)
- `src/web/history/list.js` теперь показывает отдельный completion badge в карточке history и использует новый v2-aware итоговый state поверх legacy `status`, не пряча важное различие между `FINISH`, `OPERATOR STOP` и `SAFETY STOP`. (codex)
- `src/web/styles/_history.css` дополнен стилями completion badge, а подсказка `title` оставлена только как вспомогательная detail-информация, не как единственный способ понять исход процесса. (codex)
- Версия прошивки поднята до `2.0.20` как отдельный шаг миграции history list к v2 completion state summary. (codex)

## [2.0.19] - 2026-03-15

### Изменено
- `src/history.h`, `src/history.cpp` и `src/interface/webserver.cpp` расширены compact summary последнего фазового исхода: history list теперь без полной загрузки details получает `lastPhaseName`, `lastReasonCode` и `lastOperatorMessage` из последней записанной фазы процесса. (codex)
- `src/web/history/list.js` теперь показывает в карточке процесса короткую v2-aware строку итогового перехода по последней фазе, причине и операторскому сообщению, чтобы рядом с safety badge было сразу видно, чем закончился последний значимый этап. (codex)
- Для новой summary-строки в `src/web/history/list.js` добавлено HTML-экранирование, а `src/web/styles/_history.css` получил компактный ellipsis-стиль, чтобы `operatorMessage` безопасно и аккуратно отображался в списке history. (codex)
- Версия прошивки поднята до `2.0.19` как отдельный шаг миграции history list к v2 phase outcome summary. (codex)

## [2.0.18] - 2026-03-15

### Изменено
- `src/history.h` и `src/history.cpp` расширены compact safety summary для `ProcessListItem`: history list теперь при быстрой загрузке процесса вычисляет флаги `trip/ack/recovery/reset/limited` и собирает короткий итоговый `safetySummary` без полной загрузки details JSON. (codex)
- `/api/history` в `src/interface/webserver.cpp` теперь отдаёт `safetyState`, `safetySummary` и связанные safety-флаги, чтобы список процессов мог сразу показать, завершился ли safety-сценарий полным `ACK + RESET`. (codex)
- `src/web/history/list.js` и `src/web/styles/_history.css` теперь рисуют safety badge прямо в карточке процесса, поэтому в history list сразу видно `TRIP`, `ACK`, `RECOVERY` или `ACK + RESET` без открытия details modal. (codex)
- Версия прошивки поднята до `2.0.18` как отдельный шаг миграции history list к v2-aware safety summary. (codex)

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
- Устранено дублирование логики самодиагностики в `src/main.cpp` (gemini).

### Добавлено
- Реализована полноценная взвешенная матрица здоровья (Health Matrix) с 6 подсистемами: SENSORS (40%), SAFETY (20%), POWER (20%), WIFI (10%), STORAGE (5%), OTA (5%) (gemini).
- Внедрена система `RebootTracker` для глубокого анализа причин перезагрузки ESP32-S3 (WDT, Brownout, Exception, SW Reset) (gemini).
- Добавлен REST API эндпоинт `/api/reboot/status` для интеграции со сторонними сервисами мониторинга (gemini).
- Информация о причине перезагрузки интегрирована в API `/api/health` для отображения в Web UI (gemini).

---

## [1.13.6] - 2026-03-15

### Исправлено
- **CRITICAL-FIX** (`watt_control.cpp`, `watt_control.h`): Устранена причина периодических перезагрузок (WDT Reset) при использовании симисторного регулятора. Логика управления мощностью, включая расчеты и логирование, вынесена из прерывания (ISR) в выделенную FreeRTOS задачу с высоким приоритетом. Это предотвращает блокировку системы и обеспечивает стабильную работу. (gemini)
- **Архитектура**: Реализован инкапсулированный класс `WattControl` для управления симистором, следующий паттерну "ISR -> FreeRTOS Task", что соответствует лучшим практикам для real-time систем. (gemini)

### Добавлено
- **Health Matrix**: Расширен `SystemHealth` структурой с взвешенным скорингом по подсистемам (датчики, память, связь, питание, температура, стабильность) для более точной оценки состояния системы (gemini)

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

### Изменено
- Умная логика безопасности: отказ вспомогательных датчиков (ТСА, Охлаждение, Давление) больше не вызывает немедленную остановку процесса, а требует подтверждения пользователя в UI (Soft Failure). (gemini)

## [1.13.0] - 2026-03-14

### Добавлено
- **#4** (`fsm.cpp`): `getPhaseProgressPercent()` и `getPhaseTargetSec()` теперь возвращают реальный прогресс для всех режимов — Mashing (по выдержке шага), Hold (по длительности шага), NBK (по темп./времени/объёму), Fermentation (по времени). Ранее возвращали 0.
- **#4** (`types.h`): В `NbkSettings` добавлено поле `targetVolumeMl` (0=неизвестно), в `FermentationSettings` — `durationHours` (0=бессрочно). Через Web UI эти параметры можно задать для отображения прогресса.
- **#14** (`main.cpp`): Self-check лог каждые 30 минут — записывает свободный heap, uptime, причину перезагрузки и счётчики ошибок датчиков в `Logger::logf()` и Serial.

### Без изменений (подтверждено)
- **#7** `buzzerTask`: WDT не требуется — задача спит на `portMAX_DELAY` (правильный паттерн FreeRTOS).
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
