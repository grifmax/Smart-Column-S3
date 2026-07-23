#include "service_test_session.h"

#include <freertos/FreeRTOS.h>

namespace ServiceTestSession {
namespace {

portMUX_TYPE g_sessionMux = portMUX_INITIALIZER_UNLOCKED;
Snapshot g_session;

template <typename Callback> bool mutate(Callback callback) {
  portENTER_CRITICAL(&g_sessionMux);
  const bool changed = callback(g_session);
  portEXIT_CRITICAL(&g_sessionMux);
  return changed;
}

} // namespace

Snapshot getSnapshot() {
  Snapshot snapshot;
  portENTER_CRITICAL(&g_sessionMux);
  snapshot = g_session;
  portEXIT_CRITICAL(&g_sessionMux);
  return snapshot;
}

bool request(uint8_t mode, bool processWasRunning, bool alreadyPaused,
             Target target) {
  return mutate([=](Snapshot &state) {
    return ServiceTestSessionLogic::request(state, mode, processWasRunning,
                                            alreadyPaused, target);
  });
}

bool markSafePaused() {
  return mutate([](Snapshot &state) {
    return ServiceTestSessionLogic::markSafePaused(state);
  });
}

bool startTest(Target target) {
  return mutate([=](Snapshot &state) {
    return ServiceTestSessionLogic::startTest(state, target);
  });
}

bool beginStopping() {
  return mutate([](Snapshot &state) {
    return ServiceTestSessionLogic::beginStopping(state);
  });
}

bool markRestoreReady() {
  return mutate([](Snapshot &state) {
    return ServiceTestSessionLogic::markRestoreReady(state);
  });
}

bool fault() {
  return mutate([](Snapshot &state) {
    return ServiceTestSessionLogic::fault(state);
  });
}

bool mayUsePausedProcess(uint8_t currentMode, bool paused) {
  const Snapshot snapshot = getSnapshot();
  return ServiceTestSessionLogic::mayUsePausedProcess(snapshot, currentMode,
                                                       paused);
}

bool blocksProcessResume() {
  return ServiceTestSessionLogic::blocksProcessResume(getSnapshot());
}

bool claimProcessResume() {
  return mutate([](Snapshot &state) {
    if (!ServiceTestSessionLogic::mayResumeProcess(state)) return false;
    ServiceTestSessionLogic::close(state);
    return true;
  });
}

bool closeWithoutResume() {
  return mutate([](Snapshot &state) {
    if (!ServiceTestSessionLogic::mayCloseWithoutResume(state)) return false;
    ServiceTestSessionLogic::close(state);
    return true;
  });
}

void cancel() {
  mutate([](Snapshot &state) {
    ServiceTestSessionLogic::close(state);
    return true;
  });
}

void cancelForProcessStop() {
  cancel();
}

} // namespace ServiceTestSession
