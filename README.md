# Smart-Column S3

> ���������� ������������� ���������������� ������� �� ESP32-S3

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/framework-Arduino-00979D.svg)](https://www.arduino.cc/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![MQTT](https://img.shields.io/badge/MQTT-supported-green.svg)](docs/HOME_ASSISTANT.md)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-ready-blue.svg)](docs/HOME_ASSISTANT.md)
[![Version](https://img.shields.io/badge/firmware-v2.2.25-brightgreen.svg)](CHANGELOG.md)

## �����������

- **8 ������� ������**: ����-������������, ������ ������������, �����������, ���������, Hold (������������), ���, �����������, +IDLE
- **Web UI** � ������������� SVG-������, TFT-������� 3.5\" ILI9488
- **Watt-Control** � ���������� �������� �� �������� �������
- **Smart Decrement** � ���������� �������� �������� ������
- **����������� ��������** �� MPX5010DP + ADS1115
- **������� ���� 0-10�** � ���������� ����� MCP4725 DAC + MCP6001 Op-Amp
- **MQTT / Home Assistant** � ���������������, Energy Dashboard, push-�����������
- **������� ��������� v2** � ����������, ��������, ���������, ������� CSV/JSON, safety timeline
- **������� ���������** � ���������� ������� � ���������������� ������������
- **������������** � 10+ ����� ��������, HTTP Basic Auth, Rate Limiting
- **OTA** � ���������� �������� �� �������
- **Cloud Tunnel** � �������� ������ ����� spiritcontrol.ru
- **���������� �������������** � ��������� ����� ������������ � ���������� ����� Web UI

## ������������

| ��������� | ���������� |
|-----------|------------|
| ���������� | ESP32-S3 DevKitC-1 N16R8 (16MB Flash, 8MB PSRAM) |
| ������� T | DS18B20 ?7 (���, ����� ���/����, �����������, ���, ���� ����/�����) |
| �������� | MPX5010DP + ADS1115, BMP280 ?2 (��� + ���������) |
| �������� | SSR-40DA + PZEM-004T v3.0 |
| ����� | NEMA 17 + TMC2209 (�������������) |
| ������� | 12V NC ?3 + MOSFET (����, ������, ���) |
| ������� | 3.5\" TFT ILI9488 (SPI, touch XPT2046) |
| **������� (�����)** | **MCP4725 (I2C DAC) + MCP6001 (Op-Amp ?3) > 0-10�** |

������ ���������� � BOM � ��. [SPEC.md](SPEC.md).

## ������� �����

```bash
git clone https://github.com/grifmax/Smart-Column-S3.git
cd Smart-Column-S3

# ������ ��������
pio run -e esp32s3 -t upload    # ��������
pio run -e esp32s3 -t uploadfs  # �������� Web UI (LittleFS)

# ������ ������ (��� ��������)
pio run -e esp32s3

# OTA (�� ����)
pio run -e esp32s3_ota -t upload
```

����� �������� ESP32 ������� ����� ������� **Smart-Column-S3** (������ `12345678`).
�������� `http://192.168.4.1` ��� �������������� ��������� WiFi/MQTT.

> **PlatformIO CLI:** ���� `pio` �� � PATH, ����������� ������ ����:  
> `C:\Users\<user>\AppData\Local\Programs\Python\Python39\Scripts\pio.exe`

## ��������� �������

```
Smart-Column-S3/
+-- src/
�   +-- main.cpp
�   +-- config.h                 # ����, ���������, NVS-�����, ������ ��������
�   +-- pins_config.h            # ���������� ���� GPIO
�   +-- types.h                  # ���� ������, ������������, ��������� ���������
�   +-- control/                 # FSM, safety, watt_control, demo_simulator
�   �   +-- fsm.cpp              # �������� �������
�   �   +-- safety.cpp           # ��������� ������
�   �   +-- modes/               # ����������� ������� ������
�   �   L-- v2/                  # ����������� v2: reason codes, status adapter
�   +-- drivers/                 # �������� ������������
�   �   +-- sensors.cpp          # DS18B20, BMP280, ADS1115, PZEM
�   �   +-- heater.cpp           # SSR/PWM ���������� �����
�   �   +-- pump.cpp             # TMC2209 ������� (FreeRTOS task)
�   �   +-- valves.cpp           # ������� + �����������-�����������
�   �   +-- stirrer.cpp          # ������� 0-10� (MCP4725 DAC)  < NEW
�   �   L-- display.cpp          # TFT ILI9488 / OLED SSD1306
�   +-- interface/               # ������� ����������
�   �   +-- webserver.cpp        # AsyncWebServer + WebSocket + REST API
�   �   +-- mqtt.cpp             # MQTT + HA Discovery
�   �   +-- security.cpp         # HTTP Auth + Rate Limiting
�   �   +-- cloud_tunnel.cpp     # Cloud API proxy
�   �   L-- wifi_profiles.cpp    # ���������� WiFi-���������
�   L-- storage/                 # �������� ������
�       +-- nvs_manager.cpp      # ��������� � NVS
�       L-- logger.cpp           # ���� � LittleFS
+-- src/web/                     # ��������� Web UI (JS/CSS, ���������� > data/)
+-- data/                        # LittleFS � Web UI (html/js/css/svg)
+-- docs/                        # ������������
�   +-- API.md                   # REST API + WebSocket
�   +-- HOME_ASSISTANT.md        # ���������� � Home Assistant
�   +-- HISTORY_SCHEMA.md        # ����� �������� ������� ���������
�   L-- PROFILES_SCHEMA.md       # ����� ��������
+-- cloud_proxy/                 # PHP-������ spiritcontrol.ru
+-- cloud_tunnel_service/        # Node.js WSS-������� ESP32 - cloud
+-- tools/ui-smoke/              # Playwright E2E ����� Web UI
+-- scripts/                     # ������� ������ (build_web.py � ��.)
+-- CHANGELOG.md                 # ������ ���� ���������
+-- SPEC.md                      # ����������� ������������
L-- platformio.ini
```

## API

������ ������������: **[docs/API.md](docs/API.md)**

```http
GET  /api/status                # ������ ������ �������
GET  /api/health                # �������� ���������
POST /api/process/start         # ������ ������
POST /api/process/stop          # ���������
POST /api/process/pause         # �����
POST /api/process/resume        # �������������
POST /api/stirrer/start         # ������ ������� (�����)
POST /api/stirrer/stop          # ��������� ������� (�����)
POST /api/stirrer/set           # ��������� �������� ������� (�����)
POST /api/testing/stirrer       # ��������� ���� �������
GET  /api/history               # ������� ���������
WS   ws://<ip>/ws               # WebSocket (2� ������� / 10� ������)
```

## ������������

| �������� | �������� |
|----------|----------|
| [SPEC.md](SPEC.md) | ����������� ������������ v2, BOM, ����������, ������� |
| [docs/API.md](docs/API.md) | REST API, WebSocket, MQTT |
| [docs/HOME_ASSISTANT.md](docs/HOME_ASSISTANT.md) | ���������� � Home Assistant |
| [docs/HISTORY_SCHEMA.md](docs/HISTORY_SCHEMA.md) | ����� �������� ������� ��������� |
| [docs/PROFILES_SCHEMA.md](docs/PROFILES_SCHEMA.md) | ����� �������� ��������� |
| [CHANGELOG.md](CHANGELOG.md) | ������ ��������� |
| [TODO2.0.md](TODO2.0.md) | ������� ���������� |

## ������ ��������

| ������ | ���� | �������� ��������� |
|--------|------|-------------------|
| 2.2.25 | 2026-04-19 | Restored built-in TFT Russian text rendering by repairing the broken UTF-8 literals in `display.cpp` and switching LovyanGFX text drawing back to native UTF-8 |
| 2.2.23 | 2026-04-19 | Fixed built-in TFT Russian text rendering by adding a local UTF-8 -> CP1251 adapter in the display layer, so Cyrillic labels no longer render as mojibake on the IPS screen |
| 2.2.22 | 2026-04-19 | Rebalanced the TFT dashboard left summary blocks for manual rectification, mashing, and hold so the hero block shows the current operator-critical value first and support data stays in compact rows |
| 2.2.21 | 2026-04-19 | Added TFT HMI widgets (`SparklineBuffer`, `HMIIndicators`), fixed their integration in `display.cpp`, and finished mode-specific dashboard summary blocks for manual rectification, mashing, and hold |
| 2.2.20 | 2026-04-18 | Reworked the TFT `DASHBOARD` left summary panel into the same hero-style operator block layout, with one dominant value and three compact support rows |
| 2.2.19 | 2026-04-18 | Reworked the TFT left summary panels for rectification, distillation, and NBK into hero-style operator blocks with one primary value and three supporting rows |
| 2.2.18 | 2026-04-18 | Rebalanced TFT runtime hierarchy: main process metrics on dashboard and monitor screens are larger, tile headers are slimmer, and footer/status copy is visually quieter |
| 2.2.17 | 2026-04-18 | Tightened TFT monitor and mode-monitor spacing to the same 3 px gap system, reduced gaps between summary and metric panels, and synced the matching monitor touch zones |
| 2.2.16 | 2026-04-18 | Tightened TFT button spacing to a 3 px gap standard across `CONTROL`, footer tabs, settings toggles, manual valves, value-edit step buttons, and synced the matching touch hitboxes |
| 2.2.15 | 2026-04-17 | Unified compact TFT copy for `SETTINGS`/`RECT_PARAMS`/`SERVICE`, shortened secondary panel titles and hints, and cleaned the manual-lock overlay text |
| 2.2.14 | 2026-04-17 | Fixed TFT `SETTINGS` hitboxes to match the new 2x2 layout and resynced firmware/docs/web asset versions after `2.2.13` |
| 2.2.12 | 2026-04-17 | �� TFT ������ ��������� ��������: `EQUIPMENT`, `RECT_PARAMS` � `DIST_PARAMS` ���������� �� ������� ��������� HMI-����� � ������ hitbox � page-strip ��� ���/���������� ���������� |
| 2.2.11 | 2026-04-17 | �� TFT ��������� `CALIBRATION` � `VALUE_EDIT`: ���������� ���������� �� 2-panel layout, �������� �������� � �� value-������, ���������� ��� ������� ������ � ��������� save |
| 2.2.10 | 2026-04-17 | �� TFT ��������� `MANUAL` � `SERVICE`: ������ ����� �������� �� 2 live-������ + ��� ��������, ��������� ����� � �� 2x2 diagnostics-������ � ������ diag-������ |
| 2.2.9 | 2026-04-17 | ��� TFT ����� ���������� HMI-�������: �������� �������� �������/���, ����������� ������� ������� � ����� ������ ��������� � `CONTROL`, `SETTINGS`, `SERVICE` � fallback-monitor |
| 2.2.8 | 2026-04-17 | ��� TFT �������� ����� text-fit ����: ������� ������� ������� � ������� ������ ��������� � shared-���������� � �� ����������� ������������� HMI-layout |
| 2.2.7 | 2026-04-17 | ����� modal/runtime ���� TFT: confirm overlay ����� ������ � locked-state ������� ������ ���������� �� ����� panel-overlay, � `SERVICE` ������ ����� ������������� slow/watchdog/hard recovery ��������� |
| 2.2.6 | 2026-04-17 | ��������� TFT-����� ������ � ��� �� HMI-�����: touch-calibration, boot splash, ��������� ��������� � error overlays ���������� �� ����� fullscreen panel/overlay ���� |
| 2.2.5 | 2026-04-17 | ��������� TFT-������ `SETTINGS`/`SERVICE` � `ALL TEMPS` ��������� � ���� �� �������������� HMI-�����: ��������� ������ ����������, ���������� ��������� � ������������ ����� ���������� ��� ��������� �� footer |
| 2.2.4 | 2026-04-17 | ������� TFT/HMI ����� ���������: ����� �������, ������������� ������������ ������, ���������� header/tabs/cards/value-tiles � ����� ������� �������� ��������� `CONTROL`/`SETTINGS` |
| 2.2.3 | 2026-04-17 | HMI-�������� IPS UI ��������� ��� ���������� custom monitor �������: `MANUAL_RECT`, `MASHING` � `HOLD` ���������� �� ��� �� root-monitor ������ � ����� summary-������� � ������ step/grid layout |
| 2.2.2 | 2026-04-17 | HMI-�������� IPS UI ��������� ��� custom monitor: ������������ ������ ��������� ��� �����������, ��� � �����������, � ����� root-monitor ��������� �� ������ status/footer ������ |
| 2.2.1 | 2026-04-17 | ������ HMI-���� IPS UI: dashboard � ����� ������������ ���������� �� ������������ ������ � ����� summary-����� � ��������������� 2x3 �������� |
| 2.2.0 | 2026-04-16 | ������ HMI-�������� IPS UI: ������������� ������, ������ ������ �������, monitor-layouts ��� ��� � ����������� |
| 2.1.21 | 2026-04-16 | Policy ������� ���������� �������� ������ � IDLE, ���������� UI � smoke-�������� |
| 2.1.20 | 2026-04-15 | ������� ������ �������, ��������� � ������������ � ��������� ���� |
| 2.1.19 | 2026-04-15 | REST API � NVS-��������� �������, ������������� ������ frontend |
| 2.1.18 | 2026-04-14 | ������� ���� 0-10� (MCP4725 + MCP6001) |
| 2.1.17 | 2026-03-18 | UI ��������� sidebar, ������������ ������������ |
| 2.1.0  | 2026-03-16 | ����� Telegram-������ |
| 2.0.0  | 2026-03-15 | ����������� v2: reason codes, status adapter, history v2 |
| 1.13.x | 2026-03-14 | ���������� ������, Health matrix, TRIAC |

������ CHANGELOG: **[CHANGELOG.md](CHANGELOG.md)**

## ��������������

������� ������������� ��� ������������� �������� ������������. ���������� ������� �������� � �������������������. �� ���������� ���������� ������� ��� ���������. ������ � �������� ���������� ������� ��������������� ��� ������.

## ��������

MIT � ��. [LICENSE](LICENSE)
