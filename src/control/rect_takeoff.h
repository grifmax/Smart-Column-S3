#ifndef RECT_TAKEOFF_H
#define RECT_TAKEOFF_H

#include <Arduino.h>

#include "../types.h"

namespace RectTakeoff {

void beginSession(const Settings& settings);
void apply(const RectTakeoffCommand& command);
void stop();
RectTakeoffFeedback getFeedback();
bool requestFractionRoute(uint8_t routeIndex);
bool isFractionRouteReady(uint8_t routeIndex);
bool validateBackendConfiguration(RectTakeoffBackendType backendType,
                                  String* detail = nullptr);

} // namespace RectTakeoff

#endif // RECT_TAKEOFF_H
