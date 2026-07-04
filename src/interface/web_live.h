#pragma once

#include "../types.h"

class AsyncWebSocket;

namespace WebServerLive {

void bindWebSocket(AsyncWebSocket *socket);
void broadcastState(const SystemState &state);
void broadcastEvent(const char *event, const char *message);

} // namespace WebServerLive
