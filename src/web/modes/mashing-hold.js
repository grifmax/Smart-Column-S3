import { MODE_MASH, MODE_HOLD } from '../globals.js';
import { confirmModeSwitch } from './common.js';
import { loadStatus } from '../core/status.js';
import { addLog } from '../core/logs.js';

// ============================================================================
// Дополнительные режимы: Затирка / Hold
// ============================================================================

export function initMashingHoldControls() {
    try {
        const select = document.getElementById('extra-mode-select');
        const mashingControls = document.getElementById('mashing-controls');
        const holdControls = document.getElementById('hold-controls');

        const setExtraMode = (mode) => {
            const normalized = mode || 'none';

            if (mashingControls) {
                mashingControls.style.display = normalized === 'mashing' ? '' : 'none';
            }
            if (holdControls) {
                holdControls.style.display = normalized === 'hold' ? '' : 'none';
            }

            try {
                localStorage.setItem('control.extraMode', normalized);
            } catch {
                // ignore
            }
        };

        const mashName = document.getElementById('mash-profile-name');
        if (mashName && !mashName.value) {
            mashName.value = 'Default Mashing';
        }

        const mashStepsEl = document.getElementById('mash-steps');
        if (mashStepsEl && mashStepsEl.children.length === 0) {
            // По умолчанию: шаги как в backend-дефолте
            addMashStep({ temperature: 38.0, duration: 20, name: 'Кислотная пауза' });
            addMashStep({ temperature: 52.0, duration: 20, name: 'Белковая пауза' });
            addMashStep({ temperature: 63.0, duration: 40, name: 'Мальтозная пауза' });
            addMashStep({ temperature: 72.0, duration: 20, name: 'Осахаривание' });
            addMashStep({ temperature: 78.0, duration: 10, name: 'Мэш-аут' });
        }

        const holdStepsEl = document.getElementById('hold-steps');
        if (holdStepsEl && holdStepsEl.children.length === 0) {
            // Дефолт: одна ступень 65°C на 60 минут
            addHoldStep({ temperature: 65.0, duration: 60 });
        }

        if (select) {
            select.addEventListener('change', () => setExtraMode(select.value));

            let saved = 'none';
            try {
                saved = localStorage.getItem('control.extraMode') || 'none';
            } catch {
                // ignore
            }

            // Если разметка обновилась и select ещё не выставлен - восстановим.
            if (!select.value || select.value === 'none') {
                select.value = saved;
            }
            setExtraMode(select.value);
        } else {
            // Если селектора нет (старый HTML), просто показываем блоки по умолчанию
            if (mashingControls) mashingControls.style.display = '';
            if (holdControls) holdControls.style.display = '';
        }
    } catch (e) {
        console.error('initMashingHoldControls error:', e);
    }
}

export function createStepRow({ mode, temperature, duration, name, useCooling }) {
    const row = document.createElement('div');
    row.dataset.stepRow = mode;
    row.style.display = 'flex';
    row.style.gap = '10px';
    row.style.flexWrap = 'wrap';
    row.style.alignItems = 'center';
    row.style.marginBottom = '8px';

    const tempInput = document.createElement('input');
    tempInput.type = 'number';
    tempInput.step = '0.1';
    tempInput.min = '0';
    tempInput.placeholder = mode === 'hold' ? 'Темп. или пауза' : 'Темп., °C';
    tempInput.value = (temperature ?? '') === '' ? '' : String(temperature);
    tempInput.dataset.field = 'temperature';
    tempInput.style.width = '140px';

    const durInput = document.createElement('input');
    durInput.type = 'number';
    durInput.step = '1';
    durInput.min = '1';
    durInput.placeholder = 'Мин';
    durInput.value = (duration ?? '') === '' ? '' : String(duration);
    durInput.dataset.field = 'duration';
    durInput.style.width = '110px';

    row.appendChild(tempInput);
    row.appendChild(durInput);

    if (mode === 'hold') {
        const coolingLabel = document.createElement('label');
        coolingLabel.className = 'checkbox-label';
        coolingLabel.style.display = 'inline-flex';
        coolingLabel.style.alignItems = 'center';
        coolingLabel.style.gap = '8px';
        coolingLabel.style.minHeight = '38px';

        const coolingInput = document.createElement('input');
        coolingInput.type = 'checkbox';
        coolingInput.dataset.field = 'useCooling';
        coolingInput.checked = Boolean(useCooling);

        const coolingText = document.createElement('span');
        coolingText.textContent = 'Охлаждение';

        coolingLabel.append(coolingInput, coolingText);
        row.appendChild(coolingLabel);
    }

    if (mode === 'mash') {
        const nameInput = document.createElement('input');
        nameInput.type = 'text';
        nameInput.placeholder = 'Имя шага (опц.)';
        nameInput.value = name || '';
        nameInput.dataset.field = 'name';
        nameInput.style.flex = '1';
        nameInput.style.minWidth = '180px';
        row.appendChild(nameInput);
    }

    const removeBtn = document.createElement('button');
    removeBtn.className = 'btn btn-sm';
    removeBtn.textContent = '✖';
    removeBtn.title = 'Удалить шаг';
    removeBtn.onclick = () => row.remove();
    row.appendChild(removeBtn);

    return row;
}

