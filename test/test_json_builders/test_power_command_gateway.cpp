#include <unity.h>
#include <string.h>
#include <limits>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>

#include <../../src/control/control_command_queue_logic.h>
#include <../../src/control/power_command_gateway.h>
#include <../../src/control/power_command_gateway_logic.h>
#include <../../src/control/power_output_state_logic.h>
#include <../../src/control/state_snapshot_logic.h>
#include <../../src/control/autoclave_hardware_profile_logic.h>
#include <../../src/control/autoclave_session_logic.h>
#include <../../src/control/autoclave_safety_logic.h>
#include <../../src/control/dosing_session_logic.h>
#include <../../src/control/pump_calibration_logic.h>
#include <../../src/control/sensor_calibration_logic.h>
#include <../../src/control/service_test_session_logic.h>
#include <../../src/control/service_test_audit_logic.h>
#include <../../src/interface/tft_service_test_logic.h>

void test_control_command_queue_prioritizes_stop_and_cancels_older_commands() {
  using namespace ControlCommandQueueLogic;

  Queue queue;
  Command uninitialized;
  Status status = Status::Unknown;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(EnqueueResult::NotInitialized),
      static_cast<uint8_t>(queue.enqueue(uninitialized, status)));
  TEST_ASSERT_TRUE(queue.init(2));

  Command start;
  start.type = Type::StartMode;
  start.source = Source::Api;
  start.mode = 1;
  start.id = 101;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Accepted),
                          static_cast<uint8_t>(queue.enqueue(start, status)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Queued),
                          static_cast<uint8_t>(status));

  Command pause;
  pause.type = Type::Pause;
  pause.source = Source::Tft;
  pause.id = 102;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Accepted),
                          static_cast<uint8_t>(queue.enqueue(pause, status)));

  Command stop;
  stop.type = Type::Stop;
  stop.source = Source::Api;
  stop.id = 103;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Accepted),
                          static_cast<uint8_t>(queue.enqueue(stop, status)));
  TEST_ASSERT_EQUAL_UINT32(2, queue.cancelledCount());
  TEST_ASSERT_EQUAL_UINT16(1, queue.pending());
  TEST_ASSERT_TRUE(queue.getStatus(Source::Api, 101, status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Cancelled),
                          static_cast<uint8_t>(status));
  TEST_ASSERT_TRUE(queue.getStatus(Source::Tft, 102, status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Cancelled),
                          static_cast<uint8_t>(status));

  Command resume;
  resume.type = Type::Resume;
  resume.source = Source::Mqtt;
  resume.id = 104;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Accepted),
                          static_cast<uint8_t>(queue.enqueue(resume, status)));

  Command next;
  TEST_ASSERT_TRUE(queue.dequeue(next));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Type::Stop),
                          static_cast<uint8_t>(next.type));
  TEST_ASSERT_EQUAL_UINT32(103, next.id);
  queue.complete(next, Status::Applied);
  TEST_ASSERT_TRUE(queue.dequeue(next));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Type::Resume),
                          static_cast<uint8_t>(next.type));
  queue.complete(next, Status::Rejected);
  TEST_ASSERT_FALSE(queue.dequeue(next));
  TEST_ASSERT_TRUE(queue.getStatus(Source::Api, 103, status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Applied),
                          static_cast<uint8_t>(status));
  TEST_ASSERT_TRUE(queue.getStatus(Source::Mqtt, 104, status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Rejected),
                          static_cast<uint8_t>(status));
}

void test_control_command_queue_reports_overflow_and_coalesces_stop() {
  using namespace ControlCommandQueueLogic;

  Queue queue;
  TEST_ASSERT_TRUE(queue.init(1));
  Status status = Status::Unknown;
  Command command;
  command.type = Type::Pause;
  command.source = Source::Api;
  command.id = 201;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Accepted),
                          static_cast<uint8_t>(queue.enqueue(command, status)));
  command.type = Type::Resume;
  command.id = 202;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Full),
                          static_cast<uint8_t>(queue.enqueue(command, status)));
  TEST_ASSERT_EQUAL_UINT32(1, queue.droppedCount());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::QueueFull),
                          static_cast<uint8_t>(status));

  command.type = Type::Stop;
  command.id = 203;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Accepted),
                          static_cast<uint8_t>(queue.enqueue(command, status)));
  command.id = 204;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Accepted),
                          static_cast<uint8_t>(queue.enqueue(command, status)));
  TEST_ASSERT_EQUAL_UINT32(1, queue.cancelledCount());
  TEST_ASSERT_EQUAL_UINT32(1, queue.coalescedStopCount());
  TEST_ASSERT_EQUAL_UINT16(1, queue.pending());
  TEST_ASSERT_TRUE(queue.getStatus(Source::Api, 203, status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Superseded),
                          static_cast<uint8_t>(status));
  TEST_ASSERT_TRUE(queue.getStatus(Source::Api, 204, status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Queued),
                          static_cast<uint8_t>(status));

  Command duplicate;
  duplicate.type = Type::Pause;
  duplicate.source = Source::Api;
  duplicate.id = 201;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Duplicate),
                          static_cast<uint8_t>(queue.enqueue(duplicate, status)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Cancelled),
                          static_cast<uint8_t>(status));
}

void test_control_command_queue_assigns_ids_and_returns_idempotent_results() {
  using namespace ControlCommandQueueLogic;

  Queue queue;
  TEST_ASSERT_TRUE(queue.init());
  Command command;
  command.type = Type::Pause;
  command.source = Source::Api;
  command.reason = Reason::OperatorRequest;
  command.timestampMs = 12345;
  Status status = Status::Unknown;

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Accepted),
                          static_cast<uint8_t>(queue.enqueue(command, status)));
  TEST_ASSERT_TRUE(command.id > 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Queued),
                          static_cast<uint8_t>(status));

  Command duplicate = command;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Duplicate),
                          static_cast<uint8_t>(queue.enqueue(duplicate, status)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Queued),
                          static_cast<uint8_t>(status));

  Command processed;
  TEST_ASSERT_TRUE(queue.dequeue(processed));
  TEST_ASSERT_TRUE(queue.getStatus(Source::Api, command.id, status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Processing),
                          static_cast<uint8_t>(status));
  queue.complete(processed, Status::Applied);
  TEST_ASSERT_TRUE(queue.getStatus(Source::Api, command.id, status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Applied),
                          static_cast<uint8_t>(status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Duplicate),
                          static_cast<uint8_t>(queue.enqueue(duplicate, status)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Applied),
                          static_cast<uint8_t>(status));
}

