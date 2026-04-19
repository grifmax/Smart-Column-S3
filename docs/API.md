# Smart-Column S3 � API ������������

**������ ��������:** `2.2.25`
**��������� ����������:** 2026-04-19

---

## ������� ����������

- **������� URL:** `http://<device-ip>`
- **REST �������:** `/api/*`
- **WebSocket:** `ws://<device-ip>/ws`
- **������ ������:** `application/json`
- **��������������:** HTTP Basic Auth (���� �������� � ����������)

---

## ������ ���������

### `mode` (`/api/process/start`, `/api/status`, WebSocket)

| �������� | ����� |
|----------|-------|
| `idle` | ������� |
| `rectification` | ����-������������ |
| `distillation` | ����������� |
| `manual` | ������ ������������ (����� `manual_rect`) |
| `mashing` | ��������� ������ |
| `hold` | ������������ / ������������� ������� |
| `nbk` | ��� (����������� ������� �������) |
| `fermentation` | ����������� |

### `phaseStr` � ���� ����-������������

`idle` > `heating` > `stabilization` > `heads` > `post_heads` > `body` > `tails` > `purge` > `finish` > `completed`

### `phaseStr` � ���� ���

`idle` > `heating` > `stabilization` > `working` > `finish` > `completed`

### `phaseStr` � ���� ���������

`idle` > `acid_rest` > `protein_rest` > `beta_amylase` > `alpha_amylase` > `mash_out` > `finish`

### `phaseStr` � ���� �����������

`idle` > `running` > `completed`

---

## ��������� � �����������

### `GET /api/status`

������ ��������� �������. ����������� �� REST � WebSocket.

**�������� ����:**

```json
{
  "mode": "rectification",
  "modeStr": "������������",
  "phase": 3,
  "phaseStr": "heads",
  "paused": false,
  "safetyOk": true,
  "uptime": 3600,
  "temps": {
    "cube": 78.5,
    "columnBottom": 77.2,
    "columnTop": 76.1,
    "reflux": 74.0,
    "tsa": 25.0,
    "waterIn": 18.0,
    "waterOut": 32.0
  },
  "pressure": {
    "cube": 12.3,
    "atmosphere": 760.0,
    "ok": true
  },
  "power": {
    "voltage": 225.0,
    "current": 8.5,
    "power": 1900.0,
    "energy": 1.25,
    "frequency": 50.0,
    "powerFactor": 0.99
  },
  "pump": {
    "running": true,
    "speedMlPerHour": 900.0,
    "totalVolumeMl": 280.0
  },
  "stirrer": {
    "running": false,
    "speed": 0,
    "available": true,
    "autoMode": false
  },
  "hydrometer": {
    "abv": 73.5,
    "density": 0.851,
    "valid": true,
    "ok": true
  },
  "alarm": {
    "active": false,
    "latched": false,
    "type": "none",
    "level": "none",
    "message": "",
    "resetAvailable": true
  },
  "v2": {
    "available": true,
    "lifecycle": "running",
    "phaseToken": "heads",
    "paused": false,
    "lastReasonCode": "RC_HEADS_VOLUME_REACHED",
    "operatorMessage": "Heads fraction collected"
  },
  "mashing": {
    "active": false,
    "phase": 0,
    "phaseStr": "idle",
    "stepCount": 0,
    "currentStep": 0,
    "targetTemp": 0.0,
    "stepDurationSec": 0,
    "tempInRange": false,
    "stepName": "",
    "elapsedSec": 0,
    "remainingSec": 0
  },
  "hold": {
    "active": false,
    "stepCount": 0,
    "currentStep": 0,
    "targetTemp": 0.0,
    "tempInRange": false,
    "stepDurationSec": 0,
    "elapsedSec": 0,
    "remainingSec": 0
  }
}
```

---

### `GET /api/health`

������� ��������� �������� � ���������.

