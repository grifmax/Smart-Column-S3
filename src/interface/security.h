/**
 * Smart-Column S3 - Security
 *
 * Аутентификация и защита веб-интерфейса
 */

#ifndef SECURITY_H
#define SECURITY_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

namespace Security {
    /**
     * Инициализация security модуля
     * @param username Имя пользователя (по умолчанию "admin")
     * @param password Пароль
     */
    void init(const char* username = "admin", const char* password = nullptr);

    /**
     * Проверка Basic Authentication
     * @param request HTTP запрос
     * @return true если авторизован
     */
    bool checkAuth(AsyncWebServerRequest *request);

    /**
     * Отправка 401 Unauthorized
     * @param request HTTP запрос
     */
    void requestAuth(AsyncWebServerRequest *request);

    /**
     * Проверка rate limit
     * @param ip IP адрес клиента
     * @return true если можно обработать запрос
     */
    bool checkRateLimit(IPAddress ip);

    /**
     * Добавление security заголовков к ответу
     * @param response HTTP ответ
     */
    void addSecurityHeaders(AsyncWebServerResponse *response);

    /**
     * Установка пароля
     * @param password Новый пароль
     */
    void setPassword(const char* password);

    /**
     * Включение/отключение аутентификации
     * @param enabled Состояние
     */
    void setAuthEnabled(bool enabled);

    /**
     * Проверка включена ли аутентификация
     * @return true если включена
     */
    bool isAuthEnabled();
}

#endif // SECURITY_H
