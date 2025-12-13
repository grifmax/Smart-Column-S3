/**
 * @file process_detection.cpp
 * @brief Реализация автоматического определения типа процесса
 */

#include "process_detection.h"
#include "logger.h"
#include <vector>

// История измерений
struct TempReading {
    unsigned long timestamp;
    float cubeTemp;
    float columnTemp;
    float refluxTemp;
};

static std::vector<TempReading> tempHistory;
static const int MAX_HISTORY = 60; // Последние 60 измерений

// Инициализация модуля
void initProcessDetection() {
    LOG_INFO(LOG_CAT_SYSTEM, "Инициализация модуля определения процесса...");
    tempHistory.clear();
    tempHistory.reserve(MAX_HISTORY);
}

// Анализ температур
ProcessAnalysis analyzeProcess(float cubeTemp, float columnTemp, float refluxTemp) {
    ProcessAnalysis result;
    result.processType = PROCESS_UNKNOWN;
    result.confidence = 0;
    result.estimatedABV = 0;
    
    // Проверка валидности данных
    if (cubeTemp < 20 || cubeTemp > 120) {
        result.reason = "Некорректная температура куба";
        return result;
    }
    
    // Сохраняем в историю
    TempReading reading;
    reading.timestamp = millis();
    reading.cubeTemp = cubeTemp;
    reading.columnTemp = columnTemp;
    reading.refluxTemp = refluxTemp;
    
    if (tempHistory.size() >= MAX_HISTORY) {
        tempHistory.erase(tempHistory.begin());
    }
    tempHistory.push_back(reading);
    
    // Анализ 1: Проверка температуры кипения воды
    if (cubeTemp >= 99.0 && cubeTemp <= 101.0) {
        result.processType = PROCESS_WATER_DISTILL;
        result.confidence = 90.0;
        result.reason = "Температура соответствует кипению воды";
        result.estimatedABV = 0;
        return result;
    }
    
    // Анализ 2: Наличие колонны и дефлегматора = ректификация
    if (columnTemp > 0 && refluxTemp > 0) {
        // Проверка характерных температур для ректификации
        if (cubeTemp >= 88 && cubeTemp <= 98 && 
            refluxTemp >= 77 && refluxTemp <= 84) {
            
            result.processType = PROCESS_RECTIFICATION;
            result.confidence = 85.0;
            result.reason = "Обнаружена колонна и дефлегматор, температуры типичны для ректификации";
            result.estimatedABV = estimateABV(refluxTemp);
            
            // Увеличиваем уверенность если есть разница температур
            float tempDelta = cubeTemp - refluxTemp;
            if (tempDelta >= 8 && tempDelta <= 15) {
                result.confidence = 95.0;
                result.reason += ". Температурный градиент соответствует ректификации";
            }
            
            return result;
        }
    }
    
    // Анализ 3: Простая дистилляция
    if (cubeTemp >= 78 && cubeTemp <= 95) {
        result.processType = PROCESS_DISTILLATION;
        result.confidence = 70.0;
        result.reason = "Температура соответствует дистилляции спирта";
        result.estimatedABV = estimateABV(cubeTemp);
        
        // Проверяем историю для уточнения
        if (tempHistory.size() >= 10) {
            // Анализируем стабильность температуры
            float avgTemp = 0;
            for (int i = tempHistory.size() - 10; i < tempHistory.size(); i++) {
                avgTemp += tempHistory[i].cubeTemp;
            }
            avgTemp /= 10;
            
            float deviation = abs(cubeTemp - avgTemp);
            if (deviation < 2.0) {
                result.confidence = 85.0;
                result.reason += ". Стабильная температура подтверждает процесс";
            }
        }
        
        return result;
    }
    
    // Анализ 4: Отгонка спирта-сырца
    if (cubeTemp >= 85 && cubeTemp <= 98) {
        result.processType = PROCESS_STRIPPING;
        result.confidence = 60.0;
        result.reason = "Высокая температура, возможна отгонка спирта-сырца";
        result.estimatedABV = estimateABV(cubeTemp);
        return result;
    }
    
    // Неопределенный процесс
    result.processType = PROCESS_UNKNOWN;
    result.confidence = 0;
    result.reason = "Не удалось определить тип процесса по температурам";
    
    return result;
}

