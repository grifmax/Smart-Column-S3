# Схема хранения профилей процессов

**Версия:** 1.4.0
**Дата:** 2025-12-08

## Обзор

Система профилей позволяет сохранять настройки процессов под именем для последующего повторного использования. Профили хранятся в SPIFFS в формате JSON.

## Структура хранения

### Директория

```
/data/profiles/
├── profile_1702123456.json
├── profile_1702234567.json
└── ...
```

### Именование файлов

- Формат: `profile_{timestamp}.json`
- `timestamp` - Unix timestamp создания профиля
- Максимум: 100 профилей
- Автоматическая ротация при превышении лимита

## JSON Schema

### Полная структура профиля

```json
{
  "id": "1702123456",
  "metadata": {
    "name": "Сахарная брага 40%",
    "description": "Классическая ректификация сахарной браги с крепостью 40%",
    "category": "rectification",
    "tags": ["сахар", "классика", "40%"],
    "created": 1702123456,
    "updated": 1702234567,
    "author": "user",
    "isBuiltin": false
  },
  "parameters": {
    "mode": "rectification",
    "model": "classic",
    "heater": {
      "maxPower": 3000,
      "autoMode": true,
      "pidKp": 2.0,
      "pidKi": 0.5,
      "pidKd": 1.0
    },
    "rectification": {
      "stabilizationMin": 20,
      "headsVolume": 50,
      "bodyVolume": 2000,
      "tailsVolume": 100,
      "headsSpeed": 150,
      "bodySpeed": 300,
      "tailsSpeed": 400,
      "purgeMin": 5
    },
    "distillation": {
      "headsVolume": 0,
      "targetVolume": 3000,
      "speed": 500,
      "endTemp": 96.0
    },
    "temperatures": {
      "maxCube": 98.0,
      "maxColumn": 82.0,
      "headsEnd": 78.5,
      "bodyStart": 78.0,
      "bodyEnd": 85.0
    },
    "safety": {
      "maxRuntime": 720,
      "waterFlowMin": 2.0,
      "pressureMax": 150
    }
  },
  "statistics": {
    "useCount": 15,
    "lastUsed": 1702234567,
    "avgDuration": 25200,
    "avgYield": 2850,
    "successRate": 93.3
  }
}
```

## Поля описания

### Metadata

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | string | Уникальный ID (timestamp) |
| `name` | string | Название профиля (макс 50 символов) |
| `description` | string | Описание (макс 200 символов) |
| `category` | enum | Категория: `rectification`, `distillation`, `mashing` |
| `tags` | array | Теги для поиска и фильтрации |
| `created` | number | Unix timestamp создания |
| `updated` | number | Unix timestamp последнего обновления |
| `author` | string | Автор профиля |
| `isBuiltin` | boolean | Встроенный рецепт (нельзя удалить) |

### Parameters

Полный набор параметров процесса:

#### Heater (нагреватель)
- `maxPower` - максимальная мощность (Вт)
- `autoMode` - автоматический режим (true/false)
- `pidKp`, `pidKi`, `pidKd` - коэффициенты ПИД-регулятора

#### Rectification (ректификация)
- `stabilizationMin` - время стабилизации (минуты)
- `headsVolume` - объем голов (мл)
- `bodyVolume` - объем тела (мл)
- `tailsVolume` - объем хвостов (мл)
- `headsSpeed` - скорость отбора голов (мл/ч/кВт)
- `bodySpeed` - скорость отбора тела (мл/ч/кВт)
- `tailsSpeed` - скорость отбора хвостов (мл/ч/кВт)
- `purgeMin` - время продувки (минуты)

#### Distillation (дистилляция)
- `headsVolume` - объем голов (мл, может быть 0)
- `targetVolume` - целевой объем (мл)
- `speed` - скорость отбора (мл/ч)
- `endTemp` - температура завершения (°C)

#### Temperatures (температурные пороги)
- `maxCube` - максимальная температура куба (°C)
- `maxColumn` - максимальная температура колонны (°C)
- `headsEnd` - температура окончания голов (°C)
- `bodyStart` - температура начала тела (°C)
- `bodyEnd` - температура окончания тела (°C)

#### Safety (безопасность)
- `maxRuntime` - максимальное время работы (минуты)
- `waterFlowMin` - минимальный поток воды (л/мин)
- `pressureMax` - максимальное давление (mmHg)

### Statistics

Статистика использования профиля:

