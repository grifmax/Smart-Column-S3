/**
 * Smart-Column S3 - Pump driver
 *
 * Stepper motor NEMA17 + TMC2209 driver.
 * AccelStepper is kept, but the worker task is now cooperative so it does not
 * starve the idle task and trigger Task WDT resets while the pump is running.
 */

#include "pump.h"

#include <AccelStepper.h>
#include <math.h>
#include <freertos/semphr.h>

static AccelStepper stepper(AccelStepper::DRIVER, PIN_PUMP_STEP, PIN_PUMP_DIR);

static float mlPerRevolution = DEFAULT_PUMP_ML_PER_REV;
static float currentSpeedMlH = 0.0f;
static float appliedSpeedMlH = 0.0f;
static float targetStepsPerSec = 0.0f;
static float appliedStepsPerSec = 0.0f;
static volatile bool running = false;
static int32_t totalSteps = 0;
static float totalVolumeMl = 0.0f;
static TaskHandle_t pumpTaskHandle = NULL;
static SemaphoreHandle_t pumpMutex = NULL;
static uint32_t pumpTaskLoopCount = 0;
static uint32_t pumpCounterUpdateCount = 0;
static uint32_t pumpCooperativeSleepCount = 0;
static uint32_t pumpFastYieldCount = 0;
static uint32_t pumpLockTimeoutCount = 0;
static uint32_t pumpLastLoopAtMs = 0;
static uint32_t pumpLastYieldAtMs = 0;

static constexpr uint32_t kPumpCounterUpdateMs = 100;
static constexpr TickType_t kPumpIdleDelayTicks = pdMS_TO_TICKS(10);
static constexpr float kPumpSpeedRampStepsPerSec2 = 2000.0f;
static constexpr uint8_t kPumpMinPulseWidthUs = 4;

static void updateTotalsFromPosition(long currentPos) {
    totalSteps = currentPos;
    const float revolutions =
        static_cast<float>(totalSteps) / (PUMP_STEPS_PER_REV * PUMP_MICROSTEPS);
    totalVolumeMl = revolutions * mlPerRevolution;
}

static bool lockPump(TickType_t timeoutTicks = portMAX_DELAY) {
    if (pumpMutex == NULL) {
        return false;
    }

    const bool locked = xSemaphoreTake(pumpMutex, timeoutTicks) == pdTRUE;
    if (!locked) {
        pumpLockTimeoutCount++;
    }
    return locked;
}

static void unlockPump() {
    if (pumpMutex != NULL) {
        xSemaphoreGive(pumpMutex);
    }
}

static float mlPerHourToStepsPerSec(float mlPerHour) {
    const float revPerHour = mlPerHour / mlPerRevolution;
    const float revPerSec = revPerHour / 3600.0f;
    return revPerSec * PUMP_STEPS_PER_REV * PUMP_MICROSTEPS;
}

static float stepsPerSecToMlPerHour(float stepsPerSec) {
    const float revPerSec = stepsPerSec / (PUMP_STEPS_PER_REV * PUMP_MICROSTEPS);
    const float revPerHour = revPerSec * 3600.0f;
    return revPerHour * mlPerRevolution;
}

static float clampStepsPerSec(float stepsPerSec) {
    if (stepsPerSec < 0.0f) {
        return 0.0f;
    }
    if (stepsPerSec > PUMP_MAX_SPEED) {
        return PUMP_MAX_SPEED;
    }
    return stepsPerSec;
}

static void applyTargetSpeed(float mlPerHour) {
    float stepsPerSec = mlPerHourToStepsPerSec(mlPerHour);
    stepsPerSec = clampStepsPerSec(stepsPerSec);
    targetStepsPerSec = stepsPerSec;
    currentSpeedMlH = stepsPerSecToMlPerHour(stepsPerSec);
}

