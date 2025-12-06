/**
 * Smart-Column S3 - Драйвер перистальтического насоса
 *
 * Шаговый двигатель NEMA17 + драйвер TMC2209
 * Библиотека AccelStepper для плавного управления
 */

#include "pump.h"
#include <AccelStepper.h>

// =============================================================================
// ГЛОБАЛЬНЫЕ ОБЪЕКТЫ
// =============================================================================

// Драйвер шагового двигателя (интерфейс STEP/DIR)
static AccelStepper stepper(AccelStepper::DRIVER, PIN_PUMP_STEP, PIN_PUMP_DIR);

// =============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// =============================================================================

static float mlPerRevolution = DEFAULT_PUMP_ML_PER_REV;
static float currentSpeedMlH = 0;
static bool running = false;
static uint32_t totalSteps = 0;
static float totalVolumeMl = 0;

// =============================================================================
// ВНУТРЕННИЕ ФУНКЦИИ
// =============================================================================

/**
 * Преобразование мл/час в шаги/сек
 */
static float mlPerHourToStepsPerSec(float mlPerHour) {
    // мл/час → оборотов/час
    float revPerHour = mlPerHour / mlPerRevolution;

    // оборотов/час → оборотов/сек
    float revPerSec = revPerHour / 3600.0f;

    // оборотов/сек → шагов/сек
    float stepsPerSec = revPerSec * PUMP_STEPS_PER_REV * PUMP_MICROSTEPS;

    return stepsPerSec;
}

/**
 * Преобразование шагов/сек в мл/час
 */
static float stepsPerSecToMlPerHour(float stepsPerSec) {
    // шагов/сек → оборотов/сек
    float revPerSec = stepsPerSec / (PUMP_STEPS_PER_REV * PUMP_MICROSTEPS);

    // оборотов/сек → оборотов/час
    float revPerHour = revPerSec * 3600.0f;

    // оборотов/час → мл/час
    float mlPerHour = revPerHour * mlPerRevolution;

    return mlPerHour;
}

// =============================================================================
// ПУБЛИЧНЫЙ ИНТЕРФЕЙС
// =============================================================================

namespace Pump {

void init() {
    LOG_I("Pump: Initializing...");

    // Пины управления
    pinMode(PIN_PUMP_EN, OUTPUT);
    digitalWrite(PIN_PUMP_EN, HIGH); // Отключено (TMC2209: EN активен LOW)

    // Настройка AccelStepper
    stepper.setMaxSpeed(PUMP_MAX_SPEED);
    stepper.setAcceleration(PUMP_ACCELERATION);
    stepper.setCurrentPosition(0);

    totalSteps = 0;
    totalVolumeMl = 0;
    running = false;

    LOG_I("Pump: Init complete (microsteps=%d, ml/rev=%.2f)",
          PUMP_MICROSTEPS, mlPerRevolution);
}

void start(float mlPerHour) {
    if (mlPerHour <= 0) {
        stop();
        return;
    }

    // Включить драйвер
    digitalWrite(PIN_PUMP_EN, LOW);

    // Установить скорость
    float stepsPerSec = mlPerHourToStepsPerSec(mlPerHour);

    // Ограничить максимум
    if (stepsPerSec > PUMP_MAX_SPEED) {
        stepsPerSec = PUMP_MAX_SPEED;
        mlPerHour = stepsPerSecToMlPerHour(stepsPerSec);
    }

    stepper.setSpeed(stepsPerSec);
    currentSpeedMlH = mlPerHour;
    running = true;

    LOG_I("Pump: Started at %.1f ml/h (%.1f steps/s)",
          mlPerHour, stepsPerSec);
}

void stop() {
    stepper.setSpeed(0);
    stepper.stop();
    digitalWrite(PIN_PUMP_EN, HIGH); // Отключить драйвер
    running = false;
    currentSpeedMlH = 0;

    LOG_I("Pump: Stopped");
}

void setSpeed(float mlPerHour) {
    if (!running) {
        start(mlPerHour);
        return;
    }

    if (mlPerHour <= 0) {
        stop();
        return;
    }

    float stepsPerSec = mlPerHourToStepsPerSec(mlPerHour);

    if (stepsPerSec > PUMP_MAX_SPEED) {
        stepsPerSec = PUMP_MAX_SPEED;
        mlPerHour = stepsPerSecToMlPerHour(stepsPerSec);
    }

    stepper.setSpeed(stepsPerSec);
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

void resetVolume() {
    totalSteps = 0;
    totalVolumeMl = 0;
    stepper.setCurrentPosition(0);
    LOG_I("Pump: Volume reset");
}

void setCalibration(float mlPerRev) {
    if (mlPerRev > 0 && mlPerRev < 10.0f) {
        mlPerRevolution = mlPerRev;
        LOG_I("Pump: Calibration set to %.3f ml/rev", mlPerRev);

        // Пересчитать скорость если работает
        if (running) {
            setSpeed(currentSpeedMlH);
        }
    } else {
        LOG_E("Pump: Invalid calibration value %.3f", mlPerRev);
    }
}

void update() {
    if (!running) return;

    // Выполнить шаг (константная скорость)
    stepper.runSpeed();

    // Обновить счётчики
    long currentPos = stepper.currentPosition();
    if (currentPos != totalSteps) {
        totalSteps = currentPos;

        // Пересчитать объём
        float revolutions = (float)totalSteps / (PUMP_STEPS_PER_REV * PUMP_MICROSTEPS);
        totalVolumeMl = revolutions * mlPerRevolution;
    }
}

} // namespace Pump
