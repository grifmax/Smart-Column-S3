# Smart-Column S3

> Контроллер автоматизации ректификационной колонны на ESP32-S3

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/framework-Arduino-00979D.svg)](https://www.arduino.cc/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![MQTT](https://img.shields.io/badge/MQTT-supported-green.svg)](docs/HOME_ASSISTANT.md)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-ready-blue.svg)](docs/HOME_ASSISTANT.md)

## Возможности

- **5 режимов**: авто-ректификация, ручная ректификация, дистилляция, затирка, hold
- **Web UI** с анимированной SVG-схемой, TFT-дисплей 3.5" ILI9488
- **Watt-Control** -- автоподбор мощности по давлению колонны
- **Smart Decrement** -- адаптивное снижение скорости отбора
- **Электронный ареометр** на MPX5010DP + ADS1115
- **MQTT / Home Assistant** -- автообнаружение, Energy Dashboard, push-уведомления
- **Telegram-бот** (FastBot2) -- уведомления и удалённое управление
- **История процессов** -- сохранение, просмотр, сравнение, экспорт CSV/JSON
- **Профили процессов** -- встроенные рецепты и пользовательские конфигурации
- **Безопасность** -- 10 типов проверок, HTTP Basic Auth, Rate Limiting
- **OTA** -- обновление прошивки по воздуху

## Оборудование

| Категория | Компоненты |
|-----------|------------|
| Контроллер | ESP32-S3 DevKitC-1 N16R8 |
| Датчики T | DS18B20 x7 (куб, царга, дефлегматор, ТСА, вода) |
| Давление | MPX5010DP + ADS1115, BMP280 x2 |
| Мощность | SSR-40DA + PZEM-004T v3.0 |
| Насос | NEMA 17 + TMC2209 (перистальтика) |
| Клапаны | 12V NC x3 + MOSFET |
| Дисплей | 3.5" TFT ILI9488 (SPI, touch) |

Полная распиновка и BOM -- см. [SPEC.md](SPEC.md).

## Быстрый старт

```bash
git clone https://github.com/grifmax/Smart-Column-S3.git
cd Smart-Column-S3

pio run -e esp32s3              # Сборка
pio run -e esp32s3 -t upload    # Прошивка
pio run -e esp32s3 -t uploadfs  # Загрузка Web UI
```

После прошивки ESP32 создаст точку доступа **Smart-Column-S3** (пароль `12345678`).
Откройте `http://192.168.4.1` и настройте WiFi/MQTT/Telegram.

## Структура проекта

```
Smart-Column-S3/
├── src/
│   ├── main.cpp
│   ├── config.h                # Пины, константы
│   ├── control/                # FSM, watt_control, safety
│   ├── drivers/                # sensors, heater, pump, valves, display
│   ├── interface/              # webserver, telegram, mqtt, buttons
│   └── storage/                # nvs_manager, logger
├── src/web/                    # Исходники Web UI (JS/CSS, собираются в data/)
├── data/                       # SPIFFS -- Web UI (html/js/css/svg)
├── docs/                       # API.md, HOME_ASSISTANT.md, схемы данных
├── cloud_proxy/                # PHP-прокси spiritcontrol.ru
├── cloud_tunnel_service/       # Node.js WSS-туннель ESP32 <-> cloud
├── android_app/                # Flutter приложение
├── tools/ui-smoke/             # Playwright E2E тесты Web UI
└── platformio.ini
```

## API

Полная документация: **[docs/API.md](docs/API.md)**

```http
GET  /api/status                # Полный статус
POST /api/process/start         # Запуск режима
POST /api/process/stop          # Остановка
POST /api/process/pause         # Пауза
WS   ws://<ip>/ws               # WebSocket (fast 2s / full 10s)
```

## Документация

| Документ | Описание |
|----------|----------|
| [SPEC.md](SPEC.md) | Техническая спецификация, BOM, распиновка, формулы |
| [docs/API.md](docs/API.md) | REST API, WebSocket, MQTT |
| [docs/HOME_ASSISTANT.md](docs/HOME_ASSISTANT.md) | Интеграция с Home Assistant |
| [docs/HISTORY_SCHEMA.md](docs/HISTORY_SCHEMA.md) | Схема хранения истории процессов |
| [docs/PROFILES_SCHEMA.md](docs/PROFILES_SCHEMA.md) | Схема хранения профилей |
| [CHANGELOG.md](CHANGELOG.md) | Журнал изменений |
| [TODO2.0.md](TODO2.0.md) | Роадмап разработки |

## Предупреждение

Система предназначена для автоматизации процесса ректификации. Соблюдайте правила пожарной и электробезопасности. Не оставляйте работающую систему без присмотра.

## Лицензия

MIT -- см. [LICENSE](LICENSE)
