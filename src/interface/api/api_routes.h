#pragma once

#include "../webserver.h"

#ifndef HTTP_GET
typedef enum {
  HTTP_GET = 0b00000001,
  HTTP_POST = 0b00000010,
  HTTP_DELETE = 0b00000100,
  HTTP_PUT = 0b00001000,
  HTTP_PATCH = 0b00010000,
  HTTP_HEAD = 0b00100000,
  HTTP_OPTIONS = 0b01000000,
  HTTP_ANY = 0b01111111,
} WebRequestMethod;
#endif

#include <ESPAsyncWebServer.h>

void registerChartsRoutes(AsyncWebServer &server);
void registerHealthRoutes(AsyncWebServer &server);
void registerLogsRoutes(AsyncWebServer &server);
void registerEnergyRoutes(AsyncWebServer &server);
void registerOtaRoutes(AsyncWebServer &server);
void registerWifiRoutes(AsyncWebServer &server);

void registerCalibrationRoutes(AsyncWebServer &server);
void registerHistoryRoutes(AsyncWebServer &server);
void registerPumpRoutes(AsyncWebServer &server);
void registerProcessRoutes(AsyncWebServer &server);
void registerProfilesRoutes(AsyncWebServer &server);
void registerSafetyRoutes(AsyncWebServer &server);
void registerSettingsRoutes(AsyncWebServer &server);
void registerStatusRoutes(AsyncWebServer &server);
void registerTestingRoutes(AsyncWebServer &server);
