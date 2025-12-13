#include "rectification.h"
#include "temp_sensors.h"
#include "heater.h"
#include "pump.h"
#include "valve.h"
#include "settings.h"
#include "display.h"
#include "utils.h"
#include <Arduino.h>

// Фазы ректификации
const char* phaseNames[] = {
    "Ожидание",
    "Нагрев",
    "Стабилизация",
    "Отбор голов",
    "Стаб. после голов",
    "Отбор тела",
    "Отбор хвостов",
    "Завершено",
    "Ошибка"
};

// Текущая фаза ректификации
RectificationPhase currentPhase = RECT_PHASE_IDLE;

// Флаги состояния процесса
bool rectificationRunning = false;
bool rectificationPaused = false;

// Время запуска и паузы
unsigned long rectStartTime = 0;
unsigned long rectPauseTime = 0;
unsigned long phaseStartTime = 0;

// Счетчики собранного объёма
int headsCollected = 0;
int bodyCollected = 0;
int tailsCollected = 0;

// Состояние орошения
bool refluxState = false;
unsigned long lastRefluxToggleTime = 0;

// Последние измеренные температуры
float lastCubeTemp = 0;
float lastColumnTemp = 0;
float lastRefluxTemp = 0;

// Инициализация подсистемы ректификации
void initRectification() {
    // Сбрасываем все флаги и счётчики
    currentPhase = RECT_PHASE_IDLE;
    rectificationRunning = false;
    rectificationPaused = false;
    
    rectStartTime = 0;
    rectPauseTime = 0;
    phaseStartTime = 0;
    
    headsCollected = 0;
    bodyCollected = 0;
    tailsCollected = 0;
    
    refluxState = false;
    lastRefluxToggleTime = 0;
    
    // Обновляем температуры
    updateTemperatures();
    lastCubeTemp = getTemperature(TEMP_CUBE);
    lastColumnTemp = getTemperature(TEMP_COLUMN);
    lastRefluxTemp = getTemperature(TEMP_REFLUX);
    
    Serial.println("Подсистема ректификации инициализирована");
}

// Запуск процесса ректификации
bool startRectification() {
    // Проверяем, что процесс еще не запущен
    if (rectificationRunning) {
        return false;
    }
    
    // Проверяем наличие датчиков
    if (!isSensorConnected(TEMP_CUBE) || !isSensorConnected(TEMP_REFLUX)) {
        Serial.println("Ошибка: Не подключены необходимые датчики");
        return false;
    }
    
    // Проверяем температуру куба
    float cubeTemp = getTemperature(TEMP_CUBE);
    if (cubeTemp > 50.0f) {
        Serial.println("Ошибка: Температура куба слишком высока для запуска");
        return false;
    }
    
    // Сбрасываем счетчики и таймеры
    rectStartTime = millis();
    phaseStartTime = rectStartTime;
    rectPauseTime = 0;
    
    headsCollected = 0;
    bodyCollected = 0;
    tailsCollected = 0;
    
    // Обновляем последние температуры
    lastCubeTemp = getTemperature(TEMP_CUBE);
    lastColumnTemp = getTemperature(TEMP_COLUMN);
    lastRefluxTemp = getTemperature(TEMP_REFLUX);
    
    // Сбрасываем состояние орошения
    refluxState = false;
    lastRefluxToggleTime = 0;
    
    // Включаем нагреватель на начальной мощности
    setHeaterPower(sysSettings.rectificationSettings.heatingPowerWatts);
    
    // Закрываем клапан отбора
    valveClose();
    
    // Останавливаем насос
    pumpStop();
    
    // Устанавливаем начальную фазу
    setRectificationPhase(RECT_PHASE_HEATING);
    
    rectificationRunning = true;
    rectificationPaused = false;
    
    Serial.println("Процесс ректификации запущен");
    
    return true;
}

// Остановка процесса ректификации
void stopRectification() {
    if (!rectificationRunning) {
        return;
    }
    
    // Выключаем нагреватель
    setHeaterPower(0);
    
    // Закрываем клапан отбора
    valveClose();
    
    // Останавливаем насос
    pumpStop();
    
    // Сбрасываем состояние
    rectificationRunning = false;
    rectificationPaused = false;
    setRectificationPhase(RECT_PHASE_IDLE);
    
    Serial.println("Процесс ректификации остановлен");
}

