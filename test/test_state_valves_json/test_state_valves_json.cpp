#include <ArduinoJson.h>
#include <unity.h>
#include <stdint.h>
#include "../../src/interface/state_valves_json.h"

struct FakeValves {
  static unsigned trace;
  static bool getWater() { trace = trace * 10 + 1; return true; }
  static bool getHeads() { trace = trace * 10 + 2; return false; }
  static bool getBody() { trace = trace * 10 + 3; return true; }
  static bool hasBodyValve() { trace = trace * 10 + 4; return true; }
  static bool hasTailsValve() { trace = trace * 10 + 5; return false; }
  static bool getUno() { trace = trace * 10 + 6; return false; }
  static uint8_t getStartStop() { trace = trace * 10 + 7; return 127; }
};
unsigned FakeValves::trace = 0;
void setUp() { FakeValves::trace = 0; }
void tearDown() {}

void test_valve_common_fields_and_read_order() {
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  StateValvesJson::fillCommon<FakeValves>(obj);
  TEST_ASSERT_EQUAL_UINT32(1234567, FakeValves::trace);
  TEST_ASSERT_EQUAL_UINT(7, obj.size());
  TEST_ASSERT_TRUE(obj["water"].as<bool>());
  TEST_ASSERT_FALSE(obj["heads"].as<bool>());
  TEST_ASSERT_TRUE(obj["body"].as<bool>());
  TEST_ASSERT_TRUE(obj["bodyAvailable"].as<bool>());
  TEST_ASSERT_FALSE(obj["tailsAvailable"].as<bool>());
  TEST_ASSERT_FALSE(obj["uno"].as<bool>());
  TEST_ASSERT_EQUAL_UINT(127, obj["startStopDuty"].as<unsigned>());
}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valve_common_fields_and_read_order);
  return UNITY_END();
}
