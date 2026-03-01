import { runtimeMonitorState, resolveMode, maxHeaterPower, MODE_RECT, PHASE_HEADS, PHASE_BODY, PHASE_POST_HEADS_STAB, PHASE_TAILS, MODE_DIST, MODE_MASH, MODE_HOLD, MODE_MANUAL } from '../globals.js';
import { clampPercent, runtimeEscapeHtml, toFinite, formatDurationSafe } from '../runtime/helpers.js';
import { getEffectiveAbvForCalculations } from '../runtime/abv.js';
import { estimateRectTargets } from '../runtime/state.js';

export function renderRuntimeBars(items) {
    const barsEl = document.getElementById('mode-runtime-bars');
    if (!barsEl) return;
    if (!items.length) {
        barsEl.innerHTML = '';
        return;
    }

    barsEl.innerHTML = items.map((item) => {
        const pct = clampPercent(item.percent);
        const title = runtimeEscapeHtml(item.label || 'Этап');
        const stateClass = item.stateClass ? ` ${runtimeEscapeHtml(item.stateClass)}` : '';
        const primary = runtimeEscapeHtml(item.primary || '');
        const leftMeta = runtimeEscapeHtml(item.metaLeft || '');
        const rightMeta = runtimeEscapeHtml(item.metaRight || '');
        return `
            <div class="operator-runtime-track${stateClass}">
                <div class="operator-runtime-head">
                    <span>${title}</span>
                    <strong>${primary}</strong>
                </div>
                <div class="operator-runtime-bar">
                    <div class="operator-runtime-fill" style="width:${pct.toFixed(1)}%"></div>
                </div>
                <div class="operator-runtime-meta">
                    <span>${leftMeta}</span>
                    <span>${rightMeta}</span>
                </div>
            </div>
        `;
    }).join('');
}

export function updateManualTiles() {
    const s = runtimeMonitorState;
    const heaterMax = Math.max(1, toFinite(s.equipment.heaterPowerW, maxHeaterPower));
    const measuredPower = Math.max(0, toFinite(s.power.power, 0));
    const powerPercent = clampPercent((measuredPower / heaterMax) * 100);

    const rectPowerEl = document.getElementById('rect-power-display');
    const powerEl = document.getElementById('manual-power-display');
    const speedEl = document.getElementById('manual-speed-display');
    const waterAutoStartEl = document.getElementById('water-autostart-display');
    const headsEl = document.getElementById('manual-heads-display');
    const bodyEl = document.getElementById('manual-body-display');
    const tailsEl = document.getElementById('manual-tails-display');

    if (rectPowerEl) rectPowerEl.textContent = `${powerPercent.toFixed(0)} %`;
    if (powerEl) powerEl.textContent = `${powerPercent.toFixed(0)} %`;
    if (speedEl) speedEl.textContent = `${toFinite(s.pump.speedMlH, 0).toFixed(0)} мл/ч`;
    if (waterAutoStartEl) waterAutoStartEl.textContent = `${toFinite(s.equipment.waterAutoStartCubeTempC, 45).toFixed(1)} °C`;
    if (headsEl) headsEl.textContent = `${toFinite(s.volumes.heads, 0).toFixed(0)} мл`;
    if (bodyEl) bodyEl.textContent = `${toFinite(s.volumes.body, 0).toFixed(0)} мл`;
    if (tailsEl) tailsEl.textContent = `${toFinite(s.volumes.tails, 0).toFixed(0)} мл`;
}

