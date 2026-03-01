// ============================================================================
// Profiles - State Management
// ============================================================================

export let currentProfileId = null; // ID профиля для просмотра/редактирования

export function setCurrentProfileId(id) {
    currentProfileId = id;
}
