import { addLog } from '../core/logs.js';
import { runtimeMonitorState, DEFAULT_CUBE_VOLUME_L, currentMode, currentPaused, MODE_IDLE } from '../globals.js';
import { syncRectificationFeedVolumeLimit } from '../modes/rectification.js';
import { syncManualFeedVolumeLimit } from '../modes/control-panel.js';
import { initEquipmentNumberSteppers } from './number-stepper.js';

const CUBE_EXTENDER_PRESET_STORAGE_KEY = 'equipment.cubeExtenderPresetL';
const DEFAULT_STIRRER_SETTINGS = Object.freeze({
    enabled: false,
    defaultSpeedPercent: 50,
    autoMashing: true,
    autoFermentation: false,
    autoNbk: false
});

function toFiniteNumber(value, fallback = 0) {
    const parsed = Number(String(value ?? '').trim().replace(',', '.'));
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

function setCheckboxValue(id, checked) {
    const el = document.getElementById(id);
    if (el) {
        el.checked = Boolean(checked);
    }
}

function setTextValue(id, value) {
    const el = document.getElementById(id);
    if (el && value !== undefined && value !== null) {
        el.textContent = String(value);
    }
}

function setBadgeState(id, text, tone) {
    const el = document.getElementById(id);
    if (!el) return;
    el.textContent = text;
    el.className = `equipment-status-badge ${tone}`;
}

function getInputValue(id, fallback = 0) {
    return parseLocalizedNumber(document.getElementById(id)?.value, fallback);
}

function getCheckboxValue(id, fallback = false) {
    const el = document.getElementById(id);
    return el ? Boolean(el.checked) : fallback;
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
            const value = clamp(saved, 1, 100, DEFAULT_CUBE_VOLUME_L);
            extenderInput.value = value.toFixed(0);
            return;
        }
    } catch {
        // ignore storage failures
    }

    extenderInput.value = DEFAULT_CUBE_VOLUME_L.toFixed(0);
}

function saveCubeExtenderPreset(value) {
    try {
        localStorage.setItem(CUBE_EXTENDER_PRESET_STORAGE_KEY, String(value));
    } catch {
        // ignore storage failures
    }
}

function requestJson(url, options = {}) {
    return fetch(url, options).then(async (response) => {
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) {
            const message = payload?.message || payload?.error || `HTTP ${response.status}`;
            throw new Error(message);
        }
        return payload;
    });
}

function extractStirrerSettingsSource(payload) {
    if (payload?.settings && typeof payload.settings === 'object') {
        return payload.settings;
    }
    return payload && typeof payload === 'object' ? payload : {};
}

function extractStirrerStateSource(payload) {
    if (payload?.stirrer && typeof payload.stirrer === 'object') {
        return payload.stirrer;
    }
    return payload && typeof payload === 'object' ? payload : {};
}

function updateStirrerSettingsState(payload = {}) {
    const source = extractStirrerSettingsSource(payload);
    runtimeMonitorState.stirrerSettings = {
        ...runtimeMonitorState.stirrerSettings,
        enabled: source.enabled !== undefined ? Boolean(source.enabled) : runtimeMonitorState.stirrerSettings.enabled,
        defaultSpeedPercent: clamp(
            source.defaultSpeedPercent,
            1,
            100,
            runtimeMonitorState.stirrerSettings.defaultSpeedPercent ?? DEFAULT_STIRRER_SETTINGS.defaultSpeedPercent
        ),
        autoMashing: source.autoMashing !== undefined
            ? Boolean(source.autoMashing)
            : runtimeMonitorState.stirrerSettings.autoMashing,
        autoFermentation: source.autoFermentation !== undefined
            ? Boolean(source.autoFermentation)
            : runtimeMonitorState.stirrerSettings.autoFermentation,
        autoNbk: source.autoNbk !== undefined
            ? Boolean(source.autoNbk)
            : runtimeMonitorState.stirrerSettings.autoNbk
    };
}

