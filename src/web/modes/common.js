// ============================================================================

// Control Functions

// ============================================================================



export function confirmModeSwitch(targetModeId, targetModeName) {

    const targetLabel = targetModeName || getModeLabel(targetModeId);

    if (currentMode === MODE_IDLE) return true;

    if (currentMode === targetModeId) {
        addLog(`Mode "${targetLabel}" is already running`, 'warning');
        return false;
    }

    const currentModeLabel = getModeLabel(currentMode);

    return confirm(
        `Current mode "${currentModeLabel}" is running.\\n` +
        `Switch to "${targetLabel}"?\\n\\n` +
        `Current process will be stopped.`
    );

}
