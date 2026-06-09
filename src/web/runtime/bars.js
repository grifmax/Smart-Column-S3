import { runtimeMonitorState, currentMode, resolveMode, maxHeaterPower, MODE_IDLE, MODE_RECT, PHASE_HEADS, PHASE_BODY, PHASE_POST_HEADS_STAB, PHASE_TAILS, MODE_DIST, MODE_MASH, MODE_HOLD, MODE_MANUAL, MODE_NBK, MODE_FERMENTATION } from '../globals.js';
import { activateTabById } from '../core/tabs.js';
import { clampPercent, runtimeEscapeHtml, toFinite, formatDurationSafe } from '../runtime/helpers.js';
import { getEffectiveAbvForCalculations } from '../runtime/abv.js';
import { estimateRectTargets } from '../runtime/state.js';

let missionBindingsReady = false;

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

function setPreflightItem(id, text, tone = 'muted') {
    const el = document.getElementById(id);
    if (!el) return;
    el.textContent = text;
    el.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    el.classList.add(`is-${tone}`);

    const item = el.closest('.operator-preflight-item');
    if (!item) return;
    item.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    item.classList.add(`is-${tone}`);
}

function setPreflightState(title, detail, tone = 'muted', checks = {}) {
    const root = document.getElementById('runtime-preflight');
    const titleEl = document.getElementById('runtime-preflight-title');
    const textEl = document.getElementById('runtime-preflight-text');
    if (!root || !titleEl || !textEl) return;

    root.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    root.classList.add(`is-${tone}`);
    titleEl.textContent = title;
    textEl.textContent = detail;

    setPreflightItem('runtime-preflight-v2', checks.v2?.text || '--', checks.v2?.tone || 'muted');
    setPreflightItem('runtime-preflight-sensors', checks.sensors?.text || '--', checks.sensors?.tone || 'muted');
    setPreflightItem('runtime-preflight-safety', checks.safety?.text || '--', checks.safety?.tone || 'muted');
    setPreflightItem('runtime-preflight-alarm', checks.alarm?.text || '--', checks.alarm?.tone || 'muted');
}

function setMissionCardState(cardId, valueId, text, route = null) {
    const card = document.getElementById(cardId);
    const valueEl = document.getElementById(valueId);
    if (!card || !valueEl) return;

    valueEl.textContent = text;
    card._missionRoute = route || null;
    card.classList.toggle('is-actionable', Boolean(route));
    card.tabIndex = route ? 0 : -1;
    card.setAttribute('role', route ? 'button' : 'group');
    card.setAttribute('aria-disabled', route ? 'false' : 'true');
    card.title = route ? 'Открыть связанную секцию' : '';
}

function setMissionControl(title, detail, tone = 'muted', goal = '--', risk = '--', action = '--', routes = {}) {
    const root = document.getElementById('operator-mission-control');
    const titleEl = document.getElementById('operator-mission-title');
    const textEl = document.getElementById('operator-mission-text');
    if (!root || !titleEl || !textEl) return;

    root.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    root.classList.add(`is-${tone}`);
    titleEl.textContent = title;
    textEl.textContent = detail;
    setMissionCardState('operator-mission-goal-card', 'operator-mission-goal', goal, routes.goal || null);
    setMissionCardState('operator-mission-risk-card', 'operator-mission-risk', risk, routes.risk || null);
    setMissionCardState('operator-mission-action-card', 'operator-mission-action', action, routes.action || null);
}

function focusMissionTarget(targetId, retries = 8) {
    if (!targetId) return;
    const target = document.getElementById(targetId);
    const visible = Boolean(target) && target.getClientRects().length > 0;
    if (!visible) {
        if (retries <= 0) return;
        window.setTimeout(() => focusMissionTarget(targetId, retries - 1), 90);
        return;
    }

    target.scrollIntoView({ block: 'center', behavior: 'smooth' });
    if (typeof target.focus === 'function') {
        target.focus({ preventScroll: true });
    }

    target.classList.remove('control-field-attention');
    void target.offsetWidth;
    target.classList.add('control-field-attention');
    window.setTimeout(() => target.classList.remove('control-field-attention'), 1700);
}

function executeMissionRoute(route) {
    if (!route) return;
    if (route.tabId) {
        activateTabById(route.tabId);
    }

    window.setTimeout(() => {
        if (route.modeKey) {
            document.querySelector(`[data-mode-select="${route.modeKey}"]`)?.click();
        }
        if (route.targetId) {
            window.setTimeout(() => focusMissionTarget(route.targetId), route.modeKey ? 120 : 0);
        }
    }, 0);
}

