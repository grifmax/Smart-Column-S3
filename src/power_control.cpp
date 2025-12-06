#include "power_control.h"
#include "temp_sensors.h"
#include "utils.h"
#include <PZEM004Tv30.h>

// Глобальные переменные для управления мощностью
static int currentPowerPercent = 0;    // Текущая мощность в процентах (0-100%)
static unsigned long lastPowerUpdate = 0;  // Время последнего обновления
static unsigned long lastSsrUpdate = 0;    // Время последнего переключения SSR

// Переменные для PI-регулятора
static float pidTargetTemp = 0.0;      // Целевая температура
static int pidSensorIndex = TEMP_REFLUX; // Индекс датчика для регулирования
static float pidIntegral = 0.0;        // Интегральная составляющая
static float pidLastError = 0.0;       // Предыдущая ошибка
static unsigned long pidLastUpdateTime = 0; // Время последнего обновления регулятора

// PZEM-004T (если используется)
#ifdef PZEM_RX_PIN
    HardwareSerial PzemSerial(1);  // Используем UART1 для ESP32
    PZEM004Tv30 pzem(PzemSerial, PZEM_RX_PIN, PZEM_TX_PIN);
    static bool pzemInitialized = false;
#endif

// Инициализация управления мощностью
void initPowerControl() {
    Serial.println("Инициализация управления мощностью...");
    
    // Настраиваем пин для симисторного регулятора, если он определен
    #ifdef PIN_HEATER
        pinMode(PIN_HEATER, OUTPUT);
        digitalWrite(PIN_HEATER, LOW); // Для безопасности выключаем
    #endif
    
    // Инициализация PZEM-004T, если включен
    #ifdef PZEM_RX_PIN
        if (sysSettings.pzemEnabled) {
            PzemSerial.begin(9600);
            pzemInitialized = true;
            
            // Попытка чтения значений для проверки соединения
            float voltage = pzem.voltage();
            if (voltage > 0) {
                Serial.println("PZEM-004T успешно подключен");
            } else {
                Serial.println("PZEM-004T не отвечает");
            }
        }
    #endif
    
    // Устанавливаем начальные значения
    currentPowerPercent = 0;
    
    Serial.println("Управление мощностью инициализировано");
}

// Установка мощности в процентах (0-100%)
void setPowerPercent(int percent) {
    // Ограничиваем значение в диапазоне 0-100%
    percent = constrain(percent, 0, 100);
    
    currentPowerPercent = percent;
}

// Установка мощности в ваттах
void setPowerWatts(int watts) {
    // Ограничиваем значение максимальной мощностью
    watts = constrain(watts, 0, sysSettings.maxHeaterPowerWatts);
    
    // Переводим в проценты
    int percent = (int)((float)watts * 100.0 / sysSettings.maxHeaterPowerWatts);
    
    // Устанавливаем мощность
    setPowerPercent(percent);
}

// Получение текущей мощности в процентах
int getCurrentPowerPercent() {
    return currentPowerPercent;
}

// Получение текущей мощности в ваттах
int getCurrentPowerWatts() {
    return (int)((float)currentPowerPercent * sysSettings.maxHeaterPowerWatts / 100.0);
}