function updateStirrerState(payload = {}) {
    const source = extractStirrerStateSource(payload);
    const current = runtimeMonitorState.stirrer || {};
    runtimeMonitorState.stirrer = {
        ...current,
        running: source.running !== undefined ? Boolean(source.running) : current.running,
        speedPercent: clamp(source.speedPercent ?? source.speed, 0, 100, current.speedPercent ?? 0),
        available: source.available !== undefined ? Boolean(source.available) : current.available,
        autoMode: source.autoMode !== undefined ? Boolean(source.autoMode) : current.autoMode,
        lastUpdate: toFiniteNumber(source.lastUpdate, current.lastUpdate ?? 0)
    };
}

function getEnabledAutoModes(settings) {
    const modes = [];
    if (settings.autoMashing) modes.push('затирка');
    if (settings.autoNbk) modes.push('НБК');
    if (settings.autoFermentation) modes.push('ферментация');
    return modes;
}

function getStirrerMonitorSpeed(settings, stirrer) {
    return stirrer.running
        ? clamp(stirrer.speedPercent, 0, 100, settings.defaultSpeedPercent)
        : clamp(settings.defaultSpeedPercent, 1, 100, DEFAULT_STIRRER_SETTINGS.defaultSpeedPercent);
}

function getCurrentModeNameRu() {
    switch (currentMode) {
        case 1: return 'ректификация';
        case 2: return 'дистилляция';
        case 3: return 'ручной режим';
        case 4: return 'затирка';
        case 5: return 'пастеризация';
        case 6: return 'НБК';
        case 7: return 'ферментация';
        default: return 'активный режим';
    }
}

function getStirrerStatusMeta(settings, stirrer) {
    const safetyBlocked = !runtimeMonitorState.safetyOk || Boolean(runtimeMonitorState.currentAlarm?.latched);
    const autoModes = getEnabledAutoModes(settings);
    const settingsHintBase = autoModes.length
        ? `Автостарт настроен для режимов: ${autoModes.join(', ')}.`
        : 'Автостарт не включен ни для одного режима.';
    const modeBlocked = currentMode !== MODE_IDLE;
    const modeName = getCurrentModeNameRu();
    const modeSuffix = currentPaused ? `${modeName} на паузе` : modeName;

    if (!settings.enabled) {
        return {
            badgeText: 'Отключена',
            badgeTone: 'danger',
            modeText: 'Выкл',
            availabilityText: stirrer.available ? 'MCP4725 OK' : 'Нет MCP4725',
            settingsHint: 'Ручной запуск и автоуправление отключены.',
            monitorHint: 'Включите мешалку в параметрах оборудования.'
        };
    }

    if (!stirrer.available) {
        return {
            badgeText: 'Нет DAC',
            badgeTone: 'danger',
            modeText: 'Недоступна',
            availabilityText: 'Нет MCP4725',
            settingsHint: settingsHintBase,
            monitorHint: 'MCP4725 не обнаружен на шине I2C.'
        };
    }

    if (stirrer.running) {
        return {
            badgeText: stirrer.autoMode ? 'Авто' : 'Ручной ход',
            badgeTone: stirrer.autoMode ? 'warning' : 'success',
            modeText: stirrer.autoMode ? 'Авто' : 'Ручной',
            availabilityText: 'MCP4725 OK',
            settingsHint: settingsHintBase,
            monitorHint: stirrer.autoMode
                ? `Скорость управляется автоматически из FSM. Активен режим: ${modeSuffix}.`
                : 'Ручное управление активно.'
        };
    }

    if (safetyBlocked) {
        return {
            badgeText: 'Блокировка',
            badgeTone: 'warning',
            modeText: 'Ожидание',
            availabilityText: 'MCP4725 OK',
            settingsHint: settingsHintBase,
            monitorHint: 'Ручной запуск временно заблокирован защитой.'
        };
    }

    if (modeBlocked) {
        return {
            badgeText: 'FSM',
            badgeTone: 'warning',
            modeText: 'Занята',
            availabilityText: 'MCP4725 OK',
            settingsHint: `${settingsHintBase} Ручное управление доступно только в простое.`,
            monitorHint: `Ручное управление доступно только в простое. Сейчас активен ${modeSuffix}.`
        };
    }

    return {
        badgeText: 'Готова',
        badgeTone: 'muted',
        modeText: 'Ожидание',
        availabilityText: 'MCP4725 OK',
        settingsHint: settingsHintBase,
        monitorHint: 'Готова к ручному запуску.'
    };
}

