#include "distillation.h"
#include "temp_sensors.h"
#include "heater.h"
#include "pump.h"
#include "valve.h"
#include "settings.h"
#include "display.h"
#include "utils.h"
#include <Arduino.h>

// Фазы дистилляции
const char* distPhaseNames[] = {
    "Ожидание",
    "Нагрев",
    "Отбор",
    "Завершено",
    "Ошибка"
};

// Текущая фаза дистилляции
DistillationPhase currentDistPhase = DIST_PHASE_IDLE;

// Флаги состояния процесса
bool distillationRunning = false;
bool distillationPaused = false;

// Время запуска и паузы
unsigned long distStartTime = 0;
unsigned long distPauseTime = 0;
unsigned long distPhaseStartTime = 0;

// Счетчик собранного объёма
int distProductCollected = 0;

// Флаг отбора голов
bool distHeadsMode = false;
int distHeadsCollected = 0;

// Последние измеренные температуры
float distLastCubeTemp = 0;
float distLastColumnTemp = 0;
float distLastProductTemp = 0;

// Инициализация подсистемы дистилляции
void initDistillation() {
    // Сбрасываем все флаги и счётчики
    currentDistPhase = DIST_PHASE_IDLE;
    distillationRunning = false;
    distillationPaused = false;
    
    distStartTime = 0;
    distPauseTime = 0;
    distPhaseStartTime = 0;
    
    distProductCollected = 0;
    
    distHeadsMode = false;
    distHeadsCollected = 0;
    
    // Обновляем температуры
    updateTemperatures();
    distLastCubeTemp = getTemperature(TEMP_CUBE);
    distLastColumnTemp = getTemperature(TEMP_COLUMN);
    distLastProductTemp = getTemperature(TEMP_REFLUX); // В дистилляции используем датчик отбора
    
    Serial.println("Подсистема дистилляции инициализирована");
}

// Запуск процесса дистилляции
bool startDistillation() {
    // Проверяем, что процесс еще не запущен
    if (distillationRunning) {
        return false;
    }
    
    // Проверяем наличие датчика температуры куба
    if (!isSensorConnected(TEMP_CUBE)) {
        Serial.println("Ошибка: Не подключен датчик куба");
        return false;
    }
    
    // Проверяем температуру куба
    float cubeTemp = getTemperature(TEMP_CUBE);
    if (cubeTemp > 50.0f) {
        Serial.println("Ошибка: Температура куба слишком высока для запуска");
        return false;
    }
    
    // Сбрасываем счетчики и таймеры
    distStartTime = millis();
    distPhaseStartTime = distStartTime;
    distPauseTime = 0;
    
    distProductCollected = 0;
    
    // Определяем, нужно ли отделять головы
    distHeadsMode = sysSettings.distillationSettings.separateHeads;
    distHeadsCollected = 0;
    
    // Обновляем последние температуры
    distLastCubeTemp = getTemperature(TEMP_CUBE);
    distLastColumnTemp = getTemperature(TEMP_COLUMN);
    distLastProductTemp = getTemperature(TEMP_REFLUX); // В дистилляции используем датчик отбора
    
    // Включаем нагреватель на начальной мощности
    setHeaterPower(sysSettings.distillationSettings.heatingPowerWatts);
    
    // Закрываем клапан отбора
    valveClose();
    
    // Останавливаем насос
    pumpStop();
    
    // Устанавливаем начальную фазу
    setDistillationPhase(DIST_PHASE_HEATING);
    
    distillationRunning = true;
    distillationPaused = false;
    
    Serial.println("Процесс дистилляции запущен");
    
    return true;
}

// Остановка процесса дистилляции
void stopDistillation() {
    if (!distillationRunning) {
        return;
    }
    
    // Выключаем нагреватель
    setHeaterPower(0);
    
    // Закрываем клапан отбора
    valveClose();
    
    // Останавливаем насос
    pumpStop();
    
    // Сбрасываем состояние
    distillationRunning = false;
    distillationPaused = false;
    setDistillationPhase(DIST_PHASE_IDLE);
    
    Serial.println("Процесс дистилляции остановлен");
}

// Пауза процесса дистилляции
void pauseDistillation() {
    if (!distillationRunning || distillationPaused) {
        return;
    }
    
    // Запоминаем время паузы
    distPauseTime = millis();
    
    // Выключаем нагреватель
    setHeaterPower(0);
    
    // Закрываем клапан отбора
    valveClose();
    
    // Останавливаем насос
    pumpStop();
    
    distillationPaused = true;
    
    Serial.println("Процесс дистилляции на паузе");
}

