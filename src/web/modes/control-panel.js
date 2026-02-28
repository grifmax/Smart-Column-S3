import { MODE_RECT, MODE_MANUAL, MODE_DIST, MODE_MASH, MODE_HOLD } from '../globals.js';
import { addLog } from '../core/logs.js';
import { loadStatus } from '../core/status.js';
import { startRectification, loadRectificationStartSettings } from './rectification.js';
import { startManual } from './rectification.js';
import { startDistillation, collectDistillationSettings } from './distillation.js';
import { startMashing, startHold } from './mashing-hold.js';

const CONTROL_MODE_STORAGE_KEY = 'control.selectedMode';
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
        title: 'Hold',
        subtitle: 'Температурные ступени выдержки',
        startLabel: '▶️ Запустить Hold',
        startClass: 'btn-success',
        modeValue: MODE_HOLD
    }
};

let selectedControlMode = 'rectification';
let rectSettingsLoaded = false;

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
    const def = getModeDefinition(mode);
    const title = document.getElementById('control-mode-title');
    const subtitle = document.getElementById('control-mode-subtitle');
    const startButton = document.getElementById('mode-start-button');

    if (title) title.textContent = def.title;
    if (subtitle) subtitle.textContent = def.subtitle;
    if (startButton) {
        startButton.textContent = def.startLabel;
        startButton.dataset.mode = String(def.modeValue);
        startButton.classList.remove('btn-primary', 'btn-success', 'btn-warning', 'btn-info');
        startButton.classList.add(def.startClass);
    }
}

async function ensureRectificationSettingsLoaded() {
    if (rectSettingsLoaded) return;
    const loaded = await loadRectificationStartSettings();
    rectSettingsLoaded = Boolean(loaded);
}

function goToMonitorTab() {
    const monitorTab = document.querySelector('.tab[data-tab="monitor"]');
    if (monitorTab) monitorTab.click();
    requestAnimationFrame(() => {
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
    }

    if (options.persist !== false) {
        persistModeSelection(normalized);
    }
}

export function getSelectedControlMode() {
    return selectedControlMode;
}

export async function startSelectedMode() {
    let started = false;

    switch (selectedControlMode) {
        case 'rectification':
            started = await startRectification();
            break;
        case 'manual':
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
        default:
            addLog('Не выбран режим запуска', 'warning');
            return false;
    }

    if (started) {
        goToMonitorTab();
        setTimeout(loadStatus, 250);
    }

    return started;
}

export async function initControlModePanel() {
    if (!document.getElementById('control-mode-panel')) {
        return;
    }

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
}
