#include "control/state_snapshot.h"

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "runtime_metrics.h"
#include "state_snapshot_logic.h"

namespace {

class FreeRtosSnapshotMutex {
public:
    bool init() {
        if (semaphore_ != nullptr) return true;
        semaphore_ = xSemaphoreCreateMutex();
        return semaphore_ != nullptr;
    }
    bool try_lock() {
        return semaphore_ != nullptr && xSemaphoreTake(semaphore_, 0) == pdTRUE;
    }
    bool try_lock_for(uint32_t timeoutMs) {
        return semaphore_ != nullptr &&
               xSemaphoreTake(semaphore_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    }
    void unlock() { xSemaphoreGive(semaphore_); }

private:
    SemaphoreHandle_t semaphore_ = nullptr;
};

StateSnapshotLogic::Buffer<SystemState, FreeRtosSnapshotMutex> sSnapshot;
std::atomic<uint32_t> sRetries{0};
std::atomic<uint32_t> sPublishSkips{0};

}  // namespace

namespace StateSnapshot {

bool init() {
    return sSnapshot.init();
}

bool publish(const SystemState& state) {
    const uint32_t copyStartedAtUs = micros();
    if (!sSnapshot.publish(state, millis())) {
        sPublishSkips.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const uint32_t copyDurationUs = micros() - copyStartedAtUs;
    ControlRuntimeMetrics::recordSnapshotCopy(copyDurationUs);
    return true;
}

bool read(SystemState& out) {
    if (!sSnapshot.read(out, 20)) {
        sRetries.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

bool isReady() { return sSnapshot.isReady(20); }

uint32_t sequence() { return sSnapshot.sequence(20); }

uint32_t ageMs(uint32_t nowMs) { return sSnapshot.ageMs(nowMs, 20); }

uint32_t retryCount() { return sRetries.load(std::memory_order_relaxed); }

uint32_t publishSkipCount() {
    return sPublishSkips.load(std::memory_order_relaxed);
}

}  // namespace StateSnapshot
