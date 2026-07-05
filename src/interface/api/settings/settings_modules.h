#pragma once

#include "../api_routes.h"

void registerEquipmentSettingsApiRoutes(AsyncWebServer &server);
void registerThresholdSettingsApiRoutes(AsyncWebServer &server);
void registerModeSettingsApiRoutes(AsyncWebServer &server);
void registerMqttSettingsApiRoutes(AsyncWebServer &server);
void registerRuntimeSettingsApiRoutes(AsyncWebServer &server);
void registerSystemSettingsApiRoutes(AsyncWebServer &server);
