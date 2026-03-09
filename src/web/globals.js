// Smart-Column S3 - Web UI JavaScript



export let ws = null;
export function setWs(newWs) {
    ws = newWs;
}

export let reconnectInterval = null;
export function setReconnectInterval(interval) {
    reconnectInterval = interval;
}

export let isConnected = false;
export function setIsConnected(connected) {
    isConnected = connected;
}

export let miniChart = null;
export function setMiniChart(c) {
    miniChart = c;
}

export let miniChartData = {

    timestamps: [],

    cube: [],

    columnTop: [],

    reflux: []

};

export const MINI_CHART_MAX_POINTS = 360; // 30 минут при обновлении каждые 5 секунд
export const DEFAULT_CUBE_VOLUME_L = 37;



// Состояние процесса

export let currentMode = 0;  // 0 = IDLE
export function setCurrentMode(mode) {
    currentMode = mode;
}

export let currentPaused = false;
export function setCurrentPaused(paused) {
    currentPaused = paused;
}

export let maxHeaterPower = 3000;
export function setMaxHeaterPower(val) { maxHeaterPower = val; }  // Будет обновлено из настроек

export const MODE_IDLE = 0;
export const MODE_RECT = 1;
export const MODE_DIST = 2;
export const MODE_MANUAL = 3;
export const MODE_MASH = 4;
export const MODE_HOLD = 5;

export function getModeLabel(mode) {
    switch (mode) {
        case MODE_IDLE: return 'Idle';
        case MODE_RECT: return 'Rectification';
        case MODE_MANUAL: return 'Manual';
        case MODE_DIST: return 'Distillation';
        case MODE_MASH: return 'Mashing';
        case MODE_HOLD: return 'Hold';
        default: return 'Unknown';
    }
}

export function getModeCssClass(mode) {
    switch (mode) {
        case MODE_IDLE: return 'mode-idle';
        case MODE_RECT: return 'mode-rectification';
        case MODE_MANUAL: return 'mode-manual';
        case MODE_DIST: return 'mode-distillation';
        case MODE_MASH: return 'mode-mashing';
        case MODE_HOLD: return 'mode-hold';
        default: return 'mode-idle';
    }
}

export function resolveMode(modeValue, modeStrValue) {
    const modeNum = Number(modeValue);
    if (Number.isFinite(modeNum)) return modeNum;
    if (typeof modeStrValue !== 'string') return MODE_IDLE;

    const modeMap = {
        idle: MODE_IDLE,
        rect: MODE_RECT,
        rectification: MODE_RECT,
        manual: MODE_MANUAL,
        dist: MODE_DIST,
        distillation: MODE_DIST,
        mash: MODE_MASH,
        mashing: MODE_MASH,
        hold: MODE_HOLD
    };
    return modeMap[modeStrValue.toLowerCase()] ?? MODE_IDLE;
}

export const PHASE_HEADS = 3;
export const PHASE_POST_HEADS_STAB = 4;
export const PHASE_BODY = 5;
export const PHASE_TAILS = 6;

export let runtimeMonitorState = {
    mode: MODE_IDLE,
    modeStr: 'idle',
    phase: 0,
    phaseStr: 'IDLE',
    power: { power: 0 },
    hydrometer: { abv: 0, valid: false },
    pump: { speedMlH: 0, totalMl: 0 },
    valves: { water: false, heads: false, uno: false, tails: false },
    volumes: { heads: 0, body: 0, tails: 0 },
    equipment: {
        heaterPowerW: maxHeaterPower,
        cubeVolumeL: DEFAULT_CUBE_VOLUME_L,
        minHeaterSubmergeL: 7.5,
        waterAutoStartCubeTempC: 45
    },
    rectification: {
        feedVolumeL: DEFAULT_CUBE_VOLUME_L,
        feedAbvPercent: 40,
        headsPercent: 8,
        bodyPercent: 84,
        tailsPercent: 8,
        headsSpeedMlHKw: 300,
        bodySpeedMlHKw: 600,
        headsTargetMl: 0,
        bodyTargetMl: 0,
        tailsTargetMl: 0
    },
    distillation: {
        speedMlH: 0,
        headsVolumeMl: 0,
        targetVolumeMl: 0,
        endTempC: 0,
        powerPercent: 0
    },
    safetySettings: {
        pressureMaxMmHg: 50,
        tsaMaxC: 55,
        waterOutMaxC: 70
    },
    mashing: {
        active: false,
        stepCount: 0,
        currentStep: 0,
        stepDurationSec: 0,
        elapsedSec: 0,
        remainingSec: 0,
        stepName: ''
    },
    hold: {
        active: false,
        stepCount: 0,
        currentStep: 0,
        stepDurationSec: 0,
        elapsedSec: 0,
        remainingSec: 0,
        targetTemp: 0
    },
    progress: {
        phaseElapsedSec: 0,
        phaseTargetSec: 0,
        phaseRemainingSec: 0,
        phasePercent: 0
    }
};

export let runtimeEditContext = null;
export function setRuntimeEditContext(ctx) {
    runtimeEditContext = ctx;
}
export function getCubeVolumeLimitL(fallback = DEFAULT_CUBE_VOLUME_L) {
    const fromRuntime = Number(runtimeMonitorState?.equipment?.cubeVolumeL);
    if (Number.isFinite(fromRuntime) && fromRuntime > 0) return fromRuntime;

    const fromForm = Number(document.getElementById('cube-volume-l')?.value);
    if (Number.isFinite(fromForm) && fromForm > 0) return fromForm;

    return fallback;
}

export function clampFeedVolumeToCube(value, fallback = DEFAULT_CUBE_VOLUME_L) {
    const maxVolume = Math.max(1, Math.min(250, getCubeVolumeLimitL(fallback)));
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) return Math.min(fallback, maxVolume);
    if (parsed < 1) return 1;
    if (parsed > maxVolume) return maxVolume;
    return parsed;
}
export const ABV_PLAN_STORAGE_KEY = 'ui.plannedAbvPercent';
export let plannedAbvPercent = 40.0;
export let plannedAbvUserSet = false;

export function setPlannedAbvPercent(value) {
    plannedAbvPercent = value;
}

export function setPlannedAbvUserSet(value) {
    plannedAbvUserSet = value;
}
