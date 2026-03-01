import {
    runtimeMonitorState, maxHeaterPower, currentMode,
    MODE_MANUAL, MODE_RECT, plannedAbvPercent,
    runtimeEditContext, setRuntimeEditContext
} from '../globals.js';
import { toFinite, clampPercent, normalizeAbvPercent } from './helpers.js';
import { savePlannedAbv, renderAbvValue } from './abv.js';
import { renderModeRuntimeCard } from './bars.js';
import { addLog } from '../core/logs.js';
import { loadStatus } from '../core/status.js';

function isRuntimeEditWattsMode(config) {
    if (!config?.supportsUnitToggle) return false;
    const cbWatts = document.getElementById('runtime-edit-unit-watts');
    return Boolean(cbWatts?.checked);
}

function getRuntimeQuickAdjustments(config) {
    const groups = config?.quickAdjustments?.groups;
    if (!Array.isArray(groups) || groups.length === 0) return [];
    return groups;
}

function formatRuntimeDelta(delta) {
    const sign = delta >= 0 ? '+' : '';
    return `${sign}${delta}`;
}

function renderRuntimeQuickActions(config) {
    const root = document.getElementById('runtime-edit-quick-actions');
    if (!root) return;

    const groups = getRuntimeQuickAdjustments(config);
    if (!groups.length) {
        root.style.display = 'none';
        root.innerHTML = '';
        return;
    }

    const activeUnit = isRuntimeEditWattsMode(config) ? 'watts' : 'percent';
    root.innerHTML = groups.map((group) => {
        const values = Array.isArray(group.deltas) ? group.deltas : [];
        const buttons = values.map((delta) => (
            `<button type="button" class="btn btn-sm runtime-edit-quick-btn" data-runtime-delta-unit="${group.unit}" data-runtime-delta="${delta}">${formatRuntimeDelta(delta)}</button>`
        )).join('');
        const isActive = group.unit === activeUnit ? ' is-active' : '';
        return `
            <div class="runtime-edit-quick-group${isActive}">
                <div class="runtime-edit-quick-label">${group.label}</div>
                <div class="runtime-edit-quick-row">${buttons}</div>
            </div>
        `;
    }).join('');

    root.querySelectorAll('[data-runtime-delta]').forEach((button) => {
        button.addEventListener('click', () => {
            const unit = String(button.getAttribute('data-runtime-delta-unit') || '').trim();
            const delta = toFinite(button.getAttribute('data-runtime-delta'), NaN);
            applyRuntimeQuickDelta(unit, delta);
            renderRuntimeQuickActions(config);
        });
    });
    root.style.display = 'grid';
}

function applyRuntimeQuickDelta(unit, delta) {
    const config = runtimeEditContext;
    const inputEl = document.getElementById('runtime-edit-value');
    if (!config || !inputEl || !Number.isFinite(delta)) return;

    const min = toFinite(inputEl.min, Number.NEGATIVE_INFINITY);
    const max = toFinite(inputEl.max, Number.POSITIVE_INFINITY);
    const currentValue = toFinite(inputEl.value, min || 0);

    if (config.supportsUnitToggle) {
        const heaterMaxW = Math.max(1, toFinite(config.heaterMaxW, maxHeaterPower));
        const wattsMode = isRuntimeEditWattsMode(config);
        const currentPercent = wattsMode ? ((currentValue / heaterMaxW) * 100) : currentValue;

        let nextPercent;
        if (unit === 'watts') {
            const nextWatts = Math.max(0, Math.min(heaterMaxW, (currentPercent / 100) * heaterMaxW + delta));
            nextPercent = (nextWatts / heaterMaxW) * 100;
        } else {
            nextPercent = currentPercent + delta;
        }
        nextPercent = Math.max(0, Math.min(100, nextPercent));
        inputEl.value = wattsMode
            ? String(Math.round((nextPercent / 100) * heaterMaxW))
            : String(Math.round(nextPercent));
        inputEl.focus();
        return;
    }

    const nextValue = Math.min(max, Math.max(min, currentValue + delta));
    inputEl.value = String(Math.round(nextValue));
    inputEl.focus();
}

