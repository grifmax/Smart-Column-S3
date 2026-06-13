import { runtimeMonitorState, resolveMode, plannedAbvUserSet, plannedAbvPercent, setPlannedAbvPercent } from '../globals.js';
import { toFinite, normalizeAbvPercent, clampPercent } from './helpers.js';

function readFiniteCandidate(source, keys) {
    if (!source || typeof source !== 'object') return undefined;
    for (const key of keys) {
        if (!(key in source)) continue;
        const value = toFinite(source[key], NaN);
        if (Number.isFinite(value)) return value;
    }
    return undefined;
}

function mergeEquipmentState(s, data) {
    const sources = [];
    if (data?.equipment && typeof data.equipment === 'object') sources.push(data.equipment);
    if (data?.settings?.equipment && typeof data.settings.equipment === 'object') sources.push(data.settings.equipment);
    if (data && typeof data === 'object') sources.push(data);

    const assign = (field, keys) => {
        for (const source of sources) {
            const value = readFiniteCandidate(source, keys);
            if (value === undefined) continue;
            s.equipment[field] = value;
            return;
        }
    };

    assign('heaterPowerW', ['heaterPowerW', 'heater_power_w']);
    assign('cubeVolumeL', ['cubeVolumeL', 'cube_volume_l']);
    assign('minHeaterSubmergeL', ['minHeaterSubmergeL', 'min_heater_submerge_l']);
    assign('waterAutoStartCubeTempC', [
        'waterAutoStartCubeTempC',
        'water_auto_start_cube_temp_c',
        'water_auto_start_cube_temp'
    ]);
}

function mergeStirrerState(s, data) {
    const stirrer = (data?.stirrer && typeof data.stirrer === 'object') ? data.stirrer : null;
    if (!stirrer) return;

    if (stirrer.running !== undefined) s.stirrer.running = Boolean(stirrer.running);
    if (stirrer.speed !== undefined || stirrer.speedPercent !== undefined) {
        const speed = toFinite(stirrer.speedPercent ?? stirrer.speed, s.stirrer.speedPercent);
        s.stirrer.speedPercent = Math.max(0, Math.min(100, speed));
    }
    if (stirrer.available !== undefined) s.stirrer.available = Boolean(stirrer.available);
    if (stirrer.autoMode !== undefined) s.stirrer.autoMode = Boolean(stirrer.autoMode);
    if (stirrer.lastUpdate !== undefined) s.stirrer.lastUpdate = toFinite(stirrer.lastUpdate, s.stirrer.lastUpdate);
}

function mergePressureState(s, data) {
    const pressure = (data?.pressure && typeof data.pressure === 'object') ? data.pressure : null;

    const cube = pressure
        ? readFiniteCandidate(pressure, ['cube', 'cubeMmHg', 'currentPressure'])
        : readFiniteCandidate(data, ['p_cube']);
    if (cube !== undefined) s.pressure.cube = cube;

    const atm = pressure
        ? readFiniteCandidate(pressure, ['atm', 'atmosphere', 'atmosphereHpa'])
        : readFiniteCandidate(data, ['p_atm']);
    if (atm !== undefined) s.pressure.atm = atm;

    if (pressure?.ok !== undefined) s.pressure.ok = Boolean(pressure.ok);
    else if (data?.v2?.indicators?.pressureSensorAvailable !== undefined) {
        s.pressure.ok = Boolean(data.v2.indicators.pressureSensorAvailable);
    }

    if (pressure?.ads1115Available !== undefined) {
        s.pressure.ads1115Available = Boolean(pressure.ads1115Available);
    } else if (pressure?.ok !== undefined) {
        s.pressure.ads1115Available = Boolean(pressure.ok);
    }

    const sensorVoltage = pressure
        ? readFiniteCandidate(pressure, ['sensorVoltage', 'currentVoltage'])
        : undefined;
    if (sensorVoltage !== undefined) s.pressure.sensorVoltage = sensorVoltage;

    const sensorAdc = pressure
        ? readFiniteCandidate(pressure, ['sensorAdc', 'currentAdc'])
        : undefined;
    if (sensorAdc !== undefined) s.pressure.sensorAdc = sensorAdc;

    if (pressure?.source !== undefined) s.pressure.source = String(pressure.source);

    const lastUpdate = pressure
        ? readFiniteCandidate(pressure, ['lastUpdate'])
        : undefined;
    if (lastUpdate !== undefined) s.pressure.lastUpdate = lastUpdate;
}

