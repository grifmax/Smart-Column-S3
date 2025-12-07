#include "history.h"
#include <FS.h>
#include <algorithm>

// Глобальный экземпляр рекордера
ProcessRecorder processRecorder;

// ============================================================================
// Инициализация системы истории
// ============================================================================

bool initHistory() {
    Serial.println("Инициализация системы истории процессов...");

    // Проверить, смонтирована ли SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("Ошибка: не удалось инициализировать SPIFFS");
        return false;
    }

    // Проверить существование директории /history
    if (!SPIFFS.exists(HISTORY_DIR)) {
        Serial.println("Создание директории /history");
        // SPIFFS не требует явного создания директорий
        // Они создаются автоматически при сохранении файла
    }

    // Провести ротацию файлов (удалить лишние)
    rotateHistory();

    Serial.println("Система истории инициализирована");
    Serial.printf("Файлов в истории: %d\n", getHistoryCount());
    Serial.printf("Общий размер: %d байт\n", getHistorySize());

    return true;
}

// ============================================================================
// Сохранение процесса в историю
// ============================================================================

bool saveProcessHistory(const ProcessHistory& history) {
    String filename = String(HISTORY_DIR) + "/process_" + history.id + ".json";

    Serial.printf("Сохранение процесса в историю: %s\n", filename.c_str());

    File file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("Ошибка: не удалось создать файл истории");
        return false;
    }

    // Создать JSON документ
    DynamicJsonDocument doc(32768);  // 32 КБ для полного процесса

    // Метаданные
    doc["id"] = history.id;
    doc["version"] = history.version;

    JsonObject metadata = doc.createNestedObject("metadata");
    metadata["startTime"] = history.metadata.startTime;
    metadata["endTime"] = history.metadata.endTime;
    metadata["duration"] = history.metadata.duration;
    metadata["completedSuccessfully"] = history.metadata.completedSuccessfully;
    metadata["deviceId"] = history.metadata.deviceId;

    // Информация о процессе
    JsonObject process = doc.createNestedObject("process");
    process["type"] = history.process.type;
    process["mode"] = history.process.mode;
    process["profile"] = history.process.profile;

    // Параметры
    JsonObject parameters = doc.createNestedObject("parameters");
    parameters["targetPower"] = history.parameters.targetPower;
    parameters["headVolume"] = history.parameters.headVolume;
    parameters["bodyVolume"] = history.parameters.bodyVolume;
    parameters["tailVolume"] = history.parameters.tailVolume;
    parameters["pumpSpeedHead"] = history.parameters.pumpSpeedHead;
    parameters["pumpSpeedBody"] = history.parameters.pumpSpeedBody;
    parameters["stabilizationTime"] = history.parameters.stabilizationTime;
    parameters["wattControlEnabled"] = history.parameters.wattControlEnabled;
    parameters["smartDecrementEnabled"] = history.parameters.smartDecrementEnabled;

    // Метрики
    JsonObject metrics = doc.createNestedObject("metrics");

    JsonObject temps = metrics.createNestedObject("temperatures");
    JsonObject cube = temps.createNestedObject("cube");
    cube["min"] = history.metrics.cube.min;
    cube["max"] = history.metrics.cube.max;
    cube["avg"] = history.metrics.cube.avg;
    cube["final"] = history.metrics.cube.final;

    JsonObject columnBottom = temps.createNestedObject("columnBottom");
    columnBottom["min"] = history.metrics.columnBottom.min;
    columnBottom["max"] = history.metrics.columnBottom.max;
    columnBottom["avg"] = history.metrics.columnBottom.avg;
    columnBottom["final"] = history.metrics.columnBottom.final;

    JsonObject columnTop = temps.createNestedObject("columnTop");
    columnTop["min"] = history.metrics.columnTop.min;
    columnTop["max"] = history.metrics.columnTop.max;
    columnTop["avg"] = history.metrics.columnTop.avg;
    columnTop["final"] = history.metrics.columnTop.final;

    JsonObject deflegmator = temps.createNestedObject("deflegmator");
    deflegmator["min"] = history.metrics.deflegmator.min;
    deflegmator["max"] = history.metrics.deflegmator.max;
    deflegmator["avg"] = history.metrics.deflegmator.avg;
    deflegmator["final"] = history.metrics.deflegmator.final;

    JsonObject power = metrics.createNestedObject("power");
    power["energyUsed"] = history.metrics.energyUsed;
    power["avgPower"] = history.metrics.avgPower;
    power["peakPower"] = history.metrics.peakPower;

    JsonObject pump = metrics.createNestedObject("pump");
    pump["totalVolume"] = history.metrics.totalVolume;
    pump["avgSpeed"] = history.metrics.avgSpeed;

    // Фазы
    JsonArray phases = doc.createNestedArray("phases");
    for (const auto& phase : history.phases) {
        JsonObject p = phases.createNestedObject();
        p["name"] = phase.name;
        p["startTime"] = phase.startTime;
        p["endTime"] = phase.endTime;
        p["duration"] = phase.duration;
        p["startTemp"] = phase.startTemp;
        p["endTemp"] = phase.endTemp;
        p["volume"] = phase.volume;
        p["avgSpeed"] = phase.avgSpeed;
    }

    // Временные ряды
    JsonObject timeseries = doc.createNestedObject("timeseries");
    timeseries["interval"] = TIMESERIES_INTERVAL;
    JsonArray data = timeseries.createNestedArray("data");

    // Ограничить количество точек для экономии памяти
    size_t step = 1;
    if (history.timeseries.size() > MAX_TIMESERIES_POINTS) {
        step = history.timeseries.size() / MAX_TIMESERIES_POINTS + 1;
    }

    for (size_t i = 0; i < history.timeseries.size(); i += step) {
        const auto& point = history.timeseries[i];
        JsonObject p = data.createNestedObject();
        p["time"] = point.time;
        p["cube"] = point.cube;
        p["columnTop"] = point.columnTop;
        p["columnBottom"] = point.columnBottom;
        p["deflegmator"] = point.deflegmator;
        p["power"] = point.power;
        p["voltage"] = point.voltage;
        p["current"] = point.current;
        p["pumpSpeed"] = point.pumpSpeed;
    }

    // Результаты
    JsonObject results = doc.createNestedObject("results");
    results["headsCollected"] = history.results.headsCollected;
    results["bodyCollected"] = history.results.bodyCollected;
    results["tailsCollected"] = history.results.tailsCollected;
    results["totalCollected"] = history.results.totalCollected;
    results["status"] = history.results.status;

    JsonArray errors = results.createNestedArray("errors");
    for (const auto& error : history.results.errors) {
        JsonObject e = errors.createNestedObject();
        e["time"] = error.time;
        e["message"] = error.message;
        e["severity"] = error.severity;
    }

    JsonArray warnings = results.createNestedArray("warnings");
    for (const auto& warning : history.results.warnings) {
        JsonObject w = warnings.createNestedObject();
        w["time"] = warning.time;
        w["message"] = warning.message;
        w["severity"] = warning.severity;
    }

    // Заметки
    doc["notes"] = history.notes;

    // Сериализовать в файл
    if (serializeJson(doc, file) == 0) {
        Serial.println("Ошибка: не удалось записать JSON");
        file.close();
        return false;
    }

    file.close();

    Serial.printf("Процесс сохранён (%d байт)\n", file.size());

    // Провести ротацию
    rotateHistory();

    return true;
}