// Пауза процесса ректификации
void pauseRectification() {
    if (!rectificationRunning || rectificationPaused) {
        return;
    }
    
    // Запоминаем время паузы
    rectPauseTime = millis();
    
    // Выключаем нагреватель
    setHeaterPower(0);
    
    // Закрываем клапан отбора
    valveClose();
    
    // Останавливаем насос
    pumpStop();
    
    rectificationPaused = true;
    
    Serial.println("Процесс ректификации на паузе");
}

// Возобновление процесса ректификации
void resumeRectification() {
    if (!rectificationRunning || !rectificationPaused) {
        return;
    }
    
    // Корректируем время начала с учетом паузы
    unsigned long pauseDuration = millis() - rectPauseTime;
    rectStartTime += pauseDuration;
    phaseStartTime += pauseDuration;
    
    // Восстанавливаем мощность нагревателя в зависимости от фазы
    switch (currentPhase) {
        case RECT_PHASE_HEATING:
            setHeaterPower(sysSettings.rectificationSettings.heatingPowerWatts);
            break;
        case RECT_PHASE_STABILIZATION:
            setHeaterPower(sysSettings.rectificationSettings.stabilizationPowerWatts);
            break;
        case RECT_PHASE_HEADS:
        case RECT_PHASE_POST_HEADS_STAB:
            setHeaterPower(sysSettings.rectificationSettings.stabilizationPowerWatts);
            break;
        case RECT_PHASE_BODY:
            setHeaterPower(sysSettings.rectificationSettings.bodyPowerWatts);
            break;
        case RECT_PHASE_TAILS:
            setHeaterPower(sysSettings.rectificationSettings.tailsPowerWatts);
            break;
        default:
            setHeaterPower(0);
            break;
    }
    
    rectificationPaused = false;
    
    Serial.println("Процесс ректификации возобновлен");
}

// Обработка процесса ректификации
void processRectification() {
    // Пропускаем обработку, если процесс не запущен или на паузе
    if (!rectificationRunning || rectificationPaused) {
        return;
    }
    
    // Проверяем условия безопасности
    if (!checkRectificationSafety()) {
        Serial.println("Сработала защита! Процесс ректификации остановлен");
        setRectificationPhase(RECT_PHASE_ERROR);
        stopRectification();
        return;
    }
    
    // Обновляем данные о температуре
    updateTemperatures();
    lastCubeTemp = getTemperature(TEMP_CUBE);
    lastColumnTemp = getTemperature(TEMP_COLUMN);
    lastRefluxTemp = getTemperature(TEMP_REFLUX);
    
    // Обрабатываем текущую фазу
    switch (currentPhase) {
        case RECT_PHASE_HEATING:
            processHeatingPhase();
            break;
        case RECT_PHASE_STABILIZATION:
            processStabilizationPhase();
            break;
        case RECT_PHASE_HEADS:
            processHeadsPhase();
            break;
        case RECT_PHASE_POST_HEADS_STAB:
            processPostHeadsStabilizationPhase();
            break;
        case RECT_PHASE_BODY:
            processBodyPhase();
            break;
        case RECT_PHASE_TAILS:
            processTailsPhase();
            break;
        case RECT_PHASE_COMPLETED:
            // Процесс завершен, ничего не делаем
            break;
        case RECT_PHASE_ERROR:
            // Ошибка, ничего не делаем
            break;
        default:
            // Неизвестная фаза, останавливаем процесс
            stopRectification();
            break;
    }
}

// Обработка фазы нагрева
void processHeatingPhase() {
    // Фаза нагрева: нагреваем куб до рабочей температуры
    
    // Проверяем, достигла ли температура колонны заданной температуры голов
    if (lastRefluxTemp >= sysSettings.rectificationSettings.headsTemp) {
        // Переходим к фазе стабилизации
        setHeaterPower(sysSettings.rectificationSettings.stabilizationPowerWatts);
        setRectificationPhase(RECT_PHASE_STABILIZATION);
    }
}

