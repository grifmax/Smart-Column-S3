#include <ArduinoJson.h>
#include <unity.h>

#include <string>
#include <../../src/control/fraction_program_logic.h>
#include <../../src/control/process_resume_logic.h>
#include <../../src/control/equipment_testing_logic.h>
#include <../../src/control/rect_takeoff_logic.h>
#include <../../src/interface/tft_hmi_policy_logic.h>
#include <../../src/interface/tft_operator_overlay_logic.h>
#include <../../src/interface/tft_service_test_logic.h>
#include <../../src/interface/tft_temps_focus_logic.h>
#include <../../src/control/service_test_session_logic.h>
#include <../../src/control/service_test_audit_logic.h>
#include <../../src/control/dosing_session_logic.h>
#include <../../src/control/ads1115_logic.h>
#include <../../src/control/autoclave_hardware_profile_logic.h>
#include <../../src/control/autoclave_session_logic.h>
#include <../../src/control/autoclave_safety_logic.h>
#include <../../src/control/pump_calibration_logic.h>
#include <../../src/control/runtime_metrics_logic.h>
#include <../../src/control/control_stats_logic.h>
#include <../../src/control/sensor_calibration_logic.h>
#include <../../src/control/pzem_read_policy.h>
#include <../../src/control/pzem_sample_policy.h>
#include <../../src/control/sensor_freshness_logic.h>
#include <../../src/control/slow_pwm_policy.h>
#include <../../src/ui/hmi_page_model.h>
#include <../../src/fw_build_date.h>
#include <../../src/interface/editable_field_descriptors.h>

#include "../support/json_assertions.h"

void setUp(void) {}

void tearDown(void) {}

