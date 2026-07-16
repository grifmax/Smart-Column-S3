import {
    currentMode,
    MODE_IDLE,
    maxHeaterPower,
    runtimeMonitorState,
    MODE_RECT,
    MODE_MANUAL,
    MODE_DIST,
    MODE_MASH,
    MODE_HOLD,
    MODE_NBK,
    MODE_FERMENTATION,
    clampFeedVolumeToCube,
    getCubeVolumeLimitL
} from '../globals.js';
import { addLog } from '../core/logs.js';
import { loadStatus } from '../core/status.js';
import { getStartAvailabilityState, setPreflightState } from '../runtime/bars.js';
import { getEffectiveAbvForCalculations } from '../runtime/abv.js';
import { estimateRectTargets, getRectificationTakeoffRateMlH } from '../runtime/state.js';
import {
    startRectification,
    loadRectificationStartSettings,
    collectRectificationModalSettings
} from './rectification.js';
import { startManual } from './rectification.js';
import {
    startDistillation,
    collectDistillationSettings,
    loadDistillationStartSettings
} from './distillation.js';
import { startMashing, startHold, readStepsFromUI, setMashProfileUI } from './mashing-hold.js';
import {
    loadNbkSettings,
    loadFermentationSettings,
    collectNbkSettings,
    collectFermentationSettings,
    startNbk,
    startFermentation
} from './nbk-fermentation.js';

const CONTROL_MODE_STORAGE_KEY = 'control.selectedMode';
const MANUAL_RECT_STORAGE_KEY = 'control.manualRectSettings';
const MODE_SUPPORT_KEYS = {
    rectification: 'rectification',
    manual: 'manualRect',
    distillation: 'distillation',
    mashing: 'mashing',
    hold: 'hold',
    nbk: 'nbk',
    fermentation: 'fermentation'
};
const CONTROL_MODES = {
    rectification: {
        title: 'Авто-ректификация',
        subtitle: 'Параметры запуска процесса',
        startLabel: '▶️ Сохранить и запустить авто-ректификацию',
        startClass: 'btn-success',
        modeValue: MODE_RECT
    },
    manual: {
        title: 'Ручная ректификация',
        subtitle: 'Старт режима и ручное управление ТЭН/насос/клапаны',
        startLabel: '▶️ Запустить ручную ректификацию',
        startClass: 'btn-warning',
        modeValue: MODE_MANUAL
    },
    distillation: {
        title: 'Дистилляция',
        subtitle: 'Быстрые параметры запуска дистилляции',
        startLabel: '▶️ Запустить дистилляцию',
        startClass: 'btn-info',
        modeValue: MODE_DIST
    },
    mashing: {
        title: 'Затирка',
        subtitle: 'Температурный профиль и шаги затирки',
        startLabel: '▶️ Запустить затирку',
        startClass: 'btn-success',
        modeValue: MODE_MASH
    },
    hold: {
        title: 'Пастеризация',
        subtitle: 'Температурные шаги, паузы и управляемое охлаждение',
        startLabel: '▶️ Запустить пастеризацию',
        startClass: 'btn-success',
        modeValue: MODE_HOLD
    },
    nbk: {
        title: 'НБК',
        subtitle: 'Непрерывная бражная колонна: мощность, подача браги и контроль температуры низа колонны',
        startLabel: '▶️ Запустить НБК',
        startClass: 'btn-warning',
        modeValue: MODE_NBK
    },
    fermentation: {
        title: 'Ферментация',
        subtitle: 'Поддержание температуры брожения по датчику в кубе или ферментере',
        startLabel: '▶️ Запустить ферментацию',
        startClass: 'btn-info',
        modeValue: MODE_FERMENTATION
    }
};

let selectedControlMode = 'rectification';
let rectSettingsLoaded = false;
let distillationSettingsLoaded = false;
let nbkSettingsLoaded = false;
let fermentationSettingsLoaded = false;
let manualRectInitialized = false;
let latestModePreflight = null;
let preflightRefreshTimer = 0;

function normalizeActiveMashSteps() {
    const steps = runtimeMonitorState?.activeProfile?.mashing?.steps;
    if (!Array.isArray(steps)) {
        return [];
    }

    return steps
        .slice(0, 10)
        .map((step, index) => {
            const temperature = Number(step?.temperature);
            const duration = Number(step?.duration);
            const name = String(step?.name || '').trim() || `Шаг ${index + 1}`;
            if (!Number.isFinite(temperature) || temperature <= 0 || !Number.isFinite(duration) || duration <= 0) {
                return null;
            }
            return {
                temperature: Math.round(temperature * 10) / 10,
                duration: Math.round(duration),
                name
            };
        })
        .filter(Boolean);
}

function syncActiveMashingProfileToPanel() {
    const activeProfile = runtimeMonitorState?.activeProfile || {};
    if (!activeProfile.loaded || activeProfile.category !== 'mashing') {
        return;
    }

    const steps = normalizeActiveMashSteps();
    if (!steps.length) {
        return;
    }

    const container = document.getElementById('mash-steps');
    const appliedProfileId = container?.dataset?.profileId || '';
    const activeProfileId = String(activeProfile.id || '');
    if (appliedProfileId && activeProfileId && appliedProfileId === activeProfileId) {
        return;
    }

    setMashProfileUI(activeProfile.name || 'Затирка', steps, activeProfileId);
}

function getModeDefinition(mode) {
    return CONTROL_MODES[mode] || CONTROL_MODES.rectification;
}

function getTopologyModeSupport(mode = selectedControlMode, state = runtimeMonitorState) {
    const key = MODE_SUPPORT_KEYS[mode];
    if (!key) {
        return { known: false, supported: true, reason: '', key: '' };
    }

    const current = state?.equipment?.supportedModes?.[key];
    if (!current || typeof current.supported !== 'boolean') {
        return { known: false, supported: true, reason: '', key };
    }

    return {
        known: true,
        supported: Boolean(current.supported),
        reason: String(current.reason || '').trim(),
        key
    };
}

function getUnsupportedModeDetail(mode = selectedControlMode, state = runtimeMonitorState) {
    const support = getTopologyModeSupport(mode, state);
    if (!support.known || support.supported) {
        return '';
    }

    return support.reason || 'Для текущей комплектации не хватает обязательных термодатчиков.';
}

function ensureSelectedModeSupported() {
    const detail = getUnsupportedModeDetail(selectedControlMode, runtimeMonitorState);
    if (!detail) {
        return true;
    }

    addLog(`Запуск режима заблокирован: ${detail}`, 'warning');
    renderControlStartState();
    return false;
}

function persistModeSelection(mode) {
    try {
        localStorage.setItem(CONTROL_MODE_STORAGE_KEY, mode);
    } catch {
        // ignore storage failures
    }
}

function renderControlModeSelector(mode) {
    document.querySelectorAll('[data-mode-select]').forEach((button) => {
        const isSelected = button.dataset.modeSelect === mode;
        const unsupportedDetail = getUnsupportedModeDetail(button.dataset.modeSelect, runtimeMonitorState);
        button.classList.toggle('active', isSelected);
        button.classList.toggle('control-mode-selected', isSelected);
        button.classList.toggle('control-mode-topology-blocked', Boolean(unsupportedDetail));
        button.title = unsupportedDetail
            ? `Недоступно для текущей комплектации: ${unsupportedDetail}`
            : '';
    });
}

function renderControlModePanels(mode) {
    document.querySelectorAll('[data-mode-panel]').forEach((panel) => {
        const shouldShow = panel.dataset.modePanel === mode;
        panel.style.display = shouldShow ? '' : 'none';
    });
}

function renderControlModeHeader(mode) {
    const title = document.getElementById('control-mode-title');
    const subtitle = document.getElementById('control-mode-subtitle');
    const startButton = document.getElementById('mode-start-button');
    const confirmButton = document.getElementById('mode-start-confirm-button');
    const labels = {
        rectification: {
            title: 'Авто-ректификация',
            subtitle: 'Параметры запуска процесса',
            startLabel: 'Сохранить и запустить авто-ректификацию'
        },
        manual: {
            title: 'Ручная ректификация',
            subtitle: 'Старт режима и ручное управление нагревом с настройкой отбора тела',
            startLabel: 'Запустить ручную ректификацию'
        },
        distillation: {
            title: 'Дистилляция',
            subtitle: 'Быстрые параметры запуска дистилляции',
            startLabel: 'Запустить дистилляцию'
        },
        mashing: {
            title: 'Затирка',
            subtitle: 'Температурный профиль и шаги затирки',
            startLabel: 'Запустить затирку'
        },
        hold: {
            title: 'Пастеризация',
            subtitle: 'Температурные шаги, паузы и управляемое охлаждение',
            startLabel: 'Запустить пастеризацию'
        },
        nbk: {
            title: 'НБК',
            subtitle: 'Непрерывная бражная колонна: мощность, подача браги и контроль низа колонны',
            startLabel: 'Запустить НБК'
        },
        fermentation: {
            title: 'Ферментация',
            subtitle: 'Поддержание температуры брожения по датчику в кубе или ферментере',
            startLabel: 'Запустить ферментацию'
        }
    };
    const def = getModeDefinition(mode);
    const ui = labels[mode] || labels.rectification;
    const unsupportedDetail = getUnsupportedModeDetail(mode, runtimeMonitorState);

    if (title) title.textContent = ui.title;
    if (subtitle) {
        subtitle.textContent = unsupportedDetail
            ? `${ui.subtitle} Сейчас запуск недоступен: ${unsupportedDetail}`
            : ui.subtitle;
    }
    if (startButton) {
        startButton.textContent = ui.startLabel;
        startButton.dataset.mode = String(def.modeValue);
        startButton.classList.remove('btn-success', 'btn-warning', 'btn-info');
        startButton.classList.add('btn-primary');
        startButton.title = unsupportedDetail || '';
    }
    if (confirmButton) {
        confirmButton.textContent = ui.startLabel;
        confirmButton.title = unsupportedDetail || '';
    }
}

