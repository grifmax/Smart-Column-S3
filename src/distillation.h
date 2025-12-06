/**
 * @file distillation.h
 * @brief Интерфейс управления процессом дистилляции
 */

#ifndef DISTILLATION_H
#define DISTILLATION_H

#include <Arduino.h>

// Фазы процесса дистилляции
enum DistillationPhase {
    DIST_PHASE_IDLE,              // Ожидание
    DIST_PHASE_HEATING,           // Нагрев
    DIST_PHASE_HEADS_COLLECTION,  // Отбор голов
    DIST_PHASE_DISTILLATION,      // Основной процесс дистилляции (тело)
    DIST_PHASE_FINISHING          // Завершение процесса
};

/**
 * @brief Инициализация модуля дистилляции
 * 
 * @return true если инициализация прошла успешно
 */
bool initDistillation();

/**
 * @brief Запуск процесса дистилляции
 * 
 * @return true если процесс успешно запущен
 */
bool startDistillation();

/**
 * @brief Остановка процесса дистилляции
 */
void stopDistillation();

/**
 * @brief Пауза процесса дистилляции
 */
void pauseDistillation();

/**
 * @brief Возобновление процесса дистилляции
 */
void resumeDistillation();

/**
 * @brief Проверка запущенного процесса дистилляции
 * 
 * @return true если процесс дистилляции запущен
 */
bool isDistillationRunning();

/**
 * @brief Проверка состояния паузы процесса дистилляции
 * 
 * @return true если процесс дистилляции на паузе
 */
bool isDistillationPaused();

/**
 * @brief Получение текущей фазы дистилляции
 * 
 * @return Текущая фаза дистилляции
 */
DistillationPhase getDistillationPhase();

/**
 * @brief Получение имени текущей фазы дистилляции
 * 
 * @return Строковое представление текущей фазы
 */
String getDistillationPhaseName();

/**
 * @brief Обработка процесса дистилляции
 * 
 * Вызывается в основном цикле для обработки текущей фазы дистилляции
 */
void processDistillation();

/**
 * @brief Получение времени работы процесса дистилляции
 * 
 * @return Время работы в секундах
 */
unsigned long getDistillationUptime();

/**
 * @brief Получение времени текущей фазы дистилляции
 * 
 * @return Время текущей фазы в секундах
 */
unsigned long getDistillationPhaseTime();

/**
 * @brief Получение объема собранного продукта
 * 
 * @return Объем продукта в миллилитрах
 */
int getDistillationProductVolume();

/**
 * @brief Получение объема собранных голов
 * 
 * @return Объем голов в миллилитрах
 */
int getDistillationHeadsVolume();

/**
 * @brief Проверка режима отбора голов
 * 
 * @return true если включен режим отбора голов
 */
bool isDistillationHeadsMode();

/**
 * @brief Установка фазы дистилляции
 * 
 * @param phase Новая фаза дистилляции
 */
void setDistillationPhase(DistillationPhase phase);

/**
 * @brief Переход к следующей фазе дистилляции
 */
void moveToNextDistillationPhase();

/**
 * @brief Ручное переключение в фазу отбора голов
 * 
 * @return true если переключение успешно
 */
bool startDistillationHeadsCollection();

/**
 * @brief Ручное переключение в фазу основной дистилляции
 * 
 * @return true если переключение успешно
 */
bool startMainDistillation();

/**
 * @brief Установка скорости отбора
 * 
 * @param flowRate Скорость отбора в мл/мин
 */
void setDistillationFlowRate(float flowRate);

/**
 * @brief Получение текущей скорости отбора
 * 
 * @return Скорость отбора в мл/мин
 */
float getDistillationFlowRate();

/**
 * @brief Проверка безопасности процесса дистилляции
 * 
 * @return true если все параметры в безопасных пределах
 */
bool checkDistillationSafety();

/**
 * @brief Получение средней скорости отбора
 * 
 * @return Средняя скорость отбора в мл/мин
 */
float getDistillationAverageFlowRate();

/**
 * @brief Прогноз времени до окончания процесса
 * 
 * @return Прогнозируемое время в секундах
 */
unsigned long getDistillationEstimatedTimeLeft();

/**
 * @brief Установка целевого объема голов
 * 
 * @param volume Целевой объем в миллилитрах
 */
void setDistillationHeadsTargetVolume(int volume);

/**
 * @brief Включение или выключение режима отбора голов
 * 
 * @param enable true для включения отбора голов
 */
void setDistillationHeadsMode(bool enable);

/**
 * @brief Установка мощности нагрева для фазы дистилляции
 * 
 * @param powerWatts Мощность в ваттах
 */
void setDistillationPower(int powerWatts);

/**
 * @brief Установка температуры окончания процесса
 * 
 * @param tempC Температура в градусах Цельсия
 */
void setDistillationEndTemperature(float tempC);

/**
 * @brief Получение текущего графика температуры
 * 
 * @param buffer Буфер для сохранения температур
 * @param bufferSize Размер буфера
 * @param interval Интервал между точками в секундах
 * @return Количество записанных точек
 */
int getDistillationTemperatureGraph(float* buffer, int bufferSize, int interval = 60);

/**
 * @brief Получение целевого объема голов
 * 
 * @return Целевой объем в миллилитрах
 */
int getDistillationHeadsTargetVolume();

/**
 * @brief Ручное завершение фазы отбора голов
 * 
 * @return true если успешно переключено в основной режим
 */
bool finishHeadsCollection();

#endif // DISTILLATION_H
