#include <ArduinoJson.h>
#include <unity.h>

#include <string>
#include <../../src/control/fraction_program_logic.h>
#include <../../src/control/rect_takeoff_logic.h>

#include "../support/json_assertions.h"

void setUp(void) {}

void tearDown(void) {}

void test_json_harness_parses_and_asserts_contract_fields() {
  JsonDocument doc;
  doc["success"] = true;
  doc["message"] = "ok";
  doc["code"] = 200;

  std::string json;
  serializeJson(doc, json);

  const JsonDocument parsed = parseJsonOrFail(json.c_str());
  assertJsonBoolEquals(parsed.as<JsonVariantConst>(), "success", true);
  assertJsonStringEquals(parsed.as<JsonVariantConst>(), "message", "ok");
  assertJsonIntEquals(parsed.as<JsonVariantConst>(), "code", 200);
}

void test_fraction_program_end_reason_returns_none_without_conditions();
void test_fraction_program_end_reason_detects_each_automatic_condition();
void test_fraction_program_end_reason_uses_documented_or_priority();
void test_fraction_program_manual_advance_has_priority();
void test_fraction_program_route_gate_requires_ready_route_and_settle_delay();

void test_fraction_program_pause_shifts_step_timer();

void test_fraction_program_route_timeout_requires_missing_route();
void test_fraction_program_collection_gate_covers_route_level_emergency_and_resume();

void test_fraction_program_json_contract_round_trip();
void test_autonomous_pause_uses_full_reflux();
void test_autonomous_pause_does_not_integrate_volume();
void test_pump_backend_gate_handles_rate_and_periodic_pause();
void test_valve_backend_gate_requires_route_and_safety_ready();

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_json_harness_parses_and_asserts_contract_fields);
  RUN_TEST(test_fraction_program_end_reason_returns_none_without_conditions);
  RUN_TEST(test_fraction_program_end_reason_detects_each_automatic_condition);
  RUN_TEST(test_fraction_program_end_reason_uses_documented_or_priority);
  RUN_TEST(test_fraction_program_manual_advance_has_priority);
  RUN_TEST(test_fraction_program_route_gate_requires_ready_route_and_settle_delay);
  RUN_TEST(test_fraction_program_pause_shifts_step_timer);
  RUN_TEST(test_fraction_program_route_timeout_requires_missing_route);
  RUN_TEST(test_fraction_program_collection_gate_covers_route_level_emergency_and_resume);
  RUN_TEST(test_fraction_program_json_contract_round_trip);
  RUN_TEST(test_autonomous_pause_uses_full_reflux);
  RUN_TEST(test_autonomous_pause_does_not_integrate_volume);
  RUN_TEST(test_pump_backend_gate_handles_rate_and_periodic_pause);
  RUN_TEST(test_valve_backend_gate_requires_route_and_safety_ready);
  return UNITY_END();
}
#include <../../src/control/fraction_program_logic.h>

void test_fraction_program_end_reason_returns_none_without_conditions() {
  const auto reason = FractionProgramLogic::selectEndReason(false, false, false,
                                                            false, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(FractionProgramLogic::EndReason::None),
                          static_cast<uint8_t>(reason));
  TEST_ASSERT_FALSE(FractionProgramLogic::shouldFinish(false, false, false,
                                                        false, false));
}

