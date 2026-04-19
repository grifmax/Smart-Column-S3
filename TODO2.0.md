# TODO — Smart-Column S3

Единый список незавершённых задач. Завершённые задачи сохранены для контекста.

**Версия прошивки:** 2.2.24 | **Дата:** 2026-04-19

---

## ✅ Реализовано (Major)

- [x] **Авто-ректификация** — FSM, Watt-Control, Smart Decrement, УНО-цикл
- [x] **Ручная ректификация** — `ManualRect::update()`, анти-захлёб, смена фаз по API
- [x] **Дистилляция** — FSM с фазами, volume tracking
- [x] **Затирание** — FSM, температурные профили, шаги
- [x] **Hold / Пастеризация** — температурные ступени, несколько шагов
- [x] **НБК режим** — `NbkPhase` FSM, Watt-Control по давлению
- [x] **Ферментация** — `FermentationPhase` FSM, завершение по времени
- [x] **Мешалка куба 0-10В** — MCP4725 DAC + MCP6001 Op-Amp, авто-запуск из FSM
- [x] **Контроль мощности** — SSR + PZEM feedback, WattControl, ramp
- [x] **Насос** — TMC2209, FreeRTOS task, аппаратный PWM, калибровка
- [x] **Клапаны** — вода/головы/УНО, серво-фракционник (5 позиций), импульсы
- [x] **История процессов v2** — timeseries, фазы, safety timeline, reason codes, экспорт
- [x] **Health matrix** — взвешенный скоринг подсистем, RebootTracker
- [x] **MQTT / Home Assistant** — Discovery, Energy Dashboard, команды
- [x] **Cloud Tunnel** — WebSocket прокси, spiritcontrol.ru
- [x] **HTTP Auth + Rate Limiting** — Basic Auth, 60 req/min, per-IP
- [x] **OTA обновления** — loadfs + firmware по воздуху
- [x] **TFT дисплей** — ILI9488 3.5", mode-specific экраны, watchdog
- [x] **Тестирование оборудования** — service workbench в Web UI
- [x] **WiFi профили** — несколько сетей с приоритетами, static IP
- [x] **Система безопасности v2** — reason codes, safety supervisor, ack/reset flow

---

## 🔧 В работе / Ближайшие задачи

### Мешалка (новое, v2.2.0)

- [x] **API endpoints** — `/api/stirrer/start`, `/api/stirrer/stop`, `/api/stirrer/set` в `webserver.cpp`
- [x] **NVS сохранение** настроек мешалки — загрузка/сохранение через `nvs_manager.cpp`
- [x] **Web UI виджет** — виджет ручного управления и live-статус мешалки на главной странице
- [x] **Настройки в Web UI** — вкладка «Оборудование» → «Параметры» → секция «Мешалка»
- [x] **Сервисный тест мешалки** — вкладка «Оборудование» → «Тестирование» → карточка ручного теста мешалки

### Прошивка

- [ ] **Reboot reason tracking** — полная интеграция в `/api/reboot/status` и Web UI уведомления
- [ ] **Soak test** — 8h+ непрерывной работы, критерии приёмки
- [ ] **Compile-time pin profiles** — ревизии плат через `BOARD_REV`
- [ ] **Boot-time проверка пинов** — конфликты GPIO, сообщения об ошибках

### TFT дисплей

- [ ] **Унификация кнопок по режимам** — таблица действий вместо if/else (`display.cpp`)
- [ ] **Страница «Все температуры»** — отдельный экран со всеми 7 датчиками
- [ ] **Конфигурируемый refresh profile** — `normal` / `safe` / `fast` через настройки

### Web UI

- [ ] **Профили — полный CRUD UI** — API готов, веб-формы не завершены
- [ ] **Калибровка — полный UI** — структуры и API есть, веб-формы частично

---

## 📋 Роадмап

### v2.2.x — Финализация UI
- Полный CRUD профилей
- Полный UI калибровки
- TFT: унификация кнопок, экран температур

### v3.0.x — Hardware v3
- Compile-time профили плат
- Поддержка ревизии V3 PCB
- Boot-time self-test GPIO

---

*Обновлено: 2026-04-15*
