#pragma once

#include "service_test_audit_logic.h"

namespace ServiceTestAudit {

using Event = ServiceTestAuditLogic::Event;

// Emits one structured service_test line to Logger.  If an FSM process is
// currently being recorded, it also appends the same event to that process'
// history, preserving service interventions in the completed run.
void record(Event event, const char *target, const char *detail,
            const char *tone = "info");

} // namespace ServiceTestAudit
