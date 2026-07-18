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

const TOPOLOGY_LABELS = Object.freeze({
    cube: 'куб',
    columnBottom: 'низ царги',
    columnTop: 'верх царги',
    reflux: 'дефлегматор',
    waterIn: 'вода вход',
    waterOut: 'вода выход',
    tsa: 'ЦП'
});

const HARDWARE_FLAG_LABELS = Object.freeze({
    boosterHeaterEnabled: 'разгонный ТЭН',
    coolingPwmEnabled: 'PWM охлаждения',
    bodyLevelSensorEnabled: 'контроль уровня тела',
    leakSensorEnabled: 'контроль протечки'
});

const SAFETY_CHANNEL_LABELS = Object.freeze({
    bodyLevel: 'канал уровня тела',
    leak: 'канал протечки'
});

function asObject(value) {
    if (!value || typeof value !== 'object' || Array.isArray(value)) {
        return null;
    }
    return value;
}

function asBoolean(value) {
    return value === true;
}

function formatBusSource(equipment = {}) {
    const label = String(equipment?.temperatureBusSourceLabel || '').trim();
    if (label) {
        return label;
    }

    if (equipment?.useDs2482ForTemps) {
        const address = Number(equipment?.ds2482Address);
        return Number.isFinite(address)
            ? `DS2482 0x${address.toString(16).toUpperCase().padStart(2, '0')}`
            : 'DS2482';
    }

    const pin = Number(equipment?.tempBusGpioPin);
    return Number.isFinite(pin) ? `GPIO ${pin}` : 'GPIO 1-Wire';
}

function formatBooleanState(value) {
    return asBoolean(value) ? 'вкл.' : 'выкл.';
}

function formatModuleState(module = {}, fallbackLabel = '') {
    const label = String(module?.label || fallbackLabel || '').trim();
    const state = asBoolean(module?.available) ? 'доступен' : 'недоступен';
    return label ? `${label} ${state}` : state;
}

function parseProfileEquipmentSnapshot(profile = {}) {
    const raw = String(profile?.validation?.equipmentSnapshotJson || '').trim();
    if (!raw) {
        return null;
    }

    try {
        return asObject(JSON.parse(raw));
    } catch (error) {
        console.warn('Не удалось разобрать equipmentSnapshotJson:', error);
        return null;
    }
}

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