function ensureMissionControlBindings() {
    if (missionBindingsReady) return;
    missionBindingsReady = true;

    ['operator-mission-goal-card', 'operator-mission-risk-card', 'operator-mission-action-card'].forEach((cardId) => {
        const card = document.getElementById(cardId);
        if (!card) return;

        const run = () => executeMissionRoute(card._missionRoute || null);
        card.addEventListener('click', run);
        card.addEventListener('keydown', (event) => {
            if (event.key !== 'Enter' && event.key !== ' ') return;
            event.preventDefault();
            run();
        });
    });
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

function getLifecycleLabel(value) {
    const normalized = String(value || 'idle').toLowerCase();
    const labels = {
        idle: 'Ожидание',
        starting: 'Запуск',
        running: 'Работа',
        paused: 'Пауза',
        stopping: 'Останов',
        completed: 'Завершён',
        faulted: 'Авария'
    };
    return labels[normalized] || value;
}

function getActiveLimitsLabel(indicators, activeLimits) {
    const labels = [];
    if (Boolean(indicators.powerLimited) || Boolean(activeLimits.powerCapped)) labels.push('мощность');
    if (Boolean(activeLimits.takeoffBlocked)) labels.push('отбор');
    if (Boolean(activeLimits.phaseAdvanceBlocked)) labels.push('фаза');
    if (Boolean(activeLimits.pumpCapped)) labels.push('насос');
    return labels.length ? labels.join(', ') : 'нет';
}

function formatMissionVolumeMl(value) {
    const normalized = Math.max(0, toFinite(value, 0));
    return `${normalized.toFixed(0)} мл`;
}

function getMissionModeKey(mode) {
    switch (mode) {
        case MODE_RECT:
            return 'rectification';
        case MODE_MANUAL:
            return 'manual';
        case MODE_DIST:
            return 'distillation';
        case MODE_MASH:
            return 'mashing';
        case MODE_HOLD:
            return 'hold';
        case MODE_NBK:
            return 'nbk';
        case MODE_FERMENTATION:
            return 'fermentation';
        default:
            return '';
    }
}

function getDefaultMissionControlRoute(mode) {
    const modeKey = getMissionModeKey(mode);
    switch (modeKey) {
        case 'rectification':
            return { tabId: 'control', modeKey, targetId: 'rect-start-feed-volume' };
        case 'manual':
            return { tabId: 'control', modeKey, targetId: 'manual-feed-volume' };
        case 'distillation':
            return { tabId: 'control', modeKey, targetId: 'dist-start-end-temp' };
        case 'mashing':
            return { tabId: 'control', modeKey, targetId: 'mash-steps' };
        case 'hold':
            return { tabId: 'control', modeKey, targetId: 'hold-steps' };
        case 'nbk':
            return { tabId: 'control', modeKey, targetId: 'nbk-power-w' };
        case 'fermentation':
            return { tabId: 'control', modeKey, targetId: 'ferm-target-temp' };
        default:
            return { tabId: 'control', targetId: 'mode-start-button' };
    }
}

function buildMissionRoutes(state, indicators, activeLimits) {
    const mode = resolveMode(state.mode, state.modeStr);
    const lifecycle = String(state?.v2?.lifecycle || 'idle').toLowerCase();
    const defaultRoute = getDefaultMissionControlRoute(mode);
    const isColumnMode = mode === MODE_RECT || mode === MODE_MANUAL;
    const coolingMargin = toFinite(indicators.coolingMarginC, 0);
    const floodRisk = toFinite(indicators.floodRisk, 0);
    const stability = toFinite(indicators.stabilityIndex, 0);
    const bodyScore = toFinite(indicators.bodyEndScore, 0);
    const headsScore = toFinite(indicators.headsCompletionScore, 0);
    const hasLimits =
        Boolean(indicators.powerLimited) ||
        Boolean(activeLimits.powerCapped) ||
        Boolean(activeLimits.takeoffBlocked) ||
        Boolean(activeLimits.phaseAdvanceBlocked) ||
        Boolean(activeLimits.pumpCapped);

    if (!state?.v2?.available) {
        return {
            goal: { tabId: 'control', targetId: 'mode-start-button' },
            risk: { tabId: 'monitor', targetId: 'runtime-preflight' },
            action: { tabId: 'control', targetId: 'mode-start-button' }
        };
    }

    if (state?.currentAlarm?.active || state?.v2?.safetyLatched || lifecycle === 'faulted') {
        return {
            goal: { tabId: 'monitor', targetId: 'runtime-preflight' },
            risk: { tabId: 'monitor', targetId: 'indicator-last-reason' },
            action: { tabId: 'safety' }
        };
    }

    if (!indicators.sensorFreshnessOk) {
        return {
            goal: { tabId: 'monitor', targetId: 'indicator-sensor-freshness' },
            risk: { tabId: 'monitor', targetId: 'indicator-sensor-freshness' },
            action: { tabId: 'equipment' }
        };
    }

    if (indicators.recoveryActive) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'indicator-recovery' },
            action: { tabId: 'monitor', targetId: 'indicator-stability' }
        };
    }

    if (isColumnMode && floodRisk >= 0.65) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'indicator-flood-risk' },
            action: { tabId: 'monitor', targetId: 'indicator-cooling-margin' }
        };
    }

    if (isColumnMode && coolingMargin <= 0) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'indicator-cooling-margin' },
            action: { tabId: 'monitor', targetId: 'landing-water-out' }
        };
    }

    if (mode === MODE_FERMENTATION && (!indicators.fermTempInBand || indicators.longDeviation)) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'operator-guidance' },
            action: { tabId: 'control', modeKey: 'fermentation', targetId: 'ferm-target-temp' }
        };
    }

    if ((mode === MODE_MASH || mode === MODE_HOLD) && (indicators.overshootRisk || indicators.heatingTooSlow)) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'operator-guidance' },
            action: defaultRoute
        };
    }

    if (mode === MODE_NBK && !indicators.steamReady) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'operator-guidance' },
            action: { tabId: 'control', modeKey: 'nbk', targetId: 'nbk-power-w' }
        };
    }

    if (mode === MODE_NBK && !indicators.nbkFeedAllowed) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'operator-guidance' },
            action: { tabId: 'control', modeKey: 'nbk', targetId: 'nbk-pump-speed' }
        };
    }

    if (Boolean(activeLimits.takeoffBlocked) || (isColumnMode && !indicators.takeoffAllowed)) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'indicator-takeoff' },
            action: { tabId: 'monitor', targetId: 'indicator-stability' }
        };
    }

    if (isColumnMode && coolingMargin < 5) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'indicator-cooling-margin' },
            action: { tabId: 'monitor', targetId: 'landing-water-out' }
        };
    }

    if (isColumnMode && floodRisk >= 0.35) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'indicator-flood-risk' },
            action: { tabId: 'monitor', targetId: 'indicator-pressure-stable' }
        };
    }

    if (mode === MODE_RECT && bodyScore >= 0.8) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'indicator-body-score' },
            action: { tabId: 'control', modeKey: 'rectification', targetId: 'rect-start-body-percent' }
        };
    }

    if (mode === MODE_RECT && headsScore >= 0.8) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'indicator-heads-score' },
            action: { tabId: 'control', modeKey: 'rectification', targetId: 'rect-start-body-speed' }
        };
    }

    if (isColumnMode && stability < 0.45) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'indicator-stability' },
            action: mode === MODE_RECT
                ? { tabId: 'control', modeKey: 'rectification', targetId: 'rect-start-stabilization' }
                : defaultRoute
        };
    }

    if (hasLimits) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'indicator-power-limit' },
            action: { tabId: 'monitor', targetId: 'indicator-last-reason' }
        };
    }

    if (String(state?.v2?.operatorMessage || '').trim()) {
        return {
            goal: defaultRoute,
            risk: { tabId: 'monitor', targetId: 'operator-guidance' },
            action: { tabId: 'monitor', targetId: 'operator-guidance' }
        };
    }

    return {
        goal: defaultRoute,
        risk: {
            tabId: 'monitor',
            targetId: isColumnMode ? 'operator-guidance' : 'indicator-process-health'
        },
        action: mode === MODE_IDLE
            ? { tabId: 'control', targetId: 'mode-start-button' }
            : { tabId: 'monitor', targetId: 'operator-guidance' }
    };
}

