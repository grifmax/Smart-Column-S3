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

function mergeV2State(s, data) {
    const v2 = (data?.v2 && typeof data.v2 === 'object') ? data.v2 : null;
    if (!v2) return;

    const safety = (v2?.safety && typeof v2.safety === 'object') ? v2.safety : null;
    s.v2 = {
        ...s.v2,
        available: v2.available !== undefined ? Boolean(v2.available) : s.v2.available,
        safetyLatched: v2.safetyLatched !== undefined ? Boolean(v2.safetyLatched) : s.v2.safetyLatched,
        lastReasonCode: v2.lastReasonCode !== undefined ? String(v2.lastReasonCode) : s.v2.lastReasonCode,
        operatorMessage: v2.operatorMessage !== undefined ? String(v2.operatorMessage) : s.v2.operatorMessage,
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
    mergeV2State(s, data);

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
    mergeV2State(s, data);

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
