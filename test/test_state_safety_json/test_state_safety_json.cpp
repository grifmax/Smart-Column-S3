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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_safety_settings_keys_types_and_values);
  return UNITY_END();
}
