#include "temp_sensors.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"
#include "utils.h"

// Создаем экземпляр класса для работы с OneWire
OneWire oneWire(PIN_TEMP_SENSORS);

// Создаем экземпляр класса для работы с датчиками DS18B20
DallasTemperature tempSensors(&oneWire);

// Буфер для хранения адресов датчиков
DeviceAddress tempSensorAddress;

// Массив для хранения измеренных температур
float temperatures[MAX_TEMP_SENSORS];

// Массив для хранения времени последнего обновления температур
unsigned long lastTempUpdate[MAX_TEMP_SENSORS];

// Время последнего поиска датчиков
unsigned long lastSensorScanTime = 0;

// Количество найденных датчиков
int connectedSensorsCount = 0;

// Имена датчиков температуры
const char* tempSensorNames[MAX_TEMP_SENSORS] = {
    "Куб",
    "Царга",
    "Узел отбора",
    "ТСА",
    "Выход воды"
};

// Инициализация датчиков температуры
void initTempSensors() {
    Serial.println("Инициализация датчиков температуры...");
    
    // Инициализация библиотеки
    tempSensors.begin();
    
    // Установка разрешения (9-12 бит)
    tempSensors.setResolution(12);
    
    // Устанавливаем режим ожидания true для более быстрого получения данных
    tempSensors.setWaitForConversion(true);
    
    // Сбрасываем буфер температур
    for (int i = 0; i < MAX_TEMP_SENSORS; i++) {
        temperatures[i] = -127.0; // Значение, означающее отсутствие данных
        lastTempUpdate[i] = 0;
    }
    
    // Определяем количество подключенных датчиков
    connectedSensorsCount = tempSensors.getDeviceCount();
    Serial.print("Найдено датчиков температуры: ");
    Serial.println(connectedSensorsCount);
    
    // Проверяем, есть ли сохраненные адреса датчиков
    bool allAddressesSet = true;
    
    for (int i = 0; i < MAX_TEMP_SENSORS; i++) {
        if (!sysSettings.tempSensorEnabled[i]) {
            allAddressesSet = false;
            break;
        }
        
        // Если есть сохраненные адреса, выводим их
        if (sysSettings.tempSensorEnabled[i]) {
            Serial.print("Датчик #");
            Serial.print(i);
            Serial.print(" (");
            Serial.print(tempSensorNames[i]);
            Serial.print("): ");
            
            for (uint8_t j = 0; j < 8; j++) {
                if (sysSettings.tempSensorAddresses[i][j] < 16) Serial.print("0");
                Serial.print(sysSettings.tempSensorAddresses[i][j], HEX);
                if (j < 7) Serial.print(":");
            }
            
            Serial.print(", калибровка: ");
            Serial.println(sysSettings.tempSensorCalibration[i]);
        }
    }
    
    // Если не все адреса установлены, выполняем поиск датчиков
    if (!allAddressesSet && connectedSensorsCount > 0) {
        Serial.println("Не все адреса датчиков настроены, требуется сканирование и назначение");
        scanForTempSensors();
    }
    
    // Первое измерение температур
    updateTemperatures();
    
    Serial.println("Датчики температуры инициализированы");
}

// Обновление показаний датчиков
void updateTemperatures() {
    // Запрашиваем преобразование температуры на всех датчиках
    tempSensors.requestTemperatures();
    
    // Получаем температуры для каждого датчика по сохраненному адресу
    for (int i = 0; i < MAX_TEMP_SENSORS; i++) {
        if (sysSettings.tempSensorEnabled[i]) {
            // Получаем температуру по адресу
            float temp = tempSensors.getTempC(sysSettings.tempSensorAddresses[i]);
            
            // Применяем калибровку
            temp += sysSettings.tempSensorCalibration[i];
            
            // Проверяем, что температура в разумных пределах
            if (temp > -55.0 && temp < 125.0) {
                temperatures[i] = temp;
                lastTempUpdate[i] = millis();
            } else {
                // Если получено недействительное значение, проверяем, как давно обновлялась температура
                if (millis() - lastTempUpdate[i] > 10000) {
                    // Если больше 10 секунд нет данных, считаем датчик отключенным
                    temperatures[i] = -127.0;
                }
            }
        }
    }
}

// Поиск и установка адресов датчиков
bool scanForTempSensors() {
    Serial.println("Сканирование датчиков температуры...");
    
    // Помечаем время последнего сканирования
    lastSensorScanTime = millis();
    
    // Определяем количество подключенных датчиков
    connectedSensorsCount = tempSensors.getDeviceCount();
    
    if (connectedSensorsCount == 0) {
        Serial.println("Датчики не найдены!");
        return false;
    }
    
    Serial.print("Найдено датчиков: ");
    Serial.println(connectedSensorsCount);
    
    // Счетчик найденных датчиков
    int foundCount = 0;
    
    // Перебираем все датчики и получаем их адреса
    for (int i = 0; i < connectedSensorsCount && foundCount < MAX_TEMP_SENSORS; i++) {
        // Получаем адрес текущего датчика
        if (tempSensors.getAddress(tempSensorAddress, i)) {
            Serial.print("Датчик #");
            Serial.print(i);
            Serial.print(" имеет адрес: ");
            
            for (uint8_t j = 0; j < 8; j++) {
                if (tempSensorAddress[j] < 16) Serial.print("0");
                Serial.print(tempSensorAddress[j], HEX);
                if (j < 7) Serial.print(":");
            }
            
            Serial.println();
            
            // Если у нас меньше MAX_TEMP_SENSORS датчиков, назначаем автоматически
            if (foundCount < MAX_TEMP_SENSORS) {
                // Копируем адрес в настройки
                memcpy(sysSettings.tempSensorAddresses[foundCount], tempSensorAddress, 8);
                
                // Включаем датчик
                sysSettings.tempSensorEnabled[foundCount] = true;
                
                // Сбрасываем калибровку
                sysSettings.tempSensorCalibration[foundCount] = 0.0;
                
                foundCount++;
            }
        }
    }
    
    // Сохраняем настройки
    saveSystemSettings();
    
    Serial.print("Автоматически назначено датчиков: ");
    Serial.println(foundCount);
    
    return (foundCount > 0);
}