void test_control_command_queue_preserves_heater_override_command() {
  using namespace ControlCommandQueueLogic;

  Queue queue;
  TEST_ASSERT_TRUE(queue.init());
  Command command;
  command.type = Type::SetHeaterOverride;
  command.source = Source::Api;
  command.numericValue = 1475.0f;
  Status status = Status::Unknown;

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Accepted),
                          static_cast<uint8_t>(queue.enqueue(command, status)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Queued),
                          static_cast<uint8_t>(status));

  Command processed;
  TEST_ASSERT_TRUE(queue.dequeue(processed));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Type::SetHeaterOverride),
                          static_cast<uint8_t>(processed.type));
  TEST_ASSERT_EQUAL_FLOAT(1475.0f, processed.numericValue);
  TEST_ASSERT_TRUE(processed.id > 0);
}

void test_control_command_queue_preserves_start_payload_bytes() {
  using namespace ControlCommandQueueLogic;

  Queue queue;
  TEST_ASSERT_TRUE(queue.init());
  Command start;
  start.type = Type::StartMode;
  start.source = Source::Api;
  start.mode = 4;
  start.payloadKind = PayloadKind::Mashing;
  start.payloadSize = 6;
  const uint8_t payload[] = {3, 17, 42, 99, 128, 250};
  memcpy(start.payload, payload, sizeof(payload));

  Status status = Status::Unknown;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueResult::Accepted),
                          static_cast<uint8_t>(queue.enqueue(start, status)));
  Command processed;
  TEST_ASSERT_TRUE(queue.dequeue(processed));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadKind::Mashing),
                          static_cast<uint8_t>(processed.payloadKind));
  TEST_ASSERT_EQUAL_UINT16(sizeof(payload), processed.payloadSize);
  TEST_ASSERT_EQUAL_MEMORY(payload, processed.payload, sizeof(payload));
  queue.complete(processed, Status::Applied);
}

void test_control_command_queue_validates_active_step_edits() {
  using ControlCommandQueueLogic::isActiveStepEditAllowed;

  TEST_ASSERT_TRUE(isActiveStepEditAllowed(true, 2, 5, 2, 78.0f, 30.0f,
                                           90.0f));
  TEST_ASSERT_FALSE(isActiveStepEditAllowed(false, 2, 5, 2, 78.0f, 30.0f,
                                            90.0f));
  TEST_ASSERT_FALSE(isActiveStepEditAllowed(true, 2, 5, 1, 78.0f, 30.0f,
                                            90.0f));
  TEST_ASSERT_FALSE(isActiveStepEditAllowed(true, 5, 5, 5, 78.0f, 30.0f,
                                            90.0f));
  TEST_ASSERT_FALSE(isActiveStepEditAllowed(true, 2, 5, 2, 29.9f, 30.0f,
                                            90.0f));
  TEST_ASSERT_FALSE(isActiveStepEditAllowed(true, 2, 5, 2, 90.1f, 30.0f,
                                            90.0f));
  TEST_ASSERT_FALSE(isActiveStepEditAllowed(
      true, 2, 5, 2, std::numeric_limits<float>::quiet_NaN(), 30.0f, 90.0f));
  TEST_ASSERT_FALSE(isActiveStepEditAllowed(
      true, 2, 5, 2, std::numeric_limits<float>::infinity(), 30.0f, 90.0f));
}

void test_control_command_queue_safety_gate_allows_only_safety_overrides() {
  using namespace ControlCommandQueueLogic;

  TEST_ASSERT_TRUE(safetyGateAllows(false, Type::StartMode));
  TEST_ASSERT_TRUE(safetyGateAllows(false, Type::Resume));
  TEST_ASSERT_TRUE(safetyGateAllows(false, Type::SetHoldStepDuration));
  TEST_ASSERT_FALSE(safetyGateAllows(true, Type::StartMode));
  TEST_ASSERT_FALSE(safetyGateAllows(true, Type::Pause));
  TEST_ASSERT_FALSE(safetyGateAllows(true, Type::Resume));
  TEST_ASSERT_FALSE(safetyGateAllows(true, Type::MashingManualAdvance));
  TEST_ASSERT_TRUE(safetyGateAllows(true, Type::Stop));
  TEST_ASSERT_TRUE(safetyGateAllows(true, Type::AcknowledgeSafety));
  TEST_ASSERT_TRUE(safetyGateAllows(true, Type::ResetSafety));
}

void test_power_command_captures_owner_reason_timestamp_and_safe_stop() {
  using namespace PowerCommandGateway;

  Command command = makeCommand(Owner::MANUAL, Target::PUMP,
                                Reason::TFT_MANUAL_OUTPUT, 123456U, 850.0f);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Owner::MANUAL),
                          static_cast<uint8_t>(command.owner));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Target::PUMP),
                          static_cast<uint8_t>(command.target));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Reason::TFT_MANUAL_OUTPUT),
                          static_cast<uint8_t>(command.reason));
  TEST_ASSERT_EQUAL_UINT32(123456U, command.timestampMs);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 850.0f, command.requestedValue);
  TEST_ASSERT_TRUE(command.safeStopRequired);
  TEST_ASSERT_FALSE(command.safeStopApplied);

  markSafeStopApplied(command);
  TEST_ASSERT_TRUE(command.safeStopApplied);
}

