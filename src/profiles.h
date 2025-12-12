/**
 * Smart-Column S3 - Profiles Manager
 *
 * Система сохранения и управления профилями процессов
 * Версия: 1.3.3
 */

#ifndef PROFILES_H
#define PROFILES_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <vector>

// Константы для профилей
#define MAX_PROFILES 100              // Максимум профилей
#define MAX_BUILTIN_PROFILES 10       // Максимум встроенных рецептов
#define PROFILES_DIR "/profiles"      // Директория для хранения профилей
#define MAX_PROFILE_NAME_LEN 50       // Максимальная длина имени
#define MAX_PROFILE_DESC_LEN 200      // Максимальная длина описания

// ============================================================================
// Структуры данных для профилей
// ============================================================================

// Метаданные профиля
struct ProfileMetadata {
    String name;                      // Название профиля
    String description;               // Описание
    String category;                  // rectification, distillation, mashing
    std::vector<String> tags;         // Теги для поиска
    uint32_t created;                 // Unix timestamp создания
    uint32_t updated;                 // Unix timestamp обновления
    String author;                    // Автор профиля
    bool isBuiltin;                   // Встроенный рецепт (нельзя удалить)
};

// Параметры нагревателя
struct HeaterParams {
    uint16_t maxPower;                // Максимальная мощность (Вт)
    bool autoMode;                    // Автоматический режим
    float pidKp;                      // ПИД коэффициент Kp
    float pidKi;                      // ПИД коэффициент Ki
    float pidKd;                      // ПИД коэффициент Kd
};

// Параметры ректификации
struct RectificationParams {
    uint16_t stabilizationMin;        // Время стабилизации (минуты)
    uint16_t headsVolume;             // Объем голов (мл)
    uint16_t bodyVolume;              // Объем тела (мл)
    uint16_t tailsVolume;             // Объем хвостов (мл)
    uint16_t headsSpeed;              // Скорость отбора голов (мл/ч/кВт)
    uint16_t bodySpeed;               // Скорость отбора тела (мл/ч/кВт)
    uint16_t tailsSpeed;              // Скорость отбора хвостов (мл/ч/кВт)
    uint16_t purgeMin;                // Время продувки (минуты)
};

// Параметры дистилляции
struct DistillationParams {
    uint16_t headsVolume;             // Объем голов (мл, может быть 0)
    uint16_t targetVolume;            // Целевой объем (мл)
    uint16_t speed;                   // Скорость отбора (мл/ч)
    float endTemp;                    // Температура завершения (°C)
};

// Температурные пороги
struct TemperatureParams {
    float maxCube;                    // Максимальная температура куба (°C)
    float maxColumn;                  // Максимальная температура колонны (°C)
    float headsEnd;                   // Температура окончания голов (°C)
    float bodyStart;                  // Температура начала тела (°C)
    float bodyEnd;                    // Температура окончания тела (°C)
};

// Параметры безопасности
struct SafetyParams {
    uint16_t maxRuntime;              // Максимальное время работы (минуты)
    float waterFlowMin;               // Минимальный поток воды (л/мин)
    uint16_t pressureMax;             // Максимальное давление (mmHg)
};

// Все параметры процесса
struct ProfileParameters {
    String mode;                      // rectification, distillation, mashing
    String model;                     // classic, alternative
    HeaterParams heater;
    RectificationParams rectification;
    DistillationParams distillation;
    TemperatureParams temperatures;
    SafetyParams safety;
};

// Статистика использования профиля
struct ProfileStatistics {
    uint16_t useCount;                // Количество использований
    uint32_t lastUsed;                // Unix timestamp последнего использования
    uint32_t avgDuration;             // Средняя длительность процесса (сек)
    uint16_t avgYield;                // Средний выход продукта (мл)
    float successRate;                // Процент успешных завершений
};

// Полная структура профиля
struct Profile {
    String id;                        // Уникальный ID (timestamp)
    ProfileMetadata metadata;
    ProfileParameters parameters;
    ProfileStatistics statistics;
};

// Краткая информация о профиле для списка
struct ProfileListItem {
    String id;
    String name;
    String category;
    uint16_t useCount;
    uint32_t lastUsed;
    bool isBuiltin;
};

// ============================================================================
// Функции для работы с профилями
// ============================================================================

/**
 * Инициализация системы профилей
 * @return true если успешно
 */
bool initProfiles();

/**
 * Сохранение профиля
 * @param profile Профиль для сохранения
 * @return true если успешно
 */
bool saveProfile(const Profile& profile);

/**
 * Загрузка профиля по ID
 * @param id ID профиля
 * @param profile Структура для загрузки данных
 * @return true если успешно
 */
bool loadProfile(const String& id, Profile& profile);

/**
 * Получение списка всех профилей
 * @return Вектор с кратким описанием профилей
 */
std::vector<ProfileListItem> getProfileList();

/**
 * Удаление профиля
 * @param id ID профиля
 * @return true если успешно (встроенные нельзя удалить)
 */
bool deleteProfile(const String& id);

/**
 * Очистка всех профилей (кроме встроенных)
 * @return true если успешно
 */
bool clearProfiles();

/**
 * Загрузка встроенных рецептов
 * @return true если успешно
 */
bool loadBuiltinProfiles();

/**
 * Ротация профилей (удаление старых при превышении лимита)
 */
void rotateProfiles();

/**
 * Получение количества профилей
 * @return Количество профилей
 */
uint16_t getProfileCount();

/**
 * Валидация профиля
 * @param profile Профиль для проверки
 * @return true если валидный
 */
bool validateProfile(const Profile& profile);

/**
 * Применение профиля к текущим настройкам
 * @param id ID профиля
 * @return true если успешно
 */
bool applyProfile(const String& id);

/**
 * Обновление статистики использования профиля
 * @param id ID профиля
 * @param success Успешное ли завершение
 * @param duration Длительность процесса (сек)
 * @param yield Выход продукта (мл)
 */
void updateProfileStatistics(const String& id, bool success, uint32_t duration, uint16_t yield);

/**
 * Создание профиля из текущих настроек
 * @param name Название профиля
 * @param description Описание
 * @param category Категория
 * @return ID созданного профиля или пустая строка при ошибке
 */
String createProfileFromSettings(const String& name, const String& description, const String& category);

#endif // PROFILES_H