function buildMissionSnapshot(state, indicators, activeLimits) {
    const mode = resolveMode(state.mode, state.modeStr);
    const lifecycle = String(state?.v2?.lifecycle || 'idle').toLowerCase();
    const operatorMessage = String(state?.v2?.operatorMessage || '').trim();
    const lastReasonCode = String(state?.v2?.lastReasonCode || 'RC_NONE');
    const phase = toFinite(state.phase, 0);
    const stability = toFinite(indicators.stabilityIndex, 0);
    const floodRisk = toFinite(indicators.floodRisk, 0);
    const coolingMargin = toFinite(indicators.coolingMarginC, 0);
    const bodyScore = toFinite(indicators.bodyEndScore, 0);
    const headsScore = toFinite(indicators.headsCompletionScore, 0);
    const isColumnMode = mode === MODE_RECT || mode === MODE_MANUAL;
    const hasLimits =
        Boolean(indicators.powerLimited) ||
        Boolean(activeLimits.powerCapped) ||
        Boolean(activeLimits.takeoffBlocked) ||
        Boolean(activeLimits.phaseAdvanceBlocked) ||
        Boolean(activeLimits.pumpCapped);

    if (!state?.v2?.available) {
        return {
            tone: 'muted',
            title: 'Ждём телеметрию автоматики',
            detail: 'После первого полного статуса indicators v2 здесь появится короткая диспетчерская сводка по текущему сценарию.',
            goal: 'Подготовить режим и проверить стартовые параметры',
            risk: 'Нет свежего статуса safety и датчиков',
            action: 'Дождаться первого полного пакета или перейти во вкладку режимов'
        };
    }

    if (state?.currentAlarm?.active || state?.v2?.safetyLatched || lifecycle === 'faulted') {
        return {
            tone: 'danger',
            title: 'Процесс удержан safety',
            detail: operatorMessage || `Автоматика остановила сценарий. Последняя причина: ${getReasonCodeLabel(lastReasonCode)}.`,
            goal: 'Удержать установку в безопасном состоянии',
            risk: getReasonCodeLabel(lastReasonCode),
            action: 'Проверить alarm, воду, датчики и снять блокировку только после устранения причины'
        };
    }

    if (!indicators.sensorFreshnessOk) {
        return {
            tone: 'danger',
            title: 'Нет доверия к телеметрии',
            detail: 'Данные датчиков устарели, поэтому автоматика и веб-панель не могут уверенно вести процесс.',
            goal: 'Вернуть свежие показания всех ключевых датчиков',
            risk: 'Старые данные по температуре, давлению или safety',
            action: 'Проверить соединение датчиков и дождаться обновления телеметрии'
        };
    }

    let title = 'Пульт ожидает следующий запуск';
    let detail = 'Система в idle, можно подготовить следующий сценарий и проверить стартовые условия.';
    let goal = 'Подготовить следующий запуск';

    if (mode === MODE_RECT) {
        const effectiveAbv = getEffectiveAbvForCalculations();
        const est = estimateRectTargets(state.rectification, effectiveAbv.value);
        const targetHeads = toFinite(state.rectification.headsTargetMl, 0) > 0 ? toFinite(state.rectification.headsTargetMl, 0) : est.heads;
        const targetBody = toFinite(state.rectification.bodyTargetMl, 0) > 0 ? toFinite(state.rectification.bodyTargetMl, 0) : est.body;
        const targetTails = toFinite(state.rectification.tailsTargetMl, 0) > 0 ? toFinite(state.rectification.tailsTargetMl, 0) : est.tails;
        const headsRemaining = Math.max(0, targetHeads - toFinite(state.volumes.heads, 0));
        const bodyRemaining = Math.max(0, targetBody - toFinite(state.volumes.body, 0));
        const tailsRemaining = Math.max(0, targetTails - toFinite(state.volumes.tails, 0));

        title = lifecycle === 'starting' ? 'Колонна выходит на рабочее окно' : 'Ректификация под контролем';
        detail = `Фаза: ${state.phaseStr || phase || '-'}. Автоматика ведёт профиль отбора и следит за стабильностью колонны.`;
        if (phase < PHASE_HEADS) goal = 'Вывести колонну в рабочее окно перед отбором голов';
        else if (phase === PHASE_HEADS) goal = `Добрать головы, осталось около ${formatMissionVolumeMl(headsRemaining)}`;
        else if (phase === PHASE_POST_HEADS_STAB) goal = 'Дождаться постстабилизации перед переходом на тело';
        else if (phase === PHASE_BODY) goal = `Вести тело, осталось около ${formatMissionVolumeMl(bodyRemaining)}`;
        else if (phase >= PHASE_TAILS) goal = `Добрать хвосты, осталось около ${formatMissionVolumeMl(tailsRemaining)}`;
    } else if (mode === MODE_DIST) {
        const target = Math.max(0, toFinite(state.distillation.targetVolumeMl, 0));
        const total = Math.max(0, toFinite(state.pump.totalMl, 0));
        const remaining = Math.max(0, target - total);
        const speed = Math.max(0, toFinite(state.distillation.speedMlH, 0));
        title = 'Перегон под контролем';
        detail = target > 0
            ? `Собрано ${formatMissionVolumeMl(total)} из ${formatMissionVolumeMl(target)} при скорости около ${speed.toFixed(0)} мл/ч.`
            : `Фаза: ${state.phaseStr || phase || '-'}. Процесс идёт без жёстко заданного целевого объёма.`;
        goal = target > 0
            ? `Добрать перегон, осталось около ${formatMissionVolumeMl(remaining)}`
            : 'Вести перегон до стоп-температуры или технологического сигнала завершения';
    } else if (mode === MODE_MASH) {
        const totalSteps = Math.max(0, Math.round(toFinite(state.mashing.stepCount, 0)));
        const currentStep = Math.max(0, Math.round(toFinite(state.mashing.currentStep, 0)));
        const stepIndex = totalSteps > 0 ? Math.min(currentStep + 1, totalSteps) : 0;
        const stepName = String(state.mashing.stepName || '').trim();
        title = 'Заторный профиль выполняется';
        detail = totalSteps > 0
            ? `Активен шаг ${stepIndex} из ${totalSteps}, до конца текущего шага около ${formatDurationSafe(state.mashing.remainingSec)}.`
            : 'Профиль затирки ещё не загружен или ждёт запуска.';
        goal = totalSteps > 0
            ? `Шаг ${stepIndex} из ${totalSteps}${stepName ? `: ${stepName}` : ''}`
            : 'Подготовить и запустить профиль затирки';
    } else if (mode === MODE_HOLD) {
        const targetTemp = toFinite(state.hold.targetTemp, 0);
        const stepIndex = Math.max(0, Math.round(toFinite(state.hold.currentStep, 0))) + 1;
        title = 'Пауза выдержки выполняется';
        detail = `Осталось около ${formatDurationSafe(state.hold.remainingSec)}. ${targetTemp > 0 ? `Цель по температуре ${targetTemp.toFixed(1)}°C.` : 'Шаг идёт без активного нагрева.'}`;
        goal = targetTemp > 0
            ? `Удерживать около ${targetTemp.toFixed(1)}°C до завершения шага ${stepIndex}`
            : `Довести до конца шаг выдержки ${stepIndex}`;
    } else if (mode === MODE_MANUAL) {
        const heads = Math.max(0, toFinite(state.volumes.heads, 0));
        const body = Math.max(0, toFinite(state.volumes.body, 0));
        const tails = Math.max(0, toFinite(state.volumes.tails, 0));
        title = 'Ручной режим под контролем';
        detail = `Собрано: головы ${formatMissionVolumeMl(heads)}, тело ${formatMissionVolumeMl(body)}, хвосты ${formatMissionVolumeMl(tails)}.`;
        goal = 'Вести ручной отбор и корректировать мощность, воду и насос по месту';
    } else if (mode === MODE_NBK) {
        title = 'НБК под контролем';
        detail = `Фаза: ${state.nbk?.phaseStr || state.phaseStr || 'idle'}. Подача ${toFinite(state.pump.speedMlH, 0).toFixed(0)} мл/ч, мощность ${toFinite(state.nbk?.powerW, 0).toFixed(0)} Вт.`;
        if (!indicators.steamReady) goal = 'Разогреть НБК до готовности пара';
        else if (!indicators.nbkFeedAllowed) goal = 'Дождаться разрешения подачи браги';
        else goal = 'Вести стабильную подачу браги без провала по пару и давлению';
    } else if (mode === MODE_FERMENTATION) {
        const targetTemp = toFinite(state.fermentation?.targetTempC, 0);
        const hysteresis = toFinite(state.fermentation?.hysteresisC, 0);
        title = 'Брожение под контролем';
        detail = targetTemp > 0
            ? `Контур удерживает около ${targetTemp.toFixed(1)}°C с гистерезисом ${hysteresis.toFixed(1)}°C.`
            : 'Контур брожения активен, но целевая температура не задана.';
        goal = targetTemp > 0
            ? `Удерживать температуру брожения около ${targetTemp.toFixed(1)}°C`
            : 'Задать и удерживать рабочую температуру брожения';
    }

    if (lifecycle === 'paused') {
        title = 'Процесс на паузе';
        detail = operatorMessage || `${title}. Перед продолжением убедитесь, что условия процесса всё ещё валидны.`;
    } else if (lifecycle === 'stopping') {
        title = 'Процесс завершает цикл';
        detail = operatorMessage || 'Автоматика сворачивает режим и ведёт установку к безопасной остановке.';
    } else if (lifecycle === 'completed') {
        title = 'Цикл завершён';
        detail = operatorMessage || 'Основной сценарий отработан, можно провести оценку результата и подготовить следующий запуск.';
    }

    if (indicators.recoveryActive) {
        return {
            tone: 'warn',
            title,
            detail,
            goal,
            risk: 'Система в recovery и ждёт повторной стабилизации',
            action: 'Не форсировать процесс, дождаться ровного окна по температуре, давлению и охлаждению'
        };
    }

    if (isColumnMode && floodRisk >= 0.65) {
        return {
            tone: 'danger',
            title,
            detail,
            goal,
            risk: 'Высокий риск захлёба колонны',
            action: 'Не повышать мощность, снизить нагрузку и проверить охлаждение с давлением'
        };
    }

    if (isColumnMode && coolingMargin <= 0) {
        return {
            tone: 'danger',
            title,
            detail,
            goal,
            risk: 'Охлаждение на пределе, запас исчерпан',
            action: 'Добавить воду или снизить нагрузку, пока не вернётся положительный cooling margin'
        };
    }

    if (mode === MODE_FERMENTATION && (!indicators.fermTempInBand || indicators.longDeviation)) {
        return {
            tone: 'warn',
            title,
            detail,
            goal,
            risk: indicators.longDeviation ? 'Температура брожения долго вне диапазона' : 'Температура брожения ещё не вошла в диапазон',
            action: 'Проверить контур нагрева или охлаждения и не оставлять процесс без контроля'
        };
    }

    if ((mode === MODE_MASH || mode === MODE_HOLD) && indicators.overshootRisk) {
        return {
            tone: 'warn',
            title,
            detail,
            goal,
            risk: 'Есть риск перегрева на текущем шаге',
            action: 'Следить за подходом к цели и не поднимать нагрев агрессивнее профиля'
        };
    }

    if ((mode === MODE_MASH || mode === MODE_HOLD) && indicators.heatingTooSlow) {
        return {
            tone: 'warn',
            title,
            detail,
            goal,
            risk: 'Нагрев отстаёт от профиля',
            action: 'Проверить реальную мощность, теплопотери и корректность задания шага'
        };
    }

    if (mode === MODE_NBK && !indicators.steamReady) {
        return {
            tone: 'warn',
            title,
            detail,
            goal,
            risk: 'Пар ещё не готов для устойчивой подачи',
            action: 'Дождаться выхода НБК в рабочее окно перед подачей браги'
        };
    }

    if (mode === MODE_NBK && !indicators.nbkFeedAllowed) {
        return {
            tone: 'warn',
            title,
            detail,
            goal,
            risk: 'Подача браги пока заблокирована автоматикой',
            action: 'Не открывать подачу вручную, дождаться разрешения по пару и устойчивости'
        };
    }

    if (Boolean(activeLimits.takeoffBlocked) || (isColumnMode && !indicators.takeoffAllowed)) {
        return {
            tone: 'warn',
            title,
            detail,
            goal,
            risk: 'Отбор пока не разрешён автоматикой',
            action: 'Дождаться устойчивого окна по stability, cooling margin и состоянию датчиков'
        };
    }

    if (isColumnMode && coolingMargin < 5) {
        return {
            tone: 'warn',
            title,
            detail,
            goal,
            risk: `Запас охлаждения низкий: ${coolingMargin.toFixed(1)}°C`,
            action: 'Не форсировать мощность и следить, чтобы охлаждение не просело ещё сильнее'
        };
    }

    if (isColumnMode && floodRisk >= 0.35) {
        return {
            tone: 'warn',
            title,
            detail,
            goal,
            risk: 'Нагрузка колонны растёт, риск захлёба повышен',
            action: 'Следить за верхом колонны, давлением и скоростью отбора без резких движений'
        };
    }

    if (mode === MODE_RECT && bodyScore >= 0.8) {
        return {
            tone: 'warn',
            title,
            detail,
            goal,
            risk: 'Тело близко к завершению по body score',
            action: 'Подготовить переход по профилю и внимательнее контролировать качество продукта'
        };
    }

    if (mode === MODE_RECT && headsScore >= 0.8) {
        return {
            tone: 'good',
            title,
            detail,
            goal,
            risk: 'Критичных рисков не видно, головы почти завершены',
            action: 'Подготовиться к переходу на тело по правилам текущего профиля'
        };
    }

    if (isColumnMode && stability < 0.45) {
        return {
            tone: 'warn',
            title,
            detail,
            goal,
            risk: 'Колонна ещё не вошла в устойчивое окно',
            action: 'Дать системе стабилизироваться и не ускорять отбор раньше времени'
        };
    }

    if (hasLimits) {
        return {
            tone: 'warn',
            title,
            detail,
            goal,
            risk: `Активны ограничения: ${getActiveLimitsLabel(indicators, activeLimits)}`,
            action: 'Понять причину ограничений до того, как усиливать нагрев, насос или отбор'
        };
    }

    if (operatorMessage) {
        return {
            tone: mode === MODE_IDLE ? 'muted' : 'good',
            title,
            detail,
            goal,
            risk: 'Критичных рисков по indicators v2 сейчас не видно',
            action: operatorMessage
        };
    }

    return {
        tone: mode === MODE_IDLE ? 'good' : 'muted',
        title,
        detail,
        goal,
        risk: isColumnMode
            ? 'Критичных ограничений по колонне сейчас не видно'
            : 'Критичных ограничений по процессу сейчас не видно',
        action: mode === MODE_IDLE
            ? 'Перейти в режимы, проверить параметры и запускать сценарий'
            : 'Продолжать процесс и сверять ключевые показатели по этой панели'
    };
}

