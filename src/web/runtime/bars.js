import { runtimeMonitorState, resolveMode, maxHeaterPower, MODE_RECT, PHASE_HEADS, PHASE_BODY, PHASE_POST_HEADS_STAB, PHASE_TAILS, MODE_DIST, MODE_MASH, MODE_HOLD, MODE_MANUAL } from '../globals.js';
import { clampPercent, runtimeEscapeHtml, toFinite, formatDurationSafe } from '../runtime/helpers.js';
import { getEffectiveAbvForCalculations } from '../runtime/abv.js';
import { estimateRectTargets } from '../runtime/state.js';

function setIndicatorValue(id, text, tone = 'muted') {
    const el = document.getElementById(id);
    if (!el) return;
    el.textContent = text;
    el.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    el.classList.add(`is-${tone}`);
}

function formatIndicatorPercent(value) {
    return `${clampPercent(toFinite(value, 0) * 100).toFixed(0)}%`;
}

function boolLabel(value, goodText, badText, invert = false) {
    const ok = invert ? !value : !!value;
    return {
        text: ok ? goodText : badText,
        tone: ok ? 'good' : 'danger'
    };
}

function setGuidance(title, detail, tone = 'muted') {
    const root = document.getElementById('operator-guidance');
    const titleEl = document.getElementById('operator-guidance-title');
    const textEl = document.getElementById('operator-guidance-text');
    if (!root || !titleEl || !textEl) return;
    root.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    root.classList.add(`is-${tone}`);
    titleEl.textContent = title;
    textEl.textContent = detail;
}

function getReasonCodeLabel(code) {
    const normalized = String(code || 'RC_NONE');
    const labels = {
        RC_NONE: 'Нет',
        RC_MODE_START_REQUEST: 'Запуск режима',
        RC_MODE_STOP_REQUEST: 'Останов режима',
        RC_PRECHECK_OK: 'Проверки пройдены',
        RC_PRECHECK_FAIL_SENSOR: 'Проблема с датчиками',
        RC_PRECHECK_FAIL_SAFETY_LATCH: 'Активен safety latch',
        RC_HEATING_COMPLETE: 'Разгон завершён',
        RC_STABILIZATION_TIMER_OK: 'Стабилизация по таймеру',
        RC_STABILITY_WINDOW_REACHED: 'Окно стабильности достигнуто',
        RC_HEADS_VOLUME_REACHED: 'Головы по объёму завершены',
        RC_HEADS_SCORE_REACHED: 'Головы завершены по score',
        RC_POST_HEADS_STABILIZATION_COMPLETE: 'Постстабилизация завершена',
        RC_PURGE_COMPLETE: 'Продувка завершена',
        RC_BODY_TARGET_VOLUME_REACHED: 'Тело по объёму завершено',
        RC_BODY_END_DETECTED: 'Обнаружен конец тела',
        RC_TAILS_TARGET_REACHED: 'Хвосты завершены',
        RC_FINISH_COOLDOWN_COMPLETE: 'Финишное охлаждение завершено',
        RC_DISTILLATION_HEADS_OPTIONAL_SKIPPED: 'Головы пропущены',
        RC_DISTILLATION_END_TEMP_REACHED: 'Достигнута стоп-температура',
        RC_DISTILLATION_TARGET_VOLUME_REACHED: 'Целевой объём достигнут',
        RC_NBK_STEAM_READY: 'Пар готов',
        RC_NBK_STABILIZATION_COMPLETE: 'НБК стабилизирована',
        RC_NBK_FEED_ENABLED: 'Подача разрешена',
        RC_NBK_FINISH_LIKELY: 'Вероятен финиш НБК',
        RC_TEMP_STEP_REACHED: 'Целевая температура достигнута',
        RC_TEMP_STEP_HOLD_COMPLETE: 'Выдержка завершена',
        RC_TEMP_STEP_TIMEOUT: 'Таймаут температурного шага',
        RC_FERM_TARGET_REACHED: 'Цель брожения достигнута',
        RC_SAFETY_LIMIT_POWER: 'Ограничение мощности',
        RC_SAFETY_LIMIT_TAKEOFF: 'Ограничение отбора',
        RC_SAFETY_PHASE_BLOCKED: 'Переход фазы заблокирован',
        RC_SAFETY_RECOVERY_ENTERED: 'Вход в recovery',
        RC_SAFETY_RECOVERY_EXITED: 'Выход из recovery',
        RC_SAFETY_TRIP_PRESSURE: 'Авария по давлению',
        RC_SAFETY_TRIP_SENSOR: 'Авария по датчикам',
        RC_SAFETY_TRIP_OVERHEAT: 'Авария по перегреву',
        RC_SAFETY_TRIP_POWER: 'Авария по питанию',
        RC_SAFETY_TRIP_GENERIC: 'Общая авария safety',
        RC_SAFETY_ACKNOWLEDGED: 'Авария подтверждена',
        RC_SAFETY_RESET_COMPLETED: 'Safety reset выполнен',
        RC_OPERATOR_SERVICE_ACTION: 'Сервисное действие оператора',
        RC_MANUAL_OPERATOR_SWITCH: 'Ручное переключение',
        RC_MANUAL_OPERATOR_STOP: 'Ручной останов',
        RC_PHASE_RECOVERY_APPLIED: 'Применено восстановление фазы',
        RC_PHASE_TRANSITION_INFERRED: 'Переход фазы определён автоматически',
        RC_UNSPECIFIED: 'Без уточнения'
    };
    return labels[normalized] || normalized.replace(/^RC_/, '');
}

