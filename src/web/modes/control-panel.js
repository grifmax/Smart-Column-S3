import {
    currentMode,
    MODE_IDLE,
    runtimeMonitorState,
    MODE_RECT,
    MODE_MANUAL,
    MODE_DIST,
    MODE_MASH,
    MODE_HOLD,
    MODE_NBK,
    MODE_FERMENTATION,
    clampFeedVolumeToCube,
    getCubeVolumeLimitL
} from '../globals.js';
import { addLog } from '../core/logs.js';
import { loadStatus } from '../core/status.js';
import { getStartAvailabilityState } from '../runtime/bars.js';
import { startRectification, loadRectificationStartSettings } from './rectification.js';
import { startManual } from './rectification.js';
import { startDistillation, collectDistillationSettings } from './distillation.js';
import { startMashing, startHold } from './mashing-hold.js';
import {
    loadNbkSettings,
    loadFermentationSettings,
    startNbk,
    startFermentation
} from './nbk-fermentation.js';

const CONTROL_MODE_STORAGE_KEY = 'control.selectedMode';
const MANUAL_RECT_STORAGE_KEY = 'control.manualRectSettings';
const CONTROL_MODES = {
    rectification: {
        title: 'Авто-ректификация',
        subtitle: 'Параметры запуска процесса',
        startLabel: '▶️ Сохранить и запустить авто-ректификацию',
        startClass: 'btn-success',
        modeValue: MODE_RECT
    },
    manual: {
        title: 'Ручная ректификация',
        subtitle: 'Старт режима и ручное управление ТЭН/насос/клапаны',
        startLabel: '▶️ Запустить ручную ректификацию',
        startClass: 'btn-warning',
        modeValue: MODE_MANUAL
    },
    distillation: {
        title: 'Дистилляция',
        subtitle: 'Быстрые параметры запуска дистилляции',
        startLabel: '▶️ Запустить дистилляцию',
        startClass: 'btn-info',
        modeValue: MODE_DIST
    },
    mashing: {
        title: 'Затирка',
        subtitle: 'Температурный профиль и шаги затирки',
        startLabel: '▶️ Запустить затирку',
        startClass: 'btn-success',
        modeValue: MODE_MASH
    },
    hold: {
        title: 'Пастеризация',
        subtitle: 'Температурные шаги, паузы и управляемое охлаждение',
        startLabel: '▶️ Запустить пастеризацию',
        startClass: 'btn-success',
        modeValue: MODE_HOLD
    },
    nbk: {
        title: 'НБК',
        subtitle: 'Непрерывная бражная колонна: мощность, подача браги и контроль температуры низа колонны',
        startLabel: '▶️ Запустить НБК',
        startClass: 'btn-warning',
        modeValue: MODE_NBK
    },
    fermentation: {
        title: 'Ферментация',
        subtitle: 'Поддержание температуры брожения по датчику в кубе или ферментере',
        startLabel: '▶️ Запустить ферментацию',
        startClass: 'btn-info',
        modeValue: MODE_FERMENTATION
    }
};

let selectedControlMode = 'rectification';
let rectSettingsLoaded = false;
let nbkSettingsLoaded = false;
let fermentationSettingsLoaded = false;
let manualRectInitialized = false;

function getModeDefinition(mode) {
    return CONTROL_MODES[mode] || CONTROL_MODES.rectification;
}

function persistModeSelection(mode) {
    try {
        localStorage.setItem(CONTROL_MODE_STORAGE_KEY, mode);
    } catch {
        // ignore storage failures
    }
}

function renderControlModeSelector(mode) {
    document.querySelectorAll('[data-mode-select]').forEach((button) => {
        const isSelected = button.dataset.modeSelect === mode;
        button.classList.toggle('control-mode-selected', isSelected);
    });
}

function renderControlModePanels(mode) {
    document.querySelectorAll('[data-mode-panel]').forEach((panel) => {
        const shouldShow = panel.dataset.modePanel === mode;
        panel.style.display = shouldShow ? '' : 'none';
    });
}