export function syncStirrerUi(options = {}) {
    const { syncSettingsForm = false, syncSpeedInput = true } = options;
    const settings = runtimeMonitorState.stirrerSettings || DEFAULT_STIRRER_SETTINGS;
    const stirrer = runtimeMonitorState.stirrer || {};
    const meta = getStirrerStatusMeta(settings, stirrer);
    const monitorSpeed = getStirrerMonitorSpeed(settings, stirrer);
    const safetyBlocked = !runtimeMonitorState.safetyOk || Boolean(runtimeMonitorState.currentAlarm?.latched);
    const modeBlocked = currentMode !== MODE_IDLE;
    const canControl = settings.enabled && stirrer.available && !safetyBlocked && !modeBlocked;

    if (syncSettingsForm) {
        setCheckboxValue('stirrer-enabled', settings.enabled);
        setInputValue('stirrer-default-speed', clamp(settings.defaultSpeedPercent, 1, 100, DEFAULT_STIRRER_SETTINGS.defaultSpeedPercent));
        setCheckboxValue('stirrer-auto-mashing', settings.autoMashing);
        setCheckboxValue('stirrer-auto-fermentation', settings.autoFermentation);
        setCheckboxValue('stirrer-auto-nbk', settings.autoNbk);
    }

    if (syncSpeedInput) {
        const speedInput = document.getElementById('monitor-stirrer-speed-input');
        if (speedInput && document.activeElement !== speedInput) {
            speedInput.value = String(Math.round(monitorSpeed));
        }
    }

    const speedInput = document.getElementById('monitor-stirrer-speed-input');
    if (speedInput) speedInput.disabled = !canControl;

    setBadgeState('monitor-stirrer-badge', meta.badgeText, meta.badgeTone);
    setTextValue('monitor-stirrer-speed', `${Math.round(clamp(stirrer.speedPercent, 0, 100, 0))} %`);
    setTextValue('monitor-stirrer-mode', meta.modeText);
    setTextValue('monitor-stirrer-availability', meta.availabilityText);
    setTextValue('monitor-stirrer-hint', meta.monitorHint);

    setTextValue(
        'stirrer-settings-state',
        `${meta.badgeText} • ${Math.round(clamp(stirrer.speedPercent, 0, 100, 0))}% • ${meta.availabilityText}`
    );
    setTextValue('stirrer-settings-hint', meta.settingsHint);

    const startButton = document.getElementById('monitor-stirrer-start');
    if (startButton) startButton.disabled = !canControl || stirrer.running;
    const applyButton = document.getElementById('monitor-stirrer-apply');
    if (applyButton) applyButton.disabled = !canControl || !stirrer.running;
    const stopButton = document.getElementById('monitor-stirrer-stop');
    if (stopButton) stopButton.disabled = !stirrer.running;
}

export function addCubeExtenderVolume() {
    const cubeInput = document.getElementById('cube-volume-l');
    const extenderInput = document.getElementById('cube-extender-add-l');
    if (!cubeInput || !extenderInput) return;

    const currentCubeVolume = clamp(cubeInput.value, 5, 250, DEFAULT_CUBE_VOLUME_L);
    const extenderVolume = clamp(extenderInput.value, 1, 100, DEFAULT_CUBE_VOLUME_L);
    const nextCubeVolume = clamp(currentCubeVolume + extenderVolume, 5, 250, currentCubeVolume);

    cubeInput.value = nextCubeVolume.toFixed(1);
    extenderInput.value = extenderVolume.toFixed(0);
    saveCubeExtenderPreset(extenderVolume.toFixed(0));
    updateCubeVolumeHint({ normalizeInput: true });
}

