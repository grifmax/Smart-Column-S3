/**
 * Smart-Column S3 - Profiles Manager Implementation
 */

#include "profiles.h"
#include <FS.h>
#include <algorithm>

// ============================================================================
// Инициализация системы профилей
// ============================================================================

bool initProfiles() {
    Serial.println("Инициализация системы профилей...");

    // Проверить, смонтирована ли SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("Ошибка: не удалось инициализировать SPIFFS");
        return false;
    }

    // Проверить существование директории /profiles
    if (!SPIFFS.exists(PROFILES_DIR)) {
        Serial.println("Создание директории /profiles");
        // SPIFFS не требует явного создания директорий
    }

    // Загрузить встроенные рецепты если их нет
    if (getProfileCount() == 0) {
        Serial.println("Загрузка встроенных рецептов...");
        loadBuiltinProfiles();
    }

    // Провести ротацию профилей
    rotateProfiles();

    Serial.println("Система профилей инициализирована");
    Serial.printf("Профилей: %d\n", getProfileCount());

    return true;
}

// ============================================================================
// Сохранение профиля
// ============================================================================

bool saveProfile(const Profile& profile) {
    // Валидация
    if (!validateProfile(profile)) {
        Serial.println("Ошибка: профиль не прошел валидацию");
        return false;
    }

    String filename = String(PROFILES_DIR) + "/profile_" + profile.id + ".json";
    Serial.printf("Сохранение профиля: %s\n", filename.c_str());

    File file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("Ошибка: не удалось создать файл профиля");
        return false;
    }

    // Создать JSON документ
    DynamicJsonDocument doc(8192);  // 8 КБ для профиля

    doc["id"] = profile.id;

    // Метаданные
    JsonObject metadata = doc.createNestedObject("metadata");
    metadata["name"] = profile.metadata.name;
    metadata["description"] = profile.metadata.description;
    metadata["category"] = profile.metadata.category;

    JsonArray tags = metadata.createNestedArray("tags");
    for (const auto& tag : profile.metadata.tags) {
        tags.add(tag);
    }

    metadata["created"] = profile.metadata.created;
    metadata["updated"] = profile.metadata.updated;
    metadata["author"] = profile.metadata.author;
    metadata["isBuiltin"] = profile.metadata.isBuiltin;

    // Параметры
    JsonObject parameters = doc.createNestedObject("parameters");
    parameters["mode"] = profile.parameters.mode;
    parameters["model"] = profile.parameters.model;

    // Нагреватель
    JsonObject heater = parameters.createNestedObject("heater");
    heater["maxPower"] = profile.parameters.heater.maxPower;
    heater["autoMode"] = profile.parameters.heater.autoMode;
    heater["pidKp"] = profile.parameters.heater.pidKp;
    heater["pidKi"] = profile.parameters.heater.pidKi;
    heater["pidKd"] = profile.parameters.heater.pidKd;

    // Ректификация
    JsonObject rectification = parameters.createNestedObject("rectification");
    rectification["stabilizationMin"] = profile.parameters.rectification.stabilizationMin;
    rectification["headsVolume"] = profile.parameters.rectification.headsVolume;
    rectification["bodyVolume"] = profile.parameters.rectification.bodyVolume;
    rectification["tailsVolume"] = profile.parameters.rectification.tailsVolume;
    rectification["headsSpeed"] = profile.parameters.rectification.headsSpeed;
    rectification["bodySpeed"] = profile.parameters.rectification.bodySpeed;
    rectification["tailsSpeed"] = profile.parameters.rectification.tailsSpeed;
    rectification["purgeMin"] = profile.parameters.rectification.purgeMin;

    // Дистилляция
    JsonObject distillation = parameters.createNestedObject("distillation");
    distillation["headsVolume"] = profile.parameters.distillation.headsVolume;
    distillation["targetVolume"] = profile.parameters.distillation.targetVolume;
    distillation["speed"] = profile.parameters.distillation.speed;
    distillation["endTemp"] = profile.parameters.distillation.endTemp;

    // Температуры
    JsonObject temperatures = parameters.createNestedObject("temperatures");
    temperatures["maxCube"] = profile.parameters.temperatures.maxCube;
    temperatures["maxColumn"] = profile.parameters.temperatures.maxColumn;
    temperatures["headsEnd"] = profile.parameters.temperatures.headsEnd;
    temperatures["bodyStart"] = profile.parameters.temperatures.bodyStart;
    temperatures["bodyEnd"] = profile.parameters.temperatures.bodyEnd;

    // Безопасность
    JsonObject safety = parameters.createNestedObject("safety");
    safety["maxRuntime"] = profile.parameters.safety.maxRuntime;
    safety["waterFlowMin"] = profile.parameters.safety.waterFlowMin;
    safety["pressureMax"] = profile.parameters.safety.pressureMax;

    // Статистика
    JsonObject statistics = doc.createNestedObject("statistics");
    statistics["useCount"] = profile.statistics.useCount;
    statistics["lastUsed"] = profile.statistics.lastUsed;
    statistics["avgDuration"] = profile.statistics.avgDuration;
    statistics["avgYield"] = profile.statistics.avgYield;
    statistics["successRate"] = profile.statistics.successRate;

    // Сериализовать в файл
    if (serializeJson(doc, file) == 0) {
        Serial.println("Ошибка: не удалось записать JSON");
        file.close();
        return false;
    }

    file.close();
    Serial.printf("Профиль сохранён (%d байт)\n", file.size());

    // Провести ротацию
    rotateProfiles();

    return true;
}