void test_process_resume_heads_recommendation_scales_with_charge_volume() {
  using namespace ProcessResumeLogic;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f,
                           suggestedRecoveryHeadsMl(37.0f, 37.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f,
                           suggestedRecoveryHeadsMl(18.5f, 37.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 75.0f,
                           suggestedRecoveryHeadsMl(27.75f, 37.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f,
                           suggestedRecoveryHeadsMl(1.0f, 100.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f,
                           suggestedRecoveryHeadsMl(100.0f, 1.0f));
}

void test_process_resume_heads_offer_is_limited_to_body_phase() {
  using ProcessResumeLogic::shouldOfferRecoveryHeads;
  TEST_ASSERT_FALSE(shouldOfferRecoveryHeads(4, 300.0f, false, 0, 0.0f));
  TEST_ASSERT_TRUE(shouldOfferRecoveryHeads(5, 300.0f, false, 0, 0.0f));
  TEST_ASSERT_FALSE(shouldOfferRecoveryHeads(5, 0.0f, false, 0, 0.0f));
  TEST_ASSERT_FALSE(shouldOfferRecoveryHeads(6, 300.0f, false, 0, 0.0f));

  // An interrupted extra-head recovery remains offerable only when it had
  // originally collected heads and its saved target is still BODY.
  TEST_ASSERT_TRUE(shouldOfferRecoveryHeads(1, 0.0f, true, 5, 300.0f));
  TEST_ASSERT_FALSE(shouldOfferRecoveryHeads(1, 300.0f, true, 5, 0.0f));
  TEST_ASSERT_FALSE(shouldOfferRecoveryHeads(1, 300.0f, true, 6, 300.0f));
}

void test_process_resume_phase_and_crc_validation_primitives() {
  using namespace ProcessResumeLogic;
  for (uint8_t phase = 1; phase <= 7; ++phase) {
    TEST_ASSERT_TRUE(isRecoverableRectificationPhase(phase));
  }
  TEST_ASSERT_FALSE(isRecoverableRectificationPhase(0));
  TEST_ASSERT_FALSE(isRecoverableRectificationPhase(8));
  TEST_ASSERT_FALSE(isRecoverableRectificationPhase(9));

  const uint8_t payload[] = {1, 2, 3, 4};
  const uint32_t original = crc32(payload, sizeof(payload));
  TEST_ASSERT_EQUAL_UINT32(original, crc32(payload, sizeof(payload)));
  const uint8_t changed[] = {1, 2, 3, 5};
  TEST_ASSERT_NOT_EQUAL(original, crc32(changed, sizeof(changed)));
}

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

void test_runtime_metrics_tracks_cycle_buckets_and_maxima() {
  ControlRuntimeMetricsLogic::Snapshot snapshot;
  ControlRuntimeMetricsLogic::recordCycle(snapshot, 1000, 1200);
  ControlRuntimeMetricsLogic::recordCycle(snapshot, 1001, 2400);
  ControlRuntimeMetricsLogic::recordCycle(snapshot, 5000, 700);
  ControlRuntimeMetricsLogic::recordCycle(snapshot, 5001, 800);
  ControlRuntimeMetricsLogic::recordCycle(snapshot, 10000, 900);
  ControlRuntimeMetricsLogic::recordCycle(snapshot, 10001, 4000);

  TEST_ASSERT_EQUAL_UINT32(6, snapshot.cycleCount);
  TEST_ASSERT_EQUAL_UINT32(10001, snapshot.lastCycleUs);
  TEST_ASSERT_EQUAL_UINT32(10001, snapshot.maxCycleUs);
  TEST_ASSERT_EQUAL_UINT32(4000, snapshot.maxCycleGapUs);
  TEST_ASSERT_EQUAL_UINT32(1, snapshot.cyclesLe1Ms);
  TEST_ASSERT_EQUAL_UINT32(2, snapshot.cyclesLe5Ms);
  TEST_ASSERT_EQUAL_UINT32(2, snapshot.cyclesLe10Ms);
  TEST_ASSERT_EQUAL_UINT32(1, snapshot.cyclesGt10Ms);
}

void test_runtime_metrics_accumulates_command_batch_timings() {
  ControlRuntimeMetricsLogic::Snapshot snapshot;
  ControlRuntimeMetricsLogic::recordControlCommandBatch(snapshot, 300, 2);
  ControlRuntimeMetricsLogic::recordControlCommandBatch(snapshot, 120, 0);
  ControlRuntimeMetricsLogic::recordOutputCommandBatch(snapshot, 700, 1);
  ControlRuntimeMetricsLogic::recordOutputCommandBatch(snapshot, 900, 3);

  TEST_ASSERT_EQUAL_UINT32(2, snapshot.controlCommandBatchCount);
  TEST_ASSERT_EQUAL_UINT32(2, snapshot.controlCommandsProcessed);
  TEST_ASSERT_EQUAL_UINT32(120, snapshot.lastControlCommandBatchUs);
  TEST_ASSERT_EQUAL_UINT32(300, snapshot.maxControlCommandBatchUs);
  TEST_ASSERT_EQUAL_UINT32(2, snapshot.outputCommandBatchCount);
  TEST_ASSERT_EQUAL_UINT32(4, snapshot.outputCommandsProcessed);
  TEST_ASSERT_EQUAL_UINT32(900, snapshot.lastOutputCommandBatchUs);
  TEST_ASSERT_EQUAL_UINT32(900, snapshot.maxOutputCommandBatchUs);
}

void test_runtime_metrics_tracks_safety_snapshot_and_unknown_sample_age() {
  ControlRuntimeMetricsLogic::Snapshot snapshot;
  ControlRuntimeMetricsLogic::recordSafetyTrip(snapshot, UINT32_MAX, 10);
  ControlRuntimeMetricsLogic::recordSafetyTrip(snapshot, 25, 80);
  ControlRuntimeMetricsLogic::recordSafetyTrip(snapshot, 60, 120);
  ControlRuntimeMetricsLogic::recordRepeatedSafetyTrip(snapshot);
  ControlRuntimeMetricsLogic::recordStaleSensorCheck(snapshot);
  ControlRuntimeMetricsLogic::recordSnapshotCopy(snapshot, 45);
  ControlRuntimeMetricsLogic::recordSnapshotCopy(snapshot, 30);

  TEST_ASSERT_EQUAL_UINT32(3, snapshot.safetyTripCount);
  TEST_ASSERT_EQUAL_UINT32(2, snapshot.safetySampleAgeKnownCount);
  TEST_ASSERT_EQUAL_UINT32(60, snapshot.lastSafetySampleAgeMs);
  TEST_ASSERT_EQUAL_UINT32(60, snapshot.maxSafetySampleAgeMs);
  TEST_ASSERT_EQUAL_UINT32(120, snapshot.maxOutputStopUs);
  TEST_ASSERT_EQUAL_UINT32(1, snapshot.repeatedSafetyTripCount);
  TEST_ASSERT_EQUAL_UINT32(1, snapshot.staleSensorCheckCount);
  TEST_ASSERT_EQUAL_UINT32(2, snapshot.snapshotCopyCount);
  TEST_ASSERT_EQUAL_UINT32(30, snapshot.lastSnapshotCopyUs);
  TEST_ASSERT_EQUAL_UINT32(45, snapshot.maxSnapshotCopyUs);
  TEST_ASSERT_EQUAL_UINT32(
      25, ControlRuntimeMetricsLogic::sampleAgeMs(125, 100, true));
  TEST_ASSERT_EQUAL_UINT32(
      20, ControlRuntimeMetricsLogic::sampleAgeMs(10, UINT32_MAX - 9, true));
  TEST_ASSERT_EQUAL_UINT32(
      UINT32_MAX, ControlRuntimeMetricsLogic::sampleAgeMs(125, 100, false));
}

void test_fraction_program_end_reason_returns_none_without_conditions();
void test_runtime_metrics_tracks_cycle_buckets_and_maxima();
void test_runtime_metrics_accumulates_command_batch_timings();
void test_runtime_metrics_tracks_safety_snapshot_and_unknown_sample_age();
void test_fraction_program_end_reason_detects_each_automatic_condition();
void test_fraction_program_end_reason_uses_documented_or_priority();
void test_pzem_fresh_sample_requires_valid_snapshot();
void test_pzem_fresh_sample_rejects_stale_timestamp();
void test_pzem_stale_sample_requires_unchanged_window();
void test_pzem_sample_policy_accepts_load_step_without_delta_gate();
void test_pzem_sample_policy_rejects_physical_violations();
void test_hmi_window_clamps_scroll_and_reports_count();
void test_hmi_rail_hidden_when_page_fits();
void test_hmi_thumb_scales_with_scroll();
void test_hmi_footer_variants();
void test_hmi_table_font_shrinks_with_rows();
void test_hmi_page_stack_and_row_hit_testing();
void test_slow_pwm_splits_period_by_duty();
void test_slow_pwm_is_on_respects_phase();
void test_slow_pwm_clamps_period_and_duty();
void test_slow_pwm_survives_millis_overflow();
void test_slow_pwm_converts_raw_duty_to_percent();
void test_fw_build_date_is_valid();
void test_fraction_program_manual_advance_has_priority();
void test_fraction_program_route_gate_requires_ready_route_and_settle_delay();
void test_optional_sensor_does_not_block_when_not_required();
void test_required_sensor_blocks_when_missing_or_stale();
void test_sensor_freshness_handles_millis_rollover();
void test_sensor_safety_uses_last_successful_sample_until_timeout();

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
void test_equipment_hmi_fields_expose_canonical_limits_and_steps();
void test_safety_hmi_fields_match_api_limits_and_steps();
void test_cooling_pwm_percent_conversion_handles_limits();
void test_power_command_captures_owner_reason_timestamp_and_safe_stop();
void test_power_gateway_reserves_safe_stop_when_normal_queue_is_full();
void test_power_output_state_keeps_requested_and_applied_values_separate();
void test_snapshot_buffer_is_coherent_under_concurrent_publish_and_read();
void test_power_gateway_pump_stop_cancels_stale_starts_and_has_priority();
void test_power_gateway_queue_clear_discards_all_pending_output_commands();
void test_control_command_queue_prioritizes_stop_and_cancels_older_commands();
void test_control_command_queue_reports_overflow_and_coalesces_stop();
void test_control_command_queue_assigns_ids_and_returns_idempotent_results();
void test_control_command_queue_preserves_heater_override_command();
void test_control_command_queue_preserves_start_payload_bytes();
void test_control_command_queue_validates_active_step_edits();
void test_control_command_queue_safety_gate_allows_only_safety_overrides();
void test_power_command_safe_stop_rules_match_output_semantics();
void test_power_gateway_safety_trip_blocks_energizing_requests();
void test_power_gateway_owner_priority_is_safety_then_fsm_service_manual();
void test_power_command_pulse_contract_is_self_terminating();
void test_power_command_service_safe_stop_keeps_all_outputs_auditable();
void test_power_command_fsm_control_reason_covers_control_loop_outputs();
void test_power_command_all_valves_stop_does_not_claim_full_output_stop();
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
void test_pump_calibration_cloud_measurement_skips_prime_but_keeps_review();
void test_pump_calibration_time_fallback_requires_completed_zero_step_measurement();
void test_pump_calibration_requires_confirmation_and_hard_bounds_large_changes();
void test_sensor_calibration_keeps_ds18_mapping_unique();
void test_service_test_audit_uses_stable_lifecycle_tokens();
void test_dosing_session_requires_prime_then_tracks_only_target_volume();
void test_dosing_session_finish_reasons_have_safe_terminal_phases();
void test_dosing_session_has_independent_prime_and_dose_timeouts();
void test_ads1115_single_ended_config_selects_channels_with_bounded_settings();
void test_ads1115_conversion_math_and_timeout_handle_signed_values_and_rollover();
void test_ads1115_read_single_ended_succeeds_after_conversion_ready();
void test_ads1115_read_single_ended_fails_on_bus_error_or_conversion_timeout();
void test_autoclave_profile_requires_explicit_hardware_and_safety_approval();
void test_autoclave_sterilization_timer_only_counts_in_temperature_band();
void test_autoclave_safety_faults_on_required_sensor_pressure_rate_and_timeout();

void test_control_stats_accumulates_heater_and_power();
void test_control_stats_counts_saturation_and_unpowered_seconds();
void test_control_stats_tracks_band_overshoot_pauses_and_degraded();
void test_control_stats_counts_safety_trips_on_edges_only();
void test_control_stats_clamps_long_gaps();
void test_control_stats_ignores_absent_pressure_and_cooling_sensors();

void test_setpoint_guard_blocks_actuator_for_every_live_output();
void test_setpoint_guard_keeps_runtime_and_constant_fields_editable();
void test_setpoint_guard_reports_any_live_output_once();
void test_temps_focus_fires_on_process_start_only_once();
void test_temps_focus_fires_on_calibration_and_dosing_edges();
void test_temps_focus_ignores_steady_state_and_combinations();

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_json_harness_parses_and_asserts_contract_fields);
  RUN_TEST(test_process_resume_heads_recommendation_scales_with_charge_volume);
  RUN_TEST(test_process_resume_heads_offer_is_limited_to_body_phase);
  RUN_TEST(test_process_resume_phase_and_crc_validation_primitives);
  RUN_TEST(test_runtime_metrics_tracks_cycle_buckets_and_maxima);
  RUN_TEST(test_runtime_metrics_accumulates_command_batch_timings);
  RUN_TEST(test_runtime_metrics_tracks_safety_snapshot_and_unknown_sample_age);
  RUN_TEST(test_fraction_program_end_reason_returns_none_without_conditions);
  RUN_TEST(test_fraction_program_end_reason_detects_each_automatic_condition);
  RUN_TEST(test_fraction_program_end_reason_uses_documented_or_priority);
  RUN_TEST(test_fraction_program_manual_advance_has_priority);
  RUN_TEST(test_pzem_fresh_sample_requires_valid_snapshot);
  RUN_TEST(test_pzem_fresh_sample_rejects_stale_timestamp);
  RUN_TEST(test_pzem_stale_sample_requires_unchanged_window);
  RUN_TEST(test_pzem_sample_policy_accepts_load_step_without_delta_gate);
  RUN_TEST(test_pzem_sample_policy_rejects_physical_violations);
  RUN_TEST(test_hmi_window_clamps_scroll_and_reports_count);
  RUN_TEST(test_hmi_rail_hidden_when_page_fits);
  RUN_TEST(test_hmi_thumb_scales_with_scroll);
  RUN_TEST(test_hmi_footer_variants);
  RUN_TEST(test_hmi_table_font_shrinks_with_rows);
  RUN_TEST(test_hmi_page_stack_and_row_hit_testing);
  RUN_TEST(test_slow_pwm_splits_period_by_duty);
  RUN_TEST(test_slow_pwm_is_on_respects_phase);
  RUN_TEST(test_slow_pwm_clamps_period_and_duty);
  RUN_TEST(test_slow_pwm_survives_millis_overflow);
  RUN_TEST(test_slow_pwm_converts_raw_duty_to_percent);
  RUN_TEST(test_fw_build_date_is_valid);
  RUN_TEST(test_fraction_program_route_gate_requires_ready_route_and_settle_delay);
  RUN_TEST(test_optional_sensor_does_not_block_when_not_required);
  RUN_TEST(test_required_sensor_blocks_when_missing_or_stale);
  RUN_TEST(test_sensor_freshness_handles_millis_rollover);
  RUN_TEST(test_sensor_safety_uses_last_successful_sample_until_timeout);
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
  RUN_TEST(test_equipment_hmi_fields_expose_canonical_limits_and_steps);
  RUN_TEST(test_safety_hmi_fields_match_api_limits_and_steps);
  RUN_TEST(test_cooling_pwm_percent_conversion_handles_limits);
  RUN_TEST(test_power_command_captures_owner_reason_timestamp_and_safe_stop);
  RUN_TEST(test_power_gateway_reserves_safe_stop_when_normal_queue_is_full);
  RUN_TEST(test_power_output_state_keeps_requested_and_applied_values_separate);
  RUN_TEST(test_snapshot_buffer_is_coherent_under_concurrent_publish_and_read);
  RUN_TEST(test_power_gateway_pump_stop_cancels_stale_starts_and_has_priority);
  RUN_TEST(test_power_gateway_queue_clear_discards_all_pending_output_commands);
  RUN_TEST(test_control_command_queue_prioritizes_stop_and_cancels_older_commands);
  RUN_TEST(test_control_command_queue_reports_overflow_and_coalesces_stop);
  RUN_TEST(test_control_command_queue_assigns_ids_and_returns_idempotent_results);
  RUN_TEST(test_control_command_queue_preserves_heater_override_command);
  RUN_TEST(test_control_command_queue_preserves_start_payload_bytes);
  RUN_TEST(test_control_command_queue_validates_active_step_edits);
  RUN_TEST(test_control_command_queue_safety_gate_allows_only_safety_overrides);
  RUN_TEST(test_power_command_safe_stop_rules_match_output_semantics);
  RUN_TEST(test_power_gateway_safety_trip_blocks_energizing_requests);
  RUN_TEST(test_power_gateway_owner_priority_is_safety_then_fsm_service_manual);
  RUN_TEST(test_power_command_pulse_contract_is_self_terminating);
  RUN_TEST(test_power_command_service_safe_stop_keeps_all_outputs_auditable);
  RUN_TEST(test_power_command_fsm_control_reason_covers_control_loop_outputs);
  RUN_TEST(test_power_command_all_valves_stop_does_not_claim_full_output_stop);
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
  RUN_TEST(test_pump_calibration_cloud_measurement_skips_prime_but_keeps_review);
  RUN_TEST(test_pump_calibration_time_fallback_requires_completed_zero_step_measurement);
  RUN_TEST(test_pump_calibration_requires_confirmation_and_hard_bounds_large_changes);
  RUN_TEST(test_sensor_calibration_keeps_ds18_mapping_unique);
  RUN_TEST(test_service_test_audit_uses_stable_lifecycle_tokens);
  RUN_TEST(test_dosing_session_requires_prime_then_tracks_only_target_volume);
  RUN_TEST(test_dosing_session_finish_reasons_have_safe_terminal_phases);
  RUN_TEST(test_dosing_session_has_independent_prime_and_dose_timeouts);
  RUN_TEST(test_ads1115_single_ended_config_selects_channels_with_bounded_settings);
  RUN_TEST(test_ads1115_conversion_math_and_timeout_handle_signed_values_and_rollover);
  RUN_TEST(test_ads1115_read_single_ended_succeeds_after_conversion_ready);
  RUN_TEST(test_ads1115_read_single_ended_fails_on_bus_error_or_conversion_timeout);
  RUN_TEST(test_autoclave_profile_requires_explicit_hardware_and_safety_approval);
  RUN_TEST(test_autoclave_sterilization_timer_only_counts_in_temperature_band);
  RUN_TEST(test_autoclave_safety_faults_on_required_sensor_pressure_rate_and_timeout);
  RUN_TEST(test_control_stats_accumulates_heater_and_power);
  RUN_TEST(test_control_stats_counts_saturation_and_unpowered_seconds);
  RUN_TEST(test_control_stats_tracks_band_overshoot_pauses_and_degraded);
  RUN_TEST(test_control_stats_counts_safety_trips_on_edges_only);
  RUN_TEST(test_control_stats_clamps_long_gaps);
  RUN_TEST(test_control_stats_ignores_absent_pressure_and_cooling_sensors);
  RUN_TEST(test_temps_focus_fires_on_process_start_only_once);
  RUN_TEST(test_temps_focus_fires_on_calibration_and_dosing_edges);
  RUN_TEST(test_temps_focus_ignores_steady_state_and_combinations);
  RUN_TEST(test_setpoint_guard_blocks_actuator_for_every_live_output);
  RUN_TEST(test_setpoint_guard_keeps_runtime_and_constant_fields_editable);
  RUN_TEST(test_setpoint_guard_reports_any_live_output_once);
  return UNITY_END();
}
#include <../../src/control/fraction_program_logic.h>

void test_ads1115_single_ended_config_selects_channels_with_bounded_settings() {
  TEST_ASSERT_EQUAL_HEX16(0xC383, Ads1115Logic::singleEndedConfig(0));
  TEST_ASSERT_EQUAL_HEX16(0xD383, Ads1115Logic::singleEndedConfig(1));
  TEST_ASSERT_EQUAL_HEX16(0xE383, Ads1115Logic::singleEndedConfig(2));
  TEST_ASSERT_EQUAL_HEX16(0xF383, Ads1115Logic::singleEndedConfig(3));
  TEST_ASSERT_EQUAL_HEX16(0, Ads1115Logic::singleEndedConfig(4));
  TEST_ASSERT_FALSE(Ads1115Logic::conversionReady(0x4383));
  TEST_ASSERT_TRUE(Ads1115Logic::conversionReady(0xC383));
}

void test_ads1115_conversion_math_and_timeout_handle_signed_values_and_rollover() {
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.0f,
                           Ads1115Logic::voltsFromRaw(0));
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 2.048f,
                           Ads1115Logic::voltsFromRaw(16384));
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, -4.096f,
                           Ads1115Logic::voltsFromRaw(-32768));
  TEST_ASSERT_FALSE(Ads1115Logic::timedOut(3, UINT32_MAX - 5, 10));
  TEST_ASSERT_TRUE(Ads1115Logic::timedOut(4, UINT32_MAX - 5, 10));
}

