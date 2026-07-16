#ifndef RECT_TAKEOFF_H
#define RECT_TAKEOFF_H

#include <Arduino.h>

#include "../types.h"

namespace RectTakeoff {

void beginSession(const Settings& settings);
void apply(const RectTakeoffCommand& command);
void stop();
RectTakeoffFeedback getFeedback();
bool requestFractionRoute(RectTakeoffBackendType backendType, uint8_t routeIndex,
                          String* detail = nullptr);
bool isFractionRouteReady(RectTakeoffBackendType backendType, uint8_t routeIndex);
bool isFractionRouteSupported(RectTakeoffBackendType backendType, uint8_t routeIndex,
                              String* detail = nullptr);
bool requiresSafeVent(RectTakeoffBackendType backendType);
bool validateBackendConfiguration(RectTakeoffBackendType backendType,
                                  String* detail = nullptr);

} // namespace RectTakeoff

#endif // RECT_TAKEOFF_H