function mergeSafetySettingsState(s, data) {
    const sources = [];
    if (data?.safetySettings && typeof data.safetySettings === 'object') sources.push(data.safetySettings);
    if (data?.safety && typeof data.safety === 'object') sources.push(data.safety);
    if (data?.settings?.safety && typeof data.settings.safety === 'object') sources.push(data.settings.safety);
    if (data && typeof data === 'object') sources.push(data);

    const assign = (field, keys) => {
        for (const source of sources) {
            const value = readFiniteCandidate(source, keys);
            if (value === undefined) continue;
            s.safetySettings[field] = value;
            return;
        }
    };

    assign('pressureMaxMmHg', ['pressureMaxMmHg', 'pressure_max_mmhg', 'safetyPressureMaxMmHg']);
    assign('tsaMaxC', ['tsaMaxC', 'tsa_max_c', 'safetyTsaMaxC']);
    assign('waterOutMaxC', ['waterOutMaxC', 'water_out_max_c', 'safetyWaterOutMaxC']);
    assign('waterOutRiseRateCMin', [
        'waterOutRiseRateCMin',
        'water_out_rise_rate_c_min',
        'safetyWaterOutRiseRateCMin'
    ]);
    assign('pressureRiseRateMmHgMin', [
        'pressureRiseRateMmHgMin',
        'pressure_rise_rate_mmhg_min',
        'safetyPressureRiseRateMmHgMin'
    ]);
}

function mergeAlarmState(s, data) {
    const alarm = (data?.currentAlarm && typeof data.currentAlarm === 'object')
        ? data.currentAlarm
        : ((data?.alarm && typeof data.alarm === 'object') ? data.alarm : null);
    if (!alarm) return;

    s.currentAlarm = {
        ...s.currentAlarm,
        active: Boolean(alarm.active),
        latched: Boolean(alarm.latched),
        type: alarm.type !== undefined ? String(alarm.type) : s.currentAlarm.type,
        typeCode: toFinite(alarm.typeCode ?? alarm.type, s.currentAlarm.typeCode),
        level: alarm.level !== undefined ? String(alarm.level) : s.currentAlarm.level,
        levelCode: toFinite(alarm.levelCode ?? alarm.level, s.currentAlarm.levelCode),
        message: alarm.message !== undefined ? String(alarm.message) : s.currentAlarm.message,
        timestamp: toFinite(alarm.timestamp, s.currentAlarm.timestamp),
        acknowledged: alarm.acknowledged !== undefined ? Boolean(alarm.acknowledged) : s.currentAlarm.acknowledged,
        resetAvailable: alarm.resetAvailable !== undefined ? Boolean(alarm.resetAvailable) : s.currentAlarm.resetAvailable,
        resetBlockedReason: alarm.resetBlockedReason !== undefined
            ? String(alarm.resetBlockedReason)
            : s.currentAlarm.resetBlockedReason
    };
}

function mergeActiveProfileState(s, data) {
    const activeProfile = (data?.activeProfile && typeof data.activeProfile === 'object')
        ? data.activeProfile
        : null;
    if (!activeProfile) return;

    s.activeProfile = {
        ...s.activeProfile,
        id: activeProfile.id !== undefined ? String(activeProfile.id) : s.activeProfile.id,
        loaded: activeProfile.loaded !== undefined ? Boolean(activeProfile.loaded) : s.activeProfile.loaded,
        name: activeProfile.name !== undefined ? String(activeProfile.name) : s.activeProfile.name,
        category: activeProfile.category !== undefined ? String(activeProfile.category) : s.activeProfile.category,
        validation: (activeProfile.validation && typeof activeProfile.validation === 'object')
            ? { ...activeProfile.validation }
            : s.activeProfile.validation,
        baseTemperatures: (activeProfile.baseTemperatures && typeof activeProfile.baseTemperatures === 'object')
            ? { ...activeProfile.baseTemperatures }
            : s.activeProfile.baseTemperatures,
        baroPreview: (activeProfile.baroPreview && typeof activeProfile.baroPreview === 'object')
            ? { ...activeProfile.baroPreview }
            : s.activeProfile.baroPreview,
        effectiveTemperaturesPreview:
            (activeProfile.effectiveTemperaturesPreview &&
                typeof activeProfile.effectiveTemperaturesPreview === 'object')
                ? { ...activeProfile.effectiveTemperaturesPreview }
                : s.activeProfile.effectiveTemperaturesPreview
    };
}