export function getProfileEquipmentMismatch(profile = {}, state = runtimeMonitorState) {
    const snapshot = parseProfileEquipmentSnapshot(profile);
    const currentEquipment = asObject(state?.equipment);
    const compatibility = getProfileCompatibility(profile, state);
    const messages = [];

    if (!snapshot || !currentEquipment) {
        return {
            known: false,
            changed: false,
            summary: '',
            messages: []
        };
    }

    const snapshotSupport = asObject(snapshot?.supportedModes)?.[compatibility.modeKey];
    const currentSupport = asObject(currentEquipment?.supportedModes)?.[compatibility.modeKey];
    if (compatibility.modeKey && snapshotSupport && currentSupport) {
        const wasSupported = asBoolean(snapshotSupport.supported);
        const isSupported = asBoolean(currentSupport.supported);
        if (wasSupported !== isSupported) {
            messages.push(
                `поддержка режима "${compatibility.categoryLabel}" изменилась: было ${wasSupported ? 'доступно' : 'недоступно'}, сейчас ${isSupported ? 'доступно' : 'недоступно'}${currentSupport.reason ? ` (${currentSupport.reason})` : ''}`
            );
        }
    }

    const snapshotTopology = asObject(snapshot?.temperatureTopology);
    const currentTopology = asObject(currentEquipment?.temperatureTopology);
    if (snapshotTopology && currentTopology) {
        const missingSensors = [];
        const addedSensors = [];
        Object.entries(TOPOLOGY_LABELS).forEach(([key, label]) => {
            const hadSensor = asBoolean(snapshotTopology[key]);
            const hasSensor = asBoolean(currentTopology[key]);
            if (hadSensor && !hasSensor) {
                missingSensors.push(label);
            } else if (!hadSensor && hasSensor) {
                addedSensors.push(label);
            }
        });
        if (missingSensors.length) {
            messages.push(`нет датчиков, которые были при валидации: ${missingSensors.join(', ')}`);
        }
        if (addedSensors.length) {
            messages.push(`появились дополнительные датчики: ${addedSensors.join(', ')}`);
        }
    }

    if (formatBusSource(snapshot) !== formatBusSource(currentEquipment)) {
        messages.push(
            `изменилась 1-Wire шина: было ${formatBusSource(snapshot)}, сейчас ${formatBusSource(currentEquipment)}`
        );
    }

    Object.entries(HARDWARE_FLAG_LABELS).forEach(([key, label]) => {
        const snapshotValue = asBoolean(snapshot?.[key]);
        const currentValue = asBoolean(currentEquipment?.[key]);
        if (snapshotValue !== currentValue) {
            messages.push(`${label}: было ${formatBooleanState(snapshotValue)}, сейчас ${formatBooleanState(currentValue)}`);
        }
    });

    if (Number(snapshot?.heaterPowerW || 0) !== Number(currentEquipment?.heaterPowerW || 0)) {
        messages.push(
            `основная мощность нагрева: было ${Number(snapshot?.heaterPowerW || 0)} Вт, сейчас ${Number(currentEquipment?.heaterPowerW || 0)} Вт`
        );
    }

    if (Number(snapshot?.columnHeightMm || 0) !== Number(currentEquipment?.columnHeightMm || 0)) {
        messages.push(
            `высота царги: было ${Number(snapshot?.columnHeightMm || 0)} мм, сейчас ${Number(currentEquipment?.columnHeightMm || 0)} мм`
        );
    }

    if (String(snapshot?.packingType || '').trim() !== String(currentEquipment?.packingType || '').trim()) {
        messages.push(
            `насадка: было ${String(snapshot?.packingType || '-')}, сейчас ${String(currentEquipment?.packingType || '-')}`
        );
    }

    const snapshotModules = asObject(snapshot?.modules);
    const currentModules = asObject(currentEquipment?.modules);
    if (snapshotModules && currentModules) {
        const moduleChanges = [];
        Object.keys(snapshotModules).forEach((key) => {
            const savedModule = asObject(snapshotModules[key]);
            const currentModule = asObject(currentModules[key]);
            if (!savedModule || !currentModule) {
                return;
            }
            if (asBoolean(savedModule.available) !== asBoolean(currentModule.available)) {
                moduleChanges.push(
                    `${formatModuleState(savedModule, key)} -> ${formatModuleState(currentModule, key)}`
                );
            }
        });
        if (moduleChanges.length) {
            messages.push(`по модулям есть отличия: ${moduleChanges.slice(0, 4).join('; ')}`);
        }
    }

    const snapshotSafety = asObject(snapshot?.safetyChannels);
    const currentSafety = asObject(currentEquipment?.safetyChannels);
    if (snapshotSafety && currentSafety) {
        const safetyChanges = [];
        Object.entries(SAFETY_CHANNEL_LABELS).forEach(([key, label]) => {
            const savedChannel = asObject(snapshotSafety[key]);
            const currentChannel = asObject(currentSafety[key]);
            if (!savedChannel || !currentChannel) {
                return;
            }
            if (asBoolean(savedChannel.enabled) !== asBoolean(currentChannel.enabled)) {
                safetyChanges.push(
                    `${label}: было ${formatBooleanState(savedChannel.enabled)}, сейчас ${formatBooleanState(currentChannel.enabled)}`
                );
            } else if (asBoolean(savedChannel.available) !== asBoolean(currentChannel.available)) {
                safetyChanges.push(
                    `${label}: было ${asBoolean(savedChannel.available) ? 'доступно' : 'недоступно'}, сейчас ${asBoolean(currentChannel.available) ? 'доступно' : 'недоступно'}`
                );
            }
        });
        if (safetyChanges.length) {
            messages.push(`по safety-каналам есть отличия: ${safetyChanges.join('; ')}`);
        }
    }

    return {
        known: true,
        changed: messages.length > 0,
        summary: messages.length ? 'Профиль валидировался на другой конфигурации железа.' : '',
        messages
    };
}
