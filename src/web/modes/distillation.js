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

const FRACTION_PROGRAM_END_NONE = 0;
const FRACTION_PROGRAM_END_VOLUME = 1;
const FRACTION_PROGRAM_END_TEMPERATURE = 4;

let savedFractionProgram = null;

function clampDistillationInput(value, min, max, fallback) {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) return fallback;
    if (parsed < min) return min;
    if (parsed > max) return max;
    return parsed;
}

function setInputValue(id, value) {
    const element = document.getElementById(id);
    if (element) {
        element.value = value;
    }
}

function setCheckboxValue(id, value) {
    const element = document.getElementById(id);
    if (element) {
        element.checked = Boolean(value);
    }
}

function cloneFractionProgram(program) {
    if (!program || typeof program !== 'object') return null;
    return JSON.parse(JSON.stringify(program));
}

function buildFractionStep(overrides = {}) {
    return {
        name: String(overrides.name || ''),
        routeIndex: clampDistillationInput(overrides.routeIndex, 0, 4, 0),
        pumpRateMlH: clampDistillationInput(overrides.pumpRateMlH, 0, 65000, 0),
        heaterPowerW: clampDistillationInput(overrides.heaterPowerW, 0, 10000, 0),
        requireOperatorConfirmation: Boolean(overrides.requireOperatorConfirmation),
        confirmationPrompt: String(overrides.confirmationPrompt || '').slice(0, 63),
        allowManualAdvance: Boolean(overrides.allowManualAdvance),
        endConditions: clampDistillationInput(overrides.endConditions, 0, 255, FRACTION_PROGRAM_END_NONE),
        endVolumeMl: clampDistillationInput(overrides.endVolumeMl, 0, 50000, 0),
        endDurationSec: clampDistillationInput(overrides.endDurationSec, 0, 864000, 0),
        temperatureSensorIndex: clampDistillationInput(overrides.temperatureSensorIndex, 0, 15, 0),
        endTemperatureC: clampDistillationInput(overrides.endTemperatureC, 0, 110, 0)
    };
}

function normalizeFractionProgram(program) {
    const source = program && typeof program === 'object' ? program : {};
    const sourceSteps = Array.isArray(source.steps) ? source.steps : [];
    const steps = sourceSteps.slice(0, 8).map((step) => buildFractionStep(step));
    return {
        schemaVersion: clampDistillationInput(source.schemaVersion, 0, 255, 2),
        enabled: Boolean(source.enabled),
        stepCount: steps.length,
        heatingTemperatureSensorIndex: clampDistillationInput(source.heatingTemperatureSensorIndex, 0, 15, 0),
        heatingTargetTemperatureC: clampDistillationInput(source.heatingTargetTemperatureC, 0, 110, 78),
        steps
    };
}

function buildProgramBase() {
    const normalized = normalizeFractionProgram(savedFractionProgram);
    return {
        schemaVersion: normalized.schemaVersion,
        enabled: false,
        stepCount: 0,
        heatingTemperatureSensorIndex: normalized.heatingTemperatureSensorIndex,
        heatingTargetTemperatureC: normalized.heatingTargetTemperatureC,
        steps: []
    };
}

function buildRawSpiritProgram() {
    return buildProgramBase();
}

function buildHeadsBodyProgram(settings) {
    const program = buildProgramBase();
    const headsStep = buildFractionStep({
        name: 'Heads',
        routeIndex: 0,
        pumpRateMlH: settings.speed,
        requireOperatorConfirmation: true,
        confirmationPrompt: 'Install the heads collection container and confirm.',
        endConditions: settings.headsVolume > 0 ? FRACTION_PROGRAM_END_VOLUME : FRACTION_PROGRAM_END_NONE,
        endVolumeMl: settings.headsVolume,
        allowManualAdvance: settings.headsVolume <= 0
    });
    let bodyEndConditions = FRACTION_PROGRAM_END_NONE;
    if (settings.targetVolume > 0) bodyEndConditions |= FRACTION_PROGRAM_END_VOLUME;
    if (settings.endTemp > 0) bodyEndConditions |= FRACTION_PROGRAM_END_TEMPERATURE;
    const bodyStep = buildFractionStep({
        name: 'Body',
        routeIndex: 2,
        pumpRateMlH: settings.speed,
        requireOperatorConfirmation: true,
        confirmationPrompt: 'Replace the container for body collection and confirm.',
        endConditions: bodyEndConditions,
        endVolumeMl: settings.targetVolume,
        temperatureSensorIndex: 0,
        endTemperatureC: settings.endTemp
    });
    program.enabled = true;
    program.steps = [headsStep, bodyStep];
    program.stepCount = program.steps.length;
    return program;
}

function buildHeadsBodyTailsProgram(settings) {
    const program = buildHeadsBodyProgram(settings);
    program.steps.push(buildFractionStep({
        name: 'Tails',
        routeIndex: 4,
        pumpRateMlH: settings.speed,
        requireOperatorConfirmation: true,
        confirmationPrompt: 'Replace the container for tails collection and confirm.',
        endConditions: settings.tailsVolume > 0 ? FRACTION_PROGRAM_END_VOLUME : FRACTION_PROGRAM_END_NONE,
        endVolumeMl: settings.tailsVolume,
        allowManualAdvance: settings.tailsVolume <= 0
    }));
    program.stepCount = program.steps.length;
    return program;
}

function inferDistillationPreset(distillation) {
    const program = normalizeFractionProgram(distillation?.fractionProgram);
    if (!program.enabled || program.stepCount <= 0) return 'raw-spirit';
    if (program.stepCount === 2) return 'heads-body';
    if (program.stepCount === 3) return 'heads-body-tails';
    return 'custom';
}

