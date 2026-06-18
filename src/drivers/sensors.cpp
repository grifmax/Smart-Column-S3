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
#include "ds2482_100.h"
#include <freertos/semphr.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_ADS1X15.h>
#include <PZEM004Tv30.h>
#include <WiFi.h>

const float SystemHealth::healthWeights[6] = {0.4f, 0.1f, 0.2f, 0.05f, 0.05f,
                                              0.2f};

// =============================================================================
// ГЛОБАЛЬНЫЕ ОБЪЕКТЫ

// OneWire и DS18B20
static OneWire oneWire(PIN_ONEWIRE);
static DallasTemperature ds18b20(&oneWire);
static Ds2482_100 ds2482;
static SemaphoreHandle_t ds18b20Mutex = nullptr;
static bool gpioTempBusReady = false;
static bool ds2482_ok = false;
static bool tempBusConfigured = false;
static bool tempBusUseDs2482 = false;
static uint8_t tempBusDs2482Address = I2C_ADDR_DS2482_DEFAULT;
static uint32_t lastDs2482InitAttemptMs = 0;

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
static uint32_t lastPzemProbeMs = 0;
static uint8_t pzemConsecutiveReadFailures = 0;

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
static const uint32_t PZEM_RETRY_INTERVAL_MS = 5000UL;
static const uint8_t PZEM_READ_FAIL_LIMIT = 3;
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
static const uint32_t DS2482_RETRY_MS = 2000;
static const uint8_t DISCOVERY_INIT_ATTEMPTS = 4;
static const uint16_t DISCOVERY_INIT_DELAY_MS = 250;
static const uint8_t DISCOVERY_BUS_PASSES = 6;
static const uint8_t DISCOVERY_PASS_DELAY_MS = 80;
static uint8_t consecutiveTempReadFailures = 0;

static bool probePzem(bool verboseFailureLog = true) {
    lastPzemProbeMs = millis();
    pzemSerial.begin(PZEM_BAUD_RATE, SERIAL_8N1, PIN_PZEM_RX, PIN_PZEM_TX);
    delay(100);

    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        const float testVoltage = pzem.voltage();
        const float testFreq = pzem.frequency();

        if (!isnan(testVoltage) && !isnan(testFreq)) {
            if (testVoltage > 0 && testFreq >= 45 && testFreq <= 65) {
                pzem_ok = true;
                pzemConsecutiveReadFailures = 0;
                LOG_I("Sensors: PZEM-004T OK (V=%.1fV, F=%.1fHz)", testVoltage, testFreq);
                return true;
            }
            if (testVoltage == 0) {
                pzem_ok = true;
                pzemConsecutiveReadFailures = 0;
                LOG_WARN("Sensors: PZEM-004T OK but NO AC POWER detected");
                return true;
            }
        }

        if (attempt < 2) {
            delay(200);
        }
    }

    pzem_ok = false;
    if (verboseFailureLog) {
        LOG_E("Sensors: PZEM-004T communication FAILED after 3 attempts");
    }
    return false;
}

static bool lockDs18b20Bus(TickType_t timeoutTicks = pdMS_TO_TICKS(20)) {
    if (ds18b20Mutex == nullptr) {
        return false;
    }
    return xSemaphoreTake(ds18b20Mutex, timeoutTicks) == pdTRUE;
}

static void unlockDs18b20Bus() {
    if (ds18b20Mutex != nullptr) {
        xSemaphoreGive(ds18b20Mutex);
    }
}

static bool isZeroDeviceAddress(const DeviceAddress address) {
    for (uint8_t i = 0; i < sizeof(DeviceAddress); ++i) {
        if (address[i] != 0) {
            return false;
        }
    }
    return true;
}

static bool hasAssignedTempMap() {
    for (uint8_t i = 0; i < TEMP_COUNT; ++i) {
        if (!isZeroDeviceAddress(tempCal.addresses[i])) {
            return true;
        }
    }
    return false;
}