function renderControlModeHeader(mode) {
    const title = document.getElementById('control-mode-title');
    const subtitle = document.getElementById('control-mode-subtitle');
    const startButton = document.getElementById('mode-start-button');
    const labels = {
        rectification: {
            title: 'Авто-ректификация',
            subtitle: 'Параметры запуска процесса',
            startLabel: 'Сохранить и запустить авто-ректификацию'
        },
        manual: {
            title: 'Ручная ректификация',
            subtitle: 'Старт режима и ручное управление нагревом с настройкой отбора тела',
            startLabel: 'Запустить ручную ректификацию'
        },
        distillation: {
            title: 'Дистилляция',
            subtitle: 'Быстрые параметры запуска дистилляции',
            startLabel: 'Запустить дистилляцию'
        },
        mashing: {
            title: 'Затирка',
            subtitle: 'Температурный профиль и шаги затирки',
            startLabel: 'Запустить затирку'
        },
        hold: {
            title: 'Пастеризация',
            subtitle: 'Температурные шаги, паузы и управляемое охлаждение',
            startLabel: 'Запустить пастеризацию'
        },
        nbk: {
            title: 'НБК',
            subtitle: 'Непрерывная бражная колонна: мощность, подача браги и контроль низа колонны',
            startLabel: 'Запустить НБК'
        },
        fermentation: {
            title: 'Ферментация',
            subtitle: 'Поддержание температуры брожения по датчику в кубе или ферментере',
            startLabel: 'Запустить ферментацию'
        }
    };
    const def = getModeDefinition(mode);
    const ui = labels[mode] || labels.rectification;

    if (title) title.textContent = ui.title;
    if (subtitle) subtitle.textContent = ui.subtitle;
    if (startButton) {
        startButton.textContent = ui.startLabel;
        startButton.dataset.mode = String(def.modeValue);
        startButton.classList.remove('btn-success', 'btn-warning', 'btn-info');
        startButton.classList.add('btn-primary');
    }
}

export function renderControlStartState() {
    const startButton = document.getElementById('mode-start-button');
    const statusEl = document.getElementById('mode-start-status');
    const availability = getStartAvailabilityState(runtimeMonitorState);
    const activeProcessBlock = currentMode !== MODE_IDLE;

    if (startButton) {
        startButton.disabled = availability.disabled;
        startButton.classList.toggle('btn-disabled', availability.disabled);
        startButton.title = availability.disabled ? availability.detail : '';
    }

    if (statusEl) {
        statusEl.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
        statusEl.classList.add(`is-${availability.tone}`);
        statusEl.textContent = activeProcessBlock
            ? 'Новый запуск недоступен, пока текущий процесс не остановлен и автоматика не вернётся в idle.'
            : `${availability.title}. ${availability.detail}`;
    }
}

function normalizeControlPanelMarkup() {
    const titles = {
        rectification: 'Авто-ректификация',
        manual: 'Ручная ректификация',
        distillation: 'Дистилляция',
        nbk: 'НБК',
        fermentation: 'Ферментация',
        mashing: 'Затирка',
        hold: 'Пастеризация'
    };

    document.querySelectorAll('[data-mode-select]').forEach((button) => {
        const mode = button.dataset.modeSelect;
        button.classList.remove('btn-success', 'btn-warning', 'btn-info');
        button.classList.add('btn');
        if (titles[mode]) {
            button.textContent = titles[mode];
        }
    });

    const historyType = document.getElementById('history-filter-type');
    if (historyType) {
        historyType.innerHTML = `
            <option value="all">Все типы</option>
            <option value="rectification">Ректификация</option>
            <option value="distillation">Дистилляция</option>
            <option value="nbk">НБК</option>
            <option value="fermentation">Ферментация</option>
            <option value="mashing">Затирка</option>
            <option value="hold">Пастеризация</option>
        `;
        historyType.closest('div')?.classList.add('history-toolbar');
    }

    const pumpControl = document.getElementById('pump-speed-control')?.closest('.control-group');
    const bodyDecrementField = document.getElementById('manual-body-speed-decrement')?.closest('.form-group');
    if (pumpControl && bodyDecrementField && !document.getElementById('manual-body-pump-hint')) {
        pumpControl.style.gridColumn = '1 / -1';
        const hint = document.createElement('div');
        hint.id = 'manual-body-pump-hint';
        hint.className = 'control-inline-hint';
        hint.textContent = 'Ручная подача для отбора тела и сервисной настройки потока.';
        pumpControl.appendChild(hint);
        bodyDecrementField.insertAdjacentElement('afterend', pumpControl);
    }

    document.querySelector('#control-panel-manual .controls button[onclick*="toggleValve"]')?.closest('.control-group')?.remove();
    document.getElementById('dist-start-speed')?.closest('.form-group')?.remove();
    document.getElementById('dist-start-heads-volume')?.closest('.form-group')?.remove();
    document.getElementById('dist-start-target-volume')?.closest('.form-group')?.remove();

    const holdTitle = document.querySelector('#control-panel-hold .control-subsection-title');
    if (holdTitle) {
        holdTitle.textContent = 'Пастеризация';
    }

    const holdLabel = document.querySelector('#control-panel-hold .form-group > label');
    if (holdLabel) {
        holdLabel.textContent = 'Шаги (температура °C, длительность мин)';
    }

    const holdSteps = document.getElementById('hold-steps');
    if (holdSteps && !document.getElementById('hold-steps-hint')) {
        const hint = document.createElement('div');
        hint.id = 'hold-steps-hint';
        hint.className = 'control-inline-hint';
        hint.textContent = 'Если температура не указана, это пауза без нагрева. Охлаждение используется только для шага с температурой ниже предыдущего.';
        holdSteps.insertAdjacentElement('beforebegin', hint);
    }
}

