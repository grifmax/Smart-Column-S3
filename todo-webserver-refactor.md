# TODO: Webserver Refactor

Статус: `in_progress`

Цель этого документа: разложить по шагам безопасный рефакторинг `src/interface/webserver.cpp`, чтобы позже пройти его по чеклисту без потерь маршрутов, побочных эффектов и служебной логики.

Текущее состояние на момент составления:
- `src/interface/webserver.cpp`: около `7269` строк
- Внутри смешаны:
  - регистрация HTTP-маршрутов
  - JSON-сериализация состояния
  - preflight / launch gating / topology / safety helpers
  - WebSocket / live-broadcast
  - pump calibration / equipment testing / OTA / Wi-Fi / profiles
- Цель первого прохода: не “идеальная архитектура”, а перевод из монолита в управляемую модульную схему

---

## 1. Целевой результат

- [ ] `webserver.cpp` перестаёт быть главным местом со всей HTTP-логикой
- [ ] маршруты разложены по тематическим модулям
- [ ] `broadcastState()` и `broadcastEvent()` вынесены из route-registration слоя
- [ ] общие helper-функции и shared state перестают жить в случайном `static`-хаосе
- [ ] каждый этап проходит сборку без регрессий API
- [ ] итоговая структура понятна любому, кто открыл `src/interface/`

---

## 2. Что не делаем в первом проходе

- [ ] не переписываем бизнес-логику режимов
- [ ] не меняем payload API без необходимости
- [ ] не меняем URL-маршруты
- [ ] не делаем большой “архитектурный взрыв” с десятками мелких файлов сразу
- [ ] не тащим в этот рефакторинг TFT, MQTT, ControlV2 и прочие внешние подсистемы глубже, чем нужно

Принцип: сначала переносим код как есть, потом уже чистим структуру и только затем думаем о глубокой нормализации.

---

## 3. Целевая структура каталогов

Предлагаемая базовая структура:

```text
src/interface/
  webserver.cpp                # координатор
  webserver.h
  webserver_shared.h           # shared declarations / helpers contracts
  web_live.cpp                 # broadcastState / broadcastEvent / ws helpers
  web_live.h
  api/
    api_routes.h
    api_health.cpp
    api_logs.cpp
    api_charts.cpp
    api_energy.cpp
    api_ota.cpp
    api_wifi.cpp
    api_pump.cpp
    api_testing.cpp
    api_calibration.cpp
    api_history.cpp
    api_profiles.cpp
    api_process.cpp
    api_safety.cpp
    api_settings.cpp
    api_status.cpp
```

Возможный второй проход:

```text
src/interface/api/settings/
  api_settings_equipment.cpp
  api_settings_safety.cpp
  api_settings_security.cpp
  api_settings_mqtt.cpp
  api_settings_rect.cpp
  api_settings_aux.cpp
```

---

## 4. Главные архитектурные решения

### 4.1 Координатор

- [ ] `webserver.cpp` оставить координатором
- [ ] в координаторе держать только:
  - `AsyncWebServer server`
  - `AsyncWebSocket ws`
  - middleware
  - `server.onNotFound(...)`
  - `server.begin()`
  - вызовы `registerXxxRoutes(...)`

### 4.2 Регистрация маршрутов

- [ ] каждый API-модуль экспортирует одну публичную функцию вида:
  - `void registerHealthRoutes(AsyncWebServer& server);`
  - `void registerLogsRoutes(AsyncWebServer& server);`
  - `void registerStatusRoutes(AsyncWebServer& server, AsyncWebSocket& ws);`
- [ ] не делать глобальную магию через конструкторы и auto-registration

### 4.3 Shared state

- [ ] заранее определить единый shared-layer:
  - `g_state`
  - `g_settings`
  - `g_energyHistory`
  - `ws`
  - служебные singleton-like вызовы (`Logger`, `Safety`, `Profiles`, `CloudTunnel`, `MQTT`, `ControlV2`)
- [ ] не плодить одинаковые `extern`-объявления по всем файлам хаотично
- [ ] сделать один понятный shared header

### 4.4 Helpers

- [ ] вынести общие helpers по смыслу, а не “куда влезло”
- [ ] не держать JSON / preflight / safety / stirrer helpers вперемешку
- [ ] если helper нужен более чем одному модулю, выносить его в общий shared/helper слой