```json
{
  "overall": 95,
  "tempSensorsOk": 7,
  "tempSensorsTotal": 7,
  "bmp280": true,
  "ads1115": true,
  "pzem": true,
  "wifiRSSI": -55,
  "cpuTemp": 42.0
}
```

---

### `GET /api/version`

������ �������� + ����������.

```json
{
  "firmware": "2.2.25",
  "board": "esp32-s3-devkitc-1-n16r8",
  "buildDate": "Apr 15 2026",
  "deviceId": "abc123"
}
```

---

### `POST /api/reboot`

������������ �����������.

---

### `GET /api/reboot/status`

������� ������������: �������, ��� (WDT / Brownout / panic / SW reset).

---

## ���������� ���������

### `POST /api/process/start`

������ ������.

**����������� ������:**
```json
{ "mode": "rectification" }
```

**��������� ��� ������ �������:**

```json
// �����������
{
  "mode": "distillation",
  "params": {
    "speed": 1500,
    "headsVolume": 100,
    "targetVolume": 3000,
    "endTemp": 98.0
  }
}

// ���������
{
  "mode": "mashing",
  "params": {
    "profile": {
      "name": "������������",
      "steps": [
        {"temperature": 52, "duration": 15, "name": "�������� �����"},
        {"temperature": 63, "duration": 60, "name": "����-�������"},
        {"temperature": 72, "duration": 30, "name": "�����-�������"},
        {"temperature": 78, "duration": 5,  "name": "���-���"}
      ]
    }
  }
}

// ������������ / Hold
{
  "mode": "hold",
  "params": {
    "steps": [
      {"temperature": 63, "duration": 30},
      {"temperature": 72, "duration": 15}
    ]
  }
}

// ���
{
  "mode": "nbk",
  "params": {
    "targetVolumeMl": 5000,
    "powerPercent": 80
  }
}

// �����������
{
  "mode": "fermentation",
  "params": {
    "durationHours": 72,
    "targetTemp": 20.0
  }
}
```

---

### `POST /api/process/stop`

���������� ������� �������.

### `POST /api/process/pause`

��������� �� �����.

### `POST /api/process/resume`

����������� ����� �����.

---

## ����-������������

### `GET /api/settings/rect`

�������� ��������� ����-������������.

### `POST /api/settings/rect`

��������� ���������. �������������� ����:

| ���� | ��� | �������� | �������� |
|------|-----|----------|----------|
| `feedstock` | int | 0..7 | ��� ����� |
| `feedVolumeL` | float | 1..250 | ����� ����� (�) |
| `feedAbvPercent` | float | 1..96 | �������� ����� (%) |
| `headsPercent` | float | 0..40 | ���� ����� (%) |
| `bodyPercent` | float | 0..100 | ���� ���� (%) |
| `tailsPercent` | float | 0..100 | ���� ������� (%) |
| `headsSpeedMlHKw` | float | 10..2000 | �������� ����� (��/�/���) |
| `bodySpeedMlHKw` | float | 50..3000 | �������� ���� (��/�/���) |
| `stabilizationMin` | int | 1..180 | ����� ������������ (���) |
| `purgeMin` | int | 1..120 | ����� �������� (���) |
| `applyFeedstockDefaults` | bool | � | ��������� ������� �� ����� |

### ����� � ��������� �������

| feedstock | ����� | ������ % | ���� % | ������ % |
|-----------|----|---:|---:|---:|
| 0 | ����� | 6 | 84 | 10 |
| 1 | ����/����� | 8 | 80 | 12 |
| 2 | ����� | 7 | 81 | 12 |
| 3 | ������ | 5 | 75 | 20 |
| 4 | ������� | 8 | 74 | 18 |
| 5 | ��������/���� | 6 | 78 | 16 |
| 6 | ̸� | 7 | 79 | 14 |
| 7 | ������ | � | � | � |

---

## ������ ���������� (runtime)

### `POST /api/manual/heater`

```json
{ "power": 55 }
```

### `POST /api/rect/heater`