// ============================================================================
// Загрузка профиля
// ============================================================================

bool loadProfile(const String& id, Profile& profile) {
    String filename = String(PROFILES_DIR) + "/profile_" + id + ".json";

    if (!SPIFFS.exists(filename)) {
        Serial.printf("Ошибка: файл не найден: %s\n", filename.c_str());
        return false;
    }

    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        Serial.println("Ошибка: не удалось открыть файл");
        return false;
    }

    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("Ошибка парсинга JSON: %s\n", error.c_str());
        return false;
    }

    // Загрузить данные из JSON
    profile.id = doc["id"].as<String>();

    // Метаданные
    profile.metadata.name = doc["metadata"]["name"].as<String>();
    profile.metadata.description = doc["metadata"]["description"].as<String>();
    profile.metadata.category = doc["metadata"]["category"].as<String>();

    profile.metadata.tags.clear();
    JsonArray tags = doc["metadata"]["tags"];
    for (JsonVariant tag : tags) {
        profile.metadata.tags.push_back(tag.as<String>());
    }

    profile.metadata.created = doc["metadata"]["created"];
    profile.metadata.updated = doc["metadata"]["updated"];
    profile.metadata.author = doc["metadata"]["author"].as<String>();
    profile.metadata.isBuiltin = doc["metadata"]["isBuiltin"];

    // Параметры
    profile.parameters.mode = doc["parameters"]["mode"].as<String>();
    profile.parameters.model = doc["parameters"]["model"].as<String>();

    // Нагреватель
    profile.parameters.heater.maxPower = doc["parameters"]["heater"]["maxPower"];
    profile.parameters.heater.autoMode = doc["parameters"]["heater"]["autoMode"];
    profile.parameters.heater.pidKp = doc["parameters"]["heater"]["pidKp"];
    profile.parameters.heater.pidKi = doc["parameters"]["heater"]["pidKi"];
    profile.parameters.heater.pidKd = doc["parameters"]["heater"]["pidKd"];

    // Ректификация
    profile.parameters.rectification.stabilizationMin = doc["parameters"]["rectification"]["stabilizationMin"];
    profile.parameters.rectification.headsVolume = doc["parameters"]["rectification"]["headsVolume"];
    profile.parameters.rectification.bodyVolume = doc["parameters"]["rectification"]["bodyVolume"];
    profile.parameters.rectification.tailsVolume = doc["parameters"]["rectification"]["tailsVolume"];
    profile.parameters.rectification.headsSpeed = doc["parameters"]["rectification"]["headsSpeed"];
    profile.parameters.rectification.bodySpeed = doc["parameters"]["rectification"]["bodySpeed"];
    profile.parameters.rectification.tailsSpeed = doc["parameters"]["rectification"]["tailsSpeed"];
    profile.parameters.rectification.purgeMin = doc["parameters"]["rectification"]["purgeMin"];

    // Дистилляция
    profile.parameters.distillation.headsVolume = doc["parameters"]["distillation"]["headsVolume"];
    profile.parameters.distillation.targetVolume = doc["parameters"]["distillation"]["targetVolume"];
    profile.parameters.distillation.speed = doc["parameters"]["distillation"]["speed"];
    profile.parameters.distillation.endTemp = doc["parameters"]["distillation"]["endTemp"];

    // Температуры
    profile.parameters.temperatures.maxCube = doc["parameters"]["temperatures"]["maxCube"];
    profile.parameters.temperatures.maxColumn = doc["parameters"]["temperatures"]["maxColumn"];
    profile.parameters.temperatures.headsEnd = doc["parameters"]["temperatures"]["headsEnd"];
    profile.parameters.temperatures.bodyStart = doc["parameters"]["temperatures"]["bodyStart"];
    profile.parameters.temperatures.bodyEnd = doc["parameters"]["temperatures"]["bodyEnd"];

    // Безопасность
    profile.parameters.safety.maxRuntime = doc["parameters"]["safety"]["maxRuntime"];
    profile.parameters.safety.waterFlowMin = doc["parameters"]["safety"]["waterFlowMin"];
    profile.parameters.safety.pressureMax = doc["parameters"]["safety"]["pressureMax"];

    // Статистика
    profile.statistics.useCount = doc["statistics"]["useCount"];
    profile.statistics.lastUsed = doc["statistics"]["lastUsed"];
    profile.statistics.avgDuration = doc["statistics"]["avgDuration"];
    profile.statistics.avgYield = doc["statistics"]["avgYield"];
    profile.statistics.successRate = doc["statistics"]["successRate"];

    Serial.printf("Профиль загружен: %s\n", profile.metadata.name.c_str());
    return true;
}