export function getRuntimeEditConfig(paramKey) {
    const s = runtimeMonitorState;
    const heaterMax = Math.max(1, toFinite(s.equipment.heaterPowerW, maxHeaterPower));
    const measuredPower = Math.max(0, toFinite(s.power.power, 0));
    const currentPowerPercent = clampPercent((measuredPower / heaterMax) * 100);

    const map = {
        'rect-power': {
            title: 'Мощность нагрева (ректификация)',
            label: 'Мощность, %',
            step: '1',
            min: '0',
            max: '100',
            hint: 'Override мощности ТЭНа. Установите -1 для возврата к автоматическому управлению.',
            value: currentPowerPercent.toFixed(0),
            supportsUnitToggle: true,
            heaterMaxW: heaterMax,
            submit: async (value) => {
                const resp = await fetch('/api/rect/heater', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ power: Number(value) })
                });
                if (!resp.ok) throw new Error(await resp.text());
            }
        },
        'manual-power': {
            title: 'Мощность нагрева',
            label: 'Мощность, %',
            step: '1',
            min: '0',
            max: '100',
            hint: 'Применяется сразу в ручном режиме.',
            value: currentPowerPercent.toFixed(0),
            supportsUnitToggle: true,
            heaterMaxW: heaterMax,
            quickAdjustments: {
                groups: [
                    { unit: 'watts', label: 'Быстрые шаги, Вт', deltas: [-10, -100, 10, 100] },
                    { unit: 'percent', label: 'Быстрые шаги, %', deltas: [-1, -10, 1, 10] }
                ]
            },
            submit: async (value) => {
                const resp = await fetch('/api/manual/heater', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ power: Number(value) })
                });
                if (!resp.ok) throw new Error(await resp.text());
            }
        },
        'planned-abv': {
            title: 'Плановая крепость',
            label: 'Крепость, %',
            step: '0.1',
            min: '0',
            max: '100',
            hint: 'Используется для расчёта целей и времени, пока электронный ареометр OFF.',
            value: normalizeAbvPercent(plannedAbvPercent, 40).toFixed(1),
            submit: async (value) => {
                savePlannedAbv(value);
                renderAbvValue();
                renderModeRuntimeCard();
            }
        },
        'manual-speed': {
            title: 'Скорость отбора',
            label: 'Скорость, мл/ч',
            step: '1',
            min: '0',
            max: '5000',
            hint: '0 = остановить насос.',
            value: toFinite(s.pump.speedMlH, 0).toFixed(0),
            submit: async (value) => {
                const resp = await fetch('/api/manual/pump', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ speed: Number(value) })
                });
                if (!resp.ok) throw new Error(await resp.text());
            }
        },
        'manual-heads': {
            title: 'Объем фракции: Головы',
            label: 'Головы, мл',
            step: '1',
            min: '0',
            max: '100000',
            hint: 'Коррекция учетного объема на экране.',
            value: toFinite(s.volumes.heads, 0).toFixed(0),
            submit: async (value) => {
                const resp = await fetch('/api/manual/volumes', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ heads: Number(value), syncTotal: true })
                });
                if (!resp.ok) throw new Error(await resp.text());
            }
        },
        'manual-body': {
            title: 'Объем фракции: Тело',
            label: 'Тело, мл',
            step: '1',
            min: '0',
            max: '100000',
            hint: 'Коррекция учетного объема на экране.',
            value: toFinite(s.volumes.body, 0).toFixed(0),
            submit: async (value) => {
                const resp = await fetch('/api/manual/volumes', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ body: Number(value), syncTotal: true })
                });
                if (!resp.ok) throw new Error(await resp.text());
            }
        },
        'manual-tails': {
            title: 'Объем фракции: Хвосты',
            label: 'Хвосты, мл',
            step: '1',
            min: '0',
            max: '100000',
            hint: 'Коррекция учетного объема на экране.',
            value: toFinite(s.volumes.tails, 0).toFixed(0),
            submit: async (value) => {
                const resp = await fetch('/api/manual/volumes', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ tails: Number(value), syncTotal: true })
                });
                if (!resp.ok) throw new Error(await resp.text());
            }
        }
    };
    return map[paramKey] || null;
}