export async function loadStirrerSettings() {
    try {
        const data = await requestJson('/api/settings/stirrer');
        updateStirrerSettingsState(data);
        updateStirrerState(data);
    } catch (error) {
        addLog(`✗ Ошибка загрузки настроек мешалки: ${error.message}`, 'error');
    } finally {
        syncStirrerUi({ syncSettingsForm: true, syncSpeedInput: true });
    }
}

export async function loadEquipmentSettings() {
    try {
        const response = await fetch('/api/settings/equipment');
        if (!response.ok) throw new Error(`HTTP ${response.status}`);

        const data = await response.json();
        setInputValue('heater-power-w', clamp(data.heaterPowerW, 1000, 10000, 3000));
        setInputValue('column-height', clamp(data.columnHeightMm, 500, 3000, 1500));
        setInputValue('cube-volume-l', clamp(data.cubeVolumeL, 5, 250, DEFAULT_CUBE_VOLUME_L).toFixed(1));
        setInputValue('water-autostart-cube-temp', clamp(data.waterAutoStartCubeTempC, 20, 60, 45).toFixed(1));

        runtimeMonitorState.equipment = {
            ...runtimeMonitorState.equipment,
            heaterPowerW: clamp(data.heaterPowerW, 1000, 10000, 3000),
            columnHeightMm: clamp(data.columnHeightMm, 500, 3000, 1500),
            cubeVolumeL: clamp(data.cubeVolumeL, 5, 250, DEFAULT_CUBE_VOLUME_L),
            minHeaterSubmergeL: clamp(data.minHeaterSubmergeL, 0.5, 100, 7.5),
            waterAutoStartCubeTempC: clamp(data.waterAutoStartCubeTempC, 20, 60, 45)
        };
    } catch (error) {
        addLog(`✗ Ошибка загрузки настроек оборудования: ${error.message}`, 'error');
    } finally {
        await loadStirrerSettings();
        initEquipmentNumberSteppers();
        loadCubeExtenderPreset();
        updateCubeVolumeHint({ normalizeInput: true });
    }
}

export async function saveEquipment() {
    const heaterPower = clamp(getInputValue('heater-power-w', 3000), 1000, 10000, 3000);
    const columnHeight = clamp(getInputValue('column-height', 1500), 500, 3000, 1500);
    const cubeVolume = clamp(getInputValue('cube-volume-l', DEFAULT_CUBE_VOLUME_L), 5, 250, DEFAULT_CUBE_VOLUME_L);
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
            waterAutoStartCubeTempC
        };

        updateCubeVolumeHint({ normalizeInput: true });
        addLog('💾 Настройки оборудования сохранены', 'success');
    } catch (error) {
        addLog(`✗ Ошибка сети при сохранении оборудования: ${error.message}`, 'error');
    }
}

export async function saveStirrerSettings() {
    const payload = {
        enabled: getCheckboxValue('stirrer-enabled', DEFAULT_STIRRER_SETTINGS.enabled),
        defaultSpeedPercent: clamp(
            getInputValue('stirrer-default-speed', DEFAULT_STIRRER_SETTINGS.defaultSpeedPercent),
            1,
            100,
            DEFAULT_STIRRER_SETTINGS.defaultSpeedPercent
        ),
        autoMashing: getCheckboxValue('stirrer-auto-mashing', DEFAULT_STIRRER_SETTINGS.autoMashing),
        autoFermentation: getCheckboxValue('stirrer-auto-fermentation', DEFAULT_STIRRER_SETTINGS.autoFermentation),
        autoNbk: getCheckboxValue('stirrer-auto-nbk', DEFAULT_STIRRER_SETTINGS.autoNbk)
    };

    try {
        const data = await requestJson('/api/settings/stirrer', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });

        updateStirrerSettingsState(data);
        updateStirrerState(data);
        syncStirrerUi({ syncSettingsForm: true, syncSpeedInput: true });
        addLog('💾 Настройки мешалки сохранены', 'success');
    } catch (error) {
        addLog(`✗ Ошибка сети при сохранении мешалки: ${error.message}`, 'error');
    }
}

