import {
    runtimeMonitorState,
    resolveMode,
    MODE_IDLE,
    MODE_RECT,
    MODE_DIST,
    MODE_MANUAL,
    MODE_MASH,
    MODE_HOLD
} from '../globals.js';
import { getEffectiveAbvForCalculations } from '../runtime/abv.js';
import { addLog } from '../core/logs.js';

const MODE_SCHEME_PATHS = {
    [MODE_IDLE]: 'schemes/column-animated.svg',
    [MODE_RECT]: 'schemes/column-animated.svg',
    [MODE_MANUAL]: 'schemes/column-animated.svg',
    [MODE_DIST]: 'schemes/distillation-animated.svg',
    [MODE_MASH]: 'schemes/mash-animated.svg',
    [MODE_HOLD]: 'schemes/hold-animated.svg'
};

const MANUAL_RECT_STORAGE_KEY = 'control.manualRectSettings';
const CUBE_LIQUID_VISIBLE_TOP_Y = 648;
const CUBE_LIQUID_BOTTOM_Y = 743;
const GRADIENT_TOP_Y = 38;
const GRADIENT_LID_TOP_Y = 488;
const GRADIENT_CUBE_TOP_Y = 553;
const BOILING_TEMP_TO_CUBE_ABV_TABLE = [
    { tempC: 78.2, abvPercent: 96 },
    { tempC: 79.0, abvPercent: 92 },
    { tempC: 80.0, abvPercent: 88 },
    { tempC: 81.0, abvPercent: 84 },
    { tempC: 82.0, abvPercent: 78 },
    { tempC: 83.0, abvPercent: 72 },
    { tempC: 84.0, abvPercent: 66 },
    { tempC: 85.0, abvPercent: 60 },
    { tempC: 86.0, abvPercent: 54 },
    { tempC: 87.0, abvPercent: 47 },
    { tempC: 88.0, abvPercent: 40 },
    { tempC: 89.0, abvPercent: 34 },
    { tempC: 90.0, abvPercent: 28 },
    { tempC: 91.0, abvPercent: 23 },
    { tempC: 92.0, abvPercent: 18 },
    { tempC: 93.0, abvPercent: 14 },
    { tempC: 94.0, abvPercent: 10 },
    { tempC: 95.0, abvPercent: 7 },
    { tempC: 96.0, abvPercent: 4.5 },
    { tempC: 97.0, abvPercent: 2.5 },
    { tempC: 98.0, abvPercent: 1.2 },
    { tempC: 99.0, abvPercent: 0.5 },
    { tempC: 100.0, abvPercent: 0 }
];

function getSchemePathForData(data) {
    const mode = resolveMode(
        data?.mode ?? runtimeMonitorState.mode,
        data?.modeStr ?? runtimeMonitorState.modeStr
    );
    return MODE_SCHEME_PATHS[mode] || MODE_SCHEME_PATHS[MODE_RECT];
}

function ensureActiveScheme(obj, data) {
    const desiredPath = getSchemePathForData(data);
    const currentPath = obj.dataset.schemePath || obj.getAttribute('data') || '';

    if (!obj.dataset.schemePath) {
        obj.dataset.schemePath = currentPath;
    }

    if (currentPath === desiredPath) return false;

    obj.dataset.schemePath = desiredPath;
    obj.data = desiredPath; // Браузер сам перезагрузит src
    return true;
}

function getNestedNumber(obj, key) {
    const value = obj && typeof obj === 'object' ? obj[key] : undefined;
    const num = Number(value);
    return Number.isFinite(num) ? num : undefined;
}

function getStatusNumber(data, nestedRoot, nestedKey, flatKey) {
    const nested = getNestedNumber(data?.[nestedRoot], nestedKey);
    if (nested !== undefined) return nested;
    const flat = Number(data?.[flatKey]);
    return Number.isFinite(flat) ? flat : undefined;
}

function getPowerActualW(data) {
    const nested = getNestedNumber(data?.power, 'power');
    if (nested !== undefined) return nested;
    const flat = Number(data?.power);
    return Number.isFinite(flat) ? flat : undefined;
}

function getPowerSetPercent(data) {
    const fromPowerObject = getNestedNumber(data?.power, 'setPercent');
    if (fromPowerObject !== undefined) return fromPowerObject;
    const fromDistillation = getNestedNumber(data?.distillation, 'powerPercent');
    if (fromDistillation !== undefined) return fromDistillation;
    const runtime = Number(runtimeMonitorState?.distillation?.powerPercent);
    return Number.isFinite(runtime) ? runtime : undefined;
}

function getPositiveFiniteNumber(...candidates) {
    for (const value of candidates) {
        const num = Number(value);
        if (Number.isFinite(num) && num > 0) return num;
    }
    return undefined;
}

function readManualRectSettings() {
    try {
        const raw = localStorage.getItem(MANUAL_RECT_STORAGE_KEY);
        if (!raw) return null;
        const parsed = JSON.parse(raw);
        return parsed && typeof parsed === 'object' ? parsed : null;
    } catch {
        return null;
    }
}

function isTailsPwmEnabled() {
    const tailsPwmCheckbox = document.getElementById('manual-tails-pwm-enabled');
    if (tailsPwmCheckbox) {
        return Boolean(tailsPwmCheckbox.checked);
    }

    const parsed = readManualRectSettings();
    return Boolean(parsed?.tails?.pwmEnabled);
}

function isManualTailsEnabled() {
    const tailsEnabledCheckbox = document.getElementById('manual-tails-enabled');
    if (tailsEnabledCheckbox) {
        return Boolean(tailsEnabledCheckbox.checked);
    }

    const parsed = readManualRectSettings();
    return Boolean(parsed?.tails?.enabled);
}

function getManualFeedSetup() {
    const parsed = readManualRectSettings();
    const domFeedVolume = Number(document.getElementById('manual-feed-volume')?.value);
    const domFeedAbv = Number(document.getElementById('manual-feed-abv')?.value);

    const feedVolumeL = getPositiveFiniteNumber(
        domFeedVolume,
        parsed?.feed?.volumeL,
        runtimeMonitorState?.rectification?.feedVolumeL,
        20
    );

    const feedAbvPercent = getPositiveFiniteNumber(
        domFeedAbv,
        parsed?.feed?.abvPercent,
        runtimeMonitorState?.rectification?.feedAbvPercent,
        40
    );

    return {
        volumeL: Number(feedVolumeL) || 20,
        abvPercent: Math.max(0, Math.min(100, Number(feedAbvPercent) || 40)),
        settings: parsed || {}
    };
}

