/**
 * Smart-Column S3 - Драйвер датчиков
 *
 * Реализация для:
 * - DS18B20 ×7 (температуры)
 * - BMP280 ×2 (атмосферное давление)
 * - ADS1115 + MPX5010DP (давление куба/ареометр)
 * - PZEM-004T v3.0 (напряжение, ток, мощность, энергия, частота, PF)
 * - YF-S201 (поток воды)
 */

#include "sensors.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_ADS1X15.h>
#include <PZEM004Tv30.h>
#include <WiFi.h>

// =============================================================================
// ГЛОБАЛЬНЫЕ ОБЪЕКТЫ
// =============================================================================

// OneWire и DS18B20
static OneWire oneWire(PIN_ONEWIRE);
static DallasTemperature ds18b20(&oneWire);

// BMP280 (два датчика на разных адресах)
static Adafruit_BMP280 bmp280_1;
static Adafruit_BMP280 bmp280_2;
static bool bmp1_ok = false;
static bool bmp2_ok = false;

// ADS1115 (16-бит АЦП)
static Adafruit_ADS1115 ads1115;
static bool ads_ok = false;

// PZEM-004T v3.0 (измеритель мощности)
static HardwareSerial pzemSerial(PZEM_UART_NUM);
static PZEM004Tv30 pzem(pzemSerial, PIN_PZEM_RX, PIN_PZEM_TX);
static bool pzem_ok = false;

// Защита от переполнения energy (PZEM может сбросить счётчик)
static float lastEnergyReading = 0.0f;
static float energyOffset = 0.0f;        // Накопленная энергия от предыдущих сбросов
static bool energyInitialized = false;

// Фильтрация выбросов PZEM (spike rejection)
static float lastValidVoltage = 0.0f;
static float lastValidCurrent = 0.0f;
static float lastValidPower = 0.0f;
static bool pzemDataInitialized = false;

// Пороги для определения выбросов (допустимое изменение за одно чтение)
#define PZEM_VOLTAGE_MAX_DELTA    30.0f   // ±30V за раз
#define PZEM_CURRENT_MAX_DELTA    5.0f    // ±5A за раз
#define PZEM_POWER_MAX_DELTA      1000.0f // ±1000W за раз

// Счётчики для мониторинга здоровья
static uint16_t pzemSpikeCounter = 0;
static uint16_t tempReadErrorCounter = 0;

// Калибровка
static TempCalibration tempCal;

// Датчик потока воды (счётчик импульсов)
static volatile uint32_t flowPulseCount = 0;
static uint32_t lastFlowCheck = 0;
static float totalLiters = 0;

// Адреса DS18B20
static DeviceAddress ds18b20Addresses[TEMP_COUNT];
static bool ds18b20Found[TEMP_COUNT] = {false};
static uint8_t ds18b20Count = 0;
static uint32_t lastDs18b20DiscoveryMs = 0;

// Асинхронное чтение DS18B20
static bool conversionInProgress = false;
static uint32_t conversionStartTime = 0;
static const uint16_t CONVERSION_TIME_MS = 750; // 12-бит разрешение
static const uint32_t DISCOVERY_RETRY_MS = 2000;
static const uint8_t DISCOVERY_INIT_ATTEMPTS = 4;
static const uint16_t DISCOVERY_INIT_DELAY_MS = 250;
static uint8_t consecutiveTempReadFailures = 0;

// =============================================================================
// ISR ДАТЧИКА ПОТОКА
// =============================================================================

void IRAM_ATTR flowPulseISR() {
    flowPulseCount++;
}

// =============================================================================
// ВНУТРЕННИЕ ФУНКЦИИ
// =============================================================================

/**
 * Фильтрация выброса (spike rejection)
 * Возвращает true если значение адекватное, false если выброс
 */
static bool validateReading(float newValue, float lastValue, float maxDelta, bool initialized) {
    if (!initialized) {
        return true; // Первое чтение - принимаем
    }

    float delta = fabs(newValue - lastValue);
    return (delta <= maxDelta);
}

/**
 * Интерполяция крепости по таблице калибровки
 */