function buildGuidance(state, indicators, activeLimits) {
    const mode = resolveMode(state.mode, state.modeStr);
    const lifecycle = String(state?.v2?.lifecycle || 'idle');
    const operatorMessage = String(state?.v2?.operatorMessage || '').trim();
    const lastReasonCode = String(state?.v2?.lastReasonCode || 'RC_NONE');
    const stability = toFinite(indicators.stabilityIndex, 0);
    const floodRisk = toFinite(indicators.floodRisk, 0);
    const coolingMargin = toFinite(indicators.coolingMarginC, 0);
    const bodyScore = toFinite(indicators.bodyEndScore, 0);
    const headsScore = toFinite(indicators.headsCompletionScore, 0);
    const isColumnMode = mode === MODE_RECT || mode === MODE_MANUAL;

    if (!state?.v2?.available) {
        return {
            tone: 'muted',
            title: 'Ожидание indicators v2',
            detail: 'Ждём первый полный статус автоматики, чтобы показать осмысленную подсказку.'
        };
    }

    if (state?.currentAlarm?.active || state?.v2?.safetyLatched || lifecycle === 'faulted') {
        return {
            tone: 'danger',
            title: 'Safety удерживает процесс',
            detail: operatorMessage || `Последняя причина: ${getReasonCodeLabel(lastReasonCode)}. Проверьте alarm, ограничения и состояние датчиков.`
        };
    }

    if (!indicators.sensorFreshnessOk) {
        return {
            tone: 'danger',
            title: 'Данные датчиков устарели',
            detail: 'Автоматика снижает доверие к process indicators. Проверьте соединение датчиков и обновление телеметрии.'
        };
    }

    if (indicators.recoveryActive) {
        return {
            tone: 'warn',
            title: 'Идёт recovery',
            detail: operatorMessage || 'Система уже ограничивала процесс и сейчас ждёт повторной стабилизации перед нормальной работой.'
        };
    }

    if (isColumnMode && floodRisk >= 0.65) {
        return {
            tone: 'danger',
            title: 'Высокий риск захлёба',
            detail: 'Не повышайте мощность и не ускоряйте отбор. Проверьте охлаждение, давление и загрузку колонны.'
        };
    }

    if (isColumnMode && coolingMargin <= 0) {
        return {
            tone: 'danger',
            title: 'Охлаждение на пределе',
            detail: 'Cooling margin исчерпан. Нужна вода или снижение нагрузки, иначе процесс станет нестабильным.'
        };
    }

    if (Boolean(activeLimits.takeoffBlocked) || !indicators.takeoffAllowed) {
        return {
            tone: 'warn',
            title: 'Отбор пока заблокирован',
            detail: operatorMessage || 'Ждём безопасное окно по stability, cooling margin и состоянию датчиков.'
        };
    }

    if (isColumnMode && coolingMargin < 5) {
        return {
            tone: 'warn',
            title: 'Низкий запас охлаждения',
            detail: `Сейчас запас ${coolingMargin.toFixed(1)}°C. Лучше не форсировать процесс, пока охлаждение не выровняется.`
        };
    }

    if (isColumnMode && floodRisk >= 0.35) {
        return {
            tone: 'warn',
            title: 'Риск захлёба растёт',
            detail: 'Колонна уже нагружена. Следите за верхом колонны, давлением и скоростью отбора.'
        };
    }

    if (mode === MODE_RECT && bodyScore >= 0.8) {
        return {
            tone: 'warn',
            title: 'Вероятен конец тела',
            detail: 'Body End Score высокий. Пора внимательно смотреть на качество продукта и готовить переход дальше по профилю.'
        };
    }

    if (mode === MODE_RECT && headsScore >= 0.8) {
        return {
            tone: 'good',
            title: 'Головы почти завершены',
            detail: 'Heads Completion Score высокий. Можно готовиться к переходу на тело по правилам профиля.'
        };
    }

    if (isColumnMode && stability < 0.45) {
        return {
            tone: 'warn',
            title: 'Колонна стабилизируется',
            detail: 'Пока нет уверенного стабильного окна. Лучше дождаться ровного поведения температуры верха и давления.'
        };
    }

    if (isColumnMode && stability >= 0.75 && indicators.takeoffAllowed) {
        return {
            tone: 'good',
            title: 'Процесс устойчив',
            detail: 'Стабильность высокая, отбор разрешён, активных ограничений safety сейчас нет.'
        };
    }

    return {
        tone: 'muted',
        title: 'Процесс под наблюдением',
        detail: operatorMessage || `Последняя причина: ${getReasonCodeLabel(lastReasonCode)}. Критичных ограничений сейчас не видно.`
    };
}

