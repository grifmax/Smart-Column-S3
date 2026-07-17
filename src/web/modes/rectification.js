import { MODE_RECT, MODE_MANUAL, clampFeedVolumeToCube, getCubeVolumeLimitL } from '../globals.js';
import {
    confirmModeSwitch,
    readBoosterStartSettings,
    loadBoosterStartSettings
} from './common.js';
import { loadStatus } from '../core/status.js';
import { addLog } from '../core/logs.js';

export const RECT_FEEDSTOCK_DEFAULTS = {
    0: { heads: 6.0, body: 84.0, tails: 10.0 },
    1: { heads: 8.0, body: 80.0, tails: 12.0 },
    2: { heads: 7.0, body: 81.0, tails: 12.0 },
    3: { heads: 5.0, body: 75.0, tails: 20.0 },
    4: { heads: 8.0, body: 74.0, tails: 18.0 },
    5: { heads: 6.0, body: 78.0, tails: 16.0 },
    6: { heads: 7.0, body: 79.0, tails: 14.0 },
    7: { heads: 8.0, body: 84.0, tails: 8.0 }
};

const RECT_BOOSTER_FIELD_IDS = {
    enabled: 'rect-start-booster-enabled',
    stopTemp: 'rect-start-booster-stop-cube-temp'
};

export function clampRectInput(value, min, max, fallback) {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) return fallback;
    if (parsed < min) return min;
    if (parsed > max) return max;
    return parsed;
}

function roundRectValue(value, decimals = 1) {
    const factor = 10 ** decimals;
    return Math.round(value * factor) / factor;
}

function getRectificationTakeoffBackendLabel(backendType) {
    switch (backendType) {
    case 1:
        return '3 клапана по фракциям';
    case 2:
        return '1 клапан + переключение';
    case 0:
    default:
        return 'Насос';
    }
}

function getRectificationRefluxModeLabel(refluxMode) {
    switch (refluxMode) {
    case 1:
        return 'Флегмовое число';
    case 2:
        return 'Автоцикл';
    case 0:
    default:
        return 'Прямой отбор';
    }
}

function getRectificationPbModeLabel(usePbMode, timpPbMs) {
    const holdSec = Math.max(0, Math.round(timpPbMs / 1000));
    switch (usePbMode) {
    case 2:
        return `По давлению, ${holdSec} с`;
    case 3:
        return `Куб + давление, ${holdSec} с`;
    case 1:
        return `По кубу, ${holdSec} с`;
    case 0:
    default:
        return 'Выключено';
    }
}