async function ensureRectificationSettingsLoaded() {
    if (rectSettingsLoaded) return;
    const loaded = await loadRectificationStartSettings();
    rectSettingsLoaded = Boolean(loaded);
}
async function ensureNbkSettingsLoaded() {
    if (nbkSettingsLoaded) return;
    const loaded = await loadNbkSettings();
    nbkSettingsLoaded = Boolean(loaded);
}
async function ensureFermentationSettingsLoaded() {
    if (fermentationSettingsLoaded) return;
    const loaded = await loadFermentationSettings();
    fermentationSettingsLoaded = Boolean(loaded);
}

function goToMonitorTab() {
    const monitorTab = document.querySelector('.tab[data-tab="monitor"]');
    if (monitorTab) monitorTab.click();
    window.requestAnimationFrame(() => {
        window.scrollTo({ top: 0, left: 0, behavior: 'smooth' });
    });
}

export async function selectControlMode(mode, options = {}) {
    const normalized = CONTROL_MODES[mode] ? mode : 'rectification';
    selectedControlMode = normalized;

    renderControlModeSelector(normalized);
    renderControlModePanels(normalized);
    renderControlModeHeader(normalized);

    if (normalized === 'rectification') {
        await ensureRectificationSettingsLoaded();
    } else if (normalized === 'nbk') {
        await ensureNbkSettingsLoaded();
    } else if (normalized === 'fermentation') {
        await ensureFermentationSettingsLoaded();
    }

    if (options.persist !== false) {
        persistModeSelection(normalized);
    }

    renderControlStartState();
}

export function getSelectedControlMode() {
    return selectedControlMode;
}

export async function startSelectedMode() {
    if (currentMode !== MODE_IDLE) {
        addLog('Сначала остановите текущий процесс, затем запускайте новый режим.', 'warning');
        renderControlStartState();
        return false;
    }

    const availability = getStartAvailabilityState(runtimeMonitorState);
    if (availability.disabled) {
        addLog(`Запуск заблокирован: ${availability.detail}`, 'warning');
        renderControlStartState();
        return false;
    }

    let started;

    switch (selectedControlMode) {
        case 'rectification':
            started = await startRectification();
            break;
        case 'manual':
            saveManualRectSettings({ silent: true });
            started = await startManual();
            break;
        case 'distillation':
            started = await startDistillation(collectDistillationSettings());
            break;
        case 'mashing':
            started = await startMashing();
            break;
        case 'hold':
            started = await startHold();
            break;
        case 'nbk':
            started = await startNbk();
            break;
        case 'fermentation':
            started = await startFermentation();
            break;
        default:
            addLog('Не выбран режим запуска', 'warning');
            return false;
    }

    if (started) {
        goToMonitorTab();
        setTimeout(loadStatus, 250);
    }

    renderControlStartState();

    return started;
}

export async function initControlModePanel() {
    if (!document.getElementById('control-mode-panel')) {
        return;
    }

    initManualRectSettings();
    normalizeControlPanelMarkup();

    let initialMode = 'rectification';
    try {
        const saved = localStorage.getItem(CONTROL_MODE_STORAGE_KEY);
        if (saved && CONTROL_MODES[saved]) {
            initialMode = saved;
        }
    } catch {
        // ignore storage failures
    }

    await selectControlMode(initialMode, { persist: false });
    renderControlStartState();
}

function byId(id) {
    return document.getElementById(id);
}

function setInputValue(id, value) {
    const el = byId(id);
    if (!el || value === undefined || value === null) return;
    if (el.type === 'checkbox') {
        el.checked = Boolean(value);
        return;
    }
    el.value = String(value);
}

function getNumberValue(id, fallback) {
    const parsed = Number(byId(id)?.value);
    return Number.isFinite(parsed) ? parsed : fallback;
}