function mergeV2State(s, data) {
    const v2 = (data?.v2 && typeof data.v2 === 'object') ? data.v2 : null;
    if (!v2) return;

    const safety = (v2?.safety && typeof v2.safety === 'object') ? v2.safety : null;
    const activeLimits = (v2?.activeLimits && typeof v2.activeLimits === 'object') ? v2.activeLimits : null;
    const commandTargets = (v2?.commandTargets && typeof v2.commandTargets === 'object') ? v2.commandTargets : null;
    const indicators = (v2?.indicators && typeof v2.indicators === 'object') ? v2.indicators : null;
    const guidance = (v2?.guidance && typeof v2.guidance === 'object') ? v2.guidance : null;
    const reasonInsight = (v2?.reasonInsight && typeof v2.reasonInsight === 'object') ? v2.reasonInsight : null;
    s.v2 = {
        ...s.v2,
        available: v2.available !== undefined ? Boolean(v2.available) : s.v2.available,
        lifecycle: v2.lifecycle !== undefined ? String(v2.lifecycle) : s.v2.lifecycle,
        phaseId: v2.phaseId !== undefined ? toFinite(v2.phaseId, s.v2.phaseId) : s.v2.phaseId,
        phaseToken: v2.phaseToken !== undefined ? String(v2.phaseToken) : s.v2.phaseToken,
        timestampMs: v2.timestampMs !== undefined ? toFinite(v2.timestampMs, s.v2.timestampMs) : s.v2.timestampMs,
        safetyLatched: v2.safetyLatched !== undefined ? Boolean(v2.safetyLatched) : s.v2.safetyLatched,
        lastReasonCode: v2.lastReasonCode !== undefined ? String(v2.lastReasonCode) : s.v2.lastReasonCode,
        operatorMessage: v2.operatorMessage !== undefined ? String(v2.operatorMessage) : s.v2.operatorMessage,
        guidance: guidance ? {
            ...s.v2.guidance,
            tone: guidance.tone !== undefined ? String(guidance.tone) : s.v2.guidance.tone,
            title: guidance.title !== undefined ? String(guidance.title) : s.v2.guidance.title,
            detail: guidance.detail !== undefined ? String(guidance.detail) : s.v2.guidance.detail,
            action: guidance.action !== undefined ? String(guidance.action) : s.v2.guidance.action
        } : s.v2.guidance,
        reasonInsight: reasonInsight ? {
            ...s.v2.reasonInsight,
            tone: reasonInsight.tone !== undefined ? String(reasonInsight.tone) : s.v2.reasonInsight.tone,
            title: reasonInsight.title !== undefined ? String(reasonInsight.title) : s.v2.reasonInsight.title,
            detail: reasonInsight.detail !== undefined ? String(reasonInsight.detail) : s.v2.reasonInsight.detail,
            action: reasonInsight.action !== undefined ? String(reasonInsight.action) : s.v2.reasonInsight.action
        } : s.v2.reasonInsight,
        activeLimits: activeLimits ? {
            ...s.v2.activeLimits,
            powerCapped: activeLimits.powerCapped !== undefined ? Boolean(activeLimits.powerCapped) : s.v2.activeLimits.powerCapped,
            maxHeaterPowerPercent: activeLimits.maxHeaterPowerPercent !== undefined
                ? toFinite(activeLimits.maxHeaterPowerPercent, s.v2.activeLimits.maxHeaterPowerPercent)
                : s.v2.activeLimits.maxHeaterPowerPercent,
            pumpCapped: activeLimits.pumpCapped !== undefined ? Boolean(activeLimits.pumpCapped) : s.v2.activeLimits.pumpCapped,
            maxPumpSpeedMlH: activeLimits.maxPumpSpeedMlH !== undefined
                ? toFinite(activeLimits.maxPumpSpeedMlH, s.v2.activeLimits.maxPumpSpeedMlH)
                : s.v2.activeLimits.maxPumpSpeedMlH,
            takeoffBlocked: activeLimits.takeoffBlocked !== undefined ? Boolean(activeLimits.takeoffBlocked) : s.v2.activeLimits.takeoffBlocked,
            phaseAdvanceBlocked: activeLimits.phaseAdvanceBlocked !== undefined ? Boolean(activeLimits.phaseAdvanceBlocked) : s.v2.activeLimits.phaseAdvanceBlocked,
            antiOscillationActive: activeLimits.antiOscillationActive !== undefined
                ? Boolean(activeLimits.antiOscillationActive)
                : s.v2.activeLimits.antiOscillationActive,
            antiOscillationHoldSec: activeLimits.antiOscillationHoldSec !== undefined
                ? toFinite(activeLimits.antiOscillationHoldSec, s.v2.activeLimits.antiOscillationHoldSec)
                : s.v2.activeLimits.antiOscillationHoldSec
        } : s.v2.activeLimits,
        commandTargets: commandTargets ? {
            ...s.v2.commandTargets,
            heaterPowerPercent: commandTargets.heaterPowerPercent !== undefined
                ? toFinite(commandTargets.heaterPowerPercent, s.v2.commandTargets.heaterPowerPercent)
                : s.v2.commandTargets.heaterPowerPercent,
            pumpSpeedMlH: commandTargets.pumpSpeedMlH !== undefined
                ? toFinite(commandTargets.pumpSpeedMlH, s.v2.commandTargets.pumpSpeedMlH)
                : s.v2.commandTargets.pumpSpeedMlH,
            waterValveOpen: commandTargets.waterValveOpen !== undefined ? Boolean(commandTargets.waterValveOpen) : s.v2.commandTargets.waterValveOpen,
            headsValveOpen: commandTargets.headsValveOpen !== undefined ? Boolean(commandTargets.headsValveOpen) : s.v2.commandTargets.headsValveOpen,
            stopRequested: commandTargets.stopRequested !== undefined ? Boolean(commandTargets.stopRequested) : s.v2.commandTargets.stopRequested
        } : s.v2.commandTargets,
        indicators: indicators ? {
            ...s.v2.indicators,
            processHealth: indicators.processHealth !== undefined ? toFinite(indicators.processHealth, s.v2.indicators.processHealth) : s.v2.indicators.processHealth,
            telemetryCoverage: indicators.telemetryCoverage !== undefined ? toFinite(indicators.telemetryCoverage, s.v2.indicators.telemetryCoverage) : s.v2.indicators.telemetryCoverage,
            decisionTrust: indicators.decisionTrust !== undefined ? toFinite(indicators.decisionTrust, s.v2.indicators.decisionTrust) : s.v2.indicators.decisionTrust,
            sensorFreshnessOk: indicators.sensorFreshnessOk !== undefined ? Boolean(indicators.sensorFreshnessOk) : s.v2.indicators.sensorFreshnessOk,
            pressureStable: indicators.pressureStable !== undefined ? Boolean(indicators.pressureStable) : s.v2.indicators.pressureStable,
            pressureSensorAvailable: indicators.pressureSensorAvailable !== undefined ? Boolean(indicators.pressureSensorAvailable) : s.v2.indicators.pressureSensorAvailable,
            columnSensorsAvailable: indicators.columnSensorsAvailable !== undefined ? Boolean(indicators.columnSensorsAvailable) : s.v2.indicators.columnSensorsAvailable,
            coolingSensorAvailable: indicators.coolingSensorAvailable !== undefined ? Boolean(indicators.coolingSensorAvailable) : s.v2.indicators.coolingSensorAvailable,
            boilingDetected: indicators.boilingDetected !== undefined ? Boolean(indicators.boilingDetected) : s.v2.indicators.boilingDetected,
            columnStable: indicators.columnStable !== undefined ? Boolean(indicators.columnStable) : s.v2.indicators.columnStable,
            targetReached: indicators.targetReached !== undefined ? Boolean(indicators.targetReached) : s.v2.indicators.targetReached,
            powerLimited: indicators.powerLimited !== undefined ? Boolean(indicators.powerLimited) : s.v2.indicators.powerLimited,
            recoveryActive: indicators.recoveryActive !== undefined ? Boolean(indicators.recoveryActive) : s.v2.indicators.recoveryActive,
            takeoffAllowed: indicators.takeoffAllowed !== undefined ? Boolean(indicators.takeoffAllowed) : s.v2.indicators.takeoffAllowed,
            degradedModeActive: indicators.degradedModeActive !== undefined ? Boolean(indicators.degradedModeActive) : s.v2.indicators.degradedModeActive,
            adaptiveControlAllowed: indicators.adaptiveControlAllowed !== undefined ? Boolean(indicators.adaptiveControlAllowed) : s.v2.indicators.adaptiveControlAllowed,
            distHeatingComplete: indicators.distHeatingComplete !== undefined ? Boolean(indicators.distHeatingComplete) : s.v2.indicators.distHeatingComplete,
            distHeadsOptionalComplete: indicators.distHeadsOptionalComplete !== undefined ? Boolean(indicators.distHeadsOptionalComplete) : s.v2.indicators.distHeadsOptionalComplete,
            distBodyNearEnd: indicators.distBodyNearEnd !== undefined ? Boolean(indicators.distBodyNearEnd) : s.v2.indicators.distBodyNearEnd,
            steamReady: indicators.steamReady !== undefined ? Boolean(indicators.steamReady) : s.v2.indicators.steamReady,
            nbkWorkingStable: indicators.nbkWorkingStable !== undefined ? Boolean(indicators.nbkWorkingStable) : s.v2.indicators.nbkWorkingStable,
            nbkFeedAllowed: indicators.nbkFeedAllowed !== undefined ? Boolean(indicators.nbkFeedAllowed) : s.v2.indicators.nbkFeedAllowed,
            finishLikely: indicators.finishLikely !== undefined ? Boolean(indicators.finishLikely) : s.v2.indicators.finishLikely,
            tempInBand: indicators.tempInBand !== undefined ? Boolean(indicators.tempInBand) : s.v2.indicators.tempInBand,
            stepReady: indicators.stepReady !== undefined ? Boolean(indicators.stepReady) : s.v2.indicators.stepReady,
            stepHoldStable: indicators.stepHoldStable !== undefined ? Boolean(indicators.stepHoldStable) : s.v2.indicators.stepHoldStable,
            heatingTooSlow: indicators.heatingTooSlow !== undefined ? Boolean(indicators.heatingTooSlow) : s.v2.indicators.heatingTooSlow,
            overshootRisk: indicators.overshootRisk !== undefined ? Boolean(indicators.overshootRisk) : s.v2.indicators.overshootRisk,
            fermTempInBand: indicators.fermTempInBand !== undefined ? Boolean(indicators.fermTempInBand) : s.v2.indicators.fermTempInBand,
            longDeviation: indicators.longDeviation !== undefined ? Boolean(indicators.longDeviation) : s.v2.indicators.longDeviation,
            heatingDemand: indicators.heatingDemand !== undefined ? Boolean(indicators.heatingDemand) : s.v2.indicators.heatingDemand,
            coolingDemand: indicators.coolingDemand !== undefined ? Boolean(indicators.coolingDemand) : s.v2.indicators.coolingDemand,
            heatingRateCPerMin: indicators.heatingRateCPerMin !== undefined ? toFinite(indicators.heatingRateCPerMin, s.v2.indicators.heatingRateCPerMin) : s.v2.indicators.heatingRateCPerMin,
            topTempRateCPerMin: indicators.topTempRateCPerMin !== undefined ? toFinite(indicators.topTempRateCPerMin, s.v2.indicators.topTempRateCPerMin) : s.v2.indicators.topTempRateCPerMin,
            pressureRateMmHgPerMin: indicators.pressureRateMmHgPerMin !== undefined ? toFinite(indicators.pressureRateMmHgPerMin, s.v2.indicators.pressureRateMmHgPerMin) : s.v2.indicators.pressureRateMmHgPerMin,
            coolingMarginC: indicators.coolingMarginC !== undefined ? toFinite(indicators.coolingMarginC, s.v2.indicators.coolingMarginC) : s.v2.indicators.coolingMarginC,
            distPressureMargin: indicators.distPressureMargin !== undefined ? toFinite(indicators.distPressureMargin, s.v2.indicators.distPressureMargin) : s.v2.indicators.distPressureMargin,
            nbkPressureMargin: indicators.nbkPressureMargin !== undefined ? toFinite(indicators.nbkPressureMargin, s.v2.indicators.nbkPressureMargin) : s.v2.indicators.nbkPressureMargin,
            nbkColumnLoad: indicators.nbkColumnLoad !== undefined ? toFinite(indicators.nbkColumnLoad, s.v2.indicators.nbkColumnLoad) : s.v2.indicators.nbkColumnLoad,
            feedEnergyBalance: indicators.feedEnergyBalance !== undefined ? toFinite(indicators.feedEnergyBalance, s.v2.indicators.feedEnergyBalance) : s.v2.indicators.feedEnergyBalance,
            stabilityIndex: indicators.stabilityIndex !== undefined ? toFinite(indicators.stabilityIndex, s.v2.indicators.stabilityIndex) : s.v2.indicators.stabilityIndex,
            floodRisk: indicators.floodRisk !== undefined ? toFinite(indicators.floodRisk, s.v2.indicators.floodRisk) : s.v2.indicators.floodRisk,
            headsCompletionScore: indicators.headsCompletionScore !== undefined ? toFinite(indicators.headsCompletionScore, s.v2.indicators.headsCompletionScore) : s.v2.indicators.headsCompletionScore,
            bodyEndScore: indicators.bodyEndScore !== undefined ? toFinite(indicators.bodyEndScore, s.v2.indicators.bodyEndScore) : s.v2.indicators.bodyEndScore,
            takeoffConfidence: indicators.takeoffConfidence !== undefined ? toFinite(indicators.takeoffConfidence, s.v2.indicators.takeoffConfidence) : s.v2.indicators.takeoffConfidence,
            headsEndConfidence: indicators.headsEndConfidence !== undefined ? toFinite(indicators.headsEndConfidence, s.v2.indicators.headsEndConfidence) : s.v2.indicators.headsEndConfidence,
            bodyEndConfidence: indicators.bodyEndConfidence !== undefined ? toFinite(indicators.bodyEndConfidence, s.v2.indicators.bodyEndConfidence) : s.v2.indicators.bodyEndConfidence,
            tailsTransitionConfidence: indicators.tailsTransitionConfidence !== undefined ? toFinite(indicators.tailsTransitionConfidence, s.v2.indicators.tailsTransitionConfidence) : s.v2.indicators.tailsTransitionConfidence,
            powerLimitConfidence: indicators.powerLimitConfidence !== undefined ? toFinite(indicators.powerLimitConfidence, s.v2.indicators.powerLimitConfidence) : s.v2.indicators.powerLimitConfidence
        } : s.v2.indicators,
        safety: safety ? {
            ...s.v2.safety,
            severity: safety.severity !== undefined ? String(safety.severity) : s.v2.safety.severity,
            event: safety.event !== undefined ? String(safety.event) : s.v2.safety.event,
            reasonCode: safety.reasonCode !== undefined ? String(safety.reasonCode) : s.v2.safety.reasonCode,
            requiresAcknowledge: safety.requiresAcknowledge !== undefined
                ? Boolean(safety.requiresAcknowledge)
                : s.v2.safety.requiresAcknowledge,
            message: safety.message !== undefined ? String(safety.message) : s.v2.safety.message,
            resetAvailable: safety.resetAvailable !== undefined
                ? Boolean(safety.resetAvailable)
                : s.v2.safety.resetAvailable,
            resetBlockedReason: safety.resetBlockedReason !== undefined
                ? String(safety.resetBlockedReason)
                : s.v2.safety.resetBlockedReason
        } : s.v2.safety
    };
}

