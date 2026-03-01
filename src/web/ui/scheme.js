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

function isTailsPwmEnabled() {
    const tailsPwmCheckbox = document.getElementById('manual-tails-pwm-enabled');
    if (tailsPwmCheckbox) {
        return Boolean(tailsPwmCheckbox.checked);
    }

    try {
        const raw = localStorage.getItem(MANUAL_RECT_STORAGE_KEY);
        if (!raw) return false;
        const parsed = JSON.parse(raw);
        return Boolean(parsed?.tails?.pwmEnabled);
    } catch {
        return false;
    }
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

        impeller.style.transformOrigin = '256px 371px';
        impeller.style.transformBox = 'fill-box';

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

function updateColumnGradientLayers(svg, tempCube, tempColMid, tempReflux) {
    const heatLayer = svg.getElementById('anim-column-heat-layer');
    const refluxLayer = svg.getElementById('anim-column-reflux-layer');
    if (!heatLayer || !refluxLayer) return;

    const shell = svg.getElementById('column-shell');
    const baseY = Number(shell?.getAttribute('y')) || 244;
    const baseH = Number(shell?.getAttribute('height')) || 244;

    const tCube = Number(tempCube) || 0;
    const tMid = Number(tempColMid) || 0;
    const tReflux = Number(tempReflux) || 0;

    const clamp01 = (v) => Math.max(0, Math.min(1, v));
    const heatLevel = clamp01((tCube - 45) / 35);
    const refluxLevel = clamp01((tReflux - 65) / 18);
    const stabilityLevel = clamp01(1 - Math.abs(tMid - 76.6) / 3.5);

    // Базовые зоны: красная поднимается снизу, синяя заполняет сверху.
    let heatCoverage = 0.2 + (0.75 * heatLevel);
    let refluxCoverage = 0.15 + (0.75 * refluxLevel);

    // В стабильной зоне допускаем выраженное перекрытие в центре.
    const overlapBoost = 0.25 * stabilityLevel;
    heatCoverage = Math.min(1, heatCoverage + overlapBoost);
    refluxCoverage = Math.min(1, refluxCoverage + overlapBoost);

    // Базовое вытеснение: более "сильный" слой поджимает противоположный.
    const dominance = heatLevel - refluxLevel;
    if (dominance > 0) {
        refluxCoverage *= (1 - Math.min(0.45, dominance * 0.45));
    } else if (dominance < 0) {
        heatCoverage *= (1 - Math.min(0.35, Math.abs(dominance) * 0.35));
    }

    const heatHeight = baseH * clamp01(heatCoverage);
    const heatY = baseY + (baseH - heatHeight);
    const refluxHeight = baseH * clamp01(refluxCoverage);
    const refluxY = baseY;

    heatLayer.setAttribute('y', heatY.toFixed(2));
    heatLayer.setAttribute('height', heatHeight.toFixed(2));
    refluxLayer.setAttribute('y', refluxY.toFixed(2));
    refluxLayer.setAttribute('height', refluxHeight.toFixed(2));

    heatLayer.style.opacity = (0.12 + heatLevel * 0.78).toFixed(3);
    refluxLayer.style.opacity = (0.1 + refluxLevel * 0.74).toFixed(3);
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

async function toggleHeaterFromScheme() {
    try {
        const currentlyOn = Number(runtimeMonitorState?.power?.power || 0) > 10;
        const targetPowerPercent = currentlyOn ? 0 : 60;
        await postJson('/api/manual/heater', { power: targetPowerPercent });
        addLog(`Heater set to ${targetPowerPercent}% from scheme`, 'info');
    } catch (error) {
        addLog(`Heater control failed (${error.message})`, 'error');
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

    const powerBtn = svg.getElementById('zone-power-btn');
    if (powerBtn) {
        powerBtn.style.cursor = 'pointer';
        powerBtn.addEventListener('click', (event) => {
            event.preventDefault();
            event.stopPropagation();
            toggleHeaterFromScheme();
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

    // Левая хвостовая ветка отображается только при включенном "Хвосты: ШИМ-отбор".
    const showTailsBranch = isTailsPwmEnabled();
    setOptionalDisplay(svg.getElementById('zone-tails-left-branch'), showTailsBranch);
    setOptionalDisplay(svg.getElementById('ind-volume-tails'), showTailsBranch);

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
    updateColumnGradientLayers(svg, tempCube, tempColMid, tempReflux);

    // Капли дефлегматора — видимы при T царги > 70°C
    const refluxZone = svg.getElementById('zone-reflux');
    if (refluxZone) {
        const colMid = tempColMid || 0;
        refluxZone.classList.toggle('condensing', colMid > 70);
    }

    // Обновление давления на схеме
    let pCube = undefined;
    if (data.pressure && data.pressure.cube !== undefined) {
        pCube = data.pressure.cube;
    } else if (data.p_cube !== undefined) {
        pCube = data.p_cube;
    }
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

    // Анимация ТЭНа
    if (powerActualW !== undefined) {
        const heater = svg.getElementById('svg-heater');
        if (heater) {
            if (powerActualW > 0) heater.classList.add('heater-on');
            else heater.classList.remove('heater-on');
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
        const maxCubeVolumeL = getPositiveFiniteNumber(
            s.equipment?.cubeVolumeL,
            s.equipment?.cubeVolume,
            s.rectification?.cubeVolumeL,
            s.distillation?.cubeVolumeL,
            s.rectification?.feedVolumeL,
            20
        );
        const chargeVolumeL = Math.min(
            maxCubeVolumeL || 20,
            getPositiveFiniteNumber(
                s.rectification?.feedVolumeL,
                (s.distillation?.targetVolumeMl || 0) / 1000,
                maxCubeVolumeL,
                20
            ) || 20
        );
        const fromFractionsMl = (s.volumes?.heads || 0) + (s.volumes?.body || 0) + (s.volumes?.tails || 0);
        const collectedMl = Math.max(fromFractionsMl, s.pump?.totalMl || 0);
        const remainingVolumeL = Math.max(0, chargeVolumeL - (collectedMl / 1000));

        // Уровень показываем как долю от максимальной емкости куба.
        const fillPercent = Math.max(0, Math.min(1, remainingVolumeL / (maxCubeVolumeL || 1)));

        const tagName = String(liquidShape.tagName || '').toLowerCase();
        if (tagName === 'rect') {
            const cubeShell = svg.getElementById('cube-shell');
            const cubeY = Number(cubeShell?.getAttribute('y')) || 553;
            const cubeH = Number(cubeShell?.getAttribute('height')) || 190;
            const liquidHeight = cubeH * fillPercent;
            const liquidY = cubeY + (cubeH - liquidHeight);

            liquidShape.setAttribute('y', liquidY.toFixed(2));
            liquidShape.setAttribute('height', liquidHeight.toFixed(2));
        } else {
            // Поддержка старых версий SVG, где уровень задан path.
            const yTop = 600 - (fillPercent * 120);
            const d = `M65,${yTop} L295,${yTop} L295,600 Q295,615 280,615 L80,615 Q65,615 65,600 Z`;
            liquidShape.setAttribute('d', d);
        }

        const cubeVolumeText = svg.getElementById('txt-volume-cube');
        if (cubeVolumeText) {
            cubeVolumeText.textContent = `${remainingVolumeL.toFixed(1)} л`;
        }
    }

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

        // Heads Jar
        const headsVol = s.volumes?.heads || 0;
        // Если цель не задана, берем примерную (5% от 40% спирта в 20л сырца ~ 400мл) или дефолт 300мл
        let headsMax = s.rectification?.headsTargetMl || 0;
        if (headsMax === 0 && s.rectification?.feedVolumeL) {
            headsMax = s.rectification.feedVolumeL * 1000 * 0.4 * 0.05;
        }
        if (headsMax === 0) headsMax = 300;

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
        let bodyMax = s.rectification?.bodyTargetMl || 0;
        if (bodyMax === 0 && s.rectification?.feedVolumeL) {
            bodyMax = s.rectification.feedVolumeL * 1000 * 0.4 * 0.4; // ~3.2л
        }
        if (bodyMax === 0) bodyMax = 3000;

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
        let tailsMax = s.rectification?.tailsTargetMl || 0;
        if (tailsMax === 0 && s.rectification?.feedVolumeL) {
            tailsMax = s.rectification.feedVolumeL * 1000 * 0.4 * 0.1; // ~0.8л
        }
        if (tailsMax === 0) tailsMax = 800;

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
