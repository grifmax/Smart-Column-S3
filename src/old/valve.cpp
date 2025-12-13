#include "valve.h"
#include "utils.h"

// Статус клапана
static bool valveOpen = false;

// Параметры орошения
static float refluxRatio = 5.0;      // Соотношение орошения (например, 5 означает 5/1)
static int refluxPeriod = 60;        // Период цикла орошения в секундах

// Время последнего переключения клапана
static unsigned long lastValveSwitchTime = 0;

// Текущая фаза цикла орошения (0 - закрыто/орошение, 1 - отбор)
static int refluxPhase = 0;

// Инициализация клапана
void initValve() {
    Serial.println("Инициализация управления клапаном...");
    
    // Настройка пина
    pinMode(PIN_VALVE, OUTPUT);
    disableValve(); // Для безопасности закрываем клапан
    
    // Загружаем параметры орошения из настроек ректификации
    refluxRatio = rectParams.refluxRatio;
    refluxPeriod = rectParams.refluxPeriod;
    
    Serial.println("Управление клапаном инициализировано");
}

// Включение клапана (открытие)
void enableValve() {
    digitalWrite(PIN_VALVE, HIGH);
    valveOpen = true;
}

// Выключение клапана (закрытие)
void disableValve() {
    digitalWrite(PIN_VALVE, LOW);
    valveOpen = false;
}

// Обновление состояния клапана в соответствии с режимом орошения
void updateValve() {
    // Проверяем, в режиме ли мы ректификации в фазе отбора тела
    if (systemRunning && !systemPaused && 
        currentMode == MODE_RECTIFICATION && rectPhase == PHASE_BODY) {
        
        unsigned long currentTime = millis();
        
        // Если орошение не требуется (refluxRatio <= 0), держим клапан открытым
        if (refluxRatio <= 0) {
            enableValve();
            return;
        }
        
        // Рассчитываем длительность фаз
        float totalCycle = refluxPeriod;
        float refluxingTime = totalCycle * refluxRatio / (refluxRatio + 1);
        float collectingTime = totalCycle - refluxingTime;
        
        // Если еще не было переключений или пришло время цикла
        if (lastValveSwitchTime == 0 || 
            currentTime - lastValveSwitchTime >= ((refluxPhase == 0) ? refluxingTime : collectingTime) * 1000) {
            
            // Переключаем фазу
            refluxPhase = 1 - refluxPhase;
            lastValveSwitchTime = currentTime;
            
            // Устанавливаем состояние клапана в зависимости от фазы
            if (refluxPhase == 0) { // Фаза орошения
                disableValve(); // Закрываем клапан, чтобы жидкость возвращалась в колонну
                Serial.println("Фаза орошения - клапан закрыт");
            } else { // Фаза отбора
                enableValve(); // Открываем клапан для отбора
                Serial.println("Фаза отбора - клапан открыт");
            }
        }
    }
    // В других режимах или фазах просто держим клапан в нужном состоянии
    else if (systemRunning && !systemPaused) {
        // В фазе голов, хвостов или при дистилляции клапан открыт
        if ((currentMode == MODE_RECTIFICATION && 
             (rectPhase == PHASE_HEADS || rectPhase == PHASE_TAILS)) ||
            currentMode == MODE_DISTILLATION) {
            enableValve();
        }
        // В других фазах ректификации клапан закрыт
        else if (currentMode == MODE_RECTIFICATION) {
            disableValve();
        }
    }
    // Если система на паузе или остановлена, клапан закрыт
    else {
        disableValve();
    }
}

// Установка режима орошения (соотношение и период)
void setRefluxRatio(float ratio, int periodSeconds) {
    refluxRatio = ratio;
    refluxPeriod = periodSeconds;
    
    // Сбрасываем текущую фазу и время переключения
    refluxPhase = 0;
    lastValveSwitchTime = 0;
    
    // Обновляем параметры в настройках ректификации
    rectParams.refluxRatio = ratio;
    rectParams.refluxPeriod = periodSeconds;
    
    // Сохраняем настройки
    saveRectificationParams();
    
    Serial.print("Установлен режим орошения: соотношение ");
    Serial.print(ratio);
    Serial.print(", период ");
    Serial.print(periodSeconds);
    Serial.println(" с");
}

// Получение текущего состояния клапана (открыт/закрыт)
bool isValveOpen() {
    return valveOpen;
}

// Получение текущего режима орошения
float getRefluxRatio() {
    return refluxRatio;
}

// Получение текущего периода орошения
int getRefluxPeriod() {
    return refluxPeriod;
}