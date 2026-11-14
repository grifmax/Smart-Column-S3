#pragma once

#include "types.h"

namespace ProcessResume {

constexpr uint32_t kMagic = 0x53435253UL;  // "SCRS"
constexpr uint16_t kVersion = 2;

// The two NVS slots each store one of these records. The record is bounded and
// replaces its previous value; it is not a process history.
struct PersistedSnapshot {
  uint32_t magic = kMagic;
  uint16_t version = kVersion;
  uint16_t size = sizeof(PersistedSnapshot);
  uint32_t sequence = 0;
  uint32_t checksum = 0;
  uint8_t mode = static_cast<uint8_t>(Mode::IDLE);
  uint8_t phase = static_cast<uint8_t>(RectPhase::IDLE);
  uint8_t paused = 0;
  uint8_t bodyContainerIndex = 0;
  uint8_t bodyContainerLevelReached = 0;
  uint8_t headsContainerLevelReached = 0;
  uint8_t bodyContainerChangePending = 0;
  uint8_t recoveryActive = 0;
  uint8_t recoveryTargetPhase = static_cast<uint8_t>(RectPhase::IDLE);
  uint8_t reserved[3] = {};
  float headsVolumeMl = 0.0f;
  float bodyVolumeMl = 0.0f;
  float tailsVolumeMl = 0.0f;
  float headsTargetMl = 0.0f;
  float bodyTargetMl = 0.0f;
  float tailsTargetMl = 0.0f;
  float totalVolumeMl = 0.0f;
  float bodyContainerVolumeMl = 0.0f;
  float bodyContainerStartVolumeMl = 0.0f;
  float recoveryHeadsTargetMl = 0.0f;
  float recoveryHeadsCollectedMl = 0.0f;
  float recoveryHeadsBaseMl = 0.0f;
  float cubeVolumeL = 0.0f;
  RectParams rectParams{};
};

static_assert(sizeof(PersistedSnapshot) <= 512,
              "Rectification resume snapshot must remain compact");

bool init();
bool hasPending();
bool getPending(PersistedSnapshot& out);
bool saveNow(const SystemState& state, const Settings& settings);
void update(const SystemState& state, const Settings& settings);
void clear();
float suggestedRecoveryHeadsMl(const PersistedSnapshot& snapshot);
bool shouldOfferRecoveryHeads(const PersistedSnapshot& snapshot);

}  // namespace ProcessResume