---

## 5. Риски, которые нужно держать в голове

- [ ] скрытые зависимости через `static`-функции в верхней части `webserver.cpp`
- [ ] stateful-логика pump calibration и equipment testing
- [ ] POST body handlers `ESPAsyncWebServer`
- [ ] OTA-upload маршрут и асинхронный upload callback
- [ ] WebSocket broadcast и периодический full/fast packet
- [ ] дублирование JSON helper-кода при неаккуратном переносе
- [ ] случайный дрейф API-ответов при выносе маршрутов

---

## 6. Этап 0. Подготовка

Цель: сделать инфраструктуру, не ломая поведение.

- [x] создать `src/interface/api/`
- [x] создать `src/interface/api/api_routes.h`
- [x] создать `src/interface/webserver_shared.h`
- [x] определить в `webserver_shared.h`:
  - forward declarations
  - shared externs
  - список общих helper contracts, если нужно
- [x] убедиться, что текущий `webserver.cpp` собирается без функциональных изменений после подготовки include-слоя

Критерий готовности:
- [x] проект собирается без изменений поведения
- [x] инфраструктура для следующих этапов уже существует

---

## 7. Этап 1. Самые безопасные выносы

Цель: быстро уменьшить размер монолита с минимальным риском.

### 7.1 `api_health.cpp`

- [x] вынести `/api/health`
- [x] вынести `/api/version`
- [ ] проверить, что payload версии не изменился

### 7.2 `api_logs.cpp`

- [x] вынести `/api/logs/events`
- [x] вынести `/api/logs/events/clear`
- [x] вынести `/api/export`
- [ ] проверить очистку системного журнала

### 7.3 `api_charts.cpp`

- [x] вынести `/api/charts/live`
- [x] вынести `/api/charts/live/reset`
- [ ] проверить работу live history reset

### 7.4 `api_energy.cpp`

- [x] вынести `/api/energy`
- [ ] проверить payload энергомониторинга

### 7.5 `api_ota.cpp`

- [x] вынести `/update` GET
- [x] вынести upload-handler OTA
- [ ] проверить, что форма OTA всё ещё открывается
- [ ] проверить, что upload firmware остаётся рабочим

### 7.6 `api_wifi.cpp`

- [x] вынести `/api/wifi/scan`
- [x] вынести `/api/wifi/status`
- [x] вынести `/api/wifi/profiles` и связанные POST handlers
- [ ] проверить reorder / delete / connect

Критерий готовности этапа:
- [x] `webserver.cpp` стал заметно меньше
- [x] все перечисленные маршруты собираются из отдельных модулей
- [ ] ручная smoke-проверка пройдена

---

## 8. Этап 2. Stateful сервисные модули

Цель: вынести блоки, у которых есть собственное runtime-состояние.

### 8.1 `api_pump.cpp`

- [x] вынести `PumpCalibrationSession`
- [x] вынести `/api/pump/calibrate/start`
- [x] вынести `/api/pump/calibrate/stop`
- [x] вынести `/api/pump/calibrate/cancel`
- [x] вынести `/api/pump/start`
- [x] вынести `/api/pump/stop`
- [x] вынести `/api/pump/status`
- [x] вынести `/api/pump/diag`
- [ ] проверить, что состояние калибровки не теряется

### 8.2 `api_testing.cpp`

- [x] вынести `EquipmentTestingAction`
- [x] вынести `g_equipmentTestingActions`
- [x] вынести `g_equipmentTestingActionCount`
- [x] вынести `g_equipmentTestingActionNext`
- [x] вынести tone/log/history helpers equipment testing
- [x] вынести `/api/testing/status`
- [x] вынести `/api/testing/stop-all`
- [x] вынести сервисные тест-маршруты насос / мешалка / нагрев / клапаны / servo
- [ ] проверить историю и системный лог equipment testing

### 8.3 `api_calibration.cpp`

- [x] вынести `/api/calibration`
- [x] вынести `/api/calibration/pump`
- [x] вынести `/api/calibration/temp`
- [x] вынести pressure/arеometer calibration handlers
- [x] вынести `/api/calibration/scan`
- [x] вынести `/api/calibration/scan/raw`
- [ ] проверить DS18B20 scan и pressure calibration table

