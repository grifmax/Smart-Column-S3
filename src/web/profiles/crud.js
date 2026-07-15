import { currentProfileId, setCurrentProfileId } from './state.js';
import { loadProfilesList } from './list.js';
import {
    getProfileCompatibility,
    getProfileCompatibilityBadge,
    getProfileEquipmentMismatch
} from './compat.js';
import { loadStatus } from '../core/status.js';
import { activateTabById } from '../core/tabs.js';
import { selectControlMode } from '../modes/control-panel.js';
import { addMashStep, createStepRow, readStepsFromUI, setMashProfileUI } from '../modes/mashing-hold.js';

let currentProfileIsBuiltin = false;
let profileFormLiveBindingsReady = false;

function escapeHtml(value) {

    return String(value ?? '')
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&#39;');

}

const PROFILE_FORM_DEFAULTS = {
    metadata: {
        name: '',
        description: '',
        category: 'rectification',
        tags: []
    },
    parameters: {
        mode: 'rectification',
        model: 'classic',
        heater: {
            maxPower: 3000,
            autoMode: true,
            pidKp: 2.0,
            pidKi: 0.5,
            pidKd: 1.0,
            boosterEnabled: false,
            boosterStopCubeTempC: 78.0
        },
        rectification: {
            stabilizationMin: 30,
            headsVolume: 300,
            bodyVolume: 3200,
            tailsVolume: 300,
            headsSpeed: 300,
            bodySpeed: 600,
            tailsSpeed: 360,
            purgeMin: 5,
            baroCorrectionEnabled: true,
            takeoffBackendType: 0,
            refluxMode: 0,
            srTarget: 0,
            autonomousCycleSec: 900,
            autonomousPauseSec: 90,
            chimAutoPercent: 0,
            chimTimePerH: 0,
            chimBegPercent: 0,
            chimMinPercent: 35,
            usePbMode: 0,
            timpPbMs: 15000,
            valvePulsePeriodMs: 1000,
            valvePulseMinOpenMs: 80,
            valvePulseMaxOpenMs: 900,
            routingSettlingMs: 1500,
            routingRetargetMinMs: 3000,
            phasePowerPercent: [70, 60, 60, 50]
        },
        distillation: {
            headsVolume: 150,
            targetVolume: 3000,
            speed: 1200,
            endTemp: 96.0
        },
        mashing: {
            steps: [
                { temperature: 38.0, duration: 20, name: 'Кислотная пауза' },
                { temperature: 52.0, duration: 20, name: 'Белковая пауза' },
                { temperature: 63.0, duration: 40, name: 'Мальтозная пауза' },
                { temperature: 72.0, duration: 20, name: 'Осахаривание' },
                { temperature: 78.0, duration: 10, name: 'Мэш-аут' }
            ]
        },
        temperatures: {
            maxCube: 98.0,
            maxColumn: 82.0,
            headsEnd: 78.5,
            bodyStart: 78.0,
            bodyEnd: 85.0
        },
        safety: {
            maxRuntime: 720,
            waterFlowMin: 2.0,
            pressureMax: 50
        }
    }
};

function cloneProfileDraft(value) {

    return JSON.parse(JSON.stringify(value));

}

function clampNumber(value, min, max, fallback, digits = null) {

    const numeric = Number(value);
    if (!Number.isFinite(numeric)) {
        return fallback;
    }

    let result = Math.max(min, Math.min(max, numeric));
    if (digits !== null) {
        const multiplier = 10 ** digits;
        result = Math.round(result * multiplier) / multiplier;
    }

    return result;

}

function setInputValue(id, value) {

    const element = document.getElementById(id);
    if (element) {
        element.value = value ?? '';
    }

}

function setCheckboxValue(id, value) {

    const element = document.getElementById(id);
    if (element) {
        element.checked = Boolean(value);
    }

}

function getInputValue(id, fallback = '') {

    const element = document.getElementById(id);
    return element ? element.value : fallback;

}

function getCheckboxValue(id, fallback = false) {

    const element = document.getElementById(id);
    return element ? element.checked : fallback;

}

function normalizeMashSteps(steps, fallback = PROFILE_FORM_DEFAULTS.parameters.mashing.steps) {

    const source = Array.isArray(steps) && steps.length ? steps : fallback;
    return source
        .slice(0, 10)
        .map((step, index) => {
            const temperature = clampNumber(step?.temperature, 20, 100, 0, 1);
            const duration = clampNumber(step?.duration, 1, 240, 0);
            const name = String(step?.name || '').trim() || `Шаг ${index + 1}`;
            if (!(temperature > 0) || !(duration > 0)) {
                return null;
            }
            return { temperature, duration, name };
        })
        .filter(Boolean);

}

function clearMashStepsContainer(containerId) {

    const container = document.getElementById(containerId);
    if (container) {
        container.innerHTML = '';
    }

}

function populateMashStepsContainer(containerId, steps) {

    const container = document.getElementById(containerId);
    if (!container) {
        return;
    }

    clearMashStepsContainer(containerId);
    normalizeMashSteps(steps).forEach((step) => {
        container.appendChild(createStepRow({
            mode: 'mash',
            temperature: step.temperature,
            duration: step.duration,
            name: step.name
        }));
    });

}

function ensureProfileMashStepsInitialized() {

    const category = String(getInputValue('profile-category', 'rectification')).trim() || 'rectification';
    if (category !== 'mashing') {
        return;
    }

    const steps = readStepsFromUI('profile-mash-steps', 'mash');
    if (steps.length > 0) {
        return;
    }

    const currentPanelSteps = readStepsFromUI('mash-steps', 'mash');
    populateMashStepsContainer(
        'profile-mash-steps',
        currentPanelSteps.length ? currentPanelSteps : PROFILE_FORM_DEFAULTS.parameters.mashing.steps
    );

}

function applyMashingProfileToControlPanel(profile) {

    const steps = normalizeMashSteps(profile?.parameters?.mashing?.steps);
    const name = String(profile?.metadata?.name || profile?.name || 'Затирка').trim() || 'Затирка';
    setMashProfileUI(name, steps, profile?.id || '');

}