export function addMashStep(step = {}) {
    const el = document.getElementById('mash-steps');
    if (!el) return;
    el.appendChild(createStepRow({
        mode: 'mash',
        temperature: step.temperature,
        duration: step.duration,
        name: step.name
    }));
}

export function addHoldStep(step = {}) {
    const el = document.getElementById('hold-steps');
    if (!el) return;
    el.appendChild(createStepRow({
        mode: 'hold',
        temperature: step.temperature,
        duration: step.duration,
        useCooling: step.useCooling
    }));
}

export function readStepsFromUI(containerId, mode) {
    const el = document.getElementById(containerId);
    if (!el) return [];

    const rows = Array.from(el.querySelectorAll(`div[data-step-row="${mode}"]`));
    const steps = [];

    for (const row of rows) {
        const tempStr = row.querySelector('input[data-field="temperature"]')?.value ?? '';
        const durStr = row.querySelector('input[data-field="duration"]')?.value ?? '';
        const temperature = Number.parseFloat(tempStr);
        const duration = Number.parseInt(durStr, 10);

        if (!Number.isFinite(duration) || duration <= 0) continue;
        if (mode === 'mash' && (!Number.isFinite(temperature) || temperature <= 0)) continue;

        const step = { duration };
        if (mode === 'mash') {
            step.temperature = temperature;
            const name = (row.querySelector('input[data-field="name"]')?.value ?? '').trim();
            if (name) step.name = name;
        } else if (mode === 'hold') {
            if (Number.isFinite(temperature) && temperature > 0) {
                step.temperature = temperature;
            } else {
                step.temperature = 0;
            }
            step.useCooling = Boolean(row.querySelector('input[data-field="useCooling"]')?.checked);
        }
        steps.push(step);
    }

    return steps;
}

export async function startMashing() {
    if (!confirmModeSwitch(MODE_MASH, 'Mashing')) return false;

    try {
        const profileName = (document.getElementById('mash-profile-name')?.value ?? '').trim() || 'Mashing';
        const steps = readStepsFromUI('mash-steps', 'mash');

        if (!steps.length) {
            addLog('✗ Затирка: добавьте хотя бы один корректный шаг (температура и длительность)', 'error');
            return false;
        }

        addLog('📤 Отправка команды запуска затирки...', 'info');

        const response = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                mode: 'mashing',
                params: {
                    profile: {
                        name: profileName,
                        steps
                    }
                }
            })
        });

        if (response.ok) {
            const data = await response.json();
            addLog('✓ Затирка запущена', 'success');
            if (data.warning) addLog(`⚠️ ${data.warning}`, 'warning');
            setTimeout(loadStatus, 500);
            return true;
        }

        const error = await response.text();
        addLog(`✗ Ошибка (${response.status}): ${error}`, 'error');
        return false;
    } catch (e) {
        addLog(`✗ Ошибка сети: ${e.message}`, 'error');
        console.error('Start mashing error:', e);
        return false;
    }
}

export async function startHold() {
    if (!confirmModeSwitch(MODE_HOLD, 'Пастеризация')) return false;

    try {
        const steps = readStepsFromUI('hold-steps', 'hold');

        if (!steps.length) {
            addLog('✗ Пастеризация: добавьте хотя бы один корректный шаг или паузу с длительностью', 'error');
            return false;
        }

        addLog('📤 Отправка команды запуска пастеризации...', 'info');

        const response = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                mode: 'hold',
                params: { steps }
            })
        });

        if (response.ok) {
            const data = await response.json();
            addLog('✓ Пастеризация запущена', 'success');
            if (data.warning) addLog(`⚠️ ${data.warning}`, 'warning');
            setTimeout(loadStatus, 500);
            return true;
        }

        const error = await response.text();
        addLog(`✗ Ошибка (${response.status}): ${error}`, 'error');
        return false;
    } catch (e) {
        addLog(`✗ Ошибка сети: ${e.message}`, 'error');
        console.error('Start hold error:', e);
        return false;
    }
}
