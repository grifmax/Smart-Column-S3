#pragma once

// When should the local TFT jump to the "all temperatures" page?
//
// The operator starts a process, a pump calibration or a dosing pour from the
// web UI and walks over to the column; the panel must already show the
// temperatures instead of whatever menu was open. Only the moment the activity
// starts is interesting (rising edge) - closing the page by hand while the
// process still runs must not re-open it on the next frame.
//
// Header-only and Arduino-free so the edge logic is covered by host tests.

namespace TftTempsFocusLogic {

struct Inputs {
  bool processRunning = false;
  bool calibrationActive = false;
  bool dosingActive = false;
};

struct State {
  bool prevProcess = false;
  bool prevCalibration = false;
  bool prevDosing = false;
};

inline bool evaluate(const Inputs &in, State &state) {
  const bool started = (in.processRunning && !state.prevProcess) ||
                       (in.calibrationActive && !state.prevCalibration) ||
                       (in.dosingActive && !state.prevDosing);
  state.prevProcess = in.processRunning;
  state.prevCalibration = in.calibrationActive;
  state.prevDosing = in.dosingActive;
  return started;
}

} // namespace TftTempsFocusLogic
