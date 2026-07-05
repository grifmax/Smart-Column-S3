#pragma once

#include <ArduinoJson.h>
#include <unity.h>

inline JsonDocument parseJsonOrFail(const char *json) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json);
  if (err) {
    UNITY_TEST_FAIL(__LINE__, err.c_str());
  }
  return doc;
}

inline void assertJsonStringEquals(JsonVariantConst root, const char *key,
                                   const char *expected) {
  TEST_ASSERT_TRUE_MESSAGE(root[key].is<const char *>(), key);
  TEST_ASSERT_EQUAL_STRING(expected, root[key].as<const char *>());
}

inline void assertJsonBoolEquals(JsonVariantConst root, const char *key,
                                 bool expected) {
  TEST_ASSERT_TRUE_MESSAGE(root[key].is<bool>(), key);
  TEST_ASSERT_EQUAL(expected, root[key].as<bool>());
}

inline void assertJsonIntEquals(JsonVariantConst root, const char *key,
                                long expected) {
  TEST_ASSERT_TRUE_MESSAGE(root[key].is<long>() || root[key].is<int>() ||
                               root[key].is<unsigned long>() ||
                               root[key].is<unsigned int>(),
                           key);
  TEST_ASSERT_EQUAL_INT32(expected, root[key].as<long>());
}