static float interpolateABV(float pressure, const HydrometerCalibration& cal) {
    if (cal.pointCount < 2) {
        return 0.0f;
    }

    // Найти два ближайших калибровочных значения
    for (uint8_t i = 0; i < cal.pointCount - 1; i++) {
        if (pressure >= cal.pressurePoints[i] && pressure <= cal.pressurePoints[i + 1]) {
            // Линейная интерполяция
            float p0 = cal.pressurePoints[i];
            float p1 = cal.pressurePoints[i + 1];
            float a0 = cal.abvPoints[i];
            float a1 = cal.abvPoints[i + 1];

            float t = (pressure - p0) / (p1 - p0);
            return a0 + t * (a1 - a0);
        }
    }

    // Экстраполяция (за пределами калибровки)
    if (pressure < cal.pressurePoints[0]) {
        return cal.abvPoints[0];
    }
    return cal.abvPoints[cal.pointCount - 1];
}

static void clearDs18b20Inventory() {
    memset(ds18b20Addresses, 0, sizeof(ds18b20Addresses));
    memset(ds18b20Found, 0, sizeof(ds18b20Found));
    ds18b20Count = 0;
}

static void logDs18b20Inventory(uint8_t count) {
    LOG_I("Sensors: Found %u DS18B20 devices", count);
    for (uint8_t i = 0; i < count; i++) {
        LOG_D("Sensors: DS18B20[%d] = %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
              i,
              ds18b20Addresses[i][0], ds18b20Addresses[i][1],
              ds18b20Addresses[i][2], ds18b20Addresses[i][3],
              ds18b20Addresses[i][4], ds18b20Addresses[i][5],
              ds18b20Addresses[i][6], ds18b20Addresses[i][7]);
    }
}

static uint8_t discoverDs18b20(bool logInventory) {
    DeviceAddress addr;
    uint8_t count = 0;

    clearDs18b20Inventory();
    oneWire.reset_search();

    while (oneWire.search(addr) && count < TEMP_COUNT) {
        if (OneWire::crc8(addr, 7) != addr[7]) {
            continue;
        }

        memcpy(ds18b20Addresses[count], addr, sizeof(DeviceAddress));
        ds18b20Found[count] = true;
        ds18b20.setResolution(ds18b20Addresses[count], 12);
        count++;
    }

    oneWire.reset_search();
    ds18b20Count = count;
    lastDs18b20DiscoveryMs = millis();

    if (count == 0) {
        conversionInProgress = false;
        conversionStartTime = 0;
    }

    if (logInventory) {
        logDs18b20Inventory(count);
    }

    return count;
}

static void startTemperatureConversion(uint32_t now) {
    if (ds18b20Count == 0) {
        return;
    }

    ds18b20.requestTemperatures();
    conversionInProgress = true;
    conversionStartTime = now;
}

static void ensureDs18b20Available(uint32_t now) {
    const bool shouldRetry =
        ds18b20Count == 0 &&
        (lastDs18b20DiscoveryMs == 0 ||
         now - lastDs18b20DiscoveryMs >= DISCOVERY_RETRY_MS);

    if (!shouldRetry) {
        return;
    }

    const uint8_t count = discoverDs18b20(true);
    if (count > 0) {
        startTemperatureConversion(now);
    }
}

// =============================================================================
// ПУБЛИЧНЫЙ ИНТЕРФЕЙС
// =============================================================================