// Возобновление процесса дистилляции
void resumeDistillation() {
    if (!distillationRunning || !distillationPaused) {
        return;
    }
    
    // Корректируем время начала с учетом паузы
    unsigned long pauseDuration = millis() - distPauseTime;
    distStartTime += pauseDuration;
    distPhaseStartTime += pauseDuration;
    
    // Восстанавливаем мощность нагревателя в зависимости от фазы
    switch (currentDistPhase) {
        case DIST_PHASE_HEATING:
            setHeaterPower(sysSettings.distillationSettings.heatingPowerWatts);
            break;
        case DIST_PHASE_DISTILLATION:
            setHeaterPower(sysSettings.distillationSettings.distillationPowerWatts);
            // Восстанавливаем отбор
            valveOpen();
            if (distHeadsMode) {
                pumpStart(sysSettings.distillationSettings.headsFlowRate);
            } else {
                pumpStart(sysSettings.distillationSettings.flowRate);
            }
            break;
        default:
            setHeaterPower(0);
            break;
    }
    
    distillationPaused = false;
    
    Serial.println("Процесс дистилляции возобновлен");
}

// Обработка процесса дистилляции
void processDistillation() {
    // Пропускаем обработку, если процесс не запущен или на паузе
    if (!distillationRunning || distillationPaused) {
        return;
    }
    
    // Проверяем условия безопасности
    if (!checkDistillationSafety()) {
        Serial.println("Сработала защита! Процесс дистилляции остановлен");
        setDistillationPhase(DIST_PHASE_ERROR);
        stopDistillation();
        return;
    }
    
    // Обновляем данные о температуре
    updateTemperatures();
    distLastCubeTemp = getTemperature(TEMP_CUBE);
    distLastColumnTemp = getTemperature(TEMP_COLUMN);
    distLastProductTemp = getTemperature(TEMP_REFLUX); // В дистилляции используем датчик отбора
    
    // Обрабатываем текущую фазу
    switch (currentDistPhase) {
        case DIST_PHASE_HEATING:
            processDistHeatingPhase();
            break;
        case DIST_PHASE_DISTILLATION:
            processDistillationPhase();
            break;
        case DIST_PHASE_COMPLETED:
            // Процесс завершен, ничего не делаем
            break;
        case DIST_PHASE_ERROR:
            // Ошибка, ничего не делаем
            break;
        default:
            // Неизвестная фаза, останавливаем процесс
            stopDistillation();
            break;
    }
}

// Обработка фазы нагрева для дистилляции
void processDistHeatingPhase() {
    // Фаза нагрева: нагреваем куб до рабочей температуры
    
    // Определяем, какую температуру проверять
    float tempToCheck;
    
    if (isSensorConnected(TEMP_REFLUX)) {
        tempToCheck = distLastProductTemp;
    } else {
        tempToCheck = distLastCubeTemp;
    }
    
    // Проверяем, достигла ли температура заданной температуры начала отбора
    if (tempToCheck >= sysSettings.distillationSettings.startCollectingTemp) {
        // Переходим к фазе дистилляции
        setHeaterPower(sysSettings.distillationSettings.distillationPowerWatts);
        valveOpen();
        
        // Проверяем, нужно ли отделять головы
        if (sysSettings.distillationSettings.separateHeads) {
            distHeadsMode = true;
            pumpStart(sysSettings.distillationSettings.headsFlowRate);
        } else {
            distHeadsMode = false;
            pumpStart(sysSettings.distillationSettings.flowRate);
        }
        
        setDistillationPhase(DIST_PHASE_DISTILLATION);
    }
}

// Обработка фазы дистилляции
void processDistillationPhase() {
    // Проверяем, достигнута ли температура завершения процесса
    if (distLastCubeTemp >= sysSettings.distillationSettings.endTemp) {
        // Процесс завершен
        setHeaterPower(0);
        pumpStop();
        valveClose();
        setDistillationPhase(DIST_PHASE_COMPLETED);
        return;
    }
    
    // Если включено отделение голов, проверяем собранный объем голов
    if (distHeadsMode && sysSettings.distillationSettings.separateHeads) {
        // Увеличиваем счетчик отбора голов
        int currentExtracted = pumpGetExtractedVolume();
        distHeadsCollected += currentExtracted;
        distProductCollected += currentExtracted;
        
        // Если собрали достаточно голов, переключаемся на отбор тела
        if (distHeadsCollected >= sysSettings.distillationSettings.headsVolume) {
            distHeadsMode = false;
            pumpResetExtractedVolume();
            pumpStart(sysSettings.distillationSettings.flowRate);
            
            Serial.print("Отбор голов завершен. Собрано: ");
            Serial.print(distHeadsCollected);
            Serial.println(" мл.");
        }
    } else {
        // Увеличиваем счетчик отбора продукта
        distProductCollected += pumpGetExtractedVolume();
    }
}