// ============================================================================
// Получение списка профилей
// ============================================================================

std::vector<ProfileListItem> getProfileList() {
    std::vector<ProfileListItem> list;

    File root = SPIFFS.open(PROFILES_DIR);
    if (!root || !root.isDirectory()) {
        return list;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();

            // Проверить, что это файл профиля
            if (filename.startsWith("profile_") && filename.endsWith(".json")) {
                // Быстрая загрузка только необходимых полей
                DynamicJsonDocument doc(512);
                DeserializationError error = deserializeJson(doc, file);

                if (!error) {
                    ProfileListItem item;
                    item.id = doc["id"].as<String>();
                    item.name = doc["metadata"]["name"].as<String>();
                    item.category = doc["metadata"]["category"].as<String>();
                    item.useCount = doc["statistics"]["useCount"];
                    item.lastUsed = doc["statistics"]["lastUsed"];
                    item.isBuiltin = doc["metadata"]["isBuiltin"];

                    list.push_back(item);
                }
            }
        }
        file = root.openNextFile();
    }

    // Сортировать: встроенные первые, затем по частоте использования
    std::sort(list.begin(), list.end(), [](const ProfileListItem& a, const ProfileListItem& b) {
        if (a.isBuiltin != b.isBuiltin) return a.isBuiltin;
        return a.useCount > b.useCount;
    });

    return list;
}

// ============================================================================
// Удаление профиля
// ============================================================================

bool deleteProfile(const String& id) {
    // Сначала загрузить профиль чтобы проверить isBuiltin
    Profile profile;
    if (loadProfile(id, profile)) {
        if (profile.metadata.isBuiltin) {
            Serial.println("Ошибка: нельзя удалить встроенный рецепт");
            return false;
        }
    }

    String filename = String(PROFILES_DIR) + "/profile_" + id + ".json";

    if (!SPIFFS.exists(filename)) {
        Serial.printf("Файл не найден: %s\n", filename.c_str());
        return false;
    }

    if (SPIFFS.remove(filename)) {
        Serial.printf("Профиль удалён: %s\n", id.c_str());
        return true;
    }

    return false;
}

// ============================================================================
// Очистка всех профилей (кроме встроенных)
// ============================================================================

bool clearProfiles() {
    File root = SPIFFS.open(PROFILES_DIR);
    if (!root || !root.isDirectory()) {
        return false;
    }

    int deleted = 0;
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();

            // Проверить, что это профиль
            if (filename.startsWith("profile_") && filename.endsWith(".json")) {
                // Проверить, не встроенный ли
                DynamicJsonDocument doc(512);
                DeserializationError error = deserializeJson(doc, file);

                if (!error && !doc["metadata"]["isBuiltin"].as<bool>()) {
                    SPIFFS.remove(filename);
                    deleted++;
                }
            }
        }
        file = root.openNextFile();
    }

    Serial.printf("Удалено профилей: %d\n", deleted);
    return true;
}

// ============================================================================
// Загрузка встроенных рецептов
// ============================================================================