struct FakeAds1115Bus {
  uint16_t writtenConfig = 0;
  uint16_t conversion = 0x4000;
  uint32_t notReadyReads = 0;
  bool failWrite = false;
  bool failStatusRead = false;
  uint32_t configWrites = 0;

  bool writeRegister(uint8_t reg, uint16_t value) {
    if (failWrite || reg != 0x01) return false;
    writtenConfig = static_cast<uint16_t>(value & ~Ads1115Logic::kOsStart);
    ++configWrites;
    return true;
  }

  bool readRegister(uint8_t reg, uint16_t &value) {
    if (reg == 0x01) {
      if (failStatusRead) return false;
      if (notReadyReads > 0) {
        --notReadyReads;
        value = writtenConfig;
      } else {
        value = static_cast<uint16_t>(writtenConfig | Ads1115Logic::kOsStart);
      }
      return true;
    }
    if (reg != 0x00) return false;
    value = conversion;
    return true;
  }
};

struct FakeAds1115Clock {
  uint32_t now = 0;
  uint32_t waits = 0;
  uint32_t nowMs() const { return now; }
  void waitOneMs() { ++now; ++waits; }
};

void test_ads1115_read_single_ended_succeeds_after_conversion_ready() {
  FakeAds1115Bus bus;
  bus.notReadyReads = 2;
  FakeAds1115Clock clock;
  int16_t raw = -1;
  float volts = -1.0f;

  TEST_ASSERT_TRUE(Ads1115Logic::readSingleEnded(bus, clock, 1, 40, raw, volts));
  TEST_ASSERT_EQUAL_HEX16(0xD383, bus.writtenConfig | Ads1115Logic::kOsStart);
  TEST_ASSERT_EQUAL_UINT32(1, bus.configWrites);
  TEST_ASSERT_EQUAL_INT16(16384, raw);
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 2.048f, volts);
  TEST_ASSERT_EQUAL_UINT32(2, clock.waits);
}

