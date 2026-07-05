#include <ArduinoJson.h>
#include <unity.h>

#include <string>

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

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_json_harness_parses_and_asserts_contract_fields);
  return UNITY_END();
}