// Обработка фазы стабилизации
void processStabilizationPhase() {
    // Фаза стабилизации: выдерживаем колонну определенное время для стабилизации
    
    // Проверяем, прошло ли заданное время стабилизации
    unsigned long phaseTime = millis() - phaseStartTime;
    unsigned long stabilizationTimeMs = sysSettings.rectificationSettings.stabilizationTime * 60000; // минуты -> миллисекунды
    
    if (phaseTime >= stabilizationTimeMs) {
        // Переходим к фазе отбора голов
        setRectificationPhase(RECT_PHASE_HEADS);
    }
    else {
        // Управляем орошением во время стабилизации
        controlReflux();
    }
}

// Обработка фазы отбора голов
void processHeadsPhase() {
    // Проверяем модель ректификации
    if (sysSettings.rectificationSettings.model == 0) {
        // Классическая модель: отбираем заданный объем голов
        if (headsCollected >= sysSettings.rectificationSettings.headsVolume) {
            // Переходим к фазе отбора тела
            setHeaterPower(sysSettings.rectificationSettings.bodyPowerWatts);
            setRectificationPhase(RECT_PHASE_BODY);
            return;
        }
        
        // Отбираем головы через насос
        float headRate = sysSettings.pumpSettings.headsFlowRate;
        pumpStart(headRate);
        valveOpen();
        
        // Увеличиваем счетчик отбора голов
        headsCollected += pumpGetExtractedVolume();
        
    } else {
        // Альтернативная модель: отбираем головы заданное время
        unsigned long phaseTime = millis() - phaseStartTime;
        unsigned long headsTimeMs = sysSettings.rectificationSettings.headsTargetTime * 60000; // минуты -> миллисекунды
        
        if (phaseTime >= headsTimeMs) {
            // Переходим к фазе стабилизации после голов
            pumpStop();
            valveClose();
            setRectificationPhase(RECT_PHASE_POST_HEADS_STAB);
            return;
        }
        
        // Отбираем головы с заданной скоростью
        pumpStart(sysSettings.pumpSettings.headsFlowRate);
        valveOpen();
        
        // Увеличиваем счетчик отбора голов
        headsCollected += pumpGetExtractedVolume();
    }
}

// Обработка фазы стабилизации после отбора голов
void processPostHeadsStabilizationPhase() {
    // Фаза стабилизации после отбора голов (только для альтернативной модели)
    
    // Проверяем, прошло ли заданное время стабилизации после голов
    unsigned long phaseTime = millis() - phaseStartTime;
    unsigned long stabilizationTimeMs = sysSettings.rectificationSettings.postHeadsStabilizationTime * 60000; // минуты -> миллисекунды
    
    if (phaseTime >= stabilizationTimeMs) {
        // Переходим к фазе отбора тела
        setHeaterPower(sysSettings.rectificationSettings.bodyPowerWatts);
        setRectificationPhase(RECT_PHASE_BODY);
    }
    else {
        // Управляем орошением во время стабилизации
        controlReflux();
    }
}

// Обработка фазы отбора тела
void processBodyPhase() {
    // Проверяем модель ректификации
    if (sysSettings.rectificationSettings.model == 0) {
        // Классическая модель: отбираем заданный объем тела или до достижения температуры хвостов
        if (bodyCollected >= sysSettings.rectificationSettings.bodyVolume || 
            lastRefluxTemp >= sysSettings.rectificationSettings.tailsTemp) {
            // Переходим к фазе отбора хвостов
            setHeaterPower(sysSettings.rectificationSettings.tailsPowerWatts);
            setRectificationPhase(RECT_PHASE_TAILS);
            return;
        }
        
        // Управляем орошением при отборе тела
        controlReflux();
        
    } else {
        // Альтернативная модель: отбираем тело до определенной дельты температуры
        float tempDelta = lastRefluxTemp - sysSettings.rectificationSettings.bodyTemp;
        
        // Проверяем дельту температуры для перехода к хвостам
        if (tempDelta >= sysSettings.rectificationSettings.tempDeltaEndBody || 
            lastCubeTemp >= sysSettings.rectificationSettings.tailsCubeTemp) {
            // Переходим к фазе отбора хвостов
            setHeaterPower(sysSettings.rectificationSettings.tailsPowerWatts);
            setRectificationPhase(RECT_PHASE_TAILS);
            return;
        }
        
        // Управляем орошением при отборе тела
        controlReflux();
    }
    
    // Отбираем тело с заданной скоростью
    if (!refluxState) {
        pumpStart(sysSettings.pumpSettings.bodyFlowRate);
        valveOpen();
        
        // Увеличиваем счетчик отбора тела
        bodyCollected += pumpGetExtractedVolume();
    }
}

