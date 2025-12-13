/**
 * @file watchdog.cpp
 * @brief Реализация Watchdog Timer для защиты от зависаний системы
 */

#include "watchdog.h"
#include <esp_task_wdt.h>

// Состояние Watchdog
static bool watchdogInitialized = false;
static bool watchdogEnabled = false;
static uint32_t watchdogTimeout = 30;
static unsigned long lastFeedTime = 0;

// Инициализация Watchdog Timer
void initWatchdog(uint32_t timeoutSeconds) {
    if (watchdogInitialized) {
        Serial.println("Watchdog уже инициализирован");
        return;
    }
    
    Serial.println("Инициализация Watchdog Timer...");
    Serial.println("Таймаут: " + String(timeoutSeconds) + " секунд");
    
    watchdogTimeout = timeoutSeconds;
    
    // Конфигурация Watchdog
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = timeoutSeconds * 1000,
        .idle_core_mask = 0,  // Не отслеживать idle задачи
        .trigger_panic = true  // Вызывать panic при срабатывании
    };
    
    // Инициализация Task Watchdog
    esp_err_t err = esp_task_wdt_init(&wdt_config);
    if (err != ESP_OK) {
        Serial.println("ОШИБКА инициализации Watchdog: " + String(err));
        return;
    }
    
    // Добавляем текущую задачу в мониторинг
    err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        Serial.println("ОШИБКА добавления задачи в Watchdog: " + String(err));
        esp_task_wdt_deinit();
        return;
    }
    
    watchdogInitialized = true;
    watchdogEnabled = true;
    lastFeedTime = millis();
    
    Serial.println("Watchdog Timer успешно инициализирован");
}

// Сброс (подкормка) Watchdog Timer
void feedWatchdog() {
    if (!watchdogInitialized || !watchdogEnabled) {
        return;
    }
    
    esp_err_t err = esp_task_wdt_reset();
    if (err != ESP_OK) {
        Serial.println("ОШИБКА сброса Watchdog: " + String(err));
    }
    
    lastFeedTime = millis();
}

// Включение Watchdog Timer
void enableWatchdog() {
    if (!watchdogInitialized) {
        Serial.println("ОШИБКА: Watchdog не инициализирован");
        return;
    }
    
    if (watchdogEnabled) {
        Serial.println("Watchdog уже включен");
        return;
    }
    
    // Добавляем текущую задачу обратно в мониторинг
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        Serial.println("ОШИБКА включения Watchdog: " + String(err));
        return;
    }
    
    watchdogEnabled = true;
    lastFeedTime = millis();
    Serial.println("Watchdog включен");
}

// Выключение Watchdog Timer
void disableWatchdog() {
    if (!watchdogInitialized) {
        Serial.println("ОШИБКА: Watchdog не инициализирован");
        return;
    }
    
    if (!watchdogEnabled) {
        Serial.println("Watchdog уже выключен");
        return;
    }
    
    // Удаляем текущую задачу из мониторинга
    esp_err_t err = esp_task_wdt_delete(NULL);
    if (err != ESP_OK) {
        Serial.println("ОШИБКА выключения Watchdog: " + String(err));
        return;
    }
    
    watchdogEnabled = false;
    Serial.println("ВНИМАНИЕ: Watchdog выключен!");
}

// Проверка, включен ли Watchdog
bool isWatchdogEnabled() {
    return watchdogEnabled;
}

// Получение оставшегося времени до срабатывания
uint32_t getWatchdogTimeLeft() {
    if (!watchdogEnabled) {
        return 0;
    }
    
    unsigned long elapsed = (millis() - lastFeedTime) / 1000;
    
    if (elapsed >= watchdogTimeout) {
        return 0;
    }
    
    return (watchdogTimeout - elapsed) * 1000;
}
