#include "heater.h"
#include "power_control.h"
#include "utils.h"
#include "temp_sensors.h"

// Статус нагревателя
static bool heaterEnabled = false;

// Текущая мощность (0-100%)
static int currentPower = 0;

// Время последнего обновления состояния
static unsigned long lastUpdate = 0;

// Инициализация нагревателя
void initHeater() {
    Serial.println("Инициализация управления нагревателем...");
    
    // Настройка пина
    pinMode(PIN_HEATER, OUTPUT);
    disableHeater(); // Для безопасности
    
    Serial.println("Управление нагревателем инициализировано");
}

// Включение нагревателя
void enableHeater() {
    if (!heaterEnabled) {
        digitalWrite(PIN_HEATER, HIGH);
        heaterEnabled = true;
        Serial.println("Нагреватель включен");
    }
}

// Выключение нагревателя
void disableHeater() {
    if (heaterEnabled) {
        digitalWrite(PIN_HEATER, LOW);
        heaterEnabled = false;
        currentPower = 0;
        Serial.println("Нагреватель выключен");
    }
}

// Установка мощности нагрева в процентах (0-100%)
void setHeaterPower(int powerPercent) {
    // Ограничиваем значение в диапазоне 0-100%
    powerPercent = constrain(powerPercent, 0, 100);
    
    if (powerPercent == 0) {
        disableHeater();
    } else {
        enableHeater();
        currentPower = powerPercent;
    }
    
    // Устанавливаем мощность через модуль управления мощностью
    setPowerPercent(powerPercent);
}

// Установка мощности нагрева в ваттах
void setHeaterPowerWatts(int powerWatts) {
    // Ограничиваем значение максимальной мощностью
    powerWatts = constrain(powerWatts, 0, sysSettings.maxHeaterPowerWatts);
    
    // Переводим в проценты
    int powerPercent = (int)((float)powerWatts * 100.0 / sysSettings.maxHeaterPowerWatts);
    
    // Устанавливаем мощность
    setHeaterPower(powerPercent);
}

// Получение текущей мощности нагрева в процентах
int getHeaterPowerPercent() {
    return currentPower;
}

// Получение текущей мощности нагрева в ваттах
int getHeaterPowerWatts() {
    return (int)((float)currentPower * sysSettings.maxHeaterPowerWatts / 100.0);
}

// Обновление состояния нагревателя
void updateHeater() {
    unsigned long currentTime = millis();
    
    // Обновляем состояние каждые 100 мс
    if (currentTime - lastUpdate < 100) {
        return;
    }
    
    lastUpdate = currentTime;
    
    // Проверка безопасности - если температура куба превышает максимальную
    if (systemRunning) {
        float maxCubeTemp = 0;
        
        if (currentMode == MODE_RECTIFICATION) {
            maxCubeTemp = rectParams.maxCubeTemp;
        } else if (currentMode == MODE_DISTILLATION) {
            maxCubeTemp = distParams.maxCubeTemp;
        }
        
        if (maxCubeTemp > 0 && temperatures[TEMP_CUBE] > maxCubeTemp) {
            emergencyHeaterShutdown("Превышена максимальная температура куба");
            return;
        }
    }
    
    // Обновляем управление мощностью
    updatePowerControl();
}

// Проверка, включен ли нагреватель
bool isHeaterEnabled() {
    return heaterEnabled;
}

// Аварийное выключение нагревателя
void emergencyHeaterShutdown(const char* reason) {
    disableHeater();
    setPowerPercent(0);
    
    // Останавливаем процесс
    if (systemRunning) {
        systemRunning = false;
        systemPaused = false;
        
        // Выключаем насос и клапан
        disablePump();
        disableValve();
    }
    
    // Отправляем уведомление
    String message = "Аварийное отключение: ";
    message += reason;
    sendWebNotification(NOTIFY_ERROR, message);
    
    // Воспроизводим сигнал тревоги
    playSound(SOUND_ALARM);
    
    Serial.println(message);
}