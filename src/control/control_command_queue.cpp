#include "control_command_queue.h"

#include <math.h>
#include <string.h>
#include <freertos/FreeRTOS.h>

#include "config.h"
#include "control/fsm.h"
#include "control/power_command_gateway.h"
#include "control/safety.h"
#include "control/service_test_session.h"
#include "control/watt_control.h"
#include "storage/logger.h"

namespace ControlCommandQueue {
namespace {

portMUX_TYPE g_queueMux = portMUX_INITIALIZER_UNLOCKED;
ControlCommandQueueLogic::Queue g_queue;
uint32_t g_applied = 0;

struct DistillationPayload {
  DistillationUiSettings ui;
  FractionProgram fractionProgram;
};

struct HoldPayload {
  uint8_t count;
  TempStep steps[10];
};

struct VolumePayload {
  uint8_t fieldMask;
  bool syncTotal;
  float headsMl;
  float bodyMl;
  float tailsMl;
};

struct DistillationParametersPayload {
  float speedMlH;
  float headsVolumeMl;
  float targetVolumeMl;
  float endTempC;
  uint8_t powerPercent;
};

static_assert(sizeof(DistillationPayload) <=
                  ControlCommandQueueLogic::kMaxPayloadBytes,
              "Distillation start payload exceeds queue capacity");
static_assert(sizeof(MashProfile) <=
                  ControlCommandQueueLogic::kMaxPayloadBytes,
              "Mash start payload exceeds queue capacity");
static_assert(sizeof(HoldPayload) <=
                  ControlCommandQueueLogic::kMaxPayloadBytes,
              "Hold start payload exceeds queue capacity");

EnqueueResult enqueuePrepared(Command &command, uint32_t *assignedId,
                              Status *currentStatus) {
  portENTER_CRITICAL(&g_queueMux);
  Status status = Status::Unknown;
  const EnqueueResult result = g_queue.enqueue(command, status);
  portEXIT_CRITICAL(&g_queueMux);
  if (assignedId) *assignedId = command.id;
  if (currentStatus) *currentStatus = status;
  return result;
}

bool applyStartPayload(const Command &command, SystemState &state,
                       Settings &settings) {
  switch (command.payloadKind) {
  case PayloadKind::None:
    FSM::startMode(state, settings, static_cast<Mode>(command.mode));
    return state.mode == static_cast<Mode>(command.mode);
  case PayloadKind::Distillation: {
    if (command.payloadSize != sizeof(DistillationPayload) ||
        static_cast<Mode>(command.mode) != Mode::DISTILLATION) {
      return false;
    }
    DistillationPayload payload;
    memcpy(&payload, command.payload, sizeof(payload));
    if (!isfinite(payload.ui.speedMlH) || payload.ui.speedMlH < 0.0f ||
        !isfinite(payload.ui.headsVolumeMl) ||
        !isfinite(payload.ui.targetVolumeMl) ||
        !isfinite(payload.ui.endTempC) || !isfinite(payload.ui.powerW) ||
        payload.fractionProgram.stepCount > FRACTION_PROGRAM_MAX_STEPS) {
      return false;
    }
    settings.distillationUi = payload.ui;
    settings.fractionProgram = payload.fractionProgram;
    FSM::Distillation::setParams(
        payload.ui.speedMlH, payload.ui.headsVolumeMl,
        payload.ui.targetVolumeMl, payload.ui.endTempC);
    FSM::Distillation::setPowerWatts(
        static_cast<uint16_t>(payload.ui.powerW));
    FSM::startMode(state, settings, Mode::DISTILLATION);
    return state.mode == Mode::DISTILLATION;
  }
  case PayloadKind::Mashing: {
    if (command.payloadSize != sizeof(MashProfile) ||
        static_cast<Mode>(command.mode) != Mode::MASHING) {
      return false;
    }
    MashProfile profile;
    memcpy(&profile, command.payload, sizeof(profile));
    if (profile.stepCount == 0 || profile.stepCount > 10) return false;
    FSM::Mashing::start(state, &profile);
    return state.mode == Mode::MASHING;
  }
  case PayloadKind::Hold: {
    if (command.payloadSize != sizeof(HoldPayload) ||
        static_cast<Mode>(command.mode) != Mode::HOLD) {
      return false;
    }
    HoldPayload payload;
    memcpy(&payload, command.payload, sizeof(payload));
    if (payload.count == 0 || payload.count > 10) return false;
    FSM::Hold::start(state, payload.steps, payload.count);
    return state.mode == Mode::HOLD;
  }
  }
  return false;
}

bool apply(const Command &command, SystemState &state, Settings &settings) {
  const bool allowDemoSensorFailure =
      command.type == Type::StartMode && settings.demoMode &&
      state.currentAlarm.type == AlarmType::SENSOR_FAILURE;
  const bool safetyLocked = !state.safetyOk || Safety::isLatched(state);
  if (!ControlCommandQueueLogic::safetyGateAllows(
          safetyLocked && !allowDemoSensorFailure, command.type)) {
    return false;
  }

  switch (command.type) {
  case Type::StartMode:
    if (state.mode != Mode::IDLE ||
        command.mode == static_cast<uint8_t>(Mode::IDLE) ||
        command.mode > static_cast<uint8_t>(Mode::AUTOCLAVE)) {
      return false;
    }
    return applyStartPayload(command, state, settings);
  case Type::Stop:
    FSM::stopMode(state);
    return true;
  case Type::Pause:
    if (state.mode == Mode::IDLE || state.paused) return false;
    PowerCommandGateway::noteFsmSafeStop(
        PowerCommandGateway::Reason::FSM_CONTROL);
    FSM::pause(state);
    return state.paused;
  case Type::Resume:
    if (state.mode == Mode::IDLE || !state.paused ||
        ServiceTestSession::blocksProcessResume()) {
      return false;
    }
    PowerCommandGateway::noteFsmSetpoint(
        PowerCommandGateway::Target::ALL_OUTPUTS,
        PowerCommandGateway::Reason::FSM_CONTROL, 1.0f);
    FSM::resume(state);
    return !state.paused;
  case Type::NextFraction:
    if (state.mode != Mode::MANUAL_RECT) return false;
    {
      const RectPhase previousPhase = state.rectPhase;
      FSM::nextFraction(state, settings);
      return state.rectPhase != previousPhase;
    }
  case Type::AdvanceFractionProgram:
    return state.mode == Mode::DISTILLATION &&
           FSM::Distillation::advanceFractionProgram(state, settings);
  case Type::ConfirmFractionProgram:
    return state.mode == Mode::DISTILLATION &&
           FSM::Distillation::confirmFractionProgram(state, settings);
  case Type::MashingManualAdvance:
    if (state.mode != Mode::MASHING ||
        !FSM::Mashing::requestManualAdvance(state)) {
      return false;
    }
    FSM::resume(state);
    return !state.paused;
  case Type::ConfirmContainerChange:
    return state.mode == Mode::RECTIFICATION &&
           FSM::Rectification::confirmContainerChange(state, settings);
  case Type::ConfirmContainerChangeAndResume:
    if (state.mode != Mode::RECTIFICATION ||
        !FSM::Rectification::confirmContainerChange(state, settings)) {
      return false;
    }
    FSM::resume(state);
    return !state.paused;
  case Type::SetManualRectPhase:
    if (state.mode != Mode::MANUAL_RECT ||
        command.argument > static_cast<uint8_t>(RectPhase::COMPLETED)) {
      return false;
    }
    FSM::ManualRect::setPhase(state, static_cast<RectPhase>(command.argument));
    return state.rectPhase == static_cast<RectPhase>(command.argument);
  case Type::SetManualRectRate:
    if (state.mode != Mode::MANUAL_RECT ||
        !isfinite(command.numericValue) || command.numericValue < 0.0f ||
        command.numericValue > 120000.0f) {
      return false;
    }
    FSM::ManualRect::setTakeoffRateMlH(command.numericValue);
    return true;
  case Type::SetMashingStepTemperature:
    if (state.mode != Mode::MASHING ||
        !ControlCommandQueueLogic::isActiveStepEditAllowed(
            state.mashing.active, state.mashing.currentStep,
            state.mashing.stepCount, command.argument, command.numericValue,
            30.0f, 90.0f)) {
      return false;
    }
    state.mashing.targetTemp = command.numericValue;
    return true;
  case Type::SetMashingStepDuration:
    if (state.mode != Mode::MASHING ||
        !ControlCommandQueueLogic::isActiveStepEditAllowed(
            state.mashing.active, state.mashing.currentStep,
            state.mashing.stepCount, command.argument, command.numericValue,
            1.0f, 240.0f)) {
      return false;
    }
    state.mashing.stepDuration =
        static_cast<uint32_t>(command.numericValue) * 60UL;
    state.mashing.tempInRange = false;
    state.mashing.inRangeStartTime = 0;
    return true;
  case Type::SetHoldStepTemperature:
    if (state.mode != Mode::HOLD ||
        !ControlCommandQueueLogic::isActiveStepEditAllowed(
            state.hold.active, state.hold.currentStep, state.hold.stepCount,
            command.argument, command.numericValue, 30.0f, 95.0f)) {
      return false;
    }
    state.hold.steps[command.argument].temperature = command.numericValue;
    state.hold.targetTemp = command.numericValue;
    return true;
  case Type::SetHoldStepDuration:
    if (state.mode != Mode::HOLD ||
        !ControlCommandQueueLogic::isActiveStepEditAllowed(
            state.hold.active, state.hold.currentStep, state.hold.stepCount,
            command.argument, command.numericValue, 1.0f, 720.0f)) {
      return false;
    }
    state.hold.steps[command.argument].duration =
        static_cast<uint16_t>(command.numericValue);
    state.hold.tempInRange = false;
    state.hold.inRangeStartTime = 0;
    return true;
  case Type::SetProcessVolumes: {
    if (command.payloadSize != sizeof(VolumePayload)) return false;
    VolumePayload payload;
    memcpy(&payload, command.payload, sizeof(payload));
    if ((payload.fieldMask & 0x01U) && !isfinite(payload.headsMl)) return false;
    if ((payload.fieldMask & 0x02U) && !isfinite(payload.bodyMl)) return false;
    if ((payload.fieldMask & 0x04U) && !isfinite(payload.tailsMl)) return false;
    if ((payload.fieldMask & 0x07U) == 0) return false;
    if (payload.fieldMask & 0x01U)
      state.stats.headsVolume = payload.headsMl < 0.0f ? 0.0f : payload.headsMl;
    if (payload.fieldMask & 0x02U)
      state.stats.bodyVolume = payload.bodyMl < 0.0f ? 0.0f : payload.bodyMl;
    if (payload.fieldMask & 0x04U)
      state.stats.tailsVolume = payload.tailsMl < 0.0f ? 0.0f : payload.tailsMl;
    if (payload.syncTotal) {
      state.pump.totalVolumeMl = state.stats.headsVolume +
                                 state.stats.bodyVolume +
                                 state.stats.tailsVolume;
    }
    return true;
  }
  case Type::SetStirrerAutoMode:
    if (!isfinite(command.numericValue)) return false;
    state.stirrer.autoMode = command.numericValue > 0.0f;
    return true;
  case Type::SetHeaterOverride: {
    if (!isfinite(command.numericValue) ||
        floorf(command.numericValue) != command.numericValue) {
      return false;
    }
    const int16_t powerWatts = static_cast<int16_t>(command.numericValue);
    const uint16_t heaterMaxW = settings.equipment.heaterPowerW > 0
                                    ? settings.equipment.heaterPowerW
                                    : DEFAULT_HEATER_POWER_W;
    if (powerWatts < -1 ||
        (powerWatts >= 0 && static_cast<uint16_t>(powerWatts) > heaterMaxW)) {
      return false;
    }
    WattControl::setOverrideWatts(powerWatts);
    return true;
  }
  case Type::UpdateDistillationParameters: {
    if (command.payloadSize != sizeof(DistillationParametersPayload)) {
      return false;
    }
    DistillationParametersPayload payload;
    memcpy(&payload, command.payload, sizeof(payload));
    if (!isfinite(payload.speedMlH) || payload.speedMlH < 0.0f ||
        !isfinite(payload.headsVolumeMl) ||
        !isfinite(payload.targetVolumeMl) || !isfinite(payload.endTempC) ||
        payload.powerPercent > 100) {
      return false;
    }
    FSM::Distillation::setParams(payload.speedMlH, payload.headsVolumeMl,
                                 payload.targetVolumeMl, payload.endTempC);
    FSM::Distillation::setPowerPercent(payload.powerPercent);
    return true;
  }
  case Type::AcknowledgeSafety:
    Safety::acknowledge(state);
    return state.currentAlarm.type == AlarmType::NONE ||
           state.currentAlarm.acknowledged;
  case Type::ResetSafety: {
    char reason[128] = "";
    const bool reset = Safety::reset(state, settings, reason, sizeof(reason));
    if (!reset) {
      LOG_W("Safety reset command rejected: %s", reason[0] ? reason : "unknown");
    }
    return reset;
  }
  }
  return false;
}

} // namespace

bool init(uint8_t capacity) {
  portENTER_CRITICAL(&g_queueMux);
  const bool initialized = g_queue.init(capacity);
  g_applied = 0;
  portEXIT_CRITICAL(&g_queueMux);
  return initialized;
}

EnqueueResult enqueue(Type type, Source source, Mode mode, uint32_t id,
                      Reason reason, uint32_t *assignedId,
                      Status *currentStatus) {
  Command command;
  command.type = type;
  command.source = source;
  command.reason = reason;
  command.mode = static_cast<uint8_t>(mode);
  command.id = id;
  command.timestampMs = millis();

  return enqueuePrepared(command, assignedId, currentStatus);
}

EnqueueResult enqueueStart(Source source, const StartRequest &request,
                           uint32_t id, Reason reason,
                           uint32_t *assignedId, Status *currentStatus) {
  Command command;
  command.type = Type::StartMode;
  command.source = source;
  command.reason = reason;
  command.mode = static_cast<uint8_t>(request.mode);
  command.id = id;
  command.timestampMs = millis();
  command.payloadKind = request.payloadKind;

  switch (request.payloadKind) {
  case PayloadKind::None:
    break;
  case PayloadKind::Distillation: {
    if (request.mode != Mode::DISTILLATION) {
      if (currentStatus) *currentStatus = Status::Rejected;
      return EnqueueResult::InvalidPayload;
    }
    const DistillationPayload payload{request.distillationUi,
                                      request.fractionProgram};
    command.payloadSize = sizeof(payload);
    memcpy(command.payload, &payload, sizeof(payload));
    break;
  }
  case PayloadKind::Mashing:
    if (request.mode != Mode::MASHING || request.mashProfile.stepCount == 0 ||
        request.mashProfile.stepCount > 10) {
      if (currentStatus) *currentStatus = Status::Rejected;
      return EnqueueResult::InvalidPayload;
    }
    command.payloadSize = sizeof(request.mashProfile);
    memcpy(command.payload, &request.mashProfile,
           sizeof(request.mashProfile));
    break;
  case PayloadKind::Hold: {
    if (request.mode != Mode::HOLD || request.holdStepCount == 0 ||
        request.holdStepCount > 10) {
      if (currentStatus) *currentStatus = Status::Rejected;
      return EnqueueResult::InvalidPayload;
    }
    HoldPayload payload{};
    payload.count = request.holdStepCount;
    memcpy(payload.steps, request.holdSteps,
           sizeof(TempStep) * payload.count);
    command.payloadSize = sizeof(payload);
    memcpy(command.payload, &payload, sizeof(payload));
    break;
  }
  }
  return enqueuePrepared(command, assignedId, currentStatus);
}

EnqueueResult enqueueAction(Type type, Source source, uint8_t argument,
                            float numericValue, uint32_t id, Reason reason,
                            uint32_t *assignedId, Status *currentStatus) {
  if (type == Type::StartMode || type == Type::Stop || type == Type::Pause ||
      type == Type::Resume) {
    if (currentStatus) *currentStatus = Status::Rejected;
    return EnqueueResult::InvalidPayload;
  }
  Command command;
  command.type = type;
  command.source = source;
  command.reason = reason;
  command.argument = argument;
  command.numericValue = numericValue;
  command.timestampMs = millis();
  command.id = id;
  return enqueuePrepared(command, assignedId, currentStatus);
}

EnqueueResult enqueueVolumes(Source source, uint8_t fieldMask,
                             float headsMl, float bodyMl, float tailsMl,
                             bool syncTotal, uint32_t id,
                             uint32_t *assignedId,
                             Status *currentStatus) {
  if ((fieldMask & 0x07U) == 0 || (fieldMask & 0xF8U) != 0) {
    if (currentStatus) *currentStatus = Status::Rejected;
    return EnqueueResult::InvalidPayload;
  }
  VolumePayload payload{fieldMask, syncTotal, headsMl, bodyMl, tailsMl};
  Command command;
  command.type = Type::SetProcessVolumes;
  command.source = source;
  command.reason = Reason::OperatorRequest;
  command.timestampMs = millis();
  command.id = id;
  command.payloadSize = sizeof(payload);
  memcpy(command.payload, &payload, sizeof(payload));
  return enqueuePrepared(command, assignedId, currentStatus);
}

EnqueueResult enqueueDistillationParameters(Source source, float speedMlH,
                                            float headsVolumeMl,
                                            float targetVolumeMl,
                                            float endTempC,
                                            uint8_t powerPercent, uint32_t id,
                                            uint32_t *assignedId,
                                            Status *currentStatus) {
  DistillationParametersPayload payload{speedMlH, headsVolumeMl,
                                        targetVolumeMl, endTempC,
                                        powerPercent};
  Command command;
  command.type = Type::UpdateDistillationParameters;
  command.source = source;
  command.reason = Reason::OperatorRequest;
  command.timestampMs = millis();
  command.id = id;
  command.payloadSize = sizeof(payload);
  memcpy(command.payload, &payload, sizeof(payload));
  return enqueuePrepared(command, assignedId, currentStatus);
}

bool getStatus(Source source, uint32_t id, Status &status) {
  portENTER_CRITICAL(&g_queueMux);
  const bool found = g_queue.getStatus(source, id, status);
  portEXIT_CRITICAL(&g_queueMux);
  return found;
}

const char *statusToken(Status status) {
  switch (status) {
  case Status::Queued:
    return "queued";
  case Status::Processing:
    return "processing";
  case Status::Applied:
    return "applied";
  case Status::Rejected:
    return "rejected";
  case Status::Cancelled:
    return "cancelled";
  case Status::Superseded:
    return "superseded";
  case Status::QueueFull:
    return "queue_full";
  case Status::Unknown:
  default:
    return "unknown";
  }
}

uint16_t pending() {
  portENTER_CRITICAL(&g_queueMux);
  const uint16_t count = g_queue.pending();
  portEXIT_CRITICAL(&g_queueMux);
  return count;
}

uint32_t droppedCount() {
  portENTER_CRITICAL(&g_queueMux);
  const uint32_t count = g_queue.droppedCount();
  portEXIT_CRITICAL(&g_queueMux);
  return count;
}

uint32_t cancelledCount() {
  portENTER_CRITICAL(&g_queueMux);
  const uint32_t count = g_queue.cancelledCount();
  portEXIT_CRITICAL(&g_queueMux);
  return count;
}

uint32_t coalescedStopCount() {
  portENTER_CRITICAL(&g_queueMux);
  const uint32_t count = g_queue.coalescedStopCount();
  portEXIT_CRITICAL(&g_queueMux);
  return count;
}

uint32_t appliedCount() {
  portENTER_CRITICAL(&g_queueMux);
  const uint32_t count = g_applied;
  portEXIT_CRITICAL(&g_queueMux);
  return count;
}

uint8_t process(SystemState &state, Settings &settings,
                uint8_t maxCommands) {
  if (maxCommands == 0) return 0;

  uint8_t appliedCount = 0;
  while (appliedCount < maxCommands) {
    Command command;
    portENTER_CRITICAL(&g_queueMux);
    const bool hasCommand = g_queue.dequeue(command);
    portEXIT_CRITICAL(&g_queueMux);
    if (!hasCommand) break;

    const bool accepted = apply(command, state, settings);
    ++appliedCount;

    portENTER_CRITICAL(&g_queueMux);
    ++g_applied;
    g_queue.complete(command, accepted ? Status::Applied : Status::Rejected);
    portEXIT_CRITICAL(&g_queueMux);

    // Do not process a later start in the same control-loop pass as STOP.
    if (command.type == Type::Stop) break;
  }
  return appliedCount;
}

} // namespace ControlCommandQueue