void test_ads1115_read_single_ended_fails_on_bus_error_or_conversion_timeout() {
  FakeAds1115Bus bus;
  bus.notReadyReads = 100;
  FakeAds1115Clock clock;
  int16_t raw = 123;
  float volts = 12.0f;

  TEST_ASSERT_FALSE(Ads1115Logic::readSingleEnded(bus, clock, 0, 5, raw, volts));
  TEST_ASSERT_EQUAL_INT16(0, raw);
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.0f, volts);
  TEST_ASSERT_EQUAL_UINT32(5, clock.waits);

  bus = FakeAds1115Bus{};
  bus.failStatusRead = true;
  clock = FakeAds1115Clock{};
  raw = 123;
  volts = 12.0f;
  TEST_ASSERT_FALSE(Ads1115Logic::readSingleEnded(bus, clock, 0, 40, raw, volts));
  TEST_ASSERT_EQUAL_INT16(0, raw);
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.0f, volts);
  TEST_ASSERT_EQUAL_UINT32(0, clock.waits);
}

void test_pzem_fresh_sample_requires_valid_snapshot() {
  TEST_ASSERT_TRUE(PzemReadPolicy::isFreshSample(true, true, 5000, 4500, 2500));
  TEST_ASSERT_FALSE(PzemReadPolicy::isFreshSample(true, false, 5000, 4500, 2500));
  TEST_ASSERT_FALSE(PzemReadPolicy::isFreshSample(false, true, 5000, 4500, 2500));
}