static void updateAppliedSpeed(uint32_t nowUs) {
    static uint32_t lastSpeedUpdateUs = 0;

    if (lastSpeedUpdateUs == 0) {
        lastSpeedUpdateUs = nowUs;
    }

    const uint32_t deltaUs = nowUs - lastSpeedUpdateUs;
    lastSpeedUpdateUs = nowUs;
    const float deltaSec = static_cast<float>(deltaUs) / 1000000.0f;
    const float maxDelta = kPumpSpeedRampStepsPerSec2 * deltaSec;
    const float speedError = targetStepsPerSec - appliedStepsPerSec;

    if (fabsf(speedError) <= maxDelta || maxDelta <= 0.0f) {
        appliedStepsPerSec = targetStepsPerSec;
    } else if (speedError > 0.0f) {
        appliedStepsPerSec += maxDelta;
    } else {
        appliedStepsPerSec -= maxDelta;
    }

    appliedStepsPerSec = clampStepsPerSec(appliedStepsPerSec);
    appliedSpeedMlH = stepsPerSecToMlPerHour(appliedStepsPerSec);
    stepper.setSpeed(appliedStepsPerSec);
}

static void pumpTask(void* pvParameters) {
    uint32_t lastCounterUpdate = 0;

    while (1) {
        pumpTaskLoopCount++;
        pumpLastLoopAtMs = millis();

        if (!running) {
            pumpCooperativeSleepCount++;
            pumpLastYieldAtMs = pumpLastLoopAtMs;
            vTaskDelay(kPumpIdleDelayTicks);
            continue;
        }

        if (lockPump(portMAX_DELAY)) {
            updateAppliedSpeed(micros());
            stepper.runSpeed();

            const uint32_t nowMs = millis();
            if (nowMs - lastCounterUpdate >= kPumpCounterUpdateMs) {
                lastCounterUpdate = nowMs;
                pumpCounterUpdateCount++;
                const long currentPos = stepper.currentPosition();
                if (currentPos != totalSteps) {
                    updateTotalsFromPosition(currentPos);
                }
            }

            unlockPump();
        }

        pumpFastYieldCount++;
        pumpLastYieldAtMs = millis();
        taskYIELD();
    }
}

