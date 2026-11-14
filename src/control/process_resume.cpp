#include "process_resume.h"

#include <math.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "process_resume_logic.h"
#include "fsm.h"
#include "fsm_utils.h"
#include "storage/nvs_manager.h"

namespace ProcessResume {
namespace {

SemaphoreHandle_t g_mutex = nullptr;
PersistedSnapshot g_pending;
bool g_hasPending = false;
uint32_t g_lastSnapshotAtMs = 0;
uint8_t g_lastPhase = static_cast<uint8_t>(RectPhase::IDLE);
bool g_lastPaused = false;
bool g_wasTracking = false;

bool lock(uint32_t timeoutMs = 25) {
  return g_mutex != nullptr &&
         xSemaphoreTake(g_mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void unlock() {
  if (g_mutex != nullptr) xSemaphoreGive(g_mutex);
}

bool isRecoverable(const SystemState& state) {
  return state.mode == Mode::RECTIFICATION &&
         ProcessResumeLogic::isRecoverableRectificationPhase(
             static_cast<uint8_t>(state.rectPhase));
}

uint32_t checksum(const PersistedSnapshot& snapshot) {
  PersistedSnapshot copy = snapshot;
  copy.checksum = 0;
  return ProcessResumeLogic::crc32(
      reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

bool valid(const PersistedSnapshot& snapshot) {
  return snapshot.magic == kMagic && snapshot.version == kVersion &&
         snapshot.size == sizeof(PersistedSnapshot) &&
         snapshot.mode == static_cast<uint8_t>(Mode::RECTIFICATION) &&
         ProcessResumeLogic::isRecoverableRectificationPhase(snapshot.phase) &&
         (snapshot.recoveryActive == 0 ||
          ProcessResumeLogic::isRecoverableRectificationPhase(
              snapshot.recoveryTargetPhase)) &&
         isfinite(snapshot.headsVolumeMl) && snapshot.headsVolumeMl >= 0.0f &&
         isfinite(snapshot.bodyVolumeMl) && snapshot.bodyVolumeMl >= 0.0f &&
         isfinite(snapshot.tailsVolumeMl) && snapshot.tailsVolumeMl >= 0.0f &&
         isfinite(snapshot.headsTargetMl) && snapshot.headsTargetMl >= 0.0f &&
         isfinite(snapshot.bodyTargetMl) && snapshot.bodyTargetMl >= 0.0f &&
         isfinite(snapshot.tailsTargetMl) && snapshot.tailsTargetMl >= 0.0f &&
         isfinite(snapshot.totalVolumeMl) && snapshot.totalVolumeMl >= 0.0f &&
         isfinite(snapshot.bodyContainerVolumeMl) &&
         snapshot.bodyContainerVolumeMl >= 0.0f &&
         isfinite(snapshot.bodyContainerStartVolumeMl) &&
         snapshot.bodyContainerStartVolumeMl >= 0.0f &&
         isfinite(snapshot.recoveryHeadsTargetMl) &&
         snapshot.recoveryHeadsTargetMl >= 0.0f &&
         snapshot.recoveryHeadsTargetMl <= 100.0f &&
         isfinite(snapshot.recoveryHeadsCollectedMl) &&
         snapshot.recoveryHeadsCollectedMl >= 0.0f &&
         snapshot.recoveryHeadsCollectedMl <=
             snapshot.recoveryHeadsTargetMl + 0.01f &&
         isfinite(snapshot.recoveryHeadsBaseMl) &&
         snapshot.recoveryHeadsBaseMl >= 0.0f &&
         checksum(snapshot) == snapshot.checksum;
}

PersistedSnapshot makeSnapshot(const SystemState& state,
                               const Settings& settings,
                               uint32_t sequence) {
  PersistedSnapshot snapshot{};
  snapshot.sequence = sequence;
  snapshot.mode = static_cast<uint8_t>(state.mode);
  snapshot.phase = static_cast<uint8_t>(state.rectPhase);
  snapshot.paused = state.paused ? 1U : 0U;
  snapshot.bodyContainerIndex = state.rectBodyContainerIndex;
  snapshot.bodyContainerLevelReached = state.rectBodyContainerLevelReached ? 1U : 0U;
  snapshot.headsContainerLevelReached = state.rectHeadsContainerLevelReached ? 1U : 0U;
  snapshot.bodyContainerChangePending = state.rectBodyContainerChangePending ? 1U : 0U;
  snapshot.recoveryActive = state.rectResume.active ? 1U : 0U;
  snapshot.recoveryTargetPhase = static_cast<uint8_t>(state.rectResume.targetPhase);
  snapshot.headsVolumeMl = state.stats.headsVolume;
  snapshot.bodyVolumeMl = state.stats.bodyVolume;
  snapshot.tailsVolumeMl = state.stats.tailsVolume;
  FSM::Rectification::getTargets(snapshot.headsTargetMl,
                                 snapshot.bodyTargetMl,
                                 snapshot.tailsTargetMl);
  snapshot.totalVolumeMl = state.pump.totalVolumeMl;
  snapshot.bodyContainerVolumeMl = state.rectBodyContainerVolumeMl;
  snapshot.bodyContainerStartVolumeMl = state.rectBodyContainerStartVolumeMl;
  snapshot.recoveryHeadsTargetMl = state.rectResume.extraHeadsTargetMl;
  snapshot.recoveryHeadsCollectedMl = state.rectResume.extraHeadsCollectedMl;
  snapshot.recoveryHeadsBaseMl = state.rectResume.headsBeforeRecoveryMl;
  snapshot.cubeVolumeL = settings.equipment.cubeVolumeL;
  snapshot.rectParams = settings.rectParams;

  // The pump/valve feedback is synchronized after the FSM tick, while phase
  // statistics are normally updated inside that tick. Reconcile the active
  // fraction against the same total-volume sample so a reboot cannot repeat
  // the small amount collected between those two updates.
  const float phaseCollected = fmaxf(
      0.0f, snapshot.totalVolumeMl - FSM::getPhaseStartVolumeMl());
  if (state.rectPhase == RectPhase::HEADS) {
    if (state.rectResume.active &&
        state.rectResume.targetPhase == RectPhase::BODY &&
        state.rectResume.extraHeadsTargetMl > 0.0f) {
      snapshot.recoveryHeadsCollectedMl = fminf(
          phaseCollected, state.rectResume.extraHeadsTargetMl);
      snapshot.headsVolumeMl =
          state.rectResume.headsBeforeRecoveryMl + phaseCollected;
    } else {
      snapshot.headsVolumeMl = phaseCollected;
    }
  } else if (state.rectPhase == RectPhase::BODY) {
    snapshot.bodyVolumeMl = phaseCollected;
    snapshot.bodyContainerVolumeMl = fmaxf(
        0.0f, phaseCollected - snapshot.bodyContainerStartVolumeMl);
  } else if (state.rectPhase == RectPhase::TAILS) {
    snapshot.tailsVolumeMl = phaseCollected;
  }
  snapshot.checksum = checksum(snapshot);
  return snapshot;
}

}  // namespace

bool init() {
  if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
  if (g_mutex == nullptr) return false;

  PersistedSnapshot first;
  PersistedSnapshot second;
  const bool firstValid = NVSManager::loadProcessResumeSlot(0, first) && valid(first);
  const bool secondValid = NVSManager::loadProcessResumeSlot(1, second) && valid(second);
  if (!firstValid && !secondValid) return true;

  const PersistedSnapshot& selected =
      !firstValid ? second : !secondValid ? first
      : first.sequence >= second.sequence ? first : second;
  if (lock()) {
    g_pending = selected;
    g_hasPending = true;
    unlock();
  }
  LOG_W("Resume: pending rectification snapshot phase=%u sequence=%lu",
        selected.phase, static_cast<unsigned long>(selected.sequence));
  return true;
}

bool hasPending() {
  if (!lock()) return false;
  const bool pending = g_hasPending;
  unlock();
  return pending;
}

bool getPending(PersistedSnapshot& out) {
  if (!lock()) return false;
  const bool available = g_hasPending;
  if (available) out = g_pending;
  unlock();
  return available;
}

bool saveNow(const SystemState& state, const Settings& settings) {
  if (!isRecoverable(state)) return false;

  PersistedSnapshot previous;
  const bool hadPrevious = getPending(previous);
  const uint32_t sequence = hadPrevious ? previous.sequence + 1U : 1U;
  const PersistedSnapshot snapshot = makeSnapshot(state, settings, sequence);
  const uint8_t slot = static_cast<uint8_t>(sequence & 1U);
  if (!NVSManager::saveProcessResumeSlot(slot, snapshot)) {
    LOG_E("Resume: failed to persist snapshot");
    return false;
  }

  if (lock()) {
    g_pending = snapshot;
    g_hasPending = true;
    unlock();
  }
  g_lastSnapshotAtMs = millis();
  g_lastPhase = snapshot.phase;
  g_lastPaused = snapshot.paused != 0;
  g_wasTracking = true;
  return true;
}

void update(const SystemState& state, const Settings& settings) {
  if (!isRecoverable(state)) {
    if (g_wasTracking) clear();
    return;
  }

  const uint32_t now = millis();
  const uint8_t phase = static_cast<uint8_t>(state.rectPhase);
  const bool phaseChanged = !g_wasTracking || phase != g_lastPhase ||
                            state.paused != g_lastPaused;
  if (phaseChanged || now - g_lastSnapshotAtMs >=
                          ProcessResumeLogic::kSnapshotIntervalMs) {
    saveNow(state, settings);
  }
}

void clear() {
  NVSManager::clearProcessResumeSlots();
  if (lock()) {
    g_pending = PersistedSnapshot{};
    g_hasPending = false;
    unlock();
  }
  g_lastSnapshotAtMs = 0;
  g_lastPhase = static_cast<uint8_t>(RectPhase::IDLE);
  g_lastPaused = false;
  g_wasTracking = false;
}

float suggestedRecoveryHeadsMl(const PersistedSnapshot& snapshot) {
  const float feedVolumeL = snapshot.rectParams.feedVolumeL > 0.1f
                                ? snapshot.rectParams.feedVolumeL
                                : snapshot.cubeVolumeL;
  return ProcessResumeLogic::suggestedRecoveryHeadsMl(
      feedVolumeL, snapshot.cubeVolumeL);
}

bool shouldOfferRecoveryHeads(const PersistedSnapshot& snapshot) {
  return ProcessResumeLogic::shouldOfferRecoveryHeads(
      snapshot.phase, snapshot.headsVolumeMl,
      snapshot.recoveryActive != 0, snapshot.recoveryTargetPhase,
      snapshot.recoveryHeadsBaseMl);
}

}  // namespace ProcessResume
