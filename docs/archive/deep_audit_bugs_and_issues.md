# 🔍 Deep Code Audit — Smart-Column S3

**Дата:** 2026-03-14  
**Метод:** Ручной аудит всех основных .cpp/.h файлов  
**Файлов проанализировано:** 25+  

---

## 🐛 Подтверждённые баги

### BUG-1. `tempSensorsOk` — тип `bool`, записывается `true` вместо количества
**Файл:** [sensors.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/sensors.cpp#L629-L640)

```cpp
// types.h:  bool tempSensorsOk = false;  ← тип bool
// sensors.cpp:
health.tempSensorsOk = 0;        // строка 632 — обнуляет
if (isTempSensorValid(i)) {
    health.tempSensorsOk = true;  // строка 637 — ставит true
    // Ожидалось: подсчёт количества рабочих датчиков!
}
```

> [!CAUTION]
> Далее в [updateHealth()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/sensors.cpp#629-705) строка 670 вычисляется `tempFailures = total - tempSensorsOk`. При `bool=true` это: `total - 1`, что неверно, если рабочих датчиков > 1. **Health Score искажён.**

**Исправление:** Либо сделать `tempSensorsOk` типом `uint8_t`, либо считать количество в отдельную переменную.

---

### BUG-2. Heater ramp уничтожает свою интерполяцию
**Файл:** [heater.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/heater.cpp#L104-L119)

```cpp
void update() {
    ...
    float progress = (float)elapsed / (float)rampDuration;
    uint8_t newPower = currentPower + progress * (targetPower - currentPower);
    //                  ↑ currentPower уже обновлён setPower()!
    setPower(newPower);
    // setPower() записывает currentPower = newPower → следующая итерация "стартует" от newPower
}
```

[rampTo()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/heater.cpp#67-83) не сохраняет начальную мощность. `currentPower` обновляется в [setPower()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/heater.cpp#41-54), и формула `currentPower + progress * (target - currentPower)` сходится не линейно, а экспоненциально. Разгон получается нелинейным.

**Исправление:** Добавить `static uint8_t rampStartPower;` и использовать его в интерполяции.

---

### BUG-3. Мёртвый `return` в getMashPhaseString
**Файл:** [webserver.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/webserver.cpp#L170-L176)

```cpp
case MashPhase::FINISH:
    return "finish";
    return "finish";   // ← строка 172: дублированный return (dead code)
```

Не влияет на логику, но говорит о copy-paste ошибке.

---

### BUG-4. `flowPulseCount` — гонка данных (race condition)
**Файл:** [sensors.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/sensors.cpp#L567-L596)

```cpp
static volatile uint32_t flowPulseCount = 0; // записывается из ISR

void readWaterFlow(WaterFlow& flow) {
    float litersPerSec = flowPulseCount / pulsesPerLiter;  // ← чтение без блокировки!
    flow.litersPerMin = litersPerSec * 60.0f * ...;        // ← ещё одно чтение!
    totalLiters += litersPerSec;                           // ← третье обращение
    flow.flowing = (flowPulseCount > 0);                   // ← четвёртое!

    noInterrupts();
    flowPulseCount = 0;    // ← обнуление
    interrupts();
}
```

> [!WARNING]
> Между четырьмя чтениями `flowPulseCount` ISR может изменить значение. Правильно: один раз скопировать в локальную переменную под `noInterrupts()`.

---

### BUG-5. `totalSteps` — несовпадение типов `uint32_t` vs `long`
**Файл:** [pump.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/pump.cpp#L191-L206)

```cpp
static uint32_t totalSteps = 0;  // unsigned

void update() {
    long currentPos = stepper.currentPosition();  // signed!
    if (currentPos != totalSteps) {  // сравнение signed/unsigned
        totalSteps = currentPos;     // неявное приведение signed→unsigned
```

При реверсе направления `currentPos` будет отрицательным → `totalSteps` станет огромным → `totalVolumeMl` взлетит.

---

### BUG-6. Гидрометр читает тот же ADC канал, что и давление куба
**Файл:** [sensors.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/sensors.cpp#L434-L464)

```cpp
void readHydrometer(Hydrometer& hydro, float temperature) {
    int16_t adc = ads1115.readADC_SingleEnded(ADS_CHANNEL_PRESSURE);
    //                                         ↑ тот же канал 0!
```

В [readPressure()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/sensors.cpp#402-433) тоже используется `ADS_CHANNEL_PRESSURE = 0`. Результат: гидрометр и давление куба показывают одинаковые сырые данные. Более того, [readHydrometer()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/sensors.cpp#434-465) вызывается сразу после [readPressure()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/sensors.cpp#402-433) (main.cpp:227) → двойное обращение к I2C за одну итерацию.

---

## 🏗️ Архитектурные проблемы

### ARCH-1. Монолит [webserver.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/webserver.cpp) — 3577 строк
Этот файл содержит **все** 40+ REST API endpoints в одной функции [init()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/mqtt.cpp#53-161). Навигация невозможна.

**Рекомендация:** Разбить на файлы по доменам:
- `api_process.cpp` — start/stop/pause/resume
- `api_settings.cpp` — equipment/safety/rect/dist
- `api_system.cpp` — health/version/reboot/export
- `api_cloud.cpp` — claim/config

---

### ARCH-2. Дублирование кода webserver ↔ cloud_tunnel
Функции дублированы 1:1 между [webserver.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/webserver.cpp) и [cloud_tunnel.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/cloud_tunnel.cpp):

| Функция | webserver.cpp | cloud_tunnel.cpp |
|---|---|---|
| [getModeString()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/webserver.cpp#105-128) / [getModeToken()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/cloud_tunnel.cpp#75-87) | строка 106 | строка 76 |
| [getPhaseString()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/webserver.cpp#129-155) / [getPhaseToken()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/cloud_tunnel.cpp#88-103) | строка 129 | строка 88 |
| [fillAlarmJson()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/cloud_tunnel.cpp#104-116) | строка 210 | строка 104 |
| Обработка `/api/process/start` | строка 805 | строка 320 |
| Обработка `/api/safety/ack/reset` | строка 1050 | строка 288 |

> [!IMPORTANT]
> При добавлении нового режима (NBK) в webserver, cloud_tunnel **не обновлён** — [getModeToken()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/cloud_tunnel.cpp#75-87) не обрабатывает `Mode::NBK` и `Mode::FERMENTATION`, возвращая `"unknown"`.

---

### ARCH-3. Блокирующие `delay()` в [valves.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/valves.cpp)
**Файл:** [valves.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/valves.cpp#L162-L186)

```cpp
void setFraction(Fraction fraction, bool smooth) {
    if (smooth) {
        for (int angle = currentAngle; angle != targetAngle; angle += step) {
            fractionatorServo.write(angle);
            delay(15);   // ← блокирует loop на ~1с при повороте 60°
        }
    }
    delay(SERVO_MOVE_DELAY_MS);   // ← ещё 2000мс блокировки!
}
```

Итого при плавном повороте фракционника: `delay(15) × n° + 2000мс`. При повороте на 144° → **4+ секунд блокировки loop'а**. Safety check, WebSocket, WDT — всё остановлено.

---

### ARCH-4. `static` локальные переменные в API-обработчиках (webserver.cpp)
```cpp
// строка 907:
static MashProfile runtimeProfile;  // static в лямбде!
// строка 976:
static TempStep runtimeSteps[10];
```

`static` в лямбде означает, что данные живут вечно и переиспользуются между вызовами. Если пользователь отправит два одновременных POST-запроса (маловероятно, но возможно через cloud tunnel + web UI), данные перезапишутся.

---

## 🔒 Безопасность

### SEC-1. MQTT без TLS
PubSubClient по умолчанию использует `WiFiClient` (plain TCP). Все MQTT-сообщения (включая команды [stop](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/pump.cpp#116-125), `heater`, `valves`) передаются открытым текстом.

**Исправление:** Использовать `WiFiClientSecure` вместо `WiFiClient`:
```cpp
static WiFiClientSecure wifiClient;
wifiClient.setInsecure(); // или с сертификатом
```

---

### SEC-2. Cloud Tunnel claim на millis(), а не на реальном времени
**Файл:** [cloud_tunnel.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/cloud_tunnel.cpp#L491)

```cpp
claimExpiresAt = (uint32_t)(millis() / 1000UL) + ttlSeconds;
```

`millis()` переполняется через ~49 дней. Если устройство работает непрерывно, claim'ы начнут некорректно истекать.

---

### SEC-3. MQTT команды без авторизации
Любой, кто подключится к MQTT-брокеру, может отправлять топики в `smart-column/{id}/cmd/heater` и управлять ТЭНом. Нет проверки подписи/ACL.

---

## ⚡ Производительность и память

### PERF-1. [String](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/webserver.cpp#105-128) конкатенация в MQTT Discovery
**Файл:** [mqtt.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/mqtt.cpp#L287-L388)

```cpp
String payload = String("{") +
    "\"name\":\"Cube Temperature\"," +
    "\"uniq_id\":\"" + deviceId + "_cube_temp\"," + ...
```

5 Discovery-сообщений, каждое — ~10 конкатенаций [String](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/webserver.cpp#105-128). На ESP32 это ~60+ аллокаций кучи при каждом подключении.

**Рекомендация:** Использовать `ArduinoJson` (который уже подключён) вместо ручной конкатенации.

---

### PERF-2. `/api/status` — гигантский JSON каждые 2 сек
В [broadcastState()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/interface/webserver.cpp#3281-3564) формируется полный JSON-пакет по WebSocket каждые `INTERVAL_WEB_BROADCAST = 2000мс`. Пакет включает **все** настройки, equipment, cloud, safety, display stats — ~3-4 KB.

**Рекомендация:** Разделить на:
- **delta-broadcast** каждые 2с (только temps, power, phase, pump)
- **full-broadcast** каждые 10с (уже есть `INTERVAL_WEB_BROADCAST_FULL = 10000`, но не используется!)

---

### PERF-3. `Heater::update()` никогда не вызывается из loop
В [main.cpp](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/main.cpp) loop нет вызова `Heater::update()`, значит функция [ramp()](file:///c:/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7/src/drivers/heater.cpp#67-83) **не работает**. Она реализована, но не интегрирована.

---

## 📋 Сводная таблица

| ID | Тип | Критичность | Файл | Описание |
|---|---|---|---|---|
| BUG-1 | Баг | 🔴 | sensors.cpp:637 | `tempSensorsOk` bool вместо count → неверный Health Score |
| BUG-2 | Баг | 🟡 | heater.cpp:116 | Ramp экспоненциальный вместо линейного |
| BUG-3 | Баг | ⚪ | webserver.cpp:172 | Dead return (копипаста) |
| BUG-4 | Баг | 🔴 | sensors.cpp:577 | Race condition на flowPulseCount |
| BUG-5 | Баг | 🟡 | pump.cpp:199 | Signed/unsigned mismatch totalSteps |
| BUG-6 | Баг | 🟡 | sensors.cpp:444 | Гидрометр и давление — один ADC канал |
| ARCH-1 | Архитектура | 🟡 | webserver.cpp | 3577 строк монолит |
| ARCH-2 | Архитектура | 🔴 | webserver+cloud | Дупликация кода, рассинхрон NBK/Ferm |
| ARCH-3 | Архитектура | 🔴 | valves.cpp:162 | delay(15)×n + 2000мс в setFraction |
| ARCH-4 | Архитектура | 🟡 | webserver.cpp:907 | static в лямбдах API |
| SEC-1 | Безопасность | 🟡 | mqtt.cpp | MQTT без TLS |
| SEC-2 | Безопасность | ⚪ | cloud_tunnel.cpp:491 | millis() overflow для claim TTL |
| SEC-3 | Безопасность | 🟡 | mqtt.cpp | Команды без авторизации |
| PERF-1 | Память | 🟡 | mqtt.cpp:297 | String concat → фрагментация |
| PERF-2 | Производительность | 🟡 | webserver.cpp | Полный JSON каждые 2с |
| PERF-3 | Баг | 🔴 | main.cpp | `Heater::update()` нигде не вызывается → ramp не работает |
