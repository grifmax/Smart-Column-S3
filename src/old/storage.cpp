#include "storage.h"
#include <Preferences.h>
#include "utils.h"

// Создаем экземпляр класса Preferences
Preferences preferences;

// Имена пространств имен для хранения разных настроек
const char* SYS_NAMESPACE = "sysSettings";
const char* RECT_NAMESPACE = "rectParams";
const char* DIST_NAMESPACE = "distParams";
const char* PUMP_NAMESPACE = "pumpSettings";

// Глобальные переменные для хранения настроек и параметров
SystemSettings sysSettings;
RectificationParams rectParams;
DistillationParams distParams;
PumpSettings pumpSettings;

// Инициализация системы хранения
void initStorage() {
    Serial.println("Инициализация системы хранения настроек...");
    
    // Открываем пространство имен только для чтения, чтобы проверить, инициализированы ли настройки
    preferences.begin(SYS_NAMESPACE, true);
    bool initialized = preferences.getBool("initialized", false);
    preferences.end();
    
    // Если настройки не инициализированы, устанавливаем значения по умолчанию
    if (!initialized) {
        Serial.println("Настройки не инициализированы, устанавливаем значения по умолчанию");
        resetAllSettings();
    }
    
    Serial.println("Система хранения настроек инициализирована");
}

// Сохранение системных настроек
bool saveSystemSettings() {
    preferences.begin(SYS_NAMESPACE, false);
    
    preferences.putBool("initialized", true);
    preferences.putInt("maxHeaterPower", sysSettings.maxHeaterPowerWatts);
    preferences.putInt("powerCtrlMode", sysSettings.powerControlMode);
    
    // Сохраняем настройки PI-регулятора
    preferences.putFloat("piKp", sysSettings.piSettings.kp);
    preferences.putFloat("piKi", sysSettings.piSettings.ki);
    preferences.putFloat("piOutMin", sysSettings.piSettings.outputMin);
    preferences.putFloat("piOutMax", sysSettings.piSettings.outputMax);
    preferences.putFloat("piIntLimit", sysSettings.piSettings.integralLimit);
    
    preferences.putBool("pzemEnabled", sysSettings.pzemEnabled);
    preferences.putBool("soundEnabled", sysSettings.soundEnabled);
    preferences.putInt("soundVolume", sysSettings.soundVolume);
    
    // Сохраняем настройки дисплея
    preferences.putBool("displayEnabled", sysSettings.displaySettings.enabled);
    preferences.putInt("displayBright", sysSettings.displaySettings.brightness);
    preferences.putInt("displayRotation", sysSettings.displaySettings.rotation);
    preferences.putBool("displayInvert", sysSettings.displaySettings.invertColors);
    preferences.putInt("displayContrast", sysSettings.displaySettings.contrast);
    preferences.putInt("displayTimeout", sysSettings.displaySettings.timeout);
    preferences.putBool("displayShowLogo", sysSettings.displaySettings.showLogo);
    
    preferences.putInt("tempUpdateInt", sysSettings.tempUpdateInterval);
    preferences.putInt("tempReportInt", sysSettings.tempReportInterval);
    
    // Сохраняем настройки датчиков
    for (int i = 0; i < MAX_TEMP_SENSORS; i++) {
        String key = "tempSensEn" + String(i);
        preferences.putBool(key.c_str(), sysSettings.tempSensorEnabled[i]);
        
        key = "tempSensCal" + String(i);
        preferences.putFloat(key.c_str(), sysSettings.tempSensorCalibration[i]);
        
        // Сохраняем адрес датчика
        if (sysSettings.tempSensorEnabled[i]) {
            key = "tempSensAddr" + String(i);
            preferences.putBytes(key.c_str(), sysSettings.tempSensorAddresses[i], 8);
        }
    }
    
    preferences.end();
    
    Serial.println("Системные настройки сохранены");
    return true;
}

