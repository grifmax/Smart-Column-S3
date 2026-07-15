import { runtimeMonitorState, currentMode, resolveMode, maxHeaterPower, MODE_IDLE, MODE_RECT, PHASE_HEADS, PHASE_BODY, PHASE_POST_HEADS_STAB, PHASE_TAILS, MODE_DIST, MODE_MASH, MODE_HOLD, MODE_MANUAL, MODE_NBK, MODE_FERMENTATION } from '../globals.js';
import { activateTabById } from '../core/tabs.js';
import { clampPercent, runtimeEscapeHtml, toFinite, formatDurationSafe } from '../runtime/helpers.js';
import { getEffectiveAbvForCalculations } from '../runtime/abv.js';
import { estimateRectTargets, getRectificationTakeoffRateMlH } from '../runtime/state.js';

const FRACTION_PROGRAM_END_VOLUME = 1 << 0;
const FRACTION_PROGRAM_END_TIME = 1 << 1;
const FRACTION_PROGRAM_END_TEMPERATURE = 1 << 2;
const FRACTION_PROGRAM_END_LEVEL = 1 << 3;

let missionBindingsReady = false;
let diagnosticsPanelBindingsReady = false;
let mobileDiagnosticsBindingsReady = false;

const MOBILE_MONITOR_MEDIA = '(max-width: 900px)';
const MOBILE_DIAGNOSTICS_SECTION_IDS = [
    'mode-runtime-card',
    'operator-process-card',
    'monitor-diagnostics-panel'
];
const mobileDiagnosticsHome = new Map();

function setIndicatorValue(id, text, tone = 'muted') {
    const el = document.getElementById(id);
    if (!el) return;
    el.textContent = text;
    el.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    el.classList.add(`is-${tone}`);
}

function setElementHidden(id, hidden) {
    const el = document.getElementById(id);
    if (el) el.hidden = Boolean(hidden);
}

function hasMeaningfulConfidence(value) {
    return toFinite(value, -1) >= 0;
}

function getPressureAvailability(state, indicators) {
    const pressure = state?.pressure || {};
    const signalAvailable = Boolean(indicators?.pressureSensorAvailable || pressure.ok);
    const hardwareAvailable = signalAvailable || Boolean(pressure.ads1115Available);
    return {
        signalAvailable,
        hardwareAvailable
    };
}

function isCoolingMetricsRelevant(mode, indicators) {
    return (
        mode === MODE_RECT ||
        mode === MODE_MANUAL ||
        mode === MODE_DIST ||
        mode === MODE_NBK ||
        Boolean(indicators?.coolingSensorAvailable) ||
        Boolean(indicators?.coolingDemand) ||
        Boolean(indicators?.recoveryActive)
    );
}

function formatIndicatorPercent(value) {
    return `${clampPercent(toFinite(value, 0) * 100).toFixed(0)}%`;
}

function formatConfidencePercent(value) {
    const normalized = toFinite(value, -1);
    if (normalized < 0) {
        return '—';
    }
    return `${clampPercent(normalized * 100).toFixed(0)}%`;
}

function getConfidenceTone(value, invert = false) {
    const normalized = toFinite(value, -1);
    if (normalized < 0) {
        return 'muted';
    }
    if (invert) {
        if (normalized >= 0.75) return 'danger';
        if (normalized >= 0.45) return 'warn';
        return 'good';
    }
    if (normalized >= 0.75) return 'good';
    if (normalized >= 0.45) return 'warn';
    return 'danger';
}

function getIndicatorPercentTone(value, warnThreshold, goodThreshold, invert = false) {
    const normalized = toFinite(value, -1);
    if (normalized < 0) {
        return 'muted';
    }
    if (invert) {
        if (normalized >= goodThreshold) return 'danger';
        if (normalized >= warnThreshold) return 'warn';
        return 'good';
    }
    if (normalized >= goodThreshold) return 'good';
    if (normalized >= warnThreshold) return 'warn';
    return 'danger';
}

function boolLabel(value, goodText, badText, invert = false) {
    const ok = invert ? !value : !!value;
    return {
        text: ok ? goodText : badText,
        tone: ok ? 'good' : 'danger'
    };
}

function formatSignedValue(value, digits = 1, unit = '') {
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) {
        return '—';
    }
    const sign = numeric > 0 ? '+' : '';
    return `${sign}${numeric.toFixed(digits)}${unit}`;
}

function getRectTakeoffBackendLabel(backendType) {
    if (backendType === 1) return '3 клапана';
    if (backendType === 2) return '1 клапан + переключение';
    return 'насос';
}

function getRectTakeoffFractionLabel(fraction, fallback = 'отбор закрыт') {
    if (fraction === 1) return 'головы';
    if (fraction === 2) return 'тело';
    if (fraction === 3) return 'хвосты';
    return fallback;
}

function getRectTakeoffValveLabel(backendType, activeValve) {
    if (backendType === 2) {
        return activeValve === 1 ? 'общий клапан отбора' : 'закрыт';
    }
    if (activeValve === 1) return 'клапан голов';
    if (activeValve === 2) return 'клапан тела';
    if (activeValve === 3) return 'клапан хвостов';
    return 'закрыт';
}

function getRectTakeoffDutyLabel(backendType, actualDuty, backendActive, requestedFraction) {
    const dutyByte = Math.max(0, Math.min(255, Math.round(toFinite(actualDuty, 0))));
    const dutyPercent = clampPercent((dutyByte / 255) * 100);

    if (backendType === 0) {
        if (backendActive) return 'непрерывный (100%)';
        if (requestedFraction > 0) return 'пауза / закрыт';
        return 'выкл';
    }

    if (!backendActive && requestedFraction === 0) {
        return 'выкл';
    }

    return `${dutyByte}/255 (${dutyPercent.toFixed(0)}%)`;
}

function renderRectTakeoffDetails(container, rectification) {
    if (!container) return;

    const backendType = Math.round(toFinite(rectification?.takeoffBackendType, 0));
    const backendLabel = getRectTakeoffBackendLabel(backendType);
    const backendActive = Boolean(rectification?.takeoffBackendActive);
    const routingReady = Boolean(rectification?.takeoffRoutingReady ?? true);
    const requestedFraction = Math.round(toFinite(rectification?.takeoffRequestedFraction, 0));
    const routedFraction = Math.round(toFinite(rectification?.takeoffRoutedFraction, 0));
    const activeFraction = Math.round(toFinite(rectification?.takeoffActiveFraction, 0));
    const activeValve = Math.round(toFinite(rectification?.takeoffActiveValve, 0));
    const actualDuty = Math.round(toFinite(rectification?.takeoffActualDuty, 0));
    const actualRate = Math.max(0, toFinite(rectification?.takeoffActualEquivalentRateMlH, 0));

    const requestedLabel = getRectTakeoffFractionLabel(requestedFraction);
    const routedLabel = getRectTakeoffFractionLabel(
        routedFraction,
        backendType === 2 ? 'маршрут не занят' : 'канал не выбран'
    );
    const activeLabel = getRectTakeoffFractionLabel(activeFraction);
    const activeValveLabel = getRectTakeoffValveLabel(backendType, activeValve);
    const routeLabel = backendType === 2
        ? (routingReady ? routedLabel : `${routedLabel} -> ${requestedLabel}`)
        : (routedFraction > 0 ? routedLabel : requestedLabel);
    const dutyLabel = getRectTakeoffDutyLabel(backendType, actualDuty, backendActive, requestedFraction);
    const equivalentRateLabel = `${actualRate.toFixed(0)} мл/ч`;
    const stateLabel = backendActive
        ? `отбор открыт${actualRate > 0 ? ` • ${actualRate.toFixed(0)} мл/ч` : ''}`
        : requestedFraction > 0
            ? (routingReady ? 'готов к отбору' : 'ждет маршрута')
            : 'отбор закрыт';

    const cards = [
        { label: 'Исполнитель', value: backendLabel },
        { label: 'Команда', value: requestedLabel },
        { label: 'Маршрут', value: routeLabel },
        { label: 'Статус', value: stateLabel },
        { label: 'Duty / импульс', value: dutyLabel },
        { label: 'Эквив. скорость', value: equivalentRateLabel },
        { label: 'Активная фракция', value: activeLabel },
        { label: 'Активный клапан', value: activeValveLabel },
        { label: 'Готовность', value: routingReady ? 'маршрут готов' : 'переключение' }
    ];

    container.innerHTML = cards.map((card) => `
        <div class="operator-stat">
            <span class="operator-stat-label">${runtimeEscapeHtml(card.label)}</span>
            <strong class="operator-stat-value">${runtimeEscapeHtml(card.value)}</strong>
        </div>
    `).join('');
}

