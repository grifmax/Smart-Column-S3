#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure helpers used by TFT sensor calibration. Keeping the address update
// separate from the UI makes the one-address-per-role rule testable without
// an Arduino or sensor bus.
namespace SensorCalibrationLogic {

inline bool isZeroAddress(const uint8_t address[8]) {
  if (address == nullptr) return true;
  for (uint8_t i = 0; i < 8; ++i) {
    if (address[i] != 0) return false;
  }
  return true;
}

inline bool addressesEqual(const uint8_t left[8], const uint8_t right[8]) {
  if (left == nullptr || right == nullptr) return false;
  for (uint8_t i = 0; i < 8; ++i) {
    if (left[i] != right[i]) return false;
  }
  return true;
}

template <size_t Count>
bool assignAddress(uint8_t (&assigned)[Count][8], size_t role,
                   const uint8_t address[8]) {
  if (role >= Count || isZeroAddress(address)) return false;

  // One physical DS18B20 can serve only one semantic role. Reassigning it
  // clears the old role exactly like the REST calibration endpoint.
  for (size_t index = 0; index < Count; ++index) {
    if (index == role || !addressesEqual(assigned[index], address)) continue;
    for (uint8_t byte = 0; byte < 8; ++byte) assigned[index][byte] = 0;
  }
  for (uint8_t byte = 0; byte < 8; ++byte) assigned[role][byte] = address[byte];
  return true;
}

template <size_t Count>
bool clearAddress(uint8_t (&assigned)[Count][8], size_t role) {
  if (role >= Count) return false;
  for (uint8_t byte = 0; byte < 8; ++byte) assigned[role][byte] = 0;
  return true;
}


// Which role already owns this address (optionally ignoring one role).
template <size_t Count>
int8_t roleOwner(const uint8_t (&assigned)[Count][8], const uint8_t address[8],
                 size_t exceptRole) {
  if (address == nullptr) return -1;
  for (size_t index = 0; index < Count; ++index) {
    if (index == exceptRole) continue;
    if (addressesEqual(assigned[index], address)) {
      return static_cast<int8_t>(index);
    }
  }
  return -1;
}

// Assign discovered addresses to empty roles in role order, never reusing an
// address that already belongs to another role. Returns how many were set.
template <size_t Count>
uint8_t autoAssign(uint8_t (&assigned)[Count][8], const uint8_t discovered[][8],
                   uint8_t discoveredCount) {
  uint8_t next = 0;
  uint8_t changes = 0;
  for (size_t role = 0; role < Count; ++role) {
    if (!isZeroAddress(assigned[role])) continue;
    while (next < discoveredCount &&
           (isZeroAddress(discovered[next]) ||
            roleOwner(assigned, discovered[next], role) >= 0)) {
      ++next;
    }
    if (next >= discoveredCount) break;
    if (assignAddress(assigned, role, discovered[next])) {
      ++changes;
      ++next;
    }
  }
  return changes;
}

} // namespace SensorCalibrationLogic
