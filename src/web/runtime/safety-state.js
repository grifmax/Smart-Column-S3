function readObject(value) {
    return value && typeof value === 'object' ? value : null;
}

function readString(value, fallback = '') {
    return value !== undefined && value !== null ? String(value) : fallback;
}

export function getAlarmState(snapshot) {
    return readObject(snapshot?.currentAlarm) || readObject(snapshot?.alarm);
}

export function getV2SafetyState(snapshot) {
    return readObject(snapshot?.v2?.safety);
}

export function deriveSafetyUiState(snapshot) {
    const alarm = getAlarmState(snapshot);
    const v2 = readObject(snapshot?.v2);
    const safety = getV2SafetyState(snapshot);
    const alarmTypeCode = Number(alarm?.typeCode ?? alarm?.type ?? 0);
    const active = Boolean(alarm?.active) || alarmTypeCode !== 0 || Boolean(v2?.safetyLatched);
    const latched = alarm?.latched !== undefined ? Boolean(alarm.latched) : Boolean(v2?.safetyLatched);
    const safetyOk = snapshot?.safetyOk !== undefined ? Boolean(snapshot.safetyOk) : !active;
    const severity = readString(safety?.severity, active ? (latched ? 'latched_trip' : 'warning') : 'none');
    const message = readString(safety?.message, readString(alarm?.message));
    const resetAvailable = safety?.resetAvailable !== undefined
        ? Boolean(safety.resetAvailable)
        : (alarm?.resetAvailable !== undefined ? Boolean(alarm.resetAvailable) : !active);
    const resetBlockedReason = readString(safety?.resetBlockedReason, readString(alarm?.resetBlockedReason));
    const acknowledged = alarm?.acknowledged !== undefined ? Boolean(alarm.acknowledged) : false;
    const requiresAcknowledge = safety?.requiresAcknowledge !== undefined
        ? Boolean(safety.requiresAcknowledge)
        : (active && !acknowledged && !resetAvailable);

    let chipText = 'SAFETY OK';
    let chipClass = 'landing-chip landing-chip-ok';

    if (severity === 'recovery' || (active && resetAvailable)) {
        chipText = 'RESET READY';
        chipClass = 'landing-chip landing-chip-recovery';
    } else if (active && acknowledged) {
        chipText = 'SAFETY ACKED';
        chipClass = 'landing-chip landing-chip-neutral';
    } else if (severity === 'limited') {
        chipText = 'SAFETY LIMITED';
        chipClass = 'landing-chip landing-chip-warn';
    } else if (severity === 'warning') {
        chipText = 'SAFETY WARN';
        chipClass = 'landing-chip landing-chip-warn';
    } else if (active || safetyOk === false) {
        chipText = 'SAFETY ALERT';
        chipClass = 'landing-chip landing-chip-warn';
    }

    const chipTitle = resetBlockedReason || message || '';
    const shouldShowModal = safetyOk === false && active && alarmTypeCode !== 0 && (!acknowledged || resetAvailable);

    return {
        alarm,
        safety,
        active,
        latched,
        safetyOk,
        alarmTypeCode,
        severity,
        message,
        resetAvailable,
        resetBlockedReason,
        acknowledged,
        requiresAcknowledge,
        chipText,
        chipClass,
        chipTitle,
        shouldShowModal,
        primaryActionLabel: resetAvailable ? '♻ СБРОСИТЬ АВАРИЮ' : '✅ ИГНОРИРОВАТЬ И ПРОДОЛЖИТЬ',
        primaryActionBackground: resetAvailable ? '#0d6efd' : '#6c757d'
    };
}
