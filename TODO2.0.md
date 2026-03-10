# TODO — Smart-Column S3

Единый список незавершённых задач. Завершённые фазы убраны.

---

## Firmware: FSM и runtime

- [x] **Ручная ректификация** — Реализован `ManualRect::update()` с автоматикой воды по T_cube, подсчетом объемов фракций, анти-захлебной защитой от давления и возможностью смены фаз по API (`/api/manual/phase`).
- [ ] **Профили → система** — `applyProfile()` загружает профиль, но не применяет к `g_settings`/`g_state` (`profiles.cpp:663`). `createProfileFromSettings()` возвращает дефолты (`profiles.cpp:735`).
- [ ] **MQTT команды** — callback подписан на `cmd/#`, но тело пустое (`mqtt.cpp:36`). Нужен dispatch: start/stop/pause/resume/heater/pump/valves.
- [ ] **Давление в ареометре** — используется `density` вместо реального давления (`webserver.cpp:1659`).
- [ ] **NBK режим** — нет в `Mode` enum, нет FSM логики.
- [ ] **Ферментация** — нет в `Mode` enum, нет FSM логики.

## TFT дисплей

- [ ] **Унификация кнопок по режимам** — кнопки захардкожены if/else, нет таблицы действий по режиму (`display.cpp:2722`).
- [ ] **Страница "Все температуры"** — отдельный экран со всеми подключёнными датчиками.
- [ ] **Конфигурируемый refresh profile** (`normal` / `safe`) — сейчас единственный хардкод `DISPLAY_FORCE_REFRESH_MS = 5000`.

## Telegram (FastBot2)

- [ ] **Reconnect/backoff** — при потере WiFi бот просто пропускает `tick()`, нет backoff/retry логики.
- [ ] **Rate limiting команд** — нет троттлинга, каждое сообщение обрабатывается мгновенно.
- [ ] **Телеметрия ошибок** — ошибки отправки логируются в Serial, но нет счётчиков/статистики.
- [ ] **Команда /health** — `notifyHealthAlert()` есть как push, но нет pull-команды для пользователя.

## Системная стабильность

- [ ] **Health matrix** — `SystemHealth` struct есть и используется (MQTT publish, Telegram alert), но `overallHealth` — один скаляр без взвешенного скоринга по подсистемам.
- [ ] **Reboot reason tracking** — WDT-reset детектируется при старте (Serial), но не сохраняется в NVS и не отдаётся в API/UI.
- [ ] **Периодический self-check** с event log.
- [ ] **Soak test** 8h+ и критерии приёмки.

## Hardware abstraction

- [ ] UART/pin mapping profiles (PZEM на выделенный порт).
- [ ] Compile-time pin profiles для ревизий плат.
- [ ] Boot-time проверка пинов и конфликтов.

## Web UI

- [ ] **Профили — полный CRUD UI** — API готов, веб-интерфейс не завершён.
- [ ] **Калибровка — полный UI** — структуры есть, веб-формы частично.
