#pragma once

#include <cmath>
#include <stdint.h>

namespace ControlCommandQueueLogic {

enum class Type : uint8_t {
  StartMode = 0,
  Stop,
  Pause,
  Resume,
  RestoreRectification,
  DiscardRectificationResume,
  NextFraction,
  AdvanceFractionProgram,
  ConfirmFractionProgram,
  MashingManualAdvance,
  ConfirmContainerChange,
  ConfirmContainerChangeAndResume,
  SetManualRectPhase,
  SetManualRectRate,
  SetMashingStepTemperature,
  SetMashingStepDuration,
  SetHoldStepTemperature,
  SetHoldStepDuration,
  SetProcessVolumes,
  SetStirrerAutoMode,
  SetHeaterOverride,
  UpdateDistillationParameters,
  AcknowledgeSafety,
  ResetSafety,
};

inline bool isSafetyOverrideCommand(Type type) {
  return type == Type::Stop || type == Type::AcknowledgeSafety ||
         type == Type::ResetSafety;
}

inline bool safetyGateAllows(bool safetyLocked, Type type) {
  return !safetyLocked || isSafetyOverrideCommand(type);
}

enum class Source : uint8_t { Api = 0, Mqtt, Tft, Cloud };
enum class Reason : uint8_t { OperatorRequest = 0, SafetyRequest, ProcessAction };
enum class PayloadKind : uint8_t { None = 0, Distillation, Mashing, Hold };

static constexpr uint16_t kMaxPayloadBytes = 1024;

struct Command {
  Type type = Type::Stop;
  Source source = Source::Api;
  Reason reason = Reason::OperatorRequest;
  uint8_t mode = 0;
  uint8_t argument = 0;
  float numericValue = 0.0f;
  uint32_t id = 0;
  uint32_t timestampMs = 0;
  PayloadKind payloadKind = PayloadKind::None;
  uint16_t payloadSize = 0;
  uint8_t payload[kMaxPayloadBytes]{};
};

enum class Status : uint8_t {
  Unknown = 0,
  Queued,
  Processing,
  Applied,
  Rejected,
  Cancelled,
  Superseded,
  QueueFull,
};

enum class EnqueueResult : uint8_t {
  Accepted = 0,
  Duplicate,
  Full,
  NotInitialized,
  InvalidPayload,
};

inline bool isActiveStepEditAllowed(bool active, uint8_t currentStep,
                                    uint8_t stepCount, uint8_t requestedStep,
                                    float value, float minValue,
                                    float maxValue) {
  return active && requestedStep == currentStep && requestedStep < stepCount &&
         std::isfinite(value) && value >= minValue && value <= maxValue;
}

class Queue {
public:
  static constexpr uint8_t kMaxCapacity = 16;

  bool init(uint8_t capacity = kMaxCapacity) {
    if (capacity == 0) capacity = 1;
    if (capacity > kMaxCapacity) capacity = kMaxCapacity;
    capacity_ = capacity;
    head_ = 0;
    size_ = 0;
    stopPending_ = false;
    activePending_ = false;
    dropped_ = 0;
    cancelled_ = 0;
    coalescedStops_ = 0;
    resultNext_ = 0;
    nextId_ = 1;
    for (uint8_t i = 0; i < kResultCapacity; ++i) results_[i].valid = false;
    initialized_ = true;
    return true;
  }

  EnqueueResult enqueue(Command &command, Status &currentStatus) {
    if (!initialized_) {
      currentStatus = Status::Unknown;
      return EnqueueResult::NotInitialized;
    }
    if (command.id == 0) command.id = allocateId(command.source);

    if (findStatus(command.source, command.id, currentStatus)) {
      return EnqueueResult::Duplicate;
    }
    if (isPending(command.source, command.id)) {
      currentStatus = Status::Queued;
      return EnqueueResult::Duplicate;
    }

    if (command.type == Type::Stop) {
      for (uint8_t offset = 0; offset < size_; ++offset) {
        const uint8_t index = static_cast<uint8_t>((head_ + offset) % capacity_);
        recordStatus(commands_[index].source, commands_[index].id,
                     Status::Cancelled);
      }
      cancelled_ += size_;
      head_ = 0;
      size_ = 0;
      if (stopPending_) {
        recordStatus(stopCommand_.source, stopCommand_.id,
                     Status::Superseded);
        ++coalescedStops_;
      }
      stopCommand_ = command;
      stopPending_ = true;
      currentStatus = Status::Queued;
      return EnqueueResult::Accepted;
    }

    if (size_ >= capacity_) {
      ++dropped_;
      recordStatus(command.source, command.id, Status::QueueFull);
      currentStatus = Status::QueueFull;
      return EnqueueResult::Full;
    }

    const uint8_t tail = static_cast<uint8_t>((head_ + size_) % capacity_);
    commands_[tail] = command;
    ++size_;
    currentStatus = Status::Queued;
    return EnqueueResult::Accepted;
  }

