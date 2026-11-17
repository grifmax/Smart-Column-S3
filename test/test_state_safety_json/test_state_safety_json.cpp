#include <ArduinoJson.h>
#include <unity.h>
#include "../../src/interface/state_safety_settings_json.h"

struct FakeSafetySettings {
  float pressureMaxMmHg;
  float tsaMaxC;
  float waterOutMaxC;
  float waterOutRiseRateCMin;
  float pressureRiseRateMmHgMin;
};

void setUp() {}
void tearDown() {}

void test_safety_settings_keys_types_and_values() {
  FakeSafetySettings input{34.5f, 91.25f, 62.5f, 4.5f, 18.75f};
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  StateSafetySettingsJson::fill(obj, input);
  TEST_ASSERT_EQUAL_UINT(5, obj.size());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, input.pressureMaxMmHg, obj["pressureMaxMmHg"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, input.tsaMaxC, obj["tsaMaxC"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, input.waterOutMaxC, obj["waterOutMaxC"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, input.waterOutRiseRateCMin, obj["waterOutRiseRateCMin"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, input.pressureRiseRateMmHgMin, obj["pressureRiseRateMmHgMin"].as<float>());
  TEST_ASSERT_TRUE(obj["pressureMaxMmHg"].is<float>());
  TEST_ASSERT_TRUE(obj["tsaMaxC"].is<float>());
  TEST_ASSERT_TRUE(obj["waterOutMaxC"].is<float>());
  TEST_ASSERT_TRUE(obj["waterOutRiseRateCMin"].is<float>());
  TEST_ASSERT_TRUE(obj["pressureRiseRateMmHgMin"].is<float>());
}

void test_flat_safety_aliases_keys_and_values() {
  FakeSafetySettings input{34.5f, 91.25f, 62.5f, 4.5f, 18.75f};
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  obj["existing"] = 42;
  StateSafetySettingsJson::fillAliases(doc.as<JsonObject>(), input);
  TEST_ASSERT_EQUAL_UINT(6, obj.size());
  TEST_ASSERT_EQUAL_INT(42, obj["existing"].as<int>());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, input.pressureMaxMmHg, obj["safety_pressure_max_mmhg"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, input.tsaMaxC, obj["safety_tsa_max_c"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, input.waterOutMaxC, obj["safety_water_out_max_c"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, input.waterOutRiseRateCMin, obj["safety_water_out_rise_rate_c_min"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, input.pressureRiseRateMmHgMin, obj["safety_pressure_rise_rate_mmhg_min"].as<float>());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_safety_settings_keys_types_and_values);
  RUN_TEST(test_flat_safety_aliases_keys_and_values);
  return UNITY_END();
}