function interpolateByTemp(table, tempC) {
    const t = Number(tempC);
    if (!Number.isFinite(t)) return undefined;
    if (table.length === 0) return undefined;
    if (t <= table[0].tempC) return table[0].abvPercent;

    for (let i = 1; i < table.length; i++) {
        const prev = table[i - 1];
        const next = table[i];
        if (t <= next.tempC) {
            const span = next.tempC - prev.tempC;
            if (span <= 0) return next.abvPercent;
            const ratio = (t - prev.tempC) / span;
            return prev.abvPercent + ((next.abvPercent - prev.abvPercent) * ratio);
        }
    }

    return table[table.length - 1].abvPercent;
}

function getCubeAbvByBoilingTemp(tempC, fallbackAbvPercent) {
    const byTable = interpolateByTemp(BOILING_TEMP_TO_CUBE_ABV_TABLE, tempC);
    if (Number.isFinite(byTable)) return Math.max(0, Math.min(100, byTable));
    return Math.max(0, Math.min(100, Number(fallbackAbvPercent) || 0));
}

function pickRectTargets(runtimeState, manualSettings, absoluteAlcoholMl, tailsEnabled) {
    const rect = runtimeState?.rectification || {};
    const headsFromPercent = absoluteAlcoholMl * ((Number(rect.headsPercent) || 0) / 100);
    const bodyFromPercent = absoluteAlcoholMl * ((Number(rect.bodyPercent) || 0) / 100);
    const tailsFromPercent = absoluteAlcoholMl * ((Number(rect.tailsPercent) || 0) / 100);

    const headsTargetMl = getPositiveFiniteNumber(
        rect.headsTargetMl,
        manualSettings?.heads?.volume,
        headsFromPercent,
        300
    ) || 300;

    const tailsTargetMl = tailsEnabled
        ? (getPositiveFiniteNumber(
            rect.tailsTargetMl,
            tailsFromPercent,
            800
        ) || 800)
        : 1;

    const bodyFromRemainder = Math.max(0, absoluteAlcoholMl - headsTargetMl - (tailsEnabled ? tailsTargetMl : 0));
    const bodyTargetMl = getPositiveFiniteNumber(
        rect.bodyTargetMl,
        bodyFromRemainder,
        bodyFromPercent,
        3000
    ) || 3000;

    return {
        headsTargetMl,
        bodyTargetMl,
        tailsTargetMl
    };
}

function setOptionalDisplay(el, visible) {
    if (!el) return;
    el.style.display = visible ? '' : 'none';
}

function ensurePumpSpinKeyframes(svg) {
    if (svg.getElementById('pump-spin-style')) return;
    const style = svg.createElementNS('http://www.w3.org/2000/svg', 'style');
    style.setAttribute('id', 'pump-spin-style');
    style.textContent = '@keyframes pump-spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }';
    svg.documentElement.appendChild(style);
}

function updatePumpImpellerAnimation(svg, speedMlH) {
    const impeller = svg.getElementById('pump-impeller');
    if (!impeller) return;

    const speed = Number(speedMlH);
    const isRunning = Number.isFinite(speed) && speed > 0;

    if (!isRunning) {
        if (impeller._spinAnimation) {
            impeller._spinAnimation.cancel();
            impeller._spinAnimation = null;
        }
        impeller.style.animation = 'none';
        impeller.dataset.spinDurationMs = '';
        return;
    }

    // Визуальная скорость вращения привязана к расходу помпы:
    // малый расход -> медленнее, высокий -> быстрее.
    const durationMs = Math.max(300, Math.min(2400, 180000 / speed));
    const previousDuration = Number(impeller.dataset.spinDurationMs || 0);
    const needRestart = !impeller._spinAnimation || Math.abs(previousDuration - durationMs) > 80;

    if (needRestart) {
        if (impeller._spinAnimation) {
            impeller._spinAnimation.cancel();
            impeller._spinAnimation = null;
        }

        impeller.style.transformBox = 'fill-box';
        impeller.style.transformOrigin = 'center';

        if (typeof impeller.animate === 'function') {
            impeller._spinAnimation = impeller.animate(
                [
                    { transform: 'rotate(0deg)' },
                    { transform: 'rotate(360deg)' }
                ],
                {
                    duration: durationMs,
                    iterations: Infinity,
                    easing: 'linear'
                }
            );
        } else {
            ensurePumpSpinKeyframes(svg);
            impeller.style.animation = `pump-spin ${durationMs.toFixed(0)}ms linear infinite`;
        }

        impeller.dataset.spinDurationMs = durationMs.toFixed(0);
    } else if (impeller._spinAnimation?.playState === 'paused') {
        impeller._spinAnimation.play();
    }
}

function getTsaRiseRatePerMin(svg, tempCube, tempTsa) {
    const cube = Number(tempCube) || 0;
    const tsa = Number(tempTsa);
    if (!Number.isFinite(tsa)) return 0;

    const now = Date.now();
    if (!svg._tsaTrend) {
        svg._tsaTrend = { prevTsa: tsa, prevTs: now };
        return 0;
    }

    const prevTsa = Number(svg._tsaTrend.prevTsa);
    const prevTs = Number(svg._tsaTrend.prevTs);
    svg._tsaTrend.prevTsa = tsa;
    svg._tsaTrend.prevTs = now;

    if (cube < 45 || !Number.isFinite(prevTsa) || !Number.isFinite(prevTs)) {
        return 0;
    }

    const dtMs = now - prevTs;
    if (dtMs <= 0) return 0;
    const dtMin = dtMs / 60000;
    return (tsa - prevTsa) / dtMin;
}

function setLayerRect(layer, y, height, opacity) {
    if (!layer) return;
    const h = Math.max(0, Number(height) || 0);
    if (h <= 0.01) {
        layer.setAttribute('height', '0');
        layer.style.opacity = '0';
        return;
    }
    layer.setAttribute('y', Number(y).toFixed(2));
    layer.setAttribute('height', h.toFixed(2));
    layer.style.opacity = Number(opacity).toFixed(3);
}

