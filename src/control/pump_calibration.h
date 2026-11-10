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
/**
 * Гарантирует, что замер зафиксирован: глушит насос и переводит сессию в
 * READY_TO_REVIEW. Если сессия потерялась (фаза IDLE/READY_TO_MEASURE),
 * восстанавливает её по накопленным шагам, чтобы объём можно было применить.
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