void test_fraction_program_end_reason_detects_each_automatic_condition() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FractionProgramLogic::EndReason::Volume),
      static_cast<uint8_t>(FractionProgramLogic::selectEndReason(false, true, false,
                                                                  false, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FractionProgramLogic::EndReason::Time),
      static_cast<uint8_t>(FractionProgramLogic::selectEndReason(false, false, true,
                                                                  false, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FractionProgramLogic::EndReason::Temperature),
      static_cast<uint8_t>(FractionProgramLogic::selectEndReason(false, false, false,
                                                                  true, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FractionProgramLogic::EndReason::Level),
      static_cast<uint8_t>(FractionProgramLogic::selectEndReason(false, false, false,
                                                                  false, true)));
}

void test_fraction_program_end_reason_uses_documented_or_priority() {
  const auto reason = FractionProgramLogic::selectEndReason(false, true, true,
                                                            true, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(FractionProgramLogic::EndReason::Volume),
                          static_cast<uint8_t>(reason));
  TEST_ASSERT_TRUE(FractionProgramLogic::shouldFinish(false, false, true,
                                                       false, true));
}

void test_fraction_program_manual_advance_has_priority() {
  const auto reason = FractionProgramLogic::selectEndReason(true, true, true,
                                                            true, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(FractionProgramLogic::EndReason::Manual),
                          static_cast<uint8_t>(reason));
}
void test_fraction_program_route_gate_requires_ready_route_and_settle_delay() {
  TEST_ASSERT_FALSE(FractionProgramLogic::isRouteSettled(false, 5000, 1500));
  TEST_ASSERT_FALSE(FractionProgramLogic::isRouteSettled(true, 1499, 1500));
  TEST_ASSERT_TRUE(FractionProgramLogic::isRouteSettled(true, 1500, 1500));
  TEST_ASSERT_TRUE(FractionProgramLogic::isRouteSettled(true, 2500, 1500));
}
void test_fraction_program_pause_shifts_step_timer() {
  TEST_ASSERT_EQUAL_UINT32(
      12500, FractionProgramLogic::advanceTimestampAfterPause(10000, 2500));
}
void test_fraction_program_route_timeout_requires_missing_route() {
  TEST_ASSERT_FALSE(FractionProgramLogic::hasRouteTimedOut(false, 29999));
  TEST_ASSERT_TRUE(FractionProgramLogic::hasRouteTimedOut(false, 30000));
  TEST_ASSERT_TRUE(FractionProgramLogic::hasRouteTimedOut(false, 40000));
  TEST_ASSERT_FALSE(FractionProgramLogic::hasRouteTimedOut(true, 40000));
}
void test_fraction_program_collection_gate_covers_route_level_emergency_and_resume() {
  // Missing/failing route: never reopen the pump after the timeout path.
  TEST_ASSERT_FALSE(FractionProgramLogic::mayStartCollection(
      true, false, false, 30000, 1500));
  // Level pause and emergency stop both use the same paused/safety gates.
  TEST_ASSERT_FALSE(FractionProgramLogic::mayStartCollection(
      true, true, true, 5000, 1500));
  TEST_ASSERT_FALSE(FractionProgramLogic::mayStartCollection(
      false, false, true, 5000, 1500));
  // Resume is safe only after the route remains ready for the settle delay.
  TEST_ASSERT_FALSE(FractionProgramLogic::mayStartCollection(
      true, false, true, 1499, 1500));
  TEST_ASSERT_TRUE(FractionProgramLogic::mayStartCollection(
      true, false, true, 1500, 1500));
}
void test_fraction_program_json_contract_round_trip() {
  JsonDocument source;
  JsonObject program = source["fractionProgram"].to<JsonObject>();
  program["schemaVersion"] = 1;
  program["enabled"] = true;
  program["heatingTemperatureSensorIndex"] = 0;
  program["heatingTargetTemperatureC"] = 78.0f;
  JsonObject step = program["steps"].add<JsonObject>();
  step["name"] = "Heads";
  step["routeIndex"] = 0;
  step["pumpRateMlH"] = 350.0f;
  step["heaterPowerW"] = 1800.0f;
  step["requireOperatorConfirmation"] = true;
  step["confirmationPrompt"] = "Install heads container";
  step["endConditions"] = 7;
  step["endVolumeMl"] = 250.0f;
  step["endDurationSec"] = 900;
  step["temperatureSensorIndex"] = 2;
  step["endTemperatureC"] = 81.5f;
  step["allowManualAdvance"] = true;

  std::string json;
  serializeJson(source, json);
  const JsonDocument parsed = parseJsonOrFail(json.c_str());
  const JsonObjectConst parsedProgram = parsed["fractionProgram"];
  TEST_ASSERT_TRUE(parsedProgram["enabled"].as<bool>());
  TEST_ASSERT_EQUAL_UINT8(1, parsedProgram["schemaVersion"].as<uint8_t>());
  TEST_ASSERT_EQUAL_UINT8(1, parsedProgram["steps"].size());

  const JsonObjectConst parsedStep = parsedProgram["steps"][0];
  TEST_ASSERT_EQUAL_STRING("Heads", parsedStep["name"].as<const char *>());
  TEST_ASSERT_EQUAL_UINT8(0, parsedStep["routeIndex"].as<uint8_t>());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 350.0f, parsedStep["pumpRateMlH"].as<float>());
  TEST_ASSERT_TRUE(parsedStep["requireOperatorConfirmation"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("Install heads container",
                           parsedStep["confirmationPrompt"].as<const char *>());
  TEST_ASSERT_EQUAL_UINT8(7, parsedStep["endConditions"].as<uint8_t>());
  TEST_ASSERT_EQUAL_UINT32(900, parsedStep["endDurationSec"].as<uint32_t>());
  TEST_ASSERT_TRUE(parsedStep["allowManualAdvance"].as<bool>());
}

void test_autonomous_pause_uses_full_reflux() {
  TEST_ASSERT_FALSE(
      RectTakeoffLogic::shouldUseFullReflux(true, true, true));
  TEST_ASSERT_TRUE(
      RectTakeoffLogic::shouldUseFullReflux(true, true, false));
  TEST_ASSERT_TRUE(
      RectTakeoffLogic::shouldUseFullReflux(false, false, false));
}

void test_autonomous_pause_does_not_integrate_volume() {
  TEST_ASSERT_TRUE(RectTakeoffLogic::shouldIntegrateVolume(true, 600.0f, 1000));
  TEST_ASSERT_FALSE(RectTakeoffLogic::shouldIntegrateVolume(false, 600.0f, 1000));
  TEST_ASSERT_FALSE(RectTakeoffLogic::shouldIntegrateVolume(true, 0.0f, 1000));
  TEST_ASSERT_FALSE(RectTakeoffLogic::shouldIntegrateVolume(true, 600.0f, 0));
}

void test_pump_backend_gate_handles_rate_and_periodic_pause() {
  TEST_ASSERT_TRUE(RectTakeoffLogic::shouldRunBackend(true, true, 600.0f, false, false));
  TEST_ASSERT_FALSE(RectTakeoffLogic::shouldRunBackend(true, true, 600.0f, true, false));
  TEST_ASSERT_TRUE(RectTakeoffLogic::shouldRunBackend(true, true, 600.0f, true, true));
  TEST_ASSERT_FALSE(RectTakeoffLogic::shouldRunBackend(true, true, 0.0f, false, true));
}

void test_valve_backend_gate_requires_route_and_safety_ready() {
  TEST_ASSERT_FALSE(RectTakeoffLogic::shouldRunBackend(true, false, 600.0f, false, true));
  TEST_ASSERT_FALSE(RectTakeoffLogic::shouldRunBackend(false, true, 600.0f, false, true));
  TEST_ASSERT_TRUE(RectTakeoffLogic::shouldRunBackend(true, true, 600.0f, false, true));
  TEST_ASSERT_FALSE(RectTakeoffLogic::shouldRunBackend(true, false, 600.0f, true, true));
}