// Загрузка системных настроек
bool loadSystemSettings() {
    preferences.begin(SYS_NAMESPACE, true);
    
    bool initialized = preferences.getBool("initialized", false);
    
    if (initialized) {
        sysSettings.maxHeaterPowerWatts = preferences.getInt("maxHeaterPower", 3000);
        sysSettings.powerControlMode = (PowerControlMode)preferences.getInt("powerCtrlMode", POWER_CONTROL_MANUAL);
        
        // Загружаем настройки PI-регулятора
        sysSettings.piSettings.kp = preferences.getFloat("piKp", 0.5);
        sysSettings.piSettings.ki = preferences.getFloat("piKi", 0.1);
        sysSettings.piSettings.outputMin = preferences.getFloat("piOutMin", 0.0);
        sysSettings.piSettings.outputMax = preferences.getFloat("piOutMax", 100.0);
        sysSettings.piSettings.integralLimit = preferences.getFloat("piIntLimit", 100.0);
        
        sysSettings.pzemEnabled = preferences.getBool("pzemEnabled", false);
        sysSettings.soundEnabled = preferences.getBool("soundEnabled", true);
        sysSettings.soundVolume = preferences.getInt("soundVolume", 75);
        
        // Загружаем настройки дисплея
        sysSettings.displaySettings.enabled = preferences.getBool("displayEnabled", true);
        sysSettings.displaySettings.brightness = preferences.getInt("displayBright", 255);
        sysSettings.displaySettings.rotation = preferences.getInt("displayRotation", 0);
        sysSettings.displaySettings.invertColors = preferences.getBool("displayInvert", false);
        sysSettings.displaySettings.contrast = preferences.getInt("displayContrast", 128);
        sysSettings.displaySettings.timeout = preferences.getInt("displayTimeout", 0);
        sysSettings.displaySettings.showLogo = preferences.getBool("displayShowLogo", true);
        
        sysSettings.tempUpdateInterval = preferences.getInt("tempUpdateInt", 1000);
        sysSettings.tempReportInterval = preferences.getInt("tempReportInt", 2000);
        
        // Загружаем настройки датчиков
        for (int i = 0; i < MAX_TEMP_SENSORS; i++) {
            String key = "tempSensEn" + String(i);
            sysSettings.tempSensorEnabled[i] = preferences.getBool(key.c_str(), false);
            
            key = "tempSensCal" + String(i);
            sysSettings.tempSensorCalibration[i] = preferences.getFloat(key.c_str(), 0.0);
            
            // Загружаем адрес датчика
            if (sysSettings.tempSensorEnabled[i]) {
                key = "tempSensAddr" + String(i);
                preferences.getBytes(key.c_str(), sysSettings.tempSensorAddresses[i], 8);
            }
        }
    } else {
        // Если настройки не инициализированы, устанавливаем значения по умолчанию
        setDefaultSystemSettings();
    }
    
    preferences.end();
    
    Serial.println("Системные настройки загружены");
    return initialized;
}

// Сохранение параметров ректификации
bool saveRectificationParams() {
    preferences.begin(RECT_NAMESPACE, false);
    
    preferences.putInt("model", rectParams.model);
    
    preferences.putFloat("maxCubeTemp", rectParams.maxCubeTemp);
    preferences.putFloat("headsTemp", rectParams.headsTemp);
    preferences.putFloat("bodyTemp", rectParams.bodyTemp);
    preferences.putFloat("tailsTemp", rectParams.tailsTemp);
    preferences.putFloat("endTemp", rectParams.endTemp);
    
    preferences.putInt("heatingPowerWatts", rectParams.heatingPowerWatts);
    preferences.putInt("stabilizationPowerWatts", rectParams.stabilizationPowerWatts);
    preferences.putInt("bodyPowerWatts", rectParams.bodyPowerWatts);
    preferences.putInt("tailsPowerWatts", rectParams.tailsPowerWatts);
    
    // Для обратной совместимости сохраняем и проценты
    preferences.putInt("heatingPower", rectParams.heatingPower);
    preferences.putInt("stabilizationPower", rectParams.stabilizationPower);
    preferences.putInt("bodyPower", rectParams.bodyPower);
    preferences.putInt("tailsPower", rectParams.tailsPower);
    
    preferences.putInt("stabilizationTime", rectParams.stabilizationTime);
    preferences.putFloat("headsVolume", rectParams.headsVolume);
    preferences.putFloat("bodyVolume", rectParams.bodyVolume);
    
    // Параметры альтернативной модели
    preferences.putInt("headsTargetTime", rectParams.headsTargetTimeMinutes);
    preferences.putInt("postHeadsStabTime", rectParams.postHeadsStabilizationTime);
    preferences.putFloat("bodyFlowRate", rectParams.bodyFlowRateMlPerHour);
    preferences.putFloat("tempDeltaEndBody", rectParams.tempDeltaEndBody);
    preferences.putFloat("tailsCubeTemp", rectParams.tailsCubeTemp);
    preferences.putFloat("tailsFlowRate", rectParams.tailsFlowRateMlPerHour);
    preferences.putBool("sameFlowForTails", rectParams.useSameFlowRateForTails);
    
    // Настройки орошения
    preferences.putFloat("refluxRatio", rectParams.refluxRatio);
    preferences.putInt("refluxPeriod", rectParams.refluxPeriod);
    
    preferences.end();
    
    Serial.println("Параметры ректификации сохранены");
    return true;
}

