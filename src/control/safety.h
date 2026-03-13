/**
 * Smart-Column S3 - Safety logic
 */

#ifndef SAFETY_H
#define SAFETY_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace Safety {

void check(SystemState& state, const Settings& settings);
void acknowledge(SystemState& state);
bool reset(SystemState& state, const Settings& settings, char* reason = nullptr,
           size_t reasonSize = 0);
bool isLatched(const SystemState& state);

const char* getAlarmTypeToken(AlarmType type);
const char* getAlarmLevelToken(AlarmLevel level);

} // namespace Safety

#endif // SAFETY_H