bool loadBuiltinProfiles() {
    uint32_t now = millis() / 1000;

    // ========================================================================
    // 1. Сахарная брага 40%
    // ========================================================================
    {
        Profile profile;
        profile.id = "builtin_sugar_40";

        profile.metadata.name = "Сахарная брага 40%";
        profile.metadata.description = "Классическая ректификация сахарной браги с крепостью 40%";
        profile.metadata.category = "rectification";
        profile.metadata.tags = {"сахар", "классика", "40%"};
        profile.metadata.created = now;
        profile.metadata.updated = now;
        profile.metadata.author = "system";
        profile.metadata.isBuiltin = true;

        profile.parameters.mode = "rectification";
        profile.parameters.model = "classic";

        profile.parameters.heater.maxPower = 3000;
        profile.parameters.heater.autoMode = true;
        profile.parameters.heater.pidKp = 2.0;
        profile.parameters.heater.pidKi = 0.5;
        profile.parameters.heater.pidKd = 1.0;

        profile.parameters.rectification.stabilizationMin = 20;
        profile.parameters.rectification.headsVolume = 50;
        profile.parameters.rectification.bodyVolume = 2000;
        profile.parameters.rectification.tailsVolume = 100;
        profile.parameters.rectification.headsSpeed = 150;
        profile.parameters.rectification.bodySpeed = 300;
        profile.parameters.rectification.tailsSpeed = 400;
        profile.parameters.rectification.purgeMin = 5;

        profile.parameters.temperatures.maxCube = 98.0;
        profile.parameters.temperatures.maxColumn = 82.0;
        profile.parameters.temperatures.headsEnd = 78.5;
        profile.parameters.temperatures.bodyStart = 78.0;
        profile.parameters.temperatures.bodyEnd = 85.0;

        profile.parameters.safety.maxRuntime = 720;
        profile.parameters.safety.waterFlowMin = 2.0;
        profile.parameters.safety.pressureMax = 150;

        profile.statistics.useCount = 0;
        profile.statistics.lastUsed = 0;
        profile.statistics.avgDuration = 0;
        profile.statistics.avgYield = 0;
        profile.statistics.successRate = 0;

        saveProfile(profile);
    }

    // ========================================================================
    // 2. Зерновая брага 12%
    // ========================================================================
    {
        Profile profile;
        profile.id = "builtin_grain_12";

        profile.metadata.name = "Зерновая брага 12%";
        profile.metadata.description = "Бережная ректификация зерновой браги с сохранением органолептики";
        profile.metadata.category = "rectification";
        profile.metadata.tags = {"зерно", "пшеница", "12%"};
        profile.metadata.created = now;
        profile.metadata.updated = now;
        profile.metadata.author = "system";
        profile.metadata.isBuiltin = true;

        profile.parameters.mode = "rectification";
        profile.parameters.model = "classic";

        profile.parameters.heater.maxPower = 2500;
        profile.parameters.heater.autoMode = true;
        profile.parameters.heater.pidKp = 2.0;
        profile.parameters.heater.pidKi = 0.5;
        profile.parameters.heater.pidKd = 1.0;

        profile.parameters.rectification.stabilizationMin = 30;
        profile.parameters.rectification.headsVolume = 100;
        profile.parameters.rectification.bodyVolume = 2500;
        profile.parameters.rectification.tailsVolume = 150;
        profile.parameters.rectification.headsSpeed = 120;
        profile.parameters.rectification.bodySpeed = 250;
        profile.parameters.rectification.tailsSpeed = 350;
        profile.parameters.rectification.purgeMin = 5;

        profile.parameters.temperatures.maxCube = 98.0;
        profile.parameters.temperatures.maxColumn = 82.0;
        profile.parameters.temperatures.headsEnd = 78.0;
        profile.parameters.temperatures.bodyStart = 77.5;
        profile.parameters.temperatures.bodyEnd = 84.0;

        profile.parameters.safety.maxRuntime = 720;
        profile.parameters.safety.waterFlowMin = 2.0;
        profile.parameters.safety.pressureMax = 150;

        profile.statistics.useCount = 0;
        profile.statistics.lastUsed = 0;
        profile.statistics.avgDuration = 0;
        profile.statistics.avgYield = 0;
        profile.statistics.successRate = 0;

        saveProfile(profile);
    }

    // ========================================================================
    // 3. Фруктовая дистилляция
    // ========================================================================
    {
        Profile profile;
        profile.id = "builtin_fruit_dist";

        profile.metadata.name = "Фруктовая дистилляция";
        profile.metadata.description = "Бережная дистилляция фруктовых браг с сохранением ароматики";
        profile.metadata.category = "distillation";
        profile.metadata.tags = {"фрукты", "дистилляция", "аромат"};
        profile.metadata.created = now;
        profile.metadata.updated = now;
        profile.metadata.author = "system";
        profile.metadata.isBuiltin = true;

        profile.parameters.mode = "distillation";
        profile.parameters.model = "classic";

        profile.parameters.heater.maxPower = 2000;
        profile.parameters.heater.autoMode = false;
        profile.parameters.heater.pidKp = 2.0;
        profile.parameters.heater.pidKi = 0.5;
        profile.parameters.heater.pidKd = 1.0;

        profile.parameters.distillation.headsVolume = 30;
        profile.parameters.distillation.targetVolume = 3000;
        profile.parameters.distillation.speed = 500;
        profile.parameters.distillation.endTemp = 96.0;

        profile.parameters.temperatures.maxCube = 98.0;
        profile.parameters.temperatures.maxColumn = 90.0;
        profile.parameters.temperatures.headsEnd = 82.0;
        profile.parameters.temperatures.bodyStart = 78.0;
        profile.parameters.temperatures.bodyEnd = 96.0;

        profile.parameters.safety.maxRuntime = 480;
        profile.parameters.safety.waterFlowMin = 1.5;
        profile.parameters.safety.pressureMax = 100;

        profile.statistics.useCount = 0;
        profile.statistics.lastUsed = 0;
        profile.statistics.avgDuration = 0;
        profile.statistics.avgYield = 0;
        profile.statistics.successRate = 0;

        saveProfile(profile);
    }

    Serial.println("Встроенные рецепты загружены");
    return true;
}

// ============================================================================
// Ротация профилей
// ============================================================================

