/**
 * @file process_detection.h
 * @brief Автоматическое определение типа процесса по температуре
 */

#ifndef PROCESS_DETECTION_H
#define PROCESS_DETECTION_H

#include <Arduino.h>

// Тип обнаруженного процесса
enum DetectedProcess {
    PROCESS_UNKNOWN,          // Неизвестный процесс
    PROCESS_RECTIFICATION,    // Ректификация
    PROCESS_DISTILLATION,     // Дистилляция
    PROCESS_WATER_DISTILL,    // Дистилляция воды
    PROCESS_STRIPPING         // Отгонка спирта сырца
};

// Структура результата анализа
struct ProcessAnalysis {
    DetectedProcess processType;
    float confidence;         // Уверенность в определении (0-100%)
    String reason;           // Причина определения
    float estimatedABV;      // Оценочная крепость
};

/**
 * @brief Инициализация модуля определения процесса
 */
void initProcessDetection();

/**
 * @brief Анализ температур для определения процесса
 * 
 * @param cubeTemp Температура куба
 * @param columnTemp Температура колонны (0 если нет)
 * @param refluxTemp Температура дефлегматора (0 если нет)
 * @return Результат анализа
 */
ProcessAnalysis analyzeProcess(float cubeTemp, float columnTemp = 0, float refluxTemp = 0);

/**
 * @brief Непрерывный мониторинг для автоопределения
 * 
 * Анализирует изменения температуры во времени
 * 
 * @return Обновленный результат анализа
 */
ProcessAnalysis monitorProcess();

/**
 * @brief Рекомендация параметров для обнаруженного процесса
 * 
 * @param process Тип процесса
 * @return Строка с рекомендациями
 */
String getProcessRecommendations(DetectedProcess process);

/**
 * @brief Получение названия процесса
 * 
 * @param process Тип процесса
 * @return Название
 */
String getProcessName(DetectedProcess process);

/**
 * @brief Оценка крепости по температуре
 * 
 * @param temperature Температура паров
 * @return Оценочная крепость в % ABV
 */
float estimateABV(float temperature);

#endif // PROCESS_DETECTION_H
