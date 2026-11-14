# Сводка изменений - Ручная ректификация (только backend)

## ✅ Что закодировано

### 1. Новая структура настроек `ManualRectSettings` (types.h:747-762)

```cpp
struct ManualRectSettings {
  float headsPowerW = 2500.0f;           // Мощность голов (Вт)
  float bodyPowerW = 2000.0f;            // Мощность тела (Вт)  
  float tailsPowerW = 1500.0f;           // Мощность хвостов (Вт)
  float headsSpeedMlH = 300.0f;          // Скорость голов (мл/ч)
  float bodySpeedMlH = 600.0f;           // Скорость тела (мл/ч)
  float headsTargetVolumeMl = 0.0f;      // Целевой объем голов (0 = ручная смена)
  float bodyTargetVolumeMl = 0.0f;       // Целевой объем тела
  float tailsTargetVolumeMl = 0.0f;      // Целевой объем хвостов
  bool fractionChangeSoundEnabled = true; // Звуковой сигнал
};
```

### 2. Runtime состояние для смены тары (types.h:489-492)

```cpp
bool manualRectContainerChangePending = false;     // Ожидание смены тары
RectPhase manualRectPendingPhase = RectPhase::IDLE; // Следующая фаза
```

### 3. Обновлена логика manual_rect_handler.cpp

**Функция `getManualPhasePowerPercent()`:**
- Использует `ManualRectSettings` вместо `rectParams`
- Конвертирует Ватты → проценты
- Для стабилизации: 80% от мощности голов

**Функция `getDefaultTakeoffSpeedMlH()`:**
- Головы: `settings.manualRect.headsSpeedMlH`
- Тело: `settings.manualRect.bodySpeedMlH`
- Хвосты: старая логика сохранена

**Функция `update()` - автоматическая смена фракций:**
- Проверяет достижение целевого объема
- Останавливает отбор и нагрев
- Устанавливает флаг `manualRectContainerChangePending`
- Отправляет MQTT уведомление
- Логирует событие

**Новая функция `confirmContainerChange()`:**
- Сбрасывает флаг ожидания
- Запускает следующую фракцию
- Отправляет уведомление

### 4. Обновлен fsm.h

Добавлена функция:
```cpp
bool FSM::ManualRect::confirmContainerChange(SystemState& state, const Settings& settings);
```

### 5. NVS сохранение (config.h + nvs_manager.cpp)

**9 новых NVS ключей:**
```cpp
NVS_KEY_MANUAL_RECT_HEADS_POWER   // mrect_hpw
NVS_KEY_MANUAL_RECT_BODY_POWER    // mrect_bpw
NVS_KEY_MANUAL_RECT_TAILS_POWER   // mrect_tpw
NVS_KEY_MANUAL_RECT_HEADS_SPEED   // mrect_hsp
NVS_KEY_MANUAL_RECT_BODY_SPEED    // mrect_bsp
NVS_KEY_MANUAL_RECT_HEADS_VOL     // mrect_hvol
NVS_KEY_MANUAL_RECT_BODY_VOL      // mrect_bvol
NVS_KEY_MANUAL_RECT_TAILS_VOL     // mrect_tvol
NVS_KEY_MANUAL_RECT_SOUND         // mrect_snd
```

**Загрузка из NVS** - nvs_manager.cpp:632-640  
**Сохранение в NVS** - nvs_manager.cpp:913-922

### 6. Документация

- `docs/manual_rect_improvements.md` - полное описание изменений
- `CHANGELOG.md` - запись в Unreleased

## ⏳ Что НЕ закодировано (требует веб-разработки)

### Web API Endpoints (нужно добавить в webserver_shared.cpp):

1. **GET `/api/settings/manual-rect`**
   ```json
   {
     "headsPowerW": 2500,
     "bodyPowerW": 2000,
     ...
   }
   ```

2. **POST `/api/settings/manual-rect`**
   - Принимает JSON с настройками
   - Сохраняет в `g_settings.manualRect`
   - Вызывает `NvsManager::saveSettings()`

3. **POST `/api/process/manual-rect/confirm-container`**
   - Вызывает `FSM::ManualRect::confirmContainerChange()`
   - Возвращает успех/ошибку

4. **Обновить `/api/status`**
   - Добавить `manualRectContainerChangePending`
   - Добавить `manualRectPendingPhase`

### Web UI (нужно создать/обновить):

1. **Страница настроек ручной ректификации**
   - Текстовые поля для мощности (Вт)
   - Текстовые поля для скорости (мл/ч)
   - Поля для целевых объемов (опционально)
   - Чекбокс для звукового сигнала

2. **Модальное окно "Смена тары"**
   - Показывается когда `manualRectContainerChangePending === true`
   - Текст: "Отбор остановлен. Смените тару."
   - Кнопка "Подтвердить смену тары"
   - При нажатии: POST на `/api/process/manual-rect/confirm-container`

3. **Звуковой сигнал**
   - Проигрывать звук в браузере при `manualRectContainerChangePending === true`
   - Использовать `Audio API` или `<audio>` элемент

4. **Индикация ожидания**
   - Показать на главной странице режима
   - Иконка/бейдж "Ожидание смены тары"

## 🎯 Следующие шаги

1. Проверить компиляцию (я не могу это сделать в текущем режиме)
2. Реализовать Web API endpoints
3. Создать UI для настроек
4. Создать модальное окно смены тары
5. Добавить звуковой сигнал в браузере
6. Протестировать на реальном устройстве

## 📊 Статистика изменений

- **Измененные файлы:** 7
  - `src/types.h`
  - `src/control/modes/manual_rect_handler.cpp`
  - `src/control/fsm.h`
  - `src/config.h`
  - `src/storage/nvs_manager.cpp`
  - `CHANGELOG.md`
  - `docs/manual_rect_improvements.md` (новый)

- **Добавлено строк:** ~150
- **Новых функций:** 1 (`confirmContainerChange`)
- **Новых структур:** 1 (`ManualRectSettings`)
- **Новых NVS ключей:** 9

## ✨ Преимущества реализации

✅ Упрощение - мощность в Ваттах вместо процентов  
✅ Раздельные настройки для каждой фракции  
✅ Автоматизация смены фракций  
✅ Безопасность - автоостановка при достижении объема  
✅ Уведомления - звук + MQTT  
✅ Сохранение в NVS - настройки не сбрасываются  
✅ Обратная совместимость - старые настройки не затронуты  

## 🔄 Миграция для пользователей

Старые настройки `rectParams` продолжают работать для автоматической ректификации. 
Новые настройки `manualRect` используются только для ручного режима.
При первом запуске будут использованы дефолтные значения из структуры.
