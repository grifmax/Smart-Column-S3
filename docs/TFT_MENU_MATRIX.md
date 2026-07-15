    ссссссссссссссссссссссссс# TFT Menu Matrix

Документ фиксирует текущее поведение встроенного TFT-меню и матрицу прав
доступа для экранов.

Источник истины в коде:
- `src/drivers/display.cpp`
- `handleNavigationTap()`
- `handleScreenTap()`
- `handleModeMonitorTap()`
- `render*()` для конкретных экранов

## 1. Корневые экраны

| Экран | Когда показывается | Основное назначение |
| --- | --- | --- |
| `DASHBOARD` | Когда система в `IDLE` | Общая сводка и быстрый переход к управлению |
| `MODE_MONITOR` | Когда активен любой процесс | Runtime-экран текущего режима |
| `CONTROL` | Всегда доступен из нижних вкладок | Старт, пауза, стоп, ручной доступ |
| `SETTINGS` | Всегда доступен из нижних вкладок | Разделы настроек и быстрые системные переключатели |
| `SERVICE` | Всегда доступен из нижних вкладок | Диагностика и сервисные данные |

## 2. Подэкраны

| Экран | Откуда открывается | Назначение |
| --- | --- | --- |
| `EQUIPMENT` | `SETTINGS` | Параметры оборудования |
| `RECT_PARAMS` | `SETTINGS` | Параметры ректификации |
| `DIST_PARAMS` | `SETTINGS` | Параметры дистилляции |
| `CALIBRATION` | `SETTINGS` | Калибровки насоса и touch |
| `MANUAL` | `CONTROL` | Ручное управление узлами |
| `VALUE_EDIT` | Из редактируемых экранов | Универсальный редактор одного значения |
| `ALL_TEMPS` | `DASHBOARD`, `MODE_MONITOR`, `SERVICE` | Просмотр всех температур |

## 3. Матрица доступа

| Экран | Просмотр в `IDLE` | Редактирование в `IDLE` | Просмотр во время процесса | Редактирование во время процесса | Примечание |
| --- | --- | --- | --- | --- | --- |
| `DASHBOARD` | Да | Нет | Нет | Нет | В процессе корнем становится `MODE_MONITOR` |
| `MODE_MONITOR` | Нет | Нет | Да | Зависит от режима | См. раздел 4 |
| `CONTROL` | Да | Да | Да | Да | Старт/пауза/стоп и переход в `MANUAL` |
| `SETTINGS` | Да | Да | Да | Да | Быстрые toggles внизу доступны всегда |
| `SERVICE` | Да | Нет | Да | Нет | Только просмотр |
| `EQUIPMENT` | Да | Да | Да | Нет | Во время процесса только просмотр |
| `RECT_PARAMS` | Да | Да | Да | Нет | Во время процесса только просмотр |
| `DIST_PARAMS` | Да | Да | Да | Нет | Во время процесса только просмотр |
| `CALIBRATION` | Да | Да | Да | Нет | Во время процесса калибровка запрещена |
| `MANUAL` | Да | Да | Ограниченно | Ограниченно | Разрешён только в `IDLE` и `MANUAL_RECT` |
| `VALUE_EDIT` | Да | Да | Да | Да | Транзитный экран, сам по себе policy не задаёт |
| `ALL_TEMPS` | Да | Нет | Да | Нет | Только просмотр |

## 4. Правила для MODE_MONITOR

Редактирование на runtime-экране разрешено только для следующих режимов:

| Режим | Редактирование | Что именно редактируется |
| --- | --- | --- |
| `DISTILLATION` | Да | Мощность и конечная температура |
| `MANUAL_RECT` | Да | Скорость, мощность, цели по фракциям |
| `MASHING` | Да | Температура и длительность шагов |
| `HOLD` | Да | Температура и длительность шагов |
| `RECTIFICATION` | Нет | Только просмотр |
| `NBK` | Нет | Только просмотр |
| `FERMENTATION` | Нет | Только просмотр |

## 5. Навигационные правила

- Нижние вкладки всегда ведут в один из root-экранов.
- Для подэкранов кнопка `Back` находится в правом верхнем углу.
- `DASHBOARD`:
  просмотр температур ведёт в `ALL_TEMPS`
  тап по остальной рабочей области ведёт в `CONTROL`
- `MODE_MONITOR`:
  правая зона с метриками ведёт в `ALL_TEMPS`
  левая зона без срабатывания runtime-edit логики ведёт в `CONTROL`
- `SETTINGS`:
  карточки открывают разделы
  нижняя строка управляет быстрыми системными toggle-параметрами

## 6. Быстрые системные toggles в SETTINGS

Сейчас на корневом `SETTINGS` доступны:

