/**
 * @file rectification.h
 * @brief Интерфейс управления процессом ректификации
 */

#ifndef RECTIFICATION_H
#define RECTIFICATION_H

#include <Arduino.h>

// Фазы процесса ректификации
enum RectificationPhase {
    RECT_PHASE_IDLE,           // Ожидание
    RECT_PHASE_HEATING,        // Разгон (нагрев)
    RECT_PHASE_STABILIZATION,  // Стабилизация колонны
    RECT_PHASE_HEADS,          // Отбор голов
    RECT_PHASE_STABILIZATION2, // Стабилизация после голов
    RECT_PHASE_BODY,           // Отбор тела
    RECT_PHASE_TAILS,          // Отбор хвостов
    RECT_PHASE_FINISHING       // Завершение процесса
};

/**
 * @brief Инициализация модуля ректификации
 * 
 * @return true если инициализация прошла успешно
 */
bool initRectification();

/**
 * @brief Запуск процесса ректификации
 * 
 * @return true если процесс успешно запущен
 */
bool startRectification();

/**
 * @brief Остановка процесса ректификации
 */
void stopRectification();

/**
 * @brief Пауза процесса ректификации
 */
void pauseRectification();

/**
 * @brief Возобновление процесса ректификации
 */
void resumeRectification();

/**
 * @brief Проверка запущенного процесса ректификации
 * 
 * @return true если процесс ректификации запущен
 */
bool isRectificationRunning();

/**
 * @brief Проверка состояния паузы процесса ректификации
 * 
 * @return true если процесс ректификации на паузе
 */
bool isRectificationPaused();

/**
 * @brief Получение текущей фазы ректификации
 * 
 * @return Текущая фаза ректификации
 */
RectificationPhase getRectificationPhase();

/**
 * @brief Получение имени текущей фазы ректификации
 * 
 * @return Строковое представление текущей фазы
 */
String getRectificationPhaseName();

/**
 * @brief Обработка процесса ректификации
 * 
 * Вызывается в основном цикле для обработки текущей фазы ректификации
 */
void processRectification();

/**
 * @brief Получение времени работы процесса ректификации
 * 
 * @return Время работы в секундах
 */
unsigned long getRectificationUptime();

/**
 * @brief Получение времени текущей фазы ректификации
 * 
 * @return Время текущей фазы в секундах
 */
unsigned long getRectificationPhaseTime();

/**
 * @brief Получение объема собранных голов
 * 
 * @return Объем голов в миллилитрах
 */
int getRectificationHeadsVolume();

/**
 * @brief Получение объема собранного тела
 * 
 * @return Объем тела в миллилитрах
 */
int getRectificationBodyVolume();

/**
 * @brief Получение объема собранных хвостов
 * 
 * @return Объем хвостов в миллилитрах
 */
int getRectificationTailsVolume();

/**
 * @brief Получение общего объема собранного продукта
 * 
 * @return Общий объем в миллилитрах
 */
int getRectificationTotalVolume();

/**
 * @brief Установка фазы ректификации
 * 
 * @param phase Новая фаза ректификации
 */
void setRectificationPhase(RectificationPhase phase);

/**
 * @brief Переход к следующей фазе ректификации
 */
void moveToNextRectificationPhase();

/**
 * @brief Ручное переключение в фазу отбора голов
 * 
 * @return true если переключение успешно
 */
bool startHeadsCollection();

/**
 * @brief Ручное переключение в фазу отбора тела
 * 
 * @return true если переключение успешно
 */
bool startBodyCollection();

/**
 * @brief Ручное переключение в фазу отбора хвостов
 * 
 * @return true если переключение успешно
 */
bool startTailsCollection();

/**
 * @brief Получение состояния орошения
 * 
 * @return true если орошение включено, false если отбор
 */
bool getRectificationRefluxStatus();

/**
 * @brief Проверка безопасности процесса ректификации
 * 
 * @return true если все параметры в безопасных пределах
 */
bool checkRectificationSafety();

/**
 * @brief Расчет текущей скорости отбора
 * 
 * @return Текущая скорость отбора в мл/мин
 */
float getRectificationCurrentFlowRate();

/**
 * @brief Получение предполагаемого оставшегося времени отбора тела
 * 
 * @return Предполагаемое время в секундах
 */
unsigned long getRectificationBodyTimeLeft();

/**
 * @brief Установка нового целевого объема тела
 * 
 * @param newVolume Новый целевой объем в миллилитрах
 */
void setRectificationBodyTargetVolume(int newVolume);

/**
 * @brief Получение текущего соотношения орошения
 * 
 * @return Текущее соотношение R/D
 */
float getRectificationCurrentRefluxRatio();

/**
 * @brief Установка соотношения орошения
 * 
 * @param ratio Соотношение R/D
 */
void setRectificationRefluxRatio(float ratio);

#endif // RECTIFICATION_H