// ============================================================================
// Загрузка процесса из истории
// ============================================================================

bool loadProcessHistory(const String& id, ProcessHistory& history) {
    String filename = String(HISTORY_DIR) + "/process_" + id + ".json";

    if (!SPIFFS.exists(filename)) {
        Serial.printf("Ошибка: файл не найден: %s\n", filename.c_str());
        return false;
    }

    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        Serial.println("Ошибка: не удалось открыть файл");
        return false;
    }

    DynamicJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("Ошибка парсинга JSON: %s\n", error.c_str());
        return false;
    }

    // Загрузить данные из JSON в структуру
    history.id = doc["id"].as<String>();
    history.version = doc["version"].as<String>();

    history.metadata.startTime = doc["metadata"]["startTime"];
    history.metadata.endTime = doc["metadata"]["endTime"];
    history.metadata.duration = doc["metadata"]["duration"];
    history.metadata.completedSuccessfully = doc["metadata"]["completedSuccessfully"];
    history.metadata.deviceId = doc["metadata"]["deviceId"].as<String>();

    history.process.type = doc["process"]["type"].as<String>();
    history.process.mode = doc["process"]["mode"].as<String>();
    history.process.profile = doc["process"]["profile"].as<String>();

    history.parameters.targetPower = doc["parameters"]["targetPower"];
    history.parameters.headVolume = doc["parameters"]["headVolume"];
    history.parameters.bodyVolume = doc["parameters"]["bodyVolume"];
    history.parameters.tailVolume = doc["parameters"]["tailVolume"];
    history.parameters.pumpSpeedHead = doc["parameters"]["pumpSpeedHead"];
    history.parameters.pumpSpeedBody = doc["parameters"]["pumpSpeedBody"];
    history.parameters.stabilizationTime = doc["parameters"]["stabilizationTime"];
    history.parameters.wattControlEnabled = doc["parameters"]["wattControlEnabled"];
    history.parameters.smartDecrementEnabled = doc["parameters"]["smartDecrementEnabled"];

    // Загрузить метрики
    history.metrics.cube.min = doc["metrics"]["temperatures"]["cube"]["min"];
    history.metrics.cube.max = doc["metrics"]["temperatures"]["cube"]["max"];
    history.metrics.cube.avg = doc["metrics"]["temperatures"]["cube"]["avg"];
    history.metrics.cube.final = doc["metrics"]["temperatures"]["cube"]["final"];

    history.metrics.columnBottom.min = doc["metrics"]["temperatures"]["columnBottom"]["min"];
    history.metrics.columnBottom.max = doc["metrics"]["temperatures"]["columnBottom"]["max"];
    history.metrics.columnBottom.avg = doc["metrics"]["temperatures"]["columnBottom"]["avg"];
    history.metrics.columnBottom.final = doc["metrics"]["temperatures"]["columnBottom"]["final"];

    history.metrics.columnTop.min = doc["metrics"]["temperatures"]["columnTop"]["min"];
    history.metrics.columnTop.max = doc["metrics"]["temperatures"]["columnTop"]["max"];
    history.metrics.columnTop.avg = doc["metrics"]["temperatures"]["columnTop"]["avg"];
    history.metrics.columnTop.final = doc["metrics"]["temperatures"]["columnTop"]["final"];

    history.metrics.deflegmator.min = doc["metrics"]["temperatures"]["deflegmator"]["min"];
    history.metrics.deflegmator.max = doc["metrics"]["temperatures"]["deflegmator"]["max"];
    history.metrics.deflegmator.avg = doc["metrics"]["temperatures"]["deflegmator"]["avg"];
    history.metrics.deflegmator.final = doc["metrics"]["temperatures"]["deflegmator"]["final"];

    history.metrics.energyUsed = doc["metrics"]["power"]["energyUsed"];
    history.metrics.avgPower = doc["metrics"]["power"]["avgPower"];
    history.metrics.peakPower = doc["metrics"]["power"]["peakPower"];

    history.metrics.totalVolume = doc["metrics"]["pump"]["totalVolume"];
    history.metrics.avgSpeed = doc["metrics"]["pump"]["avgSpeed"];

    // Загрузить фазы
    history.phases.clear();
    JsonArray phases = doc["phases"];
    for (JsonObject phase : phases) {
        ProcessPhase p;
        p.name = phase["name"].as<String>();
        p.startTime = phase["startTime"];
        p.endTime = phase["endTime"];
        p.duration = phase["duration"];
        p.startTemp = phase["startTemp"];
        p.endTemp = phase["endTemp"];
        p.volume = phase["volume"];
        p.avgSpeed = phase["avgSpeed"];
        history.phases.push_back(p);
    }

    // Загрузить временные ряды
    history.timeseries.clear();
    JsonArray data = doc["timeseries"]["data"];
    for (JsonObject point : data) {
        TimeseriesPoint p;
        p.time = point["time"];
        p.cube = point["cube"];
        p.columnTop = point["columnTop"];
        p.columnBottom = point["columnBottom"];
        p.deflegmator = point["deflegmator"];
        p.power = point["power"];
        p.voltage = point["voltage"];
        p.current = point["current"];
        p.pumpSpeed = point["pumpSpeed"];
        history.timeseries.push_back(p);
    }

    // Загрузить результаты
    history.results.headsCollected = doc["results"]["headsCollected"];
    history.results.bodyCollected = doc["results"]["bodyCollected"];
    history.results.tailsCollected = doc["results"]["tailsCollected"];
    history.results.totalCollected = doc["results"]["totalCollected"];
    history.results.status = doc["results"]["status"].as<String>();

    history.results.errors.clear();
    JsonArray errors = doc["results"]["errors"];
    for (JsonObject error : errors) {
        ProcessWarning w;
        w.time = error["time"];
        w.message = error["message"].as<String>();
        w.severity = error["severity"].as<String>();
        history.results.errors.push_back(w);
    }

    history.results.warnings.clear();
    JsonArray warnings = doc["results"]["warnings"];
    for (JsonObject warning : warnings) {
        ProcessWarning w;
        w.time = warning["time"];
        w.message = warning["message"].as<String>();
        w.severity = warning["severity"].as<String>();
        history.results.warnings.push_back(w);
    }

    history.notes = doc["notes"].as<String>();

    Serial.printf("Процесс загружен: %s\n", id.c_str());
    return true;
}