function collectBaseDistillationSettings() {
    const heaterMax = Math.max(1, Number(maxHeaterPower) || 3000);
    const powerW = clampDistillationInput(
        document.getElementById('dist-start-power-percent')?.value,
        0,
        heaterMax,
        Math.round(heaterMax * 0.6)
    );
    return {
        heaterMax,
        preset: document.getElementById('dist-start-preset')?.value || 'raw-spirit',
        speed: clampDistillationInput(document.getElementById('dist-start-speed')?.value, 0, 65000, 500),
        headsVolume: clampDistillationInput(document.getElementById('dist-start-heads-volume')?.value, 0, 50000, 0),
        targetVolume: clampDistillationInput(document.getElementById('dist-start-target-volume')?.value, 0, 50000, 0),
        tailsVolume: clampDistillationInput(document.getElementById('dist-start-tails-volume')?.value, 0, 50000, 0),
        endTemp: clampDistillationInput(document.getElementById('dist-start-end-temp')?.value, 70, 110, 96),
        takeoffBackendType: clampDistillationInput(document.getElementById('dist-start-takeoff-backend')?.value, 0, 2, 0),
        valveSafeVentConfirmed: Boolean(document.getElementById('dist-start-safe-vent-confirmed')?.checked),
        powerW
    };
}

export function collectDistillationSettings() {
    const settings = collectBaseDistillationSettings();
    let fractionProgram;
    if (settings.preset === 'heads-body') {
        fractionProgram = buildHeadsBodyProgram(settings);
    } else if (settings.preset === 'heads-body-tails') {
        fractionProgram = buildHeadsBodyTailsProgram(settings);
    } else if (settings.preset === 'custom') {
        fractionProgram = normalizeFractionProgram(savedFractionProgram);
    } else {
        fractionProgram = buildRawSpiritProgram();
    }

    return {
        speed: settings.speed,
        headsVolume: settings.headsVolume,
        targetVolume: settings.targetVolume,
        tailsVolume: settings.tailsVolume,
        endTemp: settings.endTemp,
        takeoffBackendType: settings.takeoffBackendType,
        valveSafeVentConfirmed: settings.valveSafeVentConfirmed,
        powerW: settings.powerW,
        powerPercent: Math.min(100, Math.max(0, Math.round((settings.powerW / settings.heaterMax) * 100))),
        fractionProgram,
        ...readBoosterStartSettings(DIST_BOOSTER_FIELD_IDS)
    };
}

export async function loadDistillationStartSettings() {
    try {
        await loadBoosterStartSettings(DIST_BOOSTER_FIELD_IDS);
        const response = await fetch('/api/status');
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        const data = await response.json();
        const distillation = data?.distillation || {};
        savedFractionProgram = normalizeFractionProgram(distillation.fractionProgram);
        setInputValue('dist-start-preset', inferDistillationPreset(distillation));
        setInputValue('dist-start-speed', clampDistillationInput(distillation.speedMlH, 0, 65000, 500));
        setInputValue('dist-start-heads-volume', clampDistillationInput(distillation.headsVolumeMl, 0, 50000, 0));
        setInputValue('dist-start-target-volume', clampDistillationInput(distillation.targetVolumeMl, 0, 50000, 0));
        setInputValue('dist-start-tails-volume', clampDistillationInput(distillation.tailsVolumeMl, 0, 50000, 0));
        setInputValue('dist-start-end-temp', clampDistillationInput(distillation.endTempC, 70, 110, 96));
        setInputValue('dist-start-takeoff-backend', clampDistillationInput(distillation.takeoffBackendType, 0, 2, 0));
        setCheckboxValue('dist-start-safe-vent-confirmed', distillation.valveSafeVentConfirmed);
        setInputValue(
            'dist-start-power-percent',
            clampDistillationInput(
                distillation.powerW,
                0,
                Math.max(1, Number(maxHeaterPower) || 3000),
                Math.round((Math.max(1, Number(maxHeaterPower) || 3000)) * 0.6)
            )
        );
        return true;
    } catch (error) {
        addLog(`РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё РЅР°СЃС‚СЂРѕРµРє РґРёСЃС‚РёР»Р»СЏС†РёРё: ${error.message}`, 'warning');
        return false;
    }
}

export async function startDistillation(paramsOverride = null) {
    if (!confirmModeSwitch(MODE_DIST, 'Distillation')) return false;

    const params = paramsOverride || collectDistillationSettings();

    try {
        addLog('РћС‚РїСЂР°РІРєР° РєРѕРјР°РЅРґС‹ Р·Р°РїСѓСЃРєР° РґРёСЃС‚РёР»Р»СЏС†РёРё...', 'info');

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
            addLog('Р”РёСЃС‚РёР»Р»СЏС†РёСЏ Р·Р°РїСѓС‰РµРЅР°', 'success');
            if (data.warning) addLog(`РџСЂРµРґСѓРїСЂРµР¶РґРµРЅРёРµ: ${data.warning}`, 'warning');
            setTimeout(loadStatus, 500);
            return true;
        }

        const error = await response.text();
        addLog(`РћС€РёР±РєР° Р·Р°РїСѓСЃРєР° РґРёСЃС‚РёР»Р»СЏС†РёРё (${response.status}): ${error}`, 'error');
        return false;
    } catch (e) {
        addLog(`РЎРµС‚РµРІР°СЏ РѕС€РёР±РєР° РґРёСЃС‚РёР»Р»СЏС†РёРё: ${e.message}`, 'error');
        console.error('Start distillation error:', e);
        return false;
    }
}
