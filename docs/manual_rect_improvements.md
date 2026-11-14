# Упрощение настроек ручной ректификации

## Что изменено

Настройки ручной ректификации значительно упрощены и теперь более интуитивны для оператора.

## Новая структура настроек (`ManualRectSettings`)

### 1. Мощность ТЭНа для каждой фракции (Ватты)

```cpp
float headsPowerW = 2500.0f;  // Мощность для голов
float bodyPowerW = 2000.0f;   // Мощность для тела
float tailsPowerW = 1500.0f;  // Мощность для хвостов
```

**Преимущества:**
- Задаются в Ваттах, а не в процентах (понятнее для пользователя)
- Автоматически конвертируются в проценты от установленной мощности ТЭНа
- Сохраняются в NVS и не сбрасываются после перезагрузки

### 2. Скорость отбора для каждой фракции (мл/ч)

```cpp
float headsSpeedMlH = 300.0f;  // Скорость отбора голов
float bodySpeedMlH = 600.0f;   // Скорость отбора тела
```

**Преимущества:**
- Отдельные настройки для голов и тела
- Для хвостов используется существующая логика декремента
- Настройки применяются автоматически при смене фракции

### 3. Автоматическая смена фракций по объему

```cpp
float headsTargetVolumeMl = 0.0f;  // Целевой объем голов (0 = ручная смена)
float bodyTargetVolumeMl = 0.0f;   // Целевой объем тела (0 = ручная смена)
float tailsTargetVolumeMl = 0.0f;  // Целевой объем хвостов (0 = ручная смена)
```

**Логика работы:**
- Если целевой объем = 0, смена фракции происходит вручную через оператора
- Если целевой объем > 0, при достижении объема:
  1. Останавливается отбор
  2. Отключается нагрев
  3. Звучит звуковой сигнал (если включен)
  4. Появляется окно "Смена тары"
  5. После подтверждения смены тары запускается следующая фракция

### 4. Звуковой сигнал при смене фракций

```cpp
bool fractionChangeSoundEnabled = true;  // Включить звуковой сигнал
```

**Поведение:**
- Срабатывает при достижении целевого объема фракции
- Можно отключить в настройках
- Учитывает глобальную настройку `soundEnabled`

## Изменения в коде

### 1. Новые поля в `SystemState` (types.h:489-492)

```cpp
bool manualRectContainerChangePending = false;     // Ожидание смены тары
RectPhase manualRectPendingPhase = RectPhase::IDLE; // Фаза после смены тары
```

### 2. Новая функция подтверждения смены тары

```cpp
bool FSM::ManualRect::confirmContainerChange(SystemState& state, const Settings& settings);
```

### 3. Обновлена логика расчета мощности

Функция `getManualPhasePowerPercent()` теперь использует настройки из `ManualRectSettings`:
- Конвертирует Ватты в проценты
- Применяет ограничения (10-100%)
- Для стабилизации использует 80% от мощности голов

### 4. Обновлена логика скорости отбора

Функция `getDefaultTakeoffSpeedMlH()` теперь возвращает:
- Для голов: `settings.manualRect.headsSpeedMlH`
- Для тела: `settings.manualRect.bodySpeedMlH`
- Для хвостов: используется старая логика из `rectParams`

### 5. Автоматическая проверка объемов

В функции `update()` добавлена логика:
- Проверка достижения целевых объемов
- Остановка отбора и нагрева
- Установка флага `manualRectContainerChangePending`
- Уведомление через MQTT

## NVS ключи

Новые ключи для сохранения настроек:

