#ifndef HISTORY_H
#define HISTORY_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "fs_compat.h"
#include <vector>

// Константы для истории
#define MAX_HISTORY_FILES 50        // Максимум файлов истории
#define MAX_HISTORY_SIZE  2097152    // Максимум 2 МБ общего размера
#define HISTORY_DIR "/history"       // Директория для хранения истории
#define TIMESERIES_INTERVAL 60       // Интервал записи временных рядов (сек)
#define MAX_TIMESERIES_POINTS 500    // Максимум точек во временном ряду

// Структуры данных для истории процессов

// Метаданные процесса
struct ProcessMetadata {
    uint32_t startTime;              // Unix timestamp начала
    uint32_t endTime;                // Unix timestamp окончания
    uint32_t duration;               // Длительность в секундах
    bool completedSuccessfully;      // Успешное завершение
    String deviceId;                 // ID устройства
};

// Информация о процессе
struct ProcessInfo {
    String type;                     // rectification, distillation, mashing, hold
    String mode;                     // auto, manual
    String profile;                  // Имя профиля (если есть)
};

// Параметры процесса
struct ProcessParameters {
    uint16_t targetPower;            // Целевая мощность (Вт)
    uint16_t headVolume;             // Объём голов (мл)
    uint16_t bodyVolume;             // Объём тела (мл)
    uint16_t tailVolume;             // Объём хвостов (мл)
    uint16_t pumpSpeedHead;          // Скорость насоса для голов (мл/час)
    uint16_t pumpSpeedBody;          // Скорость насоса для тела (мл/час)
    uint16_t stabilizationTime;      // Время стабилизации (сек)
    bool wattControlEnabled;         // Watt-Control включен
    bool smartDecrementEnabled;      // Smart Decrement включен
};

// Статистика температур
struct TempMetrics {
    float min;
    float max;
    float avg;
    float final;
};

// Метрики процесса
struct ProcessMetrics {
    TempMetrics cube;                // Температура куба
    TempMetrics columnBottom;        // Температура царги снизу
    TempMetrics columnTop;           // Температура царги сверху
    TempMetrics deflegmator;         // Температура дефлегматора

    float energyUsed;                // Потреблённая энергия (кВт·ч)
    uint16_t avgPower;               // Средняя мощность (Вт)
    uint16_t peakPower;              // Пиковая мощность (Вт)

    uint16_t totalVolume;            // Общий объём отбора (мл)
    uint16_t avgSpeed;               // Средняя скорость насоса (мл/час)
};

// Фаза процесса
struct ProcessPhase {
    String name;                     // heating, stabilization, heads, body, tails
    uint32_t startTime;              // Unix timestamp начала
    uint32_t endTime;                // Unix timestamp окончания
    uint32_t duration;               // Длительность (сек)
    float startTemp;                 // Начальная температура
    float endTemp;                   // Конечная температура
    uint16_t volume;                 // Объём отобранный (мл)
    uint16_t avgSpeed;               // Средняя скорость насоса (мл/час)
};

// Точка временного ряда
struct TimeseriesPoint {
    uint32_t time;                   // Unix timestamp
    float cube;                      // Температура куба
    float columnTop;                 // Температура царги сверху
    float columnBottom;              // Температура царги снизу
    float deflegmator;               // Температура дефлегматора
    uint16_t power;                  // Мощность (Вт)
    float voltage;                   // Напряжение (V)
    float current;                   // Ток (A)
    uint16_t pumpSpeed;              // Скорость насоса (мл/час)
};

// Предупреждение или ошибка
struct ProcessWarning {
    uint32_t time;                   // Unix timestamp
    String message;                  // Сообщение
    String severity;                 // info, warning, error
};

// Результаты процесса
struct ProcessResults {
    uint16_t headsCollected;         // Собрано голов (мл)
    uint16_t bodyCollected;          // Собрано тела (мл)
    uint16_t tailsCollected;         // Собрано хвостов (мл)
    uint16_t totalCollected;         // Всего собрано (мл)
    String status;                   // completed, stopped, error
    std::vector<ProcessWarning> errors;
    std::vector<ProcessWarning> warnings;
};

// Полная структура истории процесса
struct ProcessHistory {
    String id;                       // Уникальный ID (Unix timestamp)
    String version;                  // Версия схемы/firmware
    ProcessMetadata metadata;
    ProcessInfo process;
    ProcessParameters parameters;
    ProcessMetrics metrics;
    std::vector<ProcessPhase> phases;
    std::vector<TimeseriesPoint> timeseries;
    ProcessResults results;
    String notes;                    // Заметки пользователя
};

// Краткая информация о процессе для списка
struct ProcessListItem {
    String id;
    String type;
    uint32_t startTime;
    uint32_t duration;
    String status;
    uint16_t totalVolume;
};

// ============================================================================
// Функции для работы с историей
// ============================================================================

// Инициализация системы истории
bool initHistory();

// Сохранение процесса в историю
bool saveProcessHistory(const ProcessHistory& history);

// Загрузка процесса из истории по ID
bool loadProcessHistory(const String& id, ProcessHistory& history);

// Получение списка всех процессов
std::vector<ProcessListItem> getProcessList();

// Удаление процесса из истории
bool deleteProcess(const String& id);

// Очистка всей истории
bool clearHistory();

// Ротация файлов истории (удаление старых при превышении лимитов)
void rotateHistory();

// Получение количества файлов в истории
uint16_t getHistoryCount();

// Получение общего размера файлов истории
size_t getHistorySize();

// Экспорт процесса в CSV
String exportProcessToCSV(const ProcessHistory& history);

// Экспорт процесса в JSON
String exportProcessToJSON(const ProcessHistory& history);

// ============================================================================
// Вспомогательные функции для сбора метрик в реальном времени
// ============================================================================

// Класс для накопления данных процесса в реальном времени
class ProcessRecorder {
public:
    ProcessRecorder();

    // Начать запись нового процесса
    void startRecording(const String& type, const String& mode);

    // Остановить запись
    void stopRecording(bool success);

    // Добавить точку временного ряда
    void addTimeseriesPoint(const TimeseriesPoint& point);

    // Установить параметры процесса
    void setParameters(const ProcessParameters& params);

    // Добавить фазу процесса
    void addPhase(const ProcessPhase& phase);

    // Добавить предупреждение
    void addWarning(const String& message, const String& severity);

    // Установить результаты
    void setResults(const ProcessResults& results);

    // Добавить заметку
    void setNotes(const String& notes);

    // Получить текущую историю
    ProcessHistory& getHistory();

    // Проверить, идёт ли запись
    bool isRecording() const { return recording; }

private:
    ProcessHistory currentHistory;
    bool recording;
    uint32_t lastTimeseriesTime;

    void calculateMetrics();
};

// Глобальный экземпляр рекордера
extern ProcessRecorder processRecorder;

#endif // HISTORY_H