function renderPressureIndicators(state, indicators) {
    const pressure = state?.pressure || {};
    const pressureCube = Number(pressure.cube);
    const pressureRate = Number(indicators.pressureRateMmHgPerMin);
    const pressureMargin = Number(indicators.distPressureMargin);
    const signalAvailable = Boolean(indicators.pressureSensorAvailable || pressure.ok);
    const hardwareAvailable = signalAvailable || Boolean(pressure.ads1115Available);

    const missingText = hardwareAvailable ? 'Нет сигнала' : 'Нет датчика';
    let pressureTone = 'muted';
    if (signalAvailable) {
        if (Number.isFinite(pressureMargin) && pressureMargin <= 0) pressureTone = 'danger';
        else if (Number.isFinite(pressureMargin) && pressureMargin < 10) pressureTone = 'warn';
        else pressureTone = 'good';
    }

    let pressureRateTone = 'muted';
    if (signalAvailable && Number.isFinite(pressureRate)) {
        const rateAbs = Math.abs(pressureRate);
        if (rateAbs >= 3) pressureRateTone = 'danger';
        else if (rateAbs >= 1) pressureRateTone = 'warn';
        else pressureRateTone = 'good';
    }

    setIndicatorValue(
        'indicator-pressure-cube',
        signalAvailable && Number.isFinite(pressureCube) ? `${pressureCube.toFixed(1)} мм` : missingText,
        pressureTone
    );
    setIndicatorValue(
        'indicator-pressure-margin',
        signalAvailable && Number.isFinite(pressureMargin) ? `${pressureMargin.toFixed(1)} мм` : '—',
        pressureTone
    );
    setIndicatorValue(
        'indicator-pressure-diagnostics',
        signalAvailable && Number.isFinite(pressureCube) ? `${pressureCube.toFixed(1)} мм рт.ст.` : missingText,
        pressureTone
    );
    setIndicatorValue(
        'indicator-pressure-rate',
        signalAvailable && Number.isFinite(pressureRate) ? formatSignedValue(pressureRate, 1, ' мм/мин') : '—',
        pressureRateTone
    );
    setIndicatorValue(
        'indicator-pressure-margin-diagnostics',
        signalAvailable && Number.isFinite(pressureMargin) ? `${pressureMargin.toFixed(1)} мм` : '—',
        pressureTone
    );

    const pressureFloodEl = document.getElementById('pressure-flood');
    if (pressureFloodEl) {
        pressureFloodEl.textContent =
            signalAvailable && Number.isFinite(pressureMargin) ? `${pressureMargin.toFixed(1)} мм` : '-- мм';
    }
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

function syncOperatorQuietPanels(state, context = {}) {
    const mode = resolveMode(state?.mode, state?.modeStr);
    const lifecycle = String(state?.v2?.lifecycle || 'idle').toLowerCase();
    const lastReasonCode = String(state?.v2?.lastReasonCode || 'RC_NONE');
    const operatorMessage = String(state?.v2?.operatorMessage || '').trim();
    const hasRuntimeItems = Boolean(context.hasRuntimeItems);
    const hasGuidanceContext = Object.prototype.hasOwnProperty.call(context, 'guidanceTone');
    const hasDiagnosticsContext = Object.prototype.hasOwnProperty.call(context, 'diagnosticsTone');

    if (hasGuidanceContext) {
        const guidanceTone = String(context.guidanceTone || 'muted').toLowerCase();
        const hasLimit = Boolean(context.hasLimit);
        const activeAlarm = Boolean(context.activeAlarm);
        const guidanceVisible =
            activeAlarm ||
            hasLimit ||
            operatorMessage.length > 0 ||
            guidanceTone === 'warn' ||
            guidanceTone === 'danger';
        setElementHidden('operator-guidance', !guidanceVisible);
    }

    const runtimeCard = document.getElementById('mode-runtime-card');
    if (runtimeCard) {
        const shouldOpenRuntime =
            hasRuntimeItems ||
            mode !== MODE_IDLE ||
            lifecycle === 'starting' ||
            lifecycle === 'running' ||
            lifecycle === 'paused';
        if (shouldOpenRuntime) runtimeCard.setAttribute('open', 'open');
        else runtimeCard.removeAttribute('open');
    }

    if (hasDiagnosticsContext) {
        const diagnosticsTone = String(context.diagnosticsTone || 'muted').toLowerCase();
        const hasLimit = Boolean(context.hasLimit);
        const activeAlarm = Boolean(context.activeAlarm);
        const telemetryDanger = Boolean(context.telemetryDanger);
        const diagnosticsMeaningful =
            activeAlarm ||
            hasLimit ||
            telemetryDanger ||
            diagnosticsTone === 'warn' ||
            diagnosticsTone === 'danger' ||
            operatorMessage.length > 0 ||
            lastReasonCode !== 'RC_NONE';

        updateDiagnosticsPanelState(diagnosticsTone, context.diagnosticsText || 'Скрыто', {
            show: diagnosticsMeaningful,
            forceOpen: diagnosticsTone === 'danger',
            forceClosed: diagnosticsTone !== 'danger'
        });
    }
}

function getPublishedGuidance(state) {
    const guidance = state?.v2?.guidance;
    if (!guidance || typeof guidance !== 'object') {
        return null;
    }

    const title = String(guidance.title || '').trim();
    const detail = String(guidance.detail || '').trim();
    if (!title && !detail) {
        return null;
    }

    return {
        tone: String(guidance.tone || 'muted').trim() || 'muted',
        title: title || 'Operator Guidance',
        detail: detail || 'Подсказка пока не опубликована.'
    };
}

function getPublishedReasonInsight(state) {
    const insight = state?.v2?.reasonInsight;
    if (!insight || typeof insight !== 'object') {
        return null;
    }

    const title = String(insight.title || '').trim();
    const detail = String(insight.detail || '').trim();
    const action = String(insight.action || '').trim();
    if (!title && !detail && !action) {
        return null;
    }

    return {
        tone: String(insight.tone || 'muted').trim() || 'muted',
        title: title || 'Причина процесса',
        detail: detail || 'Расшифровка причины пока не опубликована.',
        action: action || 'Ориентируйтесь на diagnostics, lifecycle и системный журнал.'
    };
}

function setReasonInsight(title, detail, action, tone = 'muted') {
    const root = document.getElementById('operator-reason-insight');
    const titleEl = document.getElementById('operator-reason-title');
    const detailEl = document.getElementById('operator-reason-detail');
    const actionEl = document.getElementById('operator-reason-action');
    if (!root || !titleEl || !detailEl || !actionEl) return;

    root.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    root.classList.add(`is-${tone}`);
    titleEl.textContent = title;
    detailEl.textContent = detail;
    actionEl.textContent = action;
}

function setFoldSummaryBadge(id, text, tone = 'muted') {
    const badge = document.getElementById(id);
    if (!badge) return;
    badge.textContent = text;
    badge.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    badge.classList.add(`is-${tone}`);
}

function updateDiagnosticsPanelState(tone = 'muted', text = 'Скрыто', options = {}) {
    setFoldSummaryBadge('monitor-diagnostics-badge', text, tone);
    setFoldSummaryBadge('mobile-diagnostics-trigger-badge', text, tone);
    const panel = document.getElementById('monitor-diagnostics-panel');
    if (!panel) return;

    const shouldShow = options.show !== undefined ? Boolean(options.show) : true;
    panel.hidden = !shouldShow;
    if (!shouldShow) {
        panel.dataset.userExpanded = '';
        return;
    }

    if (tone === 'danger' || options.forceOpen) {
        panel.dataset.autoToggle = '1';
        panel.setAttribute('open', 'open');
        return;
    }

    const userExpanded = panel.dataset.userExpanded === '1';
    if (options.forceClosed && !userExpanded) {
        panel.dataset.autoToggle = '1';
        panel.removeAttribute('open');
    }
}

function bindDiagnosticsPanelState() {
    if (diagnosticsPanelBindingsReady) return;
    diagnosticsPanelBindingsReady = true;

    const panel = document.getElementById('monitor-diagnostics-panel');
    if (!panel) return;

    panel.addEventListener('toggle', () => {
        if (panel.dataset.autoToggle === '1') {
            panel.dataset.autoToggle = '';
            return;
        }
        panel.dataset.userExpanded = panel.hasAttribute('open') ? '1' : '';
    });
}

function isMobileMonitorLayout() {
    return window.matchMedia(MOBILE_MONITOR_MEDIA).matches;
}

function rememberMobileDiagnosticsHome(id) {
    if (mobileDiagnosticsHome.has(id)) return;
    const element = document.getElementById(id);
    if (!element || !element.parentNode) return;
    mobileDiagnosticsHome.set(id, {
        parent: element.parentNode,
        nextSibling: element.nextSibling
    });
}

function restoreMobileDiagnosticsSections() {
    MOBILE_DIAGNOSTICS_SECTION_IDS.forEach((id) => {
        const element = document.getElementById(id);
        const home = mobileDiagnosticsHome.get(id);
        if (!element || !home?.parent) return;

        if (home.nextSibling && home.nextSibling.parentNode === home.parent) {
            home.parent.insertBefore(element, home.nextSibling);
        } else {
            home.parent.appendChild(element);
        }
    });
}

function syncMobileDiagnosticsLayout() {
    const modal = document.getElementById('mobile-diagnostics-modal');
    const modalContent = document.getElementById('mobile-diagnostics-modal-content');
    const mobileLayout = isMobileMonitorLayout();
    const modalOpen = Boolean(modal && modal.classList.contains('show'));

    if (!mobileLayout) {
        if (modalOpen) {
            closeMobileDiagnosticsModal();
            return;
        }
        restoreMobileDiagnosticsSections();
    }

    MOBILE_DIAGNOSTICS_SECTION_IDS.forEach((id) => {
        const element = document.getElementById(id);
        if (!element) return;

        const shouldHideInline = mobileLayout && !modalOpen;
        element.classList.toggle('is-mobile-inline-hidden', shouldHideInline);

        if (mobileLayout && modalOpen && modalContent && element.parentNode !== modalContent) {
            modalContent.appendChild(element);
        }
    });
}

function openMobileDiagnosticsModal() {
    if (!isMobileMonitorLayout()) return;

    const modal = document.getElementById('mobile-diagnostics-modal');
    const modalContent = document.getElementById('mobile-diagnostics-modal-content');
    if (!modal || !modalContent) return;

    MOBILE_DIAGNOSTICS_SECTION_IDS.forEach((id) => {
        rememberMobileDiagnosticsHome(id);
        const element = document.getElementById(id);
        if (!element) return;
        element.classList.remove('is-mobile-inline-hidden');
        modalContent.appendChild(element);
    });

    modal.classList.add('show');
    document.body.classList.add('mobile-diagnostics-open');
}

function closeMobileDiagnosticsModal() {
    const modal = document.getElementById('mobile-diagnostics-modal');
    if (!modal) return;

    modal.classList.remove('show');
    document.body.classList.remove('mobile-diagnostics-open');
    restoreMobileDiagnosticsSections();
    syncMobileDiagnosticsLayout();
}

function bindMobileDiagnosticsModal() {
    if (mobileDiagnosticsBindingsReady) return;
    mobileDiagnosticsBindingsReady = true;

    MOBILE_DIAGNOSTICS_SECTION_IDS.forEach((id) => rememberMobileDiagnosticsHome(id));
    syncMobileDiagnosticsLayout();

    const modal = document.getElementById('mobile-diagnostics-modal');
    if (modal) {
        modal.addEventListener('click', (event) => {
            if (event.target === modal) closeMobileDiagnosticsModal();
        });
    }

    window.addEventListener('resize', syncMobileDiagnosticsLayout);
    window.addEventListener('orientationchange', syncMobileDiagnosticsLayout);
    window.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') closeMobileDiagnosticsModal();
    });

    if (window.visualViewport) {
        window.visualViewport.addEventListener('resize', syncMobileDiagnosticsLayout);
    }
}