// Загрузка параметров ректификации
bool loadRectificationParams() {
    preferences.begin(RECT_NAMESPACE, true);
    
    rectParams.model = (RectificationModel)preferences.getInt("model", MODEL_CLASSIC);
    
    rectParams.maxCubeTemp = preferences.getFloat("maxCubeTemp", 104.0);
    rectParams.headsTemp = preferences.getFloat("headsTemp", 76.0);
    rectParams.bodyTemp = preferences.getFloat("bodyTemp", 78.0);
    rectParams.tailsTemp = preferences.getFloat("tailsTemp", 94.0);
    rectParams.endTemp = preferences.getFloat("endTemp", 99.0);
    
    rectParams.heatingPowerWatts = preferences.getInt("heatingPowerWatts", 2000);
    rectParams.stabilizationPowerWatts = preferences.getInt("stabilizationPowerWatts", 1500);
    rectParams.bodyPowerWatts = preferences.getInt("bodyPowerWatts", 1200);
    rectParams.tailsPowerWatts = preferences.getInt("tailsPowerWatts", 1000);
    
    // Для обратной совместимости загружаем и проценты
    rectParams.heatingPower = preferences.getInt("heatingPower", 80);
    rectParams.stabilizationPower = preferences.getInt("stabilizationPower", 60);
    rectParams.bodyPower = preferences.getInt("bodyPower", 50);
    rectParams.tailsPower = preferences.getInt("tailsPower", 40);
    
    rectParams.stabilizationTime = preferences.getInt("stabilizationTime", 30);
    rectParams.headsVolume = preferences.getFloat("headsVolume", 100.0);
    rectParams.bodyVolume = preferences.getFloat("bodyVolume", 1000.0);
    
    // Параметры альтернативной модели
    rectParams.headsTargetTimeMinutes = preferences.getInt("headsTargetTime", 20);
    rectParams.postHeadsStabilizationTime = preferences.getInt("postHeadsStabTime", 15);
    rectParams.bodyFlowRateMlPerHour = preferences.getFloat("bodyFlowRate", 500.0);
    rectParams.tempDeltaEndBody = preferences.getFloat("tempDeltaEndBody", 0.5);
    rectParams.tailsCubeTemp = preferences.getFloat("tailsCubeTemp", 95.0);
    rectParams.tailsFlowRateMlPerHour = preferences.getFloat("tailsFlowRate", 200.0);
    rectParams.useSameFlowRateForTails = preferences.getBool("sameFlowForTails", true);
    
    // Настройки орошения
    rectParams.refluxRatio = preferences.getFloat("refluxRatio", 5.0);
    rectParams.refluxPeriod = preferences.getInt("refluxPeriod", 60);
    
    preferences.end();
    
    Serial.println("Параметры ректификации загружены");
    return true;
}

// Сохранение параметров дистилляции
bool saveDistillationParams() {
    preferences.begin(DIST_NAMESPACE, false);
    
    preferences.putFloat("maxCubeTemp", distParams.maxCubeTemp);
    preferences.putFloat("startCollectTemp", distParams.startCollectingTemp);
    preferences.putFloat("endTemp", distParams.endTemp);
    
    preferences.putInt("heatingPowerWatts", distParams.heatingPowerWatts);
    preferences.putInt("distPowerWatts", distParams.distillationPowerWatts);
    
    // Для обратной совместимости сохраняем и проценты
    preferences.putInt("heatingPower", distParams.heatingPower);
    preferences.putInt("distPower", distParams.distillationPower);
    
    preferences.putFloat("flowRate", distParams.flowRate);
    
    // Параметры разделения голов
    preferences.putBool("separateHeads", distParams.separateHeads);
    preferences.putFloat("headsVolume", distParams.headsVolume);
    preferences.putFloat("headsFlowRate", distParams.headsFlowRate);
    
    preferences.end();
    
    Serial.println("Параметры дистилляции сохранены");
    return true;
}