void rotateProfiles() {
    std::vector<String> files;
    std::vector<bool> builtinFlags;

    // Собрать список файлов
    File root = SPIFFS.open(PROFILES_DIR);
    if (!root || !root.isDirectory()) {
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();
            if (filename.startsWith("profile_") && filename.endsWith(".json")) {
                // Проверить, встроенный ли
                DynamicJsonDocument doc(512);
                DeserializationError error = deserializeJson(doc, file);
                bool isBuiltin = false;
                if (!error) {
                    isBuiltin = doc["metadata"]["isBuiltin"].as<bool>();
                }

                files.push_back(filename);
                builtinFlags.push_back(isBuiltin);
            }
        }
        file = root.openNextFile();
    }

    // Посчитать пользовательские профили
    int userProfiles = 0;
    for (bool isBuiltin : builtinFlags) {
        if (!isBuiltin) userProfiles++;
    }

    // Удалить старые пользовательские если превышен лимит
    if (userProfiles > (MAX_PROFILES - MAX_BUILTIN_PROFILES)) {
        // Сортировать по времени (старые первые)
        std::sort(files.begin(), files.end());

        int toDelete = userProfiles - (MAX_PROFILES - MAX_BUILTIN_PROFILES);
        for (size_t i = 0; i < files.size() && toDelete > 0; i++) {
            if (!builtinFlags[i]) {
                Serial.printf("Удаление старого профиля: %s\n", files[i].c_str());
                SPIFFS.remove(files[i]);
                toDelete--;
            }
        }
    }
}

// ============================================================================
// Вспомогательные функции
// ============================================================================

uint16_t getProfileCount() {
    uint16_t count = 0;

    File root = SPIFFS.open(PROFILES_DIR);
    if (!root || !root.isDirectory()) {
        return 0;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();
            if (filename.startsWith("profile_") && filename.endsWith(".json")) {
                count++;
            }
        }
        file = root.openNextFile();
    }

    return count;
}

bool validateProfile(const Profile& profile) {
    // Проверить название
    if (profile.metadata.name.length() == 0 ||
        profile.metadata.name.length() > MAX_PROFILE_NAME_LEN) {
        Serial.println("Валидация: неверное название");
        return false;
    }

    // Проверить описание
    if (profile.metadata.description.length() > MAX_PROFILE_DESC_LEN) {
        Serial.println("Валидация: слишком длинное описание");
        return false;
    }

    // Проверить категорию
    if (profile.metadata.category != "rectification" &&
        profile.metadata.category != "distillation" &&
        profile.metadata.category != "mashing") {
        Serial.println("Валидация: неверная категория");
        return false;
    }

    // Проверить объемы
    if (profile.parameters.mode == "rectification") {
        if (profile.parameters.rectification.headsVolume == 0 ||
            profile.parameters.rectification.bodyVolume == 0) {
            Serial.println("Валидация: неверные объемы для ректификации");
            return false;
        }
    }

    // Проверить температуры
    if (profile.parameters.temperatures.maxCube < 0 ||
        profile.parameters.temperatures.maxCube > 120) {
        Serial.println("Валидация: неверная температура куба");
        return false;
    }

    // Все проверки пройдены
    return true;
}

// ============================================================================
// Применение профиля
// ============================================================================

bool applyProfile(const String& id) {
    Profile profile;
    if (!loadProfile(id, profile)) {
        return false;
    }

    Serial.printf("Применение профиля: %s\n", profile.metadata.name.c_str());

    // TODO: Применить параметры к системе
    // Эта функция будет интегрирована с основной системой управления
    // Сейчас просто логируем что профиль применяется

    Serial.println("Профиль применён (TODO: интеграция с системой)");
    return true;
}

// ============================================================================
// Обновление статистики
// ============================================================================

void updateProfileStatistics(const String& id, bool success, uint32_t duration, uint16_t yield) {
    Profile profile;
    if (!loadProfile(id, profile)) {
        return;
    }

    // Обновить статистику
    profile.statistics.useCount++;
    profile.statistics.lastUsed = millis() / 1000;

    // Обновить среднюю длительность
    if (profile.statistics.avgDuration == 0) {
        profile.statistics.avgDuration = duration;
    } else {
        profile.statistics.avgDuration =
            (profile.statistics.avgDuration * (profile.statistics.useCount - 1) + duration) /
            profile.statistics.useCount;
    }

    // Обновить средний выход
    if (profile.statistics.avgYield == 0) {
        profile.statistics.avgYield = yield;
    } else {
        profile.statistics.avgYield =
            (profile.statistics.avgYield * (profile.statistics.useCount - 1) + yield) /
            profile.statistics.useCount;
    }

    // Обновить процент успеха
    int successCount = (int)(profile.statistics.successRate * (profile.statistics.useCount - 1) / 100.0f);
    if (success) successCount++;
    profile.statistics.successRate = (float)successCount / profile.statistics.useCount * 100.0f;

    // Обновить timestamp
    profile.metadata.updated = millis() / 1000;

    // Сохранить обратно
    saveProfile(profile);

    Serial.printf("Статистика профиля обновлена: %s\n", profile.metadata.name.c_str());
}

// ============================================================================
// Создание профиля из текущих настроек
// ============================================================================