// ============================================================================
// Получение списка всех процессов
// ============================================================================

std::vector<ProcessListItem> getProcessList() {
    std::vector<ProcessListItem> list;

    File root = SPIFFS.open(HISTORY_DIR);
    if (!root || !root.isDirectory()) {
        return list;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();

            // Проверить, что это файл процесса
            if (filename.startsWith("process_") && filename.endsWith(".json")) {
                // Быстрая загрузка только необходимых полей
                DynamicJsonDocument doc(1024);
                DeserializationError error = deserializeJson(doc, file);

                if (!error) {
                    ProcessListItem item;
                    item.id = doc["id"].as<String>();
                    item.type = doc["process"]["type"].as<String>();
                    item.startTime = doc["metadata"]["startTime"];
                    item.duration = doc["metadata"]["duration"];
                    item.status = doc["results"]["status"].as<String>();
                    item.totalVolume = doc["results"]["totalCollected"];

                    list.push_back(item);
                }
            }
        }
        file = root.openNextFile();
    }

    // Сортировать по времени (новые первые)
    std::sort(list.begin(), list.end(), [](const ProcessListItem& a, const ProcessListItem& b) {
        return a.startTime > b.startTime;
    });

    return list;
}

// ============================================================================
// Удаление процесса
// ============================================================================

