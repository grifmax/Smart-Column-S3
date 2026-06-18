# Smart-Column S3

> Контроллер автоматики колонны и смежных процессов на ESP32-S3

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/framework-Arduino-00979D.svg)](https://www.arduino.cc/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![MQTT](https://img.shields.io/badge/MQTT-supported-green.svg)](docs/HOME_ASSISTANT.md)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-ready-blue.svg)](docs/HOME_ASSISTANT.md)
[![Version](https://img.shields.io/badge/firmware-v2.4.5-brightgreen.svg)](CHANGELOG.md)

## Что это

Smart-Column S3 - это прошивка и Web UI для автоматики на базе ESP32-S3. Проект управляет нагревом, водой, насосом, клапанами, мешалкой, журналированием, историей прогонов, профилями и удалённым доступом.

Система рассчитана на работу с планшета, телефона или ноутбука через Web UI. TFT-дисплей остаётся локальной панелью контроля и ручного управления.

Если нужен не только быстрый старт, а нормальное человеческое описание проекта, архитектуры и рабочего сценария, смотрите [docs/PROJECT_GUIDE.md](docs/PROJECT_GUIDE.md).

## Ключевые возможности

- 8 режимов работы: авто-ректификация, ручная ректификация, дистилляция, затирание, Hold, НБК, ферментация и IDLE
- Web UI с живой SVG-схемой, показометрами, диагностикой, историей и профилями
- Web UI профилей теперь умеет не только смотреть/экспортировать, но и редактировать пользовательские профили, делать копии встроенных рецептов, искать по базе и импортировать/экспортировать profile snapshots с предварительной проверкой совместимости прямо из браузера
- Web UI калибровок теперь включает рабочую таблицу ареометра: текущий сигнал, точки ABV и сохранение в контроллер без ручных JSON-правок
- Web UI калибровок теперь умеет и backup/restore полного calibration snapshot: насос, offsets термодатчиков, pressure sensor и ареометр
- Web UI калибровки термодатчиков теперь показывает живые адреса `DS18B20`, умеет вручную привязывать роль к конкретному датчику и сохраняет эту карту в контроллере
- Для охлаждения появился первый шаг к пропорциональному контуру: опциональный PWM-канал с настройкой окна `min/max duty`, стартовой подачей и сервисным ручным тестом прямо из Web UI
- История процессов v2: фазы, таймлайн safety, reason codes, экспорт CSV/JSON
- Run Advisor v1: post-run отчёт по фазам, энергии, устойчивости, safety, baseline-сравнению и короткому плану следующего запуска
- Профили с validation context: давление, конфигурация колонны, насадка, мощность и фактические cut points последнего успешного baseline
- Мягкая отключаемая барокоррекция температурных порогов профиля по текущему атмосферному давлению
- `Pre-flight` мастер теперь заранее показывает, будет ли применена барокоррекция к порогам активного профиля именно в этом запуске
- В блоке ректификации Web UI показывается живая адаптация рецепта: baseline профиля, текущее давление и итоговое поведение порогов для этого запуска
- Process Indicators v2: инженерные признаки процесса и объяснения состояний оператору
- Watt-Control и Smart Decrement для управления нагревом и отбором
- Основной `ADS1115` (`0x48`): электронный ареометр на `A0`, давление куба на `A1`, резервные safety-каналы на `A2/A3`
- Второй `ADS1115` (`0x49`) заранее зарезервирован под датчики уровня приёмных ёмкостей и будущие аналоговые каналы
- Мешалка куба 0-10В на MCP4725 DAC + MCP6001
- MQTT и Home Assistant Discovery
- OTA-обновление, HTTP Basic Auth, Rate Limiting
- Cloud Tunnel и сервис удалённого доступа

## Какой в этом смысл

Smart-Column S3 задуман не как "ещё одна автоматика", а как платформа повторяемого процесса:

- автоматика не только включает исполнительные механизмы, но и объясняет оператору текущее состояние
- каждый прогон сохраняется в историю с фазами, safety-событиями и indicators
- профили становятся baseline для следующих запусков, а не просто набором цифр
- Web UI — главный рабочий интерфейс, TFT — локальный инженерный пульт рядом с установкой

## Как устроен проект

- `src/control/` — режимы, FSM, indicators v2, safety
- `src/drivers/` — железо: датчики, насос, клапаны, нагрев, мешалка, дисплей
- `src/interface/` — webserver, MQTT, OTA, cloud tunnel, security
- `src/storage/` — NVS, logger, история, профили
- `src/web/` — исходники frontend
- `data/` — собранные web-ассеты для LittleFS

Более подробная карта проекта и рабочий сценарий описаны в [docs/PROJECT_GUIDE.md](docs/PROJECT_GUIDE.md).

## Аппаратная база

| Узел | Реализация |
|------|------------|
| Контроллер | ESP32-S3 DevKitC-1 N16R8 |
| Температура | DS18B20 |
| Давление и техдатчики | MPX5010DP, ADS1115 x2, BMP280 |
| Питание и мощность | SSR + PZEM-004T v3.0 |
| Насос | TMC2209 + шаговый двигатель |
| Клапаны | 12V NC + MOSFET |
| Локальный экран | 3.5" TFT ILI9488 + XPT2046 |
| Мешалка | MCP4725 + MCP6001, 0-10В |

Полный состав оборудования и pinout смотрите в [SPEC.md](SPEC.md).

## Быстрый старт

```bash
git clone https://github.com/grifmax/Smart-Column-S3.git
cd Smart-Column-S3

# Сборка прошивки
pio run -e esp32s3

# Заливка прошивки
pio run -e esp32s3 -t upload

# Заливка файловой системы с Web UI
pio run -e esp32s3 -t uploadfs

# OTA-загрузка
pio run -e esp32s3_ota -t upload
```

Если команда `pio` не найдена, используйте полный путь к PlatformIO CLI, например:

```powershell
C:\.platformio\penv\Scripts\pio.exe run -e esp32s3
```

После первой загрузки устройство поднимает точку доступа `Smart-Column-S3`. Дальнейшая настройка Wi-Fi, MQTT, профилей и режимов выполняется через Web UI.

## Первый рабочий сценарий

1. Собрать и залить прошивку с файловой системой.
2. Настроить Wi‑Fi, оборудование и safety-пороги.
3. Проверить датчики и исполнительные механизмы через сервисные страницы.
4. Загрузить профиль или вручную задать параметры режима.
5. Перед запуском пройти `Pre-flight` мастер.
6. Во время прогона следить за guidance, diagnostics и показометрами.
7. После завершения открыть историю и посмотреть `Run Advisor`.

## Структура проекта

```text
Smart-Column-S3/
|-- src/
|   |-- config.h
|   |-- control/
|   |-- drivers/
|   |-- interface/
|   |-- storage/
|   `-- web/
|-- data/                     # собранный Web UI для LittleFS
|-- docs/
|-- cloud_proxy/
|-- cloud_tunnel_service/
|-- scripts/
|-- tools/
|-- CHANGELOG.md
|-- TODO2.0.md
|-- SPEC.md
`-- platformio.ini
```

## Основные документы

- [CHANGELOG.md](CHANGELOG.md) - журнал версий и изменений
- [TODO2.0.md](TODO2.0.md) - актуальный список задач и roadmap
- [SPEC.md](SPEC.md) - оборудование, pinout и базовые схемы
- [docs/TRIAC_HEATER_WIRING.md](docs/TRIAC_HEATER_WIRING.md) - памятка по узлу нагрева `TRIAC + MOC3021/MOC3023 + zero-cross`
- [docs/PROJECT_GUIDE.md](docs/PROJECT_GUIDE.md) - нормальное описание проекта, архитектуры и сценария работы
- [docs/API.md](docs/API.md) - REST API и WebSocket
- [docs/HISTORY_SCHEMA.md](docs/HISTORY_SCHEMA.md) - структура истории процессов
- [docs/HOME_ASSISTANT.md](docs/HOME_ASSISTANT.md) - интеграция с Home Assistant
- [docs/TFT_MENU_MATRIX.md](docs/TFT_MENU_MATRIX.md) - матрица экранов и меню TFT

## Разработка

- После изменений в `src/web/` нужно выполнять `npm run build`
- После изменений в прошивке нужно выполнять `pio run -e esp32s3`
- Для значимых изменений версия поднимается в `src/config.h`, а запись добавляется в `CHANGELOG.md`
- Проект ведётся в UTF-8

## Лицензия

MIT, если в конкретном файле не указано иное.
