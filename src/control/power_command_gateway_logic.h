#pragma once

#include <stdint.h>

#include "power_command_gateway.h"

namespace PowerCommandGatewayLogic {

enum class Operation : uint8_t {
  HeaterPercent,
  HeaterWatts,
  HeaterBooster,
  HeaterTriacDelay,
  PumpSpeed,
  StirrerSpeed,
  Valve,
  ProductRoute,
  CloseProductRoutes,
  CloseAllValves,
  PulseValve,
  CoolingPwm,
  Fraction,
  FractionAngle,
  SafeStopAll,
};

struct PendingCommand {
  PowerCommandGateway::Command command;
  Operation operation = Operation::SafeStopAll;
  uint8_t argument = 0;
  bool flag = false;
  uint32_t durationMs = 0;
  bool smooth = true;
};

struct EnqueueResult {
  bool accepted = false;
  bool coalesced = false;
  uint16_t cancelled = 0;
  uint8_t cancelledSequenceCount = 0;
  uint32_t cancelledSequences[25] = {};
};

class PendingQueue {
public:
  static constexpr uint8_t kCapacity = 24;

  uint16_t clear(EnqueueResult *cancelledCommands = nullptr) {
    const uint16_t removed = pending();
    if (cancelledCommands != nullptr) {
      if (hasSafeStop_) addCancelled(*cancelledCommands, safeStop_.command.sequence);
      if (hasPumpStop_) addCancelled(*cancelledCommands, pumpStop_.command.sequence);
      for (uint8_t i = 0; i < size_; ++i) {
        const uint8_t index = static_cast<uint8_t>((head_ + i) % kCapacity);
        addCancelled(*cancelledCommands, commands_[index].command.sequence);
      }
      cancelledCommands->cancelled = removed;
    }
    head_ = 0;
    size_ = 0;
    hasSafeStop_ = false;
    hasPumpStop_ = false;
    return removed;
  }

  EnqueueResult enqueue(const PendingCommand &pending) {
    EnqueueResult result;
    if (pending.operation == Operation::SafeStopAll) {
      if (hasSafeStop_) addCancelled(result, safeStop_.command.sequence);
      if (hasPumpStop_) addCancelled(result, pumpStop_.command.sequence);
      for (uint8_t i = 0; i < size_; ++i) {
        const uint8_t index = static_cast<uint8_t>((head_ + i) % kCapacity);
        addCancelled(result, commands_[index].command.sequence);
      }
      result.cancelled = size_ + (hasPumpStop_ ? 1 : 0);
      if (hasSafeStop_) ++result.cancelled;
      head_ = 0;
      size_ = 0;
      hasPumpStop_ = false;
      result.coalesced = hasSafeStop_;
      safeStop_ = pending;
      hasSafeStop_ = true;
      result.accepted = true;
      return result;
    }

    if (isPumpStop(pending)) {
      result.cancelled = removePendingPumpStarts(result);
      if (hasPumpStop_) addCancelled(result, pumpStop_.command.sequence);
      if (hasPumpStop_) ++result.cancelled;
      result.coalesced = hasPumpStop_;
      pumpStop_ = pending;
      hasPumpStop_ = true;
      result.accepted = true;
      return result;
    }

    const bool repeatable = pending.operation != Operation::PulseValve;
    for (uint8_t i = 0; i < size_; ++i) {
      const uint8_t index = static_cast<uint8_t>((head_ + i) % kCapacity);
      const PendingCommand &existing = commands_[index];
      if (repeatable && existing.operation == pending.operation &&
          existing.command.owner == pending.command.owner &&
          existing.command.target == pending.command.target) {
        addCancelled(result, existing.command.sequence);
        ++result.cancelled;
        commands_[index] = pending;
        result.accepted = true;
        result.coalesced = true;
        return result;
      }
    }

    if (size_ >= kCapacity) return result;
    const uint8_t tail = static_cast<uint8_t>((head_ + size_) % kCapacity);
    commands_[tail] = pending;
    ++size_;
    result.accepted = true;
    return result;
  }

  bool dequeue(PendingCommand &pending) {
    if (hasSafeStop_) {
      pending = safeStop_;
      hasSafeStop_ = false;
      return true;
    }
    if (hasPumpStop_) {
      pending = pumpStop_;
      hasPumpStop_ = false;
      return true;
    }
    if (size_ == 0) return false;

    pending = commands_[head_];
    head_ = static_cast<uint8_t>((head_ + 1) % kCapacity);
    --size_;
    if (size_ == 0) head_ = 0;
    return true;
  }

  uint16_t pending() const {
    return static_cast<uint16_t>(size_ + (hasSafeStop_ ? 1 : 0) +
                                 (hasPumpStop_ ? 1 : 0));
  }

private:
  static void addCancelled(EnqueueResult &result, uint32_t sequence) {
    if (sequence == 0 || result.cancelledSequenceCount >= 25) return;
    result.cancelledSequences[result.cancelledSequenceCount++] = sequence;
  }

  static bool isPumpStop(const PendingCommand &pending) {
    return pending.operation == Operation::PumpSpeed &&
           pending.command.requestedValue <= 0.0f;
  }

  uint16_t removePendingPumpStarts(EnqueueResult &result) {
    uint16_t removed = 0;
    const uint8_t oldSize = size_;
    uint8_t kept = 0;
    for (uint8_t read = 0; read < oldSize; ++read) {
      const uint8_t readIndex = static_cast<uint8_t>((head_ + read) % kCapacity);
      const bool pumpStart =
          commands_[readIndex].operation == Operation::PumpSpeed &&
          commands_[readIndex].command.requestedValue > 0.0f;
      if (pumpStart) {
        ++removed;
        addCancelled(result, commands_[readIndex].command.sequence);
      } else {
        const uint8_t writeIndex =
            static_cast<uint8_t>((head_ + kept) % kCapacity);
        if (writeIndex != readIndex) commands_[writeIndex] = commands_[readIndex];
        ++kept;
      }
    }
    size_ = kept;
    if (size_ == 0) head_ = 0;
    return removed;
  }

  PendingCommand commands_[kCapacity]{};
  PendingCommand safeStop_{};
  PendingCommand pumpStop_{};
  uint8_t head_ = 0;
  uint8_t size_ = 0;
  bool hasSafeStop_ = false;
  bool hasPumpStop_ = false;
};

} // namespace PowerCommandGatewayLogic
