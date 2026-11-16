#pragma once

#include <ArduinoJson.h>
#include <cstring>

namespace HistoryJsonKeys {

// is<T>() считает null отсутствующим, но старый формат различает эти случаи.
inline bool contains(JsonObjectConst object, const char* key) {
  if (!key) return false;
  const size_t keyLength = std::strlen(key);
  for (JsonPairConst pair : object) {
    JsonString candidate = pair.key();
    if (candidate.size() == keyLength &&
        std::memcmp(candidate.c_str(), key, keyLength) == 0) return true;
  }
  return false;
}

} // namespace HistoryJsonKeys