function getStringValue(id, fallback) {
    const value = byId(id)?.value;
    return typeof value === 'string' && value.length > 0 ? value : fallback;
}

function getCheckboxValue(id) {
    return Boolean(byId(id)?.checked);
}

export function calcManualHeadsSpeed() {
    const volume = getNumberValue('manual-heads-volume', 50);
    const time = Math.max(1, getNumberValue('manual-heads-time', 180));
    const speed = (volume / time) * 60;
    const out = byId('manual-heads-calc-speed');
    if (out) out.textContent = speed.toFixed(1);
}

export function calcManualHeadsTime() {
    const volume = getNumberValue('manual-heads-volume', 50);
    const speed = Math.max(1, getNumberValue('manual-heads-speed', 300));
    const time = (volume / speed) * 60;
    const out = byId('manual-heads-calc-time');
    if (out) out.textContent = time.toFixed(0);
}

export function updateManualHeadsMode() {
    const mode = getStringValue('manual-heads-mode', 'time');
    const timeGroup = byId('manual-heads-time-group');
    const speedGroup = byId('manual-heads-speed-group');
    const tempGroup = byId('manual-heads-temp-group');
    if (timeGroup) timeGroup.style.display = mode === 'time' ? '' : 'none';
    if (speedGroup) speedGroup.style.display = mode === 'speed' ? '' : 'none';
    if (tempGroup) tempGroup.style.display = mode === 'temp' ? '' : 'none';

    if (mode === 'time') calcManualHeadsSpeed();
    if (mode === 'speed') calcManualHeadsTime();
}

export function updateManualBodyToTailsMode() {
    const mode = getStringValue('manual-body-to-tails-mode', 'stabilize');
    const stabilizeGroup = byId('manual-body-stabilize-group');
    const tempGroup = byId('manual-body-temp-group');
    if (stabilizeGroup) stabilizeGroup.style.display = (mode === 'stabilize' || mode === 'both') ? '' : 'none';
    if (tempGroup) tempGroup.style.display = (mode === 'temp' || mode === 'both') ? '' : 'none';
}

export function updateManualTailsMode() {
    const tailsSettings = byId('manual-tails-settings');
    if (tailsSettings) tailsSettings.style.display = getCheckboxValue('manual-tails-enabled') ? '' : 'none';
}

export function updateManualTailsStopMode() {
    const mode = getStringValue('manual-tails-stop-mode', 'temp');
    const tempGroup = byId('manual-tails-temp-group');
    const abvGroup = byId('manual-tails-abv-group');
    if (tempGroup) tempGroup.style.display = mode === 'temp' ? '' : 'none';
    if (abvGroup) abvGroup.style.display = mode === 'abv' ? '' : 'none';
}

export function updateManualTailsPwmMode() {
    const pwmSettings = byId('manual-tails-pwm-settings');
    if (pwmSettings) pwmSettings.style.display = getCheckboxValue('manual-tails-pwm-enabled') ? '' : 'none';
}

export function syncManualFeedVolumeLimit() {
    const input = byId('manual-feed-volume');
    if (!input) return;
    const maxFeedVolumeL = Math.max(1, Math.min(250, getCubeVolumeLimitL()));
    input.max = String(maxFeedVolumeL);
    input.value = String(clampFeedVolumeToCube(input.value, Number(input.value) || 20));
}

function collectManualRectSettings() {
    return {
        feed: {
            volumeL: clampFeedVolumeToCube(getNumberValue('manual-feed-volume', 20), 20),
            abvPercent: getNumberValue('manual-feed-abv', 40)
        },
        heads: {
            volume: getNumberValue('manual-heads-volume', 50),
            mode: getStringValue('manual-heads-mode', 'time'),
            time: getNumberValue('manual-heads-time', 180),
            speed: getNumberValue('manual-heads-speed', 300),
            temp: getNumberValue('manual-heads-temp', 78.5)
        },
        body: {
            spikeThreshold: getNumberValue('manual-body-spike-threshold', 0.2),
            speedDecrement: getNumberValue('manual-body-speed-decrement', 5),
            toTailsMode: getStringValue('manual-body-to-tails-mode', 'stabilize'),
            stabilizeTimeout: getNumberValue('manual-body-stabilize-timeout', 10),
            toTailsTemp: getNumberValue('manual-body-to-tails-temp', 92)
        },
        tails: {
            enabled: getCheckboxValue('manual-tails-enabled'),
            stopMode: getStringValue('manual-tails-stop-mode', 'temp'),
            stopTemp: getNumberValue('manual-tails-stop-temp', 93),
            stopAbv: getNumberValue('manual-tails-stop-abv', 40),
            pwmEnabled: getCheckboxValue('manual-tails-pwm-enabled'),
            pwmDuty: getNumberValue('manual-tails-pwm-duty', 30),
            pwmPeriod: getNumberValue('manual-tails-pwm-period', 10)
        }
    };
}

