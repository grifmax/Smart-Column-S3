# TODO � Smart-Column S3

������ ������ ������������� �����. ����������� ������ ��������� ��� ���������.

**������ ��������:** 2.2.25 | **����:** 2026-04-19

---

## ? ����������� (Major)

- [x] **����-������������** � FSM, Watt-Control, Smart Decrement, ���-����
- [x] **������ ������������** � `ManualRect::update()`, ����-�����, ����� ��� �� API
- [x] **�����������** � FSM � ������, volume tracking
- [x] **���������** � FSM, ������������� �������, ����
- [x] **Hold / ������������** � ������������� �������, ��������� �����
- [x] **��� �����** � `NbkPhase` FSM, Watt-Control �� ��������
- [x] **�����������** � `FermentationPhase` FSM, ���������� �� �������
- [x] **������� ���� 0-10�** � MCP4725 DAC + MCP6001 Op-Amp, ����-������ �� FSM
- [x] **�������� ��������** � SSR + PZEM feedback, WattControl, ramp
- [x] **�����** � TMC2209, FreeRTOS task, ���������� PWM, ����������
- [x] **�������** � ����/������/���, �����-����������� (5 �������), ��������
- [x] **������� ��������� v2** � timeseries, ����, safety timeline, reason codes, �������
- [x] **Health matrix** � ���������� ������� ���������, RebootTracker
- [x] **MQTT / Home Assistant** � Discovery, Energy Dashboard, �������
- [x] **Cloud Tunnel** � WebSocket ������, spiritcontrol.ru
- [x] **HTTP Auth + Rate Limiting** � Basic Auth, 60 req/min, per-IP
- [x] **OTA ����������** � loadfs + firmware �� �������
- [x] **TFT �������** � ILI9488 3.5", mode-specific ������, watchdog
- [x] **������������ ������������** � service workbench � Web UI
- [x] **WiFi �������** � ��������� ����� � ������������, static IP
- [x] **������� ������������ v2** � reason codes, safety supervisor, ack/reset flow

---

## ?? � ������ / ��������� ������

### ������� (�����, v2.2.0)

- [x] **API endpoints** � `/api/stirrer/start`, `/api/stirrer/stop`, `/api/stirrer/set` � `webserver.cpp`
- [x] **NVS ����������** �������� ������� � ��������/���������� ����� `nvs_manager.cpp`
- [x] **Web UI ������** � ������ ������� ���������� � live-������ ������� �� ������� ��������
- [x] **��������� � Web UI** � ������� ������������� > ����������� > ������ ��������
- [x] **��������� ���� �������** � ������� ������������� > ������������� > �������� ������� ����� �������

### ��������

- [ ] **Reboot reason tracking** � ������ ���������� � `/api/reboot/status` � Web UI �����������
- [ ] **Soak test** � 8h+ ����������� ������, �������� ������
- [ ] **Compile-time pin profiles** � ������� ���� ����� `BOARD_REV`
- [ ] **Boot-time �������� �����** � ��������� GPIO, ��������� �� �������

### TFT �������

- [ ] **���������� ������ �� �������** � ������� �������� ������ if/else (`display.cpp`)
- [ ] **�������� ���� ������������** � ��������� ����� �� ����� 7 ���������
- [ ] **��������������� refresh profile** � `normal` / `safe` / `fast` ����� ���������

### Web UI

- [ ] **������� � ������ CRUD UI** � API �����, ���-����� �� ���������
- [ ] **���������� � ������ UI** � ��������� � API ����, ���-����� ��������

---

## ?? �������

### v2.2.x � ����������� UI
- ������ CRUD ��������
- ������ UI ����������
- TFT: ���������� ������, ����� ����������

### v3.0.x � Hardware v3
- Compile-time ������� ����
- ��������� ������� V3 PCB
- Boot-time self-test GPIO

---

*���������: 2026-04-15*