// Обработка фазы отбора хвостов
void processTailsPhase() {
    // Проверяем, достигнута ли температура завершения процесса
    if (lastCubeTemp >= sysSettings.rectificationSettings.endTemp) {
        // Процесс завершен
        setHeaterPower(0);
        pumpStop();
        valveClose();
        setRectificationPhase(RECT_PHASE_COMPLETED);
        return;
    }
    
    // Отбираем хвосты с заданной скоростью
    float tailsRate = sysSettings.rectificationSettings.useSameFlowForTails ? 
                       sysSettings.pumpSettings.bodyFlowRate : 
                       sysSettings.pumpSettings.tailsFlowRate;
                       
    // Отбираем хвосты в режиме прерывистого отбора
    controlReflux();
    
    if (!refluxState) {
        pumpStart(tailsRate);
        valveOpen();
        
        // Увеличиваем счетчик отбора хвостов
        tailsCollected += pumpGetExtractedVolume();
    }
}

// Управление орошением
void controlReflux() {
    // Управление орошением на основе настроек
    unsigned long currentTime = millis();
    unsigned long refluxPeriod = sysSettings.rectificationSettings.refluxPeriod * 1000; // секунды -> миллисекунды
    
    // Определяем соотношение орошения
    float refluxRatio = sysSettings.rectificationSettings.refluxRatio;
    
    // Рассчитываем длительности фаз орошения и отбора
    unsigned long totalPeriod = refluxPeriod;
    unsigned long refluxTime = totalPeriod * refluxRatio / (refluxRatio + 1);
    unsigned long extractionTime = totalPeriod - refluxTime;
    
    // Определяем текущую позицию в цикле
    unsigned long cycleTime = (currentTime - lastRefluxToggleTime) % totalPeriod;
    bool newRefluxState = (cycleTime < refluxTime);
    
    // Если состояние орошения изменилось
    if (newRefluxState != refluxState || lastRefluxToggleTime == 0) {
        refluxState = newRefluxState;
        lastRefluxToggleTime = currentTime - cycleTime;
        
        if (refluxState) {
            // Орошение включено - закрываем клапан отбора (жидкость возвращается в колонну)
            valveClose();
            pumpStop();
        } else {
            // Орошение выключено - открываем клапан отбора
            valveOpen();
            
            // Запускаем насос при отборе с заданной скоростью в зависимости от фазы
            if (currentPhase == RECT_PHASE_HEADS) {
                pumpStart(sysSettings.pumpSettings.headsFlowRate);
            } else if (currentPhase == RECT_PHASE_BODY) {
                pumpStart(sysSettings.pumpSettings.bodyFlowRate);
            } else if (currentPhase == RECT_PHASE_TAILS) {
                pumpStart(sysSettings.pumpSettings.tailsFlowRate);
            }
        }
    }
}

// Проверка условий безопасности для ректификации
bool checkRectificationSafety() {
    // Проверка максимальной температуры куба
    if (lastCubeTemp > sysSettings.rectificationSettings.maxCubeTemp) {
        Serial.println("Превышена максимальная температура куба!");
        return false;
    }
    
    // Проверка наличия датчиков температуры
    if (!isSensorConnected(TEMP_CUBE) || !isSensorConnected(TEMP_REFLUX)) {
        Serial.println("Один из необходимых датчиков отключен!");
        return false;
    }
    
    // Все проверки пройдены
    return true;
}

