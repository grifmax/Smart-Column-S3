function toNumber(value, fallback = NaN) {
    const parsed = Number(String(value ?? '').replace(',', '.'));
    return Number.isFinite(parsed) ? parsed : fallback;
}

function getStepPrecision(stepAttr) {
    const raw = String(stepAttr ?? '').trim();
    if (!raw || raw.toLowerCase() === 'any') return 0;
    const normalized = raw.replace(',', '.');
    const dot = normalized.indexOf('.');
    return dot >= 0 ? Math.max(0, normalized.length - dot - 1) : 0;
}

function clampValue(value, min, max) {
    let next = value;
    if (Number.isFinite(min)) next = Math.max(min, next);
    if (Number.isFinite(max)) next = Math.min(max, next);
    return next;
}

function formatStepValue(value, precision) {
    if (!Number.isFinite(value)) return '';
    return precision > 0 ? value.toFixed(precision) : String(Math.round(value));
}

function applyDelta(input, units) {
    const step = Math.max(0.000001, toNumber(input.step, 1));
    const precision = getStepPrecision(input.step);
    const min = toNumber(input.min, NaN);
    const max = toNumber(input.max, NaN);
    const current = toNumber(input.value, Number.isFinite(min) ? min : 0);
    const delta = step * units;
    const next = clampValue(current + delta, min, max);

    input.value = formatStepValue(next, precision);
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new Event('change', { bubbles: true }));
}

function createStepperButton(label, units, input) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'btn btn-sm equipment-step-btn';
    button.textContent = label;
    button.addEventListener('click', () => applyDelta(input, units));
    return button;
}

function wrapNumberInput(input) {
    if (!input || input.dataset.stepperWrapped === '1') return;
    if (input.closest('.equipment-num-stepper')) return;
    if (input.disabled || input.readOnly) return;

    const wrapper = document.createElement('div');
    wrapper.className = 'equipment-num-stepper';

    const leftGroup = document.createElement('div');
    leftGroup.className = 'equipment-step-group equipment-step-group-left';
    leftGroup.appendChild(createStepperButton('--', -10, input));
    leftGroup.appendChild(createStepperButton('-', -1, input));

    const rightGroup = document.createElement('div');
    rightGroup.className = 'equipment-step-group equipment-step-group-right';
    rightGroup.appendChild(createStepperButton('+', 1, input));
    rightGroup.appendChild(createStepperButton('++', 10, input));

    input.classList.add('equipment-num-input');
    input.dataset.stepperWrapped = '1';

    const parent = input.parentNode;
    if (!parent) return;

    parent.insertBefore(wrapper, input);
    wrapper.appendChild(leftGroup);
    wrapper.appendChild(input);
    wrapper.appendChild(rightGroup);
}

export function initEquipmentNumberSteppers(root) {
    const scope = root || document.getElementById('equipment');
    if (!scope) return;

    const numberInputs = scope.querySelectorAll('input[type="number"]:not([data-no-stepper="1"])');
    numberInputs.forEach((input) => wrapNumberInput(input));
}