function buildRectificationAutoCorrectionPreset() {
    const takeoffBackendType = Math.round(clampRectInput(document.getElementById('rect-start-takeoff-backend')?.value, 0, 2, 0));
    const refluxMode = Math.round(clampRectInput(document.getElementById('rect-start-reflux-mode')?.value, 0, 2, 0));
    const bodySpeedMlHKw = clampRectInput(document.getElementById('rect-start-body-speed')?.value, 50, 3000, 600);
    const valvePulsePeriodMs = Math.round(clampRectInput(document.getElementById('rect-start-valve-pulse-period-ms')?.value, 100, 5000, 1000));
    const valvePulseMinOpenMs = Math.round(clampRectInput(document.getElementById('rect-start-valve-pulse-min-open-ms')?.value, 0, 5000, 80));
    const usesValveTakeoff = takeoffBackendType !== 0;
    const usesSwitchedRouting = takeoffBackendType === 2;
    const speedFactor = clampRectInput(bodySpeedMlHKw / 600, 0.5, 4.5, 1);
    const minValveDutyPercent = usesValveTakeoff && valvePulsePeriodMs > 0
        ? roundRectValue((clampRectInput(valvePulseMinOpenMs, 0, valvePulsePeriodMs, 80) / valvePulsePeriodMs) * 100, 1)
        : 0;

    let chimAutoPercent = 0.4;
    if (usesValveTakeoff) chimAutoPercent += 0.3;
    if (usesSwitchedRouting) chimAutoPercent += 0.2;
    if (refluxMode === 1) chimAutoPercent += 0.2;
    if (refluxMode === 2) chimAutoPercent += 0.4;
    if (speedFactor > 1) chimAutoPercent += (speedFactor - 1) * 0.15;
    chimAutoPercent = roundRectValue(clampRectInput(chimAutoPercent, 0, 200, 0.4), 1);

    let chimTimePerH = bodySpeedMlHKw * (usesValveTakeoff ? 0.025 : 0.015);
    if (refluxMode === 1) chimTimePerH *= 1.1;
    if (refluxMode === 2) chimTimePerH *= 1.25;
    if (usesSwitchedRouting) chimTimePerH *= 1.1;
    chimTimePerH = Math.round(clampRectInput(chimTimePerH, -2000, 2000, 0));

    let chimBegPercent = usesValveTakeoff ? 4 : 0;
    if (refluxMode === 1) chimBegPercent += 2;
    if (refluxMode === 2) chimBegPercent += 5;
    if (usesSwitchedRouting) chimBegPercent += 2;
    chimBegPercent = roundRectValue(clampRectInput(chimBegPercent, -100, 200, 0), 1);

    let chimMinPercent = 35;
    if (usesValveTakeoff) {
        chimMinPercent = Math.max(35, minValveDutyPercent + 10 + (refluxMode === 2 ? 3 : 0));
    }
    chimMinPercent = roundRectValue(clampRectInput(chimMinPercent, 0, 100, 35), 1);

    let usePbMode = 0;
    if (refluxMode === 2) {
        usePbMode = usesValveTakeoff ? 2 : 3;
    } else if (usesSwitchedRouting && bodySpeedMlHKw >= 800) {
        usePbMode = 2;
    }

    let timpPbMs = 15000;
    if (usePbMode !== 0) {
        timpPbMs = usesSwitchedRouting ? 18000 : (usesValveTakeoff ? 15000 : 12000);
    }

    return {
        chimAutoPercent,
        chimTimePerH,
        chimBegPercent,
        chimMinPercent,
        usePbMode,
        timpPbMs,
        summaryProfile: `${getRectificationTakeoffBackendLabel(takeoffBackendType)} + ${getRectificationRefluxModeLabel(refluxMode)}`,
        summaryMinPercent: usesValveTakeoff
            ? `${chimMinPercent.toFixed(1)}% (из импульса ${minValveDutyPercent.toFixed(1)}%)`
            : `${chimMinPercent.toFixed(1)}%`,
        summaryBodyEnd: getRectificationPbModeLabel(usePbMode, timpPbMs)
    };
}

function syncRectificationAutoCorrectionInputs(preset) {
    const setValue = (id, value) => {
        const el = document.getElementById(id);
        if (el) el.value = String(value);
    };

    setValue('rect-start-chim-auto', preset.chimAutoPercent);
    setValue('rect-start-chim-time', preset.chimTimePerH);
    setValue('rect-start-chim-beg', preset.chimBegPercent);
    setValue('rect-start-chim-min', preset.chimMinPercent);
    setValue('rect-start-use-pb-mode', preset.usePbMode);
    setValue('rect-start-timp-pb-ms', preset.timpPbMs);
}

export function updateRectificationAutoCorrectionSummary() {
    const preset = buildRectificationAutoCorrectionPreset();
    syncRectificationAutoCorrectionInputs(preset);

    const setSummaryValue = (id, value) => {
        const el = document.getElementById(id);
        if (el) el.value = value;
    };

    setSummaryValue('rect-start-auto-correction-profile-summary', preset.summaryProfile);
    setSummaryValue('rect-start-auto-correction-temp-summary', `${preset.chimAutoPercent.toFixed(1)} %/°C`);
    setSummaryValue('rect-start-auto-correction-time-summary', `${preset.chimTimePerH} мл/ч за час`);
    setSummaryValue('rect-start-auto-correction-beg-summary', `${preset.chimBegPercent.toFixed(1)} %`);
    setSummaryValue('rect-start-auto-correction-min-summary', preset.summaryMinPercent);
    setSummaryValue('rect-start-auto-correction-pb-summary', preset.summaryBodyEnd);
}