export function renderControlStartState() {
    renderControlModeSelector(selectedControlMode);
    renderControlModeHeader(selectedControlMode);

    const startButton = document.getElementById('mode-start-button');
    const statusEl = document.getElementById('mode-start-status');
    const confirmButton = document.getElementById('mode-start-confirm-button');
    const availability = getStartAvailabilityState(runtimeMonitorState);
    const unsupportedDetail = getUnsupportedModeDetail(selectedControlMode, runtimeMonitorState);
    const topologyBlocked = Boolean(unsupportedDetail);
    const activeProcessBlock = currentMode !== MODE_IDLE;
    const backendBlocked = Boolean(latestModePreflight) && !latestModePreflight.ready && !activeProcessBlock;
    const backendWarn = Boolean(latestModePreflight) &&
        latestModePreflight.ready &&
        Number(latestModePreflight.warningCount || 0) > 0;
    const effectiveState = activeProcessBlock
        ? availability
        : (topologyBlocked
            ? {
                ...availability,
                tone: 'danger',
                title: 'Режим не поддерживается этой комплектацией',
                detail: unsupportedDetail,
                disabled: true
            }
            : (backendBlocked
                ? {
                    ...availability,
                    tone: 'danger',
                    title: latestModePreflight.title || availability.title,
                    detail: latestModePreflight.detail || availability.detail,
                    disabled: true
                }
                : (backendWarn
                    ? {
                        ...availability,
                        tone: 'warn',
                        title: latestModePreflight.title || availability.title,
                        detail: latestModePreflight.detail || availability.detail
                    }
                    : availability)));

    if (startButton) {
        startButton.disabled = effectiveState.disabled;
        startButton.classList.toggle('btn-disabled', effectiveState.disabled);
        startButton.title = effectiveState.disabled ? effectiveState.detail : '';
    }

    if (statusEl) {
        statusEl.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
        statusEl.classList.add(`is-${effectiveState.tone}`);
        statusEl.textContent = activeProcessBlock
            ? 'Новый запуск недоступен, пока текущий процесс не остановлен и автоматика не вернётся в idle.'
            : `${effectiveState.title}. ${effectiveState.detail}`;
    }

    if (confirmButton) {
        confirmButton.disabled = effectiveState.disabled;
    }

    renderRectificationAdaptiveRecipeSummary();
    renderControlModeSummary();
    renderModeStartAdvisor();
    renderControlStartChecklist();
}

function clearModePreflightRefreshTimer() {
    if (!preflightRefreshTimer) return;
    window.clearTimeout(preflightRefreshTimer);
    preflightRefreshTimer = 0;
}

function isModeStartModalOpen() {
    return byId('mode-start-modal')?.classList.contains('active') || false;
}

function renderRuntimePreflightFallback() {
    const fallback = getStartAvailabilityState(runtimeMonitorState).preflight;
    if (!fallback) return;
    setPreflightState(fallback.title, fallback.detail, fallback.tone, fallback.checks);
}

function formatAdvisorPercent(value) {
    const numeric = Number(value);
    return Number.isFinite(numeric) ? `${Math.round(numeric)}%` : '—';
}

function formatAdvisorDurationMinutes(value) {
    const numeric = Number(value);
    if (!Number.isFinite(numeric) || numeric <= 0) return '—';
    const totalMinutes = Math.round(numeric);
    const hours = Math.floor(totalMinutes / 60);
    const minutes = totalMinutes % 60;
    if (hours <= 0) return `${minutes} мин`;
    if (minutes === 0) return `${hours} ч`;
    return `${hours} ч ${minutes} мин`;
}

function formatSignedAdvisorNumber(value, digits = 1, suffix = '') {
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) return '—';
    return `${numeric >= 0 ? '+' : ''}${numeric.toFixed(digits)}${suffix}`;
}

function appendAdvisorMetaItem(container, tone, title, detail, action = '') {
    const item = document.createElement('div');
    item.className = `modal-advisor-item is-${tone || 'muted'}`;

    const titleEl = document.createElement('strong');
    titleEl.textContent = title;
    item.appendChild(titleEl);

    const detailEl = document.createElement('div');
    detailEl.className = 'modal-advisor-text';
    detailEl.textContent = detail;
    item.appendChild(detailEl);

    if (action) {
        const actionEl = document.createElement('div');
        actionEl.className = 'modal-advisor-action';
        actionEl.textContent = action;
        item.appendChild(actionEl);
    }

    container.appendChild(item);
}

function escapeControlHtml(value) {
    return String(value ?? '')
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&#39;');
}

function formatRecipeTemp(baseValue, effectiveValue) {
    const base = Number(baseValue || 0);
    const effective = Number(effectiveValue || 0);
    if (!(base > 0)) return '—';
    if (!(effective > 0) || Math.abs(effective - base) < 0.01) {
        return `${base.toFixed(2)}°C`;
    }
    return `${base.toFixed(2)}°C → ${effective.toFixed(2)}°C`;
}

function formatRecipePressure(value) {
    const numeric = Number(value || 0);
    return numeric > 0 ? `${numeric.toFixed(1)} мм рт.ст.` : '—';
}

function formatRecipeDateTime(value) {
    const numeric = Number(value || 0);
    return numeric > 0 ? new Date(numeric * 1000).toLocaleString('ru-RU') : '—';
}

function addBoosterChecklistItem(items, config, targetId) {
    const boosterEnabled = Boolean(config?.boosterEnabled);
    const stopTemp = Number(config?.boosterStopCubeTempC);
    const cubeTemp = Number(runtimeMonitorState?.temps?.cube || 0);

    if (!boosterEnabled) {
        addChecklistItem(
            items,
            'muted',
            'Разгонный ТЭН',
            'Booster SSR для этого запуска отключён. Разогрев пойдёт только через основной TRIAC.',
            targetId
        );
        return;
    }

    if (Number.isFinite(stopTemp) && cubeTemp > 0 && cubeTemp >= stopTemp) {
        addChecklistItem(
            items,
            'warn',
            'Разгонный ТЭН',
            `Booster SSR включён, но куб уже ${cubeTemp.toFixed(1)} °C и выше порога ${stopTemp.toFixed(1)} °C. На старте он почти сразу не понадобится.`,
            targetId
        );
        return;
    }

    addChecklistItem(
        items,
        'good',
        'Разгонный ТЭН',
        Number.isFinite(stopTemp)
            ? `Booster SSR будет работать только на фазе разогрева и отключится при ${stopTemp.toFixed(1)} °C по кубу.`
            : 'Booster SSR будет участвовать только в фазе разогрева.',
        targetId
    );
}

function renderRectificationAdaptiveRecipeSummary() {
    const section = byId('rect-adaptive-recipe-section');
    const root = byId('rect-active-profile-summary');
    if (!section || !root) return;

    const isRectification = selectedControlMode === 'rectification';
    section.style.display = isRectification ? '' : 'none';
    if (!isRectification) return;

    const profile = runtimeMonitorState.activeProfile || {};
    const validation = profile.validation || {};
    const baseTemps = profile.baseTemperatures || {};
    const baroPreview = profile.baroPreview || {};
    const previewTemps = profile.effectiveTemperaturesPreview || {};
    const correctionEnabledForRun = getCheckboxValue('rect-start-baro-correction-enabled');

    let tone = 'muted';
    let title = 'Активный профиль не загружен';
    let detail = 'Загрузите профиль ректификации, чтобы увидеть baseline давления, ожидаемый сдвиг порогов и поведение рецепта для текущего запуска.';

    if (profile.loaded) {
        const profileName = profile.name || profile.id || 'без названия';
        const profileCategory = String(profile.category || '').trim();

        if (profileCategory && profileCategory !== 'rectification') {
            tone = 'danger';
            title = `Профиль "${profileName}" не подходит для авто-ректификации`;
            detail = `Сейчас активен профиль категории "${profileCategory}". Для адаптивной ректификации загрузите профиль именно категории "rectification".`;
        } else if (!correctionEnabledForRun) {
            tone = 'muted';
            title = `Профиль "${profileName}" загружен, барокоррекция отключена`;
            detail =
                `Рецепт валидирован при ${formatRecipePressure(validation.atmosphereMmHg || baroPreview.baselinePressureMmHg)}.` +
                ` Для этого запуска пороги останутся базовыми: головы ${formatRecipeTemp(baseTemps.headsEnd, baseTemps.headsEnd)},` +
                ` тело ${formatRecipeTemp(baseTemps.bodyStart, baseTemps.bodyStart)} - ${formatRecipeTemp(baseTemps.bodyEnd, baseTemps.bodyEnd)}.`;
        } else if (!baroPreview.applicable) {
            tone = 'warn';
            title = `Профиль "${profileName}" загружен`;
            detail = baroPreview.note || 'Для автоматической поправки рецепта пока не хватает baseline профиля или текущего давления BMP280.';
        } else if (baroPreview.applied) {
            tone = 'warn';
            title = `Профиль "${profileName}" адаптируется под текущее давление`;
            detail =
                `Рецепт валидирован при ${formatRecipePressure(baroPreview.baselinePressureMmHg)}, сейчас ${formatRecipePressure(baroPreview.currentPressureMmHg)}.` +
                ` Сдвиг составит ${formatSignedAdvisorNumber(baroPreview.appliedShiftC, 2, '°C')}: головы ${formatRecipeTemp(baseTemps.headsEnd, previewTemps.headsEnd)},` +
                ` тело ${formatRecipeTemp(baseTemps.bodyStart, previewTemps.bodyStart)}, конец тела ${formatRecipeTemp(baseTemps.bodyEnd, previewTemps.bodyEnd)}.`;
        } else {
            tone = 'good';
            title = `Профиль "${profileName}" близок к своему baseline`;
            detail =
                `Baseline ${formatRecipePressure(baroPreview.baselinePressureMmHg)}, сейчас ${formatRecipePressure(baroPreview.currentPressureMmHg)}.` +
                ` Условия близки, поэтому заметный сдвиг порогов не требуется.`;
        }
    }

    root.className = `control-profile-summary is-${tone}`;
    root.innerHTML = `
        <div class="control-profile-summary-title">${escapeControlHtml(title)}</div>
        <div class="control-profile-summary-text">${escapeControlHtml(detail)}</div>
        <div class="control-profile-summary-meta">
            <div>
                <span>Baseline профиля</span>
                <strong>${escapeControlHtml(formatRecipePressure(validation.atmosphereMmHg || baroPreview.baselinePressureMmHg))}</strong>
            </div>
            <div>
                <span>Текущее давление</span>
                <strong>${escapeControlHtml(formatRecipePressure(baroPreview.currentPressureMmHg))}</strong>
            </div>
            <div>
                <span>Валидация</span>
                <strong>${escapeControlHtml(formatRecipeDateTime(validation.validatedAt))}</strong>
            </div>
        </div>
    `;
}

