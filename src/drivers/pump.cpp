/**
 * Smart-Column S3 - Pump driver
 *
 * Stepper motor NEMA17 + TMC2209 driver.
 * AccelStepper is kept, but the worker task is now cooperative so it does not
 * starve the idle task and trigger Task WDT resets while the pump is running.
 */

#include "pump.h"

#include <AccelStepper.h>
#include <freertos/semphr.h>

static AccelStepper stepper(AccelStepper::DRIVER, PIN_PUMP_STEP, PIN_PUMP_DIR);

static float mlPerRevolution = DEFAULT_PUMP_ML_PER_REV;
static float currentSpeedMlH = 0.0f;
static volatile bool running = false;
static int32_t totalSteps = 0;
static float totalVolumeMl = 0.0f;
static TaskHandle_t pumpTaskHandle = NULL;
static SemaphoreHandle_t pumpMutex = NULL;

static constexpr uint32_t kPumpCounterUpdateMs = 100;
static constexpr uint32_t kPumpCooperativeSliceUs = 10000;
static constexpr TickType_t kPumpIdleDelayTicks = pdMS_TO_TICKS(10);
static constexpr TickType_t kPumpYieldDelayTicks = pdMS_TO_TICKS(PUMP_TASK_DELAY_MS);

static void updateTotalsFromPosition(long currentPos) {
    totalSteps = currentPos;
    const float revolutions =
        static_cast<float>(totalSteps) / (PUMP_STEPS_PER_REV * PUMP_MICROSTEPS);
    totalVolumeMl = revolutions * mlPerRevolution;
}

static bool lockPump(TickType_t timeoutTicks = portMAX_DELAY) {
    return pumpMutex != NULL && xSemaphoreTake(pumpMutex, timeoutTicks) == pdTRUE;
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

static void pumpTask(void* pvParameters) {
    uint32_t lastCounterUpdate = 0;
    uint32_t lastCooperativeYieldUs = micros();

    while (1) {
        if (!running) {
            lastCooperativeYieldUs = micros();
            vTaskDelay(kPumpIdleDelayTicks);
            continue;
        }

        if (lockPump(portMAX_DELAY)) {
            stepper.runSpeed();

            const uint32_t nowMs = millis();
            if (nowMs - lastCounterUpdate >= kPumpCounterUpdateMs) {
                lastCounterUpdate = nowMs;
                const long currentPos = stepper.currentPosition();
                if (currentPos != totalSteps) {
                    updateTotalsFromPosition(currentPos);
                }
            }

            unlockPump();
        }

        const uint32_t nowUs = micros();
        if (nowUs - lastCooperativeYieldUs >= kPumpCooperativeSliceUs) {
            lastCooperativeYieldUs = nowUs;
            vTaskDelay(kPumpYieldDelayTicks);
        } else {
            taskYIELD();
        }
    }
}

namespace Pump {

void init() {
    LOG_I("Pump: Initializing...");

    pinMode(PIN_PUMP_EN, OUTPUT);
    digitalWrite(PIN_PUMP_EN, HIGH);

    stepper.setMaxSpeed(PUMP_MAX_SPEED);
    stepper.setAcceleration(PUMP_ACCELERATION);
    stepper.setCurrentPosition(0);

    totalSteps = 0;
    totalVolumeMl = 0.0f;
    currentSpeedMlH = 0.0f;
    running = false;

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
            2,
            &pumpTaskHandle,
            1);
    }

    LOG_I("Pump: Init complete (microsteps=%d, ml/rev=%.2f)",
          PUMP_MICROSTEPS, mlPerRevolution);
}

void start(float mlPerHour) {
    if (mlPerHour <= 0.0f) {
        stop();
        return;
    }

    float stepsPerSec = mlPerHourToStepsPerSec(mlPerHour);
    if (stepsPerSec > PUMP_MAX_SPEED) {
        stepsPerSec = PUMP_MAX_SPEED;
        mlPerHour = stepsPerSecToMlPerHour(stepsPerSec);
    }

    digitalWrite(PIN_PUMP_EN, LOW);

    if (lockPump(portMAX_DELAY)) {
        stepper.setSpeed(stepsPerSec);
        unlockPump();
    }

    currentSpeedMlH = mlPerHour;
    running = true;

    LOG_I("Pump: Started at %.1f ml/h (%.1f steps/s)", mlPerHour, stepsPerSec);
}

void stop() {
    running = false;
    currentSpeedMlH = 0.0f;

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

    float stepsPerSec = mlPerHourToStepsPerSec(mlPerHour);
    if (stepsPerSec > PUMP_MAX_SPEED) {
        stepsPerSec = PUMP_MAX_SPEED;
        mlPerHour = stepsPerSecToMlPerHour(stepsPerSec);
    }

    if (lockPump(portMAX_DELAY)) {
        stepper.setSpeed(stepsPerSec);
        unlockPump();
    }

    currentSpeedMlH = mlPerHour;
    LOG_D("Pump: Speed changed to %.1f ml/h", mlPerHour);
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

} // namespace Pump
