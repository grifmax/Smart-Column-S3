#include "pump.h"
#include "utils.h"

// Статус насоса
static bool pumpEnabled = false;

// Текущая скорость отбора (мл/час)
static float currentFlowRate = 0.0;

// Время последнего обновления состояния
static unsigned long lastPumpUpdate = 0;

// Время последнего включения насоса в цикле
static unsigned long lastPumpCycleStart = 0;

// Отобранные объемы для каждой фазы
float headsCollected = 0.0;
float bodyCollected = 0.0;
float tailsCollected = 0.0;
float distillationCollected = 0.0;

// Инициализация насоса
void initPump() {
    Serial.println("Инициализация управления насосом...");
    
    // Настройка пина
    pinMode(PIN_PUMP, OUTPUT);
    disablePump(); // Для безопасности
    
    // Установка начальных значений
    resetCollectedVolumes();
    
    Serial.println("Управление насосом инициализировано");
}

// Включение насоса с заданной скоростью отбора (мл/час)
void enablePump(float flowRateMlPerHour) {
    // Если скорость отбора слишком мала, выключаем насос
    if (flowRateMlPerHour < pumpSettings.minFlowRate) {
        disablePump();
        return;
    }
    
    // Ограничиваем скорость максимальным значением
    if (flowRateMlPerHour > pumpSettings.maxFlowRate) {
        flowRateMlPerHour = pumpSettings.maxFlowRate;
    }
    
    // Устанавливаем текущую скорость
    currentFlowRate = flowRateMlPerHour;
    
    // Включаем насос
    pumpEnabled = true;
    
    Serial.print("Насос включен, скорость отбора: ");
    Serial.print(currentFlowRate);
    Serial.println(" мл/час");
}

// Выключение насоса
void disablePump() {
    digitalWrite(PIN_PUMP, LOW);
    pumpEnabled = false;
    currentFlowRate = 0.0;
    
    Serial.println("Насос выключен");
}

// Обновление работы насоса (вызывается периодически)
void updatePump() {
    unsigned long currentTime = millis();
    
    // Если насос отключен, выходим
    if (!pumpEnabled) {
        digitalWrite(PIN_PUMP, LOW);
        return;
    }
    
    // Рассчитываем параметры цикла ШИМ для насоса
    
    // Длительность цикла в мс (из настроек)
    int cycleDuration = pumpSettings.pumpPeriodMs;
    
    // Производительность насоса при 100% (из настроек, мл/с)
    float maxFlowRate = pumpSettings.calibrationFactor;
    
    // Рассчитываем желаемую производительность в мл/с
    float desiredFlowRate = currentFlowRate / 3600.0; // Переводим из мл/ч в мл/с
    
    // Рассчитываем коэффициент заполнения (скважность)
    float dutyRatio = desiredFlowRate / maxFlowRate;
    
    // Ограничиваем коэффициент в диапазоне 0.0-1.0
    dutyRatio = constrain(dutyRatio, 0.0, 1.0);
    
    // Рассчитываем длительность включения насоса в мс
    int onDuration = (int)(cycleDuration * dutyRatio);
    
    // Если длительность включения менее 50 мс и не 0, устанавливаем минимум 50 мс
    // Это нужно для преодоления инерции насоса
    if (onDuration > 0 && onDuration < 50) {
        onDuration = 50;
    }
    
    // Если пришло время нового цикла
    if (currentTime - lastPumpCycleStart >= cycleDuration) {
        lastPumpCycleStart = currentTime;
        
        // Если длительность включения > 0, включаем насос
        if (onDuration > 0) {
            digitalWrite(PIN_PUMP, HIGH);
        }
    }
    
    // Если пришло время выключить насос
    if (onDuration > 0 && currentTime - lastPumpCycleStart >= onDuration) {
        digitalWrite(PIN_PUMP, LOW);
    }
    
    // Обновляем собранный объем
    if (currentTime - lastPumpUpdate >= 1000) { // Каждую секунду
        float volumeCollectedPerSecond = currentFlowRate / 3600.0; // мл/с
        float timeDeltaSeconds = (currentTime - lastPumpUpdate) / 1000.0;
        float volumeCollected = volumeCollectedPerSecond * timeDeltaSeconds;
        
        // Добавляем к соответствующему счетчику в зависимости от текущей фазы
        if (systemRunning) {
            if (currentMode == MODE_RECTIFICATION) {
                switch (rectPhase) {
                    case PHASE_HEADS:
                        headsCollected += volumeCollected;
                        break;
                    case PHASE_BODY:
                        bodyCollected += volumeCollected;
                        break;
                    case PHASE_TAILS:
                        tailsCollected += volumeCollected;
                        break;
                    default:
                        break;
                }
            } 
            else if (currentMode == MODE_DISTILLATION) {
                distillationCollected += volumeCollected;
            }
        }
        
        lastPumpUpdate = currentTime;
    }
}

// Получение текущей скорости отбора (мл/час)
float getCurrentFlowRate() {
    return currentFlowRate;
}

// Получение общего объема отбора (мл) для текущей фазы
float getCollectedVolume(int phase) {
    if (currentMode == MODE_RECTIFICATION) {
        switch (phase) {
            case PHASE_HEADS:
                return headsCollected;
            case PHASE_BODY:
                return bodyCollected;
            case PHASE_TAILS:
                return tailsCollected;
            default:
                return 0.0;
        }
    } 
    else if (currentMode == MODE_DISTILLATION) {
        return distillationCollected;
    }
    
    return 0.0;
}

// Калибровка насоса
void calibratePump(float calibrationFactor) {
    if (calibrationFactor > 0.0) {
        pumpSettings.calibrationFactor = calibrationFactor;
        
        // Сохраняем настройки
        savePumpSettings();
        
        Serial.print("Насос откалиброван, коэффициент: ");
        Serial.print(calibrationFactor);
        Serial.println(" мл/с при 100% мощности");
    }
}

// Проверка, включен ли насос
bool isPumpEnabled() {
    return pumpEnabled;
}

// Сброс счетчиков отбора
void resetCollectedVolumes() {
    headsCollected = 0.0;
    bodyCollected = 0.0;
    tailsCollected = 0.0;
    distillationCollected = 0.0;
}