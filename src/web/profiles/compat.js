import { runtimeMonitorState } from '../globals.js';

const PROFILE_CATEGORY_MODE_KEYS = Object.freeze({
    rectification: 'rectification',
    distillation: 'distillation',
    mashing: 'mashing'
});

const PROFILE_CATEGORY_LABELS = Object.freeze({
    rectification: 'Р РµРєС‚РёС„РёРєР°С†РёСЏ',
    distillation: 'Р”РёСЃС‚РёР»Р»СЏС†РёСЏ',
    mashing: 'Р—Р°С‚РёСЂРєР°'
});

const TOPOLOGY_LABELS = Object.freeze({
    cube: 'РєСѓР±',
    columnBottom: 'РЅРёР· С†Р°СЂРіРё',
    columnTop: 'РІРµСЂС… С†Р°СЂРіРё',
    reflux: 'РґРµС„Р»РµРіРјР°С‚РѕСЂ',
    waterIn: 'РІРѕРґР° РІС…РѕРґ',
    waterOut: 'РІРѕРґР° РІС‹С…РѕРґ',
    tsa: 'Р¦Рџ'
});

const HARDWARE_FLAG_LABELS = Object.freeze({
    boosterHeaterEnabled: 'СЂР°Р·РіРѕРЅРЅС‹Р№ РўР­Рќ',
    coolingPwmEnabled: 'PWM РѕС…Р»Р°Р¶РґРµРЅРёСЏ',
    bodyLevelSensorEnabled: 'РєРѕРЅС‚СЂРѕР»СЊ СѓСЂРѕРІРЅСЏ С‚РµР»Р°',
    leakSensorEnabled: 'РєРѕРЅС‚СЂРѕР»СЊ РїСЂРѕС‚РµС‡РєРё'
});

const SAFETY_CHANNEL_LABELS = Object.freeze({
    bodyLevel: 'РєР°РЅР°Р» СѓСЂРѕРІРЅСЏ С‚РµР»Р°',
    leak: 'РєР°РЅР°Р» РїСЂРѕС‚РµС‡РєРё'
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
    return asBoolean(value) ? 'РІРєР».' : 'РІС‹РєР».';
}

function formatModuleState(module = {}, fallbackLabel = '') {
    const label = String(module?.label || fallbackLabel || '').trim();
    const state = asBoolean(module?.available) ? 'РґРѕСЃС‚СѓРїРµРЅ' : 'РЅРµРґРѕСЃС‚СѓРїРµРЅ';
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
        console.warn('РќРµ СѓРґР°Р»РѕСЃСЊ СЂР°Р·РѕР±СЂР°С‚СЊ equipmentSnapshotJson:', error);
        return null;
    }
}

export function getProfileCategory(profile = {}) {
    return String(profile?.metadata?.category || profile?.category || '').trim().toLowerCase();
}

