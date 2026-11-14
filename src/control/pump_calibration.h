#pragma once

#include "pump_calibration_logic.h"

namespace PumpCalibrationSession {

using Phase = PumpCalibrationLogic::Phase;
using Snapshot = PumpCalibrationLogic::State;

Snapshot getSnapshot();
bool beginPriming(float speedMlH);
bool finishPriming();
bool beginMeasurement(float speedMlH, bool allowWithoutPrime = false);
bool stopMeasurement();
bool isStopPending();
void update();
/**
 * Stops and captures an active measurement. Does not reconstruct a measurement
 * from the pump's cumulative step counter after the session was lost.
 */
bool ensureMeasurementCaptured();
bool enterMeasuredVolume(float volumeMl);
bool retry();
bool mayCommit();
float getCandidateMlPerRev();
bool outputActive();
bool hasTimedOut(uint32_t nowMs, uint32_t maxDurationMs);
void cancel();
void complete();

} // namespace PumpCalibrationSession