void test_power_gateway_reserves_safe_stop_when_normal_queue_is_full() {
  using namespace PowerCommandGateway;
  using namespace PowerCommandGatewayLogic;

  PendingQueue queue;
  for (uint8_t i = 0; i < PendingQueue::kCapacity; ++i) {
    PendingCommand pending;
    pending.command = makeCommand(
        Owner::MANUAL, static_cast<Target>(i), Reason::TFT_MANUAL_OUTPUT,
        i + 1, 1.0f);
    pending.command.sequence = static_cast<uint32_t>(i) + 1;
    pending.operation = static_cast<Operation>(i % 14);
    TEST_ASSERT_TRUE(queue.enqueue(pending).accepted);
  }
  TEST_ASSERT_EQUAL_UINT16(PendingQueue::kCapacity, queue.pending());

  PendingCommand overflow;
  overflow.command = makeCommand(Owner::SERVICE, Target::HEATER,
                                 Reason::TFT_SERVICE_TEST, 100, 1.0f);
  overflow.operation = Operation::HeaterPercent;
  TEST_ASSERT_FALSE(queue.enqueue(overflow).accepted);

  PendingCommand stop;
  stop.command = makeCommand(Owner::SERVICE, Target::ALL_OUTPUTS,
                             Reason::TFT_PROCESS_STOP, 101, 0.0f, true);
  stop.operation = Operation::SafeStopAll;
  const EnqueueResult stopResult = queue.enqueue(stop);
  TEST_ASSERT_TRUE(stopResult.accepted);
  TEST_ASSERT_EQUAL_UINT16(PendingQueue::kCapacity, stopResult.cancelled);
  TEST_ASSERT_EQUAL_UINT8(PendingQueue::kCapacity,
                          stopResult.cancelledSequenceCount);
  TEST_ASSERT_EQUAL_UINT32(1, stopResult.cancelledSequences[0]);
  TEST_ASSERT_EQUAL_UINT32(PendingQueue::kCapacity,
                           stopResult.cancelledSequences[PendingQueue::kCapacity - 1]);
  TEST_ASSERT_EQUAL_UINT16(1, queue.pending());
  stop.command.sequence = 102;
  const EnqueueResult repeatedStop = queue.enqueue(stop);
  TEST_ASSERT_TRUE(repeatedStop.accepted);
  TEST_ASSERT_TRUE(repeatedStop.coalesced);
  TEST_ASSERT_EQUAL_UINT16(1, queue.pending());

  PendingCommand next;
  TEST_ASSERT_TRUE(queue.dequeue(next));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Operation::SafeStopAll),
                          static_cast<uint8_t>(next.operation));
  TEST_ASSERT_FALSE(queue.dequeue(next));
}

struct SnapshotStressValue {
  uint32_t generation = 0;
  uint32_t inverseGeneration = 0;
  uint32_t words[48] = {};
};

class HostTimedMutex {
public:
  bool init() { return true; }
  bool try_lock() { return mutex_.try_lock(); }
  bool try_lock_for(uint32_t timeoutMs) {
    return mutex_.try_lock_for(std::chrono::milliseconds(timeoutMs));
  }
  void unlock() { mutex_.unlock(); }

private:
  std::timed_mutex mutex_;
};

void test_snapshot_buffer_is_coherent_under_concurrent_publish_and_read() {
  StateSnapshotLogic::Buffer<SnapshotStressValue, HostTimedMutex> snapshot;
  TEST_ASSERT_TRUE(snapshot.init());

  constexpr uint32_t kPublishCount = 30000;
  std::atomic<bool> finished{false};
  std::atomic<uint32_t> published{0};
  std::atomic<uint32_t> corruptReads{0};

  std::thread writer([&]() {
    for (uint32_t generation = 1; generation <= kPublishCount; ++generation) {
      SnapshotStressValue value;
      value.generation = generation;
      value.inverseGeneration = ~generation;
      for (uint32_t i = 0; i < 48; ++i) {
        value.words[i] = generation * 2654435761u + i * 2246822519u;
      }
      while (!snapshot.publish(value, generation)) std::this_thread::yield();
      published.fetch_add(1, std::memory_order_relaxed);
    }
    finished.store(true, std::memory_order_release);
  });

  auto reader = [&]() {
    do {
      SnapshotStressValue value;
      if (!snapshot.read(value, 50)) continue;
      bool valid = value.inverseGeneration == ~value.generation;
      for (uint32_t i = 0; valid && i < 48; ++i) {
        valid = value.words[i] ==
                value.generation * 2654435761u + i * 2246822519u;
      }
      if (!valid) corruptReads.fetch_add(1, std::memory_order_relaxed);
    } while (!finished.load(std::memory_order_acquire));
  };
  std::thread readerA(reader);
  std::thread readerB(reader);
  writer.join();
  readerA.join();
  readerB.join();

  SnapshotStressValue finalValue;
  TEST_ASSERT_TRUE(snapshot.read(finalValue, 50));
  TEST_ASSERT_EQUAL_UINT32(kPublishCount, published.load());
  TEST_ASSERT_EQUAL_UINT32(kPublishCount, snapshot.sequence(50));
  TEST_ASSERT_EQUAL_UINT32(kPublishCount, finalValue.generation);
  TEST_ASSERT_EQUAL_UINT32(0, corruptReads.load());
}

void test_power_output_state_keeps_requested_and_applied_values_separate() {
  using namespace PowerCommandGateway;
  OutputState state;

  Command applied = makeCommand(Owner::FSM, Target::PUMP, Reason::FSM_CONTROL,
                                10, 20.0f);
  applied.sequence = 4;
  applied.applied = true;
  applied.appliedValue = 20.0f;
  PowerOutputStateLogic::record(state, applied);
  TEST_ASSERT_EQUAL_UINT32(4, state.requestedCommandId);
  TEST_ASSERT_EQUAL_UINT32(4, state.appliedCommandId);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, state.appliedValue);

  Command queued = makeCommand(Owner::MANUAL, Target::PUMP,
                               Reason::TFT_MANUAL_OUTPUT, 11, 35.0f);
  queued.sequence = 5;
  queued.queued = true;
  PowerOutputStateLogic::record(state, queued);
  TEST_ASSERT_EQUAL_UINT32(5, state.requestedCommandId);
  TEST_ASSERT_TRUE(state.requestQueued);
  TEST_ASSERT_EQUAL_UINT32(4, state.appliedCommandId);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 35.0f, state.requestedValue);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, state.appliedValue);

  // A queued command may be applied after a newer request arrives; the applied
  // snapshot advances without rolling back the latest requested state.
  applied.sequence = 4;
  applied.timestampMs = 12;
  PowerOutputStateLogic::record(state, applied);
  TEST_ASSERT_EQUAL_UINT32(5, state.requestedCommandId);
  TEST_ASSERT_EQUAL_UINT32(4, state.appliedCommandId);

  Command rejected = makeCommand(Owner::MANUAL, Target::PUMP,
                                 Reason::TFT_MANUAL_OUTPUT, 13, 50.0f);
  rejected.sequence = 6;
  rejected.accepted = false;
  rejected.rejectReason = RejectReason::OWNER_PRIORITY;
  PowerOutputStateLogic::record(state, rejected);
  TEST_ASSERT_FALSE(state.requestAccepted);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RejectReason::OWNER_PRIORITY),
                          static_cast<uint8_t>(state.rejectReason));
  TEST_ASSERT_EQUAL_UINT32(4, state.appliedCommandId);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, state.appliedValue);
}