void test_pzem_fresh_sample_rejects_stale_timestamp() {
  TEST_ASSERT_FALSE(PzemReadPolicy::isFreshSample(true, true, 8001, 5000, 2500));
}

void test_pzem_stale_sample_requires_unchanged_window() {
  TEST_ASSERT_FALSE(PzemReadPolicy::isStaleSample(true, 6000, 1000, 5000));
  TEST_ASSERT_FALSE(PzemReadPolicy::isStaleSample(false, 5999, 1000, 5000));
  TEST_ASSERT_TRUE(PzemReadPolicy::isStaleSample(false, 6000, 1000, 5000));
}

void test_pzem_sample_policy_accepts_load_step_without_delta_gate() {
  // Реальный скачок нагрузки не должен считаться выбросом: 0 -> 9 A,
  // 0 -> 2000 Вт, 230 -> 205 В (просадка под ТЭНом).
  TEST_ASSERT_TRUE(PzemSamplePolicy::isPlausibleSnapshot(230.0f, 9.0f, 2000.0f, 30.0f));
  TEST_ASSERT_TRUE(PzemSamplePolicy::isPlausibleSnapshot(205.0f, 9.7f, 1990.0f, 30.0f));
  TEST_ASSERT_TRUE(PzemSamplePolicy::isPlausibleSnapshot(230.0f, 0.0f, 0.0f, 30.0f));
}