// Загрузка параметров дистилляции
bool loadDistillationParams() {
    preferences.begin(DIST_NAMESPACE, true);
    
    distParams.maxCubeTemp = preferences.getFloat("maxCubeTemp", 102.0);
    distParams.startCollectingTemp = preferences.getFloat("startCollectTemp", 70.0);
    distParams.endTemp = preferences.getFloat("endTemp", 98.0);
    
    distParams.heatingPowerWatts = preferences.getInt("heatingPowerWatts", 2000);
    distParams.distillationPowerWatts = preferences.getInt("distPowerWatts", 1800);
    
    // Для обратной совместимости загружаем и проценты
    distParams.heatingPower = preferences.getInt("heatingPower", 80);
    distParams.distillationPower = preferences.getInt("distPower", 70);
    
    distParams.flowRate = preferences.getFloat("flowRate", 1000.0);
    
    // Параметры разделения голов
    distParams.separateHeads = preferences.getBool("separateHeads", false);
    distParams.headsVolume = preferences.getFloat("headsVolume", 100.0);
    distParams.headsFlowRate = preferences.getFloat("headsFlowRate", 500.0);
    
    preferences.end();
    
    Serial.println("Параметры дистилляции загружены");
    return true;
}

// Сохранение настроек насоса
bool savePumpSettings() {
    preferences.begin(PUMP_NAMESPACE, false);
    
    preferences.putFloat("calibFactor", pumpSettings.calibrationFactor);
    preferences.putFloat("headsFlowRate", pumpSettings.headsFlowRate);
    preferences.putFloat("bodyFlowRate", pumpSettings.bodyFlowRate);
    preferences.putFloat("tailsFlowRate", pumpSettings.tailsFlowRate);
    preferences.putFloat("minFlowRate", pumpSettings.minFlowRate);
    preferences.putFloat("maxFlowRate", pumpSettings.maxFlowRate);
    preferences.putInt("pumpPeriodMs", pumpSettings.pumpPeriodMs);
    
    preferences.end();
    
    Serial.println("Настройки насоса сохранены");
    return true;
}

// Загрузка настроек насоса
bool loadPumpSettings() {
    preferences.begin(PUMP_NAMESPACE, true);
    
    pumpSettings.calibrationFactor = preferences.getFloat("calibFactor", 0.5);
    pumpSettings.headsFlowRate = preferences.getFloat("headsFlowRate", 100.0);
    pumpSettings.bodyFlowRate = preferences.getFloat("bodyFlowRate", 500.0);
    pumpSettings.tailsFlowRate = preferences.getFloat("tailsFlowRate", 200.0);
    pumpSettings.minFlowRate = preferences.getFloat("minFlowRate", 50.0);
    pumpSettings.maxFlowRate = preferences.getFloat("maxFlowRate", 2000.0);
    pumpSettings.pumpPeriodMs = preferences.getInt("pumpPeriodMs", 5000);
    
    preferences.end();
    
    Serial.println("Настройки насоса загружены");
    return true;
}

// Сброс всех настроек к значениям по умолчанию
bool resetAllSettings() {
    // Устанавливаем значения по умолчанию для всех настроек
    setDefaultSystemSettings();
    setDefaultRectificationParams();
    setDefaultDistillationParams();
    setDefaultPumpSettings();
    
    // Сохраняем настройки
    saveSystemSettings();
    saveRectificationParams();
    saveDistillationParams();
    savePumpSettings();
    
    Serial.println("Все настройки сброшены к значениям по умолчанию");
    return true;
}

// Проверка, инициализированы ли настройки
bool areSettingsInitialized() {
    preferences.begin(SYS_NAMESPACE, true);
    bool initialized = preferences.getBool("initialized", false);
    preferences.end();
    return initialized;
}

