/**
 * Smart-Column S3 - Веб-сервер
 * 
 * AsyncWebServer + WebSocket для Web UI и REST API
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include "../config.h"
#include "../types.h"

namespace WebServer {
    /**
     * Инициализация сервера
     */
    void init();
    
    /**
     * Отправка состояния всем клиентам WebSocket
     * @param state Состояние системы
     */
    void broadcastState(const SystemState& state);
    
    /**
     * Отправка события
     * @param event Тип события
     * @param message Сообщение
     */
    void broadcastEvent(const char* event, const char* message);
    
    /**
     * Отправка аварии
     * @param alarm Данные аварии
     */
    void broadcastAlarm(const Alarm& alarm);
    
    /**
     * Получение количества подключённых клиентов
     * @return Число клиентов
     */
    uint8_t getClientCount();
    
    // =========================================================================
    // API ENDPOINTS (регистрируются автоматически в init)
    // =========================================================================
    
    // GET
    // /api/status      - полный статус
    // /api/sensors     - показания датчиков
    // /api/log         - последние записи лога
    // /api/settings    - текущие настройки
    // /api/calibration - данные калибровки
    // /api/system/info - информация о системе
    // /api/export      - экспорт лога в CSV
    
    // POST
    // /api/mode/start  - запуск режима (params: mode, ...)
    // /api/mode/stop   - остановка
    // /api/mode/pause  - пауза
    // /api/mode/next   - следующая фракция
    // /api/pump/speed  - скорость насоса (params: speed)
    // /api/heater/power- мощность ТЭНа (params: power)
    // /api/settings    - сохранение настроек
    // /api/calibration - сохранение калибровки
    // /api/system/reboot - перезагрузка
    // /api/system/reset  - сброс настроек
}

#endif // WEBSERVER_H