void test_pzem_sample_policy_rejects_physical_violations() {
  TEST_ASSERT_FALSE(PzemSamplePolicy::isPlausibleSnapshot(NAN, 9.0f, 2000.0f, 30.0f));
  TEST_ASSERT_FALSE(PzemSamplePolicy::isPlausibleSnapshot(230.0f, NAN, 2000.0f, 30.0f));
  TEST_ASSERT_FALSE(PzemSamplePolicy::isPlausibleSnapshot(230.0f, 9.0f, NAN, 30.0f));
  TEST_ASSERT_FALSE(PzemSamplePolicy::isPlausibleSnapshot(301.0f, 9.0f, 2000.0f, 30.0f));
  TEST_ASSERT_FALSE(PzemSamplePolicy::isPlausibleSnapshot(230.0f, 31.0f, 2000.0f, 30.0f));
  TEST_ASSERT_FALSE(PzemSamplePolicy::isPlausibleSnapshot(230.0f, 9.0f, 12000.0f, 30.0f));
  TEST_ASSERT_FALSE(PzemSamplePolicy::isPlausibleSnapshot(230.0f, -1.0f, 2000.0f, 30.0f));
}

void test_hmi_window_clamps_scroll_and_reports_count() {
  using namespace HmiPage;
  TEST_ASSERT_EQUAL_UINT16(0, maxScroll(4, kVisibleRows));
  TEST_ASSERT_EQUAL_UINT16(6, maxScroll(10, kVisibleRows));
  TEST_ASSERT_EQUAL_UINT16(6, clampScroll(99, 10, kVisibleRows));
  TEST_ASSERT_EQUAL_UINT16(0, clampScroll(0, 10, kVisibleRows));

  const Window full = windowFor(0, 4, kVisibleRows);
  TEST_ASSERT_EQUAL_UINT16(0, full.first);
  TEST_ASSERT_EQUAL_UINT8(4, full.count);
  TEST_ASSERT_EQUAL_UINT16(4, full.total);

  const Window last = windowFor(99, 10, kVisibleRows);
  TEST_ASSERT_EQUAL_UINT16(6, last.first);
  TEST_ASSERT_EQUAL_UINT8(4, last.count);
}

