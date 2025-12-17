/**
 * Smart-Column S3 - Логгер данных
 *
 * Запись данных на SPIFFS в формате CSV
 */

#include "logger.h"
#include "../fs_compat.h"
#include <time.h>

static File currentLogFile;
static char currentFilename[64];
static uint32_t sessionStart = 0;

namespace Logger {

bool init() {
  LOG_I("Logger: Initializing...");

  if (!SPIFFS.begin(true)) {
    LOG_E("Logger: SPIFFS mount failed!");
    return false;
  }

  // Создать директорию для логов
  if (!SPIFFS.exists(LOG_FILE_PREFIX)) {
    SPIFFS.mkdir(LOG_FILE_PREFIX);
  }

  LOG_I("Logger: Ready");
  return true;
}

void startSession() {
  sessionStart = millis();

  // Сформировать имя файла: /logs/YYYYMMDD_HHMMSS.csv
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  snprintf(currentFilename, sizeof(currentFilename),
           "%s%04d%02d%02d_%02d%02d%02d%s", LOG_FILE_PREFIX,
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, LOG_FILE_EXT);

  // Открыть файл для записи
  currentLogFile = SPIFFS.open(currentFilename, FILE_WRITE);

  if (!currentLogFile) {
    LOG_E("Logger: Failed to open %s", currentFilename);
    return;
  }

  // Записать заголовок CSV
  currentLogFile.println("timestamp,t_cube,t_col_bot,t_col_top,t_reflux,t_tsa,"
                         "t_water_in,t_water_out,"
                         "p_cube,p_atm,abv,power,speed,volume,state,event");

  LOG_I("Logger: Session started -> %s", currentFilename);
}

void stopSession() {
  if (currentLogFile) {
    currentLogFile.close();
    LOG_I("Logger: Session stopped");
  }
}

void writeData(const SystemState &state) {
  if (!currentLogFile)
    return;

  // Формат: timestamp,temps...,pressure,abv,power,speed,volume,state,event
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

  // Периодически синхронизировать
  static uint32_t lastFlush = 0;
  if (millis() - lastFlush > 10000) {
    currentLogFile.flush();
    lastFlush = millis();
  }
}

void log(const LogEvent &event) {
  // Записать событие
  LOG_I("Event: %s", event.message);

  if (currentLogFile) {
    char line[128];
    snprintf(line, sizeof(line), "%lu,,,,,,,,,,,,,%d,%s", event.timestamp,
             event.level, event.message);
    currentLogFile.println(line);
  }
}

String getLogsList() {
  String result = "[";
  File root = SPIFFS.open(LOG_FILE_PREFIX);

  if (!root || !root.isDirectory()) {
    return "[]";
  }

  File file = root.openNextFile();
  bool first = true;

  while (file) {
    if (!file.isDirectory()) {
      if (!first)
        result += ",";
      result += "\"" + String(file.name()) + "\"";
      first = false;
    }
    file = root.openNextFile();
  }

  result += "]";
  return result;
}

String readLog(const char *filename) {
  File file = SPIFFS.open(filename, FILE_READ);
  if (!file) {
    return "";
  }

  String content = file.readString();
  file.close();
  return content;
}

bool deleteLog(const char *filename) {
  if (SPIFFS.remove(filename)) {
    LOG_I("Logger: Deleted %s", filename);
    return true;
  } else {
    LOG_E("Logger: Failed to delete %s", filename);
    return false;
  }
}

} // namespace Logger