export function closeRectificationStartModal() {
    const modal = document.getElementById('rect-start-modal');
    if (modal) modal.style.display = 'none';
}

export function updateRectificationFractionsSum() {
    const heads = clampRectInput(document.getElementById('rect-start-heads-percent')?.value, 0, 40, 0);
    const body = clampRectInput(document.getElementById('rect-start-body-percent')?.value, 0, 100, 0);
    const tails = clampRectInput(document.getElementById('rect-start-tails-percent')?.value, 0, 100, 0);
    const sum = heads + body + tails;

    const sumEl = document.getElementById('rect-start-fractions-sum');
    if (!sumEl) return;

    sumEl.textContent = `Sum: ${sum.toFixed(1)}%`;
    sumEl.style.color = sum > 100 ? 'var(--danger)' : 'var(--text-secondary)';
}

export function applyRectificationFeedstockDefaults() {
    const feedstockEl = document.getElementById('rect-start-feedstock');
    const headsEl = document.getElementById('rect-start-heads-percent');
    const bodyEl = document.getElementById('rect-start-body-percent');
    const tailsEl = document.getElementById('rect-start-tails-percent');
    if (!feedstockEl || !headsEl || !bodyEl || !tailsEl) return;

    const feedstock = clampRectInput(feedstockEl.value, 0, 7, 0);
    const defaults = RECT_FEEDSTOCK_DEFAULTS[feedstock] || RECT_FEEDSTOCK_DEFAULTS[7];
    headsEl.value = defaults.heads.toFixed(1);
    bodyEl.value = defaults.body.toFixed(1);
    tailsEl.value = defaults.tails.toFixed(1);
    updateRectificationFractionsSum();
}

export function normalizeRectificationFractions(params) {
    params.headsPercent = clampRectInput(params.headsPercent, 0, 40, 0);
    params.bodyPercent = clampRectInput(params.bodyPercent, 0, 100, 0);
    params.tailsPercent = clampRectInput(params.tailsPercent, 0, 100, 0);

    const sum = params.headsPercent + params.bodyPercent + params.tailsPercent;
    if (sum <= 100) return params;

    let excess = sum - 100;
    if (params.tailsPercent >= excess) {
        params.tailsPercent -= excess;
        return params;
    }

    excess -= params.tailsPercent;
    params.tailsPercent = 0;
    params.bodyPercent = Math.max(0, params.bodyPercent - excess);
    return params;
}

