#include "tasks.h"
#include "temp_sensors.h"
#include "power_control.h"
#include "pump.h"
#include "valve.h"
#include "utils.h"
#include "display.h"
#include "buttons.h"
#include "webserver.h"

// Идентификаторы задач FreeRTOS
TaskHandle_t temperatureTaskHandle = NULL;
TaskHandle_t controlTaskHandle = NULL;
TaskHandle_t interfaceTaskHandle = NULL;

// Время последнего обновления температур
static unsigned long lastTempUpdate = 0;
// Время последней отправки данных о температурах
static unsigned long lastTempReport = 0;
// Время последней проверки процесса
static unsigned long lastProcessCheck = 0;

// Задача для опроса датчиков температуры
void temperatureTask(void* parameter) {
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // Период 100 мс для проверки
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (true) {
        // Синхронизация задачи
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        
        unsigned long currentTime = millis();
        
        // Обновление температур с заданной периодичностью
        if (currentTime - lastTempUpdate >= sysSettings.tempUpdateInterval) {
            updateTemperatures();
            lastTempUpdate = currentTime;
        }
        
        // Отправка данных о температурах с заданной периодичностью
        if (currentTime - lastTempReport >= sysSettings.tempReportInterval) {
            sendTemperaturesWebSocket();
            lastTempReport = currentTime;
        }
    }
}

// Задача для управления процессами
void controlTask(void* parameter) {
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // Период 100 мс для проверки
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (true) {
        // Синхронизация задачи
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        
        unsigned long currentTime = millis();
        
        // Проверка и управление процессом
        if (systemRunning && !systemPaused) {
            // Проверяем процесс каждые 500 мс
            if (currentTime - lastProcessCheck >= 500) {
                if (currentMode == MODE_RECTIFICATION) {
                    processRectification();
                } else if (currentMode == MODE_DISTILLATION) {
                    processDistillation();
                }
                
                lastProcessCheck = currentTime;
            }
        }
        
        // Обновление состояния нагревателя
        updateHeater();
        
        // Обновление состояния насоса
        updatePump();
        
        // Обновление состояния клапана
        updateValve();
        
        // Обновляем статус в веб-интерфейсе каждые 1000 мс
        static unsigned long lastWebUpdate = 0;
        if (currentTime - lastWebUpdate >= 1000) {
            sendStatusWebSocket();
            lastWebUpdate = currentTime;
        }
    }
}

// Задача для интерфейса (кнопки и дисплей)
void interfaceTask(void* parameter) {
    const TickType_t xFrequency = pdMS_TO_TICKS(50); // 50 мс для опроса кнопок
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (true) {
        // Синхронизация задачи
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        
        // Обновление состояния кнопок
        updateButtons();
        
        // Обработка действий кнопок
        handleButtonActions();
        
        // Обновление дисплея
        updateDisplay();
    }
}

