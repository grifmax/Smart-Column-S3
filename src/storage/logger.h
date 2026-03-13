/**
 * Smart-Column S3 - Logger
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

#include "config.h"
#include "types.h"

namespace Logger {

bool init();
void startSession();
void stopSession();
bool isSessionActive();

void writeData(const SystemState& state);
void log(const LogEvent& event);
void logf(uint8_t level, const char* format, ...);

String getLogsList();
String readLog(const char* filename);
bool deleteLog(const char* filename);
const char* getCurrentLogFile();
String getRecentEventsJson(uint16_t limit = 100, uint32_t sinceSequence = 0);
String exportRecentEventsCsv(uint16_t limit = 200);
void clearRecentEvents();

} // namespace Logger

#endif // LOGGER_H
