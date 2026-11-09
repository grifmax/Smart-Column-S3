# Анализ проекта Smart-Column S3 — Рекомендации и доработки

**Версия прошивки:** 1.11.18  
**Платформа:** ESP32-S3 DevKitC-1 N16R8  
**Дата анализа:** 2026-03-14

---

## Общая оценка

Проект производит **очень хорошее впечатление** — чистая архитектура, грамотное разделение на слои (drivers/control/interface/storage), богатый набор функций. Код хорошо структурирован, правила из GEMINI.md в целом соблюдаются. Ниже — только конструктивные замечания и идеи для дальнейшего развития.

---

## 🔴 Критические проблемы (требуют исправления)

### 1. Blockирующий `delay()` в `Buzzer::beep()` (main.cpp:84-92)

```cpp
// ПРОБЛЕМА:
void beep(uint8_t count, uint16_t duration) {
  for (...) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(duration);   // ← БЛОКИРУЕТ весь loop!
    ...
  }
}
```

**Решение:** Переписать на неблокирующий автомат с `millis()` — аналогично тому, как реализованы другие таймеры в [loop()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/main.cpp#196-287). Либо вынести зуммер в отдельную FreeRTOS задачу с `vTaskDelay`.

---

### 2. Статические переменные в `Safety::check()` нарушают MT-безопасность (safety.cpp:165-172)

```cpp
void check(SystemState& state, ...) {
    static bool riseBaselineReady = false;  // ← разделяемое состояние!
    static float prevWaterOutC = 0.0f;
    ...
}
```

Функция вызывается из [loop()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/main.cpp#196-287) на ядре 0, а WebSocket-обработчики могут работать на ядре 1. Нет мьютекса. **Риск:** гонка на `state.safetyOk`, [forceSafeOutputs()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/control/safety.cpp#32-37).

**Решение:** Добавить `SemaphoreHandle_t g_safetyMutex` в [safety.h](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/control/safety.h), брать мьютекс в начале [check()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/control/safety.cpp#164-346) и [reset()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/control/safety.cpp#359-385).

---

### 3. Хардкод учётных данных WiFi по умолчанию (config.h:235-236)

```cpp
#define WIFI_SSID     "DistillerAP"
#define WIFI_PASSWORD "distiller12345"
```

Это SSID/пароль точки доступа в прошивке — утечка в репозиторий. Особенно опасно для AP-mode.

**Решение:** Вынести в `secrets.h` (который добавлен в [.gitignore](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/.gitignore)), либо генерировать AP-пароль на основе MAC-адреса при первом старте.

---

## 🟡 Важные доработки (средний приоритет)

### 4. Заглушки в FSM (fsm.cpp:210-215)

```cpp
uint32_t getPhaseTargetSec(...) { return 0; }     // Заглушка
uint8_t getPhaseProgressPercent(...) { return 0; } // Заглушка
```

Эти функции вызываются из WebSocket-бродкаста и влияют на отображение прогресса в UI. Пустые значения дезориентируют пользователя.

**Рекомендация:** Реализовать через объём откачанного vs целевой объём (для фаз HEADS/BODY/TAILS) и по времени (для HEATING/STABILIZATION).

---

### 5. NBK и Ферментация в TODO, но enum и FSM уже объявлены

В [types.h](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/types.h) объявлены `Mode::NBK`, `Mode::FERMENTATION`, [NbkPhase](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/control/fsm.cpp#185-195), `FermentationPhase`, а в [fsm.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/control/fsm.cpp) вызываются `Nbk::update()` и `Fermentation::update()`. Если реализация отсутствует или неполна — это **скрытый ненадёжный код**.

**Рекомендация:** Либо добавить `#error "NBK not implemented"` через compile-time guard, либо реализовать минимально безопасную заглушку, которая сразу ставит режим IDLE.

---

### 6. `INTERVAL_LOG_WRITE = 60000 мс` — слишком редко при авариях

При аварии у вас может пройти до минуты до записи последних данных в CSV-лог. Критически важные события (авария, захлёб) логируются через `Logger::logf()` немедленно, но **показания датчиков в момент аварии** в лог не попадут.

**Решение:** При срабатывании [latchAlarm()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/control/safety.cpp#42-59) в [safety.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/control/safety.cpp) — принудительно вызвать `Logger::writeData(state)` сразу.

---

### 7. Нет watchdog-сброса в FreeRTOS задачах (если есть)

`esp_task_wdt_reset()` вызывается только в [loop()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/main.cpp#196-287). Если создаются другие задачи (PUMP_TASK, Display), они не добавлены в WDT через `esp_task_wdt_add()`.

**Проверьте:** каждая задача, зарегистрированная в WDT, должна периодически вызывать `esp_task_wdt_reset()`.

---

При потере WiFi бот пропускает `tick()` — нет экспоненциального backoff. При активной прошивке это может создать шторм переподключений при восстановлении сети.

**Рекомендация:** Добавить `MIN_RETRY_MS = 5000`, удваивать до `MAX_RETRY_MS = 60000`.

---

## 🟢 Идеи для улучшения (низкий приоритет / новые фичи)

### 9. Взвешенный Health Score вместо скаляра

Сейчас `overallHealth = 100` — один скаляр без логики. 

**Предложение:** Ввести взвешенный скоринг:
| Подсистема | Вес |
|---|---|
| Датчики температуры | 30% |
| WiFi | 20% |
| Безопасность (safetyOk) | 30% |
| PZEM/давление | 20% |

---

### 10. OTA с верификацией хэша

Текущий OTA (ArduinoOTA) не проверяет подпись прошивки. Для устройства с SSR-управлением нагревателем — это критично.

**Рекомендация:** Перейти на ESP-IDF `esp_https_ota` с проверкой SHA-256, либо хотя бы добавить CRC32 проверку после загрузки.

---

### 11. Compile-time pin profiles (TODO2.0.md)

Сейчас есть только одна конфигурация пинов в [config.h](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/config.h). При разных ревизиях платы это неудобно.

**Рекомендация:**

```cpp
// platformio.ini:
build_flags = -DBOARD_REV=2

// config.h:
#if BOARD_REV == 1
  #define PIN_PZEM_RX 44
#elif BOARD_REV == 2
  #define PIN_PZEM_RX 20
#endif
```

---

```
/health — Получить текущий Health Score и статус подсистем
```

---

### 13. Страница «Все температуры» на TFT (TODO2.0.md)

Отдельный экран с 7 датчиками DS18B20, их адресами, значениями и флагами valid/invalid — очень удобно при диагностике.

---

### 14. Периодический self-check с event log (TODO2.0.md)

**Предложение:** Каждые 30 минут записывать в лог:
- Свободная heap-память
- Время работы
- Причина последней перезагрузки
- Счётчик ошибок датчиков

---

### 15. Ротация логов с сжатием

Текущий лимит `LOG_MAX_FILES = 10` при `LOG_MAX_SIZE_BYTES = 1MB`. При интенсивном использовании это ~10 MB — заполнит LittleFS.

**Рекомендация:** Компрессия старых файлов (zlib/miniz) или бинарный формат вместо CSV для экономии места.

---

## 📊 Приоритизированный план доработок

| Приоритет | Задача | Сложность |
|---|---|---|
| 🔴 HIGH | Неблокирующий зуммер | Средняя |
| 🔴 HIGH | Мьютекс в Safety::check() | Низкая |
| 🔴 HIGH | Убрать хардкод WiFi паролей | Низкая |
| 🟡 MED | Реализовать getPhaseProgressPercent() | Средняя |
| 🟡 MED | NBK/Fermentation заглушки с явным guard | Низкая |
| 🟡 MED | Форс-запись лога при аварии | Низкая |
| 🟢 LOW | Взвешенный Health Score | Средняя |
| 🟢 LOW | Compile-time pin profiles | Средняя |
| 🟢 LOW | Страница всех температур TFT | Высокая |
| 🟢 LOW | Ротация логов / сжатие | Высокая |

---

## ✅ Что сделано хорошо (не трогать)

- **Safety с latch-механизмом** — грамотный подход к блокировке до явного сброса
- **Модульная структура FSM** — каждый режим в своём файле
- **config.h** как единственный источник пинов и констант
- **LittleFS + NVS** — правильное разделение: настройки в NVS, данные в LittleFS
- **Demo-режим** — позволяет тестировать UI без реального железа
- **Captive Portal** при первом запуске
- **MQTT Discovery** для Home Assistant
- **Cloud Tunnel** как IoT-решение без входящих портов
- **Logging macros** (LOG_E/LOG_W/LOG_I/LOG_D) СЃ уровнями
