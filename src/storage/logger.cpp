/**
 * Smart-Column S3 - Logger
 */

#include "logger.h"

#include "../fs_compat.h"
#include <ArduinoJson.h>
#include <stdarg.h>
#include <time.h>
#include <vector>
#include <algorithm>

static File currentLogFile;
static char currentFilename[64] = "";
static uint32_t sessionStart = 0;
static uint32_t lastFlushMs = 0;

namespace {

constexpr size_t EVENT_BUFFER_SIZE = 128;
LogEvent recentEvents[EVENT_BUFFER_SIZE];
size_t recentEventCount = 0;
size_t recentEventWriteIndex = 0;
uint32_t nextEventSequence = 1;

bool hasValidRtc(time_t now, const tm& timeinfo) {
  return now >= 1704067200 && timeinfo.tm_year >= (2024 - 1900);
}

void buildLogFilename(char* buffer, size_t bufferSize) {
  tm timeinfo = {};
  const time_t now = time(nullptr);
  const bool rtcReady =
      localtime_r(&now, &timeinfo) != nullptr && hasValidRtc(now, timeinfo);

  char baseName[56];
  if (rtcReady) {
    snprintf(baseName, sizeof(baseName), "%s%04d%02d%02d_%02d%02d%02d",
             LOG_FILE_PREFIX, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
             timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
             timeinfo.tm_sec);
  } else {
    snprintf(baseName, sizeof(baseName), "%ssession_%010lu", LOG_FILE_PREFIX,
             millis());
  }

  for (uint16_t suffix = 0; suffix < 1000; ++suffix) {
    if (suffix == 0) {
      snprintf(buffer, bufferSize, "%s%s", baseName, LOG_FILE_EXT);
    } else {
      snprintf(buffer, bufferSize, "%s_%03u%s", baseName, suffix,
               LOG_FILE_EXT);
    }

    if (!LittleFS.exists(buffer)) {
      return;
    }
  }

  snprintf(buffer, bufferSize, "%ssession_%010lu_fallback%s", LOG_FILE_PREFIX,
           millis(), LOG_FILE_EXT);
}

void enforceRotation() {
  LOG_I("Logger: Checking for log rotation...");
  
  File root = LittleFS.open(LOG_FILE_PREFIX);
  if (!root || !root.isDirectory()) {
    return;
  }

  std::vector<String> logFiles;
  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    if (!file.isDirectory() && name.endsWith(LOG_FILE_EXT)) {
      // LittleFS file.name() может возвращать полный путь или только имя
      if (!name.startsWith(LOG_FILE_PREFIX)) {
          logFiles.push_back(String(LOG_FILE_PREFIX) + name);
      } else {
          logFiles.push_back(name);
      }
    }
    file = root.openNextFile();
  }
  root.close();

  // Сортировка по имени (формат ГГГГММДД_ЧЧММСС обеспечивает хронологический порядок)
  std::sort(logFiles.begin(), logFiles.end());

  if (logFiles.size() >= LOG_MAX_FILES) {
    int toDelete = logFiles.size() - LOG_MAX_FILES + 1;
    LOG_I("Logger: Rotation needed, deleting %d old files", toDelete);
    for (int i = 0; i < toDelete; ++i) {
      LOG_I("Logger: Deleting %s", logFiles[i].c_str());
      LittleFS.remove(logFiles[i]);
    }
  }
}

const char* getLevelToken(uint8_t level) {
  switch (level) {
    case 1:
      return "warning";
    case 2:
      return "error";
    default:
      return "info";
  }
}

void pushRecentEvent(const LogEvent& rawEvent) {
  LogEvent event = rawEvent;
  if (event.timestamp == 0) {
    event.timestamp = millis();
  }
  event.sequence = nextEventSequence++;

  recentEvents[recentEventWriteIndex] = event;
  recentEventWriteIndex = (recentEventWriteIndex + 1) % EVENT_BUFFER_SIZE;
  if (recentEventCount < EVENT_BUFFER_SIZE) {
    recentEventCount++;
  }
}

const LogEvent* getEventAt(size_t orderedIndex) {
  if (orderedIndex >= recentEventCount) {
    return nullptr;
  }

  const size_t startIndex =
      (recentEventWriteIndex + EVENT_BUFFER_SIZE - recentEventCount) %
      EVENT_BUFFER_SIZE;
  const size_t actualIndex = (startIndex + orderedIndex) % EVENT_BUFFER_SIZE;
  return &recentEvents[actualIndex];
}

} // namespace

