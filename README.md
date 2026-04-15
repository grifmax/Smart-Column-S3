# Smart-Column S3

> Контроллер автоматизации ректификационной колонны на ESP32-S3

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/framework-Arduino-00979D.svg)](https://www.arduino.cc/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![MQTT](https://img.shields.io/badge/MQTT-supported-green.svg)](docs/HOME_ASSISTANT.md)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-ready-blue.svg)](docs/HOME_ASSISTANT.md)
[![Version](https://img.shields.io/badge/firmware-v2.1.18-brightgreen.svg)](CHANGELOG.md)

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
