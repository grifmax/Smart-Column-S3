#include <ArduinoJson.h>
#include <unity.h>
#include "../../src/control/history_json_keys.h"

void setUp() {}
void tearDown() {}

void test_key_presence_matches_legacy_for_every_json_type() {
  JsonDocument document;
  DeserializationError error = deserializeJson(document,
      R"({"nil":null,"number":1.5,"integer":12,"text":"x","flag":true,"object":{},"array":[]})");
  TEST_ASSERT_FALSE(error);
  JsonObjectConst object = document.as<JsonObjectConst>();
  const char* keys[] = {"absent", "nil", "number", "integer", "text", "flag", "object", "array"};
  for (const char* key : keys) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    bool expected = object.containsKey(key);
#pragma GCC diagnostic pop
    TEST_ASSERT_EQUAL(expected, HistoryJsonKeys::contains(object, key));
  }
  TEST_ASSERT_FALSE(HistoryJsonKeys::contains(object, "absent"));
  TEST_ASSERT_TRUE(HistoryJsonKeys::contains(object, "nil"));
}

void test_key_with_embedded_zero_does_not_match_prefix() {
  JsonDocument document;
  TEST_ASSERT_FALSE(deserializeJson(document, R"({"coolingMarginMin\u0000extra":null})"));
  JsonObjectConst object = document.as<JsonObjectConst>();
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  bool old = object.containsKey("coolingMarginMin");
#pragma GCC diagnostic pop
  TEST_ASSERT_EQUAL(old, HistoryJsonKeys::contains(object, "coolingMarginMin"));
}

void test_null_object_has_no_keys() {
  JsonObjectConst object;
  TEST_ASSERT_FALSE(HistoryJsonKeys::contains(object, "nil"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_key_presence_matches_legacy_for_every_json_type);
  RUN_TEST(test_null_object_has_no_keys);
  RUN_TEST(test_key_with_embedded_zero_does_not_match_prefix);
  return UNITY_END();
}