export function renderModeRuntimeCard() {
    const titleEl = document.getElementById('mode-runtime-title');
    const captionEl = document.getElementById('mode-runtime-caption');
    const manualEl = document.getElementById('mode-runtime-manual');
    if (!titleEl || !captionEl) return;

    const s = runtimeMonitorState;
    const mode = resolveMode(s.mode, s.modeStr);
    const phase = toFinite(s.phase, 0);
    const items = [];

    if (mode === MODE_RECT) {
        titleEl.textContent = 'Прогресс авто-ректификации';
        const effectiveAbv = getEffectiveAbvForCalculations();
        const abvSourceText = effectiveAbv.source === 'sensor' ? 'датчик' : 'план';
        captionEl.textContent = `Фаза: ${s.phaseStr || phase || '-'} • крепость расчета ${effectiveAbv.value.toFixed(1)}% (${abvSourceText})`;

        const est = estimateRectTargets(s.rectification, effectiveAbv.value);
        const heaterKw = Math.max(0.1, toFinite(s.equipment.heaterPowerW, maxHeaterPower) / 1000);
        const headsSpeed = toFinite(s.rectification.headsSpeedMlHKw, 0) * heaterKw;
        const bodySpeed = toFinite(s.rectification.bodySpeedMlHKw, 0) * heaterKw;
        const tailsSpeed = Math.max(0, bodySpeed / 2);
        const targetHeads = toFinite(s.rectification.headsTargetMl, 0) > 0 ? toFinite(s.rectification.headsTargetMl, 0) : est.heads;
        const targetBody = toFinite(s.rectification.bodyTargetMl, 0) > 0 ? toFinite(s.rectification.bodyTargetMl, 0) : est.body;
        const targetTails = toFinite(s.rectification.tailsTargetMl, 0) > 0 ? toFinite(s.rectification.tailsTargetMl, 0) : est.tails;

        [
            { key: 'heads', label: 'Головы', target: targetHeads, speed: headsSpeed, value: toFinite(s.volumes.heads, 0), pending: phase < PHASE_HEADS },
            { key: 'body', label: 'Тело', target: targetBody, speed: bodySpeed, value: toFinite(s.volumes.body, 0), pending: phase < PHASE_BODY || phase === PHASE_POST_HEADS_STAB },
            { key: 'tails', label: 'Хвосты', target: targetTails, speed: tailsSpeed, value: toFinite(s.volumes.tails, 0), pending: phase < PHASE_TAILS }
        ].forEach((part) => {
            const target = Math.max(0, part.target);
            const value = Math.max(0, part.value);
            const pct = target > 0 ? clampPercent((value / target) * 100) : 0;
            const remMl = Math.max(0, target - value);
            const remSec = (part.speed > 0 && remMl > 0) ? (remMl / part.speed) * 3600 : 0;
            items.push({
                label: part.label,
                percent: pct,
                primary: target > 0 ? `${(100 - pct).toFixed(0)}% осталось` : 'Цель не задана',
                metaLeft: target > 0 ? `${value.toFixed(0)} / ${target.toFixed(0)} мл` : `${value.toFixed(0)} мл`,
                metaRight: remSec > 0 ? `~${formatDurationSafe(remSec)}` : (part.pending ? 'ожидание' : '—'),
                stateClass: part.pending ? 'is-pending' : ''
            });
        });
    } else if (mode === MODE_DIST) {
        titleEl.textContent = 'Прогресс дистилляции';
        const target = Math.max(0, toFinite(s.distillation.targetVolumeMl, 0));
        const speed = Math.max(0, toFinite(s.distillation.speedMlH, 0));
        const total = Math.max(0, toFinite(s.pump.totalMl, 0));
        const pct = target > 0 ? clampPercent((total / target) * 100) : clampPercent(s.progress.phasePercent);
        const remMl = Math.max(0, target - total);
        const remSec = (target > 0 && speed > 0) ? (remMl / speed) * 3600 : toFinite(s.progress.phaseRemainingSec, 0);
        captionEl.textContent = target > 0
            ? `Цель ${target.toFixed(0)} мл, скорость ${speed.toFixed(0)} мл/ч`
            : `Фаза: ${s.phaseStr || phase || '-'}`;
        items.push({
            label: 'Перегон',
            percent: pct,
            primary: `${(100 - pct).toFixed(0)}% осталось`,
            metaLeft: target > 0 ? `${total.toFixed(0)} / ${target.toFixed(0)} мл` : `${total.toFixed(0)} мл`,
            metaRight: remSec > 0 ? `~${formatDurationSafe(remSec)}` : '—',
            stateClass: target > 0 ? '' : 'is-waiting'
        });
    } else if (mode === MODE_MASH) {
        titleEl.textContent = 'Прогресс затирки';
        const totalSteps = Math.max(0, Math.round(toFinite(s.mashing.stepCount, 0)));
        const currentStep = Math.max(0, Math.round(toFinite(s.mashing.currentStep, 0)));
        const elapsed = Math.max(0, toFinite(s.mashing.elapsedSec, 0));
        const duration = Math.max(0, toFinite(s.mashing.stepDurationSec, 0));
        const currentPct = duration > 0 ? clampPercent((elapsed / duration) * 100) : 0;
        captionEl.textContent = totalSteps > 0
            ? `Шаг ${Math.min(currentStep + 1, totalSteps)} из ${totalSteps}`
            : 'Ожидание профиля затирки';

        for (let i = 0; i < totalSteps; i += 1) {
            let pct = 0;
            let primary = 'ожидание';
            let metaRight = '—';
            let stateClass = 'is-pending';
            if (i < currentStep) {
                pct = 100;
                primary = 'завершено';
                stateClass = '';
            } else if (i === currentStep) {
                pct = currentPct;
                primary = `${(100 - pct).toFixed(0)}% осталось`;
                metaRight = `~${formatDurationSafe(s.mashing.remainingSec)}`;
                stateClass = '';
            }

            items.push({
                label: i === currentStep && s.mashing.stepName ? s.mashing.stepName : `Шаг ${i + 1}`,
                percent: pct,
                primary,
                metaLeft: i === currentStep ? `${elapsed.toFixed(0)} / ${duration.toFixed(0)} с` : '',
                metaRight,
                stateClass
            });
        }
    } else if (mode === MODE_HOLD) {
        titleEl.textContent = 'Режим выдержки';
        const duration = Math.max(0, toFinite(s.hold.stepDurationSec, 0));
        const elapsed = Math.max(0, toFinite(s.hold.elapsedSec, 0));
        const pct = duration > 0 ? clampPercent((elapsed / duration) * 100) : clampPercent(s.progress.phasePercent);
        const remSec = duration > elapsed ? (duration - elapsed) : toFinite(s.progress.phaseRemainingSec, 0);
        captionEl.textContent = `Цель ${toFinite(s.hold.targetTemp, 0).toFixed(1)}°C`;
        items.push({
            label: 'Обратный отсчет',
            percent: pct,
            primary: `${formatDurationSafe(remSec)} осталось`,
            metaLeft: duration > 0 ? `${elapsed.toFixed(0)} / ${duration.toFixed(0)} с` : '',
            metaRight: `шаг ${Math.round(toFinite(s.hold.currentStep, 0)) + 1}`,
            stateClass: ''
        });
    } else if (mode === MODE_MANUAL) {
        titleEl.textContent = 'Ручная ректификация';
        captionEl.textContent = 'Параметры ниже редактируются нажатием на плитку';

        const heads = Math.max(0, toFinite(s.volumes.heads, 0));
        const body = Math.max(0, toFinite(s.volumes.body, 0));
        const tails = Math.max(0, toFinite(s.volumes.tails, 0));
        const total = Math.max(1, heads + body + tails);
        [
            { label: 'Головы', value: heads },
            { label: 'Тело', value: body },
            { label: 'Хвосты', value: tails }
        ].forEach((part) => {
            const pct = clampPercent((part.value / total) * 100);
            items.push({
                label: part.label,
                percent: pct,
                primary: `${part.value.toFixed(0)} мл`,
                metaLeft: `доля ${pct.toFixed(0)}%`,
                metaRight: '',
                stateClass: ''
            });
        });
    } else {
        titleEl.textContent = 'Прогресс режима';
        captionEl.textContent = 'Ожидание запуска процесса';
        items.push({
            label: 'Нет активного режима',
            percent: 0,
            primary: '0% выполнено',
            metaLeft: '',
            metaRight: '',
            stateClass: 'is-pending'
        });
    }

    renderRuntimeBars(items);
    updateManualTiles();
    if (manualEl) {
        manualEl.style.display = mode === MODE_MANUAL ? 'grid' : 'none';
    }
    const rectEl = document.getElementById('mode-runtime-rect');
    if (rectEl) {
        rectEl.style.display = mode === MODE_RECT ? 'grid' : 'none';
    }
}

export function initRuntimeMonitorUi() {
    const tiles = document.querySelectorAll('[data-edit-param]');
    tiles.forEach((tile) => {
        tile.addEventListener('click', () => {
            const param = tile.getAttribute('data-edit-param');
            if (param) openRuntimeEditModal(param);
        });
    });

    const modal = document.getElementById('runtime-edit-modal');
    if (modal) {
        modal.addEventListener('click', (event) => {
            if (event.target === modal) closeRuntimeEditModal();
        });
    }
}
