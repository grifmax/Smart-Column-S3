export function updateLandingUi(snapshot) {
    const modeChip = document.getElementById('landing-mode-chip');
    if (modeChip && snapshot.mode !== undefined) {
        const modeNum = resolveMode(snapshot.mode, snapshot.modeStr);
        modeChip.textContent = getModeLabel(modeNum).toUpperCase();
        modeChip.className = `landing-chip ${getModeCssClass(modeNum)}`;
    }

    const phaseChip = document.getElementById('landing-phase-chip');
    if (phaseChip && snapshot.phaseText !== undefined) {
        phaseChip.textContent = `PHASE ${snapshot.phaseText || '-'}`;
    }

    const safetyChip = document.getElementById('landing-safety-chip');
    if (safetyChip && snapshot.safetyOk !== undefined) {
        if (snapshot.safetyOk) {
            safetyChip.textContent = 'SAFETY OK';
            safetyChip.className = 'landing-chip landing-chip-ok';
        } else {
            safetyChip.textContent = 'SAFETY ALERT';
            safetyChip.className = 'landing-chip landing-chip-warn';
        }
    }

    if (snapshot.tCube !== undefined) {
        const el = document.getElementById('landing-cube-value');
        if (el) el.textContent = `${snapshot.tCube.toFixed(1)}°C`;
    }
    if (snapshot.power !== undefined) {
        const el = document.getElementById('landing-power-value');
        if (el) el.textContent = `${snapshot.power.toFixed(0)} W`;
    }
    if (snapshot.pressureCube !== undefined) {
        const el = document.getElementById('landing-pressure-value');
        if (el) el.textContent = `${snapshot.pressureCube.toFixed(1)} мм`;
    }
    if (snapshot.pumpSpeed !== undefined) {
        const el = document.getElementById('landing-pump-value');
        if (el) el.textContent = `${snapshot.pumpSpeed.toFixed(0)} мл/ч`;
    }
    if (snapshot.abv !== undefined) {
        const el = document.getElementById('landing-abv-value');
        if (el) el.textContent = `${snapshot.abv.toFixed(1)} %`;
    }
    if (snapshot.waterIn !== undefined) {
        const el = document.getElementById('landing-water-in');
        if (el) el.textContent = `${snapshot.waterIn.toFixed(1)}°C`;
    }
    if (snapshot.waterOut !== undefined) {
        const el = document.getElementById('landing-water-out');
        if (el) el.textContent = `${snapshot.waterOut.toFixed(1)}°C`;
    }
    if (snapshot.voltage !== undefined) {
        const el = document.getElementById('landing-voltage');
        if (el) el.textContent = `${snapshot.voltage.toFixed(0)} V`;
    }

    const upd = document.getElementById('landing-updated');
    if (upd) {
        upd.textContent = new Date().toLocaleTimeString('ru-RU', { hour12: false });
    }
}

export const STATUS_POLL_INTERVAL_MS = 2000;
export let statusPollTimer = null;

export function startStatusPolling(immediate = false) {
    if (statusPollTimer) return;
    if (immediate) loadStatus();
    statusPollTimer = setInterval(loadStatus, STATUS_POLL_INTERVAL_MS);
}

export function stopStatusPolling() {
    if (!statusPollTimer) return;
    clearInterval(statusPollTimer);
    statusPollTimer = null;
}