static int8_t findDs18b20AddressIndex(DeviceAddress addresses[], uint8_t count,
                                      const DeviceAddress candidate,
                                      const bool used[]) {
    for (uint8_t i = 0; i < count; ++i) {
        if (used && used[i]) {
            continue;
        }
        if (memcmp(addresses[i], candidate, sizeof(DeviceAddress)) == 0) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

static void invalidateTemperatureState(Temperatures& temps) {
    for (uint8_t i = 0; i < TEMP_COUNT; ++i) {
        temps.valid[i] = false;
    }
    temps.cube = 0.0f;
    temps.columnBottom = 0.0f;
    temps.columnTop = 0.0f;
    temps.reflux = 0.0f;
    temps.tsa = 0.0f;
    temps.waterIn = 0.0f;
    temps.waterOut = 0.0f;
}

static uint8_t sanitizeDs2482Address(uint8_t address) {
    if (address < I2C_ADDR_DS2482_0 || address > I2C_ADDR_DS2482_3) {
        return I2C_ADDR_DS2482_DEFAULT;
    }
    return address;
}

static bool shouldUseDs2482ForTemps() {
    return g_settings.equipment.useDs2482ForTemps;
}

static uint8_t getRequestedDs2482Address() {
    return sanitizeDs2482Address(g_settings.equipment.ds2482Address);
}

static const char* getTemperatureBusSourceKeyInternal() {
    return tempBusUseDs2482 ? "ds2482" : "gpio";
}

static const char* getTemperatureBusSourceLabelInternal() {
    return tempBusUseDs2482 ? "DS2482S-100" : "GPIO 1-Wire";
}

static bool isUsingDs2482Backend() {
    return tempBusUseDs2482;
}

static void clearDs18b20Inventory();

static void clearTemperatureTransportState() {
    clearDs18b20Inventory();
    conversionInProgress = false;
    conversionStartTime = 0;
    lastDs18b20DiscoveryMs = 0;
    consecutiveTempReadFailures = 0;
}

static bool initGpioTemperatureBus(bool verboseLog) {
    pinMode(PIN_ONEWIRE, INPUT_PULLUP);
    delay(10);
    ds18b20.begin();
    ds18b20.setWaitForConversion(false);
    ds18b20.setCheckForConversion(false);
    gpioTempBusReady = true;
    ds2482_ok = false;

    if (verboseLog) {
        LOG_I("Sensors: Temperature bus source = GPIO 1-Wire (pin %d)",
              PIN_ONEWIRE);
    }
    return true;
}

static bool initDs2482TemperatureBus(bool verboseLog) {
    lastDs2482InitAttemptMs = millis();
    const uint8_t address = getRequestedDs2482Address();
    ds2482_ok = ds2482.begin(Wire, address);
    gpioTempBusReady = false;

    if (verboseLog) {
        if (ds2482_ok) {
            LOG_I("Sensors: Temperature bus source = DS2482S-100 (I2C 0x%02X)",
                  address);
        } else {
            LOG_W("Sensors: DS2482S-100 not found at I2C 0x%02X", address);
        }
    }
    return ds2482_ok;
}

static bool ensureTemperatureBusConfigured(bool verboseLog = false) {
    const bool requestedDs2482 = shouldUseDs2482ForTemps();
    const uint8_t requestedAddress = getRequestedDs2482Address();
    const bool backendChanged = !tempBusConfigured ||
                                tempBusUseDs2482 != requestedDs2482 ||
                                tempBusDs2482Address != requestedAddress;

    if (backendChanged) {
        clearTemperatureTransportState();
        tempBusConfigured = true;
        tempBusUseDs2482 = requestedDs2482;
        tempBusDs2482Address = requestedAddress;

        if (requestedDs2482) {
            return initDs2482TemperatureBus(true);
        }
        return initGpioTemperatureBus(true);
    }

    if (tempBusUseDs2482 && !ds2482_ok &&
        millis() - lastDs2482InitAttemptMs >= DS2482_RETRY_MS) {
        return initDs2482TemperatureBus(verboseLog);
    }

    return tempBusUseDs2482 ? ds2482_ok : gpioTempBusReady;
}

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
static float interpolateABV(float densitySignal, const HydrometerCalibration& cal) {
    if (cal.pointCount < 2) {
        return 0.0f;
    }

    // Найти два ближайших калибровочных значения
    for (uint8_t i = 0; i < cal.pointCount - 1; i++) {
        if (densitySignal >= cal.pressurePoints[i] && densitySignal <= cal.pressurePoints[i + 1]) {
            // Линейная интерполяция
            float p0 = cal.pressurePoints[i];
            float p1 = cal.pressurePoints[i + 1];
            float a0 = cal.abvPoints[i];
            float a1 = cal.abvPoints[i + 1];

            if (fabsf(p1 - p0) < 0.0001f) {
                return a1;
            }

            float t = (densitySignal - p0) / (p1 - p0);
            return a0 + t * (a1 - a0);
        }
    }

    // Экстраполяция (за пределами калибровки)
    if (densitySignal < cal.pressurePoints[0]) {
        return cal.abvPoints[0];
    }
    return cal.abvPoints[cal.pointCount - 1];
}

static float interpolatePressureMmHg(float voltage, const PressureSensorCalibration& cal) {
    if (cal.pointCount < 2) {
        return NAN;
    }

    for (uint8_t i = 0; i < cal.pointCount - 1; i++) {
        if (voltage >= cal.voltagePoints[i] && voltage <= cal.voltagePoints[i + 1]) {
            const float v0 = cal.voltagePoints[i];
            const float v1 = cal.voltagePoints[i + 1];
            const float p0 = cal.pressurePoints[i];
            const float p1 = cal.pressurePoints[i + 1];
            if (fabsf(v1 - v0) < 0.0001f) {
                return p1;
            }

            const float t = (voltage - v0) / (v1 - v0);
            return p0 + t * (p1 - p0);
        }
    }

    if (voltage < cal.voltagePoints[0]) {
        return cal.pressurePoints[0];
    }
    return cal.pressurePoints[cal.pointCount - 1];
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

static bool isSupportedDs18Family(uint8_t familyCode) {
    return familyCode == 0x28 || familyCode == 0x10 || familyCode == 0x22;
}

static bool hasDs18b20Address(DeviceAddress addresses[], uint8_t count,
                              const DeviceAddress candidate) {
    for (uint8_t i = 0; i < count; ++i) {
        if (memcmp(addresses[i], candidate, sizeof(DeviceAddress)) == 0) {
            return true;
        }
    }
    return false;
}

static bool backendStartTemperatureConversionAll() {
    if (!ensureTemperatureBusConfigured()) {
        return false;
    }
    if (isUsingDs2482Backend()) {
        return ds2482.startConversionAll();
    }

    ds18b20.requestTemperatures();
    return true;
}

static bool backendStartTemperatureConversionByAddress(
    const DeviceAddress address) {
    if (!ensureTemperatureBusConfigured()) {
        return false;
    }
    if (isUsingDs2482Backend()) {
        return ds2482.startConversionByAddress(address);
    }

    ds18b20.requestTemperaturesByAddress(address);
    return true;
}

static bool backendReadTemperature(const DeviceAddress address, float& value) {
    if (!ensureTemperatureBusConfigured()) {
        return false;
    }

    if (isUsingDs2482Backend()) {
        return ds2482.readTemperatureC(address, value);
    }

    value = ds18b20.getTempC(address);
    return true;
}

static uint8_t scanDs18b20Bus(DeviceAddress addresses[]) {
    uint8_t count = 0;
    DeviceAddress addr = {0};

    memset(addresses, 0, sizeof(DeviceAddress) * TEMP_COUNT);
    if (!ensureTemperatureBusConfigured()) {
        return 0;
    }

    if (isUsingDs2482Backend()) {
        ds2482.resetSearch();
        while (count < TEMP_COUNT && ds2482.search(addr)) {
            if (!isSupportedDs18Family(addr[0])) {
                continue;
            }
            if (OneWire::crc8(addr, 7) != addr[7]) {
                LOG_W("Sensors: DS2482 search skipped address with bad CRC");
                continue;
            }
            if (hasDs18b20Address(addresses, count, addr)) {
                continue;
            }

            memcpy(addresses[count], addr, sizeof(DeviceAddress));
            count++;
        }
        return count;
    }

    oneWire.reset_search();
    while (count < TEMP_COUNT && oneWire.search(addr)) {
        if (!isSupportedDs18Family(addr[0])) {
            continue;
        }
        if (OneWire::crc8(addr, 7) != addr[7]) {
            LOG_W("Sensors: DS18B20 bus scan skipped address with bad CRC");
            continue;
        }
        if (hasDs18b20Address(addresses, count, addr)) {
            continue;
        }

        memcpy(addresses[count], addr, sizeof(DeviceAddress));
        count++;
    }
    oneWire.reset_search();
    return count;
}

static bool isValidDs18b20Reading(float value) {
    return value != DEVICE_DISCONNECTED_C && value > -50.0f && value < 150.0f;
}

static uint8_t appendKnownRespondingDs18b20(DeviceAddress addresses[],
                                           uint8_t count) {
    if (!hasAssignedTempMap()) {
        return count;
    }

    if (!backendStartTemperatureConversionAll()) {
        return count;
    }
    delay(CONVERSION_TIME_MS);

    for (uint8_t role = 0; role < TEMP_COUNT && count < TEMP_COUNT; ++role) {
        if (isZeroDeviceAddress(tempCal.addresses[role])) {
            continue;
        }
        if (hasDs18b20Address(addresses, count, tempCal.addresses[role])) {
            continue;
        }

        float value = DEVICE_DISCONNECTED_C;
        if (!backendReadTemperature(tempCal.addresses[role], value) ||
            !isValidDs18b20Reading(value)) {
            continue;
        }

        memcpy(addresses[count], tempCal.addresses[role], sizeof(DeviceAddress));
        count++;
    }

    return count;
}

static bool resetDs18b20BusWithRecovery() {
    if (!ensureTemperatureBusConfigured()) {
        return false;
    }

    if (isUsingDs2482Backend()) {
        bool presence = false;
        return ds2482.oneWireReset(&presence);
    }

    pinMode(PIN_ONEWIRE, OUTPUT);
    digitalWrite(PIN_ONEWIRE, LOW);
    delay(4);
    pinMode(PIN_ONEWIRE, INPUT_PULLUP);
    delay(4);

    oneWire.depower();
    return oneWire.reset();
}

static void prepareDs18b20BusForScan() {
    ensureTemperatureBusConfigured();

    if (conversionInProgress) {
        const uint32_t now = millis();
        if (now - conversionStartTime < CONVERSION_TIME_MS) {
            delay(CONVERSION_TIME_MS - (now - conversionStartTime));
        }
        conversionInProgress = false;
        conversionStartTime = 0;
    }

    if (!isUsingDs2482Backend()) {
        oneWire.reset_search();
    } else {
        ds2482.resetSearch();
    }
    resetDs18b20BusWithRecovery();
    delay(10);
}

static uint8_t discoverDs18b20(bool logInventory) {
    uint8_t count = 0;
    DeviceAddress bestAddresses[TEMP_COUNT] = {};
    DeviceAddress passAddresses[TEMP_COUNT] = {};
    bool busAddressUsed[TEMP_COUNT] = {false};

    for (uint8_t pass = 0; pass < DISCOVERY_BUS_PASSES; ++pass) {
        memset(passAddresses, 0, sizeof(passAddresses));
        const uint8_t passCount = scanDs18b20Bus(passAddresses);
        if (passCount > count) {
            memcpy(bestAddresses, passAddresses, sizeof(bestAddresses));
            count = passCount;
        }
        if (count >= TEMP_COUNT) {
            break;
        }
        if (pass + 1 < DISCOVERY_BUS_PASSES) {
            delay(DISCOVERY_PASS_DELAY_MS);
        }
    }

    count = appendKnownRespondingDs18b20(bestAddresses, count);

    clearDs18b20Inventory();

    if (hasAssignedTempMap()) {
        for (uint8_t role = 0; role < TEMP_COUNT; ++role) {
            if (isZeroDeviceAddress(tempCal.addresses[role])) {
                continue;
            }
            const int8_t busIndex = findDs18b20AddressIndex(
                bestAddresses, count, tempCal.addresses[role], busAddressUsed);
            if (busIndex < 0) {
                continue;
            }

            memcpy(ds18b20Addresses[role], bestAddresses[busIndex],
                   sizeof(DeviceAddress));
            ds18b20Found[role] = true;
            busAddressUsed[busIndex] = true;
            if (!isUsingDs2482Backend()) {
                ds18b20.setResolution(ds18b20Addresses[role], 12);
            }
        }
    } else {
        for (uint8_t i = 0; i < count; ++i) {
            memcpy(ds18b20Addresses[i], bestAddresses[i], sizeof(DeviceAddress));
            ds18b20Found[i] = true;
            if (!isUsingDs2482Backend()) {
                ds18b20.setResolution(ds18b20Addresses[i], 12);
            }
        }
    }

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

    if (backendStartTemperatureConversionAll()) {
        conversionInProgress = true;
        conversionStartTime = now;
    } else {
        conversionInProgress = false;
        conversionStartTime = 0;
    }
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

    if (ds18b20Mutex == nullptr) {
        ds18b20Mutex = xSemaphoreCreateMutex();
    }

    // Инициализация I2C
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    ensureTemperatureBusConfigured(true);

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
    probePzem(true);
#else
    pzem_ok = false;
    LOG_W("Sensors: PZEM disabled by build flag (PZEM_ENABLED=0)");
#endif

    // Датчик потока воды
    pinMode(PIN_FLOW_SENSOR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_SENSOR), flowPulseISR, RISING);

    // Обнулить калибровку

    LOG_I("Sensors: Init complete");
}

void readTemperatures(Temperatures& temps) {
    uint32_t now = millis();
    if (!lockDs18b20Bus(pdMS_TO_TICKS(20))) {
        return;
    }
    ensureTemperatureBusConfigured();
    ensureDs18b20Available(now);

    if (ds18b20Count == 0) {
        invalidateTemperatureState(temps);
        unlockDs18b20Bus();
        return;
    }

    // Фаза 1: Запуск конвертации (неблокирующий)
    if (!conversionInProgress) {
        startTemperatureConversion(now);
        unlockDs18b20Bus();
        return; // Выходим, не блокируя выполнение
    }

    // Фаза 2: Чтение результатов (только если прошло достаточно времени)
    if (now - conversionStartTime < CONVERSION_TIME_MS) {
        unlockDs18b20Bus();
        return; // Конвертация ещё идёт, ждём
    }

    // Прочитать значения (конвертация завершена)
    float values[TEMP_COUNT];
    uint8_t validCount = 0;
    for (uint8_t i = 0; i < TEMP_COUNT; i++) {
        if (ds18b20Found[i]) {
            float raw = DEVICE_DISCONNECTED_C;
            const bool readOk = backendReadTemperature(ds18b20Addresses[i], raw);

            // Проверка валидности (-127 = ошибка)
            if (!readOk || !isValidDs18b20Reading(raw)) {
                temps.valid[i] = false;
                values[i] = 0;
                // Инкремент счетчика ошибок конкретного датчика
                if (i < 7) g_state.health.tempErrors[i]++;
            } else {
                temps.valid[i] = true;
                values[i] = raw + tempCal.offsets[i];
                validCount++;
            }
        } else {
            temps.valid[i] = false;
            values[i] = 0;
            // Датчик не найден - тоже считаем ошибкой если он должен быть включен
            if (i < 7) g_state.health.tempErrors[i]++;
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
    unlockDs18b20Bus();
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

    // Давление в кубе (датчик на ADS1115 A1)
    if (ads_ok) {
        int16_t adc = ads1115.readADC_SingleEnded(ADS_CHANNEL_PRESSURE);
        float voltage = ads1115.computeVolts(adc);
        pressure.sensorAdc = adc;
        pressure.sensorVoltage = voltage;

        // MPX5010DP: 0.2V @ 0kPa, 4.7V @ 10kPa
        // P = (V - offset) / sensitivity
        float cubeMmHg = interpolatePressureMmHg(voltage, g_settings.pressureCal);
        if (!isfinite(cubeMmHg)) {
            float kPa = (voltage - MPX5010_OFFSET) / MPX5010_SENSITIVITY;
            cubeMmHg = kPa * 7.50062f;
        }

        // Преобразовать в мм рт.ст. (1 кПа = 7.50062 мм рт.ст.)
        pressure.cube = cubeMmHg - g_settings.pressureCal.zeroOffsetMmHg;

        // Ограничить диапазон
        if (pressure.cube < 0) pressure.cube = 0;
        if (pressure.cube > 75) pressure.cube = 75; // 10 кПа = ~75 мм рт.ст.
        pressure.ok = true;
    } else {
        pressure.cube = 0;
        pressure.sensorVoltage = 0.0f;
        pressure.sensorAdc = 0;
        pressure.ok = false;
    }

    pressure.lastUpdate = millis();
}

void readHydrometer(Hydrometer& hydro, float temperature, const HydrometerCalibration& cal) {
    // Читаем дифференциальное давление ареометра через отдельный канал ADS1115 A0
    // Давление куба и ареометр разведены: A1 = pressure, A0 = hydrometer

    if (!ads_ok) {
        hydro.ok = false;
        hydro.valid = false;
        return;
    }

    int16_t adc = ads1115.readADC_SingleEnded(ADS_CHANNEL_HYDROMETER);
    float voltage = ads1115.computeVolts(adc);
    float kPa = (voltage - MPX5010_OFFSET) / MPX5010_SENSITIVITY;
    if (kPa < 0.0f) {
        kPa = 0.0f;
    }

    // Плотность (упрощённо, без учёта высоты столба)
    // ρ = ΔP / (g × h), где g=9.81, h=высота_попугая (м)
    // Для примера: h = 0.1м
    const float height_m = 0.1f;
    const float rawDensity = (kPa * 1000.0f) / (9.81f * height_m);
    const float correctedDensity = rawDensity + cal.densityOffset;

    hydro.pressure = kPa;
    hydro.density = correctedDensity;

    if (cal.pointCount >= 2) {
        hydro.abv = interpolateABV(correctedDensity, cal);
    } else {
        hydro.abv = (1.0f - correctedDensity) * 100.0f;
    }

    if (hydro.abv < 0.0f) {
        hydro.abv = 0.0f;
    } else if (hydro.abv > 100.0f) {
        hydro.abv = 100.0f;
    }

    // Температурная коррекция (упрощённо)
    hydro.temperature = temperature;

    // Валидация
    hydro.ok = true;
    hydro.valid = (correctedDensity > 0.7f && correctedDensity < 1.1f);
    hydro.lastUpdate = millis();
}

void readPower(Power& power) {
    if (!pzem_ok) {
        const uint32_t now = millis();
        if (now - lastPzemProbeMs >= PZEM_RETRY_INTERVAL_MS) {
            probePzem(false);
        }
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

    const bool noResponse = isnan(rawVoltage) && isnan(rawCurrent) &&
                            isnan(rawPower) && isnan(rawEnergy) &&
                            isnan(rawFrequency) && isnan(rawPF);
    if (noResponse) {
        if (++pzemConsecutiveReadFailures >= PZEM_READ_FAIL_LIMIT) {
            pzem_ok = false;
            pzemConsecutiveReadFailures = 0;
            lastPzemProbeMs = 0;
            LOG_WARN("Sensors: PZEM-004T lost during runtime, switching to auto-retry");
        }
        power.voltage = 0;
        power.current = 0;
        power.power = 0;
        power.energy = 0;
        power.frequency = 0;
        power.powerFactor = 0;
        power.lastUpdate = millis();
        return;
    }
    pzemConsecutiveReadFailures = 0;

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
        // BUG-4 fix: атомарно копируем и сбрасываем счётчик за одну критическую секцию
        noInterrupts();
        uint32_t pulses = flowPulseCount;
        flowPulseCount = 0;
        interrupts();

        // YF-S201: ~7.5 импульсов на литр (зависит от модели)
        const float pulsesPerLiter = 7.5f;

        // Вычислить л/мин из скопированного значения
        float litersThisSec = (float)pulses / pulsesPerLiter;
        flow.litersPerMin = litersThisSec * 60.0f * (1000.0f / (float)elapsed);

        // Общий объём
        totalLiters += litersThisSec;
        flow.totalLiters = totalLiters;

        // Проверка потока
        flow.flowing = (pulses > 0);

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
    DeviceAddress rawAddresses[TEMP_COUNT] = {};

    if (!lockDs18b20Bus(portMAX_DELAY)) {
        return 0;
    }

    for (uint8_t i = 0; i < TEMP_COUNT; ++i) {
        memset(addresses[i], 0, sizeof(DeviceAddress));
    }

    prepareDs18b20BusForScan();

    for (uint8_t attempt = 0; attempt < DISCOVERY_INIT_ATTEMPTS; ++attempt) {
        memset(rawAddresses, 0, sizeof(rawAddresses));
        count = scanDs18b20Bus(rawAddresses);
        if (count > 0) {
            break;
        }
        delay(DISCOVERY_INIT_DELAY_MS);
    }

    count = appendKnownRespondingDs18b20(rawAddresses, count);

    for (uint8_t i = 0; i < count && i < TEMP_COUNT; ++i) {
        memcpy(addresses[i], rawAddresses[i], sizeof(DeviceAddress));
    }

    unlockDs18b20Bus();

    return count;
}

void refreshTemperatureInventory() {
    if (!lockDs18b20Bus(portMAX_DELAY)) {
        return;
    }
    ensureTemperatureBusConfigured(true);
    prepareDs18b20BusForScan();
    discoverDs18b20(true);
    if (ds18b20Count > 0) {
        startTemperatureConversion(millis());
    }
    unlockDs18b20Bus();
}

bool getDiscoveredTempAddress(uint8_t index, uint8_t address[8]) {
    if (!address || index >= TEMP_COUNT || !ds18b20Found[index]) {
        return false;
    }
    memcpy(address, ds18b20Addresses[index], sizeof(DeviceAddress));
    return true;
}

bool probeTempAddress(const uint8_t address[8], float* temperatureC) {
    if (!address || isZeroDeviceAddress(address)) {
        return false;
    }

    if (!lockDs18b20Bus(portMAX_DELAY)) {
        return false;
    }
    ensureTemperatureBusConfigured();

    const uint32_t now = millis();
    if (conversionInProgress) {
        if (now - conversionStartTime < CONVERSION_TIME_MS) {
            delay(CONVERSION_TIME_MS - (now - conversionStartTime));
        }
        conversionInProgress = false;
        conversionStartTime = 0;
    }

    float value = DEVICE_DISCONNECTED_C;
    const bool conversionOk = backendStartTemperatureConversionByAddress(address);
    if (conversionOk) {
        delay(CONVERSION_TIME_MS);
        backendReadTemperature(address, value);
    }
    if (temperatureC) {
        *temperatureC = value;
    }

    if (ds18b20Count > 0) {
        startTemperatureConversion(millis());
    }

    unlockDs18b20Bus();
    return conversionOk && isValidDs18b20Reading(value);
}

uint8_t sampleDs18b20Presence(uint8_t attempts) {
    if (attempts == 0) {
        return 0;
    }

    if (!lockDs18b20Bus(portMAX_DELAY)) {
        return 0;
    }
    ensureTemperatureBusConfigured();

    const uint32_t now = millis();
    if (conversionInProgress) {
        if (now - conversionStartTime < CONVERSION_TIME_MS) {
            delay(CONVERSION_TIME_MS - (now - conversionStartTime));
        }
        conversionInProgress = false;
        conversionStartTime = 0;
    }

    uint8_t detected = 0;
    for (uint8_t i = 0; i < attempts; ++i) {
        if (resetDs18b20BusWithRecovery()) {
            detected++;
        }
        if (i + 1 < attempts) {
            delay(8);
        }
    }

    if (ds18b20Count > 0) {
        startTemperatureConversion(millis());
    }

    unlockDs18b20Bus();
    return detected;
}

bool isTempSensorValid(uint8_t index) {
    if (index >= TEMP_COUNT) return false;

    if (!ds18b20Found[index]) return false;

    if (!lockDs18b20Bus(pdMS_TO_TICKS(20))) {
        return false;
    }

    // Попробовать прочитать
    ensureTemperatureBusConfigured();
    float temp = DEVICE_DISCONNECTED_C;
    const bool readOk = backendReadTemperature(ds18b20Addresses[index], temp);
    unlockDs18b20Bus();
    return readOk && isValidDs18b20Reading(temp);
}

bool isBmp280PrimaryAvailable() {
    return bmp1_ok;
}

bool isBmp280SecondaryAvailable() {
    return bmp2_ok;
}

bool isAds1115Available() {
    return ads_ok;
}

bool isDs2482Available() {
    ensureTemperatureBusConfigured();
    return ds2482_ok;
}

bool isPzemAvailable() {
    return pzem_ok;
}

bool isUsingDs2482ForTemps() {
    ensureTemperatureBusConfigured();
    return tempBusUseDs2482;
}

uint8_t getDs2482Address() {
    ensureTemperatureBusConfigured();
    return tempBusDs2482Address;
}

const char* getTemperatureBusSourceKey() {
    ensureTemperatureBusConfigured();
    return getTemperatureBusSourceKeyInternal();
}

const char* getTemperatureBusSourceLabel() {
    ensureTemperatureBusConfigured();
    return getTemperatureBusSourceLabelInternal();
}

bool readAds1115Channel(uint8_t channel, int16_t& adc, float& voltage) {
    adc = 0;
    voltage = 0.0f;

    if (!ads_ok || channel > 3) {
        return false;
    }

    adc = ads1115.readADC_SingleEnded(channel);
    voltage = ads1115.computeVolts(adc);
    return true;
}

void updateHealth(SystemHealth& health) {
    // Подсчёт работающих датчиков температуры
    health.tempSensorsTotal = 0;
    health.tempSensorsOk = 0;
    
    bool tempOk[7] = {false};
    for (uint8_t i = 0; i < 7; i++) {
        if (ds18b20Found[i]) {
            health.tempSensorsTotal++;
            if (isTempSensorValid(i)) {
                health.tempSensorsOk++;
                tempOk[i] = true;
            }
        }
    }

    // Состояние других датчиков
    health.bmp280Ok = bmp1_ok || bmp2_ok;
    health.ads1115Ok = ads_ok;
    health.pzemOk = pzem_ok;

    // Счётчики ошибок (интегральные)
    health.pzemSpikeCount = pzemSpikeCounter;
    health.tempReadErrors = tempReadErrorCounter;

    // WiFi
    health.wifiConnected = (WiFi.status() == WL_CONNECTED);
    health.wifiRSSI = WiFi.RSSI();

    // Системная информация
    health.uptime = millis() / 1000;
    health.freeHeap = ESP.getFreeHeap();
    health.cpuTemp = (uint8_t)temperatureRead();

    // РАСЧЁТ ВЗВЕШЕННОГО ЗДОРОВЬЯ (0-100%)
    // (0=SENSORS, 1=WIFI, 2=POWER, 3=STORAGE, 4=OTA, 5=SAFETY)

    // 0. SENSORS (Датчики температуры + Давление + Поток)
    float sensorsScore = 100.0f;
    // Критические датчики - Куб и Царга Низ
    if (!tempOk[TEMP_CUBE]) sensorsScore -= 40.0f;
    if (!tempOk[TEMP_COLUMN_BOTTOM]) sensorsScore -= 40.0f;
    // Датчики безопасности - ТСА и Давление (через ADS1115)
    if (!tempOk[TEMP_TSA]) sensorsScore -= 10.0f;
    if (!health.ads1115Ok) sensorsScore -= 10.0f;
    // Информационные датчики
    if (!tempOk[TEMP_COLUMN_TOP]) sensorsScore -= 5.0f;
    if (!tempOk[TEMP_REFLUX]) sensorsScore -= 2.0f;
    if (!tempOk[TEMP_WATER_IN]) sensorsScore -= 2.0f;
    if (!tempOk[TEMP_WATER_OUT]) sensorsScore -= 2.0f;
    health.healthScores[0] = fmaxf(0.0f, fminf(100.0f, sensorsScore));

    // 1. WIFI (Связь и сетевые интерфейсы)
    float wifiScore = 100.0f;
    if (!health.wifiConnected) wifiScore -= 50.0f; // Нет связи
    else {
        // Штраф за плохой сигнал
        if (health.wifiRSSI < -90) wifiScore -= 30.0f;
        else if (health.wifiRSSI < -80) wifiScore -= 15.0f;
        else if (health.wifiRSSI < -70) wifiScore -= 5.0f;
    }
    health.healthScores[1] = fmaxf(0.0f, fminf(100.0f, wifiScore));

    // 2. POWER (Питание и контроль мощности)
    float powerScore = 100.0f;
    if (!health.pzemOk) powerScore -= 50.0f; // Нет контроля мощности
    if (health.pzemSpikeCount > 10) powerScore -= 20.0f; // Нестабильность
    health.healthScores[2] = fmaxf(0.0f, fminf(100.0f, powerScore));

    // 3. STORAGE (Память и LittleFS)
    float storageScore = 100.0f;
    if (health.freeHeap < 32768) storageScore -= 40.0f; // Критически мало памяти
    else if (health.freeHeap < 65536) storageScore -= 20.0f; // Мало памяти
    // Проверка LittleFS (заглушка, так как LittleFS.usedBytes() медленный)
    health.healthScores[3] = fmaxf(0.0f, fminf(100.0f, storageScore));

    // 4. OTA (Стабильность прошивки и версия)
    float otaScore = 100.0f;
    if (health.tempReadErrors > 100) otaScore -= 20.0f; // Проблемы с шиной могут мешать OTA
    health.healthScores[4] = fmaxf(0.0f, fminf(100.0f, otaScore));

    // 5. SAFETY (Аварии и перегрев)
    float safetyScore = 100.0f;
    if (health.cpuTemp > 90) safetyScore -= 50.0f; // Критический перегрев ESP
    else if (health.cpuTemp > 85) safetyScore -= 20.0f;
    // Штрафы за нестабильность датчиков
    int errorPenalty = 0;
    for (int i = 0; i < 7; i++) {
        if (health.tempErrors[i] > 100) errorPenalty += 10;
        else if (health.tempErrors[i] > 50) errorPenalty += 5;
    }
    safetyScore -= errorPenalty;
    health.healthScores[5] = fmaxf(0.0f, fminf(100.0f, safetyScore));

    // Вычисление итогового взвешенного здоровья
    float overall = 0.0f;
    for (int i = 0; i < 6; i++) {
        overall += health.healthScores[i] * SystemHealth::healthWeights[i];
    }
    health.overallHealth = (uint8_t)fmaxf(0.0f, fminf(100.0f, overall));

    health.lastUpdate = millis();
}

} // namespace Sensors
