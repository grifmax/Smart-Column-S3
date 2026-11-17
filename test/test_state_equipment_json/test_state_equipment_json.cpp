#include <ArduinoJson.h>
#include <unity.h>
#include <stdint.h>
#include "../../src/interface/state_equipment_json.h"

struct FakeEquipment {
  float cubeVolumeL, minHeaterSubmergeL, waterAutoStartCubeTempC;
  bool boosterHeaterEnabled;
  uint16_t boosterHeaterPowerW;
  float boosterHeaterStopCubeTempC;
  bool coolingPwmEnabled;
  uint8_t coolingPwmHoldPercent;
  uint16_t coolingPwmPullMs;
};
struct FakeDuty { static uint8_t getStartStop() { return 127; } };
void setUp() {}
void tearDown() {}

void test_equipment_common_fields_preserve_existing_json() {
  const FakeEquipment e{37.0f, 5.5f, 30.0f, true, 1800, 88.5f, true, 45, 250};
  JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
  obj["existing"] = 123;
  StateEquipmentJson::fillCommon<FakeDuty>(obj, e);
  TEST_ASSERT_EQUAL_UINT(11, obj.size());
  TEST_ASSERT_EQUAL_INT(123, obj["existing"].as<int>());
  TEST_ASSERT_FLOAT_WITHIN(0.001, e.cubeVolumeL, obj["cubeVolumeL"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.001, e.minHeaterSubmergeL, obj["minHeaterSubmergeL"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.001, e.waterAutoStartCubeTempC, obj["waterAutoStartCubeTempC"].as<float>());
  TEST_ASSERT_TRUE(obj["boosterHeaterEnabled"].is<bool>());
  TEST_ASSERT_TRUE(obj["boosterHeaterEnabled"].as<bool>());
  TEST_ASSERT_EQUAL_UINT16(e.boosterHeaterPowerW, obj["boosterHeaterPowerW"].as<uint16_t>());
  TEST_ASSERT_FLOAT_WITHIN(0.001, e.boosterHeaterStopCubeTempC, obj["boosterHeaterStopCubeTempC"].as<float>());
  TEST_ASSERT_TRUE(obj["coolingPwmEnabled"].as<bool>());
  TEST_ASSERT_EQUAL_UINT8(e.coolingPwmHoldPercent, obj["coolingPwmHoldPercent"].as<uint8_t>());
  TEST_ASSERT_EQUAL_UINT16(e.coolingPwmPullMs, obj["coolingPwmPullMs"].as<uint16_t>());
  TEST_ASSERT_EQUAL_UINT8(127, obj["coolingPwmCurrentDuty"].as<uint8_t>());
}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_equipment_common_fields_preserve_existing_json);
  return UNITY_END();
}