namespace Pump {

void init() {
    LOG_I("Pump: Initializing...");

    pinMode(PIN_PUMP_EN, OUTPUT);
    digitalWrite(PIN_PUMP_EN, HIGH);

    stepper.setMaxSpeed(PUMP_MAX_SPEED);
    stepper.setAcceleration(PUMP_ACCELERATION);
    stepper.setMinPulseWidth(kPumpMinPulseWidthUs);
    stepper.setCurrentPosition(0);

    totalSteps = 0;
    totalVolumeMl = 0.0f;
    currentSpeedMlH = 0.0f;
    appliedSpeedMlH = 0.0f;
    targetStepsPerSec = 0.0f;
    appliedStepsPerSec = 0.0f;
    running = false;
    pumpTaskLoopCount = 0;
    pumpCounterUpdateCount = 0;
    pumpCooperativeSleepCount = 0;
    pumpFastYieldCount = 0;
    pumpLockTimeoutCount = 0;
    pumpLastLoopAtMs = 0;
    pumpLastYieldAtMs = 0;

    if (pumpMutex == NULL) {
        pumpMutex = xSemaphoreCreateMutex();
    }
    if (pumpMutex == NULL) {
        LOG_E("Pump: Failed to create mutex");
        return;
    }

    if (pumpTaskHandle == NULL) {
        xTaskCreatePinnedToCore(
            pumpTask,
            "PumpTask",
            2048,
            NULL,
            tskIDLE_PRIORITY,
            &pumpTaskHandle,
            1);
    }

    LOG_I("Pump: Init complete (microsteps=%d, ml/rev=%.2f, minPulse=%u us)",
          PUMP_MICROSTEPS, mlPerRevolution, kPumpMinPulseWidthUs);
}

void start(float mlPerHour) {
    if (mlPerHour <= 0.0f) {
        stop();
        return;
    }

    digitalWrite(PIN_PUMP_EN, LOW);

    if (lockPump(portMAX_DELAY)) {
        appliedStepsPerSec = 0.0f;
        appliedSpeedMlH = 0.0f;
        stepper.setSpeed(0.0f);
        unlockPump();
    }

    applyTargetSpeed(mlPerHour);
    running = true;

    LOG_I("Pump: Started at %.1f ml/h target (%.1f steps/s target)",
          currentSpeedMlH, targetStepsPerSec);
}

void stop() {
    running = false;
    currentSpeedMlH = 0.0f;
    targetStepsPerSec = 0.0f;
    appliedSpeedMlH = 0.0f;
    appliedStepsPerSec = 0.0f;

    if (lockPump(portMAX_DELAY)) {
        stepper.setSpeed(0.0f);
        stepper.stop();
        updateTotalsFromPosition(stepper.currentPosition());
        unlockPump();
    }

    digitalWrite(PIN_PUMP_EN, HIGH);
    LOG_I("Pump: Stopped at %d", totalSteps);
}

void setSpeed(float mlPerHour) {
    if (!running) {
        start(mlPerHour);
        return;
    }

    if (mlPerHour <= 0.0f) {
        stop();
        return;
    }

    applyTargetSpeed(mlPerHour);
    LOG_D("Pump: Speed changed to %.1f ml/h target", currentSpeedMlH);
}

float getSpeed() {
    return currentSpeedMlH;
}

bool isRunning() {
    return running;
}

float getTotalVolume() {
    return totalVolumeMl;
}

uint32_t getTotalSteps() {
    if (lockPump(pdMS_TO_TICKS(5))) {
        const uint32_t currentSteps = static_cast<uint32_t>(stepper.currentPosition());
        unlockPump();
        return currentSteps;
    }

    return static_cast<uint32_t>(totalSteps);
}

float getMaxSpeedMlH() {
    return stepsPerSecToMlPerHour(PUMP_MAX_SPEED);
}

void resetVolume() {
    totalSteps = 0;
    totalVolumeMl = 0.0f;

    if (lockPump(portMAX_DELAY)) {
        stepper.setCurrentPosition(0);
        unlockPump();
    }

    LOG_I("Pump: Volume reset");
}

void setCalibration(float mlPerRev) {
    if (mlPerRev > 0.0f && mlPerRev < 10.0f) {
        mlPerRevolution = mlPerRev;
        LOG_I("Pump: Calibration set to %.3f ml/rev", mlPerRev);

        if (running) {
            setSpeed(currentSpeedMlH);
        }
    } else {
        LOG_E("Pump: Invalid calibration value %.3f", mlPerRev);
    }
}

Diagnostics getDiagnostics() {
    Diagnostics diagnostics;
    const uint32_t nowMs = millis();
    diagnostics.taskAlive =
        pumpTaskHandle != NULL && pumpLastLoopAtMs > 0 && (nowMs - pumpLastLoopAtMs) < 2000;
    diagnostics.mutexReady = pumpMutex != NULL;
    diagnostics.taskLoopCount = pumpTaskLoopCount;
    diagnostics.counterUpdateCount = pumpCounterUpdateCount;
    diagnostics.cooperativeSleepCount = pumpCooperativeSleepCount;
    diagnostics.fastYieldCount = pumpFastYieldCount;
    diagnostics.lockTimeoutCount = pumpLockTimeoutCount;
    diagnostics.lastLoopAtMs = pumpLastLoopAtMs;
    diagnostics.lastYieldAtMs = pumpLastYieldAtMs;
    diagnostics.speedMlH = currentSpeedMlH;
    diagnostics.appliedSpeedMlH = appliedSpeedMlH;
    diagnostics.totalSteps = static_cast<uint32_t>(totalSteps);
    diagnostics.totalVolumeMl = totalVolumeMl;
    return diagnostics;
}

} // namespace Pump