bool deleteProcess(const String& id) {
    String filename = String(HISTORY_DIR) + "/process_" + id + ".json";

    if (!SPIFFS.exists(filename)) {
        Serial.printf("Файл не найден: %s\n", filename.c_str());
        return false;
    }

    if (SPIFFS.remove(filename)) {
        Serial.printf("Процесс удалён: %s\n", id.c_str());
        return true;
    }

    return false;
}

// ============================================================================
// Очистка всей истории
// ============================================================================

bool clearHistory() {
    File root = SPIFFS.open(HISTORY_DIR);
    if (!root || !root.isDirectory()) {
        return false;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();
            SPIFFS.remove(filename);
        }
        file = root.openNextFile();
    }

    Serial.println("Вся история очищена");
    return true;
}

// ============================================================================
// Ротация файлов истории
// ============================================================================

void rotateHistory() {
    std::vector<String> files;
    std::vector<size_t> sizes;
    size_t totalSize = 0;

    // Собрать список файлов и их размеры
    File root = SPIFFS.open(HISTORY_DIR);
    if (!root || !root.isDirectory()) {
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();
            if (filename.startsWith("process_") && filename.endsWith(".json")) {
                files.push_back(filename);
                sizes.push_back(file.size());
                totalSize += file.size();
            }
        }
        file = root.openNextFile();
    }

    // Сортировать по имени (timestamp, старые первые)
    std::sort(files.begin(), files.end());

    // Удалить старые файлы если превышен лимит по количеству
    while (files.size() > MAX_HISTORY_FILES) {
        Serial.printf("Удаление старого файла (превышен лимит): %s\n", files[0].c_str());
        SPIFFS.remove(files[0]);
        totalSize -= sizes[0];
        files.erase(files.begin());
        sizes.erase(sizes.begin());
    }

    // Удалить старые файлы если превышен лимит по размеру
    while (totalSize > MAX_HISTORY_SIZE && files.size() > 1) {
        Serial.printf("Удаление старого файла (превышен размер): %s\n", files[0].c_str());
        SPIFFS.remove(files[0]);
        totalSize -= sizes[0];
        files.erase(files.begin());
        sizes.erase(sizes.begin());
    }
}