function buildAdvisorFallback() {
    const availability = getStartAvailabilityState(runtimeMonitorState);
    const preflight = availability.preflight || {};
    return {
        tone: preflight.tone || availability.tone || 'muted',
        title: preflight.title || availability.title || 'Проверка перед запуском',
        detail: preflight.detail || availability.detail || 'Собираем сигналы готовности.',
        action: 'Откройте чек-лист и проверьте ключевые уставки перед подтверждением запуска.',
        confidence: {}
    };
}

function renderModeStartAdvisor(snapshot = latestModePreflight) {
    const root = byId('mode-start-advisor');
    const titleEl = byId('mode-start-advisor-title');
    const summaryEl = byId('mode-start-advisor-summary');
    const detailEl = byId('mode-start-advisor-detail');
    const actionEl = byId('mode-start-advisor-action');
    const metaEl = byId('mode-start-advisor-meta');
    if (!root || !titleEl || !summaryEl || !detailEl || !actionEl || !metaEl) return;

    const advisor = snapshot?.advisor || buildAdvisorFallback();
    const tone = advisor.tone || snapshot?.tone || 'muted';
    const confidence = advisor.confidence || {};
    const startupText = formatAdvisorPercent(confidence.startup);
    const decisionText = formatAdvisorPercent(confidence.decision);

    root.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    root.classList.add(`is-${tone}`);
    titleEl.textContent = advisor.title || 'Совет перед запуском';
    summaryEl.textContent = snapshot?.advisor
        ? `Уверенность старта ${startupText}, инженерная оценка ${decisionText}`
        : 'Покажем краткую рекомендацию после backend preflight';
    detailEl.textContent = advisor.detail || 'Собираем рекомендации перед запуском.';
    actionEl.textContent = advisor.action || '';
    metaEl.innerHTML = '';

    appendAdvisorMetaItem(
        metaEl,
        tone,
        'Уверенность запуска',
        `Старт: ${startupText}. Process health: ${formatAdvisorPercent(confidence.processHealth)}, stability: ${formatAdvisorPercent(confidence.stability)}.`
    );

    const profile = advisor.profile || {};
    if (profile.relevant) {
        const profileTone = profile.loaded ? (profile.matchesMode ? 'good' : 'warn') : 'warn';
        const profileName = profile.name || 'Профиль не привязан';
        const profileDetail = profile.loaded
            ? (profile.matchesMode
                ? `Активный профиль: ${profileName}. Категория согласована с выбранным режимом.`
                : `Активный профиль: ${profileName}. Категория не совпадает с выбранным режимом.`)
            : 'Запуск пойдёт без активного профиля, baseline для Run Advisor будет слабее.';
        appendAdvisorMetaItem(metaEl, profileTone, 'Связка с профилем', profileDetail);
    }

    const baro = advisor.baroCorrection || {};
    if (baro.enabled) {
        const effectiveTemps = baro.effectiveTemperatures || {};
        const [toneBaro, detailBaro] = baro.applicable && baro.applied
            ? [
                'warn',
                `Baseline ${Number(baro.baselinePressureMmHg || 0).toFixed(1)} мм рт.ст., сейчас ${Number(baro.currentPressureMmHg || 0).toFixed(1)} мм рт.ст., мягкий сдвиг ${formatSignedAdvisorNumber(baro.appliedShiftC, 2, '°C')}. ` +
                `Пороги на этот запуск: головы до ${Number(effectiveTemps.headsEnd || 0).toFixed(2)}°C, тело ${Number(effectiveTemps.bodyStart || 0).toFixed(2)}-${Number(effectiveTemps.bodyEnd || 0).toFixed(2)}°C.`
            ]
            : (baro.applicable
                ? [
                    'good',
                    baro.note || 'Отклонение давления небольшое, заметный сдвиг порогов не требуется.'
                ]
                : [
                    'warn',
                    baro.note || 'Для preview барокоррекции не хватает baseline профиля или текущего давления BMP280.'
                ]);

        appendAdvisorMetaItem(
            metaEl,
            toneBaro,
            'Барокоррекция профиля',
            detailBaro
        );
    }

    const dryRun = advisor.dryRun || {};
    if (dryRun.supported) {
        const charge = dryRun.charge || {};
        const volumes = dryRun.volumes || {};
        const speeds = dryRun.speeds || {};
        const totalDuration = formatAdvisorDurationMinutes(dryRun.totalMin);
        const heatingDuration = formatAdvisorDurationMinutes(dryRun.heatingMin);
        const prepDuration = formatAdvisorDurationMinutes(dryRun.preparationMin);
        const takeoffDuration = formatAdvisorDurationMinutes(dryRun.takeoffMin);
        const baselineDuration = formatAdvisorDurationMinutes(dryRun.baselineDurationMin);
        const energyText = Number.isFinite(Number(dryRun.energyKwh))
            ? `${Number(dryRun.energyKwh).toFixed(1)} кВт·ч`
            : '—';
        const baselineEnergyText = Number.isFinite(Number(dryRun.baselineEnergyKwh))
            ? `${Number(dryRun.baselineEnergyKwh).toFixed(1)} кВт·ч`
            : '—';
        const dryRunDetail =
            `${dryRun.summary || `Ожидаемо ${totalDuration}.`} ` +
            `Сырец ${Number(charge.feedVolumeL || 0).toFixed(1)} л при ${Number(charge.feedAbvPercent || 0).toFixed(1)}%, ` +
            `головы/тело/хвосты ${Number(volumes.headsMl || 0).toFixed(0)} / ${Number(volumes.bodyMl || 0).toFixed(0)} / ${Number(volumes.tailsMl || 0).toFixed(0)} мл. ` +
            `Подготовка ${prepDuration}, прогрев ${heatingDuration}, отбор ${takeoffDuration}. ` +
            `Энергия ${energyText}.`;
        const dryRunAction = dryRun.usesLearning
            ? `Прогноз подмешивает baseline профиля: длительность ${baselineDuration}, энергия ${baselineEnergyText}.`
            : 'Пока это модель по текущим уставкам без сильной опоры на успешный baseline профиля.';

        appendAdvisorMetaItem(
            metaEl,
            dryRun.usesLearning ? 'good' : 'muted',
            'Dry-run запуска',
            dryRunDetail,
            dryRunAction
        );

        if (Number.isFinite(Number(speeds.headsMlH)) || Number.isFinite(Number(speeds.bodyMlH))) {
            appendAdvisorMetaItem(
                metaEl,
                'muted',
                'Темп отбора в прогнозе',
                `Головы около ${Number(speeds.headsMlH || 0).toFixed(0)} мл/ч, тело ${Number(speeds.bodyMlH || 0).toFixed(0)} мл/ч, хвосты ${Number(speeds.tailsMlH || 0).toFixed(0)} мл/ч.`
            );
        }

        if (dryRun.riskTitle || dryRun.riskDetail) {
            appendAdvisorMetaItem(
                metaEl,
                dryRun.riskTone || 'muted',
                dryRun.riskTitle || 'Риск dry-run',
                dryRun.riskDetail || 'Сигналов риска для dry-run не обнаружено.'
            );
        }
    }

    const phaseSignals = [
        ['Отбор', confidence.takeoff],
        ['Конец голов', confidence.headsEnd],
        ['Конец тела', confidence.bodyEnd],
        ['Переход в хвосты', confidence.tails],
        ['Power limit', confidence.powerLimit]
    ].filter(([, value]) => Number.isFinite(Number(value)));

    if (phaseSignals.length) {
        const signalDetail = phaseSignals
            .map(([label, value]) => `${label}: ${formatAdvisorPercent(value)}`)
            .join(' • ');
        appendAdvisorMetaItem(metaEl, 'muted', 'Сигналы автоматики', signalDetail);
    }
}

function openModeStartModal() {
    const modal = byId('mode-start-modal');
    if (!modal) return;
    modal.classList.add('active');
}