function applyThreeLayerCoverage(
    coreLayer,
    lidLayer,
    cubeLayer,
    coverage,
    fromTop,
    opacity,
    topY,
    lidTopY,
    cubeTopY,
    bottomY
) {
    const clamp01 = (v) => Math.max(0, Math.min(1, v));
    const t = Number(topY);
    const l = Number(lidTopY);
    const c = Number(cubeTopY);
    const b = Number(bottomY);
    if (!(b > t) || !(l > t) || !(c > l) || !(b > c)) {
        setLayerRect(coreLayer, t, 0, 0);
        setLayerRect(lidLayer, l, 0, 0);
        setLayerRect(cubeLayer, c, 0, 0);
        return;
    }

    const totalSpan = b - t;
    const coreSpan = l - t;
    const lidSpan = c - l;
    const cubeSpan = b - c;
    const targetHeight = totalSpan * clamp01(coverage);

    let coreHeight;
    let lidHeight;
    let cubeHeight;
    if (fromTop) {
        coreHeight = Math.min(coreSpan, targetHeight);
        const remAfterCore = Math.max(0, targetHeight - coreHeight);
        lidHeight = Math.min(lidSpan, remAfterCore);
        cubeHeight = Math.max(0, remAfterCore - lidHeight);
    } else {
        cubeHeight = Math.min(cubeSpan, targetHeight);
        const remAfterCube = Math.max(0, targetHeight - cubeHeight);
        lidHeight = Math.min(lidSpan, remAfterCube);
        coreHeight = Math.max(0, remAfterCube - lidHeight);
    }

    const coreY = fromTop ? t : (l - coreHeight);
    const lidY = fromTop ? l : (c - lidHeight);
    const cubeY = fromTop ? c : (b - cubeHeight);
    setLayerRect(coreLayer, coreY, coreHeight, opacity);
    setLayerRect(lidLayer, lidY, lidHeight, opacity);
    setLayerRect(cubeLayer, cubeY, cubeHeight, opacity);
}

function getFeedBoilingTempC() {
    const mode = resolveMode(runtimeMonitorState?.mode, runtimeMonitorState?.modeStr);
    const manualFeed = getManualFeedSetup();
    const feedAbv = getPositiveFiniteNumber(
        mode === MODE_MANUAL ? manualFeed.abvPercent : undefined,
        runtimeMonitorState?.rectification?.feedAbvPercent,
        runtimeMonitorState?.rectification?.feedABV,
        getEffectiveAbvForCalculations()?.value,
        40
    );
    const abv = Math.max(0, Math.min(100, Number(feedAbv) || 40));
    // Приближение для Ткип смеси в зависимости от крепости сырца.
    return Math.max(78.3, Math.min(100, 100 - (0.215 * abv)));
}

function stopBoilingBubble(bubble) {
    if (bubble._boilTimer) {
        clearTimeout(bubble._boilTimer);
        bubble._boilTimer = null;
    }
    if (bubble._boilAnimation) {
        bubble._boilAnimation.cancel();
        bubble._boilAnimation = null;
    }
    bubble._boilActive = false;
    bubble.style.opacity = '0';
    bubble.style.transform = '';
}

function startBoilingBubble(svg, bubble) {
    if (bubble._boilActive) return;
    bubble._boilActive = true;
    const startY = Number(bubble.dataset.startY || bubble.getAttribute('cy') || 716);
    bubble.dataset.startY = String(startY);

    const tick = () => {
        if (!bubble._boilActive) return;
        const delayMs = 80 + Math.random() * 520;
        bubble._boilTimer = setTimeout(() => {
            if (!bubble._boilActive) return;

            const liquidTopY = Number(svg._cubeLiquidTopY || CUBE_LIQUID_VISIBLE_TOP_Y);
            const targetY = Math.max(560, Math.min(CUBE_LIQUID_VISIBLE_TOP_Y + 14, liquidTopY + 6 + (Math.random() * 8)));
            const riseDistance = Math.max(24, startY - targetY);
            const driftX = -7 + Math.random() * 14;
            const durationMs = 700 + Math.random() * 1100;

            if (typeof bubble.animate === 'function') {
                bubble._boilAnimation = bubble.animate(
                    [
                        { opacity: 0, transform: 'translate(0px, 0px) scale(0.45)' },
                        { opacity: 0.7, transform: `translate(${(driftX * 0.45).toFixed(2)}px, ${(-riseDistance * 0.55).toFixed(2)}px) scale(1)` },
                        { opacity: 0, transform: `translate(${driftX.toFixed(2)}px, ${(-riseDistance).toFixed(2)}px) scale(1.2)` }
                    ],
                    {
                        duration: durationMs,
                        easing: 'cubic-bezier(0.22, 0.08, 0.3, 1)',
                        iterations: 1
                    }
                );
                bubble._boilAnimation.onfinish = () => {
                    bubble._boilAnimation = null;
                    tick();
                };
            } else {
                bubble.style.opacity = '0.75';
                bubble.style.transform = `translate(${driftX.toFixed(2)}px, ${(-riseDistance).toFixed(2)}px)`;
                bubble._boilTimer = setTimeout(() => {
                    if (!bubble._boilActive) return;
                    bubble.style.opacity = '0';
                    bubble.style.transform = '';
                    tick();
                }, durationMs);
            }
        }, delayMs);
    };

    tick();
}

function updateBoilingBubbles(svg, tempCube, powerActualW, liquidTopY) {
    const group = svg.getElementById('anim-boil-bubbles');
    if (!group) return;

    const tCube = Number(tempCube);
    const powerW = Number(powerActualW) || 0;
    const boilingTempC = getFeedBoilingTempC();
    const active = Number.isFinite(tCube) && tCube >= boilingTempC && powerW > 120;

    svg._cubeLiquidTopY = Number.isFinite(liquidTopY) ? liquidTopY : CUBE_LIQUID_VISIBLE_TOP_Y;
    group.style.opacity = active ? '1' : '0';

    const bubbles = group.querySelectorAll('.bubble');
    bubbles.forEach((bubble) => {
        if (active) startBoilingBubble(svg, bubble);
        else stopBoilingBubble(bubble);
    });
}

