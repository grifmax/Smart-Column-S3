#pragma once

#include "../api_routes.h"

void registerEquipmentSettingsRoutes(AsyncWebServer &server);
void registerThresholdSettingsRoutes(AsyncWebServer &server);
void registerModeSettingsRoutes(AsyncWebServer &server);
void registerMqttSettingsRoutes(AsyncWebServer &server);
void registerRuntimeSettingsRoutes(AsyncWebServer &server);
void registerSystemSettingsRoutes(AsyncWebServer &server);