function renderProcessIndicatorsCard() {
    const s = runtimeMonitorState;
    const indicators = s?.v2?.indicators || {};
    const activeLimits = s?.v2?.activeLimits || {};

    const lifecycle = String(s?.v2?.lifecycle || 'idle');
    const lastReasonCode = String(s?.v2?.lastReasonCode || 'RC_NONE');
    const coolingMargin = toFinite(indicators.coolingMarginC, 0);
    const stability = toFinite(indicators.stabilityIndex, 0);
    const floodRisk = toFinite(indicators.floodRisk, 0);
    const processHealth = toFinite(indicators.processHealth, 0);
    const headsScore = toFinite(indicators.headsCompletionScore, 0);
    const bodyScore = toFinite(indicators.bodyEndScore, 0);

    setIndicatorValue(
        'indicator-last-reason',
        lastReasonCode === 'RC_NONE' ? 'Нет' : lastReasonCode.replace(/^RC_/, ''),
        lastReasonCode === 'RC_NONE' ? 'muted' : 'warn'
    );
    setIndicatorValue(
        'indicator-lifecycle',
        lifecycle,
        lifecycle === 'running' ? 'good' : (lifecycle === 'faulted' ? 'danger' : 'muted')
    );
    setIndicatorValue(
        'indicator-last-reason',
        getReasonCodeLabel(lastReasonCode),
        lastReasonCode === 'RC_NONE' ? 'muted' : 'warn'
    );

    const takeoffState = boolLabel(indicators.takeoffAllowed, 'Разрешён', 'Заблокирован');
    setIndicatorValue('indicator-takeoff', takeoffState.text, takeoffState.tone);

    let coolingTone = 'good';
    if (coolingMargin <= 0) coolingTone = 'danger';
    else if (coolingMargin < 5) coolingTone = 'warn';
    setIndicatorValue('indicator-cooling-status', `${coolingMargin.toFixed(1)} °C`, coolingTone);

    setIndicatorValue(
        'indicator-stability',
        formatIndicatorPercent(stability),
        stability >= 0.75 ? 'good' : (stability >= 0.45 ? 'warn' : 'danger')
    );
    setIndicatorValue(
        'indicator-flood-risk',
        formatIndicatorPercent(floodRisk),
        floodRisk < 0.35 ? 'good' : (floodRisk < 0.65 ? 'warn' : 'danger')
    );
    setIndicatorValue('indicator-cooling-margin', `${coolingMargin.toFixed(1)}°C`, coolingTone);
    setIndicatorValue(
        'indicator-process-health',
        formatIndicatorPercent(processHealth),
        processHealth >= 0.85 ? 'good' : (processHealth >= 0.65 ? 'warn' : 'danger')
    );

    const freshness = boolLabel(indicators.sensorFreshnessOk, 'OK', 'Просрочены');
    setIndicatorValue('indicator-sensor-freshness', freshness.text, freshness.tone);

    const pressureStable = boolLabel(indicators.pressureStable, 'Стабильно', 'Дрейф');
    setIndicatorValue('indicator-pressure-stable', pressureStable.text, pressureStable.tone);

    setIndicatorValue(
        'indicator-heads-score',
        formatIndicatorPercent(headsScore),
        headsScore >= 0.8 ? 'good' : (headsScore >= 0.5 ? 'warn' : 'muted')
    );
    setIndicatorValue(
        'indicator-body-score',
        formatIndicatorPercent(bodyScore),
        bodyScore >= 0.8 ? 'danger' : (bodyScore >= 0.55 ? 'warn' : 'good')
    );

    const hasLimit =
        Boolean(indicators.powerLimited) ||
        Boolean(activeLimits.powerCapped) ||
        Boolean(activeLimits.takeoffBlocked) ||
        Boolean(activeLimits.phaseAdvanceBlocked);
    setIndicatorValue('indicator-power-limit', hasLimit ? 'Есть' : 'Нет', hasLimit ? 'warn' : 'good');

    const recovery = boolLabel(indicators.recoveryActive, 'Активен', 'Нет');
    setIndicatorValue('indicator-recovery', recovery.text, recovery.tone);
    const guidance = buildGuidance(s, indicators, activeLimits);
    setGuidance(guidance.title, guidance.detail, guidance.tone);
}

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
        titleEl.textContent = 'Пастеризация';
        const duration = Math.max(0, toFinite(s.hold.stepDurationSec, 0));
        const elapsed = Math.max(0, toFinite(s.hold.elapsedSec, 0));
        const pct = duration > 0 ? clampPercent((elapsed / duration) * 100) : clampPercent(s.progress.phasePercent);
        const remSec = duration > elapsed ? (duration - elapsed) : toFinite(s.progress.phaseRemainingSec, 0);
        const targetTemp = toFinite(s.hold.targetTemp, 0);
        captionEl.textContent = targetTemp > 0 ? `Цель ${targetTemp.toFixed(1)}°C` : 'Шаг-пауза без нагрева';
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
    renderProcessIndicatorsCard();
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
        const openFromTile = () => {
            const param = tile.getAttribute('data-edit-param');
            if (param) openRuntimeEditModal(param, tile);
        };
        tile.addEventListener('click', openFromTile);
        if (tile.tagName !== 'BUTTON') {
            tile.addEventListener('keydown', (event) => {
                if (event.key !== 'Enter' && event.key !== ' ') return;
                event.preventDefault();
                openFromTile();
            });
        }
    });

    const modal = document.getElementById('runtime-edit-modal');
    if (modal) {
        modal.addEventListener('click', (event) => {
            if (event.target === modal) closeRuntimeEditModal();
        });
    }
}
