# Smart-Column S3

> Контроллер автоматизации ректификационной колонны на ESP32-S3

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/framework-Arduino-00979D.svg)](https://www.arduino.cc/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![MQTT](https://img.shields.io/badge/MQTT-supported-green.svg)](docs/HOME_ASSISTANT.md)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-ready-blue.svg)](docs/HOME_ASSISTANT.md)
[![Version](https://img.shields.io/badge/firmware-v2.2.21-brightgreen.svg)](CHANGELOG.md)

## Возможности

- **8 режимов работы**: авто-ректификация, ручная ректификация, дистилляция, затирание, Hold (пастеризация), НБК, ферментация, +IDLE
- **Web UI** с анимированной SVG-схемой, TFT-дисплей 3.5\" ILI9488
- **Watt-Control** — автоподбор мощности по давлению колонны
- **Smart Decrement** — адаптивное снижение скорости отбора
- **Электронный ареометр** на MPX5010DP + ADS1115
- **Мешалка куба 0-10В** — управление через MCP4725 DAC + MCP6001 Op-Amp
- **MQTT / Home Assistant** — автообнаружение, Energy Dashboard, push-уведомления
- **История процессов v2** — сохранение, просмотр, сравнение, экспорт CSV/JSON, safety timeline
- **Профили процессов** — встроенные рецепты и пользовательские конфигурации
- **Безопасность** — 10+ типов проверок, HTTP Basic Auth, Rate Limiting
- **OTA** — обновление прошивки по воздуху
- **Cloud Tunnel** — удалённый доступ через spiritcontrol.ru
- **Управление оборудованием** — сервисный экран тестирования и калибровки через Web UI

## Оборудование

| Категория | Компоненты |
|-----------|------------|
| Контроллер | ESP32-S3 DevKitC-1 N16R8 (16MB Flash, 8MB PSRAM) |
| Датчики T | DS18B20 ×7 (куб, царга низ/верх, дефлегматор, ТСА, вода вход/выход) |
| Давление | MPX5010DP + ADS1115, BMP280 ×2 (куб + атмосфера) |
| Мощность | SSR-40DA + PZEM-004T v3.0 |
| Насос | NEMA 17 + TMC2209 (перистальтика) |
| Клапаны | 12V NC ×3 + MOSFET (вода, головы, УНО) |
| Дисплей | 3.5\" TFT ILI9488 (SPI, touch XPT2046) |
| **Мешалка (новое)** | **MCP4725 (I2C DAC) + MCP6001 (Op-Amp ×3) → 0-10В** |

Полная распиновка и BOM — см. [SPEC.md](SPEC.md).

## Быстрый старт

```bash
git clone https://github.com/grifmax/Smart-Column-S3.git
cd Smart-Column-S3

# Первая прошивка
pio run -e esp32s3 -t upload    # Прошивка
pio run -e esp32s3 -t uploadfs  # Загрузка Web UI (LittleFS)

# Только сборка (без прошивки)
pio run -e esp32s3

# OTA (по сети)
pio run -e esp32s3_ota -t upload
```

После прошивки ESP32 создаст точку доступа **Smart-Column-S3** (пароль `12345678`).
Откройте `http://192.168.4.1` для первоначальной настройки WiFi/MQTT.

> **PlatformIO CLI:** если `pio` не в PATH, используйте полный путь:  
> `C:\Users\<user>\AppData\Local\Programs\Python\Python39\Scripts\pio.exe`

## Структура проекта

```
Smart-Column-S3/
├── src/
│   ├── main.cpp
│   ├── config.h                 # Пины, константы, NVS-ключи, версия прошивки
│   ├── pins_config.h            # Назначение всех GPIO
│   ├── types.h                  # Типы данных, перечисления, структуры состояния
│   ├── control/                 # FSM, safety, watt_control, demo_simulator
│   │   ├── fsm.cpp              # Конечный автомат
│   │   ├── safety.cpp           # Аварийная защита
│   │   ├── modes/               # Обработчики каждого режима
│   │   └── v2/                  # Архитектура v2: reason codes, status adapter
│   ├── drivers/                 # Драйверы оборудования
│   │   ├── sensors.cpp          # DS18B20, BMP280, ADS1115, PZEM
│   │   ├── heater.cpp           # SSR/PWM управление ТЭНом
│   │   ├── pump.cpp             # TMC2209 шаговик (FreeRTOS task)
│   │   ├── valves.cpp           # Клапаны + сервопривод-фракционник
│   │   ├── stirrer.cpp          # Мешалка 0-10В (MCP4725 DAC)  ← NEW
│   │   └── display.cpp          # TFT ILI9488 / OLED SSD1306
│   ├── interface/               # Внешние интерфейсы
│   │   ├── webserver.cpp        # AsyncWebServer + WebSocket + REST API
│   │   ├── mqtt.cpp             # MQTT + HA Discovery
│   │   ├── security.cpp         # HTTP Auth + Rate Limiting
│   │   ├── cloud_tunnel.cpp     # Cloud API proxy
│   │   └── wifi_profiles.cpp    # Управление WiFi-профилями
│   └── storage/                 # Хранение данных
│       ├── nvs_manager.cpp      # Настройки в NVS
│       └── logger.cpp           # Логи в LittleFS
├── src/web/                     # Исходники Web UI (JS/CSS, собираются → data/)
├── data/                        # LittleFS — Web UI (html/js/css/svg)
├── docs/                        # Документация
│   ├── API.md                   # REST API + WebSocket
│   ├── HOME_ASSISTANT.md        # Интеграция с Home Assistant
│   ├── HISTORY_SCHEMA.md        # Схема хранения истории процессов
│   └── PROFILES_SCHEMA.md       # Схема профилей
├── cloud_proxy/                 # PHP-прокси spiritcontrol.ru
├── cloud_tunnel_service/        # Node.js WSS-туннель ESP32 ↔ cloud
├── tools/ui-smoke/              # Playwright E2E тесты Web UI
├── scripts/                     # Скрипты сборки (build_web.py и др.)
├── CHANGELOG.md                 # Журнал всех изменений
├── SPEC.md                      # Техническая спецификация
└── platformio.ini
```

## API

Полная документация: **[docs/API.md](docs/API.md)**

```http
GET  /api/status                # Полный статус системы
GET  /api/health                # Здоровье подсистем
POST /api/process/start         # Запуск режима
POST /api/process/stop          # Остановка
POST /api/process/pause         # Пауза
POST /api/process/resume        # Возобновление
POST /api/stirrer/start         # Запуск мешалки (новое)
POST /api/stirrer/stop          # Остановка мешалки (новое)
POST /api/stirrer/set           # Изменение скорости мешалки (новое)
POST /api/testing/stirrer       # Сервисный тест мешалки
GET  /api/history               # История процессов
WS   ws://<ip>/ws               # WebSocket (2с быстрый / 10с полный)
```

## Документация

| Документ | Описание |
|----------|----------|
| [SPEC.md](SPEC.md) | Техническая спецификация v2, BOM, распиновка, формулы |
| [docs/API.md](docs/API.md) | REST API, WebSocket, MQTT |
| [docs/HOME_ASSISTANT.md](docs/HOME_ASSISTANT.md) | Интеграция с Home Assistant |
| [docs/HISTORY_SCHEMA.md](docs/HISTORY_SCHEMA.md) | Схема хранения истории процессов |
| [docs/PROFILES_SCHEMA.md](docs/PROFILES_SCHEMA.md) | Схема профилей процессов |
| [CHANGELOG.md](CHANGELOG.md) | Журнал изменений |
| [TODO2.0.md](TODO2.0.md) | Роадмап разработки |

## Версии прошивки

| Версия | Дата | Ключевые изменения |
|--------|------|-------------------|
| 2.2.21 | 2026-04-19 | Added TFT HMI widgets (`SparklineBuffer`, `HMIIndicators`), fixed their integration in `display.cpp`, and finished mode-specific dashboard summary blocks for manual rectification, mashing, and hold |
| 2.2.20 | 2026-04-18 | Reworked the TFT `DASHBOARD` left summary panel into the same hero-style operator block layout, with one dominant value and three compact support rows |
| 2.2.19 | 2026-04-18 | Reworked the TFT left summary panels for rectification, distillation, and NBK into hero-style operator blocks with one primary value and three supporting rows |
| 2.2.18 | 2026-04-18 | Rebalanced TFT runtime hierarchy: main process metrics on dashboard and monitor screens are larger, tile headers are slimmer, and footer/status copy is visually quieter |
| 2.2.17 | 2026-04-18 | Tightened TFT monitor and mode-monitor spacing to the same 3 px gap system, reduced gaps between summary and metric panels, and synced the matching monitor touch zones |
| 2.2.16 | 2026-04-18 | Tightened TFT button spacing to a 3 px gap standard across `CONTROL`, footer tabs, settings toggles, manual valves, value-edit step buttons, and synced the matching touch hitboxes |
| 2.2.15 | 2026-04-17 | Unified compact TFT copy for `SETTINGS`/`RECT_PARAMS`/`SERVICE`, shortened secondary panel titles and hints, and cleaned the manual-lock overlay text |
| 2.2.14 | 2026-04-17 | Fixed TFT `SETTINGS` hitboxes to match the new 2x2 layout and resynced firmware/docs/web asset versions after `2.2.13` |
| 2.2.12 | 2026-04-17 | На TFT добиты подэкраны настроек: `EQUIPMENT`, `RECT_PARAMS` и `DIST_PARAMS` переведены на плотную плиточную HMI-сетку с новыми hitbox и page-strip для тех/профильных параметров |
| 2.2.11 | 2026-04-17 | На TFT уплотнены `CALIBRATION` и `VALUE_EDIT`: калибровка переведена на 2-panel layout, редактор значения — на value-плитку, компактный ряд шаговых кнопок и отдельный save |
| 2.2.10 | 2026-04-17 | На TFT уплотнены `MANUAL` и `SERVICE`: ручной экран переведён на 2 live-плитки + ряд клапанов, сервисный экран — на 2x2 diagnostics-плитки с нижним diag-блоком |
| 2.2.9 | 2026-04-17 | Для TFT введён компактный HMI-словарь: короткие названия режимов/фаз, укороченные русские подписи и более жёсткие подсказки в `CONTROL`, `SETTINGS`, `SERVICE` и fallback-monitor |
| 2.2.8 | 2026-04-17 | Для TFT добавлен общий text-fit слой: длинные русские подписи и статусы теперь ужимаются в shared-примитивах и не разламывают прямоугольный HMI-layout |
| 2.2.7 | 2026-04-17 | Добит modal/runtime слой TFT: confirm overlay смены режима и locked-state ручного экрана переведены на общий panel-overlay, а `SERVICE` теперь явнее сигнализирует slow/watchdog/hard recovery состояния |
| 2.2.6 | 2026-04-17 | Вторичные TFT-сцены добиты в том же HMI-стиле: touch-calibration, boot splash, системные сообщения и error overlays переведены на общий fullscreen panel/overlay язык |
| 2.2.5 | 2026-04-17 | Вложенные TFT-экраны `SETTINGS`/`SERVICE` и `ALL TEMPS` приведены к тому же прямоугольному HMI-языку: строковые панели параметров, компактные подсказки и исправленная сетка температур без налезания на footer |
| 2.2.4 | 2026-04-17 | Базовый TFT/HMI стиль переписан: новая палитра, прямоугольные промышленные кнопки, обновлённые header/tabs/cards/value-tiles и более строгая цветовая кодировка `CONTROL`/`SETTINGS` |
| 2.2.3 | 2026-04-17 | HMI-редизайн IPS UI продолжен для оставшихся custom monitor режимов: `MANUAL_RECT`, `MASHING` и `HOLD` переведены на тот же root-monitor каркас с левой summary-панелью и правым step/grid layout |
| 2.2.2 | 2026-04-17 | HMI-редизайн IPS UI продолжен для custom monitor: операторские панели добавлены для дистилляции, НБК и ферментации, а общий root-monitor переведен на единый status/footer каркас |
| 2.2.1 | 2026-04-17 | Второй HMI-срез IPS UI: dashboard и экран ректификации переведены на операторскую панель с левой summary-зоной и унифицированной 2x3 метрикой |
| 2.2.0 | 2026-04-16 | Первый HMI-редизайн IPS UI: прямоугольные панели, полный список режимов, monitor-layouts для НБК и ферментации |
| 2.1.21 | 2026-04-16 | Policy ручного управления мешалкой только в IDLE, блокировка UI и smoke-покрытие |
| 2.1.20 | 2026-04-15 | Главный виджет мешалки, настройки в оборудовании и сервисный тест |
| 2.1.19 | 2026-04-15 | REST API и NVS-настройки мешалки, синхронизация версии frontend |
| 2.1.18 | 2026-04-14 | Мешалка куба 0-10В (MCP4725 + MCP6001) |
| 2.1.17 | 2026-03-18 | UI настройки sidebar, тестирование оборудования |
| 2.1.0  | 2026-03-16 | Удалён Telegram-модуль |
| 2.0.0  | 2026-03-15 | Архитектура v2: reason codes, status adapter, history v2 |
| 1.13.x | 2026-03-14 | Калибровка насоса, Health matrix, TRIAC |

Полный CHANGELOG: **[CHANGELOG.md](CHANGELOG.md)**

## Предупреждение

Система предназначена для автоматизации процесса ректификации. Соблюдайте правила пожарной и электробезопасности. Не оставляйте работающую систему без присмотра. Работа с горючими жидкостями требует соответствующих мер защиты.

## Лицензия

MIT — см. [LICENSE](LICENSE)