window.openMobileDiagnosticsModal = openMobileDiagnosticsModal;
window.closeMobileDiagnosticsModal = closeMobileDiagnosticsModal;

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

export function setPreflightState(title, detail, tone = 'muted', checks = {}) {
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
    setPreflightItem('runtime-preflight-profile', checks.profile?.text || '--', checks.profile?.tone || 'muted');
    setPreflightItem('runtime-preflight-takeoff', checks.takeoff?.text || '--', checks.takeoff?.tone || 'muted');
    setPreflightItem('runtime-preflight-water', checks.water?.text || '--', checks.water?.tone || 'muted');
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

function setMissionActionButtonState(buttonId, label, route = null) {
    const button = document.getElementById(buttonId);
    if (!button) return;

    button.textContent = label;
    button._missionRoute = route || null;
    button.disabled = !route;
    button.hidden = !route;
    button.title = route ? label : '';
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
    setMissionActionButtonState('operator-mission-goal-btn', 'К цели', routes.goal || null);
    setMissionActionButtonState('operator-mission-risk-btn', 'Проверить риск', routes.risk || null);
    setMissionActionButtonState('operator-mission-action-btn', 'К действию', routes.action || null);
}

function focusMissionTarget(targetId, retries = 8) {
    if (!targetId) return;
    const target = document.getElementById(targetId);
    target?.closest('details')?.setAttribute('open', 'open');
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

    ['operator-mission-goal-btn', 'operator-mission-risk-btn', 'operator-mission-action-btn'].forEach((buttonId) => {
        const button = document.getElementById(buttonId);
        if (!button) return;
        button.addEventListener('click', () => executeMissionRoute(button._missionRoute || null));
    });
}

function isMissionPlaceholder(text) {
    const value = String(text || '').trim();
    return !value || value === '--' || value === '—';
}

function isGenericMissionRisk(text) {
    const value = String(text || '').trim().toLowerCase();
    return value.startsWith('критичных');
}

function isGenericMissionAction(text) {
    const value = String(text || '').trim().toLowerCase();
    return value.startsWith('продолжать процесс');
}

function setMissionCardStateCompact(cardId, valueId, text, route = null) {
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

function setMissionCardVisibilityCompact(cardId, visible) {
    const card = document.getElementById(cardId);
    if (!card) return;
    card.hidden = !visible;
    if (!visible) {
        card._missionRoute = null;
        card.classList.remove('is-actionable');
        card.tabIndex = -1;
        card.setAttribute('role', 'group');
        card.setAttribute('aria-disabled', 'true');
        card.removeAttribute('title');
    }
}

function syncMissionLayoutCompact(root, visibleCount = 0, quiet = false) {
    if (!root) return;
    root.classList.toggle('is-quiet', Boolean(quiet));
    root.classList.toggle('has-single-card', visibleCount <= 1);
    root.classList.toggle('has-two-cards', visibleCount === 2);
}

function setMissionControlCompact(title, detail, tone = 'muted', goal = '--', risk = '--', action = '--', routes = {}, options = {}) {
    const root = document.getElementById('operator-mission-control');
    const titleEl = document.getElementById('operator-mission-title');
    const textEl = document.getElementById('operator-mission-text');
    if (!root || !titleEl || !textEl) return;

    const quiet = Boolean(options.quiet);
    const showGoal = !isMissionPlaceholder(goal);
    const showRisk = !isMissionPlaceholder(risk) && !(quiet && isGenericMissionRisk(risk));
    let showAction = !isMissionPlaceholder(action) && !(quiet && showGoal && isGenericMissionAction(action));
    if (!showGoal && !showRisk && !showAction) {
        showAction = true;
    }

    root.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    root.classList.add(`is-${tone}`);
    titleEl.textContent = title;
    textEl.textContent = detail;

    setMissionCardStateCompact('operator-mission-goal-card', 'operator-mission-goal', goal, showGoal ? (routes.goal || null) : null);
    setMissionCardStateCompact('operator-mission-risk-card', 'operator-mission-risk', risk, showRisk ? (routes.risk || null) : null);
    setMissionCardStateCompact('operator-mission-action-card', 'operator-mission-action', action, showAction ? (routes.action || null) : null);

    setMissionCardVisibilityCompact('operator-mission-goal-card', showGoal);
    setMissionCardVisibilityCompact('operator-mission-risk-card', showRisk);
    setMissionCardVisibilityCompact('operator-mission-action-card', showAction);
    syncMissionLayoutCompact(root, [showGoal, showRisk, showAction].filter(Boolean).length, quiet);

    setMissionActionButtonState('operator-mission-goal-btn', 'К цели', showGoal ? (routes.goal || null) : null);
    setMissionActionButtonState('operator-mission-risk-btn', 'Проверить риск', showRisk ? (routes.risk || null) : null);
    setMissionActionButtonState('operator-mission-action-btn', 'К действию', showAction ? (routes.action || null) : null);
}

function syncOperatorQuietPanelsCompact(state, context = {}) {
    const mode = resolveMode(state?.mode, state?.modeStr);
    const lifecycle = String(state?.v2?.lifecycle || 'idle').toLowerCase();
    const lastReasonCode = String(state?.v2?.lastReasonCode || 'RC_NONE');
    const operatorMessage = String(state?.v2?.operatorMessage || '').trim();
    const hasRuntimeItems = Boolean(context.hasRuntimeItems);
    const hasGuidanceContext = Object.prototype.hasOwnProperty.call(context, 'guidanceTone');
    const hasDiagnosticsContext = Object.prototype.hasOwnProperty.call(context, 'diagnosticsTone');
    const activeAlarm = Boolean(context.activeAlarm);
    const hasLimit = Boolean(context.hasLimit);
    const telemetryDanger = Boolean(context.telemetryDanger);
    const guidanceTone = String(context.guidanceTone || 'muted').toLowerCase();
    const diagnosticsTone = String(context.diagnosticsTone || 'muted').toLowerCase();
    const primaryVisibleCount = Math.max(0, Number(context.primaryVisibleCount) || 0);
    const secondaryVisibleCount = Math.max(0, Number(context.secondaryVisibleCount) || 0);
    const diagnosticsVisible = Boolean(context.diagnosticsVisible);

    if (hasGuidanceContext) {
        const guidanceVisible =
            activeAlarm ||
            hasLimit ||
            operatorMessage.length > 0 ||
            guidanceTone === 'warn' ||
            guidanceTone === 'danger';
        setElementHidden('operator-guidance', !guidanceVisible);
    }

    const shouldOpenRuntime =
        hasRuntimeItems ||
        mode !== MODE_IDLE ||
        lifecycle === 'starting' ||
        lifecycle === 'running' ||
        lifecycle === 'paused';

    const quietState =
        mode === MODE_IDLE &&
        lifecycle === 'idle' &&
        !activeAlarm &&
        !hasLimit &&
        !telemetryDanger &&
        operatorMessage.length === 0 &&
        (guidanceTone === 'muted' || guidanceTone === 'good') &&
        (diagnosticsTone === 'muted' || diagnosticsTone === 'good');

    const runtimeCard = document.getElementById('mode-runtime-card');
    const priorityGrid = document.querySelector('.operator-priority-grid');
    if (runtimeCard) {
        runtimeCard.hidden = quietState && !shouldOpenRuntime;
        if (runtimeCard.hidden) {
            runtimeCard.removeAttribute('open');
        } else if (shouldOpenRuntime) {
            runtimeCard.setAttribute('open', 'open');
        } else {
            runtimeCard.removeAttribute('open');
        }
        runtimeCard.classList.toggle('is-quiet', quietState && !shouldOpenRuntime);
    }
    if (priorityGrid) {
        priorityGrid.classList.toggle('is-single-card', !runtimeCard || runtimeCard.hidden);
    }

    const processCard = document.getElementById('operator-process-card');
    const processVisible = !quietState || primaryVisibleCount > 0 || secondaryVisibleCount > 0;
    if (processCard) {
        processCard.hidden = !processVisible;
        processCard.classList.toggle('is-quiet', quietState);
    }

    const secondaryPanel = document.getElementById('operator-process-secondary');
    if (secondaryPanel) {
        if (activeAlarm || hasLimit || telemetryDanger || diagnosticsTone === 'danger') {
            secondaryPanel.setAttribute('open', 'open');
        } else {
            secondaryPanel.removeAttribute('open');
        }
    }

    const stackKicker = document.querySelector('.operator-stack-kicker');
    if (stackKicker) {
        stackKicker.hidden = !diagnosticsVisible;
    }

    const diagnosticsTrigger = document.getElementById('mobile-diagnostics-trigger');
    if (diagnosticsTrigger) {
        diagnosticsTrigger.hidden = !isMobileMonitorLayout() || (!diagnosticsVisible && runtimeCard?.hidden && !processVisible);
    }

    const panelStack = document.querySelector('.operator-panel-stack');
    if (panelStack) {
        panelStack.hidden = !processVisible && !diagnosticsVisible;
    }

    if (hasDiagnosticsContext) {
        const diagnosticsMeaningful =
            activeAlarm ||
            hasLimit ||
            telemetryDanger ||
            diagnosticsTone === 'warn' ||
            diagnosticsTone === 'danger' ||
            operatorMessage.length > 0 ||
            lastReasonCode !== 'RC_NONE';

        updateDiagnosticsPanelState(diagnosticsTone, context.diagnosticsText || 'Скрыто', {
            show: diagnosticsMeaningful,
            forceOpen: diagnosticsTone === 'danger',
            forceClosed: diagnosticsTone !== 'danger'
        });
    }
}

// Keep legacy helpers referenced while the monitor layer is being migrated in-place.
const legacyMissionHelperRefs = [setMissionControl, syncOperatorQuietPanels];
void legacyMissionHelperRefs;

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

function getReasonCodeInsight(code, operatorMessage = '') {
    const normalized = String(code || 'RC_NONE');
    const label = getReasonCodeLabel(normalized);
    const message = String(operatorMessage || '').trim();

    if (message) {
        return {
            tone: normalized.startsWith('RC_SAFETY_') ? 'danger' : 'warn',
            title: label,
            detail: message,
            action: 'Сверьте это сообщение с alarm, датчиками и текущими ограничениями автоматики.'
        };
    }

    switch (normalized) {
        case 'RC_NONE':
            return {
                tone: 'muted',
                title: 'Ожидание причины',
                detail: 'Автоматика ещё не публиковала осмысленную последнюю причину или переход фазы.',
                action: 'Ничего исправлять не нужно. После первого события здесь появится расшифровка.'
            };
        case 'RC_MODE_START_REQUEST':
            return {
                tone: 'good',
                title: label,
                detail: 'Режим принят в работу и автоматика начала штатный сценарий запуска.',
                action: 'Следите за lifecycle, стабильностью и тем, что контур выходит в рабочее окно без ограничений.'
            };
        case 'RC_MODE_STOP_REQUEST':
        case 'RC_MANUAL_OPERATOR_STOP':
            return {
                tone: 'warn',
                title: label,
                detail: 'Текущий сценарий был остановлен оператором или переведён к завершению вручную.',
                action: 'Проверьте, что нагрев, насос и отбор действительно свернулись в безопасное состояние.'
            };
        case 'RC_PRECHECK_OK':
            return {
                tone: 'good',
                title: label,
                detail: 'Предпусковые условия были валидны: датчики, safety и базовая телеметрия выглядели рабочими.',
                action: 'Можно использовать это как ориентир для следующего старта аналогичного режима.'
            };
        case 'RC_PRECHECK_FAIL_SENSOR':
        case 'RC_SAFETY_TRIP_SENSOR':
            return {
                tone: 'danger',
                title: label,
                detail: 'Автоматика потеряла доверие к температурным данным или свежести телеметрии.',
                action: 'Проверьте датчики, шину 1-Wire/I2C, обновление статуса и не продолжайте процесс вслепую.'
            };
        case 'RC_PRECHECK_FAIL_SAFETY_LATCH':
            return {
                tone: 'danger',
                title: label,
                detail: 'Перед запуском уже был активен safety latch, поэтому старт заблокирован на стороне контроллера.',
                action: 'Сначала разберите причину trip, затем подтвердите или сбросьте аварийное состояние.'
            };
        case 'RC_SAFETY_LIMIT_POWER':
            return {
                tone: 'warn',
                title: label,
                detail: 'Safety supervisor уже принудительно ограничивает нагрев из-за риска по процессу.',
                action: 'Проверьте охлаждение, давление, TSA и не наращивайте мощность, пока ограничение не исчезнет.'
            };
        case 'RC_SAFETY_LIMIT_TAKEOFF':
            return {
                tone: 'warn',
                title: label,
                detail: 'Автоматика временно запрещает или душит отбор, потому что колонна не выглядит достаточно стабильной.',
                action: 'Дождитесь рабочего окна по stability, pressure и cooling margin, не открывайте отбор вручную.'
            };
        case 'RC_SAFETY_PHASE_BLOCKED':
            return {
                tone: 'warn',
                title: label,
                detail: 'Переход к следующей фазе был задержан защитной логикой, потому что условия ещё неубедительны.',
                action: 'Смотрите diagnostics и guidance: сначала нужно снять ограничение, а не форсировать фазу.'
            };
        case 'RC_SAFETY_RECOVERY_ENTERED':
        case 'RC_PHASE_RECOVERY_APPLIED':
            return {
                tone: 'warn',
                title: label,
                detail: 'Система вошла в recovery или восстановила фазу после нестабильного участка процесса.',
                action: 'Дайте колонне заново стабилизироваться и не делайте резких изменений нагрева, воды и отбора.'
            };
        case 'RC_SAFETY_RECOVERY_EXITED':
            return {
                tone: 'good',
                title: label,
                detail: 'Recovery завершён, автоматика считает, что система вернулась в рабочее состояние.',
                action: 'Проверьте, что показатели действительно ровные, и только потом возвращайтесь к обычной нагрузке.'
            };
        case 'RC_SAFETY_TRIP_PRESSURE':
            return {
                tone: 'danger',
                title: label,
                detail: 'Процесс был аварийно ограничен или остановлен из-за опасного давления.',
                action: 'Проверьте засоры, захлёб, клапаны, холодильник и не перезапускайте процесс до нормализации.'
            };
        case 'RC_SAFETY_TRIP_OVERHEAT':
            return {
                tone: 'danger',
                title: label,
                detail: 'Автоматика увидела перегрев по критическим температурным каналам.',
                action: 'Проверьте воду, TSA, дефлегматор и фактическую тепловую нагрузку перед продолжением.'
            };
        case 'RC_SAFETY_TRIP_POWER':
            return {
                tone: 'danger',
                title: label,
                detail: 'Защита сработала из-за питания, мощности или связанного с ними аномального поведения нагрузки.',
                action: 'Проверьте SSR, сеть, PZEM и реальную подачу мощности на нагрев.'
            };
        case 'RC_SAFETY_TRIP_GENERIC':
            return {
                tone: 'danger',
                title: label,
                detail: 'Сработала общая safety-авария без более узкой классификации.',
                action: 'Сверьте журнал, alarm и последние transition-события, прежде чем возвращать процесс в работу.'
            };
        case 'RC_HEADS_VOLUME_REACHED':
        case 'RC_HEADS_SCORE_REACHED':
            return {
                tone: 'good',
                title: label,
                detail: 'Этап голов считается завершённым: либо по объёму, либо по индикаторам качества перехода.',
                action: 'Проверьте, что колонна готова к следующей фазе, и контролируйте качество входа в тело.'
            };
        case 'RC_BODY_END_DETECTED':
        case 'RC_BODY_TARGET_VOLUME_REACHED':
            return {
                tone: 'warn',
                title: label,
                detail: 'Автоматика считает, что основной отбор тела закончен по цели или признакам завершения.',
                action: 'Сверьте продукт, верха колонны и body score, чтобы подтвердить корректность перехода.'
            };
        case 'RC_TAILS_TARGET_REACHED':
        case 'RC_FINISH_COOLDOWN_COMPLETE':
            return {
                tone: 'good',
                title: label,
                detail: 'Сценарий дошёл до хвостовой или финишной части и штатно завершает цикл.',
                action: 'Оцените результат прогона и подготовьте историю/отчёт для следующего запуска.'
            };
        case 'RC_DISTILLATION_END_TEMP_REACHED':
        case 'RC_DISTILLATION_TARGET_VOLUME_REACHED':
            return {
                tone: 'good',
                title: label,
                detail: 'Дистилляция дошла до заданного технологического финиша по температуре или объёму.',
                action: 'Сверьте фактический выход и энергозатраты с целями профиля.'
            };
        case 'RC_NBK_STEAM_READY':
        case 'RC_NBK_STABILIZATION_COMPLETE':
        case 'RC_NBK_FEED_ENABLED':
            return {
                tone: 'good',
                title: label,
                detail: 'НБК проходит ключевые точки готовности и автоматика разрешает следующий рабочий этап.',
                action: 'Контролируйте пар, подачу браги и давление без резких изменений нагрузки.'
            };
        case 'RC_NBK_FINISH_LIKELY':
            return {
                tone: 'warn',
                title: label,
                detail: 'Автоматика видит признаки приближения к финалу НБК и снижает уверенность в обычном режиме работы.',
                action: 'Подготовьтесь к завершению и внимательнее следите за низом колонны, давлением и подачей.'
            };
        case 'RC_TEMP_STEP_REACHED':
        case 'RC_TEMP_STEP_HOLD_COMPLETE':
        case 'RC_TEMP_STEP_TIMEOUT':
            return {
                tone: normalized === 'RC_TEMP_STEP_TIMEOUT' ? 'warn' : 'good',
                title: label,
                detail: normalized === 'RC_TEMP_STEP_TIMEOUT'
                    ? 'Температурный шаг завершён по таймауту, а не по идеальному выполнению профиля.'
                    : 'Температурный шаг профиля отработан и автоматика готова двигаться дальше.',
                action: 'Проверьте, насколько фактический нагрев совпал с профилем, особенно если это был таймаут.'
            };
        case 'RC_FERM_TARGET_REACHED':
            return {
                tone: 'good',
                title: label,
                detail: 'Контур брожения вышел в заданный температурный диапазон и удерживает цель.',
                action: 'Продолжайте наблюдать за длительными отклонениями и стабильностью температуры среды.'
            };
        case 'RC_OPERATOR_SERVICE_ACTION':
        case 'RC_MANUAL_OPERATOR_SWITCH':
            return {
                tone: 'warn',
                title: label,
                detail: 'Состояние процесса менялось из-за сервисного или ручного действия оператора, а не по чистой автоматике.',
                action: 'Учитывайте это при разборе истории: сравнивать такой участок с эталонным автопрогоном нужно осторожно.'
            };
        case 'RC_PHASE_TRANSITION_INFERRED':
            return {
                tone: 'warn',
                title: label,
                detail: 'Переход фазы пришлось восстановить аналитически по текущему состоянию процесса.',
                action: 'Проверьте историю переходов: желательно понять, почему явный transition был пропущен.'
            };
        default:
            return {
                tone: 'muted',
                title: label,
                detail: 'Причина зафиксирована, но для неё ещё нет отдельной операторской расшифровки.',
                action: 'Ориентируйтесь на lifecycle, guidance, diagnostics и системный журнал для детального разбора.'
            };
    }
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
    if (activeLimits.antiOscillationActive) labels.push('антидребезг');
    if (Boolean(indicators.powerLimited) || Boolean(activeLimits.powerCapped)) labels.push('мощность');
    if (activeLimits.takeoffBlocked) labels.push('отбор');
    if (activeLimits.phaseAdvanceBlocked) labels.push('фаза');
    if (activeLimits.pumpCapped) labels.push('насос');
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
            risk: { tabId: 'control', targetId: 'mode-start-status' },
            action: { tabId: 'control', targetId: 'mode-start-button' }
        };
    }

    if (state?.currentAlarm?.active || state?.v2?.safetyLatched || lifecycle === 'faulted') {
        return {
            goal: { tabId: 'control', targetId: 'mode-start-status' },
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

export function renderProcessIndicatorsCard() {
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
    const telemetryCoverage = toFinite(indicators.telemetryCoverage, -1);
    const decisionTrust = toFinite(indicators.decisionTrust, -1);

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
    setIndicatorValue('indicator-telemetry-coverage', formatConfidencePercent(telemetryCoverage), getIndicatorPercentTone(telemetryCoverage, 0.7, 0.9));
    setIndicatorValue('indicator-decision-trust', formatConfidencePercent(decisionTrust), getIndicatorPercentTone(decisionTrust, 0.55, 0.8));
    setIndicatorValue('indicator-degraded-mode', indicators.degradedModeActive ? 'Активен' : 'Нет', indicators.degradedModeActive ? 'warn' : 'good');
    setIndicatorValue('indicator-adaptive-control', indicators.adaptiveControlAllowed ? 'Разрешён' : 'Ограничен', indicators.adaptiveControlAllowed ? 'good' : 'warn');

    const pressureStable = boolLabel(indicators.pressureStable, 'Стабильно', 'Дрейф');
    setIndicatorValue('indicator-pressure-stable', pressureStable.text, pressureStable.tone);
    renderPressureIndicators(s, indicators);

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
    const guidance = getPublishedGuidance(s) || buildGuidance(s, indicators, activeLimits);
    setGuidance(guidance.title, guidance.detail, guidance.tone);
}

function renderProcessIndicatorsPanel() {
    const s = runtimeMonitorState;
    const indicators = s?.v2?.indicators || {};
    const activeLimits = s?.v2?.activeLimits || {};
    const mode = resolveMode(s.mode, s.modeStr);

    const lifecycle = String(s?.v2?.lifecycle || 'idle');
    const lastReasonCode = String(s?.v2?.lastReasonCode || 'RC_NONE');
    const operatorMessage = String(s?.v2?.operatorMessage || '').trim();
    const coolingMargin = toFinite(indicators.coolingMarginC, 0);
    const stability = toFinite(indicators.stabilityIndex, 0);
    const floodRisk = toFinite(indicators.floodRisk, 0);
    const processHealth = toFinite(indicators.processHealth, 0);
    const headsScore = toFinite(indicators.headsCompletionScore, 0);
    const bodyScore = toFinite(indicators.bodyEndScore, 0);
    const activeAlarm = Boolean(s?.currentAlarm?.active);
    const telemetryCoverage = toFinite(indicators.telemetryCoverage, -1);
    const decisionTrust = toFinite(indicators.decisionTrust, -1);
    const takeoffConfidence = toFinite(indicators.takeoffConfidence, -1);
    const headsEndConfidence = toFinite(indicators.headsEndConfidence, -1);
    const bodyEndConfidence = toFinite(indicators.bodyEndConfidence, -1);
    const tailsTransitionConfidence = toFinite(indicators.tailsTransitionConfidence, -1);
    const powerLimitConfidence = toFinite(indicators.powerLimitConfidence, 0);
    const hasLimit =
        Boolean(indicators.powerLimited) ||
        Boolean(activeLimits.powerCapped) ||
        Boolean(activeLimits.takeoffBlocked) ||
        Boolean(activeLimits.phaseAdvanceBlocked) ||
        Boolean(activeLimits.pumpCapped);
    const pressureAvailability = getPressureAvailability(s, indicators);
    const coolingRelevant = isCoolingMetricsRelevant(mode, indicators);
    const quietIdle = mode === MODE_IDLE && lifecycle === 'idle' && !activeAlarm && !hasLimit;

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
    setIndicatorValue('indicator-telemetry-coverage', formatConfidencePercent(telemetryCoverage), getIndicatorPercentTone(telemetryCoverage, 0.7, 0.9));
    setIndicatorValue('indicator-decision-trust', formatConfidencePercent(decisionTrust), getIndicatorPercentTone(decisionTrust, 0.55, 0.8));
    setIndicatorValue('indicator-degraded-mode', indicators.degradedModeActive ? 'Активен' : 'Нет', indicators.degradedModeActive ? 'warn' : 'good');
    setIndicatorValue('indicator-adaptive-control', indicators.adaptiveControlAllowed ? 'Разрешён' : 'Ограничен', indicators.adaptiveControlAllowed ? 'good' : 'warn');

    const pressureStable = boolLabel(indicators.pressureStable, 'Стабильно', 'Дрейф');
    setIndicatorValue('indicator-pressure-stable', pressureStable.text, pressureStable.tone);
    renderPressureIndicators(s, indicators);

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

    const showTakeoffConfidence =
        mode === MODE_RECT || mode === MODE_MANUAL || mode === MODE_DIST || mode === MODE_NBK;
    const showHeadsConfidence =
        mode === MODE_RECT || mode === MODE_MANUAL || mode === MODE_DIST;
    const showBodyConfidence =
        mode === MODE_RECT || mode === MODE_MANUAL || mode === MODE_DIST;
    const showTailsConfidence =
        mode === MODE_RECT || mode === MODE_MANUAL || mode === MODE_DIST || mode === MODE_NBK;

    setIndicatorValue(
        'indicator-confidence-takeoff',
        showTakeoffConfidence ? formatConfidencePercent(takeoffConfidence) : '—',
        showTakeoffConfidence ? getConfidenceTone(takeoffConfidence) : 'muted'
    );
    setIndicatorValue(
        'indicator-confidence-heads-end',
        showHeadsConfidence ? formatConfidencePercent(headsEndConfidence) : '—',
        showHeadsConfidence ? getConfidenceTone(headsEndConfidence) : 'muted'
    );
    setIndicatorValue(
        'indicator-confidence-body-end',
        showBodyConfidence ? formatConfidencePercent(bodyEndConfidence) : '—',
        showBodyConfidence ? getConfidenceTone(bodyEndConfidence) : 'muted'
    );
    setIndicatorValue(
        'indicator-confidence-tails',
        showTailsConfidence ? formatConfidencePercent(tailsTransitionConfidence) : '—',
        showTailsConfidence ? getConfidenceTone(tailsTransitionConfidence) : 'muted'
    );
    setIndicatorValue(
        'indicator-confidence-power-limit',
        formatConfidencePercent(powerLimitConfidence),
        getConfidenceTone(powerLimitConfidence, true)
    );

    const showPressureMarginCard = pressureAvailability.signalAvailable;
    const showCoolingCard = coolingRelevant;
    const showLifecycleCard =
        mode !== MODE_IDLE ||
        lifecycle !== 'idle' ||
        activeAlarm ||
        hasLimit ||
        operatorMessage.length > 0;
    const showTakeoffCard =
        mode === MODE_RECT ||
        mode === MODE_MANUAL ||
        mode === MODE_DIST ||
        mode === MODE_NBK ||
        (hasLimit && Boolean(indicators.takeoffAllowed) === false);
    const showStabilityCard = !quietIdle || stability < 0.999 || activeAlarm || hasLimit;
    const showFloodRiskCard =
        mode === MODE_RECT ||
        mode === MODE_MANUAL ||
        mode === MODE_NBK ||
        floodRisk > 0.001;
    const showCoolingMarginCard = coolingRelevant;
    const showProcessHealthCard = !quietIdle || processHealth < 0.999 || activeAlarm || hasLimit;
    const showPressureCubeCard = pressureAvailability.hardwareAvailable;
    const showRecoveryCard = Boolean(indicators.recoveryActive);

    setElementHidden('operator-stat-lifecycle-card', !showLifecycleCard);
    setElementHidden('operator-stat-takeoff-card', !showTakeoffCard);
    setElementHidden('operator-stat-pressure-margin-card', !showPressureMarginCard);
    setElementHidden('operator-stat-cooling-card', !showCoolingCard);
    setElementHidden('operator-secondary-stability-card', !showStabilityCard);
    setElementHidden('operator-secondary-flood-risk-card', !showFloodRiskCard);
    setElementHidden('operator-secondary-cooling-margin-card', !showCoolingMarginCard);
    setElementHidden('operator-secondary-process-health-card', !showProcessHealthCard);
    setElementHidden('operator-secondary-pressure-cube-card', !showPressureCubeCard);
    setElementHidden('operator-secondary-recovery-card', !showRecoveryCard);

    const secondaryVisibleCount = [
        showStabilityCard,
        showFloodRiskCard,
        showCoolingMarginCard,
        showProcessHealthCard,
        showPressureCubeCard,
        showRecoveryCard
    ].filter(Boolean).length;
    const primaryVisibleCount = [
        showLifecycleCard,
        showTakeoffCard,
        showCoolingCard,
        showPressureMarginCard
    ].filter(Boolean).length;

    const guidance = getPublishedGuidance(s) || buildGuidance(s, indicators, activeLimits);
    setGuidance(guidance.title, guidance.detail, guidance.tone);
    const reasonInsight = getPublishedReasonInsight(s) || getReasonCodeInsight(lastReasonCode, operatorMessage);
    setReasonInsight(reasonInsight.title, reasonInsight.detail, reasonInsight.action, reasonInsight.tone);

    let diagnosticsTone = 'good';
    let diagnosticsText = 'Фон';
    if (activeAlarm || lifecycle === 'faulted' || freshness.tone === 'danger') {
        diagnosticsTone = 'danger';
        diagnosticsText = 'Требует внимания';
    } else if (hasLimit || processHealth < 0.85 || decisionTrust < 0.8) {
        diagnosticsTone = 'warn';
        diagnosticsText = 'Есть ограничения';
    }

    const showLastReasonRow = lastReasonCode !== 'RC_NONE' || operatorMessage.length > 0;
    const showTelemetryRow = telemetryCoverage >= 0 && (!quietIdle || telemetryCoverage < 0.999 || activeAlarm || hasLimit);
    const showDecisionRow = decisionTrust >= 0 && (!quietIdle || decisionTrust < 0.999 || activeAlarm || hasLimit);
    const showDegradedRow = Boolean(indicators.degradedModeActive);
    const showAdaptiveRow =
        mode !== MODE_IDLE &&
        (
            Boolean(indicators.adaptiveControlAllowed) ||
            Boolean(indicators.columnSensorsAvailable) ||
            Boolean(indicators.coolingSensorAvailable) ||
            Boolean(indicators.degradedModeActive)
        );
    const showFreshnessRow = mode !== MODE_IDLE || !indicators.sensorFreshnessOk;
    const showPressureRow = pressureAvailability.hardwareAvailable;
    const showPressureRateRow = pressureAvailability.signalAvailable;
    const showPressureMarginRow = pressureAvailability.signalAvailable;
    const showPressureStableRow = pressureAvailability.signalAvailable;
    const showHeadsScoreRow = showHeadsConfidence && (!quietIdle || headsScore > 0.001);
    const showBodyScoreRow = showBodyConfidence && (!quietIdle || bodyScore > 0.001);
    const showPowerLimitRow = hasLimit;
    const showTakeoffConfidenceRow = showTakeoffConfidence && hasMeaningfulConfidence(takeoffConfidence);
    const showHeadsConfidenceRow = showHeadsConfidence && hasMeaningfulConfidence(headsEndConfidence);
    const showBodyConfidenceRow = showBodyConfidence && hasMeaningfulConfidence(bodyEndConfidence);
    const showTailsConfidenceRow = showTailsConfidence && hasMeaningfulConfidence(tailsTransitionConfidence);
    const showPowerLimitConfidenceRow = hasLimit || powerLimitConfidence > 0.001;
    const showReasonInsight = showLastReasonRow || activeAlarm || hasLimit;
    const diagnosticsVisible =
        showLastReasonRow ||
        showTelemetryRow ||
        showDecisionRow ||
        showDegradedRow ||
        showAdaptiveRow ||
        showFreshnessRow ||
        showPressureRow ||
        showPressureRateRow ||
        showPressureMarginRow ||
        showPressureStableRow ||
        showHeadsScoreRow ||
        showBodyScoreRow ||
        showPowerLimitRow ||
        showTakeoffConfidenceRow ||
        showHeadsConfidenceRow ||
        showBodyConfidenceRow ||
        showTailsConfidenceRow ||
        showPowerLimitConfidenceRow ||
        showReasonInsight;

    setElementHidden('operator-diag-last-reason-row', !showLastReasonRow);
    setElementHidden('operator-diag-telemetry-row', !showTelemetryRow);
    setElementHidden('operator-diag-decision-row', !showDecisionRow);
    setElementHidden('operator-diag-degraded-row', !showDegradedRow);
    setElementHidden('operator-diag-adaptive-row', !showAdaptiveRow);
    setElementHidden('operator-diag-freshness-row', !showFreshnessRow);
    setElementHidden('operator-diag-pressure-row', !showPressureRow);
    setElementHidden('operator-diag-pressure-rate-row', !showPressureRateRow);
    setElementHidden('operator-diag-pressure-margin-row', !showPressureMarginRow);
    setElementHidden('operator-diag-pressure-stable-row', !showPressureStableRow);
    setElementHidden('operator-diag-heads-score-row', !showHeadsScoreRow);
    setElementHidden('operator-diag-body-score-row', !showBodyScoreRow);
    setElementHidden('operator-diag-power-limit-row', !showPowerLimitRow);
    setElementHidden('operator-diag-confidence-takeoff-row', !showTakeoffConfidenceRow);
    setElementHidden('operator-diag-confidence-heads-row', !showHeadsConfidenceRow);
    setElementHidden('operator-diag-confidence-body-row', !showBodyConfidenceRow);
    setElementHidden('operator-diag-confidence-tails-row', !showTailsConfidenceRow);
    setElementHidden('operator-diag-confidence-power-limit-row', !showPowerLimitConfidenceRow);
    setElementHidden('operator-reason-insight', !showReasonInsight);

    const secondaryPanel = document.getElementById('operator-process-secondary');
    if (secondaryPanel) {
        secondaryPanel.hidden = secondaryVisibleCount === 0;
        if (secondaryVisibleCount === 0) {
            secondaryPanel.removeAttribute('open');
        }
    }

    syncOperatorQuietPanelsCompact(s, {
        guidanceTone: guidance.tone,
        diagnosticsTone,
        diagnosticsText,
        hasLimit,
        activeAlarm,
        telemetryDanger: freshness.tone === 'danger',
        primaryVisibleCount,
        secondaryVisibleCount,
        diagnosticsVisible
    });
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
    const measuredPower = Math.max(0, toFinite(s.power.power, 0));
    const setPower = Math.max(0, toFinite(s.power.setW, measuredPower));

    const rectPowerEl = document.getElementById('rect-power-display');
    const powerEl = document.getElementById('manual-power-display');
    const speedEl = document.getElementById('manual-speed-display');
    const waterAutoStartEl = document.getElementById('water-autostart-display');
    const headsEl = document.getElementById('manual-heads-display');
    const bodyEl = document.getElementById('manual-body-display');
    const tailsEl = document.getElementById('manual-tails-display');

    if (rectPowerEl) rectPowerEl.textContent = `${setPower.toFixed(0)} Вт`;
    if (powerEl) powerEl.textContent = `${setPower.toFixed(0)} Вт`;
    if (speedEl) speedEl.textContent = `${toFinite(s.pump.speedMlH, 0).toFixed(0)} мл/ч`;
    if (waterAutoStartEl) waterAutoStartEl.textContent = `${toFinite(s.equipment.waterAutoStartCubeTempC, 45).toFixed(1)} °C`;
    if (headsEl) headsEl.textContent = `${toFinite(s.volumes.heads, 0).toFixed(0)} мл`;
    if (bodyEl) bodyEl.textContent = `${toFinite(s.volumes.body, 0).toFixed(0)} мл`;
    if (tailsEl) tailsEl.textContent = `${toFinite(s.volumes.tails, 0).toFixed(0)} мл`;
}

function formatFractionProgramCriterion(fractionProgram = {}) {
    const conditions = Math.round(toFinite(fractionProgram.endConditions, 0));
    const parts = [];

    if (conditions & FRACTION_PROGRAM_END_VOLUME) {
        const volumeMl = Math.max(0, toFinite(fractionProgram.endVolumeMl, 0));
        if (volumeMl > 0) parts.push(`до ${volumeMl.toFixed(0)} мл`);
    }

    if (conditions & FRACTION_PROGRAM_END_TIME) {
        const durationSec = Math.max(0, toFinite(fractionProgram.endDurationSec, 0));
        if (durationSec > 0) parts.push(`до ${formatDurationSafe(durationSec)}`);
    }

    if (conditions & FRACTION_PROGRAM_END_TEMPERATURE) {
        const sensorIndex = Math.max(0, Math.round(toFinite(fractionProgram.temperatureSensorIndex, 0)));
        const temperatureC = toFinite(fractionProgram.endTemperatureC, 0);
        if (temperatureC > 0) parts.push(`до T${sensorIndex} ${temperatureC.toFixed(1)}°C`);
    }

    if (conditions & FRACTION_PROGRAM_END_LEVEL) {
        parts.push('до уровня');
    }

    if (!parts.length && fractionProgram.allowManualAdvance) {
        parts.push('ручной переход');
    }

    return parts.join(' • ');
}

function renderDistillationRuntimeActions(container, state, fractionProgram) {
    if (!container) return;

    const paused = Boolean(state.paused);
    const idle = resolveMode(state.mode, state.modeStr) === MODE_IDLE;
    const canAdvance = Boolean(fractionProgram?.active) &&
        Boolean(fractionProgram?.allowManualAdvance) &&
        !Boolean(fractionProgram?.waitingForConfirmation);

    container.innerHTML = `
        <button class="btn ${paused ? 'btn-success' : 'btn-warning'}" type="button" onclick="${paused ? 'resumeProcess()' : 'pauseProcess()'}" data-runtime-action="${paused ? 'resume' : 'pause'}" ${idle ? 'disabled' : ''}>
            ${paused ? 'Продолжить' : 'Пауза'}
        </button>
        <button class="btn btn-secondary" type="button" onclick="requestFractionProgramNext()" data-runtime-action="next-fraction" ${canAdvance ? '' : 'disabled'}>
            Следующая фракция
        </button>
        <button class="btn btn-danger" type="button" onclick="stopProcess()" data-runtime-action="stop" ${idle ? 'disabled' : ''}>
            Остановить
        </button>
    `;
    container.style.display = 'grid';
}

export function renderModeRuntimeCard() {
    const titleEl = document.getElementById('mode-runtime-title');
    const captionEl = document.getElementById('mode-runtime-caption');
    const manualEl = document.getElementById('mode-runtime-manual');
    if (!titleEl || !captionEl) return;
    if (manualEl && !manualEl.dataset.manualTemplate) {
        manualEl.dataset.manualTemplate = manualEl.innerHTML;
    }

    const s = runtimeMonitorState;
    const mode = resolveMode(s.mode, s.modeStr);
    const phase = toFinite(s.phase, 0);
    const items = [];

    if (mode === MODE_RECT) {
        titleEl.textContent = 'Прогресс авто-ректификации';
        const effectiveAbv = getEffectiveAbvForCalculations();
        const abvSourceText = effectiveAbv.source === 'sensor' ? 'датчик' : 'план';
        const backendType = Math.round(toFinite(s.rectification.takeoffBackendType, 0));
        const backendLabel = getRectTakeoffBackendLabel(backendType);
        const routingReady = Boolean(s.rectification.takeoffRoutingReady ?? true);
        const activeFraction = Math.round(toFinite(s.rectification.takeoffActiveFraction, 0));
        const requestedFraction = Math.round(toFinite(s.rectification.takeoffRequestedFraction, 0));
        const actualRate = Math.max(0, toFinite(s.rectification.takeoffActualEquivalentRateMlH, 0));
        const activeFractionLabel = getRectTakeoffFractionLabel(
            activeFraction || requestedFraction,
            'отбор закрыт'
        );
        captionEl.textContent = `Фаза: ${s.phaseStr || phase || '-'} • крепость расчета ${effectiveAbv.value.toFixed(1)}% (${abvSourceText}) • ${backendLabel} • ${routingReady ? 'маршрут готов' : 'маршрут переключается'} • ${activeFractionLabel}${actualRate > 0 ? ` • ${actualRate.toFixed(0)} мл/ч` : ''}`;

        const est = estimateRectTargets(s.rectification, effectiveAbv.value);
        const effectiveEquipment = {
            ...s.equipment,
            heaterPowerW: Math.max(0, toFinite(s.equipment.heaterPowerW, maxHeaterPower))
        };
        const rectMode = Math.round(toFinite(s.rectification.refluxMode, 0));
        const directHeadsSpeed = getRectificationTakeoffRateMlH(
            s.rectification.headsSpeedMlHKw,
            effectiveEquipment
        );
        const directBodySpeed = getRectificationTakeoffRateMlH(
            s.rectification.bodySpeedMlHKw,
            effectiveEquipment
        );
        const rectDuty = (() => {
            if (rectMode === 1) {
                const ratio = Math.max(0, toFinite(s.rectification.srRatio, 0));
                return ratio <= 0 ? 0 : (1 / (ratio + 1));
            }
            if (rectMode === 2) {
                const cycle = Math.max(1, toFinite(s.rectification.autonomousCycleSec, 900));
                const pause = Math.max(0, Math.min(cycle - 1, toFinite(s.rectification.autonomousPauseSec, 90)));
                return Math.max(0, (cycle - pause) / cycle);
            }
            return 1;
        })();
        const headsSpeed = rectMode === 0 ? directHeadsSpeed : directHeadsSpeed * rectDuty;
        const bodySpeed = rectMode === 0 ? directBodySpeed : directBodySpeed * rectDuty;
        const tailsSpeed = Math.max(0, bodySpeed / 2);
        const targetHeads = toFinite(s.rectification.headsTargetMl, 0) > 0 ? toFinite(s.rectification.headsTargetMl, 0) : est.heads;
        const targetBody = toFinite(s.rectification.bodyTargetMl, 0) > 0 ? toFinite(s.rectification.bodyTargetMl, 0) : est.body;
        const targetTails = toFinite(s.rectification.tailsTargetMl, 0) > 0 ? toFinite(s.rectification.tailsTargetMl, 0) : est.tails;

        [
            { key: 'heads', label: 'Головы', target: targetHeads, speed: headsSpeed, value: toFinite(s.volumes.heads, 0), pending: phase < PHASE_HEADS },
            { key: 'body', label: 'Тело', target: targetBody, speed: bodySpeed, value: toFinite(s.volumes.body, 0), pending: phase < PHASE_BODY || phase === PHASE_POST_HEADS_STAB },
            { key: 'tails', label: 'Хвосты', target: targetTails, speed: tailsSpeed, value: toFinite(s.volumes.tails, 0), pending: phase < PHASE_TAILS }
        ].forEach((part) => {
            const partFractionId = part.key === 'heads' ? 1 : part.key === 'body' ? 2 : 3;
            const target = Math.max(0, part.target);
            const value = Math.max(0, part.value);
            const effectiveSpeed =
                partFractionId === activeFraction && actualRate > 0
                    ? actualRate
                    : part.speed;
            const pct = target > 0 ? clampPercent((value / target) * 100) : 0;
            const remMl = Math.max(0, target - value);
            const remSec = (effectiveSpeed > 0 && remMl > 0)
                ? (remMl / effectiveSpeed) * 3600
                : 0;
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
        const fractionProgram = s.fractionProgram && typeof s.fractionProgram === 'object' ? s.fractionProgram : null;
        const fractionCollected = Math.max(0, toFinite(fractionProgram?.collectedMl, 0));
        const fractionRate = Math.max(0, toFinite(fractionProgram?.actualRateMlH, 0));
        const requestedRoute = Math.round(toFinite(fractionProgram?.requestedRoute, 0));
        const routedRoute = Math.round(toFinite(fractionProgram?.routedRoute, 0));
        if (fractionProgram) {
            const fractionStep = Math.max(0, Math.round(toFinite(fractionProgram.currentStep, 0))) + 1;
            const criterion = formatFractionProgramCriterion(fractionProgram);
            const stepName = String(fractionProgram.stepName || '').trim();
            const routeText = `Маршрут ${requestedRoute} → ${routedRoute}`;
            items.push({
                label: stepName ? `Фракция ${fractionStep}: ${stepName}` : `Фракция ${fractionStep}`,
                percent: 0,
                primary: criterion ? `${routeText} • ${criterion}` : routeText,
                metaLeft: `${fractionCollected.toFixed(0)} мл`,
                metaRight: fractionRate > 0 ? `${fractionRate.toFixed(0)} мл/ч` : 'ожидание',
                stateClass: fractionProgram.waitingForConfirmation ? 'is-waiting' : ''
            });
        }
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

        const backendType = Math.round(toFinite(s.rectification.takeoffBackendType, 0));
        const backendLabel = backendType === 1
            ? '3 клапана'
            : backendType === 2
                ? '1 клапан + переключение'
                : 'насос';
        const routingReady = Boolean(s.rectification.takeoffRoutingReady ?? true);
        const activeFraction = Math.round(toFinite(s.rectification.takeoffActiveFraction, 0));
        const activeFractionLabel = activeFraction === 1
            ? 'головы'
            : activeFraction === 2
                ? 'тело'
                : activeFraction === 3
                    ? 'хвосты'
                    : 'отбор закрыт';
        captionEl.textContent = `Отбор: ${backendLabel} • ${routingReady ? 'маршрут готов' : 'маршрут переключается'} • ${activeFractionLabel}`;

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
        captionEl.textContent = 'После запуска здесь появятся фаза, остаток и рабочие шаги процесса.';
    }

    const indicators = s?.v2?.indicators || {};
    const activeLimits = s?.v2?.activeLimits || {};
    const mission = buildMissionSnapshot(s, indicators, activeLimits);
    const missionRoutes = buildMissionRoutes(s, indicators, activeLimits);
    const missionQuiet =
        mode === MODE_IDLE &&
        String(s?.v2?.lifecycle || 'idle').toLowerCase() === 'idle' &&
        mission.tone !== 'warn' &&
        mission.tone !== 'danger';
    const preflight = getRuntimePreflightState(s);
    setMissionControlCompact(
        mission.title,
        mission.detail,
        mission.tone,
        mission.goal,
        mission.risk,
        mission.action,
        missionRoutes,
        { quiet: missionQuiet }
    );
    setPreflightState(preflight.title, preflight.detail, preflight.tone, preflight.checks);
    renderRuntimeBars(items);
    if (manualEl && mode === MODE_MANUAL && manualEl.dataset.manualTemplate && manualEl.innerHTML !== manualEl.dataset.manualTemplate) {
        manualEl.innerHTML = manualEl.dataset.manualTemplate;
    }
    updateManualTiles();
    renderProcessIndicatorsPanel();
    syncOperatorQuietPanelsCompact(s, {
        hasRuntimeItems: items.length > 0
    });
    if (manualEl) {
        if (mode === MODE_MANUAL) {
            manualEl.style.display = 'grid';
        } else if (mode === MODE_DIST) {
            renderDistillationRuntimeActions(
                manualEl,
                s,
                s.fractionProgram && typeof s.fractionProgram === 'object' ? s.fractionProgram : null
            );
        } else {
            if (manualEl.dataset.manualTemplate && manualEl.innerHTML !== manualEl.dataset.manualTemplate) {
                manualEl.innerHTML = manualEl.dataset.manualTemplate;
            }
            manualEl.style.display = 'none';
        }
    }
    const rectEl = document.getElementById('mode-runtime-rect');
    if (rectEl) {
        rectEl.style.display = mode === MODE_RECT ? 'grid' : 'none';
        if (mode === MODE_RECT) {
            renderRectTakeoffDetails(rectEl, s.rectification);
        } else {
            rectEl.innerHTML = '';
        }
    }
}

export function initRuntimeMonitorUi() {
    ensureMissionControlBindings();
    bindDiagnosticsPanelState();
    bindMobileDiagnosticsModal();
    syncMobileDiagnosticsLayout();
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