// Непрерывный мониторинг
ProcessAnalysis monitorProcess() {
    if (tempHistory.empty()) {
        ProcessAnalysis result;
        result.processType = PROCESS_UNKNOWN;
        result.confidence = 0;
        result.reason = "Недостаточно данных для анализа";
        return result;
    }
    
    // Используем последнее измерение
    const TempReading& last = tempHistory.back();
    ProcessAnalysis result = analyzeProcess(last.cubeTemp, last.columnTemp, last.refluxTemp);
    
    // Дополнительный анализ тренда
    if (tempHistory.size() >= 5) {
        // Вычисляем скорость изменения температуры
        float tempChange = tempHistory.back().cubeTemp - tempHistory[tempHistory.size() - 5].cubeTemp;
        unsigned long timeChange = (tempHistory.back().timestamp - tempHistory[tempHistory.size() - 5].timestamp) / 1000;
        
        if (timeChange > 0) {
            float rateOfChange = (tempChange / timeChange) * 60; // °C/мин
            
            LOG_DEBUGF(LOG_CAT_PROCESS, "Скорость изменения температуры: %.2f °C/мин", rateOfChange);
            
            // Быстрый рост = нагрев
            if (rateOfChange > 2.0) {
                result.reason += ". Быстрый нагрев";
            }
            // Стабильная температура = рабочий режим
            else if (abs(rateOfChange) < 0.5) {
                result.reason += ". Стабильная температура";
                result.confidence += 5.0; // Немного увеличиваем уверенность
                if (result.confidence > 100) result.confidence = 100;
            }
        }
    }
    
    return result;
}

// Рекомендации для процесса
String getProcessRecommendations(DetectedProcess process) {
    switch (process) {
        case PROCESS_RECTIFICATION:
            return "Рекомендации для ректификации:\n"
                   "- Стабилизация колонны: 30-45 мин\n"
                   "- Отбор голов: 50-100 мл при 78°C\n"
                   "- Отбор тела: 78.4°C, 300-400 мл/ч\n"
                   "- Следите за температурой дефлегматора";
        
        case PROCESS_DISTILLATION:
            return "Рекомендации для дистилляции:\n"
                   "- Начало отбора при 78-80°C\n"
                   "- Отбор голов: 50 мл\n"
                   "- Основной отбор до 85-90°C\n"
                   "- Скорость отбора: 500-800 мл/ч";
        
        case PROCESS_WATER_DISTILL:
            return "Дистилляция воды:\n"
                   "- Температура кипения: 100°C\n"
                   "- Обеспечьте хорошее охлаждение";
        
        case PROCESS_STRIPPING:
            return "Отгонка спирта-сырца:\n"
                   "- Быстрый отбор без разделения\n"
                   "- Скорость: 1-2 л/ч\n"
                   "- Отбор до 98°C в кубе";
        
        default:
            return "Процесс не определен. Проверьте температуры.";
    }
}

// Название процесса
String getProcessName(DetectedProcess process) {
    switch (process) {
        case PROCESS_RECTIFICATION:
            return "Ректификация";
        case PROCESS_DISTILLATION:
            return "Дистилляция";
        case PROCESS_WATER_DISTILL:
            return "Дистилляция воды";
        case PROCESS_STRIPPING:
            return "Отгонка спирта-сырца";
        default:
            return "Неизвестный процесс";
    }
}

// Оценка крепости по температуре (упрощенная)
float estimateABV(float temperature) {
    // Упрощенная формула на основе температуры кипения водно-спиртовой смеси
    // При атмосферном давлении
    
    if (temperature <= 78.15) {
        return 100.0; // Чистый спирт
    }
    if (temperature >= 100.0) {
        return 0.0;   // Вода
    }
    
    // Линейная интерполяция (очень упрощенная!)
    // В реальности зависимость нелинейная
    float abv = 100.0 - ((temperature - 78.15) / (100.0 - 78.15)) * 100.0;
    
    // Коррекция для типичных значений
    if (temperature >= 78.0 && temperature <= 78.5) {
        abv = 95.0 + (78.5 - temperature) * 10.0; // 95-100%
    } else if (temperature >= 78.5 && temperature <= 80.0) {
        abv = 85.0 + (80.0 - temperature) * 6.67; // 85-95%
    } else if (temperature >= 80.0 && temperature <= 85.0) {
        abv = 60.0 + (85.0 - temperature) * 5.0;  // 60-85%
    } else if (temperature >= 85.0 && temperature <= 90.0) {
        abv = 30.0 + (90.0 - temperature) * 6.0;  // 30-60%
    } else if (temperature >= 90.0 && temperature <= 95.0) {
        abv = 10.0 + (95.0 - temperature) * 4.0;  // 10-30%
    } else {
        abv = (100.0 - temperature) * 0.5;        // < 10%
    }
    
    if (abv < 0) abv = 0;
    if (abv > 100) abv = 100;
    
    return abv;
}
