#pragma once

#include <stdint.h>
#include <limits.h>

namespace StateSnapshotLogic {

// Mutex must provide init(), try_lock(), try_lock_for(milliseconds), unlock().
// The exact same storage/copy boundary is used by firmware and host concurrency
// tests so the test exercises the production publication semantics.
template <typename T, typename Mutex> class Buffer {
public:
  bool init() { return mutex_.init(); }

  bool publish(const T &value, uint32_t publishedAtMs) {
    if (!mutex_.try_lock()) return false;
    buffer_ = value;
    ++sequence_;
    lastPublishMs_ = publishedAtMs;
    ready_ = true;
    mutex_.unlock();
    return true;
  }

  bool read(T &out, uint32_t timeoutMs) {
    if (!mutex_.try_lock_for(timeoutMs)) return false;
    const bool available = ready_;
    if (available) out = buffer_;
    mutex_.unlock();
    return available;
  }

  bool isReady(uint32_t timeoutMs) {
    if (!mutex_.try_lock_for(timeoutMs)) return false;
    const bool available = ready_;
    mutex_.unlock();
    return available;
  }

  uint32_t sequence(uint32_t timeoutMs) {
    if (!mutex_.try_lock_for(timeoutMs)) return 0;
    const uint32_t value = sequence_;
    mutex_.unlock();
    return value;
  }

  uint32_t ageMs(uint32_t nowMs, uint32_t timeoutMs) {
    if (!mutex_.try_lock_for(timeoutMs)) return UINT32_MAX;
    const bool available = ready_;
    const uint32_t publishedAtMs = lastPublishMs_;
    mutex_.unlock();
    return available ? static_cast<uint32_t>(nowMs - publishedAtMs)
                     : UINT32_MAX;
  }

private:
  Mutex mutex_;
  T buffer_{};
  uint32_t sequence_ = 0;
  uint32_t lastPublishMs_ = 0;
  bool ready_ = false;
};

}  // namespace StateSnapshotLogic
