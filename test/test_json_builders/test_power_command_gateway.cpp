#include <unity.h>

#include <../../src/control/power_command_gateway.h>
#include <../../src/control/autoclave_hardware_profile_logic.h>
#include <../../src/control/autoclave_session_logic.h>
#include <../../src/control/autoclave_safety_logic.h>
#include <../../src/control/fermentation_hardware_profile_logic.h>
#include <../../src/control/dosing_session_logic.h>
#include <../../src/control/pump_calibration_logic.h>
#include <../../src/control/sensor_calibration_logic.h>
#include <../../src/control/service_test_session_logic.h>
#include <../../src/control/service_test_audit_logic.h>
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

void test_sensor_calibration_keeps_ds18_mapping_unique() {
  using namespace SensorCalibrationLogic;

  uint8_t assigned[3][8] = {};
  const uint8_t first[8] = {0x28, 1, 2, 3, 4, 5, 6, 7};
  const uint8_t second[8] = {0x28, 7, 6, 5, 4, 3, 2, 1};
  const uint8_t empty[8] = {};

  TEST_ASSERT_TRUE(assignAddress(assigned, 0, first));
  TEST_ASSERT_TRUE(assignAddress(assigned, 1, second));
  TEST_ASSERT_TRUE(assignAddress(assigned, 2, first));
  TEST_ASSERT_TRUE(isZeroAddress(assigned[0]));
  TEST_ASSERT_TRUE(addressesEqual(assigned[1], second));
  TEST_ASSERT_TRUE(addressesEqual(assigned[2], first));
  TEST_ASSERT_TRUE(clearAddress(assigned, 2));
  TEST_ASSERT_TRUE(isZeroAddress(assigned[2]));
  TEST_ASSERT_FALSE(assignAddress(assigned, 1, empty));
}

void test_service_test_audit_uses_stable_lifecycle_tokens() {
  using namespace ServiceTestAuditLogic;

  TEST_ASSERT_EQUAL_STRING("requested", eventToken(Event::REQUESTED));
  TEST_ASSERT_EQUAL_STRING("changed", eventToken(Event::CHANGED));
  TEST_ASSERT_EQUAL_STRING("faulted", eventToken(Event::FAULTED));
  TEST_ASSERT_TRUE(isTerminal(Event::RESULT));
  TEST_ASSERT_TRUE(isTerminal(Event::RESTORED));
  TEST_ASSERT_FALSE(isTerminal(Event::PAUSED));
}

void test_dosing_session_requires_prime_then_tracks_only_target_volume() {
  using namespace DosingSessionLogic;

  State dosing;
  TEST_ASSERT_FALSE(configure(dosing, Route::NONE, 120.0f, 300.0f));
  TEST_ASSERT_TRUE(configure(dosing, Route::HEADS, 120.0f, 300.0f));
  TEST_ASSERT_FALSE(beginDispensing(dosing, 0.5f, 10.0f, 1));
  TEST_ASSERT_TRUE(beginPriming(dosing, 0.5f, 10));
  TEST_ASSERT_TRUE(isOutputActive(dosing));
  TEST_ASSERT_TRUE(finishPriming(dosing));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::READY),
                          static_cast<uint8_t>(dosing.phase));
  TEST_ASSERT_TRUE(beginDispensing(dosing, 0.5f, 42.0f, 20));
  updateActualVolume(dosing, 119.0f);
  TEST_ASSERT_FALSE(targetReached(dosing));
  updateActualVolume(dosing, 120.0f);
  TEST_ASSERT_TRUE(targetReached(dosing));
  TEST_ASSERT_TRUE(finish(dosing, FinishReason::TARGET_REACHED, 40));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::COMPLETED),
                          static_cast<uint8_t>(dosing.phase));
  TEST_ASSERT_EQUAL_STRING("target_reached",
                           finishReasonToken(dosing.finishReason));

  TEST_ASSERT_TRUE(configure(dosing, Route::TAILS, 30.0f, 200.0f));
  TEST_ASSERT_TRUE(beginPriming(dosing, 0.5f, 50));
  TEST_ASSERT_TRUE(finish(dosing, FinishReason::SAFETY, 55));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::FAULTED),
                          static_cast<uint8_t>(dosing.phase));
}

void test_autoclave_profile_requires_explicit_hardware_and_safety_approval() {
  using namespace AutoclaveHardwareProfileLogic;

  Profile profile;
  TEST_ASSERT_EQUAL_STRING("not_approved", validationToken(validate(profile)));

  profile.approved = true;
  profile.steamEnabled = true;
  profile.heaterAssigned = true;
  profile.cubeTemperatureAssigned = true;
  profile.pressureSensorAssigned = true;
  profile.heatPowerLimitW = 2000.0f;
  profile.pressureMaxMmHg = 30.0f;
  profile.pressureRiseRateMaxMmHgMin = 5.0f;
  TEST_ASSERT_EQUAL_STRING("mechanical_safety_unreviewed",
                           validationToken(validate(profile)));
  profile.mechanicalSafetyReviewed = true;
  TEST_ASSERT_TRUE(mayOfferSteam(profile));
  TEST_ASSERT_FALSE(mayOfferWater(profile));

  profile.waterEnabled = true;
  TEST_ASSERT_EQUAL_STRING("water_circulation_unassigned",
                           validationToken(validate(profile)));
  profile.circulationPumpAssigned = true;
  TEST_ASSERT_EQUAL_STRING("water_cooling_unassigned",
                           validationToken(validate(profile)));
  profile.coolingAssigned = true;
  TEST_ASSERT_TRUE(mayOfferWater(profile));
}