function getRequestedMonitorStirrerSpeed() {
    return clamp(
        getInputValue(
            'monitor-stirrer-speed-input',
            runtimeMonitorState.stirrerSettings?.defaultSpeedPercent ?? DEFAULT_STIRRER_SETTINGS.defaultSpeedPercent
        ),
        1,
        100,
        runtimeMonitorState.stirrerSettings?.defaultSpeedPercent ?? DEFAULT_STIRRER_SETTINGS.defaultSpeedPercent
    );
}

async function submitStirrerAction(url, payload, successMessage) {
    const options = {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' }
    };
    if (payload !== undefined) {
        options.body = JSON.stringify(payload);
    }

    const data = await requestJson(url, options);
    updateStirrerState(data);
    syncStirrerUi({ syncSpeedInput: true });
    addLog(successMessage, 'success');
}

async function startMonitorStirrer() {
    const speed = getRequestedMonitorStirrerSpeed();
    await submitStirrerAction('/api/stirrer/start', { speed }, `✓ Мешалка запущена на ${speed}%`);
}

async function applyMonitorStirrerSpeed() {
    const speed = getRequestedMonitorStirrerSpeed();
    await submitStirrerAction('/api/stirrer/set', { speed }, `✓ Скорость мешалки изменена: ${speed}%`);
}

async function stopMonitorStirrer() {
    await submitStirrerAction('/api/stirrer/stop', undefined, '✓ Мешалка остановлена');
}

function normalizeStirrerSettingsInput() {
    const input = document.getElementById('stirrer-default-speed');
    if (!input) return;
    input.value = String(clamp(input.value, 1, 100, DEFAULT_STIRRER_SETTINGS.defaultSpeedPercent));
}

function normalizeMonitorStirrerInput() {
    const input = document.getElementById('monitor-stirrer-speed-input');
    if (!input) return;
    input.value = String(getRequestedMonitorStirrerSpeed());
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
            const value = clamp(extenderInput.value, 1, 100, DEFAULT_CUBE_VOLUME_L);
            extenderInput.value = value.toFixed(0);
            saveCubeExtenderPreset(value.toFixed(0));
        });
    }

    const stirrerDefaultSpeedInput = document.getElementById('stirrer-default-speed');
    if (stirrerDefaultSpeedInput) {
        stirrerDefaultSpeedInput.addEventListener('change', normalizeStirrerSettingsInput);
        stirrerDefaultSpeedInput.addEventListener('blur', normalizeStirrerSettingsInput);
    }

    const monitorStirrerSpeedInput = document.getElementById('monitor-stirrer-speed-input');
    if (monitorStirrerSpeedInput) {
        monitorStirrerSpeedInput.addEventListener('change', normalizeMonitorStirrerInput);
        monitorStirrerSpeedInput.addEventListener('blur', normalizeMonitorStirrerInput);
    }

    document.querySelectorAll('[data-stirrer-speed-preset]').forEach((button) => {
        button.addEventListener('click', () => {
            const nextSpeed = clamp(button.dataset.stirrerSpeedPreset, 1, 100, DEFAULT_STIRRER_SETTINGS.defaultSpeedPercent);
            setInputValue('monitor-stirrer-speed-input', nextSpeed);
        });
    });

    document.getElementById('monitor-stirrer-start')?.addEventListener('click', () => {
        void startMonitorStirrer().catch((error) => addLog(`✗ Мешалка: ${error.message}`, 'error'));
    });
    document.getElementById('monitor-stirrer-apply')?.addEventListener('click', () => {
        void applyMonitorStirrerSpeed().catch((error) => addLog(`✗ Мешалка: ${error.message}`, 'error'));
    });
    document.getElementById('monitor-stirrer-stop')?.addEventListener('click', () => {
        void stopMonitorStirrer().catch((error) => addLog(`✗ Мешалка: ${error.message}`, 'error'));
    });

    loadCubeExtenderPreset();
    updateCubeVolumeHint({ normalizeInput: true });
    updateStirrerSettingsState(DEFAULT_STIRRER_SETTINGS);
    syncStirrerUi({ syncSettingsForm: true, syncSpeedInput: true });
}