void test_power_gateway_queue_clear_discards_all_pending_output_commands() {
  using namespace PowerCommandGateway;
  using namespace PowerCommandGatewayLogic;

  PendingQueue queue;
  PendingCommand command;
  command.operation = Operation::HeaterPercent;
  command.command = makeCommand(Owner::MANUAL, Target::HEATER,
                                Reason::TFT_MANUAL_OUTPUT, 1, 20.0f);
  TEST_ASSERT_TRUE(queue.enqueue(command).accepted);

  command.operation = Operation::PumpSpeed;
  command.command = makeCommand(Owner::SERVICE, Target::PUMP,
                                Reason::TFT_DOSING, 2, 0.0f, true);
  TEST_ASSERT_TRUE(queue.enqueue(command).accepted);
  command.operation = Operation::SafeStopAll;
  command.command = makeCommand(Owner::SAFETY, Target::ALL_OUTPUTS,
                                Reason::SAFETY_TRIP, 3, 0.0f, true);
  TEST_ASSERT_TRUE(queue.enqueue(command).accepted);
  command.operation = Operation::PumpSpeed;
  command.command = makeCommand(Owner::SERVICE, Target::PUMP,
                                Reason::TFT_DOSING, 4, 0.0f, true);
  TEST_ASSERT_TRUE(queue.enqueue(command).accepted);

  TEST_ASSERT_EQUAL_UINT16(2, queue.pending());
  TEST_ASSERT_EQUAL_UINT16(2, queue.clear());
  TEST_ASSERT_EQUAL_UINT16(0, queue.pending());
}

void test_power_gateway_pump_stop_cancels_stale_starts_and_has_priority() {
  using namespace PowerCommandGateway;
  using namespace PowerCommandGatewayLogic;

  PendingQueue queue;
  PendingCommand pumpStart;
  pumpStart.operation = Operation::PumpSpeed;
  pumpStart.command = makeCommand(Owner::MANUAL, Target::PUMP,
                                  Reason::TFT_MANUAL_OUTPUT, 1, 500.0f);
  pumpStart.command.sequence = 1;
  TEST_ASSERT_TRUE(queue.enqueue(pumpStart).accepted);

  PendingCommand unrelated;
  unrelated.operation = Operation::HeaterPercent;
  unrelated.command = makeCommand(Owner::FSM, Target::HEATER,
                                  Reason::FSM_CONTROL, 2, 30.0f);
  TEST_ASSERT_TRUE(queue.enqueue(unrelated).accepted);

  pumpStart.command.owner = Owner::SERVICE;
  pumpStart.command.sequence = 3;
  TEST_ASSERT_TRUE(queue.enqueue(pumpStart).accepted);

  PendingCommand pumpStop;
  pumpStop.operation = Operation::PumpSpeed;
  pumpStop.command = makeCommand(Owner::SERVICE, Target::PUMP,
                                 Reason::TFT_DOSING, 4, 0.0f, true);
  const EnqueueResult stopResult = queue.enqueue(pumpStop);
  TEST_ASSERT_TRUE(stopResult.accepted);
  TEST_ASSERT_EQUAL_UINT16(2, stopResult.cancelled);
  TEST_ASSERT_EQUAL_UINT8(2, stopResult.cancelledSequenceCount);
  TEST_ASSERT_EQUAL_UINT32(1, stopResult.cancelledSequences[0]);
  TEST_ASSERT_EQUAL_UINT32(3, stopResult.cancelledSequences[1]);
  TEST_ASSERT_EQUAL_UINT16(2, queue.pending());

  PendingCommand next;
  TEST_ASSERT_TRUE(queue.dequeue(next));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f,
                           next.command.requestedValue);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Operation::PumpSpeed),
                          static_cast<uint8_t>(next.operation));
  TEST_ASSERT_TRUE(queue.dequeue(next));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Operation::HeaterPercent),
                          static_cast<uint8_t>(next.operation));
  TEST_ASSERT_FALSE(queue.dequeue(next));

  pumpStart.command.sequence = 5;
  TEST_ASSERT_TRUE(queue.enqueue(pumpStop).accepted);
  TEST_ASSERT_TRUE(queue.enqueue(pumpStart).accepted);
  TEST_ASSERT_TRUE(queue.dequeue(next));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f,
                           next.command.requestedValue);
  TEST_ASSERT_TRUE(queue.dequeue(next));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 500.0f,
                           next.command.requestedValue);
}

void test_power_command_safe_stop_rules_match_output_semantics() {
  using namespace PowerCommandGateway;

  TEST_ASSERT_TRUE(safeStopAppliedForRequest(Target::HEATER, 0.0f));
  TEST_ASSERT_FALSE(safeStopAppliedForRequest(Target::HEATER, 1.0f));

  TEST_ASSERT_TRUE(safeStopAppliedForRequest(Target::PUMP, 0.0f));
  TEST_ASSERT_TRUE(safeStopAppliedForRequest(Target::PUMP, -1.0f));
  TEST_ASSERT_FALSE(safeStopAppliedForRequest(Target::PUMP, 850.0f));

  TEST_ASSERT_TRUE(safeStopAppliedForRequest(Target::WATER_VALVE, 0.0f));
  TEST_ASSERT_FALSE(safeStopAppliedForRequest(Target::WATER_VALVE, 1.0f));
  TEST_ASSERT_TRUE(safeStopAppliedForRequest(Target::START_STOP_PWM, 0.0f));
  TEST_ASSERT_FALSE(safeStopAppliedForRequest(Target::START_STOP_PWM, 32.0f));

  TEST_ASSERT_FALSE(safeStopAppliedForRequest(Target::FRACTIONATOR, 0.0f));
  TEST_ASSERT_FALSE(safeStopAppliedForRequest(Target::FRACTIONATOR, 2.0f));
  TEST_ASSERT_TRUE(safeStopAppliedForRequest(Target::ALL_VALVES, 0.0f));
  TEST_ASSERT_TRUE(safeStopAppliedForRequest(Target::ALL_OUTPUTS, 0.0f));
}