| Поле | Тип | Описание |
|------|-----|----------|
| `useCount` | number | Количество использований |
| `lastUsed` | number | Unix timestamp последнего использования |
| `avgDuration` | number | Средняя длительность процесса (сек) |
| `avgYield` | number | Средний выход продукта (мл) |
| `successRate` | number | Процент успешных завершений |

## Категории профилей

### Rectification (Ректификация)
Полный цикл ректификации с отбором голов, тела и хвостов.

**Параметры:**
- Все поля `rectification`
- Температурные пороги
- ПИД-регулятор

### Distillation (Дистилляция)
Простая дистилляция без разделения на фракции.

**Параметры:**
- Все поля `distillation`
- Базовые температуры
- Простое управление мощностью

### Mashing (Затирка солода)
Режим для затирания солода с температурными паузами.

**Параметры:**
- Температурные ступени
- Время выдержки
- Контроль мощности

## API Endpoints

### Список профилей
```http
GET /api/profiles
```

**Response:**
```json
{
  "profiles": [
    {
      "id": "1702123456",
      "name": "Сахарная брага 40%",
      "category": "rectification",
      "useCount": 15,
      "lastUsed": 1702234567
    }
  ],
  "total": 1
}
```

### Получить профиль
```http
GET /api/profiles/{id}
```

**Response:** Полный JSON профиля

### Создать профиль
```http
POST /api/profiles
Content-Type: application/json

{
  "metadata": { ... },
  "parameters": { ... }
}
```

**Response:**
```json
{
  "success": true,
  "id": "1702123456"
}
```

### Обновить профиль
```http
PUT /api/profiles/{id}
Content-Type: application/json

{
  "metadata": { ... },
  "parameters": { ... }
}
```

### Удалить профиль
```http
DELETE /api/profiles/{id}
```

**Note:** Встроенные профили (`isBuiltin: true`) нельзя удалить

### Загрузить профиль
```http
POST /api/profiles/{id}/load
```

Загружает параметры профиля в текущие настройки системы.

## Встроенные рецепты

### 1. Сахарная брага 40%
```json
{
  "metadata": {
    "name": "Сахарная брага 40%",
    "category": "rectification",
    "isBuiltin": true
  },
  "parameters": {
    "rectification": {
      "stabilizationMin": 20,
      "headsVolume": 50,
      "bodyVolume": 2000,
      "headsSpeed": 150,
      "bodySpeed": 300
    }
  }
}
```

### 2. Зерновая брага 12%
```json
{
  "metadata": {
    "name": "Зерновая брага 12%",
    "category": "rectification",
    "isBuiltin": true
  },
  "parameters": {
    "rectification": {
      "stabilizationMin": 30,
      "headsVolume": 100,
      "bodyVolume": 2500,
      "headsSpeed": 120,
      "bodySpeed": 250
    }
  }
}
```

### 3. Фруктовая дистилляция
```json
{
  "metadata": {
    "name": "Фруктовая дистилляция",
    "category": "distillation",
    "isBuiltin": true
  },
  "parameters": {
    "distillation": {
      "headsVolume": 30,
      "targetVolume": 3000,
      "speed": 500,
      "endTemp": 96.0
    }
  }
}
```

## Ограничения

- Максимум 100 профилей
- Максимум 10 встроенных рецептов
- Размер одного профиля: ~2 KB
- Общий размер: ~200 KB

## Валидация

При создании/обновлении профиля проверяется:
- Название не пустое и ≤ 50 символов
- Категория из списка допустимых
- Все числовые параметры в допустимых диапазонах
- Объемы > 0
- Температуры в диапазоне 0-120°C
- Скорости > 0

## Примеры использования

### Создание профиля из текущих настроек

```javascript
async function saveCurrentSettings() {
  const profile = {
    metadata: {
      name: prompt('Название профиля:'),
      description: prompt('Описание:'),
      category: 'rectification',
      tags: ['custom'],
      author: 'user',
      isBuiltin: false
    },
    parameters: getCurrentSettings()
  };

  const response = await fetch('/api/profiles', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(profile)
  });

  const result = await response.json();
  console.log('Profile created:', result.id);
}
```

### Загрузка профиля

```javascript
async function loadProfile(profileId) {
  const response = await fetch(`/api/profiles/${profileId}/load`, {
    method: 'POST'
  });

  if (response.ok) {
    console.log('Profile loaded successfully');
    location.reload(); // Перезагрузить страницу
  }
}
```

---

*Версия документа: 1.4.0*
*Дата: 2025-12-08*
