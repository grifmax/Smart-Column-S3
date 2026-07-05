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

void registerChartsApiRoutes(AsyncWebServer &server);
void registerHealthApiRoutes(AsyncWebServer &server);
void registerLogsApiRoutes(AsyncWebServer &server);
void registerEnergyApiRoutes(AsyncWebServer &server);
void registerOtaApiRoutes(AsyncWebServer &server);
void registerWifiApiRoutes(AsyncWebServer &server);

void registerCalibrationApiRoutes(AsyncWebServer &server);
void registerHistoryApiRoutes(AsyncWebServer &server);
void registerPumpApiRoutes(AsyncWebServer &server);
void registerProcessApiRoutes(AsyncWebServer &server);
void registerProfilesApiRoutes(AsyncWebServer &server);
void registerSafetyApiRoutes(AsyncWebServer &server);
void registerSettingsApiRoutes(AsyncWebServer &server);
void registerStatusApiRoutes(AsyncWebServer &server);
void registerTestingApiRoutes(AsyncWebServer &server);