void test_hmi_rail_hidden_when_page_fits() {
  using namespace HmiPage;
  TEST_ASSERT_FALSE(railVisible(4, kVisibleRows));
  TEST_ASSERT_TRUE(railVisible(5, kVisibleRows));
  TEST_ASSERT_FALSE(railVisible(0, kVisibleRows));
}

void test_hmi_thumb_scales_with_scroll() {
  using namespace HmiPage;
  const Thumb top = thumbFor(200, 10, kVisibleRows, 0);
  TEST_ASSERT_EQUAL_UINT16(0, top.top);
  TEST_ASSERT_EQUAL_UINT16(80, top.height);

  const Thumb bottom = thumbFor(200, 10, kVisibleRows, 6);
  TEST_ASSERT_EQUAL_UINT16(120, bottom.top);
  TEST_ASSERT_EQUAL_UINT16(80, bottom.height);

  // Одна страница: ползунок занимает всю дорожку.
  const Thumb single = thumbFor(200, 3, kVisibleRows, 0);
  TEST_ASSERT_EQUAL_UINT16(0, single.top);
  TEST_ASSERT_EQUAL_UINT16(200, single.height);

  // Минимальная высота ползунка не даёт ему исчезнуть на длинном списке.
  const Thumb longList = thumbFor(200, 200, kVisibleRows, 0);
  TEST_ASSERT_EQUAL_UINT16(kMinThumbPx, longList.height);
}

void test_hmi_footer_variants() {
  using namespace HmiPage;

  const Footer idleRoot = footerFor(false, false, false, false, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::Back),
                          static_cast<uint8_t>(idleRoot.left));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::Exit),
                          static_cast<uint8_t>(idleRoot.middle));
  TEST_ASSERT_FALSE(idleRoot.leftEnabled);
  TEST_ASSERT_FALSE(idleRoot.rightEnabled);

  const Footer dirtySub = footerFor(false, true, false, false, true);
  TEST_ASSERT_TRUE(dirtySub.rightEnabled);

  const Footer running = footerFor(true, false, false, false, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::Pause),
                          static_cast<uint8_t>(running.left));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::Stop),
                          static_cast<uint8_t>(running.middle));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::Service),
                          static_cast<uint8_t>(running.right));

  const Footer infoIdle = footerFor(false, false, true, false, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::None),
                          static_cast<uint8_t>(infoIdle.middle));
  TEST_ASSERT_FALSE(infoIdle.rightEnabled);

  const Footer infoRunning = footerFor(true, false, true, false, false);
  TEST_ASSERT_TRUE(infoRunning.rightEnabled);

  const Footer keyboard = footerFor(false, true, false, true, true);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::Cancel),
                          static_cast<uint8_t>(keyboard.left));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::Done),
                          static_cast<uint8_t>(keyboard.right));
}

void test_hmi_table_font_shrinks_with_rows() {
  using namespace HmiPage;
  const uint8_t sixRows = fontForRows(6, 248, 10, 17);
  const uint8_t elevenRows = fontForRows(11, 248, 10, 17);
  TEST_ASSERT_TRUE(sixRows > elevenRows);
  TEST_ASSERT_EQUAL_UINT8(17, fontForRows(2, 248, 10, 17));
  TEST_ASSERT_EQUAL_UINT8(10, fontForRows(30, 248, 10, 17));
  TEST_ASSERT_EQUAL_UINT8(6, rowsPerColumn(11, 2));
  TEST_ASSERT_EQUAL_UINT8(4, rowsPerColumn(7, 2));
}

void test_slow_pwm_splits_period_by_duty() {
  using namespace SlowPwm;
  TEST_ASSERT_EQUAL_UINT32(5000, periodMs(5));
  TEST_ASSERT_EQUAL_UINT32(2500, onTimeMs(5, 50));
  TEST_ASSERT_EQUAL_UINT32(2500, offTimeMs(5, 50));
  TEST_ASSERT_EQUAL_UINT32(0, onTimeMs(5, 0));
  TEST_ASSERT_EQUAL_UINT32(5000, offTimeMs(5, 0));
  TEST_ASSERT_EQUAL_UINT32(5000, onTimeMs(5, 100));
  TEST_ASSERT_EQUAL_UINT32(0, offTimeMs(5, 100));
  // 30 % от 10 с = 3 с открыт, 7 с закрыт
  TEST_ASSERT_EQUAL_UINT32(3000, onTimeMs(10, 30));
  TEST_ASSERT_EQUAL_UINT32(7000, offTimeMs(10, 30));
}

void test_slow_pwm_is_on_respects_phase() {
  using namespace SlowPwm;
  // цикл 5 с, 50 %: открыт [0..2500), закрыт [2500..5000)
  TEST_ASSERT_TRUE(isOn(0, 0, 5, 50));
  TEST_ASSERT_TRUE(isOn(2499, 0, 5, 50));
  TEST_ASSERT_FALSE(isOn(2500, 0, 5, 50));
  TEST_ASSERT_FALSE(isOn(4999, 0, 5, 50));
  TEST_ASSERT_TRUE(isOn(5000, 0, 5, 50));   // второй цикл, начало открытой части
  TEST_ASSERT_TRUE(isOn(5001, 0, 5, 50));
  TEST_ASSERT_FALSE(isOn(7500, 0, 5, 50));  // второй цикл, начало закрытой части

  // скважность 0 и 100 — всегда закрыт / всегда открыт
  TEST_ASSERT_FALSE(isOn(100, 0, 5, 0));
  TEST_ASSERT_TRUE(isOn(100, 0, 5, 100));
  TEST_ASSERT_TRUE(isOn(999999, 0, 5, 100));
}

