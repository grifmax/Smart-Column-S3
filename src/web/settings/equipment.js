import { addLog } from '../core/logs.js';
import { runtimeMonitorState, DEFAULT_CUBE_VOLUME_L } from '../globals.js';
import { syncRectificationFeedVolumeLimit } from '../modes/rectification.js';
import { syncManualFeedVolumeLimit } from '../modes/control-panel.js';
import { initEquipmentNumberSteppers } from './number-stepper.js';

const CUBE_EXTENDER_PRESET_STORAGE_KEY = 'equipment.cubeExtenderPresetL';

function toFiniteNumber(value, fallback = 0) {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : fallback;
}

function parseLocalizedNumber(value, fallback = NaN) {
    if (typeof value === 'number') {
        return Number.isFinite(value) ? value : fallback;
    }
    if (typeof value !== 'string') {
        return fallback;
    }

    const normalized = value.trim().replace(',', '.');
    if (!normalized) return fallback;
    return toFiniteNumber(normalized, fallback);
}

function clamp(value, min, max, fallback = min) {
    const parsed = toFiniteNumber(value, fallback);
    if (parsed < min) return min;
    if (parsed > max) return max;
    return parsed;
}

function setInputValue(id, value) {
    const el = document.getElementById(id);
    if (el && value !== undefined && value !== null) {
        el.value = String(value);
    }
}

function getInputValue(id, fallback = 0) {
    return parseLocalizedNumber(document.getElementById(id)?.value, fallback);
}

function syncFeedVolumeLimits() {
    syncRectificationFeedVolumeLimit();
    syncManualFeedVolumeLimit();
}

function updateCubeVolumeHint(options = {}) {
    const { normalizeInput = false } = options;
    const cubeVolumeInput = document.getElementById('cube-volume-l');
    const cubeVolumeRaw = parseLocalizedNumber(
        cubeVolumeInput?.value,
        runtimeMonitorState?.equipment?.cubeVolumeL ?? DEFAULT_CUBE_VOLUME_L
    );
    const cubeVolume = clamp(cubeVolumeRaw, 5, 250, DEFAULT_CUBE_VOLUME_L);

    const cubeHint = document.getElementById('cube-volume-hint');
    if (cubeHint) {
        cubeHint.textContent = `Лимит объема сырца: ${cubeVolume.toFixed(1)} л`;
    }

    if (cubeVolumeInput && normalizeInput) {
        cubeVolumeInput.value = cubeVolume.toFixed(1);
    }

    runtimeMonitorState.equipment = {
        ...runtimeMonitorState.equipment,
        cubeVolumeL: cubeVolume
    };
    syncFeedVolumeLimits();
}

function loadCubeExtenderPreset() {
    const extenderInput = document.getElementById('cube-extender-add-l');
    if (!extenderInput) return;

    try {
        const saved = localStorage.getItem(CUBE_EXTENDER_PRESET_STORAGE_KEY);
        if (saved !== null) {
            const value = clamp(saved, 0.1, 100, DEFAULT_CUBE_VOLUME_L);
            extenderInput.value = value.toFixed(1);
            return;
        }
    } catch {
        // ignore storage failures
    }

    extenderInput.value = DEFAULT_CUBE_VOLUME_L.toFixed(1);
}

function saveCubeExtenderPreset(value) {
    try {
        localStorage.setItem(CUBE_EXTENDER_PRESET_STORAGE_KEY, String(value));
    } catch {
        // ignore storage failures
    }
}

export function addCubeExtenderVolume() {
    const cubeInput = document.getElementById('cube-volume-l');
    const extenderInput = document.getElementById('cube-extender-add-l');
    if (!cubeInput || !extenderInput) return;

    const currentCubeVolume = clamp(cubeInput.value, 5, 250, DEFAULT_CUBE_VOLUME_L);
    const extenderVolume = clamp(extenderInput.value, 0.1, 100, DEFAULT_CUBE_VOLUME_L);
    const nextCubeVolume = clamp(currentCubeVolume + extenderVolume, 5, 250, currentCubeVolume);

    cubeInput.value = nextCubeVolume.toFixed(1);
    extenderInput.value = extenderVolume.toFixed(1);
    saveCubeExtenderPreset(extenderVolume.toFixed(1));
    updateCubeVolumeHint({ normalizeInput: true });
}