Критерий готовности этапа:
- [x] stateful части больше не живут в `webserver.cpp`
- [ ] калибровка и сервисный workbench не сломаны

---

## 9. Этап 3. История и профили

### 9.1 `api_history.cpp`

- [x] вынести `/api/history` GET
- [x] вынести `/api/history` DELETE
- [x] вынести `/api/history/:id` GET
- [x] вынести `/api/history/:id` DELETE
- [x] вынести `/api/history/:id/export`
- [x] вынести `/api/history/demo` POST/DELETE
- [ ] проверить export/import history-потоки, если есть body-handlers

### 9.2 `api_profiles.cpp`

- [x] вынести `/api/profiles` GET
- [x] вынести `/api/profiles` POST
- [x] вынести `/api/profiles` DELETE
- [x] вынести `/api/profiles/:id` GET
- [x] вынести `/api/profiles/:id` PUT
- [x] вынести `/api/profiles/:id` DELETE
- [x] вынести `/api/profiles/:id/load`
- [x] вынести `/api/profiles/:id/export`
- [x] вынести `/api/profiles/export`
- [x] вынести `/api/profiles/import`
- [ ] проверить profile CRUD из Web UI

Критерий готовности этапа:
- [x] история и профили независимы от остального маршрутизатора
- [ ] импорт/экспорт живы

---

## 10. Этап 4. Управление процессом и safety

### 10.1 `api_process.cpp`

- [ ] вынести `/api/process/preflight`
- [ ] вынести `/api/process/start`
- [ ] вынести `/api/process/stop`
- [ ] вынести `/api/process/pause`
- [ ] вынести `/api/process/resume`
- [ ] вынести route abort / emergency stop, если есть
- [ ] вынести `/api/stirrer/stop`
- [ ] вынести прочие stirrer control endpoints
- [ ] проверить старт/пауза/возобновление/останов

### 10.2 `api_safety.cpp`

- [ ] вынести `/api/safety/ack`
- [ ] вынести `/api/safety/reset`
- [ ] вынести cloud-claim/config, если логически остаются здесь, либо решить отдельный `api_cloud.cpp`
- [ ] проверить safety ack/reset flow

Критерий готовности этапа:
- [ ] запуск режимов и safety flow не зависят от монолитного файла
- [ ] preflight работает как до рефакторинга

---

## 11. Этап 5. Settings

Первый проход: один файл `api_settings.cpp`, без сверхдробления.

- [ ] вынести `/api/settings/equipment` GET/POST
- [ ] вынести `/api/settings/safety` GET/POST
- [ ] вынести `/api/settings/security` GET/POST
- [ ] вынести `/api/settings/nbk` GET/POST
- [ ] вынести `/api/settings/fermentation` GET/POST
- [ ] вынести `/api/settings/stirrer` GET/POST
- [ ] вынести `/api/settings/mqtt` GET/POST
- [ ] вынести `/api/settings/mqtt/test`
- [ ] вынести `/api/settings/rect` GET/POST
- [ ] вынести `/api/settings/demo` GET/POST
- [ ] вынести `/api/reboot/status`
- [ ] вынести `/api/reboot`
- [ ] вынести ручные runtime endpoints:
  - `/api/manual/heater`
  - `/api/rect/heater`
  - `/api/manual/pump`
  - `/api/manual/valves`
  - `/api/manual/phase`
  - `/api/manual/volumes`

Второй проход, если понадобится:
- [ ] разделить `api_settings.cpp` на несколько settings-подмодулей

Критерий готовности этапа:
- [ ] настройки полностью живут вне координатора
- [ ] Web UI настроек работает без регрессий

---

## 12. Этап 6. Самый сложный вынос: status и live layer

### 12.1 `api_status.cpp`

- [ ] вынести `/api/status`
- [ ] вынести `fillTemperatureTopologyJson`
- [ ] вынести `fillTemperatureModeSupportJson`
- [ ] вынести `buildBlockingRequiredSensorsList`
- [ ] вынести `buildMissingRequiredSensorsList`
- [ ] вынести `buildStartupMissingSensorsList`
- [ ] вынести helper-цепочку preflight / topology / mode-support
- [ ] аккуратно проверить payload `/api/status` побайтно или по ключам

