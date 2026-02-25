export const RECT_FEEDSTOCK_DEFAULTS = {
    0: { heads: 6.0, body: 84.0, tails: 10.0 },
    1: { heads: 8.0, body: 80.0, tails: 12.0 },
    2: { heads: 7.0, body: 81.0, tails: 12.0 },
    3: { heads: 5.0, body: 75.0, tails: 20.0 },
    4: { heads: 8.0, body: 74.0, tails: 18.0 },
    5: { heads: 6.0, body: 78.0, tails: 16.0 },
    6: { heads: 7.0, body: 79.0, tails: 14.0 },
    7: { heads: 8.0, body: 84.0, tails: 8.0 }
};

export function clampRectInput(value, min, max, fallback) {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) return fallback;
    if (parsed < min) return min;
    if (parsed > max) return max;
    return parsed;
}

export function closeRectificationStartModal() {
    const modal = document.getElementById('rect-start-modal');
    if (modal) modal.style.display = 'none';
}

export function updateRectificationFractionsSum() {
    const heads = clampRectInput(document.getElementById('rect-start-heads-percent')?.value, 0, 40, 0);
    const body = clampRectInput(document.getElementById('rect-start-body-percent')?.value, 0, 100, 0);
    const tails = clampRectInput(document.getElementById('rect-start-tails-percent')?.value, 0, 100, 0);
    const sum = heads + body + tails;

    const sumEl = document.getElementById('rect-start-fractions-sum');
    if (!sumEl) return;

    sumEl.textContent = `Sum: ${sum.toFixed(1)}%`;
    sumEl.style.color = sum > 100 ? 'var(--danger)' : 'var(--text-secondary)';
}

export function applyRectificationFeedstockDefaults() {
    const feedstockEl = document.getElementById('rect-start-feedstock');
    const headsEl = document.getElementById('rect-start-heads-percent');
    const bodyEl = document.getElementById('rect-start-body-percent');
    const tailsEl = document.getElementById('rect-start-tails-percent');
    if (!feedstockEl || !headsEl || !bodyEl || !tailsEl) return;

    const feedstock = clampRectInput(feedstockEl.value, 0, 7, 0);
    const defaults = RECT_FEEDSTOCK_DEFAULTS[feedstock] || RECT_FEEDSTOCK_DEFAULTS[7];
    headsEl.value = defaults.heads.toFixed(1);
    bodyEl.value = defaults.body.toFixed(1);
    tailsEl.value = defaults.tails.toFixed(1);
    updateRectificationFractionsSum();
}

export function normalizeRectificationFractions(params) {
    params.headsPercent = clampRectInput(params.headsPercent, 0, 40, 0);
    params.bodyPercent = clampRectInput(params.bodyPercent, 0, 100, 0);
    params.tailsPercent = clampRectInput(params.tailsPercent, 0, 100, 0);

    const sum = params.headsPercent + params.bodyPercent + params.tailsPercent;
    if (sum <= 100) return params;

    let excess = sum - 100;
    if (params.tailsPercent >= excess) {
        params.tailsPercent -= excess;
        return params;
    }

    excess -= params.tailsPercent;
    params.tailsPercent = 0;
    params.bodyPercent = Math.max(0, params.bodyPercent - excess);
    return params;
}

export function collectRectificationModalSettings() {
    const params = {
        feedstock: clampRectInput(document.getElementById('rect-start-feedstock')?.value, 0, 7, 0),
        feedVolumeL: clampRectInput(document.getElementById('rect-start-feed-volume')?.value, 1, 250, 20),
        feedAbvPercent: clampRectInput(document.getElementById('rect-start-feed-abv')?.value, 1, 96, 40),
        headsPercent: clampRectInput(document.getElementById('rect-start-heads-percent')?.value, 0, 40, 8),
        bodyPercent: clampRectInput(document.getElementById('rect-start-body-percent')?.value, 0, 100, 84),
        tailsPercent: clampRectInput(document.getElementById('rect-start-tails-percent')?.value, 0, 100, 8),
        headsSpeedMlHKw: clampRectInput(document.getElementById('rect-start-heads-speed')?.value, 10, 2000, 300),
        bodySpeedMlHKw: clampRectInput(document.getElementById('rect-start-body-speed')?.value, 50, 3000, 600),
        stabilizationMin: Math.round(clampRectInput(document.getElementById('rect-start-stabilization')?.value, 1, 180, 30)),
        purgeMin: Math.round(clampRectInput(document.getElementById('rect-start-purge')?.value, 1, 120, 5))
    };

    return normalizeRectificationFractions(params);
}

