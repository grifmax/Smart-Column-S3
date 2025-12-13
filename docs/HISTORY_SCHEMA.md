# Схема данных истории процессов

## Обзор

Каждый завершенный процесс сохраняется в отдельный JSON файл в директории `/data/history/`.

**Формат имени файла:** `process_{timestamp}.json`
**Пример:** `process_1704672000.json` (Unix timestamp)

**Ротация:**
- Максимум 50 файлов
- Максимум 2 МБ общего размера
- При превышении удаляются самые старые файлы

---

## JSON Schema

```json
{
  "id": "1704672000",
  "version": "1.3.0",
  "metadata": {
    "startTime": 1704672000,
    "endTime": 1704686400,
    "duration": 14400,
    "completedSuccessfully": true,
    "deviceId": "smartcolumn_abc123"
  },
  "process": {
    "type": "rectification",
    "mode": "auto",
    "profile": null
  },
  "parameters": {
    "targetPower": 2500,
    "headVolume": 50,
    "bodyVolume": 1500,
    "tailVolume": 100,
    "pumpSpeedHead": 200,
    "pumpSpeedBody": 300,
    "stabilizationTime": 1800,
    "wattControlEnabled": true,
    "smartDecrementEnabled": true
  },
  "metrics": {
    "temperatures": {
      "cube": {
        "min": 78.5,
        "max": 98.2,
        "avg": 88.4,
        "final": 95.1
      },
      "columnBottom": {
        "min": 75.0,
        "max": 82.5,
        "avg": 78.2,
        "final": 80.1
      },
      "columnTop": {
        "min": 76.0,
        "max": 80.5,
        "avg": 78.5,
        "final": 79.2
      },
      "deflegmator": {
        "min": 78.0,
        "max": 79.5,
        "avg": 78.8,
        "final": 78.9
      }
    },
    "power": {
      "energyUsed": 10.5,
      "avgPower": 2450,
      "peakPower": 2600
    },
    "pump": {
      "totalVolume": 1650,
      "avgSpeed": 285
    }
  },
  "phases": [
    {
      "name": "heating",
      "startTime": 1704672000,
      "endTime": 1704675600,
      "duration": 3600,
      "startTemp": 20.5,
      "endTemp": 78.5
    },
    {
      "name": "stabilization",
      "startTime": 1704675600,
      "endTime": 1704677400,
      "duration": 1800,
      "startTemp": 78.5,
      "endTemp": 78.8
    },
    {
      "name": "heads",
      "startTime": 1704677400,
      "endTime": 1704681000,
      "duration": 3600,
      "volume": 50,
      "avgSpeed": 200
    },
    {
      "name": "body",
      "startTime": 1704681000,
      "endTime": 1704685500,
      "duration": 4500,
      "volume": 1500,
      "avgSpeed": 300
    },
    {
      "name": "tails",
      "startTime": 1704685500,
      "endTime": 1704686400,
      "duration": 900,
      "volume": 100,
      "avgSpeed": 250
    }
  ],
  "timeseries": {
    "interval": 60,
    "data": [
      {
        "time": 1704672000,
        "cube": 20.5,
        "columnTop": 20.2,
        "power": 2500,
        "pumpSpeed": 0
      },
      {
        "time": 1704672060,
        "cube": 25.3,
        "columnTop": 22.1,
        "power": 2500,
        "pumpSpeed": 0
      }
    ]
  },
  "results": {
    "headsCollected": 50,
    "bodyCollected": 1500,
    "tailsCollected": 100,
    "totalCollected": 1650,
    "status": "completed",
    "errors": [],
    "warnings": [
      {
        "time": 1704683200,
        "message": "Температура куба превысила 95°C",
        "severity": "warning"
      }
    ]
  },
  "notes": ""
}
```

---

## Описание полей