// Обновление управления мощностью
void updatePowerControl() {
    unsigned long currentTime = millis();
    
    // Обрабатываем управление мощностью в зависимости от выбранного режима
    if (systemRunning && !systemPaused) {
        switch (sysSettings.powerControlMode) {
            case POWER_CONTROL_MANUAL:
                // В ручном режиме мощность устанавливается напрямую
                break;
                
            case POWER_CONTROL_PI:
                // Обновляем PI-регулятор с заданной периодичностью
                if (currentTime - pidLastUpdateTime >= 1000) { // Каждую секунду
                    updatePIControl();
                    pidLastUpdateTime = currentTime;
                }
                break;
                
            case POWER_CONTROL_PZEM:
                // Обновляем мощность на основе показаний PZEM с заданной периодичностью
                #ifdef PZEM_RX_PIN
                    if (sysSettings.pzemEnabled && pzemInitialized && 
                        currentTime - lastPowerUpdate >= POWER_CONTROL_INTERVAL) {
                        
                        float pzemPower = pzem.power();
                        int targetPower = 0;
                        
                        // Определяем целевую мощность в зависимости от режима
                        if (currentMode == MODE_RECTIFICATION) {
                            switch (rectPhase) {
                                case PHASE_HEATING:
                                    targetPower = rectParams.heatingPowerWatts;
                                    break;
                                case PHASE_STABILIZATION:
                                    targetPower = rectParams.stabilizationPowerWatts;
                                    break;
                                case PHASE_HEADS:
                                case PHASE_POST_HEADS_STABILIZATION:
                                    targetPower = rectParams.stabilizationPowerWatts;
                                    break;
                                case PHASE_BODY:
                                    targetPower = rectParams.bodyPowerWatts;
                                    break;
                                case PHASE_TAILS:
                                    targetPower = rectParams.tailsPowerWatts;
                                    break;
                                default:
                                    break;
                            }
                        } 
                        else if (currentMode == MODE_DISTILLATION) {
                            switch (distPhase) {
                                case DIST_PHASE_HEATING:
                                    targetPower = distParams.heatingPowerWatts;
                                    break;
                                case DIST_PHASE_DISTILLATION:
                                    targetPower = distParams.distillationPowerWatts;
                                    break;
                                default:
                                    break;
                            }
                        }
                        
                        // Если текущая мощность отличается от целевой больше чем на 5%
                        if (abs(pzemPower - targetPower) > (targetPower * 0.05)) {
                            // Корректируем мощность
                            float adjustment = 0;
                            
                            if (pzemPower < targetPower) {
                                adjustment = 5.0; // Увеличиваем на 5%
                            } else {
                                adjustment = -5.0; // Уменьшаем на 5%
                            }
                            
                            // Применяем корректировку
                            setPowerPercent(currentPowerPercent + (int)adjustment);
                        }
                        
                        lastPowerUpdate = currentTime;
                    }
                #endif
                break;
        }
    }
    
    // Обновляем регулирование мощности с помощью SSR
    // Используем метод фазового регулирования с помощью таймера
    
    // Период управления SSR (обычно 100 мс = 10 Гц)
    const unsigned long ssrPeriod = 100;
    
    // Если прошел период SSR, начинаем новый цикл
    if (currentTime - lastSsrUpdate >= ssrPeriod) {
        lastSsrUpdate = currentTime;
        
        // Включаем SSR, если мощность > 0
        if (currentPowerPercent > 0) {
            digitalWrite(PIN_HEATER, HIGH);
        }
    }
    
    // Рассчитываем длительность включения
    unsigned long onDuration = (ssrPeriod * currentPowerPercent) / 100;
    
    // Если пришло время выключить SSR
    if (currentTime - lastSsrUpdate >= onDuration) {
        digitalWrite(PIN_HEATER, LOW);
    }
}

// Установка режима управления мощностью
void setPowerControlMode(PowerControlMode mode) {
    sysSettings.powerControlMode = mode;
    
    // Сбрасываем переменные PI-регулятора при переключении режимов
    if (mode == POWER_CONTROL_PI) {
        pidIntegral = 0.0;
        pidLastError = 0.0;
        pidLastUpdateTime = 0;
    }
    
    // Сохраняем настройки
    saveSystemSettings();
    
    Serial.print("Установлен режим управления мощностью: ");
    switch (mode) {
        case POWER_CONTROL_MANUAL:
            Serial.println("Ручной");
            break;
        case POWER_CONTROL_PI:
            Serial.println("PI-регулятор");
            break;
        case POWER_CONTROL_PZEM:
            Serial.println("По PZEM");
            break;
    }
}

// Получение текущего режима управления мощностью
PowerControlMode getPowerControlMode() {
    return sysSettings.powerControlMode;
}

