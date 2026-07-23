#pragma once

#include <cstdint>

// Platform-independent decisions shared by the TFT presenter and native
// tests. Hardware/FSM calls stay in their owning modules; this header only
// answers whether the HMI may expose an action.
namespace TftHmiPolicyLogic {

enum class EditPolicy : uint8_t {
  ViewOnly = 0,
  Always,
  IdleOnly,
  ManualOnly,
  ModeMonitorRuntime,
};

enum class RuntimeMode : uint8_t {
  Idle = 0,
  Rectification,
  ManualRectification,
  Distillation,
  Mashing,
  Hold,
  Nbk,
  Fermentation,
};

enum class FieldVisibility : uint8_t { Hidden = 0, ReadOnly, Editable };

inline bool isModeMonitorEditable(RuntimeMode mode) {
  return mode == RuntimeMode::Distillation ||
         mode == RuntimeMode::ManualRectification ||
         mode == RuntimeMode::Mashing || mode == RuntimeMode::Hold;
}

inline FieldVisibility resolveFieldVisibility(EditPolicy policy,
                                              bool modeRunning,
                                              RuntimeMode mode) {
  switch (policy) {
  case EditPolicy::Always:
    return FieldVisibility::Editable;
  case EditPolicy::IdleOnly:
    return modeRunning ? FieldVisibility::ReadOnly : FieldVisibility::Editable;
  case EditPolicy::ManualOnly:
    return (mode == RuntimeMode::Idle ||
            mode == RuntimeMode::ManualRectification)
               ? FieldVisibility::Editable
               : FieldVisibility::ReadOnly;
  case EditPolicy::ModeMonitorRuntime:
    return isModeMonitorEditable(mode) ? FieldVisibility::Editable
                                        : FieldVisibility::ReadOnly;
  case EditPolicy::ViewOnly:
  default:
    return FieldVisibility::ReadOnly;
  }
}

static constexpr uint32_t kDraftDirtyNone = 0;
static constexpr uint32_t kDraftDirtyProfile = 1U << 0;

struct DraftState {
  bool valid = false;
  bool profileRelevant = false;
  bool profileSelected = false;
  bool profileCompatible = false;
  bool profileChanged = false;
  uint32_t dirtyMask = kDraftDirtyNone;
};

inline bool hasUnsavedDraft(const DraftState &state) {
  return state.valid && state.dirtyMask != kDraftDirtyNone;
}

inline void refreshProfileDirty(DraftState &state) {
  if (state.profileChanged) {
    state.dirtyMask |= kDraftDirtyProfile;
  } else {
    state.dirtyMask &= ~kDraftDirtyProfile;
  }
}

inline bool maySaveDraft(const DraftState &state) {
  return state.valid &&
         (!state.profileRelevant || !state.profileSelected ||
          state.profileCompatible);
}

inline void markDraftSaved(DraftState &state) {
  state.profileChanged = false;
  state.dirtyMask = kDraftDirtyNone;
}

inline void markDraftDiscarded(DraftState &state) {
  state.profileChanged = false;
  state.dirtyMask = kDraftDirtyNone;
}

inline bool shouldBlockDraftNavigation(const DraftState &state,
                                       bool targetModeDiffers) {
  return hasUnsavedDraft(state) && targetModeDiffers;
}

enum class DraftNavigationResult : uint8_t { Allow = 0, ResolveDraft };

inline DraftNavigationResult resolveDraftNavigation(
    const DraftState &state, bool targetModeDiffers) {
  return shouldBlockDraftNavigation(state, targetModeDiffers)
             ? DraftNavigationResult::ResolveDraft
             : DraftNavigationResult::Allow;
}

// A TFT start is legal only at the end of the explicit setup route. Keep this
// gate independent from the renderer so native tests can protect it from a
// future shortcut that bypasses draft resolution or backend pre-flight.
struct StartRouteState {
  bool idle = false;
  bool draftResolved = false;
  bool preflightReady = false;
};

inline bool mayStartFromTft(const StartRouteState &state) {
  return state.idle && state.draftResolved && state.preflightReady;
}

enum class ActiveControlAction : uint8_t {
  Monitor = 0,
  ProcessParameters,
  Safety,
  Events,
  PauseResume,
  Stop,
};

inline bool mayUseActiveControlAction(bool modeRunning,
                                      ActiveControlAction) {
  return modeRunning;
}

enum class ServiceSessionEvent : uint8_t {
  EnterServiceRoot = 0,
  LeaveServiceRoot,
  SafetyEmergency,
};

inline bool transitionServiceSession(bool active, ServiceSessionEvent event) {
  switch (event) {
  case ServiceSessionEvent::EnterServiceRoot:
    return true;
  case ServiceSessionEvent::LeaveServiceRoot:
  case ServiceSessionEvent::SafetyEmergency:
  default:
    return false;
  }
}

enum class PostAlarmAction : uint8_t { None = 0, Acknowledge, Reset };

inline PostAlarmAction selectPostAlarmAction(bool alarmActive,
                                             bool acknowledged,
                                             bool resetAvailable) {
  if (!alarmActive) {
    return PostAlarmAction::None;
  }
  if (!acknowledged) {
    return PostAlarmAction::Acknowledge;
  }
  return resetAvailable ? PostAlarmAction::Reset : PostAlarmAction::None;
}

} // namespace TftHmiPolicyLogic