void test_slow_pwm_clamps_period_and_duty() {
  using namespace SlowPwm;
  TEST_ASSERT_EQUAL_UINT16(kMinPeriodSec, clampPeriodSec(0));
  TEST_ASSERT_EQUAL_UINT16(kMaxPeriodSec, clampPeriodSec(1000));
  TEST_ASSERT_EQUAL_UINT16(5, clampPeriodSec(5));
  TEST_ASSERT_EQUAL_UINT8(100, clampDutyPercent(250));
  TEST_ASSERT_EQUAL_UINT8(0, clampDutyPercent(0));
  TEST_ASSERT_EQUAL_UINT8(37, clampDutyPercent(37));
}

void test_slow_pwm_survives_millis_overflow() {
  using namespace SlowPwm;
  // старт незадолго до переполнения millis(): 512 мс внутри цикла
  const uint32_t start = 0xFFFFFF00u;
  const uint32_t now = start + 512u;  // переполнение
  TEST_ASSERT_EQUAL_UINT32(512, phaseMs(now, start, 5));
  TEST_ASSERT_TRUE(isOn(now, start, 5, 50));
  TEST_ASSERT_FALSE(isOn(start + 3000u, start, 5, 50));
}

void test_slow_pwm_converts_raw_duty_to_percent() {
  using namespace SlowPwm;
  TEST_ASSERT_EQUAL_UINT8(0, dutyPercentFromRaw(0));
  TEST_ASSERT_EQUAL_UINT8(100, dutyPercentFromRaw(255));
  TEST_ASSERT_EQUAL_UINT8(50, dutyPercentFromRaw(127));
  TEST_ASSERT_EQUAL_UINT8(50, dutyPercentFromRaw(128));
  TEST_ASSERT_EQUAL_UINT8(10, dutyPercentFromRaw(26));
}


void test_fw_build_date_is_valid() {
  TEST_ASSERT_TRUE(FwBuildInfo::kDay >= 1 && FwBuildInfo::kDay <= 31);
  TEST_ASSERT_TRUE(FwBuildInfo::kMonth >= 1 && FwBuildInfo::kMonth <= 12);
  TEST_ASSERT_TRUE(FwBuildInfo::kYear >= 2024 && FwBuildInfo::kYear <= 2099);
  char buf[16];
  FwBuildInfo::format(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_CHAR('.', buf[2]);
  TEST_ASSERT_EQUAL_CHAR('.', buf[5]);
  TEST_ASSERT_EQUAL_UINT8(10, static_cast<uint8_t>(strlen(buf)));
}

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
  TEST_ASSERT_FALSE(isModeMonitorEditable(RuntimeMode::Autoclave));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FieldVisibility::ReadOnly),
      static_cast<uint8_t>(resolveFieldVisibility(
          EditPolicy::ModeMonitorRuntime, true, RuntimeMode::Autoclave)));
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

void test_optional_sensor_does_not_block_when_not_required() {
  TEST_ASSERT_TRUE(SensorFreshnessLogic::isAvailableWhenRequired(
      false, false, 0, 10000, 5000));
  TEST_ASSERT_TRUE(SensorFreshnessLogic::isAvailableWhenRequired(
      false, false, 1000, 10000, 5000));
}

void test_required_sensor_blocks_when_missing_or_stale() {
  TEST_ASSERT_FALSE(SensorFreshnessLogic::isAvailableWhenRequired(
      true, false, 1000, 2000, 5000));
  TEST_ASSERT_FALSE(SensorFreshnessLogic::isAvailableWhenRequired(
      true, true, 1000, 7001, 5000));
  TEST_ASSERT_TRUE(SensorFreshnessLogic::isAvailableWhenRequired(
      true, true, 1000, 6000, 5000));
}

void test_sensor_freshness_handles_millis_rollover() {
  const uint32_t sampleMs = UINT32_MAX - 500;
  const uint32_t nowMs = 250;
  TEST_ASSERT_TRUE(SensorFreshnessLogic::isFresh(
      true, sampleMs, nowMs, 1000));
  TEST_ASSERT_FALSE(SensorFreshnessLogic::isFresh(
      true, sampleMs, nowMs, 700));
}

void test_sensor_safety_uses_last_successful_sample_until_timeout() {
  TEST_ASSERT_TRUE(SensorFreshnessLogic::isRecentSample(1000, 15000, 15000));
  TEST_ASSERT_FALSE(SensorFreshnessLogic::isRecentSample(1000, 16001, 15000));
  TEST_ASSERT_FALSE(SensorFreshnessLogic::isRecentSample(0, 15000, 15000));
}