```cpp
#define NVS_KEY_MANUAL_RECT_HEADS_POWER "mrect_hpw"  // Мощность голов (Вт)
#define NVS_KEY_MANUAL_RECT_BODY_POWER "mrect_bpw"   // Мощность тела (Вт)
#define NVS_KEY_MANUAL_RECT_TAILS_POWER "mrect_tpw"  // Мощность хвостов (Вт)
#define NVS_KEY_MANUAL_RECT_HEADS_SPEED "mrect_hsp"  // Скорость голов (мл/ч)
#define NVS_KEY_MANUAL_RECT_BODY_SPEED "mrect_bsp"   // Скорость тела (мл/ч)
#define NVS_KEY_MANUAL_RECT_HEADS_VOL "mrect_hvol"   // Целевой объем голов (мл)
#define NVS_KEY_MANUAL_RECT_BODY_VOL "mrect_bvol"    // Целевой объем тела (мл)
#define NVS_KEY_MANUAL_RECT_TAILS_VOL "mrect_tvol"   // Целевой объем хвостов (мл)
#define NVS_KEY_MANUAL_RECT_SOUND "mrect_snd"        // Звуковой сигнал (bool)
```

## Веб-интерфейс (требуется реализация)

### API endpoints (планируется)

1. **GET `/api/settings/manual-rect`** - получить настройки ручной ректификации
2. **POST `/api/settings/manual-rect`** - сохранить настройки
3. **POST `/api/process/manual-rect/confirm-container`** - подтвердить смену тары

### Пример JSON для настроек:

```json
{
  "headsPowerW": 2500,
  "bodyPowerW": 2000,
  "tailsPowerW": 1500,
  "headsSpeedMlH": 300,
  "bodySpeedMlH": 600,
  "headsTargetVolumeMl": 500,
  "bodyTargetVolumeMl": 3000,
  "tailsTargetVolumeMl": 1000,
  "fractionChangeSoundEnabled": true
}
```

### Состояние смены тары в `/api/status`:

```json
{
  "manualRectContainerChangePending": true,
  "manualRectPendingPhase": "BODY"
}
```

## Пример использования

### Сценарий 1: Ручная смена фракций

```cpp
settings.manualRect.headsPowerW = 2500;
settings.manualRect.bodyPowerW = 2000;
settings.manualRect.headsSpeedMlH = 300;
settings.manualRect.bodySpeedMlH = 600;
settings.manualRect.headsTargetVolumeMl = 0;  // Ручная смена
settings.manualRect.bodyTargetVolumeMl = 0;
```

Оператор вручную переключает фразы через веб-интерфейс.

### Сценарий 2: Автоматическая смена фракций

```cpp
settings.manualRect.headsTargetVolumeMl = 500;   // 500 мл голов
settings.manualRect.bodyTargetVolumeMl = 3000;   // 3000 мл тела
settings.manualRect.fractionChangeSoundEnabled = true;
```

1. Отбираем головы на мощности 2500 Вт, скорость 300 мл/ч
2. При достижении 500 мл:
   - Звучит сигнал
   - Отбор останавливается
   - Появляется окно "Смена тары"
3. Оператор меняет тару и подтверждает
4. Автоматически запускается отбор тела на мощности 2000 Вт, скорость 600 мл/ч

## Преимущества

✅ **Упрощение настроек** - мощность в Ваттах вместо процентов  
✅ **Раздельные настройки** - для каждой фракции свои параметры  
✅ **Автоматизация** - смена фракций по объему с уведомлением  
✅ **Безопасность** - отбор останавливается автоматически  
✅ **Удобство** - звуковой сигнал напоминает о смене тары  
✅ **Гибкость** - можно выбрать ручной или автоматический режим  

## TODO для веб-интерфейса

- [ ] Добавить страницу настроек ручной ректификации
- [ ] Реализовать API endpoints
- [ ] Добавить модальное окно "Смена тары"
- [ ] Добавить звуковой сигнал в браузере
- [ ] Добавить индикацию ожидания смены тары
- [ ] Добавить возможность отмены смены (вернуться к текущей фракции)

## Обратная совместимость

Старые настройки `rectParams.phasePowerPercent[]` и `rectParams.*SpeedMlHKw` **сохранены** для автоматической ректификации и не затрагиваются этими изменениями.
