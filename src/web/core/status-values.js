function toFiniteNumber(value) {
    const numeric = Number(value);
    return Number.isFinite(numeric) ? numeric : undefined;
}

export function getStatusNumber(data, nestedRoot, nestedKey, ...legacyKeys) {
    const root = data?.[nestedRoot];
    if (root && typeof root === 'object') {
        const nestedValue = toFiniteNumber(root[nestedKey]);
        if (nestedValue !== undefined) return nestedValue;
    }

    for (const key of legacyKeys) {
        const legacyValue = toFiniteNumber(data?.[key]);
        if (legacyValue !== undefined) return legacyValue;
    }

    return undefined;
}

export function getStatusTemperature(data, key, legacyKey) {
    return getStatusNumber(data, 'temps', key, legacyKey);
}

export function getStatusPressure(data, key, legacyKey) {
    return getStatusNumber(data, 'pressure', key, legacyKey);
}

export function getStatusPower(data, key, legacyKey) {
    return getStatusNumber(data, 'power', key, legacyKey);
}

export function getStatusPump(data, key, legacyKey) {
    return getStatusNumber(data, 'pump', key, legacyKey);
}

export function getHistoryPointNumber(point, modernKey, legacyKey) {
    const modernValue = toFiniteNumber(point?.[modernKey]);
    if (modernValue !== undefined) return modernValue;
    return toFiniteNumber(point?.[legacyKey]);
}

export function isStatusTempInvalid(data, key) {
    if (data?.tempValid && typeof data.tempValid === 'object' && data.tempValid[key] === false) {
        return true;
    }

    if (Array.isArray(data?.temperatures)) {
        const channel = data.temperatures.find((item) => item?.roleKey === key);
        if (channel?.valid === false) return true;
    }

    return false;
}
