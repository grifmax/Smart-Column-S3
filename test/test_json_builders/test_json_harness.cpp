#include <ArduinoJson.h>
#include <unity.h>

#include <string>
#include <../../src/control/fraction_program_logic.h>
#include <../../src/control/equipment_testing_logic.h>
#include <../../src/control/rect_takeoff_logic.h>
#include <../../src/interface/tft_hmi_policy_logic.h>
#include <../../src/interface/tft_operator_overlay_logic.h>
#include <../../src/interface/tft_service_test_logic.h>
#include <../../src/control/service_test_session_logic.h>
#include <../../src/control/service_test_audit_logic.h>
#include <../../src/control/dosing_session_logic.h>
#include <../../src/control/autoclave_hardware_profile_logic.h>
#include <../../src/control/autoclave_session_logic.h>
#include <../../src/control/autoclave_safety_logic.h>
#include <../../src/control/fermentation_hardware_profile_logic.h>
#include <../../src/control/pump_calibration_logic.h>
#include <../../src/control/sensor_calibration_logic.h>

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
void test_safety_proof_guard_blocks_unsafe_states();
void test_safety_proof_guard_allows_only_idle_safe_hardware();
void test_editable_field_descriptor_exposes_canonical_tft_metadata();
void test_editable_field_normalization_clamps_and_snaps_to_step();
void test_power_command_captures_owner_reason_timestamp_and_safe_stop();
void test_operator_overlay_claims_an_action_only_once_per_open();
void test_tft_access_policy_respects_runtime_mode();
void test_tft_draft_save_discard_and_navigation_policy();
void test_tft_start_route_requires_idle_saved_draft_and_ready_preflight();
void test_tft_active_control_actions_require_running_mode();
void test_tft_field_visibility_is_explicit();
void test_tft_service_session_closes_after_emergency();
void test_tft_post_alarm_action_never_resumes_process();
void test_tft_service_test_hides_unavailable_hardware();
void test_service_test_session_requires_safe_pause_and_explicit_restore();
void test_pump_calibration_requires_prime_measure_review_and_commit();
void test_sensor_calibration_keeps_ds18_mapping_unique();
void test_service_test_audit_uses_stable_lifecycle_tokens();
void test_dosing_session_requires_prime_then_tracks_only_target_volume();
void test_autoclave_profile_requires_explicit_hardware_and_safety_approval();
void test_autoclave_sterilization_timer_only_counts_in_temperature_band();
void test_autoclave_safety_faults_on_required_sensor_pressure_rate_and_timeout();
void test_fermentation_profile_requires_two_unique_complete_zones();

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
  RUN_TEST(test_safety_proof_guard_blocks_unsafe_states);
  RUN_TEST(test_safety_proof_guard_allows_only_idle_safe_hardware);
  RUN_TEST(test_editable_field_descriptor_exposes_canonical_tft_metadata);
  RUN_TEST(test_editable_field_normalization_clamps_and_snaps_to_step);
  RUN_TEST(test_power_command_captures_owner_reason_timestamp_and_safe_stop);
  RUN_TEST(test_operator_overlay_claims_an_action_only_once_per_open);
  RUN_TEST(test_tft_access_policy_respects_runtime_mode);
  RUN_TEST(test_tft_draft_save_discard_and_navigation_policy);
  RUN_TEST(test_tft_start_route_requires_idle_saved_draft_and_ready_preflight);
  RUN_TEST(test_tft_active_control_actions_require_running_mode);
  RUN_TEST(test_tft_field_visibility_is_explicit);
  RUN_TEST(test_tft_service_session_closes_after_emergency);
  RUN_TEST(test_tft_post_alarm_action_never_resumes_process);
  RUN_TEST(test_tft_service_test_hides_unavailable_hardware);
  RUN_TEST(test_service_test_session_requires_safe_pause_and_explicit_restore);
  RUN_TEST(test_pump_calibration_requires_prime_measure_review_and_commit);
  RUN_TEST(test_sensor_calibration_keeps_ds18_mapping_unique);
  RUN_TEST(test_service_test_audit_uses_stable_lifecycle_tokens);
  RUN_TEST(test_dosing_session_requires_prime_then_tracks_only_target_volume);
  RUN_TEST(test_autoclave_profile_requires_explicit_hardware_and_safety_approval);
  RUN_TEST(test_autoclave_sterilization_timer_only_counts_in_temperature_band);
  RUN_TEST(test_autoclave_safety_faults_on_required_sensor_pressure_rate_and_timeout);
  RUN_TEST(test_fermentation_profile_requires_two_unique_complete_zones);
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

