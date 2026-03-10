import { addLog } from '../core/logs.js';
import { runtimeMonitorState } from '../globals.js';

const DEFAULTS = {
    pressureMaxMmHg: 50,
    tsaMaxC: 55,
    waterOutMaxC: 70,
    waterOutRiseRateCMin: 8,
    pressureRiseRateMmHgMin: 20,
    minHeaterSubmergeL: 7.5
};

function byId(id) {
    return document.getElementById(id);
}

function toFinite(value, fallback = NaN) {
    const normalized = String(value ?? '').trim().replace(',', '.');
    const parsed = Number(normalized);
    return Number.isFinite(parsed) ? parsed : fallback;
}

function clamp(value, min, max, fallback) {
    const parsed = toFinite(value, fallback);
    if (!Number.isFinite(parsed)) return fallback;
    if (parsed < min) return min;
    if (parsed > max) return max;
    return parsed;
}

function setInputValue(id, value, digits = 1) {
    const el = byId(id);
    if (!el || !Number.isFinite(value)) return;
    el.value = Number(value).toFixed(digits);
}

function getInputValue(id, min, max, fallback) {
    return clamp(byId(id)?.value, min, max, fallback);
}

function updateRuntimeState(snapshot) {
    runtimeMonitorState.safetySettings = {
        ...runtimeMonitorState.safetySettings,
        pressureMaxMmHg: snapshot.pressureMaxMmHg,
        tsaMaxC: snapshot.tsaMaxC,
        waterOutMaxC: snapshot.waterOutMaxC,
        waterOutRiseRateCMin: snapshot.waterOutRiseRateCMin,
        pressureRiseRateMmHgMin: snapshot.pressureRiseRateMmHgMin
    };

    runtimeMonitorState.equipment = {
        ...runtimeMonitorState.equipment,
        minHeaterSubmergeL: snapshot.minHeaterSubmergeL
    };
}

export async function loadSafetySettings() {
    const safetyRequest = fetch('/api/settings/safety').catch(() => null);
    const equipmentRequest = fetch('/api/settings/equipment').catch(() => null);

    const [safetyResponse, equipmentResponse] = await Promise.all([safetyRequest, equipmentRequest]);

    const nextValues = {
        ...DEFAULTS
    };

    try {
        if (safetyResponse && safetyResponse.ok) {
            const safety = await safetyResponse.json();
            nextValues.pressureMaxMmHg = clamp(safety.pressureMaxMmHg, 5, 200, DEFAULTS.pressureMaxMmHg);
            nextValues.tsaMaxC = clamp(safety.tsaMaxC, 35, 120, DEFAULTS.tsaMaxC);
            nextValues.waterOutMaxC = clamp(safety.waterOutMaxC, 30, 120, DEFAULTS.waterOutMaxC);
            nextValues.waterOutRiseRateCMin = clamp(safety.waterOutRiseRateCMin, 0.5, 60, DEFAULTS.waterOutRiseRateCMin);
            nextValues.pressureRiseRateMmHgMin = clamp(safety.pressureRiseRateMmHgMin, 1, 200, DEFAULTS.pressureRiseRateMmHgMin);
        }

        if (equipmentResponse && equipmentResponse.ok) {
            const equipment = await equipmentResponse.json();
            nextValues.minHeaterSubmergeL = clamp(
                equipment.minHeaterSubmergeL,
                0.5,
                100,
                DEFAULTS.minHeaterSubmergeL
            );
        }

        setInputValue('safety-pressure-max', nextValues.pressureMaxMmHg, 1);
        setInputValue('safety-tsa-max', nextValues.tsaMaxC, 1);
        setInputValue('safety-water-out-max', nextValues.waterOutMaxC, 1);
        setInputValue('safety-water-rise-rate', nextValues.waterOutRiseRateCMin, 1);
        setInputValue('safety-pressure-rise-rate', nextValues.pressureRiseRateMmHgMin, 1);
        setInputValue('safety-min-heater-submerge-l', nextValues.minHeaterSubmergeL, 1);

        updateRuntimeState(nextValues);
    } catch (error) {
        addLog(`? Ошибка загрузки настроек безопасности: ${error.message}`, 'error');
    }
}

export async function saveSafetySettings() {
    const payloadSafety = {
        pressureMaxMmHg: getInputValue('safety-pressure-max', 5, 200, DEFAULTS.pressureMaxMmHg),
        tsaMaxC: getInputValue('safety-tsa-max', 35, 120, DEFAULTS.tsaMaxC),
        waterOutMaxC: getInputValue('safety-water-out-max', 30, 120, DEFAULTS.waterOutMaxC),
        waterOutRiseRateCMin: getInputValue('safety-water-rise-rate', 0.5, 60, DEFAULTS.waterOutRiseRateCMin),
        pressureRiseRateMmHgMin: getInputValue('safety-pressure-rise-rate', 1, 200, DEFAULTS.pressureRiseRateMmHgMin)
    };

    const payloadEquipment = {
        minHeaterSubmergeL: getInputValue('safety-min-heater-submerge-l', 0.5, 100, DEFAULTS.minHeaterSubmergeL)
    };

    try {
        const [safetyResp, equipmentResp] = await Promise.all([
            fetch('/api/settings/safety', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payloadSafety)
            }),
            fetch('/api/settings/equipment', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payloadEquipment)
            })
        ]);

        if (!safetyResp.ok || !equipmentResp.ok) {
            const safeMsg = !safetyResp.ok ? `safety=${safetyResp.status}` : '';
            const equipMsg = !equipmentResp.ok ? `equipment=${equipmentResp.status}` : '';
            throw new Error([safeMsg, equipMsg].filter(Boolean).join(', '));
        }

        updateRuntimeState({
            ...payloadSafety,
            ...payloadEquipment
        });

        addLog('?? Настройки аварийной безопасности сохранены', 'success');
    } catch (error) {
        addLog(`? Ошибка сохранения безопасности: ${error.message}`, 'error');
    }
}