### Metadata (Метаданные)

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | string | Уникальный ID процесса (Unix timestamp) |
| `version` | string | Версия схемы/firmware |
| `metadata.startTime` | number | Unix timestamp начала процесса |
| `metadata.endTime` | number | Unix timestamp окончания процесса |
| `metadata.duration` | number | Длительность в секундах |
| `metadata.completedSuccessfully` | boolean | Успешное завершение |
| `metadata.deviceId` | string | ID устройства |

### Process (Процесс)

| Поле | Тип | Описание |
|------|-----|----------|
| `process.type` | string | Тип: `rectification`, `distillation`, `mashing`, `hold` |
| `process.mode` | string | Режим: `auto`, `manual` |
| `process.profile` | string/null | Имя профиля (если загружен) |

### Parameters (Параметры)

Настройки процесса на момент запуска.

| Поле | Тип | Описание |
|------|-----|----------|
| `targetPower` | number | Целевая мощность (Вт) |
| `headVolume` | number | Объём голов (мл) |
| `bodyVolume` | number | Объём тела (мл) |
| `tailVolume` | number | Объём хвостов (мл) |
| `pumpSpeedHead` | number | Скорость насоса для голов (мл/час) |
| `pumpSpeedBody` | number | Скорость насоса для тела (мл/час) |
| `stabilizationTime` | number | Время стабилизации (сек) |
| `wattControlEnabled` | boolean | Watt-Control включен |
| `smartDecrementEnabled` | boolean | Smart Decrement включен |

### Metrics (Метрики)

Агрегированные данные за весь процесс.

**Temperatures:**
- `min` - минимальная температура
- `max` - максимальная температура
- `avg` - средняя температура
- `final` - финальная температура

**Power:**
- `energyUsed` - потребленная энергия (кВт·ч)
- `avgPower` - средняя мощность (Вт)
- `peakPower` - пиковая мощность (Вт)

**Pump:**
- `totalVolume` - общий объём отбора (мл)
- `avgSpeed` - средняя скорость (мл/час)

### Phases (Фазы)

Массив фаз процесса с временными метками и параметрами.

| Поле | Тип | Описание |
|------|-----|----------|
| `name` | string | Название фазы |
| `startTime` | number | Unix timestamp начала |
| `endTime` | number | Unix timestamp окончания |
| `duration` | number | Длительность (сек) |
| `volume` | number | Объём отобранный (мл) |
| `avgSpeed` | number | Средняя скорость насоса (мл/час) |

### Timeseries (Временные ряды)

Детальные данные с заданным интервалом.

| Поле | Тип | Описание |
|------|-----|----------|
| `interval` | number | Интервал записи (сек) |
| `data` | array | Массив точек данных |

**Точка данных:**
```json
{
  "time": 1704672000,
  "cube": 20.5,
  "columnTop": 20.2,
  "columnBottom": 20.1,
  "deflegmator": 19.8,
  "power": 2500,
  "voltage": 230,
  "current": 10.9,
  "pumpSpeed": 0
}
```

### Results (Результаты)

| Поле | Тип | Описание |
|------|-----|----------|
| `headsCollected` | number | Собрано голов (мл) |
| `bodyCollected` | number | Собрано тела (мл) |
| `tailsCollected` | number | Собрано хвостов (мл) |
| `totalCollected` | number | Всего собрано (мл) |
| `status` | string | Статус: `completed`, `stopped`, `error` |
| `errors` | array | Массив ошибок |
| `warnings` | array | Массив предупреждений |
| `notes` | string | Заметки пользователя |

---

## API Endpoints

### GET /api/history

Получить список всех процессов.

**Response:**
```json
{
  "total": 15,
  "processes": [
    {
      "id": "1704672000",
      "type": "rectification",
      "startTime": 1704672000,
      "duration": 14400,
      "status": "completed",
      "totalVolume": 1650
    }
  ]
}
```

### GET /api/history/{id}

Получить полные данные процесса.

**Response:** Полный JSON объект процесса

### GET /api/history/{id}/export?format=csv