// ============================================================================
// Вспомогательные функции
// ============================================================================

uint16_t getHistoryCount() {
    uint16_t count = 0;

    File root = SPIFFS.open(HISTORY_DIR);
    if (!root || !root.isDirectory()) {
        return 0;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();
            if (filename.startsWith("process_") && filename.endsWith(".json")) {
                count++;
            }
        }
        file = root.openNextFile();
    }

    return count;
}

size_t getHistorySize() {
    size_t totalSize = 0;

    File root = SPIFFS.open(HISTORY_DIR);
    if (!root || !root.isDirectory()) {
        return 0;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();
            if (filename.startsWith("process_") && filename.endsWith(".json")) {
                totalSize += file.size();
            }
        }
        file = root.openNextFile();
    }

    return totalSize;
}

// ============================================================================
// Экспорт в CSV
// ============================================================================

String exportProcessToCSV(const ProcessHistory& history) {
    String csv = "Time,Cube Temp,Column Top,Column Bottom,Deflegmator,Power,Voltage,Current,Pump Speed\n";

    for (const auto& point : history.timeseries) {
        csv += String(point.time) + ",";
        csv += String(point.cube, 1) + ",";
        csv += String(point.columnTop, 1) + ",";
        csv += String(point.columnBottom, 1) + ",";
        csv += String(point.deflegmator, 1) + ",";
        csv += String(point.power) + ",";
        csv += String(point.voltage, 1) + ",";
        csv += String(point.current, 2) + ",";
        csv += String(point.pumpSpeed) + "\n";
    }

    return csv;
}

// ============================================================================
// Экспорт в JSON
// ============================================================================

String exportProcessToJSON(const ProcessHistory& history) {
    DynamicJsonDocument doc(32768);

    // Используем ту же структуру, что и для сохранения
    doc["id"] = history.id;
    doc["version"] = history.version;
    // ... (аналогично saveProcessHistory)

    String json;
    serializeJson(doc, json);
    return json;
}

// ============================================================================
// ProcessRecorder - класс для записи процесса в реальном времени
// ============================================================================

ProcessRecorder::ProcessRecorder() : recording(false), lastTimeseriesTime(0) {
}

void ProcessRecorder::startRecording(const String& type, const String& mode) {
    currentHistory = ProcessHistory();
    recording = true;
    lastTimeseriesTime = 0;

    uint32_t now = millis() / 1000;  // или использовать NTP время
    currentHistory.id = String(now);
    currentHistory.version = "1.3.0";
    currentHistory.metadata.startTime = now;
    currentHistory.process.type = type;
    currentHistory.process.mode = mode;

    Serial.printf("Начата запись процесса: %s (%s)\n", type.c_str(), mode.c_str());
}

void ProcessRecorder::stopRecording(bool success) {
    if (!recording) return;

    uint32_t now = millis() / 1000;
    currentHistory.metadata.endTime = now;
    currentHistory.metadata.duration = now - currentHistory.metadata.startTime;
    currentHistory.metadata.completedSuccessfully = success;
    currentHistory.results.status = success ? "completed" : "stopped";

    // Вычислить метрики
    calculateMetrics();

    // Сохранить в SPIFFS
    saveProcessHistory(currentHistory);

    recording = false;
    Serial.println("Запись процесса завершена");
}

void ProcessRecorder::addTimeseriesPoint(const TimeseriesPoint& point) {
    if (!recording) return;

    uint32_t now = millis() / 1000;

    // Добавлять точку только если прошёл интервал
    if (now - lastTimeseriesTime >= TIMESERIES_INTERVAL) {
        currentHistory.timeseries.push_back(point);
        lastTimeseriesTime = now;
    }
}