void test_power_gateway_safety_trip_blocks_energizing_requests() {
  using namespace PowerCommandGateway;

  TEST_ASSERT_TRUE(safetyTripAllows(false, Owner::MANUAL, Target::PUMP,
                                    500.0f));
  TEST_ASSERT_FALSE(safetyTripAllows(true, Owner::MANUAL, Target::PUMP,
                                     500.0f));
  TEST_ASSERT_FALSE(safetyTripAllows(true, Owner::SERVICE, Target::HEATER,
                                     40.0f));
  TEST_ASSERT_FALSE(safetyTripAllows(true, Owner::FSM,
                                     Target::HEATER_TRIAC_DELAY, 0.0f));
  TEST_ASSERT_TRUE(safetyTripAllows(true, Owner::MANUAL, Target::PUMP,
                                    0.0f));
  TEST_ASSERT_TRUE(safetyTripAllows(true, Owner::FSM, Target::WATER_VALVE,
                                    0.0f));
  TEST_ASSERT_TRUE(safetyTripAllows(true, Owner::SAFETY, Target::ALL_OUTPUTS,
                                    0.0f));
  TEST_ASSERT_FALSE(safetyTripAllows(true, Owner::MANUAL,
                                     Target::FRACTIONATOR, 0.0f));
}

void test_power_gateway_owner_priority_is_safety_then_fsm_service_manual() {
  using namespace PowerCommandGateway;

  TEST_ASSERT_TRUE(ownerPriority(Owner::SAFETY) > ownerPriority(Owner::FSM));
  TEST_ASSERT_TRUE(ownerPriority(Owner::FSM) > ownerPriority(Owner::SERVICE));
  TEST_ASSERT_TRUE(ownerPriority(Owner::SERVICE) > ownerPriority(Owner::MANUAL));
  TEST_ASSERT_TRUE(ownerMeetsFloor(Owner::SAFETY, Owner::FSM));
  TEST_ASSERT_TRUE(ownerMeetsFloor(Owner::FSM, Owner::SERVICE));
  TEST_ASSERT_FALSE(ownerMeetsFloor(Owner::SERVICE, Owner::FSM));
  TEST_ASSERT_TRUE(ownerMeetsFloor(Owner::SERVICE, Owner::MANUAL));
  TEST_ASSERT_FALSE(ownerMeetsFloor(Owner::MANUAL, Owner::SERVICE));
  TEST_ASSERT_TRUE(ownerFloorAllows(Owner::SERVICE, Owner::FSM, Target::PUMP,
                                    0.0f));
  TEST_ASSERT_FALSE(ownerFloorAllows(Owner::SERVICE, Owner::FSM, Target::PUMP,
                                     500.0f));
}

void test_power_command_pulse_contract_is_self_terminating() {
  using namespace PowerCommandGateway;

  TEST_ASSERT_TRUE(safeStopAppliedForPulse(Target::WATER_VALVE));
  TEST_ASSERT_TRUE(safeStopAppliedForPulse(Target::HEADS_VALVE));
  TEST_ASSERT_TRUE(safeStopAppliedForPulse(Target::BODY_VALVE));
  TEST_ASSERT_TRUE(safeStopAppliedForPulse(Target::TAILS_VALVE));
  TEST_ASSERT_TRUE(safeStopAppliedForPulse(Target::UNO_VALVE));
  TEST_ASSERT_FALSE(safeStopAppliedForPulse(Target::PUMP));
  TEST_ASSERT_FALSE(safeStopAppliedForPulse(Target::FRACTIONATOR));
}

void test_power_command_service_safe_stop_keeps_all_outputs_auditable() {
  using namespace PowerCommandGateway;

  Command command = makeCommand(
      Owner::SERVICE, Target::ALL_OUTPUTS, Reason::TFT_SERVICE_TEST, 77U, 0.0f,
      safeStopAppliedForRequest(Target::ALL_OUTPUTS, 0.0f));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Owner::SERVICE),
                          static_cast<uint8_t>(command.owner));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Target::ALL_OUTPUTS),
                          static_cast<uint8_t>(command.target));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Reason::TFT_SERVICE_TEST),
                          static_cast<uint8_t>(command.reason));
  TEST_ASSERT_EQUAL_UINT32(77U, command.timestampMs);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, command.requestedValue);
  TEST_ASSERT_TRUE(command.safeStopRequired);
  TEST_ASSERT_TRUE(command.safeStopApplied);
}

void test_power_command_fsm_control_reason_covers_control_loop_outputs() {
  using namespace PowerCommandGateway;

  Command command = makeCommand(Owner::FSM, Target::PRODUCT_ROUTE,
                                Reason::FSM_CONTROL, 91U, 0.0f,
                                safeStopAppliedForRequest(
                                    Target::PRODUCT_ROUTE, 0.0f));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Owner::FSM),
                          static_cast<uint8_t>(command.owner));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Reason::FSM_CONTROL),
                          static_cast<uint8_t>(command.reason));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Target::PRODUCT_ROUTE),
                          static_cast<uint8_t>(command.target));
  TEST_ASSERT_TRUE(command.safeStopApplied);
}

void test_power_command_all_valves_stop_does_not_claim_full_output_stop() {
  using namespace PowerCommandGateway;

  Command command = makeCommand(
      Owner::FSM, Target::ALL_VALVES, Reason::FSM_CONTROL, 92U, 0.0f,
      safeStopAppliedForRequest(Target::ALL_VALVES, 0.0f));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Target::ALL_VALVES),
                          static_cast<uint8_t>(command.target));
  TEST_ASSERT_TRUE(command.safeStopApplied);
  TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(Target::ALL_OUTPUTS),
                        static_cast<uint8_t>(command.target));
}

void test_tft_service_test_hides_unavailable_hardware() {
  using namespace TftServiceTestLogic;

  Availability board;
  board.heater = true;
  board.pump = true;
  board.waterValve = true;
  board.headsValve = true;
  board.sensors = true;

  TEST_ASSERT_TRUE(isAvailable(Device::Heater, board));
  TEST_ASSERT_TRUE(isAvailable(Device::Pump, board));
  TEST_ASSERT_TRUE(isAvailable(Device::WaterValve, board));
  TEST_ASSERT_TRUE(isAvailable(Device::Sensors, board));
  TEST_ASSERT_FALSE(isAvailable(Device::Stirrer, board));
  TEST_ASSERT_FALSE(isAvailable(Device::BodyValve, board));
  TEST_ASSERT_FALSE(isAvailable(Device::TailsValve, board));
  TEST_ASSERT_FALSE(isAvailable(Device::CoolingPwm, board));
  TEST_ASSERT_FALSE(isAvailable(Device::Fractionator, board));
}