  EnqueueResult enqueue(const Command &command) {
    Command copy = command;
    Status ignored = Status::Unknown;
    return enqueue(copy, ignored);
  }

  bool dequeue(Command &command) {
    if (!initialized_) return false;
    if (stopPending_) {
      command = stopCommand_;
      stopPending_ = false;
      activeCommand_ = command;
      activePending_ = true;
      return true;
    }
    if (size_ == 0) return false;

    command = commands_[head_];
    head_ = static_cast<uint8_t>((head_ + 1) % capacity_);
    --size_;
    activeCommand_ = command;
    activePending_ = true;
    return true;
  }

  uint16_t pending() const {
    return static_cast<uint16_t>(size_ + (stopPending_ ? 1 : 0));
  }

  uint32_t droppedCount() const { return dropped_; }
  uint32_t cancelledCount() const { return cancelled_; }
  uint32_t coalescedStopCount() const { return coalescedStops_; }

  void complete(const Command &command, Status status) {
    if (!initialized_) return;
    recordStatus(command.source, command.id, status);
    if (activePending_ && activeCommand_.source == command.source &&
        activeCommand_.id == command.id) {
      activePending_ = false;
    }
  }

  bool getStatus(Source source, uint32_t id, Status &status) const {
    if (!initialized_ || id == 0) return false;
    if (isPending(source, id)) {
      status = Status::Queued;
      return true;
    }
    if (activePending_ && activeCommand_.source == source &&
        activeCommand_.id == id) {
      status = Status::Processing;
      return true;
    }
    return findStatus(source, id, status);
  }

private:
  struct ResultRecord {
    Source source = Source::Api;
    uint32_t id = 0;
    Status status = Status::Unknown;
    bool valid = false;
  };

  static constexpr uint8_t kResultCapacity = 32;

  bool isPending(Source source, uint32_t id) const {
    if (stopPending_ && stopCommand_.source == source && stopCommand_.id == id) {
      return true;
    }
    for (uint8_t offset = 0; offset < size_; ++offset) {
      const uint8_t index = static_cast<uint8_t>((head_ + offset) % capacity_);
      if (commands_[index].source == source && commands_[index].id == id) {
        return true;
      }
    }
    return false;
  }

  bool findStatus(Source source, uint32_t id, Status &status) const {
    for (uint8_t i = 0; i < kResultCapacity; ++i) {
      const ResultRecord &record = results_[i];
      if (record.valid && record.source == source && record.id == id) {
        status = record.status;
        return true;
      }
    }
    return false;
  }

  void recordStatus(Source source, uint32_t id, Status status) {
    for (uint8_t i = 0; i < kResultCapacity; ++i) {
      ResultRecord &record = results_[i];
      if (record.valid && record.source == source && record.id == id) {
        record.status = status;
        return;
      }
    }
    ResultRecord &record = results_[resultNext_];
    record.source = source;
    record.id = id;
    record.status = status;
    record.valid = true;
    resultNext_ = static_cast<uint8_t>((resultNext_ + 1) % kResultCapacity);
  }

  uint32_t allocateId(Source source) {
    for (uint16_t attempt = 0; attempt < 65535; ++attempt) {
      uint32_t candidate = nextId_++;
      if (candidate == 0) candidate = nextId_++;
      Status ignored = Status::Unknown;
      if (!isPending(source, candidate) &&
          !findStatus(source, candidate, ignored)) {
        return candidate;
      }
    }
    return 0;
  }

  Command commands_[kMaxCapacity]{};
  Command stopCommand_{};
  Command activeCommand_{};
  ResultRecord results_[kResultCapacity]{};
  uint8_t capacity_ = 0;
  uint8_t head_ = 0;
  uint8_t size_ = 0;
  uint8_t resultNext_ = 0;
  bool stopPending_ = false;
  bool activePending_ = false;
  bool initialized_ = false;
  uint32_t nextId_ = 1;
  uint32_t dropped_ = 0;
  uint32_t cancelled_ = 0;
  uint32_t coalescedStops_ = 0;
};

} // namespace ControlCommandQueueLogic