namespace Logger {

bool init() {
  LOG_I("Logger: Initializing...");

  if (!LittleFS.begin(true)) {
    LOG_E("Logger: LittleFS mount failed!");
    return false;
  }

  if (!LittleFS.exists(LOG_FILE_PREFIX)) {
    LittleFS.mkdir(LOG_FILE_PREFIX);
  }

  LOG_I("Logger: Ready");
  return true;
}

void startSession() {
  if (currentLogFile) {
    currentLogFile.close();
  }

  // Ротация перед созданием нового файла (Analysis Step 15)
  enforceRotation();

  sessionStart = millis();
  lastFlushMs = sessionStart;
  currentFilename[0] = '\0';

  buildLogFilename(currentFilename, sizeof(currentFilename));
  currentLogFile = LittleFS.open(currentFilename, FILE_WRITE);

  if (!currentLogFile) {
    LOG_E("Logger: Failed to open %s", currentFilename);
    currentFilename[0] = '\0';
    return;
  }

  currentLogFile.println("timestamp,t_cube,t_col_bot,t_col_top,t_reflux,t_tsa,"
                         "t_water_in,t_water_out,"
                         "p_cube,p_atm,abv,power,speed,volume,state,event");

  LOG_I("Logger: Session started -> %s", currentFilename);
}

void stopSession() {
  if (currentLogFile) {
    currentLogFile.flush();
    currentLogFile.close();
    LOG_I("Logger: Session stopped");
  }
}

bool isSessionActive() {
  return static_cast<bool>(currentLogFile);
}

void writeData(const SystemState& state) {
  if (!currentLogFile) {
    return;
  }

  char line[256];
  snprintf(line, sizeof(line),
           "%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.0f,%.1f,%."
           "1f,%d,",
           millis() - sessionStart, state.temps.cube, state.temps.columnBottom,
           state.temps.columnTop, state.temps.reflux, state.temps.tsa,
           state.temps.waterIn, state.temps.waterOut, state.pressure.cube,
           state.pressure.atmosphere, state.hydrometer.abv, state.power.power,
           state.pump.speedMlPerHour, state.pump.totalVolumeMl,
           static_cast<int>(state.rectPhase));

  currentLogFile.println(line);

  if (millis() - lastFlushMs > 10000) {
    currentLogFile.flush();
    lastFlushMs = millis();
  }
}

void log(const LogEvent& event) {
  LOG_I("Event: %s", event.message);
  pushRecentEvent(event);

  if (currentLogFile) {
    char line[128];
    const uint32_t timestamp = sessionStart > 0 ? millis() - sessionStart : 0;
    snprintf(line, sizeof(line), "%lu,,,,,,,,,,,,,%d,%s", timestamp,
             event.level, event.message);
    currentLogFile.println(line);
    currentLogFile.flush();
    lastFlushMs = millis();
  }
}

void logf(uint8_t level, const char* format, ...) {
  if (!format || !format[0]) {
    return;
  }

  LogEvent event{};
  event.timestamp = millis();
  event.level = level;

  va_list args;
  va_start(args, format);
  vsnprintf(event.message, sizeof(event.message), format, args);
  va_end(args);

  log(event);
}

String getLogsList() {
  String result = "[";
  File root = LittleFS.open(LOG_FILE_PREFIX);

  if (!root || !root.isDirectory()) {
    return "[]";
  }

  File file = root.openNextFile();
  bool first = true;

  while (file) {
    if (!file.isDirectory()) {
      if (!first) {
        result += ",";
      }
      result += "\"" + String(file.name()) + "\"";
      first = false;
    }
    file = root.openNextFile();
  }

  result += "]";
  return result;
}

String readLog(const char* filename) {
  File file = LittleFS.open(filename, FILE_READ);
  if (!file) {
    return "";
  }

  String content = file.readString();
  file.close();
  return content;
}

bool deleteLog(const char* filename) {
  if (LittleFS.remove(filename)) {
    LOG_I("Logger: Deleted %s", filename);
    return true;
  }

  LOG_E("Logger: Failed to delete %s", filename);
  return false;
}

const char* getCurrentLogFile() {
  return currentFilename[0] ? currentFilename : nullptr;
}

String getRecentEventsJson(uint16_t limit, uint32_t sinceSequence) {
  const size_t effectiveLimit =
      limit == 0 ? recentEventCount
                 : (recentEventCount < static_cast<size_t>(limit)
                        ? recentEventCount
                        : static_cast<size_t>(limit));
  size_t matchedCount = 0;

  for (size_t i = 0; i < recentEventCount; ++i) {
    const LogEvent* event = getEventAt(i);
    if (!event || event->sequence <= sinceSequence) {
      continue;
    }
    matchedCount++;
  }

  const size_t returnedCount =
      sinceSequence == 0 ? min(matchedCount, effectiveLimit) : matchedCount;
  
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  JsonArray events = root["events"].to<JsonArray>();

  const size_t skipCount =
      (sinceSequence == 0 && matchedCount > returnedCount)
          ? (matchedCount - returnedCount)
          : 0;
  size_t skipped = 0;
  uint32_t latestSequence = 0;

  for (size_t i = 0; i < recentEventCount; ++i) {
    const LogEvent* event = getEventAt(i);
    if (!event) {
      continue;
    }

    latestSequence = max(latestSequence, event->sequence);
    if (event->sequence <= sinceSequence) {
      continue;
    }
    if (skipped < skipCount) {
      skipped++;
      continue;
    }

    JsonObject item = events.add<JsonObject>();
    item["seq"] = event->sequence;
    item["timestamp"] = event->timestamp;
    item["level"] = event->level;
    item["levelStr"] = getLevelToken(event->level);
    item["message"] = event->message;
  }

  root["count"] = events.size();
  root["nextSeq"] = latestSequence;

  String result;
  serializeJson(doc, result);
  return result;
}

String exportRecentEventsCsv(uint16_t limit) {
  String csv = "sequence,timestamp_ms,level,message\n";
  const size_t effectiveLimit =
      limit == 0 ? recentEventCount
                 : (recentEventCount < static_cast<size_t>(limit)
                        ? recentEventCount
                        : static_cast<size_t>(limit));
  const size_t startIndex =
      recentEventCount > effectiveLimit ? (recentEventCount - effectiveLimit) : 0;

  for (size_t i = startIndex; i < recentEventCount; ++i) {
    const LogEvent* event = getEventAt(i);
    if (!event) {
      continue;
    }

    String message = event->message;
    message.replace("\"", "\"\"");
    csv += String(event->sequence);
    csv += ",";
    csv += String(event->timestamp);
    csv += ",";
    csv += getLevelToken(event->level);
    csv += ",\"";
    csv += message;
    csv += "\"\n";
  }

  return csv;
}

void clearRecentEvents() {
  recentEventCount = 0;
  recentEventWriteIndex = 0;
}

} // namespace Logger