function normalizeProfileDraft(profile = null) {

    const draft = cloneProfileDraft(PROFILE_FORM_DEFAULTS);
    if (!profile || typeof profile !== 'object') {
        return draft;
    }

    draft.metadata.name = String(profile?.metadata?.name || profile?.name || '').trim();
    draft.metadata.description = String(profile?.metadata?.description || '').trim();
    draft.metadata.category = String(profile?.metadata?.category || profile?.category || 'rectification').trim() || 'rectification';
    draft.metadata.tags = Array.isArray(profile?.metadata?.tags)
        ? profile.metadata.tags.map((tag) => String(tag || '').trim()).filter(Boolean)
        : [];

    draft.parameters.mode = String(profile?.parameters?.mode || draft.metadata.category || 'rectification').trim() || draft.metadata.category;
    draft.parameters.model = String(profile?.parameters?.model || 'classic').trim() || 'classic';
    draft.parameters.heater.maxPower = clampNumber(profile?.parameters?.heater?.maxPower, 300, 10000, draft.parameters.heater.maxPower);
    draft.parameters.heater.autoMode = Boolean(profile?.parameters?.heater?.autoMode ?? draft.parameters.heater.autoMode);
    draft.parameters.heater.pidKp = clampNumber(profile?.parameters?.heater?.pidKp, 0, 100, draft.parameters.heater.pidKp, 2);
    draft.parameters.heater.pidKi = clampNumber(profile?.parameters?.heater?.pidKi, 0, 100, draft.parameters.heater.pidKi, 2);
    draft.parameters.heater.pidKd = clampNumber(profile?.parameters?.heater?.pidKd, 0, 100, draft.parameters.heater.pidKd, 2);
    draft.parameters.heater.boosterEnabled = Boolean(
        profile?.parameters?.heater?.boosterEnabled ?? draft.parameters.heater.boosterEnabled
    );
    draft.parameters.heater.boosterStopCubeTempC = clampNumber(
        profile?.parameters?.heater?.boosterStopCubeTempC,
        20,
        100,
        draft.parameters.heater.boosterStopCubeTempC,
        1
    );
    const rectificationProfile = profile?.parameters?.rectification && typeof profile.parameters.rectification === 'object'
        ? profile.parameters.rectification
        : {};
    const rectificationPhasePower = Array.isArray(rectificationProfile.phasePowerPercent)
        ? rectificationProfile.phasePowerPercent
        : [];

    draft.parameters.rectification.stabilizationMin = clampNumber(rectificationProfile.stabilizationMin, 1, 180, draft.parameters.rectification.stabilizationMin);
    draft.parameters.rectification.headsVolume = clampNumber(rectificationProfile.headsVolume, 1, 10000, draft.parameters.rectification.headsVolume);
    draft.parameters.rectification.bodyVolume = clampNumber(rectificationProfile.bodyVolume, 1, 50000, draft.parameters.rectification.bodyVolume);
    draft.parameters.rectification.tailsVolume = clampNumber(rectificationProfile.tailsVolume, 0, 20000, draft.parameters.rectification.tailsVolume);
    draft.parameters.rectification.headsSpeed = clampNumber(rectificationProfile.headsSpeed, 10, 2000, draft.parameters.rectification.headsSpeed);
    draft.parameters.rectification.bodySpeed = clampNumber(rectificationProfile.bodySpeed, 50, 3000, draft.parameters.rectification.bodySpeed);
    draft.parameters.rectification.tailsSpeed = clampNumber(rectificationProfile.tailsSpeed, 0, 3000, draft.parameters.rectification.tailsSpeed);
    draft.parameters.rectification.purgeMin = clampNumber(rectificationProfile.purgeMin, 1, 120, draft.parameters.rectification.purgeMin);
    draft.parameters.rectification.baroCorrectionEnabled = Boolean(
        rectificationProfile.baroCorrectionEnabled ?? draft.parameters.rectification.baroCorrectionEnabled
    );
    draft.parameters.rectification.takeoffBackendType = clampNumber(rectificationProfile.takeoffBackendType, 0, 2, draft.parameters.rectification.takeoffBackendType);
    draft.parameters.rectification.refluxMode = clampNumber(rectificationProfile.refluxMode, 0, 2, draft.parameters.rectification.refluxMode);
    draft.parameters.rectification.srTarget = clampNumber(
        rectificationProfile.srTarget ?? rectificationProfile.srRatio,
        0,
        20,
        draft.parameters.rectification.srTarget,
        1
    );
    draft.parameters.rectification.autonomousCycleSec = clampNumber(rectificationProfile.autonomousCycleSec, 1, 7200, draft.parameters.rectification.autonomousCycleSec);
    draft.parameters.rectification.autonomousPauseSec = clampNumber(rectificationProfile.autonomousPauseSec, 0, 7199, draft.parameters.rectification.autonomousPauseSec);
    if (draft.parameters.rectification.autonomousPauseSec >= draft.parameters.rectification.autonomousCycleSec) {
        draft.parameters.rectification.autonomousPauseSec = Math.max(0, draft.parameters.rectification.autonomousCycleSec - 1);
    }
    draft.parameters.rectification.chimAutoPercent = clampNumber(rectificationProfile.chimAutoPercent, 0, 200, draft.parameters.rectification.chimAutoPercent, 1);
    draft.parameters.rectification.chimTimePerH = clampNumber(rectificationProfile.chimTimePerH, -2000, 2000, draft.parameters.rectification.chimTimePerH, 0);
    draft.parameters.rectification.chimBegPercent = clampNumber(rectificationProfile.chimBegPercent, -100, 200, draft.parameters.rectification.chimBegPercent, 1);
      draft.parameters.rectification.chimMinPercent = clampNumber(rectificationProfile.chimMinPercent, 0, 100, draft.parameters.rectification.chimMinPercent, 1);
      draft.parameters.rectification.usePbMode = clampNumber(rectificationProfile.usePbMode, 0, 3, draft.parameters.rectification.usePbMode);
      draft.parameters.rectification.timpPbMs = clampNumber(rectificationProfile.timpPbMs, 0, 600000, draft.parameters.rectification.timpPbMs);
      draft.parameters.rectification.valvePulsePeriodMs = clampNumber(rectificationProfile.valvePulsePeriodMs, 100, 5000, draft.parameters.rectification.valvePulsePeriodMs);
      draft.parameters.rectification.valvePulseMinOpenMs = clampNumber(rectificationProfile.valvePulseMinOpenMs, 0, draft.parameters.rectification.valvePulsePeriodMs, draft.parameters.rectification.valvePulseMinOpenMs);
      draft.parameters.rectification.valvePulseMaxOpenMs = clampNumber(rectificationProfile.valvePulseMaxOpenMs, draft.parameters.rectification.valvePulseMinOpenMs, draft.parameters.rectification.valvePulsePeriodMs, draft.parameters.rectification.valvePulseMaxOpenMs);
      draft.parameters.rectification.routingSettlingMs = clampNumber(rectificationProfile.routingSettlingMs, 0, 10000, draft.parameters.rectification.routingSettlingMs);
      draft.parameters.rectification.routingRetargetMinMs = clampNumber(rectificationProfile.routingRetargetMinMs, 0, 30000, draft.parameters.rectification.routingRetargetMinMs);
      draft.parameters.rectification.phasePowerPercent = [
        clampNumber(rectificationProfile.phasePowerStabilization ?? rectificationPhasePower[0], 1, 100, draft.parameters.rectification.phasePowerPercent[0]),
        clampNumber(rectificationProfile.phasePowerHeads ?? rectificationPhasePower[1], 1, 100, draft.parameters.rectification.phasePowerPercent[1]),
        clampNumber(rectificationProfile.phasePowerBody ?? rectificationPhasePower[2], 1, 100, draft.parameters.rectification.phasePowerPercent[2]),
        clampNumber(rectificationProfile.phasePowerTails ?? rectificationPhasePower[3], 1, 100, draft.parameters.rectification.phasePowerPercent[3])
    ];
    draft.parameters.distillation.headsVolume = clampNumber(profile?.parameters?.distillation?.headsVolume, 0, 10000, draft.parameters.distillation.headsVolume);
    draft.parameters.distillation.targetVolume = clampNumber(profile?.parameters?.distillation?.targetVolume, 1, 50000, draft.parameters.distillation.targetVolume);
    draft.parameters.distillation.speed = clampNumber(profile?.parameters?.distillation?.speed, 50, 120000, draft.parameters.distillation.speed);
    draft.parameters.distillation.endTemp = clampNumber(profile?.parameters?.distillation?.endTemp, 50, 110, draft.parameters.distillation.endTemp, 1);
    draft.parameters.mashing.steps = normalizeMashSteps(
        profile?.parameters?.mashing?.steps,
        draft.metadata.category === 'mashing' ? PROFILE_FORM_DEFAULTS.parameters.mashing.steps : []
    );
    draft.parameters.temperatures.maxCube = clampNumber(profile?.parameters?.temperatures?.maxCube, 50, 120, draft.parameters.temperatures.maxCube, 2);
    draft.parameters.temperatures.maxColumn = clampNumber(profile?.parameters?.temperatures?.maxColumn, 50, 110, draft.parameters.temperatures.maxColumn, 2);
    draft.parameters.temperatures.headsEnd = clampNumber(profile?.parameters?.temperatures?.headsEnd, 50, 110, draft.parameters.temperatures.headsEnd, 2);
    draft.parameters.temperatures.bodyStart = clampNumber(profile?.parameters?.temperatures?.bodyStart, 50, 110, draft.parameters.temperatures.bodyStart, 2);
    draft.parameters.temperatures.bodyEnd = clampNumber(profile?.parameters?.temperatures?.bodyEnd, 50, 120, draft.parameters.temperatures.bodyEnd, 2);
    draft.parameters.safety.maxRuntime = clampNumber(profile?.parameters?.safety?.maxRuntime, 10, 5000, draft.parameters.safety.maxRuntime);
    draft.parameters.safety.waterFlowMin = clampNumber(profile?.parameters?.safety?.waterFlowMin, 0, 20, draft.parameters.safety.waterFlowMin, 1);
    draft.parameters.safety.pressureMax = clampNumber(profile?.parameters?.safety?.pressureMax, 5, 200, draft.parameters.safety.pressureMax);

    return draft;

}