| Элемент | Действие |
| --- | --- |
| `Theme` | Переключение светлой и тёмной темы TFT |
| `Sound` | Глобальное включение/выключение звука |
| `Language` | Переключение RU/EN |
| `Display` | Цикл `Normal -> Safe -> Fast -> Normal` |
| `Stirrer` | Глобальное включение/выключение мешалки |

Отдельное правило для `Stirrer`:
- при выключении с TFT сразу вызывается `Stirrer::stop()`
- `autoMode` у мешалки сбрасывается

## 7. Что считается зафиксированным policy

- Подэкраны настроек во время активного автопроцесса работают в режиме
  только просмотра.
- Runtime-редактирование на `MODE_MONITOR` не является общим правилом для
  всех режимов и разрешено только для явно перечисленных режимов.
- `MANUAL` не считается обычным runtime-экраном и живёт по отдельной
  политике доступа.

## 8. Политика redraw

Ниже зафиксировано, какие экраны сейчас работают по схеме полного redraw,
а какие уже используют `full-once + partial updates`.

### Общие правила

- `full redraw` выполняется при первом кадре нового экрана.
- Для экранов с partial-update полный redraw также разрешён при recovery
  display watchdog.
- Для корневого `SETTINGS` полный redraw дополнительно разрешён при смене
  `theme` и `language`, потому что это меняет весь экранный chrome.
- Если экран не перечислен как partial-update, его нужно считать экраном
  полного redraw до отдельной фиксации в этом документе.

### Матрица redraw

| Экран | Текущая схема redraw | Когда допустим полный redraw | Что обновляется частично |
| --- | --- | --- | --- |
| `DASHBOARD` | `full-once + partial` | Вход на экран, смена layout, recovery | status bar, плитки, footer, отдельные runtime-значения |
| `MODE_MONITOR` | `full-once + partial` | Вход на экран, смена layout режима, recovery | status bar, runtime tiles, footer, summary-панели |
| `CONTROL` | Полный redraw | Каждый redraw этого экрана | В текущей реализации partial policy не зафиксирован |
| `SETTINGS` | `full-once + partial` | Вход на экран, смена `theme`, смена `language`, recovery | карточки разделов, быстрые toggles, footer |
| `EQUIPMENT` | `full-once + partial` | Вход на экран, recovery | value tiles, footer |
| `RECT_PARAMS` | `full-once + partial` | Вход на экран, смена страницы `feed/cuts <-> flow/temp`, recovery | page button, value tiles, footer |
| `DIST_PARAMS` | `full-once + partial` | Вход на экран, recovery | value tiles, footer |
| `CALIBRATION` | `full-once + partial` | Вход на экран, recovery | calibration tiles, footer |
| `MANUAL` | Полный redraw | Каждый redraw этого экрана | Partial policy пока не зафиксирован |
| `VALUE_EDIT` | Полный redraw | Каждый redraw этого экрана | Partial policy пока не зафиксирован |
| `SERVICE` | `full-once + partial` | Вход на экран, recovery | диагностические tiles, footer |
| `ALL_TEMPS` | `full-once + partial` | Вход на экран, recovery | только value-области температурных tiles |

### Практическая интерпретация

- Для `SETTINGS` и его подэкранов цель сейчас такая:
  никаких повторных `fillScreen()` на обычных refresh-циклах.
- Если после future changes экран снова начинает делать полный clear на
  каждом update, это считается регрессией redraw policy.
- Следующие кандидаты на перевод в `full-once + partial` при дальнейшей
  работе: `CONTROL`, `MANUAL`, `VALUE_EDIT`.

## 9. Причины redraw

Ниже зафиксированы причины, по которым экран может запросить redraw, и
ожидаемая реакция UI-слоя.

### Нормальные причины redraw

| Причина | Когда возникает | Ожидаемая реакция |
| --- | --- | --- |
| `screen enter` | Переход на другой экран через `switchRoot`, `pushScreen`, `popScreen` | Полный redraw нового экрана |
| `tap handled` | Пользовательский tap изменил состояние UI или параметр | Обычно partial redraw текущего экрана; полный redraw только если сменился экран |
| `data changed` | Изменились температуры, мощность, статусы, параметры | Partial redraw только тех зон, где реально изменились данные |
| `phase timer progress` | Обновление таймера фазы и progress bar при стабильных температурах | Partial redraw status/footer/runtime-метрик |
| `forced refresh interval` | Периодический refresh даже при стабильных данных | Для partial-экранов не должен превращаться в полный clear |
| `sparkline refresh` | Обновление историй графиков | Partial redraw связанных tiles |
| `theme change` | Переключение светлой/тёмной темы | Полный redraw всего экрана |
| `language change` | Переключение RU/EN | Полный redraw всего экрана |
| `layout change` | Для runtime-экранов меняется layout по режиму/странице | Полный redraw текущего layout |
| `watchdog recovery` | Soft/hard recovery display watchdog | Разрешён полный redraw |

