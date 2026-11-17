#include <ArduinoJson.h>
#include <unity.h>
#include <stdint.h>
#include "../../src/interface/state_core_json.h"

struct FakeState { bool paused; bool safetyOk; uint32_t uptime; };
void setUp() {}
void tearDown() {}

void test_core_values_and_existing_fields_are_preserved() {
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  obj["existing"] = 11;
  const FakeState state{true, false, 0xFEDCBA98u};
  StateCoreJson::fill(obj, state);
  TEST_ASSERT_EQUAL_UINT(4, obj.size());
  TEST_ASSERT_EQUAL_INT(11, obj["existing"].as<int>());
  TEST_ASSERT_TRUE(obj["paused"].is<bool>());
  TEST_ASSERT_TRUE(obj["paused"].as<bool>());
  TEST_ASSERT_TRUE(obj["safetyOk"].is<bool>());
  TEST_ASSERT_FALSE(obj["safetyOk"].as<bool>());
  TEST_ASSERT_EQUAL_UINT32(state.uptime, obj["uptime"].as<uint32_t>());
}

void test_initialized_document_accepts_as_object() {
  JsonDocument doc;
  doc["mode"] = 1;
  JsonObject obj = doc.as<JsonObject>();
  StateCoreJson::fill(obj, FakeState{true, false, 1});
  TEST_ASSERT_EQUAL_UINT(4, doc.size());
  TEST_ASSERT_EQUAL_INT(1, doc["mode"].as<int>());
  TEST_ASSERT_TRUE(doc["paused"].as<bool>());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_core_values_and_existing_fields_are_preserved);
  RUN_TEST(test_initialized_document_accepts_as_object);
  return UNITY_END();
}