Override �������� ���� � ����-������������.  
`power = -1` � ����� override, ������� ���������� Watt-Control.

### `POST /api/manual/pump`

```json
{ "speed": 900 }
```

`speed <= 0` � ����. `speed > 0` � ��/�.

### `POST /api/manual/valves`

```json
{ "water": true, "heads": false, "uno": true }
```

### `POST /api/manual/volumes`

������ ������������� ������� �������.

```json
{
  "heads": 120,
  "body": 2500,
  "tails": 150,
  "syncTotal": true
}
```

---

## ������� ���� (0-10�, MCP4725)

### `POST /api/stirrer/start`

��������� �������.

```json
{ "speed": 70 }
```

`speed` � 0..100%. ���� �� ������ ��� 0, ������������ `defaultSpeedPercent` �� ��������.

������ ���������� �������� ��������� ������ � `IDLE`. ���� ����� ������� ������� ��� ��������� �� �����, backend ����� `409`.

### `POST /api/stirrer/stop`

���������� �������.

### `POST /api/stirrer/set`

���������� �������� � ��� ���������� ������� ��� ���������� `start`.

```json
{ "speed": 50 }
```

`speed` � 1..100%. ��� ������ ��������� ����������� `POST /api/stirrer/stop`.

### ���� � WebSocket / `/api/status`

```json
"stirrer": {
  "running": true,
  "speed": 70,
  "available": true,
  "autoMode": false
}
```

| ���� | �������� |
|------|----------|
| `running` | ������� �������� |
| `speed` | ������� �������� 0�100% |
| `available` | MCP4725 ��������� (I2C OK) |
| `autoMode` | �������� ������������� �� FSM |

---

## ��������� �������

### `GET /api/settings/stirrer`

```json
{
  "enabled": true,
  "defaultSpeedPercent": 60,
  "autoMashing": true,
  "autoFermentation": false,
  "autoNbk": false
}
```

### `POST /api/settings/stirrer`

��������� ��������� ������� � NVS.

---

## ���������� � ������������

- `GET /api/calibration`
- `POST /api/calibration/pump`
- `POST /api/calibration/temp`
- `POST /api/calibration/hydrometer`
- `GET /api/calibration/scan`
- `POST /api/pump/calibrate/start`
- `POST /api/pump/calibrate/stop`
- `POST /api/pump/calibrate/cancel`
- `POST /api/pump/calibrate/finish`
- `POST /api/pump/start`
- `POST /api/pump/stop`
- `GET /api/pump/status`
- `GET /api/pump/diag` � ����������� ����������� FreeRTOS pump-task
- `GET /api/energy`

---

## ������������ ������������

### `GET /api/testing/status`

������ ������ ������������: ����������, �������� �����, �����, �������, ���, �������, �����, �������, �������.

### `POST /api/testing/stop-all`

������������� ���������� ��� �������� ����� ������������.

### `POST /api/testing/pump`

```json
{ "action": "start", "speed": 800 }
```

### `POST /api/testing/stirrer`

```json
{ "action": "start", "speedPercent": 60 }
```

```json
{ "action": "set", "speedPercent": 45 }
```

```json
{ "action": "stop" }
```

### `POST /api/testing/heater`

```json
{ "action": "start", "power": 10 }
```

### `POST /api/testing/valves`

```json
{ "valve": "water", "action": "open" }
// ���
{ "valve": "heads", "action": "pulse", "durationMs": 2000 }
```

### `POST /api/testing/servo`

```json
{ "fraction": "body" }
```

---

## ������������

### `POST /api/safety/ack`

����������� (acknowledge) �������� ������.

**�����:**
```json
{
  "success": true,
  "v2": {
    "safetyLatched": true,
    "severity": "latched_trip",
    "resetAvailable": false,
    "resetBlockedReason": "����������� �� ��� �������"
  }
}
```

### `POST /api/safety/reset`

�������� ������ (���� ������� ���������).