async function buildProfileDraftFromSystem(category = 'rectification') {

    const draft = normalizeProfileDraft(null);
    draft.metadata.category = category;
    draft.parameters.mode = category;
    if (category === 'mashing') {
        const currentMashSteps = readStepsFromUI('mash-steps', 'mash');
        draft.parameters.mashing.steps = normalizeMashSteps(currentMashSteps);
    }

    try {
        const [equipmentResponse, safetyResponse, rectResponse, statusResponse] = await Promise.all([
            fetch('/api/settings/equipment'),
            fetch('/api/settings/safety'),
            fetch('/api/settings/rect'),
            fetch('/api/status')
        ]);

        const equipment = equipmentResponse.ok ? await equipmentResponse.json() : {};
        const safety = safetyResponse.ok ? await safetyResponse.json() : {};
        const rect = rectResponse.ok ? await rectResponse.json() : {};
        const status = statusResponse.ok ? await statusResponse.json() : {};
        const distillation = status?.distillation && typeof status.distillation === 'object'
            ? status.distillation
            : {};
        const activeProfileTemps = status?.activeProfile?.baseTemperatures && typeof status.activeProfile.baseTemperatures === 'object'
            ? status.activeProfile.baseTemperatures
            : {};

        const feedVolumeL = clampNumber(rect.feedVolumeL, 1, 250, 25, 1);
        const feedAbvPercent = clampNumber(rect.feedAbvPercent, 1, 96, 40, 1);
        const absoluteAlcoholMl = Math.max(0, feedVolumeL * 10 * feedAbvPercent);
        const headsPercent = clampNumber(rect.headsPercent, 0, 40, 8, 1);
        const bodyPercent = clampNumber(rect.bodyPercent, 0, 100, 84, 1);
        const tailsPercent = clampNumber(rect.tailsPercent, 0, 100, 8, 1);

        draft.parameters.heater.maxPower = clampNumber(equipment.heaterPowerW, 300, 10000, draft.parameters.heater.maxPower);
        draft.parameters.heater.boosterEnabled = Boolean(equipment.boosterHeaterEnabled ?? draft.parameters.heater.boosterEnabled);
        draft.parameters.heater.boosterStopCubeTempC = clampNumber(
            equipment.boosterHeaterStopCubeTempC,
            20,
            100,
            draft.parameters.heater.boosterStopCubeTempC,
            1
        );
        draft.parameters.rectification.stabilizationMin = clampNumber(rect.stabilizationMin, 1, 180, draft.parameters.rectification.stabilizationMin);
        draft.parameters.rectification.purgeMin = clampNumber(rect.purgeMin, 1, 120, draft.parameters.rectification.purgeMin);
        draft.parameters.rectification.headsSpeed = clampNumber(rect.headsSpeedMlHKw, 10, 2000, draft.parameters.rectification.headsSpeed);
        draft.parameters.rectification.bodySpeed = clampNumber(rect.bodySpeedMlHKw, 50, 3000, draft.parameters.rectification.bodySpeed);
        draft.parameters.rectification.tailsSpeed = clampNumber(rect.bodySpeedMlHKw, 50, 3000, draft.parameters.rectification.bodySpeed * 0.6, 0);
        draft.parameters.rectification.headsVolume = clampNumber(absoluteAlcoholMl * headsPercent / 100, 1, 10000, draft.parameters.rectification.headsVolume);
        draft.parameters.rectification.bodyVolume = clampNumber(absoluteAlcoholMl * bodyPercent / 100, 1, 50000, draft.parameters.rectification.bodyVolume);
        draft.parameters.rectification.tailsVolume = clampNumber(absoluteAlcoholMl * tailsPercent / 100, 0, 20000, draft.parameters.rectification.tailsVolume);
        draft.parameters.rectification.baroCorrectionEnabled = Boolean(rect.baroCorrectionEnabled ?? draft.parameters.rectification.baroCorrectionEnabled);
        draft.parameters.rectification.takeoffBackendType = clampNumber(rect.takeoffBackendType, 0, 2, draft.parameters.rectification.takeoffBackendType);
        draft.parameters.rectification.refluxMode = clampNumber(rect.refluxMode, 0, 2, draft.parameters.rectification.refluxMode);
        draft.parameters.rectification.srTarget = clampNumber(rect.srRatio, 0, 20, draft.parameters.rectification.srTarget, 1);
        draft.parameters.rectification.autonomousCycleSec = clampNumber(rect.autonomousCycleSec, 1, 7200, draft.parameters.rectification.autonomousCycleSec);
        draft.parameters.rectification.autonomousPauseSec = clampNumber(rect.autonomousPauseSec, 0, 7199, draft.parameters.rectification.autonomousPauseSec);
        draft.parameters.rectification.chimAutoPercent = clampNumber(rect.chimAutoPercent, 0, 200, draft.parameters.rectification.chimAutoPercent, 1);
        draft.parameters.rectification.chimTimePerH = clampNumber(rect.chimTimePerH, -2000, 2000, draft.parameters.rectification.chimTimePerH);
        draft.parameters.rectification.chimBegPercent = clampNumber(rect.chimBegPercent, -100, 200, draft.parameters.rectification.chimBegPercent, 1);
        draft.parameters.rectification.chimMinPercent = clampNumber(rect.chimMinPercent, 0, 100, draft.parameters.rectification.chimMinPercent, 1);
        draft.parameters.rectification.usePbMode = clampNumber(rect.usePbMode, 0, 3, draft.parameters.rectification.usePbMode);
        draft.parameters.rectification.timpPbMs = clampNumber(rect.timpPbMs, 0, 600000, draft.parameters.rectification.timpPbMs);
        draft.parameters.rectification.valvePulsePeriodMs = clampNumber(rect.valvePulsePeriodMs, 100, 5000, draft.parameters.rectification.valvePulsePeriodMs);
        draft.parameters.rectification.valvePulseMinOpenMs = clampNumber(rect.valvePulseMinOpenMs, 0, draft.parameters.rectification.valvePulsePeriodMs, draft.parameters.rectification.valvePulseMinOpenMs);
        draft.parameters.rectification.valvePulseMaxOpenMs = clampNumber(rect.valvePulseMaxOpenMs, draft.parameters.rectification.valvePulseMinOpenMs, draft.parameters.rectification.valvePulsePeriodMs, draft.parameters.rectification.valvePulseMaxOpenMs);
        draft.parameters.rectification.routingSettlingMs = clampNumber(rect.routingSettlingMs, 0, 10000, draft.parameters.rectification.routingSettlingMs);
        draft.parameters.rectification.routingRetargetMinMs = clampNumber(rect.routingRetargetMinMs, 0, 30000, draft.parameters.rectification.routingRetargetMinMs);
        draft.parameters.rectification.phasePowerPercent = [
            clampNumber(rect.phasePowerStabilization, 1, 100, draft.parameters.rectification.phasePowerPercent[0]),
            clampNumber(rect.phasePowerHeads, 1, 100, draft.parameters.rectification.phasePowerPercent[1]),
            clampNumber(rect.phasePowerBody, 1, 100, draft.parameters.rectification.phasePowerPercent[2]),
            clampNumber(rect.phasePowerTails, 1, 100, draft.parameters.rectification.phasePowerPercent[3])
        ];
        if (draft.parameters.rectification.autonomousPauseSec >= draft.parameters.rectification.autonomousCycleSec) {
            draft.parameters.rectification.autonomousPauseSec = Math.max(0, draft.parameters.rectification.autonomousCycleSec - 1);
        }
        draft.parameters.distillation.headsVolume = clampNumber(distillation.headsVolumeMl, 0, 10000, draft.parameters.distillation.headsVolume);
        draft.parameters.distillation.targetVolume = clampNumber(distillation.targetVolumeMl, 1, 50000, draft.parameters.distillation.targetVolume);
        draft.parameters.distillation.speed = clampNumber(distillation.speedMlH, 50, 120000, draft.parameters.distillation.speed);
        draft.parameters.distillation.endTemp = clampNumber(distillation.endTempC, 50, 110, draft.parameters.distillation.endTemp, 1);
        draft.parameters.temperatures.maxCube = clampNumber(activeProfileTemps.maxCube, 50, 120, draft.parameters.temperatures.maxCube, 2);
        draft.parameters.temperatures.maxColumn = clampNumber(activeProfileTemps.maxColumn, 50, 110, draft.parameters.temperatures.maxColumn, 2);
        draft.parameters.temperatures.headsEnd = clampNumber(activeProfileTemps.headsEnd, 50, 110, draft.parameters.temperatures.headsEnd, 2);
        draft.parameters.temperatures.bodyStart = clampNumber(activeProfileTemps.bodyStart, 50, 110, draft.parameters.temperatures.bodyStart, 2);
        draft.parameters.temperatures.bodyEnd = clampNumber(activeProfileTemps.bodyEnd, 50, 120, draft.parameters.temperatures.bodyEnd, 2);
        draft.parameters.safety.pressureMax = clampNumber(safety.pressureMaxMmHg, 5, 200, draft.parameters.safety.pressureMax);
    } catch (error) {
        console.warn('Не удалось полностью собрать текущие системные настройки для нового профиля:', error);
    }

    return draft;

}