String createProfileFromSettings(const String& name, const String& description, const String& category) {
    Profile profile;

    uint32_t now = millis() / 1000;
    profile.id = String(now);

    profile.metadata.name = name;
    profile.metadata.description = description;
    profile.metadata.category = category;
    profile.metadata.created = now;
    profile.metadata.updated = now;
    profile.metadata.author = "user";
    profile.metadata.isBuiltin = false;

    // TODO: Получить текущие настройки из системы
    // Пока заполняем значениями по умолчанию
    profile.parameters.mode = category;
    profile.parameters.model = "classic";

    // Нагреватель
    profile.parameters.heater.maxPower = 3000;
    profile.parameters.heater.autoMode = true;
    profile.parameters.heater.pidKp = 2.0;
    profile.parameters.heater.pidKi = 0.5;
    profile.parameters.heater.pidKd = 1.0;

    // Ректификация
    profile.parameters.rectification.stabilizationMin = 20;
    profile.parameters.rectification.headsVolume = 50;
    profile.parameters.rectification.bodyVolume = 2000;
    profile.parameters.rectification.tailsVolume = 100;
    profile.parameters.rectification.headsSpeed = 150;
    profile.parameters.rectification.bodySpeed = 300;
    profile.parameters.rectification.tailsSpeed = 400;
    profile.parameters.rectification.purgeMin = 5;

    // Температуры
    profile.parameters.temperatures.maxCube = 98.0;
    profile.parameters.temperatures.maxColumn = 82.0;
    profile.parameters.temperatures.headsEnd = 78.5;
    profile.parameters.temperatures.bodyStart = 78.0;
    profile.parameters.temperatures.bodyEnd = 85.0;

    // Безопасность
    profile.parameters.safety.maxRuntime = 720;
    profile.parameters.safety.waterFlowMin = 2.0;
    profile.parameters.safety.pressureMax = 150;

    // Статистика
    profile.statistics.useCount = 0;
    profile.statistics.lastUsed = 0;
    profile.statistics.avgDuration = 0;
    profile.statistics.avgYield = 0;
    profile.statistics.successRate = 0;

    if (saveProfile(profile)) {
        return profile.id;
    }

    return "";
}

// ============================================================================
// Экспорт/Импорт профилей
// ============================================================================

String exportProfileToJSON(const String& id) {
    Profile profile;
    if (!loadProfile(id, profile)) {
        Serial.printf("Ошибка: профиль не найден для экспорта: %s\n", id.c_str());
        return "";
    }

    DynamicJsonDocument doc(8192);

    // Используем ту же структуру, что и при сохранении
    doc["id"] = profile.id;

    JsonObject metadata = doc.createNestedObject("metadata");
    metadata["name"] = profile.metadata.name;
    metadata["description"] = profile.metadata.description;
    metadata["category"] = profile.metadata.category;

    JsonArray tags = metadata.createNestedArray("tags");
    for (const auto& tag : profile.metadata.tags) {
        tags.add(tag);
    }

    metadata["created"] = profile.metadata.created;
    metadata["updated"] = profile.metadata.updated;
    metadata["author"] = profile.metadata.author;
    metadata["isBuiltin"] = profile.metadata.isBuiltin;

    JsonObject parameters = doc.createNestedObject("parameters");
    parameters["mode"] = profile.parameters.mode;
    parameters["model"] = profile.parameters.model;

    JsonObject heater = parameters.createNestedObject("heater");
    heater["maxPower"] = profile.parameters.heater.maxPower;
    heater["autoMode"] = profile.parameters.heater.autoMode;
    heater["pidKp"] = profile.parameters.heater.pidKp;
    heater["pidKi"] = profile.parameters.heater.pidKi;
    heater["pidKd"] = profile.parameters.heater.pidKd;

    JsonObject rectification = parameters.createNestedObject("rectification");
    rectification["stabilizationMin"] = profile.parameters.rectification.stabilizationMin;
    rectification["headsVolume"] = profile.parameters.rectification.headsVolume;
    rectification["bodyVolume"] = profile.parameters.rectification.bodyVolume;
    rectification["tailsVolume"] = profile.parameters.rectification.tailsVolume;
    rectification["headsSpeed"] = profile.parameters.rectification.headsSpeed;
    rectification["bodySpeed"] = profile.parameters.rectification.bodySpeed;
    rectification["tailsSpeed"] = profile.parameters.rectification.tailsSpeed;
    rectification["purgeMin"] = profile.parameters.rectification.purgeMin;

    JsonObject distillation = parameters.createNestedObject("distillation");
    distillation["headsVolume"] = profile.parameters.distillation.headsVolume;
    distillation["targetVolume"] = profile.parameters.distillation.targetVolume;
    distillation["speed"] = profile.parameters.distillation.speed;
    distillation["endTemp"] = profile.parameters.distillation.endTemp;

    JsonObject temperatures = parameters.createNestedObject("temperatures");
    temperatures["maxCube"] = profile.parameters.temperatures.maxCube;
    temperatures["maxColumn"] = profile.parameters.temperatures.maxColumn;
    temperatures["headsEnd"] = profile.parameters.temperatures.headsEnd;
    temperatures["bodyStart"] = profile.parameters.temperatures.bodyStart;
    temperatures["bodyEnd"] = profile.parameters.temperatures.bodyEnd;

    JsonObject safety = parameters.createNestedObject("safety");
    safety["maxRuntime"] = profile.parameters.safety.maxRuntime;
    safety["waterFlowMin"] = profile.parameters.safety.waterFlowMin;
    safety["pressureMax"] = profile.parameters.safety.pressureMax;

    JsonObject statistics = doc.createNestedObject("statistics");
    statistics["useCount"] = profile.statistics.useCount;
    statistics["lastUsed"] = profile.statistics.lastUsed;
    statistics["avgDuration"] = profile.statistics.avgDuration;
    statistics["avgYield"] = profile.statistics.avgYield;
    statistics["successRate"] = profile.statistics.successRate;

    String json;
    serializeJson(doc, json);

    Serial.printf("Профиль экспортирован: %s (%d байт)\n", profile.metadata.name.c_str(), json.length());
    return json;
}