// Установка значений по умолчанию для системных настроек
void setDefaultSystemSettings() {
    sysSettings.maxHeaterPowerWatts = 3000;
    sysSettings.powerControlMode = POWER_CONTROL_MANUAL;
    
    // Настройки PI-регулятора
    sysSettings.piSettings.kp = 0.5;
    sysSettings.piSettings.ki = 0.1;
    sysSettings.piSettings.outputMin = 0.0;
    sysSettings.piSettings.outputMax = 100.0;
    sysSettings.piSettings.integralLimit = 100.0;
    
    sysSettings.pzemEnabled = false;
    sysSettings.soundEnabled = true;
    sysSettings.soundVolume = 75;
    
    // Настройки дисплея
    sysSettings.displaySettings.enabled = true;
    sysSettings.displaySettings.brightness = 255;
    sysSettings.displaySettings.rotation = 0;
    sysSettings.displaySettings.invertColors = false;
    sysSettings.displaySettings.contrast = 128;
    sysSettings.displaySettings.timeout = 0;
    sysSettings.displaySettings.showLogo = true;
    
    sysSettings.tempUpdateInterval = 1000;
    sysSettings.tempReportInterval = 2000;
    
    // Настройки датчиков температуры
    for (int i = 0; i < MAX_TEMP_SENSORS; i++) {
        sysSettings.tempSensorEnabled[i] = false;
        sysSettings.tempSensorCalibration[i] = 0.0;
        
        // Устанавливаем нулевые адреса
        for (int j = 0; j < 8; j++) {
            sysSettings.tempSensorAddresses[i][j] = 0;
        }
    }
}

// Установка значений по умолчанию для параметров ректификации
void setDefaultRectificationParams() {
    rectParams.model = MODEL_CLASSIC;
    
    rectParams.maxCubeTemp = 104.0;
    rectParams.headsTemp = 76.0;
    rectParams.bodyTemp = 78.0;
    rectParams.tailsTemp = 94.0;
    rectParams.endTemp = 99.0;
    
    rectParams.heatingPowerWatts = 2000;
    rectParams.stabilizationPowerWatts = 1500;
    rectParams.bodyPowerWatts = 1200;
    rectParams.tailsPowerWatts = 1000;
    
    // Для обратной совместимости устанавливаем и проценты
    rectParams.heatingPower = 80;
    rectParams.stabilizationPower = 60;
    rectParams.bodyPower = 50;
    rectParams.tailsPower = 40;
    
    rectParams.stabilizationTime = 30;
    rectParams.headsVolume = 100.0;
    rectParams.bodyVolume = 1000.0;
    
    // Параметры альтернативной модели
    rectParams.headsTargetTimeMinutes = 20;
    rectParams.postHeadsStabilizationTime = 15;
    rectParams.bodyFlowRateMlPerHour = 500.0;
    rectParams.tempDeltaEndBody = 0.5;
    rectParams.tailsCubeTemp = 95.0;
    rectParams.tailsFlowRateMlPerHour = 200.0;
    rectParams.useSameFlowRateForTails = true;
    
    // Настройки орошения
    rectParams.refluxRatio = 5.0;
    rectParams.refluxPeriod = 60;
}

// Установка значений по умолчанию для параметров дистилляции
void setDefaultDistillationParams() {
    distParams.maxCubeTemp = 102.0;
    distParams.startCollectingTemp = 70.0;
    distParams.endTemp = 98.0;
    
    distParams.heatingPowerWatts = 2000;
    distParams.distillationPowerWatts = 1800;
    
    // Для обратной совместимости устанавливаем и проценты
    distParams.heatingPower = 80;
    distParams.distillationPower = 70;
    
    distParams.flowRate = 1000.0;
    
    // Параметры разделения голов
    distParams.separateHeads = false;
    distParams.headsVolume = 100.0;
    distParams.headsFlowRate = 500.0;
}

// Установка значений по умолчанию для настроек насоса
void setDefaultPumpSettings() {
    pumpSettings.calibrationFactor = 0.5;
    pumpSettings.headsFlowRate = 100.0;
    pumpSettings.bodyFlowRate = 500.0;
    pumpSettings.tailsFlowRate = 200.0;
    pumpSettings.minFlowRate = 50.0;
    pumpSettings.maxFlowRate = 2000.0;
    pumpSettings.pumpPeriodMs = 5000;
}
/**
 * @brief Сброс счетчиков собранных объемов
 */
void resetCollectedVolumes() {
    Serial.println("Сброс счетчиков собранных объемов");
    
    // Эти переменные должны быть глобальными в соответствующих модулях
    // Здесь мы просто логируем вызов
    
    // В rectification.cpp должны быть сброшены:
    // headsCollected = 0;
    // bodyCollected = 0;
    // tailsCollected = 0;
    
    // В distillation.cpp должны быть сброшены:
    // productCollected = 0;
    // headsCollected = 0;
}
