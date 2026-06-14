import {
    currentMode,
    MODE_IDLE,
    getModeLabel,
    runtimeMonitorState
} from '../globals.js';
import { addLog } from '../core/logs.js';

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

function clampBoosterStopCubeTemp(value, fallback = 78) {
    const parsed = Number(String(value ?? '').trim().replace(',', '.'));
    if (!Number.isFinite(parsed)) return fallback;
    if (parsed < 20) return 20;
    if (parsed > 100) return 100;
    return parsed;
}

export function readBoosterStartSettings(fieldIds, fallback = {}) {
    return {
        boosterEnabled: Boolean(
            document.getElementById(fieldIds.enabled)?.checked ??
            fallback.boosterEnabled ??
            false
        ),
        boosterStopCubeTempC: clampBoosterStopCubeTemp(
            document.getElementById(fieldIds.stopTemp)?.value,
            fallback.boosterStopCubeTempC ?? 78
        )
    };
}

export function applyBoosterStartSettings(fieldIds, settings = {}) {
    const enabledEl = document.getElementById(fieldIds.enabled);
    if (enabledEl) {
        enabledEl.checked = Boolean(settings.boosterEnabled);
    }

    const stopTempEl = document.getElementById(fieldIds.stopTemp);
    if (stopTempEl) {
        stopTempEl.value = String(
            clampBoosterStopCubeTemp(settings.boosterStopCubeTempC, 78)
        );
    }
}

export async function loadBoosterStartSettings(fieldIds) {
    const runtimeEquipment = runtimeMonitorState?.equipment || {};
    const fallback = {
        boosterEnabled: Boolean(runtimeEquipment.boosterHeaterEnabled),
        boosterStopCubeTempC: clampBoosterStopCubeTemp(
            runtimeEquipment.boosterHeaterStopCubeTempC,
            78
        )
    };

    try {
        const response = await fetch('/api/settings/equipment');
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        const data = await response.json();
        const resolved = {
            boosterEnabled: Boolean(
                data.boosterHeaterEnabled ?? fallback.boosterEnabled
            ),
            boosterStopCubeTempC: clampBoosterStopCubeTemp(
                data.boosterHeaterStopCubeTempC,
                fallback.boosterStopCubeTempC
            )
        };

        runtimeMonitorState.equipment = {
            ...runtimeMonitorState.equipment,
            boosterHeaterEnabled: resolved.boosterEnabled,
            boosterHeaterStopCubeTempC: resolved.boosterStopCubeTempC
        };

        applyBoosterStartSettings(fieldIds, resolved);
        return resolved;
    } catch (error) {
        applyBoosterStartSettings(fieldIds, fallback);
        return fallback;
    }
}