namespace Sensors {

void init() {
    LOG_I("Sensors: Initializing...");

    // Инициализация I2C
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    // DS18B20
    pinMode(PIN_ONEWIRE, INPUT_PULLUP);
    delay(10);
    ds18b20.begin();
    // Не блокировать цикл во время конвертации температуры
    ds18b20.setWaitForConversion(false);
    ds18b20.setCheckForConversion(false);

    uint8_t deviceCount = 0;
    for (uint8_t attempt = 0; attempt < DISCOVERY_INIT_ATTEMPTS; attempt++) {
        if (attempt > 0) {
            delay(DISCOVERY_INIT_DELAY_MS);
        }

        deviceCount = discoverDs18b20(false);
        if (deviceCount > 0) {
            break;
        }
    }
    logDs18b20Inventory(deviceCount);

    if (deviceCount > 0) {
        startTemperatureConversion(millis());
    }

    // BMP280 #1
    bmp1_ok = bmp280_1.begin(I2C_ADDR_BMP280_1);
    if (bmp1_ok) {
        bmp280_1.setSampling(Adafruit_BMP280::MODE_NORMAL,
                             Adafruit_BMP280::SAMPLING_X2,
                             Adafruit_BMP280::SAMPLING_X16,
                             Adafruit_BMP280::FILTER_X16,
                             Adafruit_BMP280::STANDBY_MS_500);
        LOG_I("Sensors: BMP280 #1 OK (0x%02X)", I2C_ADDR_BMP280_1);
    } else {
        LOG_E("Sensors: BMP280 #1 NOT FOUND");
    }

    // BMP280 #2 (опциональный)
    bmp2_ok = bmp280_2.begin(I2C_ADDR_BMP280_2);
    if (bmp2_ok) {
        bmp280_2.setSampling(Adafruit_BMP280::MODE_NORMAL,
                             Adafruit_BMP280::SAMPLING_X2,
                             Adafruit_BMP280::SAMPLING_X16,
                             Adafruit_BMP280::FILTER_X16,
                             Adafruit_BMP280::STANDBY_MS_500);
        LOG_I("Sensors: BMP280 #2 OK (0x%02X)", I2C_ADDR_BMP280_2);
    }

    // ADS1115
    ads_ok = ads1115.begin(I2C_ADDR_ADS1115);
    if (ads_ok) {
        // Gain 1 = ±4.096V (для MPX5010DP: 0.2V-4.7V)
        ads1115.setGain(GAIN_ONE);
        LOG_I("Sensors: ADS1115 OK (0x%02X)", I2C_ADDR_ADS1115);
    } else {
        LOG_E("Sensors: ADS1115 NOT FOUND");
    }

#if PZEM_ENABLED
    // PZEM-004T v3.0
    pzemSerial.begin(PZEM_BAUD_RATE, SERIAL_8N1, PIN_PZEM_RX, PIN_PZEM_TX);
    delay(100);

    pzem_ok = false;
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        float testVoltage = pzem.voltage();
        float testFreq = pzem.frequency();

        if (!isnan(testVoltage) && !isnan(testFreq)) {
            if (testVoltage > 0 && testFreq >= 45 && testFreq <= 65) {
                pzem_ok = true;
                LOG_I("Sensors: PZEM-004T OK (V=%.1fV, F=%.1fHz)", testVoltage, testFreq);
                break;
            } else if (testVoltage == 0) {
                pzem_ok = true;
                LOG_WARN("Sensors: PZEM-004T OK but NO AC POWER detected");
                break;
            }
        }

        if (attempt < 2) {
            delay(200);
        }
    }

    if (!pzem_ok) {
        LOG_E("Sensors: PZEM-004T communication FAILED after 3 attempts");
    }
#else
    pzem_ok = false;
    LOG_W("Sensors: PZEM disabled by build flag (PZEM_ENABLED=0)");
#endif

    // Датчик потока воды
    pinMode(PIN_FLOW_SENSOR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_SENSOR), flowPulseISR, RISING);

    // Обнулить калибровку
    memset(&tempCal, 0, sizeof(tempCal));

    LOG_I("Sensors: Init complete");
}

