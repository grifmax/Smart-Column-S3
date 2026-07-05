#include "api_routes.h"

#include "settings/settings_modules.h"

void registerSettingsApiRoutes(AsyncWebServer &server) {
  registerEquipmentSettingsApiRoutes(server);
  registerThresholdSettingsApiRoutes(server);
  registerModeSettingsApiRoutes(server);
  registerMqttSettingsApiRoutes(server);
  registerRuntimeSettingsApiRoutes(server);
  registerSystemSettingsApiRoutes(server);
}