String exportAllProfilesToJSON(bool includeBuiltin) {
    std::vector<ProfileListItem> profiles = getProfileList();

    DynamicJsonDocument doc(32768); // 32 КБ для массива профилей
    JsonArray array = doc.to<JsonArray>();

    int exported = 0;
    for (const auto& item : profiles) {
        // Пропустить встроенные если не требуется
        if (!includeBuiltin && item.isBuiltin) {
            continue;
        }

        // Загрузить полный профиль
        Profile profile;
        if (loadProfile(item.id, profile)) {
            JsonObject obj = array.createNestedObject();

            obj["id"] = profile.id;

            JsonObject metadata = obj.createNestedObject("metadata");
            metadata["name"] = profile.metadata.name;
            metadata["description"] = profile.metadata.description;
            metadata["category"] = profile.metadata.category;

            JsonArray tags = metadata.createNestedArray("tags");
            for (const auto& tag : profile.metadata.tags) {
                tags.add(tag);
            }

            metadata["created"] = profile.metadata.created;
            metadata["updated"] = profile.metadata.updated;
            metadata["author"] = profile.metadata.author;
            metadata["isBuiltin"] = profile.metadata.isBuiltin;

            JsonObject parameters = obj.createNestedObject("parameters");
            parameters["mode"] = profile.parameters.mode;
            parameters["model"] = profile.parameters.model;

            JsonObject heater = parameters.createNestedObject("heater");
            heater["maxPower"] = profile.parameters.heater.maxPower;
            heater["autoMode"] = profile.parameters.heater.autoMode;
            heater["pidKp"] = profile.parameters.heater.pidKp;
            heater["pidKi"] = profile.parameters.heater.pidKi;
            heater["pidKd"] = profile.parameters.heater.pidKd;

            JsonObject rectification = parameters.createNestedObject("rectification");
            rectification["stabilizationMin"] = profile.parameters.rectification.stabilizationMin;
            rectification["headsVolume"] = profile.parameters.rectification.headsVolume;
            rectification["bodyVolume"] = profile.parameters.rectification.bodyVolume;
            rectification["tailsVolume"] = profile.parameters.rectification.tailsVolume;
            rectification["headsSpeed"] = profile.parameters.rectification.headsSpeed;
            rectification["bodySpeed"] = profile.parameters.rectification.bodySpeed;
            rectification["tailsSpeed"] = profile.parameters.rectification.tailsSpeed;
            rectification["purgeMin"] = profile.parameters.rectification.purgeMin;

            JsonObject distillation = parameters.createNestedObject("distillation");
            distillation["headsVolume"] = profile.parameters.distillation.headsVolume;
            distillation["targetVolume"] = profile.parameters.distillation.targetVolume;
            distillation["speed"] = profile.parameters.distillation.speed;
            distillation["endTemp"] = profile.parameters.distillation.endTemp;

            JsonObject temperatures = parameters.createNestedObject("temperatures");
            temperatures["maxCube"] = profile.parameters.temperatures.maxCube;
            temperatures["maxColumn"] = profile.parameters.temperatures.maxColumn;
            temperatures["headsEnd"] = profile.parameters.temperatures.headsEnd;
            temperatures["bodyStart"] = profile.parameters.temperatures.bodyStart;
            temperatures["bodyEnd"] = profile.parameters.temperatures.bodyEnd;

            JsonObject safety = parameters.createNestedObject("safety");
            safety["maxRuntime"] = profile.parameters.safety.maxRuntime;
            safety["waterFlowMin"] = profile.parameters.safety.waterFlowMin;
            safety["pressureMax"] = profile.parameters.safety.pressureMax;

            JsonObject statistics = obj.createNestedObject("statistics");
            statistics["useCount"] = profile.statistics.useCount;
            statistics["lastUsed"] = profile.statistics.lastUsed;
            statistics["avgDuration"] = profile.statistics.avgDuration;
            statistics["avgYield"] = profile.statistics.avgYield;
            statistics["successRate"] = profile.statistics.successRate;

            exported++;
        }
    }

    String json;
    serializeJson(doc, json);

    Serial.printf("Экспортировано профилей: %d (%d байт)\n", exported, json.length());
    return json;
}