function updateColumnGradientLayers(svg, tempCube, tempColMid, tempReflux, tempTsa, pressureCube, liquidTopY) {
    const heatCore = svg.getElementById('anim-gradient-core-heat');
    const refluxCore = svg.getElementById('anim-gradient-core-reflux');
    const heatLid = svg.getElementById('anim-gradient-lid-heat');
    const refluxLid = svg.getElementById('anim-gradient-lid-reflux');
    const heatCube = svg.getElementById('anim-gradient-cube-heat');
    const refluxCube = svg.getElementById('anim-gradient-cube-reflux');
    if (!heatCore || !refluxCore || !heatLid || !refluxLid || !heatCube || !refluxCube) return;

    const tCube = Number(tempCube) || 0;
    const tMid = Number(tempColMid) || 0;
    const tReflux = Number(tempReflux) || 0;
    const tTsa = Number(tempTsa) || 0;
    const pCube = Number(pressureCube) || 0;
    const tsaRisePerMin = getTsaRiseRatePerMin(svg, tCube, tTsa);

    const clamp01 = (v) => Math.max(0, Math.min(1, v));
    const heatLevel = clamp01((tCube - 45) / 35);
    const refluxLevel = clamp01((tReflux - 65) / 18);
    const stabilityLevel = clamp01(1 - Math.abs(tMid - 76.6) / 3.5);
    const tsaHotLevel = clamp01((tTsa - 78.5) / 3.0);
    const pressureLevel = clamp01((pCube - 8) / 20);
    const tsaDeltaLevel = clamp01((tsaRisePerMin - 0.02) / 0.20);

    const redPush = Math.max(tsaHotLevel, pressureLevel, tsaDeltaLevel);
    let heatCoverage = 0.24 + (0.70 * heatLevel) + (0.18 * redPush);
    let refluxCoverage = 0.18 + (0.62 * refluxLevel) + (0.20 * stabilityLevel);

    // По мере разогрева красный слой вытесняет синий вверх.
    refluxCoverage *= (1 - Math.min(0.80, (heatLevel * 0.78) + (redPush * 0.52)));
    heatCoverage = Math.min(1, heatCoverage);
    refluxCoverage = Math.max(0.03, Math.min(1, refluxCoverage));

    const bottomY = Math.max(
        GRADIENT_CUBE_TOP_Y + 1,
        Math.min(CUBE_LIQUID_BOTTOM_Y, Number(liquidTopY) || CUBE_LIQUID_VISIBLE_TOP_Y)
    );
    const heatOpacity = Math.min(0.96, 0.10 + (heatLevel * 0.80) + (redPush * 0.22));
    const refluxOpacity = Math.max(0.08, (0.12 + refluxLevel * 0.76) * (1 - redPush * 0.58));

    applyThreeLayerCoverage(
        heatCore,
        heatLid,
        heatCube,
        heatCoverage,
        false,
        heatOpacity,
        GRADIENT_TOP_Y,
        GRADIENT_LID_TOP_Y,
        GRADIENT_CUBE_TOP_Y,
        bottomY
    );
    applyThreeLayerCoverage(
        refluxCore,
        refluxLid,
        refluxCube,
        refluxCoverage,
        true,
        refluxOpacity,
        GRADIENT_TOP_Y,
        GRADIENT_LID_TOP_Y,
        GRADIENT_CUBE_TOP_Y,
        bottomY
    );
}

function stopRefluxDropChaos(drop) {
    if (drop._refluxChaosTimer) {
        clearTimeout(drop._refluxChaosTimer);
        drop._refluxChaosTimer = null;
    }
    if (drop._refluxChaosAnimation) {
        drop._refluxChaosAnimation.cancel();
        drop._refluxChaosAnimation = null;
    }
    drop._refluxChaosActive = false;
    drop.style.opacity = '0';
    drop.style.transform = '';
}

function startRefluxDropChaos(drop) {
    if (drop._refluxChaosActive) return;
    drop._refluxChaosActive = true;

    const tick = () => {
        if (!drop._refluxChaosActive) return;

        const delayMs = 120 + Math.random() * 950;
        drop._refluxChaosTimer = setTimeout(() => {
            if (!drop._refluxChaosActive) return;

            const durationMs = 450 + Math.random() * 1200;
            const driftStart = -0.18 + (Math.random() * 0.36);
            const driftEnd = -0.22 + (Math.random() * 0.44);
            const endDrop = 128 + Math.random() * 52;

            if (typeof drop.animate === 'function') {
                drop._refluxChaosAnimation = drop.animate(
                    [
                        { opacity: 0, transform: `translate(${driftStart.toFixed(2)}px, 0px) scale(0.92)` },
                        { opacity: 0.95, transform: `translate(${driftStart.toFixed(2)}px, ${(endDrop * 0.32).toFixed(2)}px) scale(1)` },
                        { opacity: 0, transform: `translate(${driftEnd.toFixed(2)}px, ${endDrop.toFixed(2)}px) scale(0.58)` }
                    ],
                    {
                        duration: durationMs,
                        easing: 'cubic-bezier(0.25, 0.05, 0.35, 1)',
                        iterations: 1
                    }
                );
                drop._refluxChaosAnimation.onfinish = () => {
                    drop._refluxChaosAnimation = null;
                    tick();
                };
            } else {
                // Fallback без Web Animations API.
                drop.style.opacity = '0.9';
                drop.style.transform = `translate(${driftEnd.toFixed(2)}px, ${endDrop.toFixed(2)}px)`;
                drop._refluxChaosTimer = setTimeout(() => {
                    if (!drop._refluxChaosActive) return;
                    drop.style.opacity = '0';
                    drop.style.transform = '';
                    tick();
                }, durationMs);
            }
        }, delayMs);
    };

    tick();
}

function updateRefluxDropChaos(svg, tempReflux) {
    const zone = svg.getElementById('zone-reflux');
    if (!zone) return;

    const active = Number(tempReflux) >= 65;
    zone.classList.toggle('condensing', active);

    const drops = zone.querySelectorAll('.drop');
    drops.forEach((drop) => {
        if (active) startRefluxDropChaos(drop);
        else stopRefluxDropChaos(drop);
    });
}


function setValveClass(svg, valveId, opened) {
    const el = svg.getElementById(valveId);
    if (!el) return;
    el.classList.toggle('valve-open', Boolean(opened));
    el.classList.toggle('valve-closed', !opened);
}

