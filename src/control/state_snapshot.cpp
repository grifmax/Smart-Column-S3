#include "control/state_snapshot.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

namespace {

portMUX_TYPE sMux = portMUX_INITIALIZER_UNLOCKED;
SystemState sBuffer;
volatile uint32_t sSequence = 0;
volatile bool sReady = false;
volatile uint32_t sRetries = 0;

inline uint32_t readSequence() {
    uint32_t value;
    portENTER_CRITICAL(&sMux);
    value = sSequence;
    portEXIT_CRITICAL(&sMux);
    return value;
}

inline void writeSequence(uint32_t value) {
    portENTER_CRITICAL(&sMux);
    sSequence = value;
    portEXIT_CRITICAL(&sMux);
}

}  // namespace

namespace StateSnapshot {

void publish(const SystemState& state) {
    // Нечётная последовательность = идёт запись, читатель обязан повторить.
    writeSequence(readSequence() + 1);
    __sync_synchronize();

    sBuffer = state;

    __sync_synchronize();
    writeSequence(readSequence() + 1);
    sReady = true;
}

bool read(SystemState& out) {
    if (!sReady) {
        return false;
    }

    for (uint8_t attempt = 0; attempt < 4; ++attempt) {
        const uint32_t before = readSequence();
        if ((before & 1U) != 0U) {
            continue;  // публикация в процессе
        }

        out = sBuffer;

        if (readSequence() == before) {
            return true;
        }
        sRetries++;
    }

    return false;
}

void readOrLive(SystemState& out) {
    if (!read(out)) {
        out = g_state;
    }
}

bool isReady() { return sReady; }

uint32_t sequence() { return sSequence; }

uint32_t retryCount() { return sRetries; }

}  // namespace StateSnapshot