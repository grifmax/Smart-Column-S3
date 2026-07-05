#include "api_routes.h"

#include "settings/settings_modules.h"

void registerSettingsRoutes(AsyncWebServer &server) {
  registerEquipmentSettingsRoutes(server);
  registerThresholdSettingsRoutes(server);
  registerModeSettingsRoutes(server);
  registerMqttSettingsRoutes(server);
  registerRuntimeSettingsRoutes(server);
  registerSystemSettingsRoutes(server);
}