void test_service_test_session_requires_safe_pause_and_explicit_restore() {
  using namespace ServiceTestSessionLogic;

  State session;
  TEST_ASSERT_FALSE(request(session, 1, false, false, Target::PUMP));
  TEST_ASSERT_TRUE(request(session, 1, true, false, Target::PUMP));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::REQUESTED),
                          static_cast<uint8_t>(session.phase));
  TEST_ASSERT_TRUE(blocksProcessResume(session));
  TEST_ASSERT_FALSE(startTest(session, Target::PUMP));
  TEST_ASSERT_TRUE(markSafePaused(session));
  TEST_ASSERT_TRUE(mayUsePausedProcess(session, 1, true));
  TEST_ASSERT_TRUE(startTest(session, Target::PUMP));
  TEST_ASSERT_FALSE(startTest(session, Target::HEATER));
  TEST_ASSERT_TRUE(beginStopping(session));
  TEST_ASSERT_TRUE(markRestoreReady(session));
  TEST_ASSERT_TRUE(mayResumeProcess(session));
  close(session);
  TEST_ASSERT_FALSE(blocksProcessResume(session));

  TEST_ASSERT_TRUE(request(session, 2, true, true, Target::WATER_VALVE));
  TEST_ASSERT_TRUE(markSafePaused(session));
  TEST_ASSERT_TRUE(beginStopping(session));
  TEST_ASSERT_TRUE(markRestoreReady(session));
  TEST_ASSERT_TRUE(mayCloseWithoutResume(session));
  TEST_ASSERT_FALSE(mayResumeProcess(session));
  close(session);

  TEST_ASSERT_TRUE(request(session, 2, true, false, Target::HEATER));
  TEST_ASSERT_TRUE(fault(session));
  TEST_ASSERT_TRUE(blocksProcessResume(session));
  TEST_ASSERT_FALSE(mayResumeProcess(session));
}

void test_pump_calibration_requires_prime_measure_review_and_commit() {
  using namespace PumpCalibrationLogic;

  State calibration;
  TEST_ASSERT_FALSE(beginMeasurement(calibration, 600.0f, 10, false));
  TEST_ASSERT_TRUE(beginPriming(calibration, 600.0f, 10));
  TEST_ASSERT_TRUE(outputActive(calibration));
  TEST_ASSERT_TRUE(finishPriming(calibration));
  TEST_ASSERT_TRUE(beginMeasurement(calibration, 600.0f, 20));
  TEST_ASSERT_TRUE(stopMeasurement(calibration, 6400, 3020));
  TEST_ASSERT_FALSE(enterMeasuredVolume(calibration, 0.0f, 3200));
  TEST_ASSERT_TRUE(enterMeasuredVolume(calibration, 2.5f, 3200));
  TEST_ASSERT_TRUE(mayCommit(calibration));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.25f,
                           calibration.candidateMlPerRev);
  TEST_ASSERT_TRUE(retry(calibration));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::READY_TO_MEASURE),
                          static_cast<uint8_t>(calibration.phase));
}

void test_pump_calibration_cloud_measurement_skips_prime_but_keeps_review() {
  using namespace PumpCalibrationLogic;

  State calibration;
  TEST_ASSERT_TRUE(beginMeasurement(calibration, 600.0f, 10, true));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::MEASURING),
                          static_cast<uint8_t>(calibration.phase));

  calibration.startSteps = 400;
  TEST_ASSERT_TRUE(stopMeasurement(calibration, 2000, 3010));
  TEST_ASSERT_TRUE(enterMeasuredVolume(calibration, 4.0f, 1600));
  TEST_ASSERT_TRUE(mayCommit(calibration));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 4.0f, calibration.candidateMlPerRev);
}

void test_pump_calibration_time_fallback_requires_completed_zero_step_measurement() {
  using namespace PumpCalibrationLogic;

  State calibration;
  TEST_ASSERT_TRUE(beginPriming(calibration, 600.0f, 1000));
  TimeFallbackAssessment assessment =
      assessTimeFallback(calibration, 10.0f, 1.0f, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ChangeStatus::Invalid),
                          static_cast<uint8_t>(assessment.status));

  TEST_ASSERT_TRUE(finishPriming(calibration));
  TEST_ASSERT_TRUE(beginMeasurement(calibration, 600.0f, 2000));
  assessment = assessTimeFallback(calibration, 10.0f, 1.0f, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ChangeStatus::Invalid),
                          static_cast<uint8_t>(assessment.status));
  TEST_ASSERT_TRUE(stopMeasurement(calibration, 0, 62000));
  TEST_ASSERT_TRUE(calibration.measurementCompleted);
  TEST_ASSERT_TRUE(calibration.stepCounterValid);
  assessment = assessTimeFallback(calibration, 10.0f, 1.0f, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ChangeStatus::Accepted),
                          static_cast<uint8_t>(assessment.status));
  TEST_ASSERT_EQUAL_UINT32(60000, assessment.durationMs);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, assessment.expectedVolumeMl);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, assessment.candidateMlPerRev);

  calibration = State{};
  TEST_ASSERT_TRUE(beginMeasurement(calibration, 600.0f, 2000, true));
  TEST_ASSERT_TRUE(stopMeasurement(calibration, 0, 31999));
  assessment = assessTimeFallback(calibration, 5.0f, 1.0f, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ChangeStatus::Invalid),
                          static_cast<uint8_t>(assessment.status));

  calibration = State{};
  TEST_ASSERT_TRUE(beginMeasurement(calibration, 100.0f, 2000, true));
  TEST_ASSERT_TRUE(stopMeasurement(calibration, 0, 62000));
  assessment = assessTimeFallback(calibration, 5.0f, 1.0f, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ChangeStatus::Invalid),
                          static_cast<uint8_t>(assessment.status));

  calibration = State{};
  TEST_ASSERT_TRUE(beginMeasurement(calibration, 600.0f, 2000, true));
  TEST_ASSERT_TRUE(stopMeasurement(calibration, 3200, 62000));
  assessment = assessTimeFallback(calibration, 10.0f, 1.0f, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ChangeStatus::Invalid),
                          static_cast<uint8_t>(assessment.status));
}