export function closeModeStartModal() {
    const modal = byId('mode-start-modal');
    if (!modal) return;
    clearModePreflightRefreshTimer();
    modal.classList.remove('active');
    latestModePreflight = null;
    renderRuntimePreflightFallback();
    renderControlStartState();
}

function focusChecklistTarget(targetId) {
    const target = byId(targetId);
    if (!target) return;

    closeModeStartModal();

    window.setTimeout(() => {
        let detailsParent = target.closest('details');
        while (detailsParent) {
            detailsParent.open = true;
            detailsParent = detailsParent.parentElement?.closest('details') || null;
        }

        target.scrollIntoView({ block: 'center', behavior: 'smooth' });
        if (typeof target.focus === 'function') {
            target.focus({ preventScroll: true });
        }

        target.classList.remove('control-field-attention');
        void target.offsetWidth;
        target.classList.add('control-field-attention');
        window.setTimeout(() => target.classList.remove('control-field-attention'), 1700);
    }, 30);
}

function addChecklistItem(items, tone, title, detail, targetId = '') {
    items.push({ tone, title, detail, targetId });
}

function formatMinutesTotal(totalMinutes) {
    const hours = Math.floor(totalMinutes / 60);
    const minutes = Math.round(totalMinutes % 60);
    if (hours <= 0) return `${minutes} мин`;
    if (minutes === 0) return `${hours} ч`;
    return `${hours} ч ${minutes} мин`;
}

function buildControlModeSummary() {
    const availability = getStartAvailabilityState(runtimeMonitorState);
    const summary = {
        tone: availability.tone,
        kicker: 'Прогноз запуска',
        title: 'Что запустится',
        text: 'Проверьте настройки режима, затем переходите к старту.',
        metrics: []
    };

    if (selectedControlMode === 'rectification') {
        const effectiveAbv = getEffectiveAbvForCalculations();
        const settings = {
            ...runtimeMonitorState.rectification,
            feedVolumeL: getNumberValue('rect-start-feed-volume', 20),
            feedAbvPercent: getNumberValue('rect-start-feed-abv', 40),
            headsPercent: getNumberValue('rect-start-heads-percent', 8),
            bodyPercent: getNumberValue('rect-start-body-percent', 84),
            tailsPercent: getNumberValue('rect-start-tails-percent', 8),
            headsSpeedMlHKw: getNumberValue('rect-start-heads-speed', 300),
            bodySpeedMlHKw: getNumberValue('rect-start-body-speed', 600),
            stabilizationMin: getNumberValue('rect-start-stabilization', 30),
            purgeMin: getNumberValue('rect-start-purge', 5),
            boosterEnabled: getCheckboxValue('rect-start-booster-enabled', Boolean(runtimeMonitorState?.equipment?.boosterHeaterEnabled)),
            boosterStopCubeTempC: getNumberValue('rect-start-booster-stop-cube-temp', Number(runtimeMonitorState?.equipment?.boosterHeaterStopCubeTempC || 78))
        };
        const targets = estimateRectTargets(settings, effectiveAbv.value);
        const effectiveEquipment = {
            ...runtimeMonitorState.equipment,
            heaterPowerW: Math.max(0, Number(runtimeMonitorState.equipment.heaterPowerW || maxHeaterPower) || 0)
        };
        const headsSpeed = getRectificationTakeoffRateMlH(settings.headsSpeedMlHKw, effectiveEquipment);
        const bodySpeed = getRectificationTakeoffRateMlH(settings.bodySpeedMlHKw, effectiveEquipment);
        summary.title = 'Авто-ректификация';
        summary.text = `Будет рассчитан отбор фракций по ${effectiveAbv.source === 'sensor' ? 'данным ареометра' : 'плановой крепости'}, затем выполнены стабилизация и продувка перед телом.`;
        summary.metrics = [
            { label: 'Сырец', value: `${settings.feedVolumeL.toFixed(1)} л • ${settings.feedAbvPercent.toFixed(1)}%` },
            { label: 'Фракции', value: `${targets.heads.toFixed(0)} / ${targets.body.toFixed(0)} / ${targets.tails.toFixed(0)} мл` },
            { label: 'Скорости', value: `головы ${headsSpeed.toFixed(0)} • тело ${bodySpeed.toFixed(0)} мл/ч` },
            { label: 'Подготовка', value: `${formatMinutesTotal(settings.stabilizationMin + settings.purgeMin)}` }
        ];
        return summary;
    }

    if (selectedControlMode === 'manual') {
        const settings = collectManualRectSettings();
        const headsModeLabel = settings.heads.mode === 'time'
            ? `${settings.heads.time.toFixed(0)} мин`
            : (settings.heads.mode === 'speed'
                ? `${settings.heads.speed.toFixed(0)} мл/ч`
                : `${settings.heads.temp.toFixed(1)} °C`);
        summary.title = 'Ручная ректификация';
        summary.text = 'Запустится ручной режим с живым управлением нагревом и насосом, а логика фракций останется опорой для оператора.';
        summary.metrics = [
            { label: 'Сырец', value: `${settings.feed.volumeL.toFixed(1)} л • ${settings.feed.abvPercent.toFixed(1)}%` },
            { label: 'Головы', value: `${settings.heads.volume.toFixed(0)} мл • ${headsModeLabel}` },
            { label: 'Тело', value: `скачок ${settings.body.spikeThreshold.toFixed(2)} °C • шаг ${settings.body.speedDecrement.toFixed(0)}%` },
            { label: 'Хвосты', value: settings.tails.enabled ? 'отбор включён' : 'отбор отключён' }
        ];
        return summary;
    }

    if (selectedControlMode === 'distillation') {
        const settings = collectDistillationSettings();
        const factPower = getNumberValue('dist-start-power-fact', 0);
        summary.title = 'Дистилляция';
        summary.text = 'Процесс пойдёт на заданной мощности и завершится по температуре окончания, без сложной фазовой логики колонны.';
        summary.metrics = [
            { label: 'Стоп-условие', value: `${settings.endTemp.toFixed(1)} °C` },
            { label: 'Уставка', value: `${settings.powerW.toFixed(0)} Вт` },
            { label: 'Факт сейчас', value: `${factPower.toFixed(0)} Вт` }
        ];
        return summary;
    }

    if (selectedControlMode === 'nbk') {
        const settings = collectNbkSettings();
        summary.title = 'НБК';
        summary.text = 'Система прогреет НБК, дождётся рабочей температуры низа колонны и затем откроет подачу браги на заданной скорости.';
        summary.metrics = [
            { label: 'Нагрев', value: `${settings.powerW.toFixed(0)} Вт` },
            { label: 'Подача', value: `${settings.pumpSpeedMlH.toFixed(0)} мл/ч` },
            { label: 'Защитный порог', value: `${settings.columnBottomTempThresholdC.toFixed(1)} °C` }
        ];
        return summary;
    }

    if (selectedControlMode === 'fermentation') {
        const settings = collectFermentationSettings();
        summary.title = 'Ферментация';
        summary.text = 'Контур перейдёт в режим поддержания температуры и будет держать среду около цели с указанным гистерезисом.';
        summary.metrics = [
            { label: 'Цель', value: `${settings.targetTempC.toFixed(1)} °C` },
            { label: 'Гистерезис', value: `${settings.hysteresisC.toFixed(1)} °C` },
            { label: 'Нагрев', value: settings.useHeater ? 'ТЭН участвует' : 'ТЭН отключён' }
        ];
        return summary;
    }

    if (selectedControlMode === 'mashing') {
        const steps = readStepsFromUI('mash-steps', 'mash');
        const temps = steps.map((step) => Number(step.temperature)).filter(Number.isFinite);
        const totalMinutes = steps.reduce((sum, step) => sum + Number(step.duration || 0), 0);
        summary.title = 'Затирка';
        summary.text = 'Профиль пройдёт по шагам затирки последовательно, удерживая температуру и таймер каждого этапа.';
        summary.metrics = [
            { label: 'Шаги', value: `${steps.length} этап(ов)` },
            { label: 'Длительность', value: formatMinutesTotal(totalMinutes) },
            { label: 'Диапазон', value: temps.length ? `${Math.min(...temps).toFixed(1)}–${Math.max(...temps).toFixed(1)} °C` : 'не задан' }
        ];
        return summary;
    }

    if (selectedControlMode === 'hold') {
        const steps = readStepsFromUI('hold-steps', 'hold');
        const totalMinutes = steps.reduce((sum, step) => sum + Number(step.duration || 0), 0);
        const tempSteps = steps.filter((step) => Number(step.temperature) > 0);
        summary.title = 'Пастеризация';
        summary.text = 'Режим выполнит температурные ступени и паузы по списку, включая охлаждение там, где оно отмечено в шагах.';
        summary.metrics = [
            { label: 'Шаги', value: `${steps.length} этап(ов)` },
            { label: 'Длительность', value: formatMinutesTotal(totalMinutes) },
            { label: 'Темп. ступени', value: `${tempSteps.length} активных шаг(ов)` }
        ];
        return summary;
    }

    return summary;
}

function finalizeControlModeSummary(summary) {
    const unsupportedDetail = getUnsupportedModeDetail(selectedControlMode, runtimeMonitorState);
    if (!unsupportedDetail) {
        return summary;
    }

    return {
        ...summary,
        tone: 'danger',
        kicker: 'Комплектация ограничивает режим',
        text: `Этот режим сейчас нельзя запустить: ${unsupportedDetail}`,
        metrics: [
            { label: 'Статус', value: 'Запуск заблокирован' },
            ...summary.metrics
        ].slice(0, 4)
    };
}