void test_autoclave_sterilization_timer_only_counts_in_temperature_band() {
  using namespace AutoclaveSessionLogic;

  Config config;
  config.sterilizationTempC = 121.0f;
  config.temperatureBandC = 1.0f;
  config.sterilizationDurationMs = 1000;
  config.coolingStopTempC = 40.0f;
  State session;
  TEST_ASSERT_TRUE(start(session, 10));
  TEST_ASSERT_FALSE(confirmPrecheck(session, false, 20));
  TEST_ASSERT_TRUE(confirmPrecheck(session, true, 20));
  TEST_ASSERT_TRUE(update(session, config, true, 120.0f, 30));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::STERILIZING),
                          static_cast<uint8_t>(session.phase));
  TEST_ASSERT_FALSE(update(session, config, true, 120.0f, 130));
  TEST_ASSERT_FALSE(update(session, config, true, 118.0f, 1130));
  TEST_ASSERT_EQUAL_UINT32(0, session.sterilizationAccumulatedMs);
  TEST_ASSERT_FALSE(update(session, config, true, 121.0f, 1230));
  TEST_ASSERT_FALSE(update(session, config, true, 121.0f, 1730));
  TEST_ASSERT_EQUAL_UINT32(500, session.sterilizationAccumulatedMs);
  TEST_ASSERT_TRUE(update(session, config, true, 121.0f, 2230));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::COOLING),
                          static_cast<uint8_t>(session.phase));
  TEST_ASSERT_TRUE(update(session, config, true, 40.0f, 3000));
  TEST_ASSERT_TRUE(finish(session, 3010));
  TEST_ASSERT_TRUE(isTerminal(session.phase));
}

void test_autoclave_safety_faults_on_required_sensor_pressure_rate_and_timeout() {
  using namespace AutoclaveSafetyLogic;

  Config config;
  config.pressureMaxMmHg = 50.0f;
  config.pressureRiseRateMaxMmHgMin = 10.0f;
  config.heatingTimeoutMs = 1000;
  State safety;
  const auto phase = AutoclaveSessionLogic::Phase::HEATING;
  TEST_ASSERT_EQUAL_STRING("temperature_sensor",
                           faultToken(evaluate(safety, config, phase, 0,
                                               false, true, 0.0f, 10)));
  TEST_ASSERT_EQUAL_STRING("pressure_sensor",
                           faultToken(evaluate(safety, config, phase, 0,
                                               true, false, 0.0f, 20)));
  TEST_ASSERT_EQUAL_STRING("none", faultToken(evaluate(
                                       safety, config, phase, 0, true, true,
                                       10.0f, 100)));
  TEST_ASSERT_EQUAL_STRING("pressure_rise_rate", faultToken(evaluate(
                                                   safety, config, phase, 0,
                                                   true, true, 20.0f, 10100)));

  State timeoutSafety;
  TEST_ASSERT_EQUAL_STRING("heating_timeout", faultToken(evaluate(
                                               timeoutSafety, config, phase,
                                               0, true, true, 10.0f, 1000)));
  State highPressureSafety;
  TEST_ASSERT_EQUAL_STRING("pressure_limit", faultToken(evaluate(
                                               highPressureSafety, config, phase,
                                               0, true, true, 51.0f, 10)));
}

void test_fermentation_profile_requires_two_unique_complete_zones() {
  using namespace FermentationHardwareProfileLogic;

  Profile profile;
  profile.zoneCount = 1;
  profile.zones[0] = {0, 10, 11, kUnassigned};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Validation::OK),
                          static_cast<uint8_t>(validate(profile)));
  TEST_ASSERT_FALSE(isSecondZoneAvailable(profile));

  profile.zoneCount = 2;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(Validation::ZONE2_TEMPERATURE_UNASSIGNED),
      static_cast<uint8_t>(validate(profile)));

  profile.zones[1] = {0, 12, 13, kUnassigned};
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(Validation::TEMPERATURE_SENSOR_REUSED),
      static_cast<uint8_t>(validate(profile)));

  profile.zones[1] = {1, 10, 13, kUnassigned};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Validation::OUTPUT_ID_REUSED),
                          static_cast<uint8_t>(validate(profile)));

  profile.zones[1] = {1, 12, 13, 14};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Validation::OK),
                          static_cast<uint8_t>(validate(profile)));
  TEST_ASSERT_TRUE(isSecondZoneAvailable(profile));
}
