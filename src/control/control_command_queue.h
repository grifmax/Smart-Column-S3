#pragma once

#include "types.h"
#include "control_command_queue_logic.h"

// Boundary between asynchronous interfaces and the single control loop.
// Queue commands are deliberately small and contain no pointers into request
// buffers. The loop is the only context that applies these transitions.
namespace ControlCommandQueue {

using Type = ControlCommandQueueLogic::Type;
using Source = ControlCommandQueueLogic::Source;
using Reason = ControlCommandQueueLogic::Reason;
using Command = ControlCommandQueueLogic::Command;
using Status = ControlCommandQueueLogic::Status;
using EnqueueResult = ControlCommandQueueLogic::EnqueueResult;
using PayloadKind = ControlCommandQueueLogic::PayloadKind;

struct StartRequest {
  Mode mode = Mode::IDLE;
  PayloadKind payloadKind = PayloadKind::None;
  DistillationUiSettings distillationUi{};
  FractionProgram fractionProgram{};
  MashProfile mashProfile{};
  TempStep holdSteps[10]{};
  uint8_t holdStepCount = 0;
};

bool init(uint8_t capacity = 16);
EnqueueResult enqueue(Type type, Source source, Mode mode = Mode::IDLE,
                      uint32_t id = 0,
                      Reason reason = Reason::OperatorRequest,
                      uint32_t *assignedId = nullptr,
                      Status *currentStatus = nullptr);
EnqueueResult enqueueStart(Source source, const StartRequest &request,
                           uint32_t id = 0,
                           Reason reason = Reason::OperatorRequest,
                           uint32_t *assignedId = nullptr,
                           Status *currentStatus = nullptr);
EnqueueResult enqueueAction(Type type, Source source, uint8_t argument = 0,
                            float numericValue = 0.0f, uint32_t id = 0,
                            Reason reason = Reason::OperatorRequest,
                            uint32_t *assignedId = nullptr,
                            Status *currentStatus = nullptr);
EnqueueResult enqueueVolumes(Source source, uint8_t fieldMask,
                             float headsMl, float bodyMl, float tailsMl,
                             bool syncTotal, uint32_t id = 0,
                             uint32_t *assignedId = nullptr,
                             Status *currentStatus = nullptr);
EnqueueResult enqueueDistillationParameters(Source source, float speedMlH,
                                            float headsVolumeMl,
                                            float targetVolumeMl,
                                            float endTempC,
                                            uint8_t powerPercent,
                                            uint32_t id = 0,
                                            uint32_t *assignedId = nullptr,
                                            Status *currentStatus = nullptr);
bool getStatus(Source source, uint32_t id, Status &status);
const char *statusToken(Status status);
uint16_t pending();
uint32_t droppedCount();
uint32_t cancelledCount();
uint32_t coalescedStopCount();
uint32_t appliedCount();

// Apply a bounded batch. Must be called only by the main control loop.
uint8_t process(SystemState &state, Settings &settings,
                uint8_t maxCommands = 8);

} // namespace ControlCommandQueue
