/**
 * @file emergency.cpp
 * @brief Реализация системы аварийного восстановления
 */

#include "emergency.h"
#include "heater.h"
#include "pump.h"
#include "valve.h"
#include "config.h"
#include <LittleFS.h>

// Текущее состояние аварии
static EmergencyInfo currentEmergency;
static bool emergencyActive = false;

// История аварий
#define MAX_EMERGENCY_LOG 50
static String emergencyLog[MAX_EMERGENCY_LOG];
static int emergencyLogIndex = 0;
static int emergencyCount = 0;

// Инициализация системы аварийного восстановления
void initEmergency() {
    Serial.println("Инициализация системы аварийного восстановления...");
    
    currentEmergency.level = EMERGENCY_NONE;
    currentEmergency.reason = "";
    currentEmergency.timestamp = 0;
    currentEmergency.systemShutdown = false;
    
    emergencyActive = false;
    emergencyLogIndex = 0;
    emergencyCount = 0;
    
    // Очищаем лог
    for (int i = 0; i < MAX_EMERGENCY_LOG; i++) {
        emergencyLog[i] = "";
    }
    
    Serial.println("Система аварийного восстановления инициализирована");
}

// Срабатывание аварийной ситуации
void triggerEmergency(EmergencyLevel level, const String& reason) {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║   АВАРИЙНАЯ СИТУАЦИЯ ОБНАРУЖЕНА!      ║");
    Serial.println("╚════════════════════════════════════════╝");
    
    // Определяем уровень аварии
    String levelStr;
    switch (level) {
        case EMERGENCY_WARNING:
            levelStr = "ПРЕДУПРЕЖДЕНИЕ";
            break;
        case EMERGENCY_ERROR:
            levelStr = "ОШИБКА";
            break;
        case EMERGENCY_CRITICAL:
            levelStr = "КРИТИЧЕСКАЯ";
            break;
        default:
            levelStr = "НЕИЗВЕСТНО";
    }
    
    Serial.println("Уровень: " + levelStr);
    Serial.println("Причина: " + reason);
    Serial.println("Время: " + String(millis() / 1000) + " сек");
    
    // Обновляем текущую аварию
    currentEmergency.level = level;
    currentEmergency.reason = reason;
    currentEmergency.timestamp = millis();
    emergencyActive = true;
    emergencyCount++;
    
    // Логируем аварию
    String logMessage = String(millis() / 1000) + "s [" + levelStr + "] " + reason;
    logEmergency(logMessage);
    
    // Действия в зависимости от уровня
    if (level == EMERGENCY_CRITICAL || level == EMERGENCY_ERROR) {
        Serial.println("\n► Выполняется аварийное отключение...");
        shutdownAll();
        currentEmergency.systemShutdown = true;
        
        // Воспроизводим звук тревоги
        #ifdef PIN_BUZZER
        for (int i = 0; i < 3; i++) {
            tone(PIN_BUZZER, 2093, 150);
            delay(150);
            tone(PIN_BUZZER, 1047, 150);
            delay(200);
        }
        #endif
    } else if (level == EMERGENCY_WARNING) {
        Serial.println("\n► ПРЕДУПРЕЖДЕНИЕ: Обнаружена потенциальная проблема");
        
        // Воспроизводим предупреждающий звук
        #ifdef PIN_BUZZER
        tone(PIN_BUZZER, 1318, 200);
        delay(250);
        tone(PIN_BUZZER, 1318, 200);
        #endif
    }
    
    // Уведомляем пользователя
    notifyUserEmergency(reason);
    
    Serial.println("\n════════════════════════════════════════\n");
}

// Сброс аварийной ситуации
bool resetEmergency() {
    if (!emergencyActive) {
        Serial.println("Нет активной аварийной ситуации для сброса");
        return false;
    }
    
    if (!canResetEmergency()) {
        Serial.println("ОШИБКА: Невозможно сбросить аварию - причина не устранена");
        return false;
    }
    
    Serial.println("Сброс аварийной ситуации...");
    
    emergencyActive = false;
    currentEmergency.level = EMERGENCY_NONE;
    currentEmergency.reason = "";
    currentEmergency.systemShutdown = false;
    
    logEmergency("Аварийная ситуация сброшена");
    
    Serial.println("Аварийная ситуация успешно сброшена");
    
    return true;
}

// Проверка активности аварийной ситуации
bool isEmergencyActive() {
    return emergencyActive;
}

// Получение информации о текущей аварии
EmergencyInfo getEmergencyInfo() {
    return currentEmergency;
}

// Полное отключение всех систем
void shutdownAll() {
    Serial.println("=== АВАРИЙНОЕ ОТКЛЮЧЕНИЕ ВСЕХ СИСТЕМ ===");
    
    // 1. Выключаем нагреватель
    Serial.print("• Отключение нагревателя... ");
    heaterOff();
    Serial.println("OK");
    
    // 2. Останавливаем насос
    Serial.print("• Остановка насоса... ");
    pumpStop();
    Serial.println("OK");
    
    // 3. Закрываем клапан
    Serial.print("• Закрытие клапана... ");
    valveClose();
    Serial.println("OK");
    
    // 4. Логируем событие
    logEmergency("Выполнено полное отключение системы");
    
    Serial.println("========================================");
}

// Логирование аварийной ситуации
void logEmergency(const String& message) {
    // Добавляем в кольцевой буфер
    emergencyLog[emergencyLogIndex] = message;
    emergencyLogIndex = (emergencyLogIndex + 1) % MAX_EMERGENCY_LOG;
    
    // Выводим в Serial
    Serial.println("[EMERGENCY] " + message);
    
    // Сохраняем в файл (если файловая система доступна)
    if (LittleFS.begin(false)) {
        File logFile = LittleFS.open("/emergency.log", "a");
        if (logFile) {
            logFile.println(message);
            logFile.close();
        }
    }
}

// Уведомление пользователя об аварии
void notifyUserEmergency(const String& message) {
    // Здесь можно добавить отправку через веб-сокет, email, телеграм и т.д.
    Serial.println("📢 УВЕДОМЛЕНИЕ ПОЛЬЗОВАТЕЛЯ: " + message);
    
    // TODO: Интеграция с системой уведомлений
    // sendWebSocketNotification("emergency", message);
    // sendEmailNotification(message);
}

// Получение времени с момента аварии
unsigned long getTimeSinceEmergency() {
    if (!emergencyActive || currentEmergency.timestamp == 0) {
        return 0;
    }
    return (millis() - currentEmergency.timestamp) / 1000;
}

// Проверка возможности сброса аварии
bool canResetEmergency() {
    if (!emergencyActive) {
        return false;
    }
    
    // Проверяем, что прошло достаточно времени
    if (getTimeSinceEmergency() < 30) {
        Serial.println("Подождите хотя бы 30 секунд перед сбросом");
        return false;
    }
    
    // Проверяем, что система в безопасном состоянии
    // (это требует интеграции с модулем safety)
    // if (!checkSafetyConditions()) {
    //     return false;
    // }
    
    // Для критических аварий требуется ручное подтверждение
    if (currentEmergency.level == EMERGENCY_CRITICAL) {
        Serial.println("Критическая авария требует тщательной проверки системы");
        // Можно добавить дополнительные проверки
    }
    
    return true;
}