async function postJson(url, payload) {
    const response = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    });
    if (!response.ok) {
        const text = await response.text();
        throw new Error(text || `HTTP ${response.status}`);
    }
}

async function setManualValve(svg, valveKey, opened) {
    const valveMap = {
        water: 'svg-valve-water',
        heads: 'svg-valve-heads',
        uno: 'svg-valve-uno'
    };
    const valveId = valveMap[valveKey];
    if (!valveId) return;

    setValveClass(svg, valveId, opened);
    try {
        await postJson('/api/manual/valves', { [valveKey]: opened });
        runtimeMonitorState.valves = { ...runtimeMonitorState.valves, [valveKey]: opened };
        addLog(`Valve ${valveKey}: ${opened ? 'open' : 'closed'}`, 'info');
    } catch (error) {
        setValveClass(svg, valveId, !opened);
        addLog(`Valve ${valveKey}: request failed (${error.message})`, 'error');
    }
}

function applySchemeTheme(svg) {
    const theme = document.body?.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
    if (svg._appliedTheme === theme) return;
    svg._appliedTheme = theme;

    const isDark = theme === 'dark';
    const strokeColor = isDark ? '#d5dbe4' : '#000000';
    const labelColor = isDark ? '#e8edf5' : '#333333';
    const bodyFill = isDark ? '#2c3138' : '#ffffff';

    svg.querySelectorAll('.struct').forEach((el) => {
        el.style.stroke = strokeColor;
        el.style.fill = bodyFill;
    });
    svg.querySelectorAll('.struct-line,.pipe,.jar,.valve').forEach((el) => {
        el.style.stroke = strokeColor;
    });
    svg.querySelectorAll('.label-text,.indicator-text').forEach((el) => {
        el.style.fill = labelColor;
    });

    // In dark theme some scheme lines use hardcoded stroke="#000" in SVG attributes.
    // Promote those contours to a readable color without touching colored process lines.
    svg.querySelectorAll('[stroke]').forEach((el) => {
        const attrStroke = String(el.getAttribute('stroke') || '').trim().toLowerCase();
        const hardcodedBlack = attrStroke === '#000' || attrStroke === '#000000' || attrStroke === 'black';
        if (hardcodedBlack) {
            el.style.stroke = isDark ? strokeColor : '';
        }
    });
}
function initSchemeInteractions(svg) {
    if (svg._interactiveBound) return;
    svg._interactiveBound = true;

    const bindValve = (id, key) => {
        const valve = svg.getElementById(id);
        if (!valve) return;
        valve.style.cursor = 'pointer';
        valve.addEventListener('click', (event) => {
            event.preventDefault();
            event.stopPropagation();
            const nextState = !valve.classList.contains('valve-open');
            setManualValve(svg, key, nextState);
        });
    };

    bindValve('svg-valve-water', 'water');
    bindValve('svg-valve-heads', 'heads');
    bindValve('svg-valve-uno', 'uno');

    const tailsValve = svg.getElementById('svg-valve-tails');
    if (tailsValve) {
        tailsValve.style.cursor = 'not-allowed';
        tailsValve.addEventListener('click', (event) => {
            event.preventDefault();
            event.stopPropagation();
            addLog('Tails valve control is not available in current hardware', 'warning');
        });
    }

}

// ============================================================================
// Интерактивная схема (SVG)
// ============================================================================

