#pragma once

#include <stdint.h>

// Stable, interface-neutral event names for a service-test lifecycle.  They
// are stored in the system event log and, when a process is active, in its
// history record, so do not use translated display text as identifiers.
namespace ServiceTestAuditLogic {

enum class Event : uint8_t {
  REQUESTED = 0,
  PAUSED,
  STARTED,
  CHANGED,
  STOPPED,
  CANCELLED,
  RESULT,
  FAULTED,
  RESTORED,
  REJECTED
};

inline const char *eventToken(Event event) {
  switch (event) {
  case Event::REQUESTED:
    return "requested";
  case Event::PAUSED:
    return "paused";
  case Event::STARTED:
    return "started";
  case Event::CHANGED:
    return "changed";
  case Event::STOPPED:
    return "stopped";
  case Event::CANCELLED:
    return "cancelled";
  case Event::RESULT:
    return "result";
  case Event::FAULTED:
    return "faulted";
  case Event::RESTORED:
    return "restored";
  case Event::REJECTED:
  default:
    return "rejected";
  }
}

inline bool isTerminal(Event event) {
  return event == Event::CANCELLED || event == Event::RESULT ||
         event == Event::FAULTED || event == Event::RESTORED ||
         event == Event::REJECTED;
}

} // namespace ServiceTestAuditLogic