void readTemperatures(Temperatures& temps) {
    uint32_t now = millis();
    ensureDs18b20Available(now);

    if (ds18b20Count == 0) {
        return;
    }

    // Фаза 1: Запуск конвертации (неблокирующий)
    if (!conversionInProgress) {
        startTemperatureConversion(now);
        return; // Выходим, не блокируя выполнение
    }

    // Фаза 2: Чтение результатов (только если прошло достаточно времени)
    if (now - conversionStartTime < CONVERSION_TIME_MS) {
        return; // Конвертация ещё идёт, ждём
    }

    // Прочитать значения (конвертация завершена)
    float values[TEMP_COUNT];
    uint8_t validCount = 0;
    for (uint8_t i = 0; i < TEMP_COUNT; i++) {
        if (ds18b20Found[i]) {
            float raw = ds18b20.getTempC(ds18b20Addresses[i]);

            // Проверка валидности (-127 = ошибка)
            if (raw == DEVICE_DISCONNECTED_C || raw < -50 || raw > 150) {
                temps.valid[i] = false;
                values[i] = 0;
            } else {
                temps.valid[i] = true;
                values[i] = raw + tempCal.offsets[i];
                validCount++;
            }
        } else {
            temps.valid[i] = false;
            values[i] = 0;
        }
    }

    // Записать в структуру
    temps.cube = values[TEMP_CUBE];
    temps.columnBottom = values[TEMP_COLUMN_BOTTOM];
    temps.columnTop = values[TEMP_COLUMN_TOP];
    temps.reflux = values[TEMP_REFLUX];
    temps.tsa = values[TEMP_TSA];
    temps.waterIn = values[TEMP_WATER_IN];
    temps.waterOut = values[TEMP_WATER_OUT];

    if (validCount > 0) {
        temps.lastUpdate = now;
        consecutiveTempReadFailures = 0;
    } else {
        consecutiveTempReadFailures++;
        tempReadErrorCounter++;

        if (consecutiveTempReadFailures >= 3 &&
            now - lastDs18b20DiscoveryMs >= DISCOVERY_RETRY_MS) {
            LOG_W("Sensors: DS18B20 read failed, re-scanning temperature bus");
            discoverDs18b20(true);
        }
    }

    // Сброс флага для следующего цикла
    conversionInProgress = false;
}

void readPressure(Pressure& pressure) {
    // Атмосферное давление (BMP280)
    if (bmp1_ok) {
        pressure.atmosphere = bmp280_1.readPressure() / 100.0f; // Па → гПа
    } else if (bmp2_ok) {
        pressure.atmosphere = bmp280_2.readPressure() / 100.0f;
    } else {
        pressure.atmosphere = 1013.25f; // Стандартное
    }

    // Давление в кубе (MPX5010DP через ADS1115)
    if (ads_ok) {
        int16_t adc = ads1115.readADC_SingleEnded(ADS_CHANNEL_PRESSURE);
        float voltage = ads1115.computeVolts(adc);

        // MPX5010DP: 0.2V @ 0kPa, 4.7V @ 10kPa
        // P = (V - offset) / sensitivity
        float kPa = (voltage - MPX5010_OFFSET) / MPX5010_SENSITIVITY;

        // Преобразовать в мм рт.ст. (1 кПа = 7.50062 мм рт.ст.)
        pressure.cube = kPa * 7.50062f;

        // Ограничить диапазон
        if (pressure.cube < 0) pressure.cube = 0;
        if (pressure.cube > 75) pressure.cube = 75; // 10 кПа = ~75 мм рт.ст.
    } else {
        pressure.cube = 0;
    }

    pressure.lastUpdate = millis();
}

void readHydrometer(Hydrometer& hydro, float temperature) {
    // Читаем дифференциальное давление (столб жидкости в попугае)
    // Используем тот же канал, что и давление куба
    // В реальной системе нужен отдельный канал ADS1115

    if (!ads_ok) {
        hydro.valid = false;
        return;
    }

    int16_t adc = ads1115.readADC_SingleEnded(ADS_CHANNEL_PRESSURE);
    float voltage = ads1115.computeVolts(adc);
    float kPa = (voltage - MPX5010_OFFSET) / MPX5010_SENSITIVITY;

    // Плотность (упрощённо, без учёта высоты столба)
    // ρ = ΔP / (g × h), где g=9.81, h=высота_попугая (м)
    // Для примера: h = 0.1м
    const float height_m = 0.1f;
    hydro.pressure = (kPa * 1000) / (9.81f * height_m); // г/см³

    // ABV (здесь нужна калибровочная таблица)
    // Пока используем упрощённую формулу
    hydro.abv = (1.0f - hydro.pressure) * 100.0f;

    // Температурная коррекция (упрощённо)
    hydro.temperature = temperature;

    // Валидация
    hydro.valid = (hydro.pressure > 0.7f && hydro.pressure < 1.1f);
    hydro.lastUpdate = millis();
}

