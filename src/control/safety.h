/**
 * Smart-Column S3 - Safety logic
 */

#ifndef SAFETY_H
#define SAFETY_H

#include <Arduino.h>
#include <freertos/semphr.h>
#include "config.h"
#include "types.h"

namespace Safety {

struct RequiredSensorsMask {
    bool cubeTemp = false;
    bool columnBottomTemp = false;
    bool tsaTemp = false;
    bool waterOutTemp = false;
    bool pressure = false;
};

extern SemaphoreHandle_t g_safetyMutex;

void init();
void check(SystemState& state, const Settings& settings);
void acknowledge(SystemState& state);
bool reset(SystemState& state, const Settings& settings, char* reason = nullptr,
           size_t reasonSize = 0);
bool isLatched(const SystemState& state);
bool canResetNow(const SystemState& state, const Settings& settings, char* reason = nullptr,
                 size_t reasonSize = 0);
bool isTempSensorInstalled(const EquipmentSettings& equipment, uint8_t index);
uint8_t getInstalledTempSensorCount(const EquipmentSettings& equipment);
bool isModeTemperatureTopologySupported(Mode mode, const EquipmentSettings& equipment,
                                        char* reason = nullptr, size_t reasonSize = 0);
RequiredSensorsMask getRequiredSensorsForMode(Mode mode, const Settings& settings);

const char* getAlarmTypeToken(AlarmType type);
const char* getAlarmLevelToken(AlarmLevel level);

} // namespace Safety

#endif // SAFETY_H
