import { MODE_DIST } from '../globals.js';
import { confirmModeSwitch } from './common.js';
import { loadStatus } from '../core/status.js';
import { addLog } from '../core/logs.js';

function clampDistillationInput(value, min, max, fallback) {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) return fallback;
    if (parsed < min) return min;
    if (parsed > max) return max;
    return parsed;
}

export function collectDistillationSettings() {
    return {
        speed: clampDistillationInput(document.getElementById('dist-start-speed')?.value, 10, 6000, 500),
        headsVolume: clampDistillationInput(document.getElementById('dist-start-heads-volume')?.value, 0, 3000, 0),
        targetVolume: clampDistillationInput(document.getElementById('dist-start-target-volume')?.value, 0, 50000, 0),
        endTemp: clampDistillationInput(document.getElementById('dist-start-end-temp')?.value, 70, 110, 96),
        powerPercent: clampDistillationInput(document.getElementById('dist-start-power-percent')?.value, 0, 100, 60)
    };
}

export async function startDistillation(paramsOverride = null) {
    if (!confirmModeSwitch(MODE_DIST, 'Distillation')) return false;

    const params = paramsOverride || collectDistillationSettings();

    try {
        addLog('Sending distillation start command...', 'info');

        const response = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                mode: 'distillation',
                params
            })
        });

        if (response.ok) {
            const data = await response.json();
            addLog('Distillation started', 'success');
            if (data.warning) addLog(`Warning: ${data.warning}`, 'warning');
            setTimeout(loadStatus, 500);
            return true;
        } else {
            const error = await response.text();
            addLog(`Distillation start error (${response.status}): ${error}`, 'error');
            return false;
        }
    } catch (e) {
        addLog(`Distillation network error: ${e.message}`, 'error');
        console.error('Start distillation error:', e);
        return false;
    }
}