void test_operator_overlay_claims_an_action_only_once_per_open() {
  TftOperatorOverlayLogic::State overlay;
  TftOperatorOverlayLogic::open(
      overlay, TftOperatorOverlayLogic::Action::ProcessStop);

  TEST_ASSERT_TRUE(TftOperatorOverlayLogic::mayDispatch(overlay));
  TEST_ASSERT_TRUE(TftOperatorOverlayLogic::claimAction(overlay));
  TEST_ASSERT_FALSE(TftOperatorOverlayLogic::mayDispatch(overlay));
  TEST_ASSERT_FALSE(TftOperatorOverlayLogic::claimAction(overlay));

  TftOperatorOverlayLogic::close(overlay);
  TEST_ASSERT_FALSE(TftOperatorOverlayLogic::isActive(overlay));
  TEST_ASSERT_FALSE(TftOperatorOverlayLogic::mayDispatch(overlay));
}

void test_tft_access_policy_respects_runtime_mode() {
  using namespace TftHmiPolicyLogic;
  TEST_ASSERT_TRUE(isModeMonitorEditable(RuntimeMode::Distillation));
  TEST_ASSERT_TRUE(isModeMonitorEditable(RuntimeMode::ManualRectification));
  TEST_ASSERT_TRUE(isModeMonitorEditable(RuntimeMode::Mashing));
  TEST_ASSERT_TRUE(isModeMonitorEditable(RuntimeMode::Hold));
  TEST_ASSERT_FALSE(isModeMonitorEditable(RuntimeMode::Rectification));
  TEST_ASSERT_FALSE(isModeMonitorEditable(RuntimeMode::Nbk));
}

void test_tft_draft_save_discard_and_navigation_policy() {
  using namespace TftHmiPolicyLogic;
  DraftState draft;
  draft.valid = true;
  draft.profileRelevant = true;
  draft.profileSelected = true;
  draft.profileCompatible = true;
  draft.profileChanged = true;
  refreshProfileDirty(draft);

  TEST_ASSERT_TRUE(hasUnsavedDraft(draft));
  TEST_ASSERT_TRUE(maySaveDraft(draft));
  TEST_ASSERT_TRUE(shouldBlockDraftNavigation(draft, true));
  TEST_ASSERT_FALSE(shouldBlockDraftNavigation(draft, false));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(DraftNavigationResult::ResolveDraft),
      static_cast<uint8_t>(resolveDraftNavigation(draft, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(DraftNavigationResult::Allow),
      static_cast<uint8_t>(resolveDraftNavigation(draft, false)));

  markDraftSaved(draft);
  TEST_ASSERT_FALSE(hasUnsavedDraft(draft));
  TEST_ASSERT_FALSE(draft.profileChanged);

  draft.profileChanged = true;
  refreshProfileDirty(draft);
  markDraftDiscarded(draft);
  TEST_ASSERT_FALSE(hasUnsavedDraft(draft));
  TEST_ASSERT_FALSE(draft.profileChanged);

  draft.profileSelected = true;
  draft.profileCompatible = false;
  TEST_ASSERT_FALSE(maySaveDraft(draft));
}

void test_tft_start_route_requires_idle_saved_draft_and_ready_preflight() {
  using TftHmiPolicyLogic::StartRouteState;

  TEST_ASSERT_TRUE(
      TftHmiPolicyLogic::mayStartFromTft({true, true, true}));
  TEST_ASSERT_FALSE(
      TftHmiPolicyLogic::mayStartFromTft({false, true, true}));
  TEST_ASSERT_FALSE(
      TftHmiPolicyLogic::mayStartFromTft({true, false, true}));
  TEST_ASSERT_FALSE(
      TftHmiPolicyLogic::mayStartFromTft({true, true, false}));
}

void test_tft_active_control_actions_require_running_mode() {
  using TftHmiPolicyLogic::ActiveControlAction;

  const ActiveControlAction actions[] = {
      ActiveControlAction::Monitor, ActiveControlAction::ProcessParameters,
      ActiveControlAction::Safety, ActiveControlAction::Events,
      ActiveControlAction::PauseResume, ActiveControlAction::Stop};
  for (const ActiveControlAction action : actions) {
    TEST_ASSERT_TRUE(
        TftHmiPolicyLogic::mayUseActiveControlAction(true, action));
    TEST_ASSERT_FALSE(
        TftHmiPolicyLogic::mayUseActiveControlAction(false, action));
  }
}

void test_tft_field_visibility_is_explicit() {
  using namespace TftHmiPolicyLogic;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FieldVisibility::Editable),
      static_cast<uint8_t>(resolveFieldVisibility(
          EditPolicy::IdleOnly, false, RuntimeMode::Idle)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FieldVisibility::ReadOnly),
      static_cast<uint8_t>(resolveFieldVisibility(
          EditPolicy::IdleOnly, true, RuntimeMode::Distillation)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FieldVisibility::Editable),
      static_cast<uint8_t>(resolveFieldVisibility(
          EditPolicy::ManualOnly, true, RuntimeMode::ManualRectification)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FieldVisibility::ReadOnly),
      static_cast<uint8_t>(resolveFieldVisibility(
          EditPolicy::ModeMonitorRuntime, true, RuntimeMode::Rectification)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FieldVisibility::ReadOnly),
      static_cast<uint8_t>(resolveFieldVisibility(
          EditPolicy::ViewOnly, false, RuntimeMode::Idle)));
}