export function getRuntimePreflightState(state = runtimeMonitorState) {
    const indicators = state?.v2?.indicators || {};
    const activeLimits = state?.v2?.activeLimits || {};
    const lifecycle = String(state?.v2?.lifecycle || 'idle').toLowerCase();
    const activeAlarm = Boolean(state?.currentAlarm?.active);
    const safetyLatched = Boolean(state?.v2?.safetyLatched);
    const v2Available = Boolean(state?.v2?.available);
    const sensorsFresh = Boolean(indicators.sensorFreshnessOk);
    const operatorMessage = String(state?.v2?.operatorMessage || '').trim();
    const hasLimits =
        Boolean(indicators.powerLimited) ||
        Boolean(activeLimits.powerCapped) ||
        Boolean(activeLimits.takeoffBlocked) ||
        Boolean(activeLimits.phaseAdvanceBlocked) ||
        Boolean(activeLimits.pumpCapped);

    const checks = {
        v2: {
            text: v2Available ? 'OK' : 'Нет данных',
            tone: v2Available ? 'good' : 'warn'
        },
        sensors: {
            text: !v2Available ? 'Ждём' : (sensorsFresh ? 'OK' : 'Проверьте'),
            tone: !v2Available ? 'muted' : (sensorsFresh ? 'good' : 'danger')
        },
        safety: {
            text: !v2Available ? 'Ждём' : (safetyLatched ? 'Latch' : 'Норма'),
            tone: !v2Available ? 'muted' : (safetyLatched ? 'danger' : 'good')
        },
        alarm: {
            text: activeAlarm ? 'Активна' : 'Нет',
            tone: activeAlarm ? 'danger' : 'good'
        }
    };

    if (!v2Available) {
        return {
            tone: 'muted',
            title: 'Ждём статус автоматики',
            detail: 'Контур indicators v2 ещё не прислал полный пакет. Перед запуском дождитесь первого осмысленного статуса.',
            checks
        };
    }

    if (activeAlarm || safetyLatched || lifecycle === 'faulted') {
        return {
            tone: 'danger',
            title: 'Запуск заблокирован',
            detail: operatorMessage || 'Есть активная авария или safety latch. Сначала снимите блокировку и проверьте причину последнего trip.',
            checks
        };
    }

    if (!sensorsFresh) {
        return {
            tone: 'danger',
            title: 'Нужна проверка датчиков',
            detail: 'Телеметрия устарела. Без свежих датчиков автоматика не должна запускать процесс в рабочем режиме.',
            checks
        };
    }

    if (lifecycle === 'starting' || lifecycle === 'running' || lifecycle === 'paused' || lifecycle === 'stopping') {
        return {
            tone: 'warn',
            title: 'Режим уже активен',
            detail: operatorMessage || `Сейчас lifecycle: ${getLifecycleLabel(lifecycle)}. Это уже не стартовый экран, а контроль запущенного процесса.`,
            checks
        };
    }

    if (hasLimits) {
        return {
            tone: 'warn',
            title: 'Старт возможен с оговорками',
            detail: `Автоматика уже видит ограничения: ${getActiveLimitsLabel(indicators, activeLimits)}. Перед запуском лучше понять, почему они появились заранее.`,
            checks
        };
    }

    return {
        tone: 'good',
        title: 'Можно запускать',
        detail: 'Контур v2 на связи, датчики свежие, аварий и latch сейчас нет. Можно переходить к старту режима с веб-интерфейса.',
        checks
    };
}

