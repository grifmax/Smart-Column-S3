import { runtimeMonitorState } from '../globals.js';

const PROFILE_CATEGORY_MODE_KEYS = Object.freeze({
    rectification: 'rectification',
    distillation: 'distillation',
    mashing: 'mashing'
});

const PROFILE_CATEGORY_LABELS = Object.freeze({
    rectification: 'Ректификация',
    distillation: 'Дистилляция',
    mashing: 'Затирка'
});

export function getProfileCategory(profile = {}) {
    return String(profile?.metadata?.category || profile?.category || '').trim().toLowerCase();
}

export function getProfileCompatibility(profile = {}, state = runtimeMonitorState) {
    const category = getProfileCategory(profile);
    const modeKey = PROFILE_CATEGORY_MODE_KEYS[category];
    const categoryLabel = PROFILE_CATEGORY_LABELS[category] || category || 'Профиль';

    if (!modeKey) {
        return {
            category,
            categoryLabel,
            modeKey: '',
            known: false,
            supported: true,
            reason: ''
        };
    }

    const support = state?.equipment?.supportedModes?.[modeKey];
    if (!support || typeof support.supported !== 'boolean') {
        return {
            category,
            categoryLabel,
            modeKey,
            known: false,
            supported: true,
            reason: ''
        };
    }

    return {
        category,
        categoryLabel,
        modeKey,
        known: true,
        supported: Boolean(support.supported),
        reason: String(support.reason || '').trim()
    };
}

export function getProfileCompatibilityBadge(profile = {}, state = runtimeMonitorState) {
    const compatibility = getProfileCompatibility(profile, state);

    if (!compatibility.known) {
        return {
            tone: 'muted',
            label: 'Комплектация не проверена',
            detail: ''
        };
    }

    if (compatibility.supported) {
        return {
            tone: 'good',
            label: 'Совместим с текущим железом',
            detail: ''
        };
    }

    return {
        tone: 'warn',
        label: 'Нужна другая комплектация',
        detail: compatibility.reason || 'Для этого профиля не хватает обязательных датчиков.'
    };
}