String importProfileFromJSON(const String& jsonStr) {
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        Serial.printf("Ошибка парсинга JSON при импорте: %s\n", error.c_str());
        return "";
    }

    Profile profile;

    // Генерируем новый ID на основе текущего времени
    uint32_t now = millis() / 1000;
    profile.id = String(now);

    // Метаданные
    profile.metadata.name = doc["metadata"]["name"].as<String>();
    profile.metadata.description = doc["metadata"]["description"].as<String>();
    profile.metadata.category = doc["metadata"]["category"].as<String>();

    profile.metadata.tags.clear();
    JsonArray tags = doc["metadata"]["tags"];
    for (JsonVariant tag : tags) {
        profile.metadata.tags.push_back(tag.as<String>());
    }

    profile.metadata.created = now; // Новое время создания
    profile.metadata.updated = now;
    profile.metadata.author = doc["metadata"]["author"] | "imported";
    profile.metadata.isBuiltin = false; // Импортированные профили не встроенные

    // Параметры
    profile.parameters.mode = doc["parameters"]["mode"].as<String>();
    profile.parameters.model = doc["parameters"]["model"] | "classic";

    profile.parameters.heater.maxPower = doc["parameters"]["heater"]["maxPower"];
    profile.parameters.heater.autoMode = doc["parameters"]["heater"]["autoMode"];
    profile.parameters.heater.pidKp = doc["parameters"]["heater"]["pidKp"];
    profile.parameters.heater.pidKi = doc["parameters"]["heater"]["pidKi"];
    profile.parameters.heater.pidKd = doc["parameters"]["heater"]["pidKd"];

    profile.parameters.rectification.stabilizationMin = doc["parameters"]["rectification"]["stabilizationMin"];
    profile.parameters.rectification.headsVolume = doc["parameters"]["rectification"]["headsVolume"];
    profile.parameters.rectification.bodyVolume = doc["parameters"]["rectification"]["bodyVolume"];
    profile.parameters.rectification.tailsVolume = doc["parameters"]["rectification"]["tailsVolume"];
    profile.parameters.rectification.headsSpeed = doc["parameters"]["rectification"]["headsSpeed"];
    profile.parameters.rectification.bodySpeed = doc["parameters"]["rectification"]["bodySpeed"];
    profile.parameters.rectification.tailsSpeed = doc["parameters"]["rectification"]["tailsSpeed"];
    profile.parameters.rectification.purgeMin = doc["parameters"]["rectification"]["purgeMin"];

    profile.parameters.distillation.headsVolume = doc["parameters"]["distillation"]["headsVolume"];
    profile.parameters.distillation.targetVolume = doc["parameters"]["distillation"]["targetVolume"];
    profile.parameters.distillation.speed = doc["parameters"]["distillation"]["speed"];
    profile.parameters.distillation.endTemp = doc["parameters"]["distillation"]["endTemp"];

    profile.parameters.temperatures.maxCube = doc["parameters"]["temperatures"]["maxCube"];
    profile.parameters.temperatures.maxColumn = doc["parameters"]["temperatures"]["maxColumn"];
    profile.parameters.temperatures.headsEnd = doc["parameters"]["temperatures"]["headsEnd"];
    profile.parameters.temperatures.bodyStart = doc["parameters"]["temperatures"]["bodyStart"];
    profile.parameters.temperatures.bodyEnd = doc["parameters"]["temperatures"]["bodyEnd"];

    profile.parameters.safety.maxRuntime = doc["parameters"]["safety"]["maxRuntime"];
    profile.parameters.safety.waterFlowMin = doc["parameters"]["safety"]["waterFlowMin"];
    profile.parameters.safety.pressureMax = doc["parameters"]["safety"]["pressureMax"];

    // Сбросить статистику для импортированного профиля
    profile.statistics.useCount = 0;
    profile.statistics.lastUsed = 0;
    profile.statistics.avgDuration = 0;
    profile.statistics.avgYield = 0;
    profile.statistics.successRate = 0;

    if (saveProfile(profile)) {
        Serial.printf("Профиль импортирован: %s (новый ID: %s)\n",
                      profile.metadata.name.c_str(), profile.id.c_str());
        return profile.id;
    }

    return "";
}

uint16_t importProfilesFromJSON(const String& jsonStr) {
    DynamicJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        Serial.printf("Ошибка парсинга JSON массива при импорте: %s\n", error.c_str());
        return 0;
    }

    if (!doc.is<JsonArray>()) {
        Serial.println("Ошибка: JSON не является массивом");
        return 0;
    }

    JsonArray array = doc.as<JsonArray>();
    uint16_t imported = 0;

    for (JsonObject obj : array) {
        // Сериализуем каждый объект обратно в строку для передачи в importProfileFromJSON
        String profileJson;
        serializeJson(obj, profileJson);

        if (!importProfileFromJSON(profileJson).isEmpty()) {
            imported++;
        }
    }

    Serial.printf("Импортировано профилей: %d из %d\n", imported, array.size());
    return imported;
}
