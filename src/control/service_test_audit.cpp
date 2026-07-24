#include "service_test_audit.h"

#include <string.h>

#include "control/v2/reason_codes.h"
#include "history.h"
#include "storage/logger.h"

namespace ServiceTestAudit {
namespace {

const char *historySeverity(const char *tone, Event event) {
  if (event == Event::FAULTED || event == Event::REJECTED ||
      (tone != nullptr && strcmp(tone, "danger") == 0)) {
    return "error";
  }
  if (event == Event::CANCELLED || event == Event::STOPPED ||
      (tone != nullptr && strcmp(tone, "warning") == 0)) {
    return "warning";
  }
  return "info";
}

uint8_t logLevel(const char *tone, Event event) {
  return (event == Event::FAULTED || event == Event::REJECTED ||
          (tone != nullptr && strcmp(tone, "danger") == 0))
             ? 2
             : ((tone != nullptr && strcmp(tone, "warning") == 0) ? 1 : 0);
}

void sanitize(char *destination, size_t destinationSize, const char *source) {
  if (destination == nullptr || destinationSize == 0) return;
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  size_t out = 0;
  while (*source != '\0' && out + 1 < destinationSize) {
    char character = *source++;
    if (character == '"' || character == '\r' || character == '\n') {
      character = '\'';
    }
    destination[out++] = character;
  }
  destination[out] = '\0';
}

} // namespace

void record(Event event, const char *target, const char *detail,
            const char *tone) {
  char safeTarget[24];
  char safeDetail[33];
  char historyDetail[144];
  char safeTone[12];
  sanitize(safeTarget, sizeof(safeTarget),
           (target != nullptr && target[0] != '\0') ? target : "equipment");
  sanitize(safeDetail, sizeof(safeDetail), detail != nullptr ? detail : "");
  sanitize(historyDetail, sizeof(historyDetail),
           detail != nullptr ? detail : "");
  sanitize(safeTone, sizeof(safeTone), tone != nullptr ? tone : "info");

  const char *eventName = ServiceTestAuditLogic::eventToken(event);
  Logger::logf(logLevel(safeTone, event),
               "service_test event=%s target=%s tone=%s detail=\"%s\"",
               eventName, safeTarget, safeTone, safeDetail);

  if (!processRecorder.isRecording()) return;

  String operatorMessage = "service_test event=";
  operatorMessage += eventName;
  operatorMessage += " target=";
  operatorMessage += safeTarget;
  processRecorder.addWarning(
      historyDetail[0] != '\0' ? String(historyDetail)
                             : String("Service test action"),
      historySeverity(safeTone, event),
      ControlV2::reasonCodeToString(
          ControlV2::ReasonCodeV2::RC_OPERATOR_SERVICE_ACTION),
      operatorMessage);
}

} // namespace ServiceTestAudit