// Обработка процесса ректификации
void processRectification() {
    // Проверка безопасности - максимальная температура куба
    if (temperatures[TEMP_CUBE] > rectParams.maxCubeTemp) {
        sendWebNotification(NOTIFY_ERROR, "Превышена максимальная температура куба");
        emergencyHeaterShutdown("Превышена максимальная температура куба");
        return;
    }
    
    // Обработка в зависимости от текущей фазы
    switch (rectPhase) {
        case PHASE_HEATING:
            // Фаза нагрева - ждем, пока температура колонны достигнет температуры голов
            if (temperatures[TEMP_REFLUX] >= rectParams.headsTemp) {
                // Переходим к фазе стабилизации
                rectPhase = PHASE_STABILIZATION;
                stabilizationStartTime = millis();
                
                // Устанавливаем мощность для стабилизации
                if (sysSettings.powerControlMode == POWER_CONTROL_MANUAL) {
                    setPowerPercent(rectParams.stabilizationPower);
                } else {
                    setPowerWatts(rectParams.stabilizationPowerWatts);
                }
                
                sendWebNotification(NOTIFY_INFO, "Начало фазы стабилизации колонны");
                playSound(SOUND_PHASE_CHANGE);
            }
            break;
            
        case PHASE_STABILIZATION:
            // Фаза стабилизации - ждем заданное время
            if (millis() - stabilizationStartTime >= (unsigned long)rectParams.stabilizationTime * 60 * 1000) {
                // Переходим к фазе отбора голов
                rectPhase = PHASE_HEADS;
                headsStartTime = millis();
                
                // Открываем клапан для отбора голов
                enableValve();
                
                // Включаем насос на скорости отбора голов
                enablePump(pumpSettings.headsFlowRate);
                
                sendWebNotification(NOTIFY_INFO, "Начало отбора голов");
                playSound(SOUND_PHASE_CHANGE);
            }
            break;
            
        case PHASE_HEADS:
            // Фаза отбора голов
            if (rectParams.model == MODEL_CLASSIC) {
                // Для классической модели проверяем объем отобранных голов
                if (headsCollected >= rectParams.headsVolume) {
                    // Переходим к фазе отбора тела
                    if (rectParams.postHeadsStabilizationTime > 0) {
                        // Если задано время стабилизации после голов, переходим к ней
                        rectPhase = PHASE_POST_HEADS_STABILIZATION;
                        stabilizationStartTime = millis();
                        
                        // Выключаем насос и клапан
                        disablePump();
                        disableValve();
                        
                        sendWebNotification(NOTIFY_INFO, "Начало фазы стабилизации после отбора голов");
                        playSound(SOUND_PHASE_CHANGE);
                    } else {
                        // Иначе сразу к отбору тела
                        rectPhase = PHASE_BODY;
                        
                        // Устанавливаем мощность для отбора тела
                        if (sysSettings.powerControlMode == POWER_CONTROL_MANUAL) {
                            setPowerPercent(rectParams.bodyPower);
                        } else {
                            setPowerWatts(rectParams.bodyPowerWatts);
                        }
                        
                        // Включаем насос на скорости отбора тела
                        enablePump(pumpSettings.bodyFlowRate);
                        
                        // Устанавливаем параметры орошения
                        setRefluxRatio(rectParams.refluxRatio, rectParams.refluxPeriod);
                        
                        sendWebNotification(NOTIFY_INFO, "Начало отбора тела");
                        playSound(SOUND_PHASE_CHANGE);
                    }
                }
                // Проверяем, не поднялась ли температура слишком высоко
                else if (temperatures[TEMP_REFLUX] >= rectParams.bodyTemp) {
                    sendWebNotification(NOTIFY_WARNING, "Температура колонны превысила температуру тела, переход к отбору тела");
                    
                    // Переходим к фазе отбора тела
                    if (rectParams.postHeadsStabilizationTime > 0) {
                        // Если задано время стабилизации после голов, переходим к ней
                        rectPhase = PHASE_POST_HEADS_STABILIZATION;
                        stabilizationStartTime = millis();
                        
                        // Выключаем насос и клапан
                        disablePump();
                        disableValve();
                        
                        playSound(SOUND_PHASE_CHANGE);
                    } else {
                        // Иначе сразу к отбору тела
                        rectPhase = PHASE_BODY;
                        
                        // Устанавливаем мощность для отбора тела
                        if (sysSettings.powerControlMode == POWER_CONTROL_MANUAL) {
                            setPowerPercent(rectParams.bodyPower);
                        } else {
                            setPowerWatts(rectParams.bodyPowerWatts);
                        }
                        
                        // Включаем насос на скорости отбора тела
                        enablePump(pumpSettings.bodyFlowRate);
                        
                        // Устанавливаем параметры орошения
                        setRefluxRatio(rectParams.refluxRatio, rectParams.refluxPeriod);
                        
                        playSound(SOUND_PHASE_CHANGE);
                    }
                }
            } 
            else { // Для альтернативной модели
                // Проверяем время отбора голов
                if (millis() - headsStartTime >= (unsigned long)rectParams.headsTargetTimeMinutes * 60 * 1000) {
                    // Переходим к фазе стабилизации после голов
                    rectPhase = PHASE_POST_HEADS_STABILIZATION;
                    stabilizationStartTime = millis();
                    
                    // Выключаем насос и клапан
                    disablePump();
                    disableValve();
                    
                    sendWebNotification(NOTIFY_INFO, "Начало фазы стабилизации после отбора голов");
                    playSound(SOUND_PHASE_CHANGE);
                }
            }
            break;
            
        case PHASE_POST_HEADS_STABILIZATION:
            // Фаза стабилизации после голов
            if (millis() - stabilizationStartTime >= (unsigned long)rectParams.postHeadsStabilizationTime * 60 * 1000) {
                // Переходим к фазе отбора тела
                rectPhase = PHASE_BODY;
                
                // Устанавливаем мощность для отбора тела
                if (sysSettings.powerControlMode == POWER_CONTROL_MANUAL) {
                    setPowerPercent(rectParams.bodyPower);
                } else {
                    setPowerWatts(rectParams.bodyPowerWatts);
                }
                
                // Включаем насос на скорости отбора тела
                enablePump(pumpSettings.bodyFlowRate);
                
                // Устанавливаем параметры орошения
                setRefluxRatio(rectParams.refluxRatio, rectParams.refluxPeriod);
                
                sendWebNotification(NOTIFY_INFO, "Начало отбора тела");
                playSound(SOUND_PHASE_CHANGE);
            }
            break;
            
        case PHASE_BODY:
            // Фаза отбора тела
            if (rectParams.model == MODEL_CLASSIC) {
                // Для классической модели проверяем температуру колонны
                if (temperatures[TEMP_REFLUX] >= rectParams.tailsTemp) {
                    // Переходим к фазе отбора хвостов
                    rectPhase = PHASE_TAILS;
                    
                    // Устанавливаем мощность для отбора хвостов
                    if (sysSettings.powerControlMode == POWER_CONTROL_MANUAL) {
                        setPowerPercent(rectParams.tailsPower);
                    } else {
                        setPowerWatts(rectParams.tailsPowerWatts);
                    }
                    
                    // Включаем насос на скорости отбора хвостов
                    enablePump(pumpSettings.tailsFlowRate);
                    
                    // Отключаем орошение (клапан всегда открыт)
                    enableValve();
                    
                    sendWebNotification(NOTIFY_INFO, "Начало отбора хвостов");
                    playSound(SOUND_PHASE_CHANGE);
                }
                // Проверяем объем отобранного тела
                else if (bodyCollected >= rectParams.bodyVolume) {
                    // Переходим к завершению процесса, если достигнут целевой объем
                    rectPhase = PHASE_COMPLETED;
                    
                    // Выключаем нагрев, насос и клапан
                    disableHeater();
                    setPowerPercent(0);
                    disablePump();
                    disableValve();
                    
                    systemRunning = false;
                    
                    sendWebNotification(NOTIFY_SUCCESS, "Процесс ректификации завершен");
                    playSound(SOUND_PROCESS_COMPLETE);
                }
            } 
            else { // Для альтернативной модели
                // Проверяем скорость изменения температуры
                float tempRateOfChange = getTemperatureRateOfChange(TEMP_REFLUX, 60); // за минуту
                
                // Если температура колонны стабилизировалась
                if (!temperatureStabilized && abs(tempRateOfChange) < rectParams.tempDeltaEndBody) {
                    temperatureStabilized = true;
                    sendWebNotification(NOTIFY_INFO, "Температура колонны стабилизировалась");
                }
                
                // Если температура куба достигла предельной для тела или поднялась температура колонны
                if (temperatures[TEMP_CUBE] >= rectParams.tailsCubeTemp || 
                    temperatures[TEMP_REFLUX] >= rectParams.tailsTemp) {
                    
                    // Переходим к фазе отбора хвостов
                    rectPhase = PHASE_TAILS;
                    
                    // Устанавливаем мощность для отбора хвостов
                    if (sysSettings.powerControlMode == POWER_CONTROL_MANUAL) {
                        setPowerPercent(rectParams.tailsPower);
                    } else {
                        setPowerWatts(rectParams.tailsPowerWatts);
                    }
                    
                    // Включаем насос на скорости отбора хвостов
                    float tailsFlowRate = rectParams.useSameFlowRateForTails ? 
                                         rectParams.bodyFlowRateMlPerHour : 
                                         rectParams.tailsFlowRateMlPerHour;
                    enablePump(tailsFlowRate);
                    
                    // Отключаем орошение (клапан всегда открыт)
                    enableValve();
                    
                    sendWebNotification(NOTIFY_INFO, "Начало отбора хвостов");
                    playSound(SOUND_PHASE_CHANGE);
                }
            }
            break;
            
        case PHASE_TAILS:
            // Фаза отбора хвостов
            // Проверяем температуру колонны для завершения
            if (temperatures[TEMP_REFLUX] >= rectParams.endTemp) {
                // Переходим к завершению процесса
                rectPhase = PHASE_COMPLETED;
                
                // Выключаем нагрев, насос и клапан
                disableHeater();
                setPowerPercent(0);
                disablePump();
                disableValve();
                
                systemRunning = false;
                
                sendWebNotification(NOTIFY_SUCCESS, "Процесс ректификации завершен");
                playSound(SOUND_PROCESS_COMPLETE);
            }
            break;
            
        case PHASE_COMPLETED:
            // Процесс уже завершен, ничего не делаем
            break;
            
        default:
            break;
    }
}