export function getStartAvailabilityState(state = runtimeMonitorState) {
    const preflight = getRuntimePreflightState(state);
    const lifecycle = String(state?.v2?.lifecycle || 'idle').toLowerCase();
    const activeAlarm = Boolean(state?.currentAlarm?.active);
    const safetyLatched = Boolean(state?.v2?.safetyLatched);
    const sensorsFresh = Boolean(state?.v2?.indicators?.sensorFreshnessOk);
    const isIdle = currentMode === MODE_IDLE;
    const runtimeActive = !isIdle || lifecycle === 'starting' || lifecycle === 'running' || lifecycle === 'paused' || lifecycle === 'stopping';
    const blockStart = runtimeActive || activeAlarm || safetyLatched || lifecycle === 'faulted' || (Boolean(state?.v2?.available) && !sensorsFresh);

    if (runtimeActive) {
        return {
            tone: 'warn',
            title: 'Сначала завершите текущий процесс',
            detail: 'Пока автоматика не вернулась в idle, запуск нового режима с панели недоступен.',
            disabled: true,
            buttonLabel: 'Открыть управление',
            preflight
        };
    }

    if (blockStart) {
        return {
            tone: 'danger',
            title: preflight.title,
            detail: preflight.detail,
            disabled: true,
            buttonLabel: 'Проверить условия старта',
            preflight
        };
    }

    if (preflight.tone === 'warn') {
        return {
            tone: 'warn',
            title: 'Можно запускать с оговорками',
            detail: preflight.detail,
            disabled: false,
            buttonLabel: 'Выбрать режим и запустить',
            preflight
        };
    }

    if (preflight.tone === 'good') {
        return {
            tone: 'good',
            title: 'Система готова к запуску',
            detail: 'Выберите режим, проверьте параметры и запускайте процесс с панели управления.',
            disabled: false,
            buttonLabel: 'Выбрать режим и запустить',
            preflight
        };
    }

    return {
        tone: 'muted',
        title: 'Статус запуска уточняется',
        detail: preflight.detail,
        disabled: false,
        buttonLabel: 'Открыть режимы',
        preflight
    };
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
        getLifecycleLabel(lifecycle),
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
        Boolean(activeLimits.phaseAdvanceBlocked) ||
        Boolean(activeLimits.pumpCapped);
    setIndicatorValue('indicator-power-limit', hasLimit ? 'Есть' : 'Нет', hasLimit ? 'warn' : 'good');

    const recovery = boolLabel(indicators.recoveryActive, 'Активен', 'Нет');
    setIndicatorValue('indicator-recovery', recovery.text, recovery.tone);
    setIndicatorValue(
        'indicator-power-limit',
        getActiveLimitsLabel(indicators, activeLimits),
        hasLimit ? 'warn' : 'good'
    );
    const guidance = buildGuidance(s, indicators, activeLimits);
    setGuidance(guidance.title, guidance.detail, guidance.tone);
}