function populateProfileForm(profile) {

    const draft = normalizeProfileDraft(profile);

    setInputValue('profile-name', draft.metadata.name);
    setInputValue('profile-description', draft.metadata.description);
    setInputValue('profile-category', draft.metadata.category);
    setInputValue('profile-tags', draft.metadata.tags.join(', '));
    setInputValue('profile-model', draft.parameters.model);
    setInputValue('profile-heater-max-power', draft.parameters.heater.maxPower);
    setCheckboxValue('profile-heater-auto-mode', draft.parameters.heater.autoMode);
    setInputValue('profile-heater-pid-kp', draft.parameters.heater.pidKp);
    setInputValue('profile-heater-pid-ki', draft.parameters.heater.pidKi);
    setInputValue('profile-heater-pid-kd', draft.parameters.heater.pidKd);
    setCheckboxValue('profile-heater-booster-enabled', draft.parameters.heater.boosterEnabled);
    setInputValue('profile-heater-booster-stop-cube-temp', draft.parameters.heater.boosterStopCubeTempC);
    setInputValue('profile-rect-stabilization', draft.parameters.rectification.stabilizationMin);
    setInputValue('profile-rect-purge', draft.parameters.rectification.purgeMin);
    setInputValue('profile-rect-heads-volume', draft.parameters.rectification.headsVolume);
    setInputValue('profile-rect-body-volume', draft.parameters.rectification.bodyVolume);
    setInputValue('profile-rect-tails-volume', draft.parameters.rectification.tailsVolume);
    setInputValue('profile-rect-heads-speed', draft.parameters.rectification.headsSpeed);
    setInputValue('profile-rect-body-speed', draft.parameters.rectification.bodySpeed);
    setInputValue('profile-rect-tails-speed', draft.parameters.rectification.tailsSpeed);
    setCheckboxValue('profile-rect-baro-correction-enabled', draft.parameters.rectification.baroCorrectionEnabled);
    setInputValue('profile-rect-takeoff-backend-type', draft.parameters.rectification.takeoffBackendType);
    setInputValue('profile-rect-reflux-mode', draft.parameters.rectification.refluxMode);
    setInputValue('profile-rect-sr-target', draft.parameters.rectification.srTarget);
    setInputValue('profile-rect-autonomous-cycle-sec', draft.parameters.rectification.autonomousCycleSec);
    setInputValue('profile-rect-autonomous-pause-sec', draft.parameters.rectification.autonomousPauseSec);
    setInputValue('profile-rect-chim-auto-percent', draft.parameters.rectification.chimAutoPercent);
    setInputValue('profile-rect-chim-time-per-h', draft.parameters.rectification.chimTimePerH);
    setInputValue('profile-rect-chim-beg-percent', draft.parameters.rectification.chimBegPercent);
    setInputValue('profile-rect-chim-min-percent', draft.parameters.rectification.chimMinPercent);
    setInputValue('profile-rect-use-pb-mode', draft.parameters.rectification.usePbMode);
    setInputValue('profile-rect-timp-pb-ms', draft.parameters.rectification.timpPbMs);
    setInputValue('profile-rect-valve-pulse-period-ms', draft.parameters.rectification.valvePulsePeriodMs);
    setInputValue('profile-rect-valve-pulse-min-open-ms', draft.parameters.rectification.valvePulseMinOpenMs);
    setInputValue('profile-rect-valve-pulse-max-open-ms', draft.parameters.rectification.valvePulseMaxOpenMs);
    setInputValue('profile-rect-routing-settling-ms', draft.parameters.rectification.routingSettlingMs);
    setInputValue('profile-rect-routing-retarget-min-ms', draft.parameters.rectification.routingRetargetMinMs);
    setInputValue('profile-rect-phase-power-stabilization', draft.parameters.rectification.phasePowerPercent[0]);
    setInputValue('profile-rect-phase-power-heads', draft.parameters.rectification.phasePowerPercent[1]);
    setInputValue('profile-rect-phase-power-body', draft.parameters.rectification.phasePowerPercent[2]);
    setInputValue('profile-rect-phase-power-tails', draft.parameters.rectification.phasePowerPercent[3]);
    setInputValue('profile-dist-heads-volume', draft.parameters.distillation.headsVolume);
    setInputValue('profile-dist-target-volume', draft.parameters.distillation.targetVolume);
    setInputValue('profile-dist-speed', draft.parameters.distillation.speed);
    setInputValue('profile-dist-end-temp', draft.parameters.distillation.endTemp);
    populateMashStepsContainer('profile-mash-steps', draft.parameters.mashing.steps);
    setInputValue('profile-temp-max-cube', draft.parameters.temperatures.maxCube);
    setInputValue('profile-temp-max-column', draft.parameters.temperatures.maxColumn);
    setInputValue('profile-temp-heads-end', draft.parameters.temperatures.headsEnd);
    setInputValue('profile-temp-body-start', draft.parameters.temperatures.bodyStart);
    setInputValue('profile-temp-body-end', draft.parameters.temperatures.bodyEnd);
    setInputValue('profile-safety-max-runtime', draft.parameters.safety.maxRuntime);
    setInputValue('profile-safety-water-flow-min', draft.parameters.safety.waterFlowMin);
    setInputValue('profile-safety-pressure-max', draft.parameters.safety.pressureMax);
    toggleProfileCategoryFields(draft.metadata.category);
    renderProfileEditorSummary(draft);
    syncProfileModalDeleteButton();

}

function ensureProfileFormLiveBindings() {

    if (profileFormLiveBindingsReady) {
        return;
    }

    const modal = document.getElementById('profile-modal');
    if (!modal) {
        return;
    }

    const refreshSummary = () => {
        renderProfileEditorSummary(collectProfileFromForm());
        syncProfileModalDeleteButton();
    };

    modal.addEventListener('input', (event) => {
        if (event.target instanceof HTMLElement && event.target.closest('#profile-editor-summary')) {
            return;
        }
        refreshSummary();
    });

    modal.addEventListener('change', refreshSummary);
    profileFormLiveBindingsReady = true;

}

function buildProfileEditorMetrics(draft) {

    const category = draft?.metadata?.category || 'rectification';
    const metrics = [
        { label: 'Категория', value: category === 'mashing' ? 'Затирка' : category === 'distillation' ? 'Дистилляция' : 'Ректификация' },
        { label: 'Мощность', value: `${Number(draft?.parameters?.heater?.maxPower || 0).toFixed(0)} Вт` }
    ];

    if (category === 'rectification') {
        metrics.push(
            { label: 'Стабилизация', value: `${Number(draft?.parameters?.rectification?.stabilizationMin || 0).toFixed(0)} мин` },
            { label: 'Тело', value: `${Number(draft?.parameters?.rectification?.bodyVolume || 0).toFixed(0)} мл` },
            { label: 'Скорость тела', value: `${Number(draft?.parameters?.rectification?.bodySpeed || 0).toFixed(0)} мл/ч/кВт` },
            { label: 'Давление max', value: `${Number(draft?.parameters?.safety?.pressureMax || 0).toFixed(0)} мм` }
        );
    } else if (category === 'distillation') {
        metrics.push(
            { label: 'Целевой объём', value: `${Number(draft?.parameters?.distillation?.targetVolume || 0).toFixed(0)} мл` },
            { label: 'Скорость', value: `${Number(draft?.parameters?.distillation?.speed || 0).toFixed(0)} мл/ч` },
            { label: 'Финиш', value: `${Number(draft?.parameters?.distillation?.endTemp || 0).toFixed(1)} °C` },
            { label: 'Давление max', value: `${Number(draft?.parameters?.safety?.pressureMax || 0).toFixed(0)} мм` }
        );
    } else if (category === 'mashing') {
        const steps = Array.isArray(draft?.parameters?.mashing?.steps) ? draft.parameters.mashing.steps : [];
        const totalMinutes = steps.reduce((sum, step) => sum + Number(step?.duration || 0), 0);
        metrics.push(
            { label: 'Шагов', value: `${steps.length}` },
            { label: 'Общая длительность', value: `${totalMinutes.toFixed(0)} мин` },
            { label: 'Старт / финиш', value: steps.length ? `${Number(steps[0]?.temperature || 0).toFixed(1)} → ${Number(steps[steps.length - 1]?.temperature || 0).toFixed(1)} °C` : 'Нет шагов' },
            { label: 'Макс. куб', value: `${Number(draft?.parameters?.temperatures?.maxCube || 0).toFixed(1)} °C` }
        );
    }

    return metrics;

}

function renderProfileEditorSummary(profileDraft = null) {

    const host = document.getElementById('profile-editor-summary');
    if (!host) {
        return;
    }

    const draft = profileDraft || collectProfileFromForm();
    const compatibility = getProfileCompatibilityBadge(draft);
    const metrics = buildProfileEditorMetrics(draft);
    const notes = [];

    if (!draft.metadata.name) {
        notes.push('Добавьте понятное имя профиля, чтобы потом не путаться в истории и compare-view.');
    }

    if (draft.metadata.category === 'mashing' && (!Array.isArray(draft?.parameters?.mashing?.steps) || draft.parameters.mashing.steps.length === 0)) {
        notes.push('Для профиля затирки нужен хотя бы один температурный шаг.');
    }

    if (compatibility.tone === 'warn' && compatibility.detail) {
        notes.push(compatibility.detail);
    }

    notes.push(currentProfileId
        ? 'После сохранения обновится текущая пользовательская версия профиля.'
        : 'После сохранения появится новый пользовательский профиль без затрагивания встроенных рецептов.');

    host.innerHTML = `
        <div class="profile-import-meta">
            <div class="profile-import-meta-title">${draft.metadata.name || 'Новый профиль'}</div>
            <div class="profile-import-meta-copy">
                ${draft.metadata.description || 'Профиль собирается из текущих полей редактора. Здесь видно, что именно получится перед сохранением.'}
            </div>
        </div>
        <div class="profile-import-card ${compatibility.tone === 'warn' ? 'is-warning' : compatibility.tone === 'good' ? 'is-good' : ''}">
            <div class="profile-import-card-head">
                <strong>Совместимость с текущим железом</strong>
                <span class="profile-import-badge ${compatibility.tone === 'warn' ? 'is-warning' : compatibility.tone === 'good' ? 'is-good' : ''}">${compatibility.label}</span>
            </div>
            ${compatibility.detail ? `<div class="profile-import-note-block"><p>${escapeHtml(compatibility.detail)}</p></div>` : ''}
        </div>
        <div class="profile-editor-summary-grid">
            ${metrics.map((item) => `
                <div class="profile-editor-summary-metric">
                    <span>${escapeHtml(item.label)}</span>
                    <strong>${escapeHtml(item.value)}</strong>
                </div>
            `).join('')}
        </div>
        <div class="profile-import-card ${notes.length > 2 ? 'is-warning' : ''}">
            <div class="profile-import-card-head">
                <strong>Что проверить перед сохранением</strong>
                <span class="profile-import-badge ${notes.length > 2 ? 'is-warning' : 'is-good'}">${notes.length > 2 ? 'Проверить' : 'Готово'}</span>
            </div>
            <ul class="profile-import-list">
                ${notes.map((item) => `<li>${escapeHtml(item)}</li>`).join('')}
            </ul>
        </div>
    `;

}

