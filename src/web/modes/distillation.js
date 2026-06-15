import { MODE_DIST, maxHeaterPower } from '../globals.js';
import {
    confirmModeSwitch,
    readBoosterStartSettings,
    loadBoosterStartSettings
} from './common.js';
import { loadStatus } from '../core/status.js';
import { addLog } from '../core/logs.js';

const DIST_BOOSTER_FIELD_IDS = {
    enabled: 'dist-start-booster-enabled',
    stopTemp: 'dist-start-booster-stop-cube-temp'
};

function clampDistillationInput(value, min, max, fallback) {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) return fallback;
    if (parsed < min) return min;
    if (parsed > max) return max;
    return parsed;
}

export function collectDistillationSettings() {
    const heaterMax = Math.max(1, Number(maxHeaterPower) || 3000);
    const powerW = clampDistillationInput(
        document.getElementById('dist-start-power-percent')?.value,
        0,
        heaterMax,
        Math.round(heaterMax * 0.6)
    );
    return {
        endTemp: clampDistillationInput(document.getElementById('dist-start-end-temp')?.value, 70, 110, 96),
        powerW,
        powerPercent: Math.min(100, Math.max(0, Math.round((powerW / heaterMax) * 100))),
        ...readBoosterStartSettings(DIST_BOOSTER_FIELD_IDS)
    };
}

export async function loadDistillationStartSettings() {
    try {
        await loadBoosterStartSettings(DIST_BOOSTER_FIELD_IDS);
        return true;
    } catch (error) {
        addLog(`Ошибка загрузки booster-настроек дистилляции: ${error.message}`, 'warning');
        return false;
    }
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
        }

        const error = await response.text();
        addLog(`Ошибка запуска дистилляции (${response.status}): ${error}`, 'error');
        return false;
    } catch (e) {
        addLog(`Сетевая ошибка дистилляции: ${e.message}`, 'error');
        console.error('Start distillation error:', e);
        return false;
    }
}
