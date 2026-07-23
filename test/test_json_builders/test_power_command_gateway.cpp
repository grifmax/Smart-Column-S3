#include <unity.h>

#include <../../src/control/power_command_gateway.h>
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