function syncProfileModalDeleteButton() {

    const deleteBtn = document.getElementById('profile-modal-delete-btn');
    if (!deleteBtn) {
        return;
    }

    const showDelete = Boolean(currentProfileId) && !currentProfileIsBuiltin;
    deleteBtn.style.display = showDelete ? '' : 'none';

}

  function collectProfileFromForm() {

      const category = String(getInputValue('profile-category', 'rectification')).trim() || 'rectification';
      const tagsStr = String(getInputValue('profile-tags', '')).trim();
      const valvePulsePeriodMs = clampNumber(getInputValue('profile-rect-valve-pulse-period-ms', 1000), 100, 5000, 1000);
      const valvePulseMinOpenMs = clampNumber(getInputValue('profile-rect-valve-pulse-min-open-ms', 80), 0, valvePulsePeriodMs, 80);
      const valvePulseMaxOpenMs = clampNumber(getInputValue('profile-rect-valve-pulse-max-open-ms', 900), valvePulseMinOpenMs, valvePulsePeriodMs, 900);

    return normalizeProfileDraft({
        metadata: {
            name: String(getInputValue('profile-name', '')).trim(),
            description: String(getInputValue('profile-description', '')).trim(),
            category,
            tags: tagsStr ? tagsStr.split(',').map((tag) => tag.trim()).filter(Boolean) : []
        },
        parameters: {
            mode: category,
            model: String(getInputValue('profile-model', 'classic')).trim() || 'classic',
            heater: {
                maxPower: clampNumber(getInputValue('profile-heater-max-power', 3000), 300, 10000, 3000),
                autoMode: getCheckboxValue('profile-heater-auto-mode', true),
                pidKp: clampNumber(getInputValue('profile-heater-pid-kp', 2), 0, 100, 2, 2),
                pidKi: clampNumber(getInputValue('profile-heater-pid-ki', 0.5), 0, 100, 0.5, 2),
                pidKd: clampNumber(getInputValue('profile-heater-pid-kd', 1), 0, 100, 1, 2),
                boosterEnabled: getCheckboxValue('profile-heater-booster-enabled', false),
                boosterStopCubeTempC: clampNumber(getInputValue('profile-heater-booster-stop-cube-temp', 78), 20, 100, 78, 1)
            },
            rectification: {
                stabilizationMin: clampNumber(getInputValue('profile-rect-stabilization', 30), 1, 180, 30),
                headsVolume: clampNumber(getInputValue('profile-rect-heads-volume', 300), 1, 10000, 300),
                bodyVolume: clampNumber(getInputValue('profile-rect-body-volume', 3200), 1, 50000, 3200),
                tailsVolume: clampNumber(getInputValue('profile-rect-tails-volume', 300), 0, 20000, 300),
                headsSpeed: clampNumber(getInputValue('profile-rect-heads-speed', 300), 10, 2000, 300),
                bodySpeed: clampNumber(getInputValue('profile-rect-body-speed', 600), 50, 3000, 600),
                tailsSpeed: clampNumber(getInputValue('profile-rect-tails-speed', 360), 0, 3000, 360),
                purgeMin: clampNumber(getInputValue('profile-rect-purge', 5), 1, 120, 5),
                baroCorrectionEnabled: getCheckboxValue('profile-rect-baro-correction-enabled', true),
                takeoffBackendType: clampNumber(getInputValue('profile-rect-takeoff-backend-type', 0), 0, 2, 0),
                refluxMode: clampNumber(getInputValue('profile-rect-reflux-mode', 0), 0, 2, 0),
                srTarget: clampNumber(getInputValue('profile-rect-sr-target', 0), 0, 20, 0, 1),
                autonomousCycleSec: clampNumber(getInputValue('profile-rect-autonomous-cycle-sec', 900), 1, 7200, 900),
                autonomousPauseSec: clampNumber(getInputValue('profile-rect-autonomous-pause-sec', 90), 0, 7199, 90),
                chimAutoPercent: clampNumber(getInputValue('profile-rect-chim-auto-percent', 0), 0, 200, 0, 1),
                chimTimePerH: clampNumber(getInputValue('profile-rect-chim-time-per-h', 0), -2000, 2000, 0),
                chimBegPercent: clampNumber(getInputValue('profile-rect-chim-beg-percent', 0), -100, 200, 0, 1),
                  chimMinPercent: clampNumber(getInputValue('profile-rect-chim-min-percent', 35), 0, 100, 35, 1),
                  usePbMode: clampNumber(getInputValue('profile-rect-use-pb-mode', 0), 0, 3, 0),
                  timpPbMs: clampNumber(getInputValue('profile-rect-timp-pb-ms', 15000), 0, 600000, 15000),
                  valvePulsePeriodMs,
                  valvePulseMinOpenMs,
                  valvePulseMaxOpenMs,
                  routingSettlingMs: clampNumber(getInputValue('profile-rect-routing-settling-ms', 1500), 0, 10000, 1500),
                  routingRetargetMinMs: clampNumber(getInputValue('profile-rect-routing-retarget-min-ms', 3000), 0, 30000, 3000),
                  phasePowerPercent: [
                    clampNumber(getInputValue('profile-rect-phase-power-stabilization', 70), 1, 100, 70),
                    clampNumber(getInputValue('profile-rect-phase-power-heads', 60), 1, 100, 60),
                    clampNumber(getInputValue('profile-rect-phase-power-body', 60), 1, 100, 60),
                    clampNumber(getInputValue('profile-rect-phase-power-tails', 50), 1, 100, 50)
                ]
            },
            distillation: {
                headsVolume: clampNumber(getInputValue('profile-dist-heads-volume', 150), 0, 10000, 150),
                targetVolume: clampNumber(getInputValue('profile-dist-target-volume', 3000), 1, 50000, 3000),
                speed: clampNumber(getInputValue('profile-dist-speed', 1200), 50, 120000, 1200),
                endTemp: clampNumber(getInputValue('profile-dist-end-temp', 96), 50, 110, 96, 1)
            },
            mashing: {
                steps: normalizeMashSteps(readStepsFromUI('profile-mash-steps', 'mash'), [])
            },
            temperatures: {
                maxCube: clampNumber(getInputValue('profile-temp-max-cube', 98), 50, 120, 98, 2),
                maxColumn: clampNumber(getInputValue('profile-temp-max-column', 82), 50, 110, 82, 2),
                headsEnd: clampNumber(getInputValue('profile-temp-heads-end', 78.5), 50, 110, 78.5, 2),
                bodyStart: clampNumber(getInputValue('profile-temp-body-start', 78), 50, 110, 78, 2),
                bodyEnd: clampNumber(getInputValue('profile-temp-body-end', 85), 50, 120, 85, 2)
            },
            safety: {
                maxRuntime: clampNumber(getInputValue('profile-safety-max-runtime', 720), 10, 5000, 720),
                waterFlowMin: clampNumber(getInputValue('profile-safety-water-flow-min', 2), 0, 20, 2, 1),
                pressureMax: clampNumber(getInputValue('profile-safety-pressure-max', 50), 5, 200, 50)
            }
        }
    });

}

export function toggleProfileCategoryFields(category = null) {

    const selectedCategory = String(category || getInputValue('profile-category', 'rectification')).trim() || 'rectification';
    document.querySelectorAll('[data-profile-category-block]').forEach((element) => {
        const blockCategory = String(element.getAttribute('data-profile-category-block') || '').trim();
        element.style.display = blockCategory === selectedCategory ? '' : 'none';
    });
    ensureProfileMashStepsInitialized();
    renderProfileEditorSummary();

}

export function addProfileMashStep(step = {}) {

    const container = document.getElementById('profile-mash-steps');
    if (!container) {
        return;
    }

    container.appendChild(createStepRow({
        mode: 'mash',
        temperature: step.temperature,
        duration: step.duration,
        name: step.name
    }));

}

export function copyCurrentMashingToProfileForm() {

    populateMashStepsContainer('profile-mash-steps', readStepsFromUI('mash-steps', 'mash'));
    ensureProfileMashStepsInitialized();

}

// Показать модальное окно создания профиля

export async function showCreateProfileModal() {

    ensureProfileFormLiveBindings();
    setCurrentProfileId(null);
    currentProfileIsBuiltin = false;
    document.getElementById('profile-modal-title').textContent = 'Создание профиля';
    populateProfileForm(await buildProfileDraftFromSystem('rectification'));
    document.getElementById('profile-modal').style.display = 'flex';

}