// Получение температуры конкретного датчика
float getTemperature(int sensorIndex) {
    if (sensorIndex >= 0 && sensorIndex < MAX_TEMP_SENSORS) {
        return temperatures[sensorIndex];
    }
    return -127.0; // Возвращаем недействительное значение
}

// Проверка подключения датчика
bool isSensorConnected(int sensorIndex) {
    if (sensorIndex >= 0 && sensorIndex < MAX_TEMP_SENSORS) {
        return (temperatures[sensorIndex] > -100.0);
    }
    return false;
}

// Калибровка датчика температуры
void calibrateTempSensor(int sensorIndex, float offset) {
    if (sensorIndex >= 0 && sensorIndex < MAX_TEMP_SENSORS && sysSettings.tempSensorEnabled[sensorIndex]) {
        sysSettings.tempSensorCalibration[sensorIndex] = offset;
        
        // Сохраняем настройки
        saveSystemSettings();
        
        Serial.print("Установлена калибровка для датчика #");
        Serial.print(sensorIndex);
        Serial.print(" (");
        Serial.print(tempSensorNames[sensorIndex]);
        Serial.print("): ");
        Serial.println(offset);
    }
}

// Получение количества подключенных датчиков
int getConnectedSensorsCount() {
    return connectedSensorsCount;
}

// Проверка температуры на достижение заданного значения с учетом гистерезиса
bool isTemperatureReached(int sensorIndex, float targetTemp, float hysteresis) {
    if (sensorIndex >= 0 && sensorIndex < MAX_TEMP_SENSORS && sysSettings.tempSensorEnabled[sensorIndex]) {
        return (temperatures[sensorIndex] >= (targetTemp - hysteresis));
    }
    return false;
}

// Проверка на превышение максимальной температуры
bool isTemperatureExceeded(int sensorIndex, float maxTemp) {
    if (sensorIndex >= 0 && sensorIndex < MAX_TEMP_SENSORS && sysSettings.tempSensorEnabled[sensorIndex]) {
        return (temperatures[sensorIndex] > maxTemp);
    }
    return false;
}

// Получение скорости изменения температуры (градусов в минуту)
float getTemperatureRateOfChange(int sensorIndex, float periodSeconds) {
    static float lastTemperatures[MAX_TEMP_SENSORS] = {0};
    static unsigned long lastUpdateTime[MAX_TEMP_SENSORS] = {0};
    
    if (sensorIndex >= 0 && sensorIndex < MAX_TEMP_SENSORS && sysSettings.tempSensorEnabled[sensorIndex]) {
        unsigned long currentTime = millis();
        float currentTemp = temperatures[sensorIndex];
        
        // Если это первое измерение или прошло слишком мало времени
        if (lastUpdateTime[sensorIndex] == 0 || (currentTime - lastUpdateTime[sensorIndex]) < (periodSeconds * 1000)) {
            lastTemperatures[sensorIndex] = currentTemp;
            lastUpdateTime[sensorIndex] = currentTime;
            return 0.0;
        }
        
        // Рассчитываем изменение температуры
        float tempDelta = currentTemp - lastTemperatures[sensorIndex];
        float timeDeltaMinutes = (float)(currentTime - lastUpdateTime[sensorIndex]) / 60000.0;
        
        // Обновляем последние значения
        lastTemperatures[sensorIndex] = currentTemp;
        lastUpdateTime[sensorIndex] = currentTime;
        
        // Возвращаем скорость изменения в градусах в минуту
        return tempDelta / timeDeltaMinutes;
    }
    
    return 0.0;
}

// Получение имени датчика
String getTempSensorName(int sensorIndex) {
    if (sensorIndex >= 0 && sensorIndex < MAX_TEMP_SENSORS) {
        return String(tempSensorNames[sensorIndex]);
    }
    return "Неизвестный";
}
/**
 * @brief Проверить наличие всех необходимых датчиков
 * 
 * @return true если все критичные датчики подключены
 */
bool checkRequiredSensors() {
    Serial.println("Проверка необходимых датчиков...");
    
    bool allConnected = true;
    
    // Проверяем критичные датчики
    if (!isSensorConnected(TEMP_CUBE)) {
        Serial.println("ОШИБКА: Датчик температуры куба не подключен!");
        allConnected = false;
    }
    
    if (!isSensorConnected(TEMP_REFLUX)) {
        Serial.println("ОШИБКА: Датчик температуры дефлегматора не подключен!");
        allConnected = false;
    }
    
    if (allConnected) {
        Serial.println("Все необходимые датчики подключены");
    }
    
    return allConnected;
}