export function getProfileCompatibility(profile = {}, state = runtimeMonitorState) {
    const category = getProfileCategory(profile);
    const modeKey = PROFILE_CATEGORY_MODE_KEYS[category];
    const categoryLabel = PROFILE_CATEGORY_LABELS[category] || category || 'РџСЂРѕС„РёР»СЊ';

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
            label: 'РљРѕРјРїР»РµРєС‚Р°С†РёСЏ РЅРµ РїСЂРѕРІРµСЂРµРЅР°',
            detail: ''
        };
    }

    if (compatibility.supported) {
        return {
            tone: 'good',
            label: 'РЎРѕРІРјРµСЃС‚РёРј СЃ С‚РµРєСѓС‰РёРј Р¶РµР»РµР·РѕРј',
            detail: ''
        };
    }

    return {
        tone: 'warn',
        label: 'РќСѓР¶РЅР° РґСЂСѓРіР°СЏ РєРѕРјРїР»РµРєС‚Р°С†РёСЏ',
        detail: compatibility.reason || 'Р”Р»СЏ СЌС‚РѕРіРѕ РїСЂРѕС„РёР»СЏ РЅРµ С…РІР°С‚Р°РµС‚ РѕР±СЏР·Р°С‚РµР»СЊРЅС‹С… РґР°С‚С‡РёРєРѕРІ.'
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
                `РїРѕРґРґРµСЂР¶РєР° СЂРµР¶РёРјР° "${compatibility.categoryLabel}" РёР·РјРµРЅРёР»Р°СЃСЊ: Р±С‹Р»Рѕ ${wasSupported ? 'РґРѕСЃС‚СѓРїРЅРѕ' : 'РЅРµРґРѕСЃС‚СѓРїРЅРѕ'}, СЃРµР№С‡Р°СЃ ${isSupported ? 'РґРѕСЃС‚СѓРїРЅРѕ' : 'РЅРµРґРѕСЃС‚СѓРїРЅРѕ'}${currentSupport.reason ? ` (${currentSupport.reason})` : ''}`
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
            messages.push(`РЅРµС‚ РґР°С‚С‡РёРєРѕРІ, РєРѕС‚РѕСЂС‹Рµ Р±С‹Р»Рё РїСЂРё РІР°Р»РёРґР°С†РёРё: ${missingSensors.join(', ')}`);
        }
        if (addedSensors.length) {
            messages.push(`РїРѕСЏРІРёР»РёСЃСЊ РґРѕРїРѕР»РЅРёС‚РµР»СЊРЅС‹Рµ РґР°С‚С‡РёРєРё: ${addedSensors.join(', ')}`);
        }
    }

    if (formatBusSource(snapshot) !== formatBusSource(currentEquipment)) {
        messages.push(
            `РёР·РјРµРЅРёР»Р°СЃСЊ 1-Wire С€РёРЅР°: Р±С‹Р»Рѕ ${formatBusSource(snapshot)}, СЃРµР№С‡Р°СЃ ${formatBusSource(currentEquipment)}`
        );
    }

    Object.entries(HARDWARE_FLAG_LABELS).forEach(([key, label]) => {
        const snapshotValue = asBoolean(snapshot?.[key]);
        const currentValue = asBoolean(currentEquipment?.[key]);
        if (snapshotValue !== currentValue) {
            messages.push(`${label}: Р±С‹Р»Рѕ ${formatBooleanState(snapshotValue)}, СЃРµР№С‡Р°СЃ ${formatBooleanState(currentValue)}`);
        }
    });

    if (Number(snapshot?.heaterPowerW || 0) !== Number(currentEquipment?.heaterPowerW || 0)) {
        messages.push(
            `РѕСЃРЅРѕРІРЅР°СЏ РјРѕС‰РЅРѕСЃС‚СЊ РЅР°РіСЂРµРІР°: Р±С‹Р»Рѕ ${Number(snapshot?.heaterPowerW || 0)} Р’С‚, СЃРµР№С‡Р°СЃ ${Number(currentEquipment?.heaterPowerW || 0)} Р’С‚`
        );
    }

    if (Number(snapshot?.columnHeightMm || 0) !== Number(currentEquipment?.columnHeightMm || 0)) {
        messages.push(
            `РІС‹СЃРѕС‚Р° С†Р°СЂРіРё: Р±С‹Р»Рѕ ${Number(snapshot?.columnHeightMm || 0)} РјРј, СЃРµР№С‡Р°СЃ ${Number(currentEquipment?.columnHeightMm || 0)} РјРј`
        );
    }

    if (String(snapshot?.packingType || '').trim() !== String(currentEquipment?.packingType || '').trim()) {
        messages.push(
            `РЅР°СЃР°РґРєР°: Р±С‹Р»Рѕ ${String(snapshot?.packingType || '-')}, СЃРµР№С‡Р°СЃ ${String(currentEquipment?.packingType || '-')}`
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
            messages.push(`РїРѕ РјРѕРґСѓР»СЏРј РµСЃС‚СЊ РѕС‚Р»РёС‡РёСЏ: ${moduleChanges.slice(0, 4).join('; ')}`);
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
                    `${label}: Р±С‹Р»Рѕ ${formatBooleanState(savedChannel.enabled)}, СЃРµР№С‡Р°СЃ ${formatBooleanState(currentChannel.enabled)}`
                );
            } else if (asBoolean(savedChannel.available) !== asBoolean(currentChannel.available)) {
                safetyChanges.push(
                    `${label}: Р±С‹Р»Рѕ ${asBoolean(savedChannel.available) ? 'РґРѕСЃС‚СѓРїРЅРѕ' : 'РЅРµРґРѕСЃС‚СѓРїРЅРѕ'}, СЃРµР№С‡Р°СЃ ${asBoolean(currentChannel.available) ? 'РґРѕСЃС‚СѓРїРЅРѕ' : 'РЅРµРґРѕСЃС‚СѓРїРЅРѕ'}`
                );
            }
        });
        if (safetyChanges.length) {
            messages.push(`РїРѕ safety-РєР°РЅР°Р»Р°Рј РµСЃС‚СЊ РѕС‚Р»РёС‡РёСЏ: ${safetyChanges.join('; ')}`);
        }
    }

    return {
        known: true,
        changed: messages.length > 0,
        summary: messages.length ? 'РџСЂРѕС„РёР»СЊ РІР°Р»РёРґРёСЂРѕРІР°Р»СЃСЏ РЅР° РґСЂСѓРіРѕР№ РєРѕРЅС„РёРіСѓСЂР°С†РёРё Р¶РµР»РµР·Р°.' : '',
        messages
    };
}