void readPower(Power& power) {
    if (!pzem_ok) {
        // PZEM не инициализирован
        power.voltage = 0;
        power.current = 0;
        power.power = 0;
        power.energy = 0;
        power.frequency = 0;
        power.powerFactor = 0;
        power.lastUpdate = millis();
        return;
    }

    // Читаем все параметры с PZEM-004T
    float rawVoltage = pzem.voltage();
    float rawCurrent = pzem.current();
    float rawPower = pzem.power();
    float rawEnergy = pzem.energy();
    float rawFrequency = pzem.frequency();
    float rawPF = pzem.pf();

    // Проверка на NaN и базовые диапазоны
    if (isnan(rawVoltage) || rawVoltage < 0 || rawVoltage > 300) {
        rawVoltage = lastValidVoltage;
    }
    if (isnan(rawCurrent) || rawCurrent < 0 || rawCurrent > PZEM_CURRENT_MAX) {
        rawCurrent = lastValidCurrent;
    }
    if (isnan(rawPower) || rawPower < 0 || rawPower > 10000) {
        rawPower = lastValidPower;
    }

    // Фильтрация выбросов (spike rejection)
    if (validateReading(rawVoltage, lastValidVoltage, PZEM_VOLTAGE_MAX_DELTA, pzemDataInitialized)) {
        power.voltage = rawVoltage;
        lastValidVoltage = rawVoltage;
    } else {
        power.voltage = lastValidVoltage; // Отбросить выброс
        pzemSpikeCounter++;
        LOG_WARN("PZEM: Voltage spike rejected (%.1fV -> %.1fV)", lastValidVoltage, rawVoltage);
    }

    if (validateReading(rawCurrent, lastValidCurrent, PZEM_CURRENT_MAX_DELTA, pzemDataInitialized)) {
        power.current = rawCurrent;
        lastValidCurrent = rawCurrent;
    } else {
        power.current = lastValidCurrent; // Отбросить выброс
        pzemSpikeCounter++;
        LOG_WARN("PZEM: Current spike rejected (%.2fA -> %.2fA)", lastValidCurrent, rawCurrent);
    }

    if (validateReading(rawPower, lastValidPower, PZEM_POWER_MAX_DELTA, pzemDataInitialized)) {
        power.power = rawPower;
        lastValidPower = rawPower;
    } else {
        power.power = lastValidPower; // Отбросить выброс
        pzemSpikeCounter++;
        LOG_WARN("PZEM: Power spike rejected (%.1fW -> %.1fW)", lastValidPower, rawPower);
    }

    // Частота и PF (без фильтрации, но с валидацией)
    if (isnan(rawFrequency) || rawFrequency < 45 || rawFrequency > 65) {
        power.frequency = 50; // По умолчанию 50 Гц
    } else {
        power.frequency = rawFrequency;
    }

    if (isnan(rawPF) || rawPF < 0 || rawPF > 1) {
        power.powerFactor = 0;
    } else {
        power.powerFactor = rawPF;
    }

    // Отметить как инициализированные
    pzemDataInitialized = true;

    // Обработка energy с защитой от переполнения/сброса
    if (isnan(rawEnergy) || rawEnergy < 0) {
        rawEnergy = 0;
    }

    if (!energyInitialized) {
        // Первое чтение - инициализация
        lastEnergyReading = rawEnergy;
        energyOffset = 0;
        energyInitialized = true;
    } else {
        // Проверка на сброс счётчика (значение уменьшилось)
        if (rawEnergy < lastEnergyReading - 0.01f) {  // -0.01 для защиты от флуктуаций
            // Счётчик был сброшен - сохраняем предыдущее значение
            energyOffset += lastEnergyReading;
            LOG_WARN("Sensors: PZEM energy counter reset detected (was %.3f kWh)", lastEnergyReading);
        }
        lastEnergyReading = rawEnergy;
    }

    // Итоговая энергия = offset + текущее показание
    power.energy = energyOffset + rawEnergy;

    power.lastUpdate = millis();
}

void readWaterFlow(WaterFlow& flow) {
    uint32_t now = millis();
    uint32_t elapsed = now - lastFlowCheck;

    if (elapsed >= 1000) { // Обновляем раз в секунду
        // YF-S201: ~7.5 импульсов на литр (зависит от модели)
        const float pulsesPerLiter = 7.5f;

        // Вычислить л/мин
        float litersPerSec = flowPulseCount / pulsesPerLiter;
        flow.litersPerMin = litersPerSec * 60.0f * (1000.0f / elapsed);

        // Общий объём
        totalLiters += litersPerSec;
        flow.totalLiters = totalLiters;

        // Проверка потока
        flow.flowing = (flowPulseCount > 0);

        // Обнулить счётчик
        noInterrupts();
        flowPulseCount = 0;
        interrupts();

        lastFlowCheck = now;
    }

    flow.lastPulse = millis();
}