export function collectRectificationModalSettings() {
    const maxFeedVolumeL = Math.max(1, Math.min(250, getCubeVolumeLimitL()));
    const refluxMode = Math.round(clampRectInput(document.getElementById('rect-start-reflux-mode')?.value, 0, 2, 0));
    const phasePowerStabilization = Math.round(clampRectInput(document.getElementById('rect-start-phase-power-stabilization')?.value, 1, 100, 70));
    const phasePowerHeads = Math.round(clampRectInput(document.getElementById('rect-start-phase-power-heads')?.value, 1, 100, 60));
    const phasePowerBody = Math.round(clampRectInput(document.getElementById('rect-start-phase-power-body')?.value, 1, 100, 60));
    const phasePowerTails = Math.round(clampRectInput(document.getElementById('rect-start-phase-power-tails')?.value, 1, 100, 50));
    const pressureControlEnabled = Boolean(document.getElementById('rect-start-pressure-control-enabled')?.checked);
    const pressureMinPowerPercent = Math.round(clampRectInput(document.getElementById('rect-start-pressure-min-power')?.value, 0, 100, 30));
    const autoCorrectionPreset = buildRectificationAutoCorrectionPreset();
    const params = {
        feedstock: clampRectInput(document.getElementById('rect-start-feedstock')?.value, 0, 7, 0),
        feedVolumeL: clampRectInput(document.getElementById('rect-start-feed-volume')?.value, 1, maxFeedVolumeL, clampFeedVolumeToCube(20)),
        feedAbvPercent: clampRectInput(document.getElementById('rect-start-feed-abv')?.value, 1, 96, 40),
        headsPercent: clampRectInput(document.getElementById('rect-start-heads-percent')?.value, 0, 40, 8),
        bodyPercent: clampRectInput(document.getElementById('rect-start-body-percent')?.value, 0, 100, 84),
        tailsPercent: clampRectInput(document.getElementById('rect-start-tails-percent')?.value, 0, 100, 8),
        headsSpeedMlHKw: clampRectInput(document.getElementById('rect-start-heads-speed')?.value, 10, 2000, 300),
        bodySpeedMlHKw: clampRectInput(document.getElementById('rect-start-body-speed')?.value, 50, 3000, 600),
        bodyContainerCount: Math.round(clampRectInput(document.getElementById('rect-start-body-containers')?.value, 1, 8, 1)),
        takeoffBackendType: Math.round(clampRectInput(document.getElementById('rect-start-takeoff-backend')?.value, 0, 2, 0)),
        stabilizationMin: Math.round(clampRectInput(document.getElementById('rect-start-stabilization')?.value, 1, 180, 30)),
        purgeMin: Math.round(clampRectInput(document.getElementById('rect-start-purge')?.value, 1, 120, 5)),
        baroCorrectionEnabled: Boolean(document.getElementById('rect-start-baro-correction-enabled')?.checked),
        refluxMode,
        srRatio: clampRectInput(document.getElementById('rect-start-sr-ratio')?.value, 0, 20, 0),
        autonomousCycleSec: Math.round(clampRectInput(document.getElementById('rect-start-auto-cycle')?.value, 1, 7200, 900)),
        autonomousPauseSec: Math.round(clampRectInput(document.getElementById('rect-start-auto-pause')?.value, 0, 7199, 90)),
        chimAutoPercent: autoCorrectionPreset.chimAutoPercent,
        chimTimePerH: autoCorrectionPreset.chimTimePerH,
        chimBegPercent: autoCorrectionPreset.chimBegPercent,
        chimMinPercent: autoCorrectionPreset.chimMinPercent,
        phasePowerStabilization,
        phasePowerHeads,
        phasePowerBody,
        phasePowerTails,
        phasePowerPercent: [phasePowerStabilization, phasePowerHeads, phasePowerBody, phasePowerTails],
        pressureControlEnabled,
        pressureMinPowerPercent,
        usePbMode: autoCorrectionPreset.usePbMode,
        timpPbMs: autoCorrectionPreset.timpPbMs,
        valvePulsePeriodMs: Math.round(clampRectInput(document.getElementById('rect-start-valve-pulse-period-ms')?.value, 100, 5000, 1000)),
        valvePulseMinOpenMs: Math.round(clampRectInput(document.getElementById('rect-start-valve-pulse-min-open-ms')?.value, 0, 5000, 80)),
        valvePulseMaxOpenMs: Math.round(clampRectInput(document.getElementById('rect-start-valve-pulse-max-open-ms')?.value, 0, 5000, 900)),
        routingSettlingMs: Math.round(clampRectInput(document.getElementById('rect-start-routing-settling-ms')?.value, 0, 10000, 1500)),
        routingRetargetMinMs: Math.round(clampRectInput(document.getElementById('rect-start-routing-retarget-min-ms')?.value, 0, 30000, 3000)),
        ...readBoosterStartSettings(RECT_BOOSTER_FIELD_IDS)
    };

    if (params.autonomousPauseSec >= params.autonomousCycleSec) {
        params.autonomousPauseSec = Math.max(0, params.autonomousCycleSec - 1);
    }

    return normalizeRectificationFractions(params);
}