function buildDuplicateProfileName(name) {

    const source = String(name || '').trim();
    if (!source) {
        return 'Новый профиль';
    }

    return /\(копия\)$/i.test(source) ? source : `${source} (копия)`;

}

export async function showDuplicateProfileModal(id) {

    try {
        ensureProfileFormLiveBindings();
        const response = await fetch(`/api/profiles/${id}`);
        if (!response.ok) {
            throw new Error('Не удалось загрузить профиль для копирования');
        }

        const profile = await response.json();
        const duplicated = normalizeProfileDraft(profile);
        duplicated.metadata.name = buildDuplicateProfileName(profile?.metadata?.name || profile?.name);

        setCurrentProfileId(null);
        currentProfileIsBuiltin = false;
        document.getElementById('profile-modal-title').textContent = 'Новый профиль на основе выбранного';
        populateProfileForm(duplicated);
        document.getElementById('profile-modal').style.display = 'flex';
    } catch (error) {
        console.error('Ошибка создания копии профиля:', error);
        alert('❌ Ошибка подготовки копии профиля');
    }

}



// Закрыть модальное окно создания

export function closeProfileModal() {

    document.getElementById('profile-modal').style.display = 'none';
    currentProfileIsBuiltin = false;
    setCurrentProfileId(null);
    syncProfileModalDeleteButton();

}



// Сохранить профиль

export async function saveProfile() {

    const profile = collectProfileFromForm();
    if (!profile.metadata.name) {
        alert('Пожалуйста, введите название профиля');
        return;
    }
    if (profile.metadata.category === 'mashing' && (!Array.isArray(profile.parameters?.mashing?.steps) || profile.parameters.mashing.steps.length === 0)) {
        alert('Для профиля затирки добавьте хотя бы один шаг.');
        return;
    }

    if (currentProfileId && currentProfileIsBuiltin) {
        alert('Встроенный профиль нельзя редактировать напрямую.');
        return;
    }

    try {
        const response = await fetch(currentProfileId ? `/api/profiles/${currentProfileId}` : '/api/profiles', {
            method: currentProfileId ? 'PUT' : 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(profile)
        });
        const data = await response.json();

        if (!response.ok || !data.success) {
            throw new Error(data.error || 'Неизвестная ошибка');
        }

        closeProfileModal();
        loadProfilesList();
        alert(currentProfileId ? '✅ Профиль успешно обновлён!' : '✅ Профиль успешно создан!');
    } catch (error) {
        console.error('Ошибка сохранения профиля:', error);
        alert(`❌ Ошибка сохранения профиля: ${error.message || 'Неизвестная ошибка'}`);
    }

}



// Просмотр профиля

export function viewProfile(id) {

    fetch(`/api/profiles/${id}`)

        .then(response => response.json())

        .then(profile => {

            showProfileViewModal(profile);

        })

        .catch(error => {

            console.error('Ошибка загрузки профиля:', error);

            alert('❌ Ошибка загрузки профиля');

        });

}

export async function showEditProfileModal(id) {

    try {
        ensureProfileFormLiveBindings();
        const response = await fetch(`/api/profiles/${id}`);
        if (!response.ok) {
            throw new Error('Не удалось загрузить профиль для редактирования');
        }

        const profile = await response.json();
        if (profile?.metadata?.isBuiltin) {
            alert('Встроенный профиль нельзя редактировать напрямую. Сначала создайте пользовательскую копию.');
            return;
        }

        setCurrentProfileId(profile.id);
        currentProfileIsBuiltin = Boolean(profile?.metadata?.isBuiltin);
        document.getElementById('profile-modal-title').textContent = 'Редактирование профиля';
        populateProfileForm(profile);
        document.getElementById('profile-modal').style.display = 'flex';
    } catch (error) {
        console.error('Ошибка открытия редактора профиля:', error);
        alert('❌ Ошибка загрузки профиля для редактирования');
    }

}

export function deleteCurrentProfileFromModal() {

    if (!currentProfileId || currentProfileIsBuiltin) {
        return;
    }

    const profileId = currentProfileId;
    closeProfileModal();
    deleteProfile(profileId);

}

export function editCurrentProfile() {

    if (!currentProfileId) {
        return;
    }

    closeProfileViewModal();
    void showEditProfileModal(currentProfileId);

}

export function duplicateCurrentProfile() {

    if (!currentProfileId) {
        return;
    }

    const profileId = currentProfileId;
    closeProfileViewModal();
    void showDuplicateProfileModal(profileId);

}

function formatProfileMinutes(seconds) {

    const value = Number(seconds || 0);
    return value > 0 ? `${Math.round(value / 60)} мин` : '—';

}

function formatProfileNumber(value, digits = 1, suffix = '') {

    const numeric = Number(value || 0);
    return numeric > 0 ? `${numeric.toFixed(digits)}${suffix}` : '—';

}

function formatProfileDateTime(timestamp) {

    const numeric = Number(timestamp || 0);
    if (!numeric) return '—';

    return new Date(numeric * 1000).toLocaleString('ru-RU');

}

function formatPackingType(value) {

    const labels = {
        spn_3_5: 'СПН 3.5',
        spn_4_0: 'СПН 4.0',
        raschig: 'Кольца Рашига',
        custom: 'Своя насадка'
    };

    return labels[String(value || '').trim()] || '—';

}

function formatTemperatureThreshold(baseValue, effectiveValue) {

    const base = Number(baseValue || 0);
    const effective = Number(effectiveValue || 0);
    if (!(base > 0)) {
        return '—';
    }
    if (!(effective > 0) || Math.abs(effective - base) < 0.01) {
        return `${base.toFixed(2)}°C`;
    }
    return `${base.toFixed(2)}°C → ${effective.toFixed(2)}°C`;

}

function buildProfileLoadWarning(profile) {

    const correction = profile?.baroCorrection || {};
    const effective = profile?.effectiveTemperatures || {};

    if (!correction.enabled || !correction.applicable || !correction.applied) {
        return '';
    }

    const delta = Number(correction.pressureDeltaMmHg || 0);
    const shift = Number(correction.appliedShiftC || 0);
    const absDelta = Math.abs(delta);
    const signedDelta = `${delta >= 0 ? '+' : ''}${delta.toFixed(1)}`;
    const signedShift = `${shift >= 0 ? '+' : ''}${shift.toFixed(2)}`;
    const severity = absDelta >= 12 ? 'Внимание' : 'Замечание';

    return `${severity}: профиль "${profile?.metadata?.name || profile?.id || ''}" валидирован при ${Number(correction.baselinePressureMmHg || 0).toFixed(1)} мм рт.ст., сейчас ${Number(correction.currentPressureMmHg || 0).toFixed(1)} мм рт.ст. (Δ ${signedDelta}). Мягкая барокоррекция сдвинет пороги на ${signedShift}°C: головы ${formatTemperatureThreshold(profile?.parameters?.temperatures?.headsEnd, effective.headsEnd)}, тело ${formatTemperatureThreshold(profile?.parameters?.temperatures?.bodyStart, effective.bodyStart)}, конец тела ${formatTemperatureThreshold(profile?.parameters?.temperatures?.bodyEnd, effective.bodyEnd)}.`;

}

function buildProfileTopologyWarning(profile) {
    const compatibility = getProfileCompatibilityBadge(profile);
    if (compatibility.tone !== 'warn') {
        return '';
    }

    return `Совместимость: ${compatibility.detail || 'Для этого профиля не хватает обязательных датчиков текущей комплектации.'}`;
}

function buildProfileEquipmentWarning(profile) {
    const mismatch = getProfileEquipmentMismatch(profile);
    if (!mismatch.known || !mismatch.changed) {
        return '';
    }

    const details = mismatch.messages.slice(0, 6).map((item) => `- ${item}`).join('\n');
    return `Профиль валидировался на другой конфигурации железа.\n${details}`;
}

async function fetchProfileLoadWarning(profileId) {

    try {
        const profileResponse = await fetch(`/api/profiles/${profileId}`);
        if (!profileResponse.ok) {
            return '';
        }

        return buildProfileLoadWarning(await profileResponse.json());
    } catch (error) {
        console.warn('Не удалось получить warning по validation context:', error);
        return '';
    }

}



// Показать модальное окно просмотра профиля