export function renderControlModeSummary() {
    const root = byId('control-mode-summary');
    const titleEl = byId('control-mode-summary-title');
    const kickerEl = byId('control-mode-summary-kicker');
    const textEl = byId('control-mode-summary-text');
    const metricsEl = byId('control-mode-summary-metrics');
    if (!root || !titleEl || !kickerEl || !textEl || !metricsEl) return;

    const summary = finalizeControlModeSummary(buildControlModeSummary());
    root.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    root.classList.add(`is-${summary.tone}`);
    titleEl.textContent = summary.title;
    kickerEl.textContent = summary.kicker;
    textEl.textContent = summary.text;
    metricsEl.innerHTML = '';

    summary.metrics.forEach((metric) => {
        const card = document.createElement('div');
        card.className = 'control-mode-summary-metric';
        const label = document.createElement('span');
        label.textContent = metric.label;
        const value = document.createElement('strong');
        value.textContent = metric.value;
        card.append(label, value);
        metricsEl.appendChild(card);
    });
}

function buildRectificationChecklist(items) {
    const settings = {
        feedVolumeL: getNumberValue('rect-start-feed-volume', 20),
        feedAbvPercent: getNumberValue('rect-start-feed-abv', 40),
        headsPercent: getNumberValue('rect-start-heads-percent', 8),
        bodyPercent: getNumberValue('rect-start-body-percent', 84),
        tailsPercent: getNumberValue('rect-start-tails-percent', 8),
        headsSpeedMlHKw: getNumberValue('rect-start-heads-speed', 300),
        bodySpeedMlHKw: getNumberValue('rect-start-body-speed', 600),
        boosterEnabled: getCheckboxValue('rect-start-booster-enabled', Boolean(runtimeMonitorState?.equipment?.boosterHeaterEnabled)),
        boosterStopCubeTempC: getNumberValue('rect-start-booster-stop-cube-temp', Number(runtimeMonitorState?.equipment?.boosterHeaterStopCubeTempC || 78))
    };
    const fractionsSum = settings.headsPercent + settings.bodyPercent + settings.tailsPercent;
    const maxVolume = Math.max(1, Math.min(250, getCubeVolumeLimitL()));

    addChecklistItem(
        items,
        settings.feedVolumeL > 0 && settings.feedVolumeL <= maxVolume ? 'good' : 'danger',
        'Сырец и объём куба',
        settings.feedVolumeL <= maxVolume
            ? `Объём ${settings.feedVolumeL.toFixed(1)} л укладывается в лимит куба ${maxVolume.toFixed(1)} л.`
            : `Объём ${settings.feedVolumeL.toFixed(1)} л превышает допустимый лимит куба ${maxVolume.toFixed(1)} л.`,
        'rect-start-feed-volume'
    );

    addChecklistItem(
        items,
        fractionsSum > 100 ? 'danger' : (fractionsSum < 99 ? 'warn' : 'good'),
        'Баланс фракций',
        fractionsSum > 100
            ? `Сумма фракций ${fractionsSum.toFixed(1)}%. Нужно не больше 100%.`
            : (fractionsSum < 99
                ? `Сумма фракций ${fractionsSum.toFixed(1)}%. Проверьте, не потеряли ли часть выхода.`
                : `Сумма фракций ${fractionsSum.toFixed(1)}%. Баланс выглядит корректно.`),
        'rect-start-heads-percent'
    );

    addChecklistItem(
        items,
        settings.bodySpeedMlHKw > settings.headsSpeedMlHKw ? 'good' : 'warn',
        'Скорости отбора',
        settings.bodySpeedMlHKw > settings.headsSpeedMlHKw
            ? `Тело (${settings.bodySpeedMlHKw.toFixed(0)}) быстрее голов (${settings.headsSpeedMlHKw.toFixed(0)}), логика профиля сохранена.`
            : 'Скорость тела не выше скорости голов. Обычно тело ведут заметно быстрее.',
        'rect-start-body-speed'
    );

    addBoosterChecklistItem(items, settings, 'rect-start-booster-enabled');
}

function buildManualChecklist(items) {
    const settings = collectManualRectSettings();
    const maxVolume = Math.max(1, Math.min(250, getCubeVolumeLimitL()));
    const headsMode = settings.heads.mode;
    const tailsEnabled = settings.tails.enabled;

    addChecklistItem(
        items,
        settings.feed.volumeL > 0 && settings.feed.volumeL <= maxVolume ? 'good' : 'danger',
        'Сырец для ручной ректификации',
        settings.feed.volumeL <= maxVolume
            ? `Объём ${settings.feed.volumeL.toFixed(1)} л и крепость ${settings.feed.abvPercent.toFixed(1)}% выглядят рабочими.`
            : `Объём ${settings.feed.volumeL.toFixed(1)} л превышает лимит куба ${maxVolume.toFixed(1)} л.`,
        'manual-feed-volume'
    );

    addChecklistItem(
        items,
        headsMode === 'time'
            ? (settings.heads.time >= 30 ? 'good' : 'warn')
            : (headsMode === 'speed'
                ? (settings.heads.speed >= 50 ? 'good' : 'warn')
                : ((settings.heads.temp >= 75 && settings.heads.temp <= 85) ? 'good' : 'warn')),
        'Режим отбора голов',
        headsMode === 'time'
            ? `Отбор по времени: ${settings.heads.time.toFixed(0)} мин на ${settings.heads.volume.toFixed(0)} мл.`
            : (headsMode === 'speed'
                ? `Отбор по скорости: ${settings.heads.speed.toFixed(0)} мл/ч на ${settings.heads.volume.toFixed(0)} мл.`
                : `Отбор по температуре куба: ${settings.heads.temp.toFixed(1)} °C.`),
        headsMode === 'time'
            ? 'manual-heads-time'
            : (headsMode === 'speed' ? 'manual-heads-speed' : 'manual-heads-temp')
    );

    addChecklistItem(
        items,
        settings.body.spikeThreshold > 0 && settings.body.speedDecrement > 0 ? 'good' : 'warn',
        'Переход тела и хвостов',
        `Порог скачка ${settings.body.spikeThreshold.toFixed(2)} °C, декремент ${settings.body.speedDecrement.toFixed(0)}%, режим перехода: ${settings.body.toTailsMode}.`,
        'manual-body-spike-threshold'
    );

    addChecklistItem(
        items,
        !tailsEnabled
            ? 'warn'
            : (settings.tails.stopMode === 'temp'
                ? ((settings.tails.stopTemp >= 85 && settings.tails.stopTemp <= 99) ? 'good' : 'warn')
                : ((settings.tails.stopAbv >= 10 && settings.tails.stopAbv <= 70) ? 'good' : 'warn')),
        'Хвосты',
        !tailsEnabled
            ? 'Отбор хвостов отключён. Это допустимо, но проверьте, что хвостовая часть вам не нужна.'
            : (settings.tails.stopMode === 'temp'
                ? `Остановка хвостов по температуре куба: ${settings.tails.stopTemp.toFixed(1)} °C.`
                : `Остановка хвостов по крепости: ${settings.tails.stopAbv.toFixed(0)}%.`),
        tailsEnabled
            ? (settings.tails.stopMode === 'temp' ? 'manual-tails-stop-temp' : 'manual-tails-stop-abv')
            : 'manual-tails-enabled'
    );
}

function buildDistillationChecklist(items) {
    const settings = collectDistillationSettings();
    const actualPower = getNumberValue('dist-start-power-fact', 0);

    addChecklistItem(
        items,
        settings.powerW > 0 ? 'good' : 'danger',
        'Мощность нагрева',
        settings.powerW > 0
            ? `Установка ${settings.powerW.toFixed(0)} Вт, фактическая мощность сейчас около ${actualPower.toFixed(0)} Вт.`
            : 'Старт с нулевой мощностью не имеет смысла. Поднимите уставку нагрева.',
        'dist-start-power-percent'
    );

    addChecklistItem(
        items,
        settings.endTemp >= 88 && settings.endTemp <= 100 ? 'good' : 'warn',
        'Температура окончания',
        settings.endTemp >= 88 && settings.endTemp <= 100
            ? `Стоп-температура ${settings.endTemp.toFixed(1)} °C находится в рабочем диапазоне.`
            : `Стоп-температура ${settings.endTemp.toFixed(1)} °C выглядит нетипично. Проверьте целевой сценарий.`,
        'dist-start-end-temp'
    );

    addBoosterChecklistItem(items, settings, 'dist-start-booster-enabled');
}

function buildNbkChecklist(items) {
    const settings = collectNbkSettings();

    addChecklistItem(
        items,
        settings.powerW >= 1000 ? 'good' : 'warn',
        'Мощность НБК',
        settings.powerW >= 1000
            ? `Рабочая мощность ${settings.powerW.toFixed(0)} Вт задана.`
            : `Мощность ${settings.powerW.toFixed(0)} Вт может быть слишком низкой для устойчивой НБК.`,
        'nbk-power-w'
    );

    addChecklistItem(
        items,
        settings.pumpSpeedMlH >= 500 ? 'good' : 'warn',
        'Подача браги',
        settings.pumpSpeedMlH >= 500
            ? `Подача ${settings.pumpSpeedMlH.toFixed(0)} мл/ч задана.`
            : 'Подача браги выглядит слишком низкой. Проверьте производительность насоса.',
        'nbk-pump-speed'
    );

    addChecklistItem(
        items,
        settings.columnBottomTempThresholdC >= 85 && settings.columnBottomTempThresholdC <= 100 ? 'good' : 'warn',
        'Порог температуры низа колонны',
        `Порог защиты ${settings.columnBottomTempThresholdC.toFixed(1)} °C.`,
        'nbk-column-bottom-threshold'
    );

    addBoosterChecklistItem(items, settings, 'nbk-booster-enabled');
}