function applyRectificationSettingsToInputs(params) {
    const setValue = (id, value) => {
        const el = document.getElementById(id);
        if (el && value !== undefined && value !== null) el.value = String(value);
    };

    setValue('rect-start-feedstock', params.feedstock ?? 0);
    const feedVolumeInput = document.getElementById('rect-start-feed-volume');
    if (feedVolumeInput) {
        const maxFeedVolumeL = Math.max(1, Math.min(250, getCubeVolumeLimitL()));
        feedVolumeInput.max = String(maxFeedVolumeL);
        feedVolumeInput.value = String(clampFeedVolumeToCube(params.feedVolumeL ?? 20));
    }
    setValue('rect-start-feed-abv', params.feedAbvPercent ?? 40);
    setValue('rect-start-heads-percent', params.headsPercent ?? 8);
    setValue('rect-start-body-percent', params.bodyPercent ?? 84);
    setValue('rect-start-tails-percent', params.tailsPercent ?? 8);
    setValue('rect-start-heads-speed', params.headsSpeedMlHKw ?? 300);
    setValue('rect-start-body-speed', params.bodySpeedMlHKw ?? 600);
    setValue('rect-start-body-containers', params.bodyContainerCount ?? 1);
    setValue('rect-start-takeoff-backend', params.takeoffBackendType ?? 0);
    setValue('rect-start-stabilization', params.stabilizationMin ?? 30);
    setValue('rect-start-purge', params.purgeMin ?? 5);
    setValue('rect-start-reflux-mode', params.refluxMode ?? 0);
    setValue('rect-start-sr-ratio', params.srRatio ?? 0);
    setValue('rect-start-auto-cycle', params.autonomousCycleSec ?? 900);
    setValue('rect-start-auto-pause', params.autonomousPauseSec ?? 90);
    setValue('rect-start-chim-auto', params.chimAutoPercent ?? 0);
    setValue('rect-start-chim-time', params.chimTimePerH ?? 0);
    setValue('rect-start-chim-beg', params.chimBegPercent ?? 0);
    setValue('rect-start-chim-min', params.chimMinPercent ?? 35);
    setValue('rect-start-phase-power-stabilization', params.phasePowerStabilization ?? 70);
    setValue('rect-start-phase-power-heads', params.phasePowerHeads ?? 60);
    setValue('rect-start-phase-power-body', params.phasePowerBody ?? 60);
    setValue('rect-start-phase-power-tails', params.phasePowerTails ?? 50);
    setValue('rect-start-pressure-min-power', params.pressureMinPowerPercent ?? 30);
    const pressureControlCheckbox = document.getElementById('rect-start-pressure-control-enabled');
    if (pressureControlCheckbox) {
        pressureControlCheckbox.checked = Boolean(params.pressureControlEnabled);
    }
    setValue('rect-start-use-pb-mode', params.usePbMode ?? 0);
    setValue('rect-start-timp-pb-ms', params.timpPbMs ?? 15000);
    setValue('rect-start-valve-pulse-period-ms', params.valvePulsePeriodMs ?? 1000);
    setValue('rect-start-valve-pulse-min-open-ms', params.valvePulseMinOpenMs ?? 80);
    setValue('rect-start-valve-pulse-max-open-ms', params.valvePulseMaxOpenMs ?? 900);
    setValue('rect-start-routing-settling-ms', params.routingSettlingMs ?? 1500);
    setValue('rect-start-routing-retarget-min-ms', params.routingRetargetMinMs ?? 3000);
    const baroCorrectionCheckbox = document.getElementById('rect-start-baro-correction-enabled');
    if (baroCorrectionCheckbox) {
        baroCorrectionCheckbox.checked = params.baroCorrectionEnabled !== false;
    }
    updateRectificationRefluxModeFields();
    updateRectificationTakeoffBackendFields();
    updateRectificationAutoCorrectionSummary();
    updateRectificationFractionsSum();
}