export function showProfileViewModal(profile) {

    setCurrentProfileId(profile.id);
    currentProfileIsBuiltin = Boolean(profile?.metadata?.isBuiltin);

    document.getElementById('profile-view-title').textContent = profile.metadata.name;



    const body = document.getElementById('profile-view-body');

    const catNames = {

        'rectification': 'Ректификация',

        'distillation': 'Дистилляция',

        'mashing': 'Затирка'

    };



    const learning = profile.learning || {};
    const validation = profile.validation || {};
    const baroCorrection = profile.baroCorrection || {};
    const effectiveTemperatures = profile.effectiveTemperatures || profile.parameters.temperatures || {};
    const lastSuccessfulRun = learning.lastSuccessfulRun || null;
    const advisorItems = Array.isArray(learning.lastAdvisorSnapshot?.items)
        ? learning.lastAdvisorSnapshot.items.slice(0, 3)
        : [];
    const compatibility = getProfileCompatibility(profile);
    const compatibilityBadge = getProfileCompatibilityBadge(profile);
    const mashingSteps = normalizeMashSteps(profile?.parameters?.mashing?.steps, []);
    let processSection = '';

    if (profile.metadata.category === 'rectification') {
        processSection = `
        <div class="modal-section">
            <div class="modal-section-title">⚙️ Параметры ректификации</div>
            <div class="modal-info-grid">
                <div><strong>Стабилизация:</strong> ${profile.parameters.rectification.stabilizationMin} мин</div>
                <div><strong>Продувка:</strong> ${profile.parameters.rectification.purgeMin} мин</div>
                <div><strong>Объём голов:</strong> ${profile.parameters.rectification.headsVolume} мл</div>
                <div><strong>Объём тела:</strong> ${profile.parameters.rectification.bodyVolume} мл</div>
                <div><strong>Объём хвостов:</strong> ${profile.parameters.rectification.tailsVolume} мл</div>
                <div><strong>Скорость голов:</strong> ${profile.parameters.rectification.headsSpeed} мл/ч/кВт</div>
                <div><strong>Скорость тела:</strong> ${profile.parameters.rectification.bodySpeed} мл/ч/кВт</div>
                <div><strong>Скорость хвостов:</strong> ${profile.parameters.rectification.tailsSpeed} мл/ч/кВт</div>
            </div>
        </div>`;
    } else if (profile.metadata.category === 'distillation') {
        processSection = `
        <div class="modal-section">
            <div class="modal-section-title">🥃 Параметры дистилляции</div>
            <div class="modal-info-grid">
                <div><strong>Головы:</strong> ${formatProfileNumber(profile.parameters.distillation.headsVolume, 0, ' мл')}</div>
                <div><strong>Целевой объём:</strong> ${formatProfileNumber(profile.parameters.distillation.targetVolume, 0, ' мл')}</div>
                <div><strong>Скорость:</strong> ${formatProfileNumber(profile.parameters.distillation.speed, 0, ' мл/ч')}</div>
                <div><strong>Температура завершения:</strong> ${formatProfileNumber(profile.parameters.distillation.endTemp, 1, '°C')}</div>
            </div>
        </div>`;
    } else if (profile.metadata.category === 'mashing') {
        processSection = `
        <div class="modal-section">
            <div class="modal-section-title">🌾 Рецепт затирки</div>
            ${mashingSteps.length ? `
                <div style="display: grid; gap: 10px;">
                    ${mashingSteps.map((step, index) => `
                        <div style="padding: 12px; border: 1px solid var(--border-color); border-radius: 10px; background: var(--bg-secondary);">
                            <div style="font-weight: 600; margin-bottom: 4px;">${step.name || `Шаг ${index + 1}`}</div>
                            <div style="color: var(--text-secondary);">
                                ${step.temperature.toFixed(1)}°C • ${step.duration} мин
                            </div>
                        </div>
                    `).join('')}
                </div>
            ` : '<div style="color: var(--text-secondary);">Шаги затирки пока не сохранены.</div>'}
        </div>`;
    }

    let html = `

        <div class="modal-section">

            <div class="modal-section-title">📋 Метаданные</div>

            <div class="modal-info-grid">

                <div><strong>Название:</strong> ${profile.metadata.name}</div>

                <div><strong>Категория:</strong> ${catNames[profile.metadata.category] || profile.metadata.category}</div>

                <div><strong>Описание:</strong> ${profile.metadata.description || '—'}</div>

                <div><strong>Автор:</strong> ${profile.metadata.author}</div>

                <div><strong>Теги:</strong> ${profile.metadata.tags.join(', ') || '—'}</div>

                <div><strong>Встроенный:</strong> ${profile.metadata.isBuiltin ? 'Да' : 'Нет'}</div>

                <div><strong>Совместимость:</strong> ${compatibilityBadge.label}</div>

            </div>

            ${compatibility.known && !compatibility.supported ? `
                <div style="margin-top: 12px; padding: 12px; border-radius: 10px; border: 1px solid rgba(216,119,6,0.35); background: rgba(245,158,11,0.12); color: var(--text-primary);">
                    <strong>Профиль не подходит для текущей комплектации.</strong>
                    <div style="margin-top: 6px; color: var(--text-secondary);">
                        ${compatibility.reason || 'Для этого профиля не хватает обязательных датчиков.'}
                    </div>
                </div>
            ` : ''}

        </div>



        <div class="modal-section">

            <div class="modal-section-title">🔥 Нагрев</div>

            <div class="modal-info-grid">

                <div><strong>Макс. мощность:</strong> ${formatProfileNumber(profile.parameters.heater.maxPower, 0, ' Вт')}</div>

                <div><strong>Авто-режим нагрева:</strong> ${profile.parameters.heater.autoMode ? 'Да' : 'Нет'}</div>

                <div><strong>Booster SSR:</strong> ${profile.parameters.heater.boosterEnabled ? 'Включён' : 'Отключён'}</div>

                <div><strong>Отключать booster при:</strong> ${formatProfileNumber(profile.parameters.heater.boosterStopCubeTempC, 1, '°C')}</div>

            </div>

        </div>



        ${processSection}



        <div class="modal-section">

            <div class="modal-section-title">🌡️ Температурные пороги</div>

            <div class="modal-info-grid">

                <div><strong>Макс. куб:</strong> ${formatTemperatureThreshold(profile.parameters.temperatures.maxCube, effectiveTemperatures.maxCube)}</div>

                <div><strong>Макс. колонна:</strong> ${formatTemperatureThreshold(profile.parameters.temperatures.maxColumn, effectiveTemperatures.maxColumn)}</div>

                <div><strong>Окончание голов:</strong> ${formatTemperatureThreshold(profile.parameters.temperatures.headsEnd, effectiveTemperatures.headsEnd)}</div>

                <div><strong>Начало тела:</strong> ${formatTemperatureThreshold(profile.parameters.temperatures.bodyStart, effectiveTemperatures.bodyStart)}</div>

                <div><strong>Окончание тела:</strong> ${formatTemperatureThreshold(profile.parameters.temperatures.bodyEnd, effectiveTemperatures.bodyEnd)}</div>

            </div>

            <div style="margin-top: 12px; color: var(--text-secondary);">
                ${baroCorrection.enabled
                    ? (baroCorrection.applicable
                        ? (baroCorrection.applied
                            ? `Барокоррекция активна: baseline ${formatProfileNumber(baroCorrection.baselinePressureMmHg, 1, ' мм рт.ст.')} • сейчас ${formatProfileNumber(baroCorrection.currentPressureMmHg, 1, ' мм рт.ст.')} • мягкий сдвиг ${baroCorrection.appliedShiftC >= 0 ? '+' : ''}${Number(baroCorrection.appliedShiftC || 0).toFixed(2)}°C.`
                            : (baroCorrection.note || 'Барокоррекция включена, но текущий сдвиг слишком мал для применения.'))
                        : (baroCorrection.note || 'Барокоррекция включена, но ещё нет достаточных данных для baseline.'))
                    : 'Барокоррекция выключена в настройках ректификации.'}
            </div>

        </div>



        <div class="modal-section">

            <div class="modal-section-title">📊 Статистика использования</div>

            <div class="modal-info-grid">

                <div><strong>Использований:</strong> ${profile.statistics.useCount}</div>

                <div><strong>Средняя длительность:</strong> ${formatProfileMinutes(profile.statistics.avgDuration)}</div>

                <div><strong>Средний выход:</strong> ${profile.statistics.avgYield} мл</div>

                <div><strong>Успешность:</strong> ${profile.statistics.successRate.toFixed(1)}%</div>

                <div><strong>Успешных прогонов:</strong> ${learning.successfulRuns || 0}</div>

                <div><strong>Неуспешных прогонов:</strong> ${learning.failedRuns || 0}</div>

            </div>

        </div>

        <div class="modal-section">

            <div class="modal-section-title">🧠 Learning Loop</div>

            <div class="modal-info-grid">

                <div><strong>Средняя энергия:</strong> ${formatProfileNumber(learning.avgEnergyUsed, 2, ' кВт·ч')}</div>

                <div><strong>Энергия на литр:</strong> ${formatProfileNumber(learning.avgEnergyPerLiter, 2, ' кВт·ч/л')}</div>

                <div><strong>Process health:</strong> ${formatProfileNumber((learning.avgProcessHealth || 0) * 100, 0, '%')}</div>

                <div><strong>Stability index:</strong> ${formatProfileNumber((learning.avgStabilityIndex || 0) * 100, 0, '%')}</div>

                <div><strong>Типовой финал куба:</strong> ${formatProfileNumber(learning.typicalCubeFinalTemp, 1, '°C')}</div>

                <div><strong>Типовой верх колонны:</strong> ${formatProfileNumber(learning.typicalColumnTopFinalTemp, 2, '°C')}</div>

            </div>

        </div>

        <div class="modal-section">

            <div class="modal-section-title">🧪 Условия последней валидации</div>

            <div class="modal-info-grid">

                <div><strong>Дата валидации:</strong> ${formatProfileDateTime(validation.validatedAt)}</div>

                <div><strong>ID baseline:</strong> ${validation.sourceProcessId || '—'}</div>

                <div><strong>Атм. давление:</strong> ${formatProfileNumber(validation.atmosphereMmHg, 1, ' мм рт.ст.')}</div>

                <div><strong>Высота колонны:</strong> ${formatProfileNumber(validation.columnHeightMm, 0, ' мм')}</div>

                <div><strong>Насадка:</strong> ${formatPackingType(validation.packingType)}</div>

                <div><strong>Коэфф. насадки:</strong> ${formatProfileNumber(validation.packingCoeff, 1)}</div>

                <div><strong>Мощность ТЭНа:</strong> ${formatProfileNumber(validation.heaterPowerW, 0, ' Вт')}</div>

                <div><strong>Рабочая мощность:</strong> ${formatProfileNumber(validation.targetPowerW, 0, ' Вт')}</div>

                <div><strong>Объём сырья:</strong> ${formatProfileNumber(validation.feedVolumeL, 1, ' л')}</div>

                <div><strong>Крепость сырья:</strong> ${formatProfileNumber(validation.feedAbvPercent, 1, '%')}</div>

                <div><strong>Заполнение куба:</strong> ${formatProfileNumber(validation.cubeChargePercent, 0, '%')}</div>

                <div><strong>Стабильность:</strong> ${formatProfileNumber((validation.avgStabilityIndex || 0) * 100, 0, '%')}</div>

            </div>

            <div class="modal-info-grid" style="margin-top: 12px;">

                <div><strong>Факт голов:</strong> ${formatProfileNumber(validation.headsActualMl, 0, ' мл')}</div>

                <div><strong>Факт тела:</strong> ${formatProfileNumber(validation.bodyActualMl, 0, ' мл')}</div>

                <div><strong>Факт хвостов:</strong> ${formatProfileNumber(validation.tailsActualMl, 0, ' мл')}</div>

                <div><strong>Срез голов:</strong> ${formatProfileNumber(validation.headsCutColumnTopC, 2, '°C')}</div>

                <div><strong>Срез тела:</strong> ${formatProfileNumber(validation.bodyCutColumnTopC, 2, '°C')}</div>

                <div><strong>Срез хвостов:</strong> ${formatProfileNumber(validation.tailsCutColumnTopC, 2, '°C')}</div>

                <div><strong>Финал куба:</strong> ${formatProfileNumber(validation.cubeFinalC, 1, '°C')}</div>

                <div><strong>Финал верха:</strong> ${formatProfileNumber(validation.columnTopFinalC, 2, '°C')}</div>

                <div><strong>Process health:</strong> ${formatProfileNumber((validation.avgProcessHealth || 0) * 100, 0, '%')}</div>

            </div>

        </div>

        <div class="modal-section">

            <div class="modal-section-title">🎯 Последний успешный baseline</div>

            <div class="modal-info-grid">

                <div><strong>ID прогона:</strong> ${lastSuccessfulRun?.id || '—'}</div>

                <div><strong>Длительность:</strong> ${formatProfileMinutes(lastSuccessfulRun?.duration)}</div>

                <div><strong>Выход:</strong> ${lastSuccessfulRun?.totalCollected ? `${lastSuccessfulRun.totalCollected} мл` : '—'}</div>

                <div><strong>Энергия:</strong> ${formatProfileNumber(lastSuccessfulRun?.energyUsed, 2, ' кВт·ч')}</div>

            </div>

            ${advisorItems.length ? `
                <div style="margin-top: 14px;">
                    <strong>Последние рекомендации Run Advisor:</strong>
                    <div style="display: grid; gap: 10px; margin-top: 10px;">
                        ${advisorItems.map((item) => `
                            <div style="padding: 12px; border: 1px solid var(--border-color); border-radius: 10px; background: var(--bg-secondary);">
                                <div style="font-weight: 600; margin-bottom: 6px;">${item.title}</div>
                                <div style="color: var(--text-secondary); margin-bottom: 6px;">${item.detail}</div>
                                <div>${item.action}</div>
                            </div>
                        `).join('')}
                    </div>
                </div>
            ` : '<div style="margin-top: 14px; color: var(--text-secondary);">Для последнего успешного прогона snapshot рекомендаций пока не сохранён.</div>'}

        </div>

    `;



    body.innerHTML = html;

    const editBtn = document.getElementById('profile-view-edit-btn');
    if (editBtn) {
        editBtn.style.display = profile?.metadata?.isBuiltin ? 'none' : '';
    }
    const duplicateBtn = document.getElementById('profile-view-duplicate-btn');
    if (duplicateBtn) {
        duplicateBtn.style.display = '';
    }

    document.getElementById('profile-view-modal').style.display = 'flex';

}