### Что считается правильным поведением

- `screen enter` всегда может делать полный redraw.
- `theme change` и `language change` считаются глобальными причинами и тоже
  могут делать полный redraw.
- `data changed`, `forced refresh interval` и `sparkline refresh` для экранов,
  уже переведённых на partial-update, не должны вызывать повторный
  `fillScreen()`.
- `tap handled` не должен автоматически означать полный redraw, если экран не
  сменился и layout не поменялся.

### Что считать регрессией

- Статичный экран вроде `SETTINGS`, `EQUIPMENT`, `DIST_PARAMS` или
  `CALIBRATION` уходит в полный clear на обычном таймерном refresh.
- Изменение одного значения на экране приводит к полной перерисовке всего TFT
  без необходимости.
- Частичный экран начинает вести себя как `full redraw on every update` после
  рефактора или добавления новой UI-логики.

### Для будущей телеметрии

Если в код будет добавляться диагностика display redraw, минимально полезные
 причины для счётчиков такие:

- `screen_enter`
- `tap_action`
- `live_data_changed`
- `timer_keepalive`
- `sparkline_refresh`
- `theme_changed`
- `language_changed`
- `layout_changed`
- `recovery_redraw`

## 10. Чек-лист проверки на железе

Ниже минимальный чек-лист для реальной проверки TFT redraw после прошивки.

### Подготовка

- Прошить актуальную сборку на устройство.
- Убедиться, что TFT и touch работают штатно.
- Если есть доступ к web/API, открыть статус display telemetry параллельно
  визуальной проверке.

### Что проверять глазами

#### 1. Корневой `SETTINGS`

- Открыть `SETTINGS`.
- Ничего не трогать 10-20 секунд.
- Убедиться, что экран не делает видимого полного мигания фона.
- Допустимо:
  локальное обновление карточек, toggles или footer без общего clear.
- Недопустимо:
  повторный полный `fillScreen`-эффект с заметным морганием всего экрана.

#### 2. Подэкраны `EQUIPMENT`, `RECT_PARAMS`, `DIST_PARAMS`, `CALIBRATION`

- По очереди открыть каждый подэкран.
- На каждом экране постоять 10-20 секунд без действий.
- Для `RECT_PARAMS` отдельно проверить обе страницы:
  `feed/cuts` и `flow/temp`.
- Убедиться, что нет полного моргания экрана на обычных refresh-циклах.

#### 3. Быстрые toggles в `SETTINGS`

- Нажать `Sound`.
- Нажать `Display`.
- Нажать `Stirrer`.
- Проверить, что меняются только нужные кнопки/подписи, без полного
  моргания всего экрана.

#### 4. Глобальные full redraw случаи

- Переключить `Theme`.
- Переключить `Language`.
- Здесь полный redraw допустим и ожидаем.
- Проверить, что после него экран приходит в стабильное состояние и не
  продолжает мигать на последующих idle-refresh.

#### 5. Навигация

- `SETTINGS -> EQUIPMENT -> Back`
- `SETTINGS -> RECT_PARAMS -> Back`
- `SETTINGS -> DIST_PARAMS -> Back`
- `SETTINGS -> CALIBRATION -> Back`
- Проверить, что полный redraw виден только на входе в новый экран, а не
  повторяется циклически после входа.

### Что проверять по telemetry

Если доступен web/API, при тесте смотреть такие поля:

- `display.fullRedraws`
- `display.partialRedraws`
- `display.lastReason`
- `display.reasons.screenEnter`
- `display.reasons.tapAction`
- `display.reasons.liveDataChanged`
- `display.reasons.timerKeepalive`
- `display.reasons.themeChanged`
- `display.reasons.languageChanged`
- `display.reasons.layoutChanged`
- `display.reasons.recovery`

### Ожидаемая интерпретация telemetry

- При входе на экран должен расти `screenEnter`, и может увеличиваться
  `fullRedraws`.
- При обычном ожидании на `SETTINGS` и его подэкранах допустим рост
  `partialRedraws`, но не постоянный рост `fullRedraws`.
- После `Theme` и `Language` допустим рост `fullRedraws` и соответствующих
  причин `themeChanged` / `languageChanged`.
- Если в покое на `SETTINGS` быстро растут `fullRedraws`, это считать
  регрессией redraw policy.
- Если появляется `recovery`, отдельно проверять, не связано ли это уже не с
  UI policy, а с производительностью дисплея или SPI.

### Короткий итог проверки

После прохода зафиксировать четыре вывода:

- Есть ли видимое моргание на `SETTINGS`
- Есть ли видимое моргание на подэкранах настроек
- Растёт ли `fullRedraws` там, где ожидался только partial-update
- Были ли `recovery` во время проверки
