#include <unity.h>

#include <../../src/interface/tft_setpoint_guard_logic.h>

using TftSetpointGuard::OutputActivity;
using TftSetpointGuard::SetpointClass;

void test_setpoint_guard_blocks_actuator_for_every_live_output() {
  OutputActivity a;
  TEST_ASSERT_FALSE(TftSetpointGuard::isLocked(SetpointClass::Actuator, a));

  a.serviceTest = true;
  TEST_ASSERT_TRUE(TftSetpointGuard::isLocked(SetpointClass::Actuator, a));
  a = OutputActivity();
  a.heater = true;
  TEST_ASSERT_TRUE(TftSetpointGuard::isLocked(SetpointClass::Actuator, a));
  a = OutputActivity();
  a.pump = true;
  TEST_ASSERT_TRUE(TftSetpointGuard::isLocked(SetpointClass::Actuator, a));
  a = OutputActivity();
  a.stirrer = true;
  TEST_ASSERT_TRUE(TftSetpointGuard::isLocked(SetpointClass::Actuator, a));
  a = OutputActivity();
  a.valve = true;
  TEST_ASSERT_TRUE(TftSetpointGuard::isLocked(SetpointClass::Actuator, a));
  a = OutputActivity();
  a.dosing = true;
  TEST_ASSERT_TRUE(TftSetpointGuard::isLocked(SetpointClass::Actuator, a));
  a = OutputActivity();
  a.calibration = true;
  TEST_ASSERT_TRUE(TftSetpointGuard::isLocked(SetpointClass::Actuator, a));
  a = OutputActivity();
  a.processRunning = true;
  TEST_ASSERT_TRUE(TftSetpointGuard::isLocked(SetpointClass::Actuator, a));
}

void test_setpoint_guard_keeps_runtime_and_constant_fields_editable() {
  OutputActivity running;
  running.heater = true;
  running.processRunning = true;

  TEST_ASSERT_FALSE(
      TftSetpointGuard::isLocked(SetpointClass::RuntimeAllowed, running));
  TEST_ASSERT_FALSE(
      TftSetpointGuard::isLocked(SetpointClass::ProcessConstant, running));
  TEST_ASSERT_FALSE(TftSetpointGuard::isLocked(SetpointClass::None, running));
}

void test_setpoint_guard_reports_any_live_output_once() {
  OutputActivity a;
  TEST_ASSERT_FALSE(TftSetpointGuard::anyLiveOutput(a));
  a.calibration = true;
  TEST_ASSERT_TRUE(TftSetpointGuard::anyLiveOutput(a));
}