// Настройка PI-регулятора
void configurePIController(float kp, float ki, float outputMin, float outputMax) {
    sysSettings.piSettings.kp = kp;
    sysSettings.piSettings.ki = ki;
    sysSettings.piSettings.outputMin = outputMin;
    sysSettings.piSettings.outputMax = outputMax;
    
    // Сбрасываем переменные регулятора
    pidIntegral = 0.0;
    pidLastError = 0.0;
    
    // Сохраняем настройки
    saveSystemSettings();
    
    Serial.println("Настройки PI-регулятора обновлены");
}

// Установка целевой температуры для PI-регулятора
void setPITargetTemperature(float targetTemp, int sensorIndex) {
    pidTargetTemp = targetTemp;
    pidSensorIndex = sensorIndex;
    
    // Сбрасываем переменные регулятора при изменении целевой температуры
    pidIntegral = 0.0;
    pidLastError = 0.0;
    
    Serial.print("Установлена целевая температура для PI-регулятора: ");
    Serial.print(targetTemp);
    Serial.println(" °C");
}

// Обновление PI-регулятора
void updatePIControl() {
    // Проверяем, что датчик подключен
    if (!isSensorConnected(pidSensorIndex)) {
        Serial.println("Ошибка PI-регулятора: датчик не подключен");
        return;
    }
    
    // Получаем текущую температуру
    float currentTemp = temperatures[pidSensorIndex];
    
    // Вычисляем ошибку
    float error = pidTargetTemp - currentTemp;
    
    // Пропорциональная составляющая
    float p = sysSettings.piSettings.kp * error;
    
    // Интегральная составляющая (с ограничением)
    pidIntegral += error;
    pidIntegral = constrain(pidIntegral, -sysSettings.piSettings.integralLimit, sysSettings.piSettings.integralLimit);
    float i = sysSettings.piSettings.ki * pidIntegral;
    
    // Вычисляем выходной сигнал
    float output = p + i;
    
    // Ограничиваем выходной сигнал
    output = constrain(output, sysSettings.piSettings.outputMin, sysSettings.piSettings.outputMax);
    
    // Применяем выходной сигнал
    setPowerPercent((int)output);
    
    // Запоминаем текущую ошибку
    pidLastError = error;
    
    // Отладочная информация
    Serial.print("PI: цель=");
    Serial.print(pidTargetTemp);
    Serial.print("°C, текущая=");
    Serial.print(currentTemp);
    Serial.print("°C, ошибка=");
    Serial.print(error);
    Serial.print(", P=");
    Serial.print(p);
    Serial.print(", I=");
    Serial.print(i);
    Serial.print(", выход=");
    Serial.println(output);
}

// Получение текущей мощности от PZEM-004T (если подключен)
float getPzemPowerWatts() {
    #ifdef PZEM_RX_PIN
        if (sysSettings.pzemEnabled && pzemInitialized) {
            return pzem.power();
        }
    #endif
    return 0.0;
}

// Получение текущего напряжения от PZEM-004T (если подключен)
float getPzemVoltage() {
    #ifdef PZEM_RX_PIN
        if (sysSettings.pzemEnabled && pzemInitialized) {
            return pzem.voltage();
        }
    #endif
    return 0.0;
}

// Получение текущего тока от PZEM-004T (если подключен)
float getPzemCurrent() {
    #ifdef PZEM_RX_PIN
        if (sysSettings.pzemEnabled && pzemInitialized) {
            return pzem.current();
        }
    #endif
    return 0.0;
}

// Получение текущей энергии от PZEM-004T (если подключен)
float getPzemEnergy() {
    #ifdef PZEM_RX_PIN
        if (sysSettings.pzemEnabled && pzemInitialized) {
            return pzem.energy();
        }
    #endif
    return 0.0;
}

// Сброс счетчика энергии PZEM-004T
void resetPzemEnergy() {
    #ifdef PZEM_RX_PIN
        if (sysSettings.pzemEnabled && pzemInitialized) {
            pzem.resetEnergy();
            Serial.println("Счетчик энергии PZEM-004T сброшен");
        }
    #endif
}