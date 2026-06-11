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
export const MODE_NBK = 6;
export const MODE_FERMENTATION = 7;

export function getModeLabel(mode) {
    switch (mode) {
        case MODE_IDLE: return 'Idle';
        case MODE_RECT: return 'Rectification';
        case MODE_MANUAL: return 'Manual';
        case MODE_DIST: return 'Distillation';
        case MODE_MASH: return 'Mashing';
        case MODE_HOLD: return 'Пастеризация';
        case MODE_NBK: return 'NBK';
        case MODE_FERMENTATION: return 'Fermentation';
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
        case MODE_NBK: return 'mode-nbk';
        case MODE_FERMENTATION: return 'mode-fermentation';
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
        hold: MODE_HOLD,
        nbk: MODE_NBK,
        fermentation: MODE_FERMENTATION
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
    safetyOk: true,
    currentAlarm: {
        active: false,
        latched: false,
        type: 'none',
        typeCode: 0,
        level: 'none',
        levelCode: 0,
        message: '',
        timestamp: 0,
        acknowledged: false,
        resetAvailable: true,
        resetBlockedReason: ''
    },
    activeProfile: {
        id: '',
        loaded: false,
        name: '',
        category: '',
        validation: {},
        baseTemperatures: {},
        baroPreview: {},
        effectiveTemperaturesPreview: {}
    },
    v2: {
        available: false,
        lifecycle: 'idle',
        phaseId: 0,
        phaseToken: 'idle',
        timestampMs: 0,
        safetyLatched: false,
        lastReasonCode: 'RC_NONE',
        operatorMessage: '',
        guidance: {
            tone: 'muted',
            title: '',
            detail: '',
            action: ''
        },
        activeLimits: {
            powerCapped: false,
            maxHeaterPowerPercent: 100,
            pumpCapped: false,
            maxPumpSpeedMlH: 0,
            takeoffBlocked: false,
            phaseAdvanceBlocked: false,
            antiOscillationActive: false,
            antiOscillationHoldSec: 0
        },
        commandTargets: {
            heaterPowerPercent: 0,
            pumpSpeedMlH: 0,
            waterValveOpen: false,
            headsValveOpen: false,
            stopRequested: false
        },
        indicators: {
            processHealth: 0,
            sensorFreshnessOk: false,
            pressureStable: false,
            boilingDetected: false,
            columnStable: false,
            targetReached: false,
            powerLimited: false,
            recoveryActive: false,
            takeoffAllowed: false,
            distHeatingComplete: false,
            distHeadsOptionalComplete: false,
            distBodyNearEnd: false,
            steamReady: false,
            nbkWorkingStable: false,
            nbkFeedAllowed: false,
            finishLikely: false,
            tempInBand: false,
            stepReady: false,
            stepHoldStable: false,
            heatingTooSlow: false,
            overshootRisk: false,
            fermTempInBand: false,
            longDeviation: false,
            heatingDemand: false,
            coolingDemand: false,
            heatingRateCPerMin: 0,
            topTempRateCPerMin: 0,
            pressureRateMmHgPerMin: 0,
            coolingMarginC: 0,
            distPressureMargin: 0,
            nbkPressureMargin: 0,
            nbkColumnLoad: 0,
            feedEnergyBalance: 0,
            stabilityIndex: 0,
            floodRisk: 0,
            headsCompletionScore: 0,
            bodyEndScore: 0,
            takeoffConfidence: -1,
            headsEndConfidence: -1,
            bodyEndConfidence: -1,
            tailsTransitionConfidence: -1,
            powerLimitConfidence: 0
        },
        safety: {
            severity: 'none',
            event: 'none',
            reasonCode: 'RC_NONE',
            requiresAcknowledge: false,
            message: '',
            resetAvailable: true,
            resetBlockedReason: ''
        }
    },
    power: { power: 0 },
    hydrometer: { abv: 0, valid: false },
    pump: { speedMlH: 0, totalMl: 0 },
    stirrer: {
        running: false,
        speedPercent: 0,
        available: false,
        autoMode: false,
        lastUpdate: 0
    },
    temps: { cube: 0, columnBottom: 0 },
    valves: { water: false, heads: false, uno: false, tails: false },
    volumes: { heads: 0, body: 0, tails: 0 },
    equipment: {
        heaterPowerW: maxHeaterPower,
        cubeVolumeL: DEFAULT_CUBE_VOLUME_L,
        minHeaterSubmergeL: 7.5,
        waterAutoStartCubeTempC: 45
    },
    stirrerSettings: {
        enabled: false,
        defaultSpeedPercent: 50,
        autoMashing: true,
        autoFermentation: false,
        autoNbk: false
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
    nbk: {
        powerW: 2500,
        pumpSpeedMlH: 20000,
        columnBottomTempThresholdC: 95,
        phase: 0,
        phaseStr: 'idle'
    },
    fermentation: {
        targetTempC: 28,
        hysteresisC: 0.5,
        useHeater: true,
        phase: 0,
        phaseStr: 'idle'
    },
    safetySettings: {
        pressureMaxMmHg: 50,
        tsaMaxC: 55,
        waterOutMaxC: 70,
        waterOutRiseRateCMin: 8,
        pressureRiseRateMmHgMin: 20
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
