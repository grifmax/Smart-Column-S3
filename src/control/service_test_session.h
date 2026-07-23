#pragma once

#include <cstdint>

#include "service_test_session_logic.h"

// Thread-safe runtime owner of the TFT service-test lifecycle. This is shared
// with process APIs so another interface cannot resume the FSM while service
// owns its outputs.
namespace ServiceTestSession {

using Phase = ServiceTestSessionLogic::Phase;
using Target = ServiceTestSessionLogic::Target;
using Snapshot = ServiceTestSessionLogic::State;

Snapshot getSnapshot();
bool request(uint8_t mode, bool processWasRunning, bool alreadyPaused,
             Target target);
bool markSafePaused();
bool startTest(Target target);
bool beginStopping();
bool markRestoreReady();
bool fault();
bool mayUsePausedProcess(uint8_t currentMode, bool paused);
bool blocksProcessResume();
bool claimProcessResume();
bool closeWithoutResume();
void cancel();
void cancelForProcessStop();

} // namespace ServiceTestSession