export function updateInteractiveScheme(data) {
    const obj = document.querySelector('.operator-scheme');
    if (!obj) return;

    if (ensureActiveScheme(obj, data)) {
        if (!obj._schemeLoadPending) {
            obj._schemeLoadPending = true;
            obj.addEventListener('load', () => {
                obj._schemeLoadPending = false;
                updateInteractiveScheme(data);
            }, { once: true });
        }
        return;
    }

    // Доступ к документу внутри <object>
    const svg = obj.contentDocument;
    if (!svg) {
        // SVG ещё не загружен — подписываемся на load для повторного вызова
        if (!obj._schemeLoadPending) {
            obj._schemeLoadPending = true;
            obj.addEventListener('load', () => {
                obj._schemeLoadPending = false;
                updateInteractiveScheme(data);
            }, { once: true });
        }
        return;
    }

    // Обновление температур на схеме
    applySchemeTheme(svg);
    initSchemeInteractions(svg);

    const currentMode = resolveMode(
        data?.mode ?? runtimeMonitorState.mode,
        data?.modeStr ?? runtimeMonitorState.modeStr
    );
    const manualFeedSetup = getManualFeedSetup();
    const manualSettings = manualFeedSetup.settings || {};
    const manualTailsEnabled = isManualTailsEnabled();

    // Левая хвостовая ветка отображается только при включенном "Хвосты: ШИМ-отбор".
    const showLeftTailsBranch = isTailsPwmEnabled();
    setOptionalDisplay(svg.getElementById('zone-tails-left-branch'), showLeftTailsBranch);
    setOptionalDisplay(svg.getElementById('ind-volume-tails'), showLeftTailsBranch);

    // Правая хвостовая ветка (клапан+банка+патрубок) скрывается при выключенном отборе хвостов в ручном режиме.
    const showRightTailsBranch = currentMode === MODE_MANUAL ? manualTailsEnabled : true;
    setOptionalDisplay(svg.getElementById('collector-tails-pipe'), showRightTailsBranch);
    setOptionalDisplay(svg.getElementById('pipe-tails-down'), showRightTailsBranch);
    setOptionalDisplay(svg.getElementById('svg-valve-tails'), showRightTailsBranch);
    setOptionalDisplay(svg.getElementById('jar-tails-right'), showRightTailsBranch);
    setOptionalDisplay(svg.getElementById('label-tails-right'), showRightTailsBranch);
    setOptionalDisplay(svg.getElementById('ind-volume-body-small'), showRightTailsBranch);
    setOptionalDisplay(svg.getElementById('drop-tails'), showRightTailsBranch);
    setOptionalDisplay(svg.getElementById('anim-liquid-tails'), showRightTailsBranch);

    const tempCube = getStatusNumber(data, 'temps', 'cube', 't_cube');
    const tempColTop = getStatusNumber(data, 'temps', 'columnTop', 't_column_top');
    const tempColMid = getStatusNumber(data, 'temps', 'columnMiddle', 't_column_bottom');
    const tempNode = getStatusNumber(data, 'temps', 'product', 't_product');
    const tempReflux = getStatusNumber(data, 'temps', 'reflux', 't_reflux');
    const tempTsa = getStatusNumber(data, 'temps', 'tsa', 't_tsa');
    const tempWaterIn = getStatusNumber(data, 'temps', 'waterIn', 't_water_in');
    const tempWaterOut = getStatusNumber(data, 'temps', 'waterOut', 't_water_out');

    const setTxt = (id, val, unit = '') => {
        const el = svg.getElementById(id);
        if (el && val !== undefined) el.textContent = `${val.toFixed(1)}${unit}`;
    };
    setTxt('txt-temp-cube', tempCube, ' °C');
    setTxt('txt-temp-col-top', tempColTop, ' °C');
    setTxt('txt-temp-col-mid', tempColMid, ' °C');
    setTxt('txt-temp-node', tempNode, ' °C');
    setTxt('txt-temp-reflux', tempReflux, ' °C');
    setTxt('txt-temp-tsa', tempTsa, ' °C');
    setTxt('txt-water-in', tempWaterIn, ' °C');
    setTxt('txt-water-out', tempWaterOut, ' °C');

    let pCube = undefined;
    if (data.pressure && data.pressure.cube !== undefined) {
        pCube = data.pressure.cube;
    } else if (data.p_cube !== undefined) {
        pCube = data.p_cube;
    }
    let liquidTopY = CUBE_LIQUID_VISIBLE_TOP_Y;

    // Хаотичное капание в дефлегматоре при T_reflux >= 65°C.
    updateRefluxDropChaos(svg, tempReflux);

    // Обновление давления на схеме
    if (pCube !== undefined) {
        const el = svg.getElementById('txt-pressure-cube');
        if (el) el.textContent = `${pCube.toFixed(1)} мм рт.ст.`;
    }

    // Обновление ABV на схеме
    const abvEl = svg.getElementById('txt-abv');
    if (abvEl) {
        const abvData = getEffectiveAbvForCalculations();
        abvEl.textContent = abvData.value.toFixed(1) + '%';

        // Визуальная индикация источника (датчик или план)
        const box = abvEl.previousElementSibling;
        if (box) {
            // Если данные с датчика - фиолетовая рамка, иначе серая
            box.style.stroke = abvData.source === 'sensor' ? '#6610f2' : '#7f8c8d';
            box.style.strokeWidth = abvData.source === 'sensor' ? '2' : '1';
        }
    }

    const powerActualW = getPowerActualW(data);
    const powerSetPercent = getPowerSetPercent(data);

    // Анимация ТЭНа — свечение пропорционально мощности
    if (powerActualW !== undefined) {
        const heater = svg.getElementById('svg-heater');
        if (heater) {
            if (powerActualW > 0) heater.classList.add('heater-on');
            else heater.classList.remove('heater-on');
        }

        const heaterZone = svg.getElementById('zone-heater');
        if (heaterZone) {
            const maxW = (runtimeMonitorState && runtimeMonitorState.equipment && runtimeMonitorState.equipment.heaterPowerW)
                ? runtimeMonitorState.equipment.heaterPowerW
                : 3000;
            const pct = Math.min(1, Math.max(0, powerActualW / maxW));
            const coils = heaterZone.querySelectorAll('path, line');
            coils.forEach(el => {
                if (!el._origStroke) {
                    el._origStroke = el.getAttribute('stroke') || '#000';
                    el._origStrokeWidth = el.getAttribute('stroke-width') || '1';
                }
            });

            if (pct <= 0) {
                coils.forEach(el => {
                    el.setAttribute('stroke', el._origStroke || '#000');
                    el.setAttribute('stroke-width', el._origStrokeWidth || '1');
                    el.style.filter = '';
                });
            } else {
                // Интерполяция от тёмно-красного (20%) к ярко-красному (100%)
                const r = Math.round(80 + 175 * pct);
                const g = Math.round(20 * (1 - pct));
                const b = Math.round(10 * (1 - pct));
                const color = `rgb(${r},${g},${b})`;
                const glowRadius = 2 + 8 * pct;
                const glowOpacity = 0.3 + 0.7 * pct;
                coils.forEach(el => {
                    el.setAttribute('stroke', color);
                    el.setAttribute('stroke-width', String(1 + 1.5 * pct));
                    el.style.filter = `drop-shadow(0 0 ${glowRadius}px rgba(${r},${g},${b},${glowOpacity}))`;
                });
            }
        }

        // Анимация пара
        const vaporGroup = svg.getElementById('anim-vapor');
        if (vaporGroup) {
            if (powerActualW > 0) vaporGroup.classList.add('vapor-active');
            else vaporGroup.classList.remove('vapor-active');
        }

        // Прогресс-бар мощности
        const powerBar = svg.getElementById('anim-power-bar');
        if (powerBar) {
            const maxW = (runtimeMonitorState && runtimeMonitorState.equipment && runtimeMonitorState.equipment.heaterPowerW)
                ? runtimeMonitorState.equipment.heaterPowerW
                : 3000;
            const pct = Math.min(1, Math.max(0, powerActualW / maxW));
            powerBar.setAttribute('width', pct * 160); // 160 - полная ширина бара в SVG
        }

        // Прямоугольник мощности: установленная (%) и фактическая (Вт)
        const powerSetEl = svg.getElementById('txt-power-set');
        if (powerSetEl && powerSetPercent !== undefined)
            powerSetEl.textContent = powerSetPercent.toFixed(0) + '%';
        const powerActEl = svg.getElementById('txt-power-actual');
        if (powerActEl)
            powerActEl.textContent = `${powerActualW.toFixed(0)} Вт`;
    }

    const valvesState = (data.valves && typeof data.valves === 'object')
        ? data.valves
        : runtimeMonitorState.valves;
    if (valvesState) {
        if (valvesState.water !== undefined) setValveClass(svg, 'svg-valve-water', valvesState.water);
        if (valvesState.heads !== undefined) setValveClass(svg, 'svg-valve-heads', valvesState.heads);
        if (valvesState.uno !== undefined) setValveClass(svg, 'svg-valve-uno', valvesState.uno);
        if (valvesState.tails !== undefined) setValveClass(svg, 'svg-valve-tails', valvesState.tails);

        const waterFlow = svg.getElementById('anim-water-flow');
        if (waterFlow && valvesState.water !== undefined) {
            if (valvesState.water) waterFlow.classList.add('flowing');
            else waterFlow.classList.remove('flowing');
        }
    }

    // "Заливка" синей линии охлаждения при рабочем охлаждении.
    const coolingActiveByValve = Boolean(valvesState?.water);
    const coolingActiveByTemps = (
        tempWaterIn !== undefined
        && tempWaterOut !== undefined
        && tempWaterOut > tempWaterIn
        && tempWaterOut > 20
    );
    const isCoolingActive = coolingActiveByValve || coolingActiveByTemps;
    svg.querySelectorAll('.pipe-water').forEach((pipe) => {
        pipe.classList.toggle('active', isCoolingActive);
    });

    // Анимация уровня жидкости
    const liquidShape = svg.getElementById('anim-liquid-level');
    if (liquidShape && runtimeMonitorState) {
        const s = runtimeMonitorState;
        const feedVolumeL = getPositiveFiniteNumber(
            currentMode === MODE_MANUAL ? manualFeedSetup.volumeL : undefined,
            s.rectification?.feedVolumeL,
            (s.distillation?.targetVolumeMl || 0) / 1000,
            20
        ) || 20;
        const feedAbvPercent = getPositiveFiniteNumber(
            currentMode === MODE_MANUAL ? manualFeedSetup.abvPercent : undefined,
            s.rectification?.feedAbvPercent,
            getEffectiveAbvForCalculations()?.value,
            40
        ) || 40;

        const maxCubeVolumeL = getPositiveFiniteNumber(
            s.equipment?.cubeVolumeL,
            s.equipment?.cubeVolume,
            s.rectification?.cubeVolumeL,
            s.distillation?.cubeVolumeL,
            feedVolumeL,
            20
        );
        const chargeVolumeL = Math.min(
            maxCubeVolumeL || 20,
            feedVolumeL
        );
        const fromFractionsMl = (s.volumes?.heads || 0) + (s.volumes?.body || 0) + (s.volumes?.tails || 0);
        const collectedMl = Math.max(fromFractionsMl, s.pump?.totalMl || 0);
        const remainingVolumeL = Math.max(0, chargeVolumeL - (collectedMl / 1000));
        const boilingRefTempC = getPositiveFiniteNumber(tempCube, tempColTop, tempNode, tempReflux);
        const cubeAbvByTemp = getCubeAbvByBoilingTemp(boilingRefTempC, feedAbvPercent);
        const initialAbsoluteAlcoholL = Math.max(0, chargeVolumeL * (feedAbvPercent / 100));
        const remainingAbsoluteAlcoholL = Math.max(
            0,
            Math.min(initialAbsoluteAlcoholL, remainingVolumeL * (cubeAbvByTemp / 100))
        );

        // Уровень показываем как долю от максимальной емкости куба.
        const fillPercent = Math.max(0, Math.min(1, remainingVolumeL / (maxCubeVolumeL || 1)));

        const tagName = String(liquidShape.tagName || '').toLowerCase();
        if (tagName === 'rect') {
            const cubeShell = svg.getElementById('cube-shell');
            const cubeY = Number(cubeShell?.getAttribute('y')) || 553;
            const cubeH = Number(cubeShell?.getAttribute('height')) || 190;
            const visibleTopY = Math.max(cubeY, Math.min(cubeY + cubeH - 8, CUBE_LIQUID_VISIBLE_TOP_Y));
            const visibleSpan = Math.max(1, (cubeY + cubeH) - visibleTopY);
            const liquidHeight = visibleSpan * fillPercent;
            const liquidY = (cubeY + cubeH) - liquidHeight;

            liquidShape.setAttribute('y', liquidY.toFixed(2));
            liquidShape.setAttribute('height', liquidHeight.toFixed(2));
            liquidTopY = liquidY;
        } else {
            // Поддержка старых версий SVG, где уровень задан path.
            const yTop = 600 - (fillPercent * 120);
            const d = `M65,${yTop} L295,${yTop} L295,600 Q295,615 280,615 L80,615 Q65,615 65,600 Z`;
            liquidShape.setAttribute('d', d);
            liquidTopY = yTop;
        }

        const cubeVolumeText = svg.getElementById('txt-volume-cube');
        if (cubeVolumeText) {
            cubeVolumeText.textContent = `${remainingVolumeL.toFixed(1)} л`;
        }
        const cubeAAText = svg.getElementById('txt-aa-cube');
        if (cubeAAText) {
            cubeAAText.textContent = `АС ${remainingAbsoluteAlcoholL.toFixed(2)} л`;
        }
    }

    updateColumnGradientLayers(svg, tempCube, tempColMid, tempReflux, tempTsa, pCube, liquidTopY);
    updateBoilingBubbles(svg, tempCube, powerActualW, liquidTopY);

    // Визуализация капель (скорость отбора)
    const livePumpSpeed = getStatusNumber(data, 'pump', 'speedMlH', 'pump_speed');
    if (livePumpSpeed !== undefined || valvesState) {
        const speed = livePumpSpeed !== undefined
            ? livePumpSpeed
            : (runtimeMonitorState.pump.speedMlH || 0);
        const valveHeads = svg.getElementById('svg-valve-heads');
        const valveUno = svg.getElementById('svg-valve-uno');
        const valveTails = svg.getElementById('svg-valve-tails');

        const updateDrop = (dropClassPrefix, valveOpen, specificDropEle) => {
            for (let i = 1; i <= 3; i++) {
                const drop = specificDropEle ? (i === 1 ? specificDropEle : null) : svg.getElementById(`${dropClassPrefix}-${i}`);
                if (!drop) continue; // На старой схеме это один элемент drop- heads, uno, tails (без -1,2,3)
                if (speed > 0 && valveOpen) {
                    let duration = 180 / speed; // T = 180/S (~20 капель/мл)
                    if (duration < 0.1) duration = 0.1;
                    if (duration > 2.0) duration = 2.0;
                    // В column-animated анимация называется drop-fall, в distillation - dripping
                    const animName = specificDropEle ? 'drop-fall' : 'dripping';
                    drop.style.animation = `${animName} ${duration.toFixed(2)}s infinite linear`;
                } else {
                    drop.style.animation = 'none';
                    drop.style.opacity = '0';
                }
            }
        };

        // Поддержка старой схемы (rectification - один drop)
        const dropHeadsSingle = svg.getElementById('drop-heads');
        const dropUnoSingle = svg.getElementById('drop-uno');
        const dropTailsSingle = svg.getElementById('drop-tails');

        if (dropHeadsSingle) updateDrop('drop-heads', valveHeads?.classList.contains('valve-open'), dropHeadsSingle);
        if (dropUnoSingle) updateDrop('drop-uno', valveUno?.classList.contains('valve-open'), dropUnoSingle);
        if (dropTailsSingle) updateDrop('drop-tails', valveTails?.classList.contains('valve-open'), dropTailsSingle);

        // Поддержка схемы дистилляции (группа active-drops.drop-dist с drop-tails-1..3)
        // В дистилляции отбор идет всегда (без клапана отбора, только если есть скорость > 0)
        const isDistillationFlow = svg.querySelector('.drop-dist') !== null;
        if (isDistillationFlow) {
            updateDrop('drop-tails', true); // Клапана нет, льется всегда при speed>0
        }

        // Скорость насоса
        const pumpSpeedEl = svg.getElementById('txt-pump-speed');
        if (pumpSpeedEl) pumpSpeedEl.textContent = speed.toFixed(0) + ' мл/ч';

        // Анимация ротора насоса
        const zonePump = svg.getElementById('zone-pump');
        if (zonePump) zonePump.classList.toggle('pump-running', speed > 0);
        updatePumpImpellerAnimation(svg, speed);
    }

    // Индикатор фазы
    const phaseTextEl = svg.getElementById('txt-phase');
    if (phaseTextEl && data.phase !== undefined) {
        const phases = ['ОЖИДАНИЕ', 'НАГРЕВ', 'СТАБИЛ.', 'ГОЛОВЫ', 'ПРОДУВКА', 'ТЕЛО', 'ХВОСТЫ', 'ФИНИШ', 'ОШИБКА'];
        const colors = [
            '#7f8c8d', // IDLE - Gray
            '#e67e22', // HEATING - Orange
            '#f1c40f', // STABIL - Yellow
            '#e74c3c', // HEADS - Red
            '#3498db', // PURGE - Blue
            '#2ecc71', // BODY - Green
            '#9b59b6', // TAILS - Purple
            '#27ae60', // FINISH - Dark Green
            '#c0392b'  // ERROR - Dark Red
        ];
        const idx = Number(data.phase);
        if (idx >= 0 && idx < phases.length) {
            phaseTextEl.textContent = phases[idx];
            phaseTextEl.setAttribute('fill', colors[idx]);
        }
    }

    // Анимация наполнения банок (Heads / Body)
    if (runtimeMonitorState) {
        const s = runtimeMonitorState;

        const activeFeedVolumeL = getPositiveFiniteNumber(
            currentMode === MODE_MANUAL ? manualFeedSetup.volumeL : undefined,
            s.rectification?.feedVolumeL,
            20
        ) || 20;
        const activeFeedAbvPercent = getPositiveFiniteNumber(
            currentMode === MODE_MANUAL ? manualFeedSetup.abvPercent : undefined,
            s.rectification?.feedAbvPercent,
            40
        ) || 40;
        const absoluteAlcoholMl = Math.max(0, activeFeedVolumeL * 1000 * (activeFeedAbvPercent / 100));
        const targets = pickRectTargets(s, manualSettings, absoluteAlcoholMl, showRightTailsBranch);

        // Heads Jar
        const headsVol = s.volumes?.heads || 0;
        const headsMax = Math.max(1, targets.headsTargetMl);

        const headsPct = Math.min(1, headsVol / headsMax);
        const headsH = 96; // Высота SVG jar (новый SVG 415×737)
        const headsHFill = headsH * headsPct;

        const elHeads = svg.getElementById('anim-liquid-heads');
        if (elHeads) {
            elHeads.setAttribute('y', headsH - headsHFill);
            elHeads.setAttribute('height', headsHFill);
        }
        const txtHeads = svg.getElementById('txt-volume-heads');
        if (txtHeads) txtHeads.textContent = `${headsVol.toFixed(0)} мл`;

        // Body Jar
        const bodyVol = s.volumes?.body || 0;
        const bodyMax = Math.max(1, targets.bodyTargetMl);

        const bodyPct = Math.min(1, bodyVol / bodyMax);
        const bodyH = 117; // Высота SVG jar (новый SVG 415×737)
        const bodyHFill = bodyH * bodyPct;

        const elBody = svg.getElementById('anim-liquid-body');
        if (elBody) {
            elBody.setAttribute('y', bodyH - bodyHFill);
            elBody.setAttribute('height', bodyHFill);
        }
        const txtBody = svg.getElementById('txt-volume-body');
        if (txtBody) txtBody.textContent = `${bodyVol.toFixed(0)} мл`;

        // Tails Jar
        const tailsVol = s.volumes?.tails || 0;
        const tailsMax = Math.max(1, targets.tailsTargetMl);

        const tailsPct = Math.min(1, tailsVol / tailsMax);
        const tailsH = 95; // Высота SVG jar (новый SVG 415×737)
        const tailsHFill = tailsH * tailsPct;

        const elTails = svg.getElementById('anim-liquid-tails');
        if (elTails) {
            elTails.setAttribute('y', tailsH - tailsHFill);
            elTails.setAttribute('height', tailsHFill);
        }
        const txtTailsLeft = svg.getElementById('txt-volume-tails');
        if (txtTailsLeft) txtTailsLeft.textContent = `${tailsVol.toFixed(0)} мл`;
        const txtTailsRight = svg.getElementById('txt-volume-tails-right');
        if (txtTailsRight) txtTailsRight.textContent = `${tailsVol.toFixed(0)} мл`;
    }

    // Анимация конденсации (если есть нагрев и включена вода)
    const condensateGroup = svg.getElementById('anim-condensate');
    const heater = svg.getElementById('svg-heater');
    const waterFlow = svg.getElementById('anim-water-flow');

    if (condensateGroup && heater && waterFlow) {
        const isHeating = heater.classList.contains('heater-on');
        const isCooling = waterFlow.classList.contains('flowing');

        if (isHeating && isCooling) condensateGroup.classList.add('condensing');
        else condensateGroup.classList.remove('condensing');
    }
}