export function updateRuntimeStateFromStatus(data) {
    if (!data || typeof data !== 'object') return;
    const s = runtimeMonitorState;
    s.mode = resolveMode(data.mode ?? s.mode, data.modeStr ?? s.modeStr);
    if (data.modeStr !== undefined) s.modeStr = String(data.modeStr);
    if (data.phase !== undefined) s.phase = toFinite(data.phase, s.phase);
    if (data.phaseStr !== undefined) s.phaseStr = String(data.phaseStr);
    if (data.safetyOk !== undefined) s.safetyOk = Boolean(data.safetyOk);
    mergeAlarmState(s, data);
    mergeActiveProfileState(s, data);
    mergeV2State(s, data);
    mergePressureState(s, data);

    if (data.power && typeof data.power === 'object') {
        if (data.power.power !== undefined) s.power.power = toFinite(data.power.power, s.power.power);
    }
    if (data.hydrometer && typeof data.hydrometer === 'object') {
        if (data.hydrometer.abv !== undefined) s.hydrometer.abv = toFinite(data.hydrometer.abv, s.hydrometer.abv);
        if (data.hydrometer.valid !== undefined) s.hydrometer.valid = Boolean(data.hydrometer.valid);
    }
    if (data.pump && typeof data.pump === 'object') {
        if (data.pump.speedMlH !== undefined) s.pump.speedMlH = toFinite(data.pump.speedMlH, s.pump.speedMlH);
        if (data.pump.totalMl !== undefined) s.pump.totalMl = toFinite(data.pump.totalMl, s.pump.totalMl);
    }
    if (data.temps && typeof data.temps === 'object') {
        if (data.temps.cube !== undefined) s.temps.cube = toFinite(data.temps.cube, s.temps.cube);
        if (data.temps.columnBottom !== undefined) s.temps.columnBottom = toFinite(data.temps.columnBottom, s.temps.columnBottom);
    }
    if (data.valves && typeof data.valves === 'object') {
        s.valves = { ...s.valves, ...data.valves };
    }
    if (data.volumes && typeof data.volumes === 'object') {
        if (data.volumes.heads !== undefined) s.volumes.heads = toFinite(data.volumes.heads, s.volumes.heads);
        if (data.volumes.body !== undefined) s.volumes.body = toFinite(data.volumes.body, s.volumes.body);
        if (data.volumes.tails !== undefined) s.volumes.tails = toFinite(data.volumes.tails, s.volumes.tails);
    }

    mergeEquipmentState(s, data);
    mergeStirrerState(s, data);

    if (data.rectification && typeof data.rectification === 'object') {
        s.rectification = { ...s.rectification, ...data.rectification };
        if (!plannedAbvUserSet && data.rectification.feedAbvPercent !== undefined) {
            setPlannedAbvPercent(normalizeAbvPercent(data.rectification.feedAbvPercent, plannedAbvPercent));
        }
    }
    if (data.distillation && typeof data.distillation === 'object') {
        s.distillation = { ...s.distillation, ...data.distillation };
    }
    if (data.nbk && typeof data.nbk === 'object') {
        s.nbk = { ...s.nbk, ...data.nbk };
    }
    if (data.fermentation && typeof data.fermentation === 'object') {
        s.fermentation = { ...s.fermentation, ...data.fermentation };
    }
    if (data.nbkPhase !== undefined) s.nbk.phase = toFinite(data.nbkPhase, s.nbk.phase);
    if (data.nbkPhaseStr !== undefined) s.nbk.phaseStr = String(data.nbkPhaseStr);
    if (data.fermPhase !== undefined) s.fermentation.phase = toFinite(data.fermPhase, s.fermentation.phase);
    if (data.fermPhaseStr !== undefined) s.fermentation.phaseStr = String(data.fermPhaseStr);

    mergeSafetySettingsState(s, data);

    if (data.mashing && typeof data.mashing === 'object') {
        s.mashing = { ...s.mashing, ...data.mashing };
    }
    if (data.hold && typeof data.hold === 'object') {
        s.hold = { ...s.hold, ...data.hold };
    }
    if (data.progress && typeof data.progress === 'object') {
        s.progress = { ...s.progress, ...data.progress };
    }
}