void applyCalibration(const TempCalibration& cal) {
    memcpy(&tempCal, &cal, sizeof(tempCal));
    LOG_I("Sensors: Temperature calibration applied");
}

uint8_t scanDS18B20(uint8_t addresses[][8]) {
    uint8_t count = 0;
    DeviceAddress addr;

    oneWire.reset_search();
    while (oneWire.search(addr) && count < TEMP_COUNT) {
        // Проверить CRC
        if (OneWire::crc8(addr, 7) == addr[7]) {
            memcpy(addresses[count], addr, 8);
            count++;
        }
    }

    return count;
}

bool isTempSensorValid(uint8_t index) {
    if (index >= TEMP_COUNT) return false;

    if (!ds18b20Found[index]) return false;

    // Попробовать прочитать
    float temp = ds18b20.getTempC(ds18b20Addresses[index]);
    return (temp != DEVICE_DISCONNECTED_C && temp > -50 && temp < 150);
}

void updateHealth(SystemHealth& health) {
    // Подсчёт работающих датчиков температуры
    health.tempSensorsTotal = 0;
    health.tempSensorsOk = 0;
    for (uint8_t i = 0; i < TEMP_COUNT; i++) {
        if (ds18b20Found[i]) {
            health.tempSensorsTotal++;
            if (isTempSensorValid(i)) {
                health.tempSensorsOk = true;
            }
        }
    }

    // Состояние других датчиков
    health.bmp280Ok = bmp1_ok || bmp2_ok;
    health.ads1115Ok = ads_ok;
    health.pzemOk = pzem_ok;

    // Счётчики ошибок
    health.pzemSpikeCount = pzemSpikeCounter;
    health.tempReadErrors = tempReadErrorCounter;

    // WiFi
    health.wifiConnected = (WiFi.status() == WL_CONNECTED);
    health.wifiRSSI = WiFi.RSSI();

    // Системная информация
    health.uptime = millis() / 1000;
    health.freeHeap = ESP.getFreeHeap();
    health.cpuTemp = (uint8_t)temperatureRead(); // ESP32-S3 internal temp sensor

    // Расчёт общего здоровья (0-100%)
    int16_t score = 100;

    // Снижение за каждый неработающий критичный датчик
    if (!health.pzemOk) score -= 30; // Критично для контроля мощности
    if (!health.ads1115Ok) score -= 15; // Давление
    if (!health.bmp280Ok) score -= 5;

    // Снижение за неработающие температурные датчики
    if (health.tempSensorsTotal > 0) {
        uint8_t tempFailures = health.tempSensorsTotal - health.tempSensorsOk;
        score -= (tempFailures * 15); // Каждый датчик важен
    } else {
        score -= 50; // Вообще нет датчиков
    }

    // Снижение за проблемы с WiFi (только если не в режиме AP)
    if (!health.wifiConnected && WiFi.getMode() != WIFI_AP) {
        score -= 10;
    } else if (health.wifiConnected && health.wifiRSSI < -85) {
        score -= 5;  // Очень слабый сигнал
    }

    // Снижение за высокий уровень ошибок
    if (health.pzemSpikeCount > 50) score -= 5;
    if (health.pzemSpikeCount > 200) score -= 10;
    if (health.tempReadErrors > 20) score -= 5;
    if (health.tempReadErrors > 100) score -= 10;

    // Снижение за системные проблемы
    if (health.freeHeap < 32768) score -= 10; // Мало памяти
    if (health.cpuTemp > 85) score -= 20;     // Перегрев

    // Штраф за WDT перезагрузку в прошлом
    if (health.lastRebootReason == 3 || health.lastRebootReason == 4) { // ESP_RST_WDT, ESP_RST_TASK_WDT
        score -= 20;
    }

    // Ограничение 0-100
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    health.overallHealth = (uint8_t)score;

    health.lastUpdate = millis();
}

} // namespace Sensors