export async function openRectificationStartModal() {
    const modal = document.getElementById('rect-start-modal');
    if (!modal) {
        addLog('Rectification settings modal not found in layout', 'error');
        return false;
    }

    try {
        const response = await fetch('/api/settings/rect');
        if (response.ok) {
            const params = await response.json();
            const setValue = (id, value) => {
                const el = document.getElementById(id);
                if (el && value !== undefined && value !== null) el.value = String(value);
            };

            setValue('rect-start-feedstock', params.feedstock ?? 0);
            setValue('rect-start-feed-volume', params.feedVolumeL ?? 20);
            setValue('rect-start-feed-abv', params.feedAbvPercent ?? 40);
            setValue('rect-start-heads-percent', params.headsPercent ?? 8);
            setValue('rect-start-body-percent', params.bodyPercent ?? 84);
            setValue('rect-start-tails-percent', params.tailsPercent ?? 8);
            setValue('rect-start-heads-speed', params.headsSpeedMlHKw ?? 300);
            setValue('rect-start-body-speed', params.bodySpeedMlHKw ?? 600);
            setValue('rect-start-stabilization', params.stabilizationMin ?? 30);
            setValue('rect-start-purge', params.purgeMin ?? 5);
        } else {
            addLog(`Rect settings load error: ${response.status}`, 'warning');
        }
    } catch (e) {
        addLog(`Rect settings load network error: ${e.message}`, 'warning');
    }

    updateRectificationFractionsSum();
    modal.style.display = 'flex';
    return true;
}

export async function confirmStartRectification() {
    const saveButton = document.getElementById('rect-start-confirm');
    if (saveButton) saveButton.disabled = true;

    try {
        const params = collectRectificationModalSettings();

        const saveResponse = await fetch('/api/settings/rect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(params)
        });

        if (!saveResponse.ok) {
            const err = await saveResponse.text();
            addLog(`Rect settings save error (${saveResponse.status}): ${err}`, 'error');
            return;
        }

        closeRectificationStartModal();
        addLog('Starting auto-rectification with updated settings...', 'info');

        const startResponse = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mode: 'rectification' })
        });

        if (!startResponse.ok) {
            const err = await startResponse.text();
            addLog(`Start error (${startResponse.status}): ${err}`, 'error');
            return;
        }

        const data = await startResponse.json();
        addLog('Auto-rectification started', 'success');
        if (data.warning) addLog(`Warning: ${data.warning}`, 'warning');
        setTimeout(loadStatus, 500);
    } catch (e) {
        addLog(`Start rectification network error: ${e.message}`, 'error');
        console.error('confirmStartRectification error:', e);
    } finally {
        if (saveButton) saveButton.disabled = false;
    }
}

export function initRectificationStartModal() {
    const modal = document.getElementById('rect-start-modal');
    if (!modal) return;

    modal.addEventListener('click', (event) => {
        if (event.target === modal) closeRectificationStartModal();
    });

    const feedstock = document.getElementById('rect-start-feedstock');
    if (feedstock) {
        feedstock.addEventListener('change', applyRectificationFeedstockDefaults);
    }

    ['rect-start-heads-percent', 'rect-start-body-percent', 'rect-start-tails-percent']
        .forEach((id) => {
            const input = document.getElementById(id);
            if (input) input.addEventListener('input', updateRectificationFractionsSum);
        });
}

export async function startRectification() {

    if (!confirmModeSwitch(MODE_RECT, 'Auto-rectification')) return;

    await openRectificationStartModal();

}

export function startManual() {

    // Переход на страницу ручного управления

    if (!confirmModeSwitch(MODE_MANUAL, 'Manual rectification')) return;
    window.location.href = 'manual.html';

}