// Обработка процесса дистилляции
void processDistillation() {
    // Проверка безопасности - максимальная температура куба
    if (temperatures[TEMP_CUBE] > distParams.maxCubeTemp) {
        sendWebNotification(NOTIFY_ERROR, "Превышена максимальная температура куба");
        emergencyHeaterShutdown("Превышена максимальная температура куба");
        return;
    }
    
    // Обработка в зависимости от текущей фазы
    switch (distPhase) {
        case DIST_PHASE_HEATING:
            // Фаза нагрева - ждем, пока температура продукта достигнет начальной температуры отбора
            if ((temperatures[TEMP_PRODUCT] >= distParams.startCollectingTemp) || 
                (temperatures[TEMP_REFLUX] >= distParams.startCollectingTemp && !isSensorConnected(TEMP_PRODUCT))) {
                
                // Переходим к фазе дистилляции
                distPhase = DIST_PHASE_DISTILLATION;
                
                // Устанавливаем мощность для дистилляции
                if (sysSettings.powerControlMode == POWER_CONTROL_MANUAL) {
                    setPowerPercent(distParams.distillationPower);
                } else {
                    setPowerWatts(distParams.distillationPowerWatts);
                }
                
                // Открываем клапан отбора
                enableValve();
                
                // Если нужно отделять головы
                if (distParams.separateHeads) {
                    // Включаем насос на скорости отбора голов
                    enablePump(distParams.headsFlowRate);
                    
                    sendWebNotification(NOTIFY_INFO, "Начало отбора голов");
                    // Запоминаем время начала отбора голов
                    headsStartTime = millis();
                    inHeadsPhase = true;
                } else {
                    // Включаем насос на обычной скорости отбора
                    enablePump(distParams.flowRate);
                    inHeadsPhase = false;
                    
                    sendWebNotification(NOTIFY_INFO, "Начало дистилляции");
                }
                
                playSound(SOUND_PHASE_CHANGE);
            }
            break;
            
        case DIST_PHASE_DISTILLATION:
            // Если сейчас идет отбор голов и нужно переключиться на отбор тела
            if (inHeadsPhase && distillationCollected >= distParams.headsVolume) {
                // Переключаемся на отбор тела
                enablePump(distParams.flowRate);
                inHeadsPhase = false;
                
                sendWebNotification(NOTIFY_INFO, "Отбор голов завершен, переход к основному отбору");
                playSound(SOUND_PHASE_CHANGE);
            }
            
            // Проверяем температуру куба для завершения
            if (temperatures[TEMP_CUBE] >= distParams.endTemp) {
                // Переходим к завершению процесса
                distPhase = DIST_PHASE_COMPLETED;
                
                // Выключаем нагрев, насос и клапан
                disableHeater();
                setPowerPercent(0);
                disablePump();
                disableValve();
                
                systemRunning = false;
                
                sendWebNotification(NOTIFY_SUCCESS, "Процесс дистилляции завершен");
                playSound(SOUND_PROCESS_COMPLETE);
            }
            break;
            
        case DIST_PHASE_COMPLETED:
            // Процесс уже завершен, ничего не делаем
            break;
            
        default:
            break;
    }
}