function applyManualRectSettings(settings) {
    const feed = settings?.feed || {};
    const heads = settings?.heads || {};
    const body = settings?.body || {};
    const tails = settings?.tails || {};

    const manualFeedVolumeInput = byId('manual-feed-volume');
    if (manualFeedVolumeInput) {
        const maxFeedVolumeL = Math.max(1, Math.min(250, getCubeVolumeLimitL()));
        manualFeedVolumeInput.max = String(maxFeedVolumeL);
        manualFeedVolumeInput.value = String(clampFeedVolumeToCube(feed.volumeL ?? 20, 20));
    }
    setInputValue('manual-feed-abv', feed.abvPercent ?? 40);

    setInputValue('manual-heads-volume', heads.volume ?? 50);
    setInputValue('manual-heads-mode', heads.mode ?? 'time');
    setInputValue('manual-heads-time', heads.time ?? 180);
    setInputValue('manual-heads-speed', heads.speed ?? 300);
    setInputValue('manual-heads-temp', heads.temp ?? 78.5);

    setInputValue('manual-body-spike-threshold', body.spikeThreshold ?? 0.2);
    setInputValue('manual-body-speed-decrement', body.speedDecrement ?? 5);
    setInputValue('manual-body-to-tails-mode', body.toTailsMode ?? 'stabilize');
    setInputValue('manual-body-stabilize-timeout', body.stabilizeTimeout ?? 10);
    setInputValue('manual-body-to-tails-temp', body.toTailsTemp ?? 92);

    setInputValue('manual-tails-enabled', tails.enabled ?? false);
    setInputValue('manual-tails-stop-mode', tails.stopMode ?? 'temp');
    setInputValue('manual-tails-stop-temp', tails.stopTemp ?? 93);
    setInputValue('manual-tails-stop-abv', tails.stopAbv ?? 40);
    setInputValue('manual-tails-pwm-enabled', tails.pwmEnabled ?? false);
    setInputValue('manual-tails-pwm-duty', tails.pwmDuty ?? 30);
    setInputValue('manual-tails-pwm-period', tails.pwmPeriod ?? 10);

    const dutyValue = byId('manual-tails-pwm-duty-val');
    if (dutyValue) dutyValue.textContent = `${getNumberValue('manual-tails-pwm-duty', 30)}%`;

    updateManualHeadsMode();
    updateManualBodyToTailsMode();
    updateManualTailsMode();
    updateManualTailsStopMode();
    updateManualTailsPwmMode();
}

export function saveManualRectSettings(options = {}) {
    const { silent = false } = options;
    const settings = collectManualRectSettings();
    try {
        localStorage.setItem(MANUAL_RECT_STORAGE_KEY, JSON.stringify(settings));
        if (!silent) addLog('Параметры ручной ректификации сохранены', 'info');
    } catch {
        if (!silent) addLog('Не удалось сохранить параметры ручной ректификации', 'warning');
    }
}

export function loadManualRectSettings(options = {}) {
    const { silent = false } = options;
    try {
        const raw = localStorage.getItem(MANUAL_RECT_STORAGE_KEY);
        if (!raw) {
            applyManualRectSettings({});
            return;
        }
        applyManualRectSettings(JSON.parse(raw));
        if (!silent) addLog('Параметры ручной ректификации загружены', 'info');
    } catch {
        applyManualRectSettings({});
        if (!silent) addLog('Ошибка загрузки параметров ручной ректификации', 'warning');
    }
}

export function initManualRectSettings() {
    if (manualRectInitialized || !byId('manual-heads-mode')) return;
    manualRectInitialized = true;

    const headsVolume = byId('manual-heads-volume');
    if (headsVolume) headsVolume.addEventListener('input', calcManualHeadsSpeed);

    const pwmDuty = byId('manual-tails-pwm-duty');
    if (pwmDuty) {
        pwmDuty.addEventListener('input', () => {
            const dutyValue = byId('manual-tails-pwm-duty-val');
            if (dutyValue) dutyValue.textContent = `${pwmDuty.value}%`;
        });
    }

    loadManualRectSettings({ silent: true });
    syncManualFeedVolumeLimit();
}
