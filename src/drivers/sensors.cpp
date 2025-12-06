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

// Калибровка
static TempCalibration tempCal;

// Датчик потока воды (счётчик импульсов)
static volatile uint32_t flowPulseCount = 0;
static uint32_t lastFlowCheck = 0;
static float totalLiters = 0;

// Адреса DS18B20
static DeviceAddress ds18b20Addresses[TEMP_COUNT];
static bool ds18b20Found[TEMP_COUNT] = {false};

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

// =============================================================================
// ПУБЛИЧНЫЙ ИНТЕРФЕЙС
// =============================================================================

namespace Sensors {

void init() {
    LOG_I("Sensors: Initializing...");

    // Инициализация I2C
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    // DS18B20
    ds18b20.begin();
    uint8_t deviceCount = ds18b20.getDeviceCount();
    LOG_I("Sensors: Found %d DS18B20 devices", deviceCount);

    // Сканируем адреса
    for (uint8_t i = 0; i < TEMP_COUNT && i < deviceCount; i++) {
        if (ds18b20.getAddress(ds18b20Addresses[i], i)) {
            ds18b20Found[i] = true;
            ds18b20.setResolution(ds18b20Addresses[i], 12); // 12-бит

            LOG_D("Sensors: DS18B20[%d] = %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                  i,
                  ds18b20Addresses[i][0], ds18b20Addresses[i][1],
                  ds18b20Addresses[i][2], ds18b20Addresses[i][3],
                  ds18b20Addresses[i][4], ds18b20Addresses[i][5],
                  ds18b20Addresses[i][6], ds18b20Addresses[i][7]);
        }
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

    // PZEM-004T v3.0
    pzemSerial.begin(PZEM_BAUD_RATE, SERIAL_8N1, PIN_PZEM_RX, PIN_PZEM_TX);
    delay(100); // Даём время на инициализацию

    // Проверка связи с PZEM
    float testVoltage = pzem.voltage();
    if (!isnan(testVoltage) && testVoltage > 0) {
        pzem_ok = true;
        LOG_I("Sensors: PZEM-004T OK (V=%.1fV)", testVoltage);
    } else {
        LOG_E("Sensors: PZEM-004T NOT FOUND or NO AC POWER");
    }

    // Датчик потока воды
    pinMode(PIN_FLOW_SENSOR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_SENSOR), flowPulseISR, RISING);

    // Обнулить калибровку
    memset(&tempCal, 0, sizeof(tempCal));

    LOG_I("Sensors: Init complete");
}

void readTemperatures(Temperatures& temps) {
    // Запросить конвертацию
    ds18b20.requestTemperatures();

    // Прочитать значения
    float values[TEMP_COUNT];
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
    temps.lastUpdate = millis();
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
        float voltage = ads1115.computeVoltage(adc);

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
    float voltage = ads1115.computeVoltage(adc);
    float kPa = (voltage - MPX5010_OFFSET) / MPX5010_SENSITIVITY;

    // Плотность (упрощённо, без учёта высоты столба)
    // ρ = ΔP / (g × h), где g=9.81, h=высота_попугая (м)
    // Для примера: h = 0.1м
    const float height_m = 0.1f;
    hydro.density = (kPa * 1000) / (9.81f * height_m); // г/см³

    // ABV (здесь нужна калибровочная таблица)
    // Пока используем упрощённую формулу
    hydro.abv = (1.0f - hydro.density) * 100.0f;

    // Температурная коррекция (упрощённо)
    hydro.temperature = temperature;

    // Валидация
    hydro.valid = (hydro.density > 0.7f && hydro.density < 1.1f);
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
    power.voltage = pzem.voltage();
    power.current = pzem.current();
    power.power = pzem.power();
    power.energy = pzem.energy();
    power.frequency = pzem.frequency();
    power.powerFactor = pzem.pf();

    // Проверка на NaN и корректность
    if (isnan(power.voltage) || power.voltage < 0 || power.voltage > 300) {
        power.voltage = 0;
    }
    if (isnan(power.current) || power.current < 0 || power.current > PZEM_CURRENT_MAX) {
        power.current = 0;
    }
    if (isnan(power.power) || power.power < 0 || power.power > 10000) {
        power.power = 0;
    }
    if (isnan(power.energy) || power.energy < 0) {
        power.energy = 0;
    }
    if (isnan(power.frequency) || power.frequency < 45 || power.frequency > 65) {
        power.frequency = 50; // По умолчанию 50 Гц
    }
    if (isnan(power.powerFactor) || power.powerFactor < 0 || power.powerFactor > 1) {
        power.powerFactor = 0;
    }

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

} // namespace Sensors