void test_pump_calibration_requires_confirmation_and_hard_bounds_large_changes() {
  using namespace PumpCalibrationLogic;

  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ChangeStatus::ConfirmationRequired),
      static_cast<uint8_t>(assessCalibrationChange(1.0f, 0.4f, false)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ChangeStatus::Accepted),
                          static_cast<uint8_t>(
                              assessCalibrationChange(1.0f, 0.4f, true)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ChangeStatus::OutOfRange),
                          static_cast<uint8_t>(
                              assessCalibrationChange(1.0f, 0.2f, true)));
}

void test_sensor_calibration_keeps_ds18_mapping_unique() {
  using namespace SensorCalibrationLogic;

  uint8_t assigned[3][8] = {};
  const uint8_t first[8] = {0x28, 1, 2, 3, 4, 5, 6, 7};
  const uint8_t second[8] = {0x28, 7, 6, 5, 4, 3, 2, 1};
  const uint8_t empty[8] = {};

  TEST_ASSERT_TRUE(assignAddress(assigned, 0, first));
  TEST_ASSERT_TRUE(assignAddress(assigned, 1, second));
  TEST_ASSERT_TRUE(assignAddress(assigned, 2, first));
  TEST_ASSERT_TRUE(isZeroAddress(assigned[0]));
  TEST_ASSERT_TRUE(addressesEqual(assigned[1], second));
  TEST_ASSERT_TRUE(addressesEqual(assigned[2], first));
  TEST_ASSERT_TRUE(clearAddress(assigned, 2));
  TEST_ASSERT_TRUE(isZeroAddress(assigned[2]));
  TEST_ASSERT_FALSE(assignAddress(assigned, 1, empty));
}

void test_service_test_audit_uses_stable_lifecycle_tokens() {
  using namespace ServiceTestAuditLogic;

  TEST_ASSERT_EQUAL_STRING("requested", eventToken(Event::REQUESTED));
  TEST_ASSERT_EQUAL_STRING("changed", eventToken(Event::CHANGED));
  TEST_ASSERT_EQUAL_STRING("faulted", eventToken(Event::FAULTED));
  TEST_ASSERT_TRUE(isTerminal(Event::RESULT));
  TEST_ASSERT_TRUE(isTerminal(Event::RESTORED));
  TEST_ASSERT_FALSE(isTerminal(Event::PAUSED));
}

void test_dosing_session_requires_prime_then_tracks_only_target_volume() {
  using namespace DosingSessionLogic;

  State dosing;
  TEST_ASSERT_FALSE(configure(dosing, Route::NONE, 120.0f, 300.0f));
  TEST_ASSERT_TRUE(configure(dosing, Route::HEADS, 120.0f, 300.0f));
  TEST_ASSERT_FALSE(beginDispensing(dosing, 0.5f, 10.0f, 1));
  TEST_ASSERT_TRUE(beginPriming(dosing, 0.5f, 10));
  TEST_ASSERT_TRUE(isOutputActive(dosing));
  TEST_ASSERT_TRUE(finishPriming(dosing));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::READY),
                          static_cast<uint8_t>(dosing.phase));
  TEST_ASSERT_TRUE(beginDispensing(dosing, 0.5f, 42.0f, 20));
  updateEstimatedVolume(dosing, 119.0f);
  TEST_ASSERT_FALSE(targetReached(dosing));
  updateEstimatedVolume(dosing, 125.0f);
  TEST_ASSERT_TRUE(targetReached(dosing));
  TEST_ASSERT_TRUE(finish(dosing, FinishReason::TARGET_REACHED, 40));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::COMPLETED),
                          static_cast<uint8_t>(dosing.phase));
  TEST_ASSERT_EQUAL_STRING("target_reached",
                           finishReasonToken(dosing.finishReason));

  TEST_ASSERT_TRUE(configure(dosing, Route::TAILS, 30.0f, 200.0f));
  TEST_ASSERT_TRUE(beginPriming(dosing, 0.5f, 50));
  TEST_ASSERT_TRUE(finish(dosing, FinishReason::SAFETY, 55));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::FAULTED),
                          static_cast<uint8_t>(dosing.phase));
}

void test_dosing_session_finish_reasons_have_safe_terminal_phases() {
  using namespace DosingSessionLogic;

  const FinishReason cancelledReasons[] = {FinishReason::OPERATOR_STOP,
                                            FinishReason::PAGE_EXIT};
  for (const FinishReason reason : cancelledReasons) {
    State dosing;
    TEST_ASSERT_TRUE(configure(dosing, Route::HEADS, 100.0f, 300.0f));
    TEST_ASSERT_TRUE(beginPriming(dosing, 0.5f, 1000));
    TEST_ASSERT_TRUE(finish(dosing, reason, 2000));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::CANCELLED),
                            static_cast<uint8_t>(dosing.phase));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(reason),
                            static_cast<uint8_t>(dosing.finishReason));
  }

  const FinishReason faultReasons[] = {FinishReason::SAFETY,
                                       FinishReason::ROUTE_LOST,
                                       FinishReason::PRIME_TIMEOUT,
                                       FinishReason::TIMEOUT,
                                       FinishReason::CONFIG_INVALID};
  for (const FinishReason reason : faultReasons) {
    State dosing;
    TEST_ASSERT_TRUE(configure(dosing, Route::BODY, 100.0f, 300.0f));
    TEST_ASSERT_TRUE(beginPriming(dosing, 0.5f, 1000));
    TEST_ASSERT_TRUE(finish(dosing, reason, 2000));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::FAULTED),
                            static_cast<uint8_t>(dosing.phase));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(reason),
                            static_cast<uint8_t>(dosing.finishReason));
  }

  State dosing;
  TEST_ASSERT_TRUE(configure(dosing, Route::TAILS, 100.0f, 300.0f));
  TEST_ASSERT_TRUE(beginPriming(dosing, 0.5f, 1000));
  TEST_ASSERT_TRUE(finishPriming(dosing));
  TEST_ASSERT_TRUE(beginDispensing(dosing, 0.5f, 0.0f, 3000));
  TEST_ASSERT_TRUE(finish(dosing, FinishReason::TARGET_REACHED, 4000));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::COMPLETED),
                          static_cast<uint8_t>(dosing.phase));

  TEST_ASSERT_FALSE(finish(dosing, FinishReason::NONE, 5000));
}