// Закрыть модальное окно просмотра

export function closeProfileViewModal() {

    document.getElementById('profile-view-modal').style.display = 'none';

    currentProfileIsBuiltin = false;
    setCurrentProfileId(null);

}



// Быстрая загрузка профиля

export async function quickLoadProfile(id) {

    try {
        const profileResponse = await fetch(`/api/profiles/${id}`);
        if (!profileResponse.ok) {
            throw new Error('Не удалось загрузить профиль');
        }

        const profile = await profileResponse.json();
        const isMashingProfile = profile?.metadata?.category === 'mashing';
        const compatibility = getProfileCompatibility(profile);
        const warningParts = [];
        const topologyWarning = buildProfileTopologyWarning(profile);
        const baroWarning = isMashingProfile ? '' : buildProfileLoadWarning(profile);
        const equipmentWarning = buildProfileEquipmentWarning(profile);
        if (topologyWarning) warningParts.push(topologyWarning);
        if (baroWarning) warningParts.push(baroWarning);
        if (equipmentWarning) warningParts.push(equipmentWarning);
        const warning = warningParts.join('\n\n');
        const confirmText = isMashingProfile
            ? (warning
                ? `Загрузить этот рецепт в панель затирки и сделать его активным профилем?\n\n${warning}`
                : 'Загрузить этот рецепт в панель затирки и сделать его активным профилем?')
            : (warning
                ? `Загрузить этот профиль в текущие настройки?\n\n${warning}`
                : 'Загрузить этот профиль в текущие настройки?');

        if (!confirm(confirmText)) return;

        const loadResponse = await fetch(`/api/profiles/${id}/load`, {
            method: 'POST'
        });
        const data = await loadResponse.json();
        if (!loadResponse.ok || !data.success) {
            throw new Error(data.error || 'Неизвестная ошибка');
        }

        if (isMashingProfile) {
            activateTabById('control');
            await selectControlMode('mashing');
            applyMashingProfileToControlPanel(profile);
            alert(
                compatibility.known && !compatibility.supported
                    ? `⚠️ Рецепт затирки загружен, но текущая комплектация ограничивает запуск.\n\n${compatibility.reason || 'Проверьте топологию термодатчиков.'}`
                    : '✅ Рецепт затирки загружен в панель управления.'
            );
        } else {
            alert(
                compatibility.known && !compatibility.supported
                    ? `⚠️ Профиль загружен, но его запуск на текущем железе будет заблокирован.\n\n${compatibility.reason || 'Проверьте топологию термодатчиков.'}`
                    : '✅ Профиль успешно загружен! Проверьте настройки в разделе "Управление".'
            );
        }

        void loadStatus();
    } catch (error) {
        console.error('Ошибка загрузки профиля:', error);
        alert(`❌ Ошибка загрузки профиля: ${error.message || 'Неизвестная ошибка'}`);
    }

}



// Загрузка профиля в настройки (из модального окна)

export function loadProfileToSettings() {

    if (!currentProfileId) return;

    closeProfileViewModal();

    void quickLoadProfile(currentProfileId);

}



// Удаление профиля

export function deleteProfile(id) {

    if (!confirm('Удалить этот профиль? Действие нельзя отменить.')) return;



    fetch(`/api/profiles/${id}`, {

        method: 'DELETE'

    })

        .then(response => response.json())

        .then(data => {

            if (data.success) {

                loadProfilesList();

                alert('✅ Профиль удалён');

            } else {

                alert('❌ ' + (data.error || 'Ошибка удаления профиля'));

            }

        })

        .catch(error => {

            console.error('Ошибка удаления профиля:', error);

            alert('❌ Ошибка удаления профиля');

        });

}



// Очистка пользовательских профилей

export function clearUserProfiles() {

    if (!confirm('Удалить ВСЕ пользовательские профили? Встроенные рецепты останутся. Действие нельзя отменить!')) return;



    fetch('/api/profiles', {

        method: 'DELETE'

    })

        .then(response => response.json())

        .then(data => {

            if (data.success) {

                loadProfilesList();

                alert('✅ Все пользовательские профили удалены');

            } else {

                alert('❌ Ошибка очистки профилей');

            }

        })

        .catch(error => {

            console.error('Ошибка очистки профилей:', error);

            alert('❌ Ошибка очистки профилей');

        });

}
