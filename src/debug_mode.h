/**
 * @file debug_mode.h
 * @brief Режим отладки с детальным выводом
 */

#ifndef DEBUG_MODE_H
#define DEBUG_MODE_H

#include <Arduino.h>

void initDebugMode(bool enabled = false);
void setDebugMode(bool enabled);
bool isDebugMode();
void debugPrint(const String& message);
void debugPrintF(const char* format, ...);
void printSystemInfo();
void printMemoryInfo();
void dumpTaskInfo();

// Макросы для отладки
#ifdef DEBUG_MODE_ENABLED
  #define DEBUG_PRINT(x) debugPrint(x)
  #define DEBUG_PRINTF(fmt, ...) debugPrintF(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTF(fmt, ...)
#endif

#endif
