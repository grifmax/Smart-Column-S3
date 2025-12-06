/**
 * @file web.h
 * @brief Заголовочный файл для веб-интерфейса системы
 */

#ifndef WEB_H
#define WEB_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>

/**
 * @brief Инициализация веб-сервера
 * 
 * Настраивает и запускает веб-сервер на порту 80
 */
void initWebServer();

/**
 * @brief Настройка API маршрутов
 * 
 * Настраивает обработчики API запросов
 */
void setupApiRoutes();

/**
 * @brief Настройка маршрутов для статических файлов
 * 
 * Настраивает обработчики для доступа к статическим файлам
 */
void setupStaticRoutes();

/**
 * @brief Обработчик событий WebSocket
 * 
 * @param server Сервер WebSocket
 * @param client Клиент WebSocket
 * @param type Тип события
 * @param arg Аргументы
 * @param data Данные
 * @param len Длина данных
 */
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                       AwsEventType type, void *arg, uint8_t *data, size_t len);

/**
 * @brief Обновление состояния WebSocket
 * 
 * Отправляет актуальные данные клиентам WebSocket
 */
void updateWebSocket();

/**
 * @brief Установка интервала обновления WebSocket
 * 
 * @param intervalMs Интервал обновления в миллисекундах
 */
void setWebSocketUpdateInterval(int intervalMs);

/**
 * @brief Установка обработчика сообщений от клиента
 * 
 * @param callback Функция обратного вызова для обработки сообщений
 */
void setWebSocketMessageHandler(void (*callback)(AsyncWebSocketClient*, String));

/**
 * @brief Отправка сообщения конкретному клиенту WebSocket
 * 
 * @param client Указатель на клиент WebSocket
 * @param message Сообщение для отправки
 */
void sendWebSocketMessage(AsyncWebSocketClient *client, const String &message);

/**
 * @brief Отправка сообщения всем клиентам WebSocket
 * 
 * @param message Сообщение для отправки
 */
void broadcastWebSocketMessage(const String &message);

/**
 * @brief Получение текущего количества подключенных клиентов
 * 
 * @return Количество подключенных клиентов WebSocket
 */
int getConnectedClientsCount();

/**
 * @brief Проверка активности WebSocket соединения
 * 
 * @return true если хотя бы один клиент подключен
 */
bool isWebSocketActive();

#endif // WEB_H
