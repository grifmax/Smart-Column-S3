import { MODE_NBK, MODE_FERMENTATION } from '../globals.js';
import { confirmModeSwitch } from './common.js';
import { loadStatus } from '../core/status.js';
import { addLog } from '../core/logs.js';

const NBK_DEFAULTS = {
    powerW: 2500,
    pumpSpeedMlH: 20000,
    columnBottomTempThresholdC: 95
};

const FERMENTATION_DEFAULTS = {
    targetTempC: 28,
    hysteresisC: 0.5,
    useHeater: true
};

function byId(id) {
    return document.getElementById(id);
}

function clampNumber(value, min, max, fallback) {
    const parsed = Number(String(value ?? '').trim().replace(',', '.'));
    if (!Number.isFinite(parsed)) return fallback;
    if (parsed < min) return min;
    if (parsed > max) return max;
    return parsed;
}

function setInputValue(id, value) {
    const el = byId(id);
    if (!el) return;
    if (el.type === 'checkbox') {
        el.checked = Boolean(value);
        return;
    }
    if (value !== undefined && value !== null) {
        el.value = String(value);
    }
}

export function collectNbkSettings() {
    return {
        powerW: clampNumber(byId('nbk-power-w')?.value, 500, 5500, NBK_DEFAULTS.powerW),
        pumpSpeedMlH: clampNumber(byId('nbk-pump-speed')?.value, 100, 120000, NBK_DEFAULTS.pumpSpeedMlH),
        columnBottomTempThresholdC: clampNumber(
            byId('nbk-column-bottom-threshold')?.value,
            50,
            110,
            NBK_DEFAULTS.columnBottomTempThresholdC
        )
    };
}

export function collectFermentationSettings() {
    return {
        targetTempC: clampNumber(byId('ferm-target-temp')?.value, 5, 45, FERMENTATION_DEFAULTS.targetTempC),
        hysteresisC: clampNumber(byId('ferm-hysteresis')?.value, 0.1, 10, FERMENTATION_DEFAULTS.hysteresisC),
        useHeater: Boolean(byId('ferm-use-heater')?.checked)
    };
}

export async function loadNbkSettings() {
    try {
        const response = await fetch('/api/settings/nbk');
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        const data = await response.json();
        const settings = {
            powerW: clampNumber(data.powerW, 500, 5500, NBK_DEFAULTS.powerW),
            pumpSpeedMlH: clampNumber(data.pumpSpeedMlH, 100, 120000, NBK_DEFAULTS.pumpSpeedMlH),
            columnBottomTempThresholdC: clampNumber(
                data.columnBottomTempThresholdC,
                50,
                110,
                NBK_DEFAULTS.columnBottomTempThresholdC
            )
        };
        setInputValue('nbk-power-w', settings.powerW);
        setInputValue('nbk-pump-speed', settings.pumpSpeedMlH);
        setInputValue('nbk-column-bottom-threshold', settings.columnBottomTempThresholdC);
        return true;
    } catch (error) {
        addLog(`Ошибка загрузки настроек НБК: ${error.message}`, 'warning');
        setInputValue('nbk-power-w', NBK_DEFAULTS.powerW);
        setInputValue('nbk-pump-speed', NBK_DEFAULTS.pumpSpeedMlH);
        setInputValue('nbk-column-bottom-threshold', NBK_DEFAULTS.columnBottomTempThresholdC);
        return false;
    }
}

export async function loadFermentationSettings() {
    try {
        const response = await fetch('/api/settings/fermentation');
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        const data = await response.json();
        const settings = {
            targetTempC: clampNumber(data.targetTempC, 5, 45, FERMENTATION_DEFAULTS.targetTempC),
            hysteresisC: clampNumber(data.hysteresisC, 0.1, 10, FERMENTATION_DEFAULTS.hysteresisC),
            useHeater: data.useHeater !== undefined ? Boolean(data.useHeater) : FERMENTATION_DEFAULTS.useHeater
        };
        setInputValue('ferm-target-temp', settings.targetTempC);
        setInputValue('ferm-hysteresis', settings.hysteresisC);
        setInputValue('ferm-use-heater', settings.useHeater);
        return true;
    } catch (error) {
        addLog(`Ошибка загрузки настроек ферментации: ${error.message}`, 'warning');
        setInputValue('ferm-target-temp', FERMENTATION_DEFAULTS.targetTempC);
        setInputValue('ferm-hysteresis', FERMENTATION_DEFAULTS.hysteresisC);
        setInputValue('ferm-use-heater', FERMENTATION_DEFAULTS.useHeater);
        return false;
    }
}

async function saveSettings(url, payload, successLabel) {
    const response = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    });

    if (!response.ok) {
        const errorText = await response.text();
        throw new Error(errorText || `HTTP ${response.status}`);
    }

    addLog(successLabel, 'info');
}

async function startMode(mode, payload, modeId, modeName, settingsUrl, saveMessage, startMessage, successMessage) {
    if (!confirmModeSwitch(modeId, modeName)) return false;

    try {
        await saveSettings(settingsUrl, payload, saveMessage);
        addLog(startMessage, 'info');

        const response = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                mode,
                params: payload
            })
        });

        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(errorText || `HTTP ${response.status}`);
        }

        const data = await response.json();
        addLog(successMessage, 'success');
        if (data.warning) addLog(`Warning: ${data.warning}`, 'warning');
        setTimeout(loadStatus, 500);
        return true;
    } catch (error) {
        addLog(`${modeName}: ${error.message}`, 'error');
        return false;
    }
}

export async function startNbk() {
    const payload = collectNbkSettings();
    return await startMode(
        'nbk',
        payload,
        MODE_NBK,
        'НБК',
        '/api/settings/nbk',
        'Настройки НБК сохранены',
        'Запуск НБК...',
        'НБК запущена'
    );
}

export async function startFermentation() {
    const payload = collectFermentationSettings();
    return await startMode(
        'fermentation',
        payload,
        MODE_FERMENTATION,
        'Ферментация',
        '/api/settings/fermentation',
        'Настройки ферментации сохранены',
        'Запуск ферментации...',
        'Ферментация запущена'
    );
}