Экспорт процесса в CSV.

**Parameters:**
- `format` - формат экспорта: `csv`, `json`

**Response (CSV):**
```csv
Time,Cube Temp,Column Top,Column Bottom,Power,Pump Speed
1704672000,20.5,20.2,20.1,2500,0
1704672060,25.3,22.1,21.8,2500,0
```

### POST /api/history/{id}/compare

Сравнение процессов.

**Request:**
```json
{
  "processIds": ["1704672000", "1704758400"]
}
```

**Response:**
```json
{
  "processes": [...],
  "comparison": {
    "duration": {
      "process1": 14400,
      "process2": 15200,
      "diff": 800
    },
    "energyUsed": {
      "process1": 10.5,
      "process2": 11.2,
      "diff": 0.7
    }
  }
}
```

### DELETE /api/history/{id}

Удалить процесс из истории.

**Response:**
```json
{
  "success": true,
  "message": "Process deleted"
}
```

---

## Размер данных

**Оценка размера одного файла:**
- Metadata + Parameters: ~500 bytes
- Metrics: ~300 bytes
- Phases: ~500 bytes
- Timeseries (4 часа, 60 сек интервал): 240 точек × 100 bytes = ~24 KB
- **Итого:** ~25 KB на процесс

**Максимальное хранилище:**
- 50 процессов × 25 KB = **1.25 MB**
- Укладывается в лимит 2 MB

---

## Примеры использования

### Сохранение процесса

```cpp
void saveProcessHistory(ProcessData& data) {
    String filename = "/history/process_" + String(data.startTime) + ".json";

    File file = SPIFFS.open(filename, "w");
    if (!file) {
        Serial.println("Failed to create history file");
        return;
    }

    StaticJsonDocument<32768> doc;

    // Заполнить данные
    doc["id"] = String(data.startTime);
    doc["version"] = FIRMWARE_VERSION;
    doc["metadata"]["startTime"] = data.startTime;
    doc["metadata"]["endTime"] = data.endTime;
    // ...

    serializeJson(doc, file);
    file.close();

    // Проверить ротацию
    rotateHistory();
}
```

### Загрузка списка процессов

```cpp
void loadHistoryList(AsyncWebServerRequest *request) {
    StaticJsonDocument<4096> doc;
    JsonArray processes = doc.createNestedArray("processes");

    File root = SPIFFS.open("/history");
    File file = root.openNextFile();

    while (file) {
        if (!file.isDirectory()) {
            StaticJsonDocument<512> processDoc;
            deserializeJson(processDoc, file);

            JsonObject process = processes.createNestedObject();
            process["id"] = processDoc["id"];
            process["type"] = processDoc["process"]["type"];
            process["startTime"] = processDoc["metadata"]["startTime"];
            // ...
        }
        file = root.openNextFile();
    }

    doc["total"] = processes.size();

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}
```

---

## Ротация файлов

```cpp
void rotateHistory() {
    // Получить список файлов
    std::vector<String> files;
    File root = SPIFFS.open("/history");
    File file = root.openNextFile();

    while (file) {
        files.push_back(file.name());
        file = root.openNextFile();
    }

    // Сортировать по времени (старые первые)
    std::sort(files.begin(), files.end());

    // Удалить старые файлы если превышен лимит
    while (files.size() > MAX_HISTORY_FILES) {
        SPIFFS.remove(files[0]);
        files.erase(files.begin());
    }

    // Проверить общий размер
    size_t totalSize = 0;
    for (const auto& filename : files) {
        File f = SPIFFS.open(filename, "r");
        totalSize += f.size();
        f.close();
    }

    // Удалить старые файлы если превышен размер
    while (totalSize > MAX_HISTORY_SIZE && files.size() > 1) {
        File f = SPIFFS.open(files[0], "r");
        totalSize -= f.size();
        f.close();
        SPIFFS.remove(files[0]);
        files.erase(files.begin());
    }
}
```
