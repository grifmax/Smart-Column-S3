import { ABV_PLAN_STORAGE_KEY, plannedAbvPercent, setPlannedAbvPercent, setPlannedAbvUserSet, runtimeMonitorState } from '../globals.js';
import { normalizeAbvPercent } from './helpers.js';

export function loadPlannedAbv() {
    try {
        const raw = localStorage.getItem(ABV_PLAN_STORAGE_KEY);
        if (raw !== null) {
            setPlannedAbvPercent(normalizeAbvPercent(raw, plannedAbvPercent));
            setPlannedAbvUserSet(true);
        }
    } catch (e) {
        console.warn('planned ABV load failed:', e);
    }
}

export function savePlannedAbv(value) {
    setPlannedAbvPercent(normalizeAbvPercent(value, plannedAbvPercent));
    setPlannedAbvUserSet(true);
    try {
        localStorage.setItem(ABV_PLAN_STORAGE_KEY, plannedAbvPercent.toFixed(1));
    } catch (e) {
        console.warn('planned ABV save failed:', e);
    }
}

export function getEffectiveAbvForCalculations() {
    const state = runtimeMonitorState;
    const sensorRaw = Number(state.hydrometer.abv);
    const sensorHasValue = Number.isFinite(sensorRaw) && sensorRaw > 0;
    const sensorAbv = normalizeAbvPercent(sensorRaw, plannedAbvPercent);
    if (state.hydrometer.valid && sensorHasValue) {
        return { value: sensorAbv, source: 'sensor' };
    }
    return { value: plannedAbvPercent, source: 'planned' };
}

export function renderAbvValue() {
    const abvEl = document.getElementById('abv');
    const dotEl = document.getElementById('abv-source-dot');
    if (!abvEl) return;

    const effective = getEffectiveAbvForCalculations();
    if (effective.source === 'sensor') {
        abvEl.textContent = `${effective.value.toFixed(1)}%`;
    } else {
        abvEl.textContent = `~${effective.value.toFixed(1)}%`;
    }

    if (!dotEl) return;
    if (effective.source === 'sensor') {
        dotEl.classList.remove('abv-source-offline');
        dotEl.classList.add('abv-source-online');
        dotEl.title = 'Ареометр ONLINE (данные с датчика)';
    } else {
        dotEl.classList.remove('abv-source-online');
        dotEl.classList.add('abv-source-offline');
        dotEl.title = 'Ареометр OFF/нет данных (используется плановая крепость)';
    }
}
