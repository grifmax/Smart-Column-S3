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



        endTemp: clampDistillationInput(document.getElementById('dist-start-end-temp')?.value, 70, 110, 96),
        powerPercent: clampDistillationInput(document.getElementById('dist-start-power-percent')?.value, 0, 100, 60)
    };
}

export async function startDistillation(paramsOverride = null) {
    if (!confirmModeSwitch(MODE_DIST, 'Distillation')) return false;

    const params = paramsOverride || collectDistillationSettings();

    try {
        addLog('Отправка команды запуска дистилляции...', 'info');

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
            addLog('Дистилляция запущена', 'success');
            if (data.warning) addLog(`Предупреждение: ${data.warning}`, 'warning');
            setTimeout(loadStatus, 500);
            return true;
        } else {
            const error = await response.text();
            addLog(`Ошибка запуска дистилляции (${response.status}): ${error}`, 'error');
            return false;
        }
    } catch (e) {
        addLog(`Сетевая ошибка дистилляции: ${e.message}`, 'error');
        console.error('Start distillation error:', e);
        return false;
    }
}