export async function loadEquipmentSettings() {
    try {
        const response = await fetch('/api/settings/equipment');
        if (!response.ok) throw new Error(`HTTP ${response.status}`);

        const data = await response.json();
        setInputValue('heater-power-w', clamp(data.heaterPowerW, 1000, 10000, 3000));
        setInputValue('column-height', clamp(data.columnHeightMm, 500, 3000, 1500));
        setInputValue('cube-volume-l', clamp(data.cubeVolumeL, 5, 250, DEFAULT_CUBE_VOLUME_L).toFixed(1));
        setInputValue('min-heater-submerge-l', clamp(data.minHeaterSubmergeL, 0.5, 100, 7.5).toFixed(1));
        setInputValue('water-autostart-cube-temp', clamp(data.waterAutoStartCubeTempC, 20, 60, 45).toFixed(1));

        runtimeMonitorState.equipment = {
            ...runtimeMonitorState.equipment,
            heaterPowerW: clamp(data.heaterPowerW, 1000, 10000, 3000),
            columnHeightMm: clamp(data.columnHeightMm, 500, 3000, 1500),
            cubeVolumeL: clamp(data.cubeVolumeL, 5, 250, DEFAULT_CUBE_VOLUME_L),
            minHeaterSubmergeL: clamp(data.minHeaterSubmergeL, 0.5, 100, 7.5),
            waterAutoStartCubeTempC: clamp(data.waterAutoStartCubeTempC, 20, 60, 45)
        };

        initEquipmentNumberSteppers();
        loadCubeExtenderPreset();
        updateCubeVolumeHint({ normalizeInput: true });
    } catch (error) {
        addLog(`✗ Ошибка загрузки настроек оборудования: ${error.message}`, 'error');
        initEquipmentNumberSteppers();
        loadCubeExtenderPreset();
        updateCubeVolumeHint({ normalizeInput: true });
    }
}

export async function saveEquipment() {
    const heaterPower = clamp(getInputValue('heater-power-w', 3000), 1000, 10000, 3000);
    const columnHeight = clamp(getInputValue('column-height', 1500), 500, 3000, 1500);
    const cubeVolume = clamp(getInputValue('cube-volume-l', DEFAULT_CUBE_VOLUME_L), 5, 250, DEFAULT_CUBE_VOLUME_L);
    const minHeaterSubmergeL = clamp(getInputValue('min-heater-submerge-l', 7.5), 0.5, 100, 7.5);
    const waterAutoStartCubeTempC = clamp(getInputValue('water-autostart-cube-temp', 45), 20, 60, 45);

    const mlPerRev = toFiniteNumber(document.getElementById('pump-ml-per-rev')?.value, NaN);
    const stepsPerRev = toFiniteNumber(document.getElementById('pump-steps-per-rev')?.value, NaN);

    if ((Number.isFinite(mlPerRev) && mlPerRev > 0) || (Number.isFinite(stepsPerRev) && stepsPerRev > 0)) {
        try {
            const pumpData = {};
            if (Number.isFinite(mlPerRev) && mlPerRev > 0) pumpData.mlPerRev = mlPerRev;
            if (Number.isFinite(stepsPerRev) && stepsPerRev > 0) pumpData.stepsPerRev = Math.round(stepsPerRev);

            const pumpResponse = await fetch('/api/calibration/pump', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(pumpData)
            });

            if (pumpResponse.ok) {
                addLog('✓ Параметры насоса сохранены', 'success');
            } else {
                addLog('✗ Ошибка сохранения параметров насоса', 'error');
            }
        } catch {
            addLog('✗ Ошибка соединения при сохранении насоса', 'error');
        }
    }

    try {
        const response = await fetch('/api/settings/equipment', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                heaterPowerW: Math.round(heaterPower),
                columnHeightMm: Math.round(columnHeight),
                cubeVolumeL: cubeVolume,
                minHeaterSubmergeL,
                waterAutoStartCubeTempC
            })
        });

        if (!response.ok) {
            const errText = await response.text();
            addLog(`✗ Ошибка сохранения оборудования (${response.status}): ${errText}`, 'error');
            return;
        }

        runtimeMonitorState.equipment = {
            ...runtimeMonitorState.equipment,
            heaterPowerW: Math.round(heaterPower),
            columnHeightMm: Math.round(columnHeight),
            cubeVolumeL: cubeVolume,
            minHeaterSubmergeL,
            waterAutoStartCubeTempC
        };

        updateCubeVolumeHint({ normalizeInput: true });
        addLog('💾 Настройки оборудования сохранены', 'success');
    } catch (error) {
        addLog(`✗ Ошибка сети при сохранении оборудования: ${error.message}`, 'error');
    }
}

export function initEquipmentSettingsUi() {
    initEquipmentNumberSteppers();

    const cubeVolumeInput = document.getElementById('cube-volume-l');
    if (cubeVolumeInput) {
        cubeVolumeInput.addEventListener('input', () => updateCubeVolumeHint());
        cubeVolumeInput.addEventListener('change', () => updateCubeVolumeHint({ normalizeInput: true }));
        cubeVolumeInput.addEventListener('blur', () => updateCubeVolumeHint({ normalizeInput: true }));
    }

    const extenderInput = document.getElementById('cube-extender-add-l');
    if (extenderInput) {
        extenderInput.addEventListener('change', () => {
            const value = clamp(extenderInput.value, 0.1, 100, DEFAULT_CUBE_VOLUME_L);
            extenderInput.value = value.toFixed(1);
            saveCubeExtenderPreset(value.toFixed(1));
        });
    }

    loadCubeExtenderPreset();
    updateCubeVolumeHint({ normalizeInput: true });
}