export function updateRectificationRefluxModeFields() {
    const mode = Math.round(clampRectInput(document.getElementById('rect-start-reflux-mode')?.value, 0, 2, 0));
    const srGroup = document.getElementById('rect-start-sr-ratio-group');
    const autoCycleGroup = document.getElementById('rect-start-auto-cycle-group');
    const autoPauseGroup = document.getElementById('rect-start-auto-pause-group');

    if (srGroup) srGroup.style.display = mode === 1 ? '' : 'none';
    if (autoCycleGroup) autoCycleGroup.style.display = mode === 2 ? '' : 'none';
    if (autoPauseGroup) autoPauseGroup.style.display = mode === 2 ? '' : 'none';
    updateRectificationAutoCorrectionSummary();
}

export function updateRectificationTakeoffBackendFields() {
    const backendType = Math.round(clampRectInput(document.getElementById('rect-start-takeoff-backend')?.value, 0, 2, 0));
    const pulseGroup = document.getElementById('rect-start-valve-pulse-fields-group');
    const routingGroup = document.getElementById('rect-start-routing-fields-group');
    const pulsePeriodInput = document.getElementById('rect-start-valve-pulse-period-ms');
    const pulseMinOpenInput = document.getElementById('rect-start-valve-pulse-min-open-ms');
    const pulseMaxOpenInput = document.getElementById('rect-start-valve-pulse-max-open-ms');
    const routingSettlingInput = document.getElementById('rect-start-routing-settling-ms');
    const routingRetargetInput = document.getElementById('rect-start-routing-retarget-min-ms');
    const usesValveTakeoff = backendType !== 0;
    const usesSwitchedValveRouting = backendType === 2;

    if (pulseGroup) pulseGroup.style.display = usesValveTakeoff ? '' : 'none';
    if (routingGroup) routingGroup.style.display = usesSwitchedValveRouting ? '' : 'none';
    if (pulsePeriodInput) pulsePeriodInput.disabled = !usesValveTakeoff;
    if (pulseMinOpenInput) pulseMinOpenInput.disabled = !usesValveTakeoff;
    if (pulseMaxOpenInput) pulseMaxOpenInput.disabled = !usesValveTakeoff;
    if (routingSettlingInput) routingSettlingInput.disabled = !usesSwitchedValveRouting;
    if (routingRetargetInput) routingRetargetInput.disabled = !usesSwitchedValveRouting;
    updateRectificationAutoCorrectionSummary();
}

export function syncRectificationFeedVolumeLimit() {
    const feedVolumeInput = document.getElementById('rect-start-feed-volume');
    if (!feedVolumeInput) return;
    const maxFeedVolumeL = Math.max(1, Math.min(250, getCubeVolumeLimitL()));
    feedVolumeInput.max = String(maxFeedVolumeL);
    feedVolumeInput.value = String(clampFeedVolumeToCube(feedVolumeInput.value, Number(feedVolumeInput.value) || 20));
}

export async function loadRectificationStartSettings() {
    let loaded = false;

    try {
        const response = await fetch('/api/settings/rect');
        if (response.ok) {
            applyRectificationSettingsToInputs(await response.json());
            loaded = true;
        } else {
            addLog(`Rect settings load error: ${response.status}`, 'warning');
        }
    } catch (e) {
        addLog(`Rect settings load network error: ${e.message}`, 'warning');
    }

    try {
        await loadBoosterStartSettings(RECT_BOOSTER_FIELD_IDS);
    } catch (e) {
        addLog(`Booster settings load error: ${e.message}`, 'warning');
    }

    return loaded;
}

export async function openRectificationStartModal() {
    await loadRectificationStartSettings();
    const modal = document.getElementById('rect-start-modal');
    if (!modal) return false;
    modal.style.display = 'flex';
    return true;
}