// Установка фазы ректификации
void setRectificationPhase(RectificationPhase phase) {
    if (currentPhase == phase) {
        return;
    }
    
    // Сохраняем предыдущую фазу для логирования
    RectificationPhase prevPhase = currentPhase;
    
    // Устанавливаем новую фазу
    currentPhase = phase;
    phaseStartTime = millis();
    
    Serial.print("Изменение фазы ректификации: ");
    Serial.print(phaseNames[prevPhase]);
    Serial.print(" -> ");
    Serial.println(phaseNames[currentPhase]);
    
    // Действия при переходе на новую фазу
    switch (phase) {
        case RECT_PHASE_HEATING:
            setHeaterPower(sysSettings.rectificationSettings.heatingPowerWatts);
            valveClose();
            pumpStop();
            break;
        case RECT_PHASE_STABILIZATION:
            setHeaterPower(sysSettings.rectificationSettings.stabilizationPowerWatts);
            valveClose();
            pumpStop();
            break;
        case RECT_PHASE_HEADS:
            valveOpen();
            pumpStart(sysSettings.pumpSettings.headsFlowRate);
            pumpResetExtractedVolume();
            break;
        case RECT_PHASE_POST_HEADS_STAB:
            setHeaterPower(sysSettings.rectificationSettings.stabilizationPowerWatts);
            valveClose();
            pumpStop();
            break;
        case RECT_PHASE_BODY:
            setHeaterPower(sysSettings.rectificationSettings.bodyPowerWatts);
            valveOpen();
            pumpStart(sysSettings.rectificationSettings.bodyFlowRate);
            pumpResetExtractedVolume();
            break;
        case RECT_PHASE_TAILS:
            setHeaterPower(sysSettings.rectificationSettings.tailsPowerWatts);
            valveOpen();
            if (sysSettings.rectificationSettings.useSameFlowForTails) {
                pumpStart(sysSettings.rectificationSettings.bodyFlowRate);
            } else {
                pumpStart(sysSettings.rectificationSettings.tailsFlowRate);
            }
            pumpResetExtractedVolume();
            break;
        case RECT_PHASE_COMPLETED:
        case RECT_PHASE_ERROR:
            setHeaterPower(0);
            valveClose();
            pumpStop();
            break;
        default:
            break;
    }
    
    // Обновляем информацию на дисплее
    updateDisplay();
}

// Получение текущей фазы ректификации
RectificationPhase getRectificationPhase() {
    return currentPhase;
}

// Получение имени текущей фазы
const char* getRectificationPhaseName() {
    return phaseNames[currentPhase];
}

// Проверка, запущен ли процесс ректификации
bool isRectificationRunning() {
    return rectificationRunning;
}

// Проверка, на паузе ли процесс ректификации
bool isRectificationPaused() {
    return rectificationPaused;
}

// Получение объема собранных голов
int getRectificationHeadsVolume() {
    return headsCollected;
}

// Получение объема собранного тела
int getRectificationBodyVolume() {
    return bodyCollected;
}

// Получение объема собранных хвостов
int getRectificationTailsVolume() {
    return tailsCollected;
}

// Получение общего объема продукта
int getRectificationTotalVolume() {
    return headsCollected + bodyCollected + tailsCollected;
}

// Получение времени работы процесса
unsigned long getRectificationUptime() {
    if (!rectificationRunning) {
        return 0;
    }
    
    if (rectificationPaused) {
        return (rectPauseTime - rectStartTime) / 1000; // секунды
    }
    
    return (millis() - rectStartTime) / 1000; // секунды
}

// Получение времени текущей фазы
unsigned long getRectificationPhaseTime() {
    if (!rectificationRunning) {
        return 0;
    }
    
    if (rectificationPaused) {
        return (rectPauseTime - phaseStartTime) / 1000; // секунды
    }
    
    return (millis() - phaseStartTime) / 1000; // секунды
}

// Получение текущей температуры куба
float getRectificationCubeTemp() {
    return lastCubeTemp;
}

// Получение текущей температуры колонны
float getRectificationColumnTemp() {
    return lastColumnTemp;
}

// Получение текущей температуры в узле отбора
float getRectificationRefluxTemp() {
    return lastRefluxTemp;
}

// Получение статуса орошения (true - включено)
bool getRectificationRefluxStatus() {
    return refluxState;
}