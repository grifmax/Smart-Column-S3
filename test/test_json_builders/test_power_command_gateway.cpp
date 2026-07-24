#include <unity.h>

#include <../../src/control/power_command_gateway.h>
#include <../../src/control/pump_calibration_logic.h>
#include <../../src/control/service_test_session_logic.h>
#include <../../src/interface/tft_service_test_logic.h>

void test_power_command_captures_owner_reason_timestamp_and_safe_stop() {
  using namespace PowerCommandGateway;

  Command command = makeCommand(Owner::MANUAL, Target::PUMP,
                                Reason::TFT_MANUAL_OUTPUT, 123456U, 850.0f);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Owner::MANUAL),
                          static_cast<uint8_t>(command.owner));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Target::PUMP),
                          static_cast<uint8_t>(command.target));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Reason::TFT_MANUAL_OUTPUT),
                          static_cast<uint8_t>(command.reason));
  TEST_ASSERT_EQUAL_UINT32(123456U, command.timestampMs);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 850.0f, command.requestedValue);
  TEST_ASSERT_TRUE(command.safeStopRequired);
  TEST_ASSERT_FALSE(command.safeStopApplied);

  markSafeStopApplied(command);
  TEST_ASSERT_TRUE(command.safeStopApplied);
}

void test_tft_service_test_hides_unavailable_hardware() {
  using namespace TftServiceTestLogic;

  Availability board;
  board.heater = true;
  board.pump = true;
  board.waterValve = true;
  board.headsValve = true;
  board.sensors = true;

  TEST_ASSERT_TRUE(isAvailable(Device::Heater, board));
  TEST_ASSERT_TRUE(isAvailable(Device::Pump, board));
  TEST_ASSERT_TRUE(isAvailable(Device::WaterValve, board));
  TEST_ASSERT_TRUE(isAvailable(Device::Sensors, board));
  TEST_ASSERT_FALSE(isAvailable(Device::Stirrer, board));
  TEST_ASSERT_FALSE(isAvailable(Device::BodyValve, board));
  TEST_ASSERT_FALSE(isAvailable(Device::TailsValve, board));
  TEST_ASSERT_FALSE(isAvailable(Device::CoolingPwm, board));
  TEST_ASSERT_FALSE(isAvailable(Device::Fractionator, board));
}

void test_service_test_session_requires_safe_pause_and_explicit_restore() {
  using namespace ServiceTestSessionLogic;

  State session;
  TEST_ASSERT_FALSE(request(session, 1, false, false, Target::PUMP));
  TEST_ASSERT_TRUE(request(session, 1, true, false, Target::PUMP));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::REQUESTED),
                          static_cast<uint8_t>(session.phase));
  TEST_ASSERT_TRUE(blocksProcessResume(session));
  TEST_ASSERT_FALSE(startTest(session, Target::PUMP));
  TEST_ASSERT_TRUE(markSafePaused(session));
  TEST_ASSERT_TRUE(mayUsePausedProcess(session, 1, true));
  TEST_ASSERT_TRUE(startTest(session, Target::PUMP));
  TEST_ASSERT_FALSE(startTest(session, Target::HEATER));
  TEST_ASSERT_TRUE(beginStopping(session));
  TEST_ASSERT_TRUE(markRestoreReady(session));
  TEST_ASSERT_TRUE(mayResumeProcess(session));
  close(session);
  TEST_ASSERT_FALSE(blocksProcessResume(session));

  TEST_ASSERT_TRUE(request(session, 2, true, true, Target::WATER_VALVE));
  TEST_ASSERT_TRUE(markSafePaused(session));
  TEST_ASSERT_TRUE(beginStopping(session));
  TEST_ASSERT_TRUE(markRestoreReady(session));
  TEST_ASSERT_TRUE(mayCloseWithoutResume(session));
  TEST_ASSERT_FALSE(mayResumeProcess(session));
  close(session);

  TEST_ASSERT_TRUE(request(session, 2, true, false, Target::HEATER));
  TEST_ASSERT_TRUE(fault(session));
  TEST_ASSERT_TRUE(blocksProcessResume(session));
  TEST_ASSERT_FALSE(mayResumeProcess(session));
}

void test_pump_calibration_requires_prime_measure_review_and_commit() {
  using namespace PumpCalibrationLogic;

  State calibration;
  TEST_ASSERT_FALSE(beginMeasurement(calibration, 600.0f, 10, false));
  TEST_ASSERT_TRUE(beginPriming(calibration, 600.0f, 10));
  TEST_ASSERT_TRUE(outputActive(calibration));
  TEST_ASSERT_TRUE(finishPriming(calibration));
  TEST_ASSERT_TRUE(beginMeasurement(calibration, 600.0f, 20));
  TEST_ASSERT_TRUE(stopMeasurement(calibration, 6400, 3020));
  TEST_ASSERT_FALSE(enterMeasuredVolume(calibration, 0.0f, 3200));
  TEST_ASSERT_TRUE(enterMeasuredVolume(calibration, 2.5f, 3200));
  TEST_ASSERT_TRUE(mayCommit(calibration));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.25f,
                           calibration.candidateMlPerRev);
  TEST_ASSERT_TRUE(retry(calibration));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::READY_TO_MEASURE),
                          static_cast<uint8_t>(calibration.phase));
}