async function saveAndStartRectification(startButton) {
    if (startButton) startButton.disabled = true;

    try {
        const params = collectRectificationModalSettings();
        const savePayload = { ...params };
        delete savePayload.boosterEnabled;
        delete savePayload.boosterStopCubeTempC;

        const saveResponse = await fetch('/api/settings/rect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(savePayload)
        });

        if (!saveResponse.ok) {
            const err = await saveResponse.text();
            addLog(`Rect settings save error (${saveResponse.status}): ${err}`, 'error');
            return false;
        }

        closeRectificationStartModal();
        addLog('Starting auto-rectification with updated settings...', 'info');

        const startResponse = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                mode: 'rectification',
                params
            })
        });

        if (!startResponse.ok) {
            const err = await startResponse.text();
            addLog(`Start error (${startResponse.status}): ${err}`, 'error');
            return false;
        }

        const data = await startResponse.json();
        addLog('Auto-rectification started', 'success');
        if (data.warning) addLog(`Warning: ${data.warning}`, 'warning');
        setTimeout(loadStatus, 500);
        return true;
    } catch (e) {
        addLog(`Start rectification network error: ${e.message}`, 'error');
        console.error('saveAndStartRectification error:', e);
        return false;
    } finally {
        if (startButton) startButton.disabled = false;
    }
}

export async function confirmStartRectification() {
    await saveAndStartRectification(document.getElementById('rect-start-confirm'));
}

export function initRectificationStartModal() {
    const modal = document.getElementById('rect-start-modal');
    if (modal) {
        modal.addEventListener('click', (event) => {
            if (event.target === modal) closeRectificationStartModal();
        });
    }

    const feedstock = document.getElementById('rect-start-feedstock');
    if (feedstock) {
        feedstock.addEventListener('change', applyRectificationFeedstockDefaults);
    }

    const refluxMode = document.getElementById('rect-start-reflux-mode');
    if (refluxMode) {
        refluxMode.addEventListener('change', updateRectificationRefluxModeFields);
    }

    const takeoffBackend = document.getElementById('rect-start-takeoff-backend');
    if (takeoffBackend) {
        takeoffBackend.addEventListener('change', updateRectificationTakeoffBackendFields);
    }

    ['rect-start-body-speed', 'rect-start-valve-pulse-period-ms', 'rect-start-valve-pulse-min-open-ms', 'rect-start-valve-pulse-max-open-ms']
        .forEach((id) => {
            const input = document.getElementById(id);
            if (input) input.addEventListener('input', updateRectificationAutoCorrectionSummary);
        });

    ['rect-start-heads-percent', 'rect-start-body-percent', 'rect-start-tails-percent']
        .forEach((id) => {
            const input = document.getElementById(id);
            if (input) input.addEventListener('input', updateRectificationFractionsSum);
        });

    syncRectificationFeedVolumeLimit();
    updateRectificationRefluxModeFields();
    updateRectificationTakeoffBackendFields();
    updateRectificationAutoCorrectionSummary();
    updateRectificationFractionsSum();
}

export async function startRectification() {
    if (!confirmModeSwitch(MODE_RECT, 'Auto-rectification')) return false;
    return await saveAndStartRectification(document.getElementById('mode-start-button'));
}

export async function startManual() {
    if (!confirmModeSwitch(MODE_MANUAL, 'Manual rectification')) return false;

    try {
        const savePayload = { ...collectRectificationModalSettings() };
        delete savePayload.boosterEnabled;
        delete savePayload.boosterStopCubeTempC;
        await fetch('/api/settings/rect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(savePayload)
        });

        addLog('Starting manual rectification...', 'info');
        const response = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mode: 'manual_rect' })
        });

        if (!response.ok) {
            const err = await response.text();
            addLog(`Start manual error (${response.status}): ${err}`, 'error');
            return false;
        }

        const data = await response.json();
        addLog('Manual rectification started', 'success');
        if (data.warning) addLog(`Warning: ${data.warning}`, 'warning');
        setTimeout(loadStatus, 500);
        return true;
    } catch (e) {
        addLog(`Manual start network error: ${e.message}`, 'error');
        console.error('startManual error:', e);
        return false;
    }
}
