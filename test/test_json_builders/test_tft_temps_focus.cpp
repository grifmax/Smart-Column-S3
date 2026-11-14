#include <unity.h>

#include <../../src/interface/tft_temps_focus_logic.h>

using TftTempsFocusLogic::Inputs;
using TftTempsFocusLogic::State;

void test_temps_focus_fires_on_process_start_only_once() {
  State state;
  Inputs in;
  TEST_ASSERT_FALSE(TftTempsFocusLogic::evaluate(in, state));

  in.processRunning = true;
  TEST_ASSERT_TRUE(TftTempsFocusLogic::evaluate(in, state));
  // Still running: the operator may close the page without it re-opening.
  TEST_ASSERT_FALSE(TftTempsFocusLogic::evaluate(in, state));
  TEST_ASSERT_FALSE(TftTempsFocusLogic::evaluate(in, state));

  in.processRunning = false;
  TEST_ASSERT_FALSE(TftTempsFocusLogic::evaluate(in, state));
  in.processRunning = true;
  TEST_ASSERT_TRUE(TftTempsFocusLogic::evaluate(in, state));
}

void test_temps_focus_fires_on_calibration_and_dosing_edges() {
  State state;
  Inputs in;
  in.calibrationActive = true;
  TEST_ASSERT_TRUE(TftTempsFocusLogic::evaluate(in, state));
  TEST_ASSERT_FALSE(TftTempsFocusLogic::evaluate(in, state));

  in.calibrationActive = false;
  TEST_ASSERT_FALSE(TftTempsFocusLogic::evaluate(in, state));
  in.dosingActive = true;
  TEST_ASSERT_TRUE(TftTempsFocusLogic::evaluate(in, state));
  TEST_ASSERT_FALSE(TftTempsFocusLogic::evaluate(in, state));
}

void test_temps_focus_ignores_steady_state_and_combinations() {
  State state;
  Inputs in;
  in.processRunning = true;
  in.calibrationActive = true;
  TEST_ASSERT_TRUE(TftTempsFocusLogic::evaluate(in, state));
  TEST_ASSERT_FALSE(TftTempsFocusLogic::evaluate(in, state));

  // Dosing starts while the process and calibration flags stay high: that is a
  // new event and must focus the page again.
  in.dosingActive = true;
  TEST_ASSERT_TRUE(TftTempsFocusLogic::evaluate(in, state));
  TEST_ASSERT_FALSE(TftTempsFocusLogic::evaluate(in, state));

  in.processRunning = false;
  in.calibrationActive = false;
  in.dosingActive = false;
  TEST_ASSERT_FALSE(TftTempsFocusLogic::evaluate(in, state));
}