// Проверка условий безопасности для дистилляции
bool checkDistillationSafety() {
    // Проверка максимальной температуры куба
    if (distLastCubeTemp > sysSettings.distillationSettings.maxCubeTemp) {
        Serial.println("Превышена максимальная температура куба!");
        return false;
    }
    
    // Проверка наличия датчика температуры куба
    if (!isSensorConnected(TEMP_CUBE)) {
        Serial.println("Датчик куба отключен!");
        return false;
    }
    
    // Все проверки пройдены
    return true;
}

// Установка фазы дистилляции
void setDistillationPhase(DistillationPhase phase) {
    if (currentDistPhase == phase) {
        return;
    }
    
    // Сохраняем предыдущую фазу для логирования
    DistillationPhase prevPhase = currentDistPhase;
    
    // Устанавливаем новую фазу
    currentDistPhase = phase;
    distPhaseStartTime = millis();
    
    Serial.print("Изменение фазы дистилляции: ");
    Serial.print(distPhaseNames[prevPhase]);
    Serial.print(" -> ");
    Serial.println(distPhaseNames[currentDistPhase]);
    
    // Действия при переходе на новую фазу
    switch (phase) {
        case DIST_PHASE_HEATING:
            setHeaterPower(sysSettings.distillationSettings.heatingPowerWatts);
            valveClose();
            pumpStop();
            break;
        case DIST_PHASE_DISTILLATION:
            setHeaterPower(sysSettings.distillationSettings.distillationPowerWatts);
            valveOpen();
            
            // Определяем скорость отбора в зависимости от того, отбираем ли головы
            if (sysSettings.distillationSettings.separateHeads) {
                distHeadsMode = true;
                pumpStart(sysSettings.distillationSettings.headsFlowRate);
            } else {
                distHeadsMode = false;
                pumpStart(sysSettings.distillationSettings.flowRate);
            }
            
            pumpResetExtractedVolume();
            break;
        case DIST_PHASE_COMPLETED:
        case DIST_PHASE_ERROR:
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

// Получение текущей фазы дистилляции
DistillationPhase getDistillationPhase() {
    return currentDistPhase;
}

// Получение имени текущей фазы
const char* getDistillationPhaseName() {
    return distPhaseNames[currentDistPhase];
}

// Проверка, запущен ли процесс дистилляции
bool isDistillationRunning() {
    return distillationRunning;
}

// Проверка, на паузе ли процесс дистилляции
bool isDistillationPaused() {
    return distillationPaused;
}

// Получение объема собранного продукта
int getDistillationProductVolume() {
    return distProductCollected;
}

// Получение объема собранных голов
int getDistillationHeadsVolume() {
    return distHeadsCollected;
}

// Проверка, находится ли процесс в режиме отбора голов
bool isDistillationHeadsMode() {
    return distHeadsMode;
}

// Получение времени работы процесса
unsigned long getDistillationUptime() {
    if (!distillationRunning) {
        return 0;
    }
    
    if (distillationPaused) {
        return (distPauseTime - distStartTime) / 1000; // секунды
    }
    
    return (millis() - distStartTime) / 1000; // секунды
}

// Получение времени текущей фазы
unsigned long getDistillationPhaseTime() {
    if (!distillationRunning) {
        return 0;
    }
    
    if (distillationPaused) {
        return (distPauseTime - distPhaseStartTime) / 1000; // секунды
    }
    
    return (millis() - distPhaseStartTime) / 1000; // секунды
}

// Получение текущей температуры куба
float getDistillationCubeTemp() {
    return distLastCubeTemp;
}

// Получение текущей температуры колонны
float getDistillationColumnTemp() {
    return distLastColumnTemp;
}

// Получение текущей температуры продукта
float getDistillationProductTemp() {
    return distLastProductTemp;
}