### 12.2 `web_live.cpp`

- [ ] вынести `broadcastState(const SystemState&)`
- [ ] вынести `broadcastEvent(const char*, const char*)`
- [ ] если нужно, вынести shared JSON fillers:
  - `fillAlarmJson`
  - `fillV2StatusJson`
  - `fillSafetyActionV2Json`
  - `fillStirrerJson`
- [ ] проверить websocket fast/full broadcasts

Критерий готовности этапа:
- [ ] `webserver.cpp` остаётся координатором
- [ ] `/api/status` и websocket live layer работают как раньше

---

## 13. Этап 7. Финальная уборка координатора

- [ ] оставить в `webserver.cpp` только:
  - создание `server`
  - создание `ws`
  - middleware
  - captive-portal endpoints `/`, `/generate_204`, `/hotspot-detect.html`, `/connecttest.txt`
  - вызовы `registerXxxRoutes(...)`
  - `onNotFound`
  - `server.begin()`
- [ ] убрать оставшиеся лишние `static` helpers
- [ ] привести include-порядок к понятному виду
- [ ] убрать дублирующиеся helper declarations

Критерий готовности этапа:
- [ ] `webserver.cpp` стал маленьким и понятным
- [ ] архитектура читается с первого открытия файла

---

## 14. Проверки после каждого этапа

### Компиляция

- [ ] `npm run build`
- [x] `pio run -e esp32s3`

### Минимальная ручная smoke-проверка

- [ ] `/api/version`
- [ ] `/api/health`
- [ ] `/api/status`
- [ ] websocket соединение
- [ ] главная страница Web UI открывается и обновляется

### По этапам

- [ ] Wi-Fi: scan / profiles / connect
- [ ] OTA: GET `/update` + upload
- [ ] calibration: temp / pressure / scan
- [ ] testing: stop-all / test actions
- [ ] profiles: CRUD / load / export / import
- [ ] history: list / details / export / clear
- [ ] process: preflight / start / pause / resume / stop
- [ ] safety: ack / reset

---

## 15. Технические долги, которые удобно закрыть рядом

Это не часть первого обязательного прохода, но рядом будет удобно заметить и почистить.

- [ ] убрать дубли helper-объявлений в начале файла
- [ ] унифицировать naming register-функций
- [ ] выделить response helper для JSON success/error
- [ ] выделить body-parse helper для POST маршрутов
- [ ] привести регулярочные route-handlers к одному стилю
- [ ] подготовить базу под unit-level тестирование JSON builders, если позже пойдём туда

---

## 16. Приоритет этапов

### Высокий приоритет

- [x] Этап 0
- [ ] Этап 1
- [ ] Этап 2

### Средний приоритет

- [ ] Этап 3
- [ ] Этап 4
- [ ] Этап 5

### Самый рискованный, в конце

- [ ] Этап 6
- [ ] Этап 7

---

## 17. Оценка времени

Реалистично для аккуратного прохода:

- [ ] минимальный полезный результат: `1-2` рабочих дня
- [ ] нормальный безопасный проход: `3-4` рабочих дня
- [ ] полностью полированный проход: `4-6` рабочих дней

Безопасный режим работы:
- [ ] не тащить этот рефакторинг параллельно с крупными изменениями в web UI
- [ ] делать мелкими итерациями с обязательной сборкой после каждого подэтапа
- [ ] коммитить после каждого завершённого модуля

---

## 18. Definition of Done

Рефакторинг можно считать завершённым, когда:

- [ ] `webserver.cpp` больше не монолитный
- [ ] маршруты разнесены по доменам
- [ ] shared helpers не размазаны случайно
- [ ] live websocket слой отделён от HTTP registration
- [ ] проект собирается и OTA-заливается без сюрпризов
- [ ] Web UI, запуск режимов, история, профили, калибровка и OTA проходят ручную smoke-проверку
- [ ] по git-истории видно понятные шаги, а не один гигантский коммит

---

## 19. Notes

- Файл составлен как рабочий чеклист под реальный репозиторий, а не как абстрактный “идеальный” план.
- Первый проход должен быть консервативным: перенос без смены поведения.
- `api_status.cpp` и `web_live.cpp` специально оставлены на конец, потому что это самые зависимые и самые регрессионно-опасные части.