export function openRuntimeEditModal(paramKey) {
    const isRectParam = paramKey === 'rect-power';
    const isPlannedAbvParam = paramKey === 'planned-abv';
    if (!isPlannedAbvParam && currentMode !== MODE_MANUAL && !(isRectParam && currentMode === MODE_RECT)) {
        addLog('Редактирование параметров доступно только в ручной или авто-ректификации', 'warning');
        return;
    }

    const config = getRuntimeEditConfig(paramKey);
    const modal = document.getElementById('runtime-edit-modal');
    const titleEl = document.getElementById('runtime-edit-title');
    const labelEl = document.getElementById('runtime-edit-label');
    const inputEl = document.getElementById('runtime-edit-value');
    const hintEl = document.getElementById('runtime-edit-hint');
    const toggleEl = document.getElementById('runtime-edit-unit-toggle');
    const cbWatts = document.getElementById('runtime-edit-unit-watts');
    if (!config || !modal || !titleEl || !labelEl || !inputEl || !hintEl) return;

    setRuntimeEditContext(config);
    titleEl.textContent = config.title;
    labelEl.textContent = config.label;
    inputEl.min = config.min;
    inputEl.max = config.max;
    inputEl.step = config.step;
    inputEl.value = config.value;
    hintEl.textContent = config.hint;

    // Show/reset unit toggle checkbox
    if (toggleEl && cbWatts) {
        if (config.supportsUnitToggle) {
            cbWatts.checked = false;
            toggleEl.style.display = 'block';
        } else {
            toggleEl.style.display = 'none';
        }
    }
    renderRuntimeQuickActions(config);

    modal.style.display = 'flex';
    inputEl.focus();
    inputEl.select();
}

export function onRuntimeEditUnitToggle() {
    const cbWatts = document.getElementById('runtime-edit-unit-watts');
    const labelEl = document.getElementById('runtime-edit-label');
    const inputEl = document.getElementById('runtime-edit-value');
    const config = runtimeEditContext;
    if (!config || !cbWatts || !labelEl || !inputEl) return;

    const heaterMaxW = config.heaterMaxW || maxHeaterPower;
    const useWatts = cbWatts.checked;

    if (useWatts) {
        // Convert current % value to W
        const currentPct = toFinite(inputEl.value, 0);
        const watts = Math.round((currentPct / 100) * heaterMaxW);
        labelEl.textContent = 'Мощность, Вт';
        inputEl.min = '0';
        inputEl.max = String(heaterMaxW);
        inputEl.step = '10';
        inputEl.value = String(watts);
    } else {
        // Convert current W value back to %
        const currentW = toFinite(inputEl.value, 0);
        const pct = Math.round((currentW / heaterMaxW) * 100);
        labelEl.textContent = 'Мощность, %';
        inputEl.min = config.min;
        inputEl.max = config.max;
        inputEl.step = config.step;
        inputEl.value = String(Math.min(100, Math.max(0, pct)));
    }
    renderRuntimeQuickActions(config);
    inputEl.focus();
}

export function closeRuntimeEditModal() {
    setRuntimeEditContext(null);
    const modal = document.getElementById('runtime-edit-modal');
    if (modal) modal.style.display = 'none';
}

export async function submitRuntimeEditModal() {
    if (!runtimeEditContext) return;
    const inputEl = document.getElementById('runtime-edit-value');
    const cbWatts = document.getElementById('runtime-edit-unit-watts');
    if (!inputEl) return;

    const min = toFinite(inputEl.min, 0);
    const max = toFinite(inputEl.max, Number.POSITIVE_INFINITY);
    let value = toFinite(inputEl.value, NaN);
    if (!Number.isFinite(value)) {
        alert('Введите корректное число');
        return;
    }
    if (value < min) value = min;
    if (value > max) value = max;

    // If watts mode is active, convert W → % before submitting
    if (cbWatts && cbWatts.checked && runtimeEditContext.supportsUnitToggle) {
        const heaterMaxW = runtimeEditContext.heaterMaxW || maxHeaterPower;
        value = Math.round((value / heaterMaxW) * 100);
        value = Math.min(100, Math.max(0, value));
    }

    try {
        await runtimeEditContext.submit(value);
        closeRuntimeEditModal();
        addLog('Параметр обновлен', 'success');
        setTimeout(loadStatus, 250);
    } catch (error) {
        const message = error?.message || 'Ошибка сохранения';
        addLog(`Ошибка изменения параметра: ${message}`, 'error');
    }
}