function buildFermentationChecklist(items) {
    const settings = collectFermentationSettings();

    addChecklistItem(
        items,
        settings.targetTempC >= 18 && settings.targetTempC <= 32 ? 'good' : 'warn',
        'Целевая температура',
        `Задано ${settings.targetTempC.toFixed(1)} °C.`,
        'ferm-target-temp'
    );

    addChecklistItem(
        items,
        settings.hysteresisC >= 0.2 && settings.hysteresisC <= 2 ? 'good' : 'warn',
        'Гистерезис',
        `Гистерезис ${settings.hysteresisC.toFixed(1)} °C.`,
        'ferm-hysteresis'
    );

    addChecklistItem(
        items,
        settings.useHeater ? 'good' : 'warn',
        'Контур поддержания',
        settings.useHeater
            ? 'ТЭН участвует в поддержании температуры.'
            : 'Поддержание без ТЭНа включено. Убедитесь, что внешний контур действительно справится.',
        'ferm-use-heater'
    );
}

function buildMashingChecklist(items) {
    const steps = readStepsFromUI('mash-steps', 'mash');
    const name = getStringValue('mash-profile-name', '').trim();

    addChecklistItem(
        items,
        name ? 'good' : 'warn',
        'Имя профиля',
        name ? `Профиль назван: ${name}.` : 'Профиль без имени тоже запустится, но его будет сложнее отличить в истории.',
        'mash-profile-name'
    );

    addChecklistItem(
        items,
        steps.length > 0 ? 'good' : 'danger',
        'Шаги затирки',
        steps.length > 0
            ? `Подготовлено ${steps.length} шаг(ов) затирки.`
            : 'Нужен хотя бы один корректный шаг с температурой и длительностью.',
        steps.length > 0 ? 'mash-steps' : 'mash-add-step-button'
    );

    if (steps.length > 0) {
        const invalidOrder = steps.some((step, index) => index > 0 && step.temperature < steps[index - 1].temperature - 5);
        addChecklistItem(
            items,
            invalidOrder ? 'warn' : 'good',
            'Последовательность температур',
            invalidOrder
                ? 'Температуры шагов сильно скачут вниз. Проверьте, точно ли такой профиль задуман.'
                : 'Последовательность температур выглядит логичной для пошагового профиля.',
            'mash-steps'
        );
    }
}

function buildHoldChecklist(items) {
    const steps = readStepsFromUI('hold-steps', 'hold');
    const activeTempSteps = steps.filter((step) => Number(step.temperature) > 0);

    addChecklistItem(
        items,
        steps.length > 0 ? 'good' : 'danger',
        'Шаги пастеризации',
        steps.length > 0
            ? `Подготовлено ${steps.length} шаг(ов).`
            : 'Нужен хотя бы один корректный шаг или пауза с длительностью.',
        steps.length > 0 ? 'hold-steps' : 'hold-add-step-button'
    );

    addChecklistItem(
        items,
        activeTempSteps.length > 0 ? 'good' : 'warn',
        'Температурные ступени',
        activeTempSteps.length > 0
            ? `Есть ${activeTempSteps.length} температурных шаг(ов) с активным нагревом.`
            : 'Сейчас заданы только паузы без температуры. Это допустимо, но проверьте сценарий.',
        'hold-steps'
    );
}

function buildChecklistItems() {
    const availability = getStartAvailabilityState(runtimeMonitorState);
    const items = [];

    addChecklistItem(items, availability.tone, availability.title, availability.detail);

    switch (selectedControlMode) {
        case 'rectification':
            buildRectificationChecklist(items);
            break;
        case 'manual':
            buildManualChecklist(items);
            break;
        case 'distillation':
            buildDistillationChecklist(items);
            break;
        case 'nbk':
            buildNbkChecklist(items);
            break;
        case 'fermentation':
            buildFermentationChecklist(items);
            break;
        case 'mashing':
            buildMashingChecklist(items);
            break;
        case 'hold':
            buildHoldChecklist(items);
            break;
        default:
            break;
    }

    return items;
}

function buildSelectedModePreflightPayload() {
    switch (selectedControlMode) {
        case 'rectification':
            return {
                mode: 'rectification',
                params: collectRectificationModalSettings()
            };
        case 'manual':
            return {
                mode: 'manual_rect',
                params: collectManualRectSettings()
            };
        case 'distillation':
            return {
                mode: 'distillation',
                params: collectDistillationSettings()
            };
        case 'mashing':
            return {
                mode: 'mashing',
                params: {
                    profile: {
                        name: getStringValue('mash-profile-name', 'Mashing').trim() || 'Mashing',
                        steps: readStepsFromUI('mash-steps', 'mash')
                    }
                }
            };
        case 'hold':
            return {
                mode: 'hold',
                params: {
                    steps: readStepsFromUI('hold-steps', 'hold')
                }
            };
        case 'nbk':
            return {
                mode: 'nbk',
                params: collectNbkSettings()
            };
        case 'fermentation':
            return {
                mode: 'fermentation',
                params: collectFermentationSettings()
            };
        default:
            return null;
    }
}

function mapPreflightItemTarget(id) {
    switch (id) {
        case 'rect-profile':
            return 'rect-start-feed-volume';
        case 'manual-profile':
            return 'manual-feed-volume';
        case 'dist-profile':
            return 'dist-start-power-percent';
        case 'mash-profile':
            return 'mash-steps';
        case 'hold-profile':
            return 'hold-steps';
        case 'nbk-profile':
            return 'nbk-power-w';
        case 'fermentation-profile':
            return 'ferm-target-temp';
        case 'profile':
            if (selectedControlMode === 'rectification') return 'rect-start-feed-volume';
            if (selectedControlMode === 'manual') return 'manual-feed-volume';
            if (selectedControlMode === 'distillation') return 'dist-start-power-percent';
            if (selectedControlMode === 'mashing') return 'mash-steps';
            return '';
        case 'rect-takeoff':
            return 'rect-start-takeoff-backend';
        case 'manual-takeoff':
            return 'rect-start-takeoff-backend';
        case 'valves':
            return 'monitor-diagnostics-panel';
        case 'baro':
            return selectedControlMode === 'rectification'
                ? 'rect-start-baro-correction-enabled'
                : '';
        case 'booster':
            if (selectedControlMode === 'rectification') return 'rect-start-booster-enabled';
            if (selectedControlMode === 'distillation') return 'dist-start-booster-enabled';
            if (selectedControlMode === 'nbk') return 'nbk-booster-enabled';
            return '';
        case 'water':
            return 'indicator-cooling-margin';
        case 'sensors':
            return 'indicator-sensor-freshness';
        case 'cooling':
            return 'indicator-cooling-margin';
        case 'pressure':
            return 'indicator-pressure-stable';
        case 'stirrer':
            return 'monitor-stirrer-card';
        default:
            return '';
    }
}

function scheduleModePreflightRefresh() {
    clearModePreflightRefreshTimer();
    preflightRefreshTimer = window.setTimeout(() => {
        preflightRefreshTimer = 0;
        void refreshModePreflight({ silent: true });
    }, 180);
}