void test_dosing_session_has_independent_prime_and_dose_timeouts() {
  using namespace DosingSessionLogic;
  State dosing;
  TEST_ASSERT_TRUE(configure(dosing, Route::HEADS, 100.0f, 300.0f));
  TEST_ASSERT_TRUE(beginPriming(dosing, 0.5f, 1000));
  TEST_ASSERT_FALSE(timedOut(dosing, 120999, 120000, 1800000));
  TEST_ASSERT_TRUE(timedOut(dosing, 121000, 120000, 1800000));
  TEST_ASSERT_TRUE(finish(dosing, FinishReason::PRIME_TIMEOUT, 121000));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::FAULTED),
                          static_cast<uint8_t>(dosing.phase));

  TEST_ASSERT_TRUE(configure(dosing, Route::BODY, 100.0f, 300.0f));
  TEST_ASSERT_TRUE(beginPriming(dosing, 0.5f, 200000));
  TEST_ASSERT_TRUE(finishPriming(dosing));
  TEST_ASSERT_FALSE(timedOut(dosing, 200000 + 120000, 120000, 1800000));
  TEST_ASSERT_TRUE(beginDispensing(dosing, 0.5f, 0.0f, 400000));
  TEST_ASSERT_FALSE(timedOut(dosing, 400000 + 1799999, 120000, 1800000));
  TEST_ASSERT_TRUE(timedOut(dosing, 400000 + 1800000, 120000, 1800000));
}

void test_autoclave_profile_requires_explicit_hardware_and_safety_approval() {
  using namespace AutoclaveHardwareProfileLogic;

  Profile profile;
  TEST_ASSERT_EQUAL_STRING("not_approved", validationToken(validate(profile)));

  profile.approved = true;
  profile.steamEnabled = true;
  profile.heaterAssigned = true;
  profile.cubeTemperatureAssigned = true;
  profile.pressureSensorAssigned = true;
  profile.heatPowerLimitW = 2000.0f;
  profile.pressureMaxMmHg = 30.0f;
  profile.pressureRiseRateMaxMmHgMin = 5.0f;
  TEST_ASSERT_EQUAL_STRING("mechanical_safety_unreviewed",
                           validationToken(validate(profile)));
  profile.mechanicalSafetyReviewed = true;
  TEST_ASSERT_TRUE(mayOfferSteam(profile));
  TEST_ASSERT_FALSE(mayOfferWater(profile));

  profile.waterEnabled = true;
  TEST_ASSERT_EQUAL_STRING("water_circulation_unassigned",
                           validationToken(validate(profile)));
  profile.circulationPumpAssigned = true;
  TEST_ASSERT_EQUAL_STRING("water_cooling_unassigned",
                           validationToken(validate(profile)));
  profile.coolingAssigned = true;
  TEST_ASSERT_TRUE(mayOfferWater(profile));
}

void test_autoclave_sterilization_timer_only_counts_in_temperature_band() {
  using namespace AutoclaveSessionLogic;

  Config config;
  config.sterilizationTempC = 121.0f;
  config.temperatureBandC = 1.0f;
  config.sterilizationDurationMs = 1000;
  config.coolingStopTempC = 40.0f;
  State session;
  TEST_ASSERT_TRUE(start(session, 10));
  TEST_ASSERT_FALSE(confirmPrecheck(session, false, 20));
  TEST_ASSERT_TRUE(confirmPrecheck(session, true, 20));
  TEST_ASSERT_TRUE(update(session, config, true, 120.0f, 30));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::STERILIZING),
                          static_cast<uint8_t>(session.phase));
  TEST_ASSERT_FALSE(update(session, config, true, 120.0f, 130));
  TEST_ASSERT_FALSE(update(session, config, true, 118.0f, 1130));
  TEST_ASSERT_EQUAL_UINT32(0, session.sterilizationAccumulatedMs);
  TEST_ASSERT_FALSE(update(session, config, true, 121.0f, 1230));
  TEST_ASSERT_FALSE(update(session, config, true, 121.0f, 1730));
  TEST_ASSERT_EQUAL_UINT32(500, session.sterilizationAccumulatedMs);
  TEST_ASSERT_TRUE(update(session, config, true, 121.0f, 2230));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::COOLING),
                          static_cast<uint8_t>(session.phase));
  TEST_ASSERT_TRUE(update(session, config, true, 40.0f, 3000));
  TEST_ASSERT_TRUE(finish(session, 3010));
  TEST_ASSERT_TRUE(isTerminal(session.phase));
}

void test_autoclave_safety_faults_on_required_sensor_pressure_rate_and_timeout() {
  using namespace AutoclaveSafetyLogic;

  Config config;
  config.pressureMaxMmHg = 50.0f;
  config.pressureRiseRateMaxMmHgMin = 10.0f;
  config.heatingTimeoutMs = 1000;
  State safety;
  const auto phase = AutoclaveSessionLogic::Phase::HEATING;
  TEST_ASSERT_EQUAL_STRING("temperature_sensor",
                           faultToken(evaluate(safety, config, phase, 0,
                                               false, true, 0.0f, 10)));
  TEST_ASSERT_EQUAL_STRING("pressure_sensor",
                           faultToken(evaluate(safety, config, phase, 0,
                                               true, false, 0.0f, 20)));
  TEST_ASSERT_EQUAL_STRING("none", faultToken(evaluate(
                                       safety, config, phase, 0, true, true,
                                       10.0f, 100)));
  TEST_ASSERT_EQUAL_STRING("pressure_rise_rate", faultToken(evaluate(
                                                   safety, config, phase, 0,
                                                   true, true, 20.0f, 10100)));

  State timeoutSafety;
  TEST_ASSERT_EQUAL_STRING("heating_timeout", faultToken(evaluate(
                                               timeoutSafety, config, phase,
                                               0, true, true, 10.0f, 1000)));
  State highPressureSafety;
  TEST_ASSERT_EQUAL_STRING("pressure_limit", faultToken(evaluate(
                                               highPressureSafety, config, phase,
                                               0, true, true, 51.0f, 10)));
}