export function updateRuntimeStateFromWs(data) {
    if (!data || typeof data !== 'object') return;
    const s = runtimeMonitorState;
    if (data.mode !== undefined || data.modeStr !== undefined) {
        s.mode = resolveMode(data.mode ?? s.mode, data.modeStr ?? s.modeStr);
    }
    if (data.modeStr !== undefined) s.modeStr = String(data.modeStr);
    if (data.phase !== undefined) s.phase = toFinite(data.phase, s.phase);
    if (data.phaseStr !== undefined) s.phaseStr = String(data.phaseStr);
    if (data.safetyOk !== undefined) s.safetyOk = Boolean(data.safetyOk);
    mergeAlarmState(s, data);
    mergeActiveProfileState(s, data);
    mergeV2State(s, data);
    mergePressureState(s, data);

    if (data.power !== undefined) s.power.power = toFinite(data.power, s.power.power);
    if (data.abv !== undefined) s.hydrometer.abv = toFinite(data.abv, s.hydrometer.abv);
    if (data.abv_valid !== undefined) s.hydrometer.valid = Boolean(data.abv_valid);
    if (data.pump_speed !== undefined) s.pump.speedMlH = toFinite(data.pump_speed, s.pump.speedMlH);
    if (data.pump_volume !== undefined) s.pump.totalMl = toFinite(data.pump_volume, s.pump.totalMl);
    if (data.t_cube !== undefined) s.temps.cube = toFinite(data.t_cube, s.temps.cube);
    if (data.t_column_bottom !== undefined) s.temps.columnBottom = toFinite(data.t_column_bottom, s.temps.columnBottom);
    if (data.valves && typeof data.valves === 'object') {
        s.valves = { ...s.valves, ...data.valves };
    }
    if (data.volume_heads !== undefined) s.volumes.heads = toFinite(data.volume_heads, s.volumes.heads);
    if (data.volume_body !== undefined) s.volumes.body = toFinite(data.volume_body, s.volumes.body);
    if (data.volume_tails !== undefined) s.volumes.tails = toFinite(data.volume_tails, s.volumes.tails);

    if (data.phase_elapsed_sec !== undefined) s.progress.phaseElapsedSec = toFinite(data.phase_elapsed_sec, s.progress.phaseElapsedSec);
    if (data.phase_target_sec !== undefined) s.progress.phaseTargetSec = toFinite(data.phase_target_sec, s.progress.phaseTargetSec);
    if (data.phase_remaining_sec !== undefined) s.progress.phaseRemainingSec = toFinite(data.phase_remaining_sec, s.progress.phaseRemainingSec);
    if (data.phase_percent !== undefined) s.progress.phasePercent = clampPercent(data.phase_percent);

    if (data.mashing && typeof data.mashing === 'object') {
        s.mashing = { ...s.mashing, ...data.mashing };
    }
    if (data.hold && typeof data.hold === 'object') {
        s.hold = { ...s.hold, ...data.hold };
    }
    if (data.progress && typeof data.progress === 'object') {
        s.progress = { ...s.progress, ...data.progress };
    }
    if (data.rectification && typeof data.rectification === 'object') {
        s.rectification = { ...s.rectification, ...data.rectification };
    }
    if (data.distillation && typeof data.distillation === 'object') {
        s.distillation = { ...s.distillation, ...data.distillation };
    }
    if (data.nbk && typeof data.nbk === 'object') {
        s.nbk = { ...s.nbk, ...data.nbk };
    }
    if (data.fermentation && typeof data.fermentation === 'object') {
        s.fermentation = { ...s.fermentation, ...data.fermentation };
    }
    if (data.nbkPhase !== undefined) s.nbk.phase = toFinite(data.nbkPhase, s.nbk.phase);
    if (data.nbkPhaseStr !== undefined) s.nbk.phaseStr = String(data.nbkPhaseStr);
    if (data.fermPhase !== undefined) s.fermentation.phase = toFinite(data.fermPhase, s.fermentation.phase);
    if (data.fermPhaseStr !== undefined) s.fermentation.phaseStr = String(data.fermPhaseStr);

    mergeSafetySettingsState(s, data);
    mergeEquipmentState(s, data);
    mergeStirrerState(s, data);
}

export function estimateRectTargets(rect, abvPercentOverride = null) {
    const feedVolumeL = toFinite(rect.feedVolumeL, 0);
    const feedAbv = abvPercentOverride === null
        ? toFinite(rect.feedAbvPercent, 0)
        : normalizeAbvPercent(abvPercentOverride, toFinite(rect.feedAbvPercent, 0));
    const absoluteAlcoholMl = Math.max(0, feedVolumeL * 1000 * (feedAbv / 100));
    const heads = absoluteAlcoholMl * (toFinite(rect.headsPercent, 0) / 100);
    const body = absoluteAlcoholMl * (toFinite(rect.bodyPercent, 0) / 100);
    const tails = absoluteAlcoholMl * (toFinite(rect.tailsPercent, 0) / 100);
    return { heads, body, tails };
}
