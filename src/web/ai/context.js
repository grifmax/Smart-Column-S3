// ============================================================================
// Context packs: what actually goes to the model
// ============================================================================
//
// The device holds far more data than an LLM prompt can take. These builders
// turn live status, one finished run and the event log into small, stable,
// self-describing JSON objects. Every number is rounded and every optional
// block is omitted when the hardware does not provide it.

function num(value, fallback = null) {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : fallback;
}

function round(value, digits = 2) {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) return null;
    const factor = 10 ** digits;
    return Math.round(parsed * factor) / factor;
}

function put(target, key, value) {
    if (value === null || value === undefined || value === '') return;
    target[key] = value;
}

function formatUptime(ms) {
    const totalSec = Math.max(0, Math.round(Number(ms || 0) / 1000));
    const h = Math.floor(totalSec / 3600);
    const m = Math.floor((totalSec % 3600) / 60);
    const s = totalSec % 60;
    return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

function pickTemps(temps) {
    const out = {};
    const source = temps && typeof temps === 'object' ? temps : {};
    ['cube', 'columnBottom', 'columnTop', 'deflegmator', 'reflux', 'tsa', 'waterIn', 'waterOut'].forEach((key) => {
        const value = num(source[key]);
        if (value !== null) out[key] = round(value, 1);
    });
    return out;
}

const INDICATOR_KEYS = [
    'processHealth',
    'telemetryCoverage',
    'decisionTrust',
    'stabilityIndex',
    'floodRisk',
    'coolingMarginC',
    'heatingRateCPerMin',
    'topTempRateCPerMin',
    'pressureRateMmHgPerMin',
    'takeoffAllowed',
    'degradedModeActive',
    'tempInBand',
    'overshootRisk',
    'powerLimited'
];

// Compact live picture (~0.5 KB). Sent with every request so the model always
// talks about the current state instead of the state from ten messages ago.
export function buildStatePack(status) {
    if (!status || typeof status !== 'object') return null;

    const pack = {};
    put(pack, 'mode', status.modeStr || status.mode);
    put(pack, 'phase', status.phaseStr);
    put(pack, 'paused', Boolean(status.paused));
    put(pack, 'safetyOk', status.safetyOk !== false);
    put(pack, 'uptime', formatUptime(status.uptime));

    const alarm = status.alarm;
    if (alarm && alarm.active) {
        pack.alarm = {
            type: alarm.type,
            level: alarm.level,
            message: String(alarm.message || '').slice(0, 160),
            acknowledged: Boolean(alarm.acknowledged)
        };
    }

    pack.temps = pickTemps(status.temps);

    const power = status.power || {};
    const powerPack = {};
    put(powerPack, 'setW', round(power.setW, 0));
    put(powerPack, 'actualW', round(power.power, 0));
    put(powerPack, 'dutyPercent', round(power.setPercent, 0));
    put(powerPack, 'voltage', round(power.voltage, 1));
    put(powerPack, 'current', round(power.current, 2));
    put(powerPack, 'errorW', round(power.errorW, 0));
    put(powerPack, 'closedLoop', Boolean(power.closedLoopActive));
    if (Object.keys(powerPack).length) pack.power = powerPack;

    const pump = status.pump || {};
    pack.pump = {
        running: Boolean(pump.running),
        speedMlH: round(pump.speedMlH, 0),
        totalMl: round(pump.totalMl, 0)
    };

    const pressure = status.pressure || {};
    if (num(pressure.cube) !== null) {
        pack.pressure = { cubeMmHg: round(pressure.cube, 1), atmMmHg: round(pressure.atm, 1) };
    }

    const profile = status.activeProfile;
    if (profile && profile.name) {
        pack.profile = { name: profile.name, id: profile.id, category: profile.category };
    }

    const v2 = status.v2 || {};
    const guidance = v2.guidance;
    if (guidance && (guidance.title || guidance.detail)) {
        pack.guidance = {
            tone: guidance.tone,
            title: String(guidance.title || '').slice(0, 120),
            detail: String(guidance.detail || '').slice(0, 260),
            action: String(guidance.action || '').slice(0, 180)
        };
    }

    const insight = v2.reasonInsight;
    if (insight && (insight.title || insight.detail)) {
        pack.reason = {
            title: String(insight.title || '').slice(0, 120),
            detail: String(insight.detail || '').slice(0, 220)
        };
    }

    const indicators = v2.indicators || {};
    const indicatorPack = {};
    INDICATOR_KEYS.forEach((key) => {
        const value = indicators[key];
        if (value === undefined || value === null) return;
        indicatorPack[key] = typeof value === 'number' ? round(value, 2) : value;
    });
    if (Object.keys(indicatorPack).length) pack.indicators = indicatorPack;

    const limits = v2.activeLimits || {};
    const limitPack = {};
    ['powerCapped', 'pumpCapped', 'takeoffBlocked', 'phaseAdvanceBlocked', 'antiOscillationActive'].forEach((key) => {
        if (limits[key]) limitPack[key] = true;
    });
    put(limitPack, 'maxHeaterPowerPercent', round(limits.maxHeaterPowerPercent, 0));
    if (Object.keys(limitPack).length) pack.limits = limitPack;

    const running = status.progress;
    if (running && typeof running === 'object') {
        const progress = {};
        ['percent', 'phase', 'volumeMl', 'targetMl'].forEach((key) => {
            const value = num(running[key]);
            if (value !== null) progress[key] = round(value, 1);
        });
        if (Object.keys(progress).length) pack.progress = progress;
    }

    return pack;
}

function downsampleSeries(points, maxPoints = 40) {
    if (!Array.isArray(points) || points.length === 0) return [];
    const step = Math.max(1, Math.ceil(points.length / maxPoints));
    const out = [];
    for (let i = 0; i < points.length; i += step) {
        const point = points[i] || {};
        out.push({
            t: point.time,
            cube: round(point.cube, 1),
            top: round(point.columnTop, 1),
            bottom: round(point.columnBottom, 1),
            power: round(point.power, 0),
            pump: round(point.pumpSpeed, 0)
        });
    }
    const last = points[points.length - 1];
    if (last && out[out.length - 1]?.t !== last.time) {
        out.push({ t: last.time, cube: round(last.cube, 1), top: round(last.columnTop, 1), bottom: round(last.columnBottom, 1), power: round(last.power, 0), pump: round(last.pumpSpeed, 0) });
    }
    return out;
}

function pickControl(control) {
    if (!control || typeof control !== 'object') return null;
    const out = {};
    [
        'durationSec', 'pausedSec', 'degradedSec', 'saturationSec', 'unpoweredSec',
        'takeoffAllowedSec', 'inBandSec', 'safetyTrips',
        'heaterReqAvgW', 'heaterReqMaxW', 'heaterAppliedAvgPct', 'heaterAppliedMaxPct',
        'powerAvgW', 'powerMaxW', 'energyWh', 'pumpAvgMlH', 'pumpMaxMlH',
        'tempMinC', 'tempAvgC', 'tempMaxC', 'overshootMaxC',
        'healthMin', 'healthAvg', 'stabilityAvg', 'floodRiskMax',
        'coolingMarginMin', 'pressureRateAvg', 'pressureRateMax', 'pressureMarginMin'
    ].forEach((key) => {
        const value = num(control[key]);
        if (value !== null) out[key] = round(value, 2);
    });
    return Object.keys(out).length ? out : null;
}

// One finished run, compact enough to paste into a prompt (~2-4 KB).
export function buildRunPack(history) {
    if (!history || typeof history !== 'object') return null;

    const meta = history.metadata || {};
    const process = history.process || {};
    const parameters = history.parameters || {};
    const metrics = history.metrics || {};
    const results = history.results || {};

    const pack = {
        id: history.id,
        version: history.version,
        type: process.type,
        mode: process.mode,
        profile: process.profile || null,
        startTime: meta.startTime,
        durationSec: meta.duration,
        completed: Boolean(meta.completedSuccessfully),
        status: results.status
    };

    const params = {};
    ['targetPower', 'headVolume', 'bodyVolume', 'tailVolume', 'pumpSpeedHead', 'pumpSpeedBody', 'stabilizationTime'].forEach((key) => {
        const value = num(parameters[key]);
        if (value !== null) params[key] = round(value, 1);
    });
    ['wattControlEnabled', 'smartDecrementEnabled'].forEach((key) => {
        if (parameters[key] !== undefined) params[key] = Boolean(parameters[key]);
    });
    if (Object.keys(params).length) pack.parameters = params;

    const temps = metrics.temperatures || {};
    const metricPack = {};
    ['cube', 'columnTop', 'columnBottom', 'deflegmator'].forEach((key) => {
        const block = temps[key];
        if (block && typeof block === 'object') {
            metricPack[key] = {
                min: round(block.min, 1),
                max: round(block.max, 1),
                avg: round(block.avg, 1),
                final: round(block.final, 1)
            };
        }
    });
    const powerMetrics = metrics.power || {};
    const pumpMetrics = metrics.pump || {};
    const agg = {};
    put(agg, 'energyKwh', round(powerMetrics.energyUsed, 3));
    put(agg, 'avgPowerW', round(powerMetrics.avgPower, 0));
    put(agg, 'peakPowerW', round(powerMetrics.peakPower, 0));
    put(agg, 'totalVolumeMl', round(pumpMetrics.totalVolume, 0));
    put(agg, 'avgSpeedMlH', round(pumpMetrics.avgSpeed, 0));
    if (Object.keys(agg).length) metricPack.summary = agg;
    if (Object.keys(metricPack).length) pack.metrics = metricPack;

    const phases = Array.isArray(history.phases) ? history.phases : [];
    pack.phases = phases.map((phase) => {
        const item = {
            name: phase.name,
            durationSec: phase.duration,
            startTemp: round(phase.startTemp, 1),
            endTemp: round(phase.endTemp, 1),
            volumeMl: phase.volume,
            avgSpeedMlH: phase.avgSpeed
        };
        if (phase.reasonCode) item.reason = phase.reasonCode;
        const control = pickControl(phase.control);
        if (control) item.control = control;
        return item;
    });

    const runControl = pickControl(history.controlStats);
    if (runControl) pack.controlStats = runControl;

    const advisor = history.advisorSnapshot;
    if (advisor && Array.isArray(advisor.items) && advisor.items.length) {
        pack.advisor = {
            baseline: advisor.baselineProcessId || null,
            items: advisor.items.slice(0, 12).map((item) => ({
                code: item.code,
                tone: item.tone,
                title: String(item.title || '').slice(0, 120),
                parameter: item.parameterKey || null,
                previous: item.previousValue,
                suggested: item.suggestedValue
            }))
        };
    }

    const events = [];
    (results.errors || []).slice(0, 8).forEach((error) => {
        events.push({ severity: 'error', time: error.time, message: String(error.message || '').slice(0, 160), reason: error.reasonCode || null });
    });
    (results.warnings || []).slice(0, 12).forEach((warning) => {
        events.push({ severity: 'warning', time: warning.time, message: String(warning.message || '').slice(0, 160), reason: warning.reasonCode || null });
    });
    if (events.length) pack.events = events;
    if (history.notes) pack.notes = String(history.notes).slice(0, 300);

    const series = downsampleSeries(history.timeseries?.data);
    if (series.length) pack.series = series;

    return pack;
}

// Event log digest: counts + a readable tail. The full CSV stays on the
// device; the model gets structure, not kilobytes.
export function buildLogDigest(payload, limit = 30) {
    const events = Array.isArray(payload?.events) ? payload.events : [];
    const counts = { info: 0, warning: 0, error: 0 };
    events.forEach((event) => {
        const level = String(event.levelStr || '').toLowerCase();
        if (level === 'error') counts.error += 1;
        else if (level === 'warn' || level === 'warning') counts.warning += 1;
        else counts.info += 1;
    });

    const tail = events.slice(-limit).map((event) => ({
        seq: event.seq,
        t: formatUptime(event.timestamp),
        level: event.levelStr,
        kind: event.kind,
        message: String(event.message || '').replace(/\s+/g, ' ').slice(0, 220),
        reason: event.reasonCode || null
    }));

    return { counts, tail };
}

// Minimal, explicit comparison so the model does not have to diff JSON itself.
export function compareRunPacks(runA, runB) {
    const pick = (run) => ({
        id: run?.id,
        durationSec: run?.durationSec,
        phases: Object.fromEntries((run?.phases || []).map((phase) => [phase.name, phase])),
        metrics: run?.metrics || {},
        control: run?.controlStats || {}
    });

    const a = pick(runA);
    const b = pick(runB);
    const controlDelta = {};
    Object.keys(b.control || {}).forEach((key) => {
        if (typeof b.control[key] === 'number' && typeof a.control[key] === 'number') {
            controlDelta[key] = round(b.control[key] - a.control[key], 2);
        }
    });

    const phaseDelta = {};
    const names = new Set([...Object.keys(a.phases), ...Object.keys(b.phases)]);
    names.forEach((name) => {
        const pa = a.phases[name];
        const pb = b.phases[name];
        if (!pa || !pb) {
            phaseDelta[name] = { onlyIn: pa ? a.id : b.id };
            return;
        }
        phaseDelta[name] = {
            durationSec: round((pb.durationSec || 0) - (pa.durationSec || 0), 0),
            avgSpeedMlH: round((pb.avgSpeedMlH || 0) - (pa.avgSpeedMlH || 0), 0),
            volumeMl: round((pb.volumeMl || 0) - (pa.volumeMl || 0), 0)
        };
    });

    return {
        baseId: a.id,
        compareId: b.id,
        durationDeltaSec: round((b.durationSec || 0) - (a.durationSec || 0), 0),
        controlDelta,
        phaseDelta
    };
}