void test_tft_service_session_closes_after_emergency() {
  using namespace TftHmiPolicyLogic;
  bool active = transitionServiceSession(
      false, ServiceSessionEvent::EnterServiceRoot);
  TEST_ASSERT_TRUE(active);
  active = transitionServiceSession(active, ServiceSessionEvent::SafetyEmergency);
  TEST_ASSERT_FALSE(active);
  active = transitionServiceSession(active, ServiceSessionEvent::EnterServiceRoot);
  active = transitionServiceSession(active, ServiceSessionEvent::LeaveServiceRoot);
  TEST_ASSERT_FALSE(active);
}

void test_tft_post_alarm_action_never_resumes_process() {
  using namespace TftHmiPolicyLogic;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(PostAlarmAction::Acknowledge),
      static_cast<uint8_t>(selectPostAlarmAction(true, false, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(PostAlarmAction::None),
      static_cast<uint8_t>(selectPostAlarmAction(true, true, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(PostAlarmAction::Reset),
      static_cast<uint8_t>(selectPostAlarmAction(true, true, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(PostAlarmAction::None),
      static_cast<uint8_t>(selectPostAlarmAction(false, false, true)));
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

void test_safety_proof_guard_blocks_unsafe_states() {
  using EquipmentTestingLogic::BlockReason;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(BlockReason::PROCESS_ACTIVE),
      static_cast<uint8_t>(EquipmentTestingLogic::evaluateGuard(
          true, false, false, true, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(BlockReason::SAFETY_LATCHED),
      static_cast<uint8_t>(EquipmentTestingLogic::evaluateGuard(
          false, true, false, true, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(BlockReason::ALARM_ACTIVE),
      static_cast<uint8_t>(EquipmentTestingLogic::evaluateGuard(
          false, false, true, true, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(BlockReason::SAFETY_NOT_OK),
      static_cast<uint8_t>(EquipmentTestingLogic::evaluateGuard(
          false, false, false, false, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(BlockReason::DEMO_MODE),
      static_cast<uint8_t>(EquipmentTestingLogic::evaluateGuard(
          false, false, false, true, true)));
}

void test_safety_proof_guard_allows_only_idle_safe_hardware() {
  TEST_ASSERT_FALSE(EquipmentTestingLogic::isBlocked(
      false, false, false, true, false));
  TEST_ASSERT_TRUE(EquipmentTestingLogic::isBlocked(
      true, false, false, true, false));
  TEST_ASSERT_TRUE(EquipmentTestingLogic::isBlocked(
      false, false, false, true, true));
}