---

## ������� ���������

### `GET /api/history`

������ ���� ���������. ������ ������� ��������:
- `id`, `mode`, `startTime`, `endTime`, `duration`
- `completionState` � `completed`, `stopped`, `safety_stop`
- `completionReasonCode` � RC-��� ����������
- `safetySummary` � ������� safety-������
- `safetyState` � `ok`, `trip`, `ack`, `recovery`

### `GET /api/history/{id}`

������ ������ ��������: timeseries, ����, ��������������, ������, reason codes.

### `DELETE /api/history/{id}`

������� ������ ��������.

### `GET /api/history/{id}/export`

������� � JSON.

---

## WiFi � ������

- `GET /api/wifi/scan`
- `GET /api/wifi/status`
- `POST /api/wifi/connect`
- `GET /api/wifi/profiles`
- `POST /api/wifi/profiles`
- `DELETE /api/wifi/profiles/{index}`
- `POST /api/cloud/claim`
- `POST /api/cloud/config`

---

## �������

- `GET /api/profiles`
- `GET /api/profiles/{id}`
- `POST /api/profiles/{id}/load`
- `DELETE /api/profiles/{id}`

---

## ����

- `GET /api/logs/events` � ��������� ������� � JSON/CSV
- `GET /api/logs/list` � ������ ���-������
- `GET /api/logs/{filename}` � ������� ���-����

---

## WebSocket API

�����������: `ws://<device-ip>/ws`

### Fast packet (������ 2 ���)

�������� ����:
- `mode`, `modeStr`, `phase`, `phaseStr`, `paused`, `uptime`
- `temps.*` � ��� �����������
- `pressure.*` � �������� ���� � �����������
- `power.*` � ����������, ���, ��������, �������
- `pump.*` � ��������� ������
- `stirrer.*` � ��������� ������� < **NEW**
- `alarm.*` � ������� ������
- `phase_elapsed_sec`, `phase_target_sec`, `phase_percent`

### Full packet (������ 10 ���)

�������������:
- `progress` � ��������� �������� ����
- `rectification` � ��������� ������������
- `distillation` � ��������� �����������
- `mashing` � ������ ���������
- `hold` � ������ ������������
- `health` � ��������� ���������
- `memory` � heap/flash ����������
- `v2` � lifecycle, reason codes, process indicators

---

## HTTP ���� ������

| ��� | ������� |
|-----|---------|
| 400 | ������������ JSON / ��������� |
| 401 | ��������� ����������� |
| 404 | Endpoint �� ������ |
| 429 | �������� ����� �������� |
| 500 | ���������� ������ / ������ ���������� |
| 503 | ������� ����������� ���������� |

---

## Quick cURL Examples

```bash
# ������ �������
curl -u admin:admin http://192.168.4.1/api/status

# ������ ������������
curl -u admin:admin -X POST http://192.168.4.1/api/process/start \
  -H "Content-Type: application/json" \
  -d '{"mode":"rectification"}'

# ����� ������� �� 70%
curl -u admin:admin -X POST http://192.168.4.1/api/stirrer/start \
  -H "Content-Type: application/json" \
  -d '{"speed":70}'

# ���� �������
curl -u admin:admin -X POST http://192.168.4.1/api/stirrer/stop

# ��������� ������������ �� ��������� �����
curl -u admin:admin -X POST http://192.168.4.1/api/settings/rect \
  -H "Content-Type: application/json" \
  -d '{"feedstock":0,"feedVolumeL":25,"feedAbvPercent":35,"applyFeedstockDefaults":true}'

# ��������� (��������� �������)
curl -u admin:admin -X POST http://192.168.4.1/api/process/start \
  -H "Content-Type: application/json" \
  -d '{"mode":"mashing","params":{"profile":{"name":"����","steps":[{"temperature":63,"duration":60,"name":"����"}]}}}'

# ������� ���������
curl -u admin:admin http://192.168.4.1/api/history
```