void ProcessRecorder::setParameters(const ProcessParameters& params) {
    currentHistory.parameters = params;
}

void ProcessRecorder::addPhase(const ProcessPhase& phase) {
    currentHistory.phases.push_back(phase);
}

void ProcessRecorder::addWarning(const String& message, const String& severity) {
    ProcessWarning warning;
    warning.time = millis() / 1000;
    warning.message = message;
    warning.severity = severity;

    if (severity == "error") {
        currentHistory.results.errors.push_back(warning);
    } else {
        currentHistory.results.warnings.push_back(warning);
    }
}

void ProcessRecorder::setResults(const ProcessResults& results) {
    currentHistory.results = results;
}

void ProcessRecorder::setNotes(const String& notes) {
    currentHistory.notes = notes;
}

ProcessHistory& ProcessRecorder::getHistory() {
    return currentHistory;
}

void ProcessRecorder::calculateMetrics() {
    // Вычислить метрики из временных рядов
    if (currentHistory.timeseries.empty()) return;

    // Температуры куба
    float cubeMin = 999.0f, cubeMax = -999.0f, cubeSum = 0.0f;
    float colTopMin = 999.0f, colTopMax = -999.0f, colTopSum = 0.0f;
    float colBottomMin = 999.0f, colBottomMax = -999.0f, colBottomSum = 0.0f;
    float deflegMin = 999.0f, deflegMax = -999.0f, deflegSum = 0.0f;

    uint16_t powerSum = 0, powerPeak = 0;

    for (const auto& point : currentHistory.timeseries) {
        // Куб
        if (point.cube < cubeMin) cubeMin = point.cube;
        if (point.cube > cubeMax) cubeMax = point.cube;
        cubeSum += point.cube;

        // Царга верх
        if (point.columnTop < colTopMin) colTopMin = point.columnTop;
        if (point.columnTop > colTopMax) colTopMax = point.columnTop;
        colTopSum += point.columnTop;

        // Царга низ
        if (point.columnBottom < colBottomMin) colBottomMin = point.columnBottom;
        if (point.columnBottom > colBottomMax) colBottomMax = point.columnBottom;
        colBottomSum += point.columnBottom;

        // Дефлегматор
        if (point.deflegmator < deflegMin) deflegMin = point.deflegmator;
        if (point.deflegmator > deflegMax) deflegMax = point.deflegmator;
        deflegSum += point.deflegmator;

        // Мощность
        powerSum += point.power;
        if (point.power > powerPeak) powerPeak = point.power;
    }

    size_t count = currentHistory.timeseries.size();

    currentHistory.metrics.cube.min = cubeMin;
    currentHistory.metrics.cube.max = cubeMax;
    currentHistory.metrics.cube.avg = cubeSum / count;
    currentHistory.metrics.cube.final = currentHistory.timeseries.back().cube;

    currentHistory.metrics.columnTop.min = colTopMin;
    currentHistory.metrics.columnTop.max = colTopMax;
    currentHistory.metrics.columnTop.avg = colTopSum / count;
    currentHistory.metrics.columnTop.final = currentHistory.timeseries.back().columnTop;

    currentHistory.metrics.columnBottom.min = colBottomMin;
    currentHistory.metrics.columnBottom.max = colBottomMax;
    currentHistory.metrics.columnBottom.avg = colBottomSum / count;
    currentHistory.metrics.columnBottom.final = currentHistory.timeseries.back().columnBottom;

    currentHistory.metrics.deflegmator.min = deflegMin;
    currentHistory.metrics.deflegmator.max = deflegMax;
    currentHistory.metrics.deflegmator.avg = deflegSum / count;
    currentHistory.metrics.deflegmator.final = currentHistory.timeseries.back().deflegmator;

    currentHistory.metrics.avgPower = powerSum / count;
    currentHistory.metrics.peakPower = powerPeak;

    // Энергия = средняя мощность × время (в часах)
    currentHistory.metrics.energyUsed = (currentHistory.metrics.avgPower / 1000.0f) *
                                        (currentHistory.metadata.duration / 3600.0f);
}