function renderProcessIndicatorsPanel() {
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
    const hasLimit =
        Boolean(indicators.powerLimited) ||
        Boolean(activeLimits.powerCapped) ||
        Boolean(activeLimits.takeoffBlocked) ||
        Boolean(activeLimits.phaseAdvanceBlocked) ||
        Boolean(activeLimits.pumpCapped);

    setIndicatorValue(
        'indicator-lifecycle',
        getLifecycleLabel(lifecycle),
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

    const recovery = boolLabel(indicators.recoveryActive, 'Активен', 'Нет');
    setIndicatorValue('indicator-recovery', recovery.text, recovery.tone);
    setIndicatorValue(
        'indicator-power-limit',
        getActiveLimitsLabel(indicators, activeLimits),
        hasLimit ? 'warn' : 'good'
    );

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

    const indicators = s?.v2?.indicators || {};
    const activeLimits = s?.v2?.activeLimits || {};
    const mission = buildMissionSnapshot(s, indicators, activeLimits);
    const missionRoutes = buildMissionRoutes(s, indicators, activeLimits);
    const preflight = getRuntimePreflightState(s);
    setMissionControl(mission.title, mission.detail, mission.tone, mission.goal, mission.risk, mission.action, missionRoutes);
    setPreflightState(preflight.title, preflight.detail, preflight.tone, preflight.checks);
    renderRuntimeBars(items);
    updateManualTiles();
    renderProcessIndicatorsPanel();
    if (manualEl) {
        manualEl.style.display = mode === MODE_MANUAL ? 'grid' : 'none';
    }
    const rectEl = document.getElementById('mode-runtime-rect');
    if (rectEl) {
        rectEl.style.display = mode === MODE_RECT ? 'grid' : 'none';
    }
}

export function initRuntimeMonitorUi() {
    ensureMissionControlBindings();
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