async function refreshModePreflight(options = {}) {
    const { silent = false } = options;
    const payload = buildSelectedModePreflightPayload();
    if (!payload) {
        latestModePreflight = null;
        renderControlStartState();
        return null;
    }

    try {
        const response = await fetch('/api/process/preflight', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const snapshot = await response.json();
        if (!response.ok || !snapshot?.success) {
            throw new Error(snapshot?.message || `HTTP ${response.status}`);
        }

        latestModePreflight = snapshot;
        setPreflightState(snapshot.title, snapshot.detail, snapshot.tone, snapshot.checks || {});
        renderControlStartState();
        return snapshot;
    } catch (error) {
        latestModePreflight = null;
        renderRuntimePreflightFallback();
        renderControlStartState();
        if (!silent) {
            addLog(`Не удалось получить backend preflight: ${error?.message || error}`, 'warning');
        }
        return null;
    }
}

export function renderControlStartChecklist() {
    const root = byId('mode-start-checklist');
    const summaryEl = byId('mode-start-checklist-summary');
    const itemsEl = byId('mode-start-checklist-items');
    if (!root || !summaryEl || !itemsEl) return;

    const preflightItems = Array.isArray(latestModePreflight?.items)
        ? latestModePreflight.items.map((item) => ({
            tone: item.tone || 'muted',
            title: item.title || item.id || 'Проверка',
            detail: item.detail || '',
            targetId: mapPreflightItemTarget(item.id || ''),
            blocking: Boolean(item.blocking)
        }))
        : null;
    const items = preflightItems || buildChecklistItems();
    const dangerCount = preflightItems
        ? Number(latestModePreflight?.blockingCount || 0)
        : items.filter((item) => item.tone === 'danger').length;
    const warnCount = preflightItems
        ? Number(latestModePreflight?.warningCount || 0)
        : items.filter((item) => item.tone === 'warn').length;
    const rootTone = dangerCount > 0 ? 'danger' : (warnCount > 0 ? 'warn' : 'good');

    root.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
    root.classList.add(`is-${rootTone}`);
    summaryEl.textContent = dangerCount > 0
        ? `${dangerCount} критичных пункта, ${warnCount} предупреждений`
        : (warnCount > 0 ? `${warnCount} пункт(ов) требуют внимания` : 'Все основные проверки выглядят нормально');

    if (preflightItems) {
        summaryEl.textContent = dangerCount > 0
            ? `${dangerCount} критичных пункта, ${warnCount} предупреждений`
            : (warnCount > 0 ? `${warnCount} пункт(ов) требуют внимания` : 'Backend preflight не видит критичных блокировок');
    }

    itemsEl.innerHTML = '';
    items.forEach((item) => {
        const row = document.createElement('div');
        row.className = `control-start-checklist-item is-${item.tone}`;

        const copy = document.createElement('div');
        copy.className = 'control-start-checklist-copy';

        const title = document.createElement('strong');
        title.textContent = item.title;
        const detail = document.createElement('span');
        detail.textContent = item.detail;
        copy.append(title, detail);
        row.appendChild(copy);

        if (item.targetId) {
            const action = document.createElement('button');
            action.type = 'button';
            action.className = 'btn btn-secondary btn-sm';
            action.textContent = item.tone === 'good' ? 'Открыть' : 'Исправить';
            action.addEventListener('click', () => focusChecklistTarget(item.targetId));
            row.appendChild(action);
        }

        itemsEl.appendChild(row);
    });
}

function normalizeControlPanelMarkup() {
    const titles = {
        rectification: 'Авто-ректификация',
        manual: 'Ручная ректификация',
        distillation: 'Дистилляция',
        nbk: 'НБК',
        fermentation: 'Ферментация',
        mashing: 'Затирка',
        hold: 'Пастеризация'
    };

    document.querySelectorAll('[data-mode-select]').forEach((button) => {
        const mode = button.dataset.modeSelect;
        button.classList.remove('btn-success', 'btn-warning', 'btn-info');
        button.classList.add('btn');
        if (titles[mode]) {
            button.textContent = titles[mode];
        }
    });

    const historyType = document.getElementById('history-filter-type');
    if (historyType) {
        historyType.innerHTML = `
            <option value="all">Все типы</option>
            <option value="rectification">Ректификация</option>
            <option value="distillation">Дистилляция</option>
            <option value="nbk">НБК</option>
            <option value="fermentation">Ферментация</option>
            <option value="mashing">Затирка</option>
            <option value="hold">Пастеризация</option>
        `;
        historyType.closest('div')?.classList.add('history-toolbar');
    }

    const pumpControl = document.getElementById('pump-speed-control')?.closest('.control-group');
    const bodyDecrementField = document.getElementById('manual-body-speed-decrement')?.closest('.form-group');
    if (pumpControl && bodyDecrementField && !document.getElementById('manual-body-pump-hint')) {
        pumpControl.style.gridColumn = '1 / -1';
        const hint = document.createElement('div');
        hint.id = 'manual-body-pump-hint';
        hint.className = 'control-inline-hint';
        hint.textContent = 'Ручная подача для отбора тела и сервисной настройки потока.';
        pumpControl.appendChild(hint);
        bodyDecrementField.insertAdjacentElement('afterend', pumpControl);
    }

    document.querySelector('#control-panel-manual .controls button[onclick*="toggleValve"]')?.closest('.control-group')?.remove();

    const holdTitle = document.querySelector('#control-panel-hold .control-subsection-title');
    if (holdTitle) {
        holdTitle.textContent = 'Пастеризация';
    }

    const holdLabel = document.querySelector('#control-panel-hold .form-group > label');
    if (holdLabel) {
        holdLabel.textContent = 'Шаги (температура °C, длительность мин)';
    }

    const holdSteps = document.getElementById('hold-steps');
    if (holdSteps && !document.getElementById('hold-steps-hint')) {
        const hint = document.createElement('div');
        hint.id = 'hold-steps-hint';
        hint.className = 'control-inline-hint';
        hint.textContent = 'Если температура не указана, это пауза без нагрева. Охлаждение используется только для шага с температурой ниже предыдущего.';
        holdSteps.insertAdjacentElement('beforebegin', hint);
    }
}

async function ensureRectificationSettingsLoaded() {
    if (rectSettingsLoaded) return;
    const loaded = await loadRectificationStartSettings();
    rectSettingsLoaded = Boolean(loaded);
}
async function ensureDistillationSettingsLoaded() {
    if (distillationSettingsLoaded) return;
    const loaded = await loadDistillationStartSettings();
    distillationSettingsLoaded = Boolean(loaded);
}
async function ensureNbkSettingsLoaded() {
    if (nbkSettingsLoaded) return;
    const loaded = await loadNbkSettings();
    nbkSettingsLoaded = Boolean(loaded);
}
async function ensureFermentationSettingsLoaded() {
    if (fermentationSettingsLoaded) return;
    const loaded = await loadFermentationSettings();
    fermentationSettingsLoaded = Boolean(loaded);
}

function goToMonitorTab() {
    const monitorTab = document.querySelector('.tab[data-tab="monitor"]');
    if (monitorTab) monitorTab.click();
    window.requestAnimationFrame(() => {
        window.scrollTo({ top: 0, left: 0, behavior: 'smooth' });
    });
}

export async function selectControlMode(mode, options = {}) {
    const normalized = CONTROL_MODES[mode] ? mode : 'rectification';
    selectedControlMode = normalized;
    latestModePreflight = null;
    clearModePreflightRefreshTimer();

    renderControlModeSelector(normalized);
    renderControlModePanels(normalized);
    renderControlModeHeader(normalized);

    if (normalized === 'rectification') {
        await ensureRectificationSettingsLoaded();
    } else if (normalized === 'distillation') {
        await ensureDistillationSettingsLoaded();
    } else if (normalized === 'nbk') {
        await ensureNbkSettingsLoaded();
    } else if (normalized === 'fermentation') {
        await ensureFermentationSettingsLoaded();
    } else if (normalized === 'mashing') {
        syncActiveMashingProfileToPanel();
    }

    if (options.persist !== false) {
        persistModeSelection(normalized);
    }

    renderControlStartState();
}

export function getSelectedControlMode() {
    return selectedControlMode;
}

async function performSelectedModeStart() {
    if (!ensureSelectedModeSupported()) {
        return false;
    }

    if (currentMode !== MODE_IDLE) {
        addLog('Сначала остановите текущий процесс, затем запускайте новый режим.', 'warning');
        renderControlStartState();
        return false;
    }

    const availability = getStartAvailabilityState(runtimeMonitorState);
    if (availability.disabled) {
        addLog(`Запуск заблокирован: ${availability.detail}`, 'warning');
        renderControlStartState();
        return false;
    }

    let started;

    switch (selectedControlMode) {
        case 'rectification':
            started = await startRectification();
            break;
        case 'manual':
            saveManualRectSettings({ silent: true });
            started = await startManual();
            break;
        case 'distillation':
            started = await startDistillation(collectDistillationSettings());
            break;
        case 'mashing':
            started = await startMashing();
            break;
        case 'hold':
            started = await startHold();
            break;
        case 'nbk':
            started = await startNbk();
            break;
        case 'fermentation':
            started = await startFermentation();
            break;
        default:
            addLog('Не выбран режим запуска', 'warning');
            return false;
    }

    if (started) {
        goToMonitorTab();
        setTimeout(loadStatus, 250);
    }

    renderControlStartState();

    return started;
}

export async function confirmModeStart() {
    if (!ensureSelectedModeSupported()) {
        return false;
    }

    const snapshot = await refreshModePreflight();
    if (!snapshot) {
        addLog('Не удалось подтвердить условия старта на контроллере. Повторите попытку.', 'warning');
        return false;
    }
    if (!snapshot.ready) {
        addLog(`Запуск заблокирован: ${snapshot.detail}`, 'warning');
        return false;
    }

    const started = await performSelectedModeStart();
    if (started) {
        closeModeStartModal();
    }
    return started;
}

export async function startSelectedMode() {
    if (!ensureSelectedModeSupported()) {
        return false;
    }

    if (currentMode !== MODE_IDLE) {
        addLog('Сначала остановите текущий процесс, затем запускайте новый режим.', 'warning');
        renderControlStartState();
        return false;
    }

    const availability = getStartAvailabilityState(runtimeMonitorState);
    if (availability.disabled) {
        addLog(`Запуск заблокирован: ${availability.detail}`, 'warning');
        renderControlStartState();
        return false;
    }

    latestModePreflight = null;
    renderControlStartState();
    openModeStartModal();
    renderRuntimePreflightFallback();
    await refreshModePreflight();
    return false;
}

export async function initControlModePanel() {
    if (!document.getElementById('control-mode-panel')) {
        return;
    }

    initManualRectSettings();
    normalizeControlPanelMarkup();

    let initialMode = 'rectification';
    try {
        const saved = localStorage.getItem(CONTROL_MODE_STORAGE_KEY);
        if (saved && CONTROL_MODES[saved]) {
            initialMode = saved;
        }
    } catch {
        // ignore storage failures
    }

    const panel = document.getElementById('control-mode-panel');
    if (panel) {
        const handlePanelEdit = () => {
            latestModePreflight = null;
            renderControlStartState();
            if (isModeStartModalOpen()) {
                scheduleModePreflightRefresh();
            }
        };
        panel.addEventListener('input', handlePanelEdit);
        panel.addEventListener('change', handlePanelEdit);
    }

    ['mash-steps', 'hold-steps'].forEach((id) => {
        const target = document.getElementById(id);
        if (!target) return;
        const observer = new MutationObserver(() => {
            latestModePreflight = null;
            renderControlStartState();
            if (isModeStartModalOpen()) {
                scheduleModePreflightRefresh();
            }
        });
        observer.observe(target, { childList: true, subtree: true });
    });

    const startModal = byId('mode-start-modal');
    if (startModal) {
        startModal.addEventListener('click', (event) => {
            if (event.target === startModal) closeModeStartModal();
        });
    }

    document.addEventListener('runtime-status-updated', () => {
        renderControlStartState();
    });

    await selectControlMode(initialMode, { persist: false });
    renderControlStartState();
}

function byId(id) {
    return document.getElementById(id);
}

function setInputValue(id, value) {
    const el = byId(id);
    if (!el || value === undefined || value === null) return;
    if (el.type === 'checkbox') {
        el.checked = Boolean(value);
        return;
    }
    el.value = String(value);
}

function getNumberValue(id, fallback) {
    const parsed = Number(byId(id)?.value);
    return Number.isFinite(parsed) ? parsed : fallback;
}

function getStringValue(id, fallback) {
    const value = byId(id)?.value;
    return typeof value === 'string' && value.length > 0 ? value : fallback;
}

function getCheckboxValue(id) {
    return Boolean(byId(id)?.checked);
}

export function calcManualHeadsSpeed() {
    const volume = getNumberValue('manual-heads-volume', 50);
    const time = Math.max(1, getNumberValue('manual-heads-time', 180));
    const speed = (volume / time) * 60;
    const out = byId('manual-heads-calc-speed');
    if (out) out.textContent = speed.toFixed(1);
}

export function calcManualHeadsTime() {
    const volume = getNumberValue('manual-heads-volume', 50);
    const speed = Math.max(1, getNumberValue('manual-heads-speed', 300));
    const time = (volume / speed) * 60;
    const out = byId('manual-heads-calc-time');
    if (out) out.textContent = time.toFixed(0);
}

export function updateManualHeadsMode() {
    const mode = getStringValue('manual-heads-mode', 'time');
    const timeGroup = byId('manual-heads-time-group');
    const speedGroup = byId('manual-heads-speed-group');
    const tempGroup = byId('manual-heads-temp-group');
    if (timeGroup) timeGroup.style.display = mode === 'time' ? '' : 'none';
    if (speedGroup) speedGroup.style.display = mode === 'speed' ? '' : 'none';
    if (tempGroup) tempGroup.style.display = mode === 'temp' ? '' : 'none';

    if (mode === 'time') calcManualHeadsSpeed();
    if (mode === 'speed') calcManualHeadsTime();
}

export function updateManualBodyToTailsMode() {
    const mode = getStringValue('manual-body-to-tails-mode', 'stabilize');
    const stabilizeGroup = byId('manual-body-stabilize-group');
    const tempGroup = byId('manual-body-temp-group');
    if (stabilizeGroup) stabilizeGroup.style.display = (mode === 'stabilize' || mode === 'both') ? '' : 'none';
    if (tempGroup) tempGroup.style.display = (mode === 'temp' || mode === 'both') ? '' : 'none';
}

export function updateManualTailsMode() {
    const tailsSettings = byId('manual-tails-settings');
    if (tailsSettings) tailsSettings.style.display = getCheckboxValue('manual-tails-enabled') ? '' : 'none';
}

export function updateManualTailsStopMode() {
    const mode = getStringValue('manual-tails-stop-mode', 'temp');
    const tempGroup = byId('manual-tails-temp-group');
    const abvGroup = byId('manual-tails-abv-group');
    if (tempGroup) tempGroup.style.display = mode === 'temp' ? '' : 'none';
    if (abvGroup) abvGroup.style.display = mode === 'abv' ? '' : 'none';
}

export function updateManualTailsPwmMode() {
    const pwmSettings = byId('manual-tails-pwm-settings');
    if (pwmSettings) pwmSettings.style.display = getCheckboxValue('manual-tails-pwm-enabled') ? '' : 'none';
}

export function syncManualFeedVolumeLimit() {
    const input = byId('manual-feed-volume');
    if (!input) return;
    const maxFeedVolumeL = Math.max(1, Math.min(250, getCubeVolumeLimitL()));
    input.max = String(maxFeedVolumeL);
    input.value = String(clampFeedVolumeToCube(input.value, Number(input.value) || 20));
}

function collectManualRectSettings() {
    return {
        feed: {
            volumeL: clampFeedVolumeToCube(getNumberValue('manual-feed-volume', 20), 20),
            abvPercent: getNumberValue('manual-feed-abv', 40)
        },
        heads: {
            volume: getNumberValue('manual-heads-volume', 50),
            mode: getStringValue('manual-heads-mode', 'time'),
            time: getNumberValue('manual-heads-time', 180),
            speed: getNumberValue('manual-heads-speed', 300),
            temp: getNumberValue('manual-heads-temp', 78.5)
        },
        body: {
            spikeThreshold: getNumberValue('manual-body-spike-threshold', 0.2),
            speedDecrement: getNumberValue('manual-body-speed-decrement', 5),
            toTailsMode: getStringValue('manual-body-to-tails-mode', 'stabilize'),
            stabilizeTimeout: getNumberValue('manual-body-stabilize-timeout', 10),
            toTailsTemp: getNumberValue('manual-body-to-tails-temp', 92)
        },
        tails: {
            enabled: getCheckboxValue('manual-tails-enabled'),
            stopMode: getStringValue('manual-tails-stop-mode', 'temp'),
            stopTemp: getNumberValue('manual-tails-stop-temp', 93),
            stopAbv: getNumberValue('manual-tails-stop-abv', 40),
            pwmEnabled: getCheckboxValue('manual-tails-pwm-enabled'),
            pwmDuty: getNumberValue('manual-tails-pwm-duty', 30),
            pwmPeriod: getNumberValue('manual-tails-pwm-period', 10)
        }
    };
}

function applyManualRectSettings(settings) {
    const feed = settings?.feed || {};
    const heads = settings?.heads || {};
    const body = settings?.body || {};
    const tails = settings?.tails || {};

    const manualFeedVolumeInput = byId('manual-feed-volume');
    if (manualFeedVolumeInput) {
        const maxFeedVolumeL = Math.max(1, Math.min(250, getCubeVolumeLimitL()));
        manualFeedVolumeInput.max = String(maxFeedVolumeL);
        manualFeedVolumeInput.value = String(clampFeedVolumeToCube(feed.volumeL ?? 20, 20));
    }
    setInputValue('manual-feed-abv', feed.abvPercent ?? 40);

    setInputValue('manual-heads-volume', heads.volume ?? 50);
    setInputValue('manual-heads-mode', heads.mode ?? 'time');
    setInputValue('manual-heads-time', heads.time ?? 180);
    setInputValue('manual-heads-speed', heads.speed ?? 300);
    setInputValue('manual-heads-temp', heads.temp ?? 78.5);

    setInputValue('manual-body-spike-threshold', body.spikeThreshold ?? 0.2);
    setInputValue('manual-body-speed-decrement', body.speedDecrement ?? 5);
    setInputValue('manual-body-to-tails-mode', body.toTailsMode ?? 'stabilize');
    setInputValue('manual-body-stabilize-timeout', body.stabilizeTimeout ?? 10);
    setInputValue('manual-body-to-tails-temp', body.toTailsTemp ?? 92);

    setInputValue('manual-tails-enabled', tails.enabled ?? false);
    setInputValue('manual-tails-stop-mode', tails.stopMode ?? 'temp');
    setInputValue('manual-tails-stop-temp', tails.stopTemp ?? 93);
    setInputValue('manual-tails-stop-abv', tails.stopAbv ?? 40);
    setInputValue('manual-tails-pwm-enabled', tails.pwmEnabled ?? false);
    setInputValue('manual-tails-pwm-duty', tails.pwmDuty ?? 30);
    setInputValue('manual-tails-pwm-period', tails.pwmPeriod ?? 10);

    const dutyValue = byId('manual-tails-pwm-duty-val');
    if (dutyValue) dutyValue.textContent = `${getNumberValue('manual-tails-pwm-duty', 30)}%`;

    updateManualHeadsMode();
    updateManualBodyToTailsMode();
    updateManualTailsMode();
    updateManualTailsStopMode();
    updateManualTailsPwmMode();
}

export function saveManualRectSettings(options = {}) {
    const { silent = false } = options;
    const settings = collectManualRectSettings();
    try {
        localStorage.setItem(MANUAL_RECT_STORAGE_KEY, JSON.stringify(settings));
        if (!silent) addLog('Параметры ручной ректификации сохранены', 'info');
    } catch {
        if (!silent) addLog('Не удалось сохранить параметры ручной ректификации', 'warning');
    }
}

export function loadManualRectSettings(options = {}) {
    const { silent = false } = options;
    try {
        const raw = localStorage.getItem(MANUAL_RECT_STORAGE_KEY);
        if (!raw) {
            applyManualRectSettings({});
            return;
        }
        applyManualRectSettings(JSON.parse(raw));
        if (!silent) addLog('Параметры ручной ректификации загружены', 'info');
    } catch {
        applyManualRectSettings({});
        if (!silent) addLog('Ошибка загрузки параметров ручной ректификации', 'warning');
    }
}

export function initManualRectSettings() {
    if (manualRectInitialized || !byId('manual-heads-mode')) return;
    manualRectInitialized = true;

    const headsVolume = byId('manual-heads-volume');
    if (headsVolume) headsVolume.addEventListener('input', calcManualHeadsSpeed);

    const pwmDuty = byId('manual-tails-pwm-duty');
    if (pwmDuty) {
        pwmDuty.addEventListener('input', () => {
            const dutyValue = byId('manual-tails-pwm-duty-val');
            if (dutyValue) dutyValue.textContent = `${pwmDuty.value}%`;
        });
    }

    loadManualRectSettings({ silent: true });
    syncManualFeedVolumeLimit();
}
