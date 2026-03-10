function toNumber(value, fallback = NaN) {
    const normalized = String(value ?? '')
        .trim()
        .replace(/\s+/g, '')
        .replace(',', '.');
    const parsed = Number(normalized);
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

function getEffectiveStep(input) {
    const override = toNumber(input.dataset.stepperStep, NaN);
    if (Number.isFinite(override) && override > 0) return override;
    const step = toNumber(input.step, NaN);
    if (Number.isFinite(step) && step > 0) return step;
    return 1;
}

function getEffectiveStepPrecision(input) {
    if (input.dataset.stepperStep) {
        return getStepPrecision(input.dataset.stepperStep);
    }
    return getStepPrecision(input.step);
}

function applyDelta(input, units) {
    const step = Math.max(0.000001, getEffectiveStep(input));
    const precision = getEffectiveStepPrecision(input);
    const min = toNumber(input.min, NaN);
    const max = toNumber(input.max, NaN);
    const absUnits = Math.max(1, Math.round(Math.abs(units)));
    const isIncrease = units > 0;
    const stepOverride = input.dataset.stepperStep;
    const originalStep = input.getAttribute('step');
    const before = toNumber(input.value, NaN);

    if (stepOverride) {
        input.setAttribute('step', String(step));
    }

    const normalizedCurrent = toNumber(input.value, NaN);
    if (Number.isFinite(normalizedCurrent)) {
        input.value = formatStepValue(normalizedCurrent, precision);
    } else if (String(input.value).trim() === '') {
        input.value = Number.isFinite(min) ? formatStepValue(min, precision) : '0';
    }

    let changedByNative = false;
    try {
        if (typeof input.stepUp === 'function' && typeof input.stepDown === 'function') {
            if (isIncrease) input.stepUp(absUnits);
            else input.stepDown(absUnits);
            const afterNative = toNumber(input.value, NaN);
            if (Number.isFinite(afterNative) && Number.isFinite(before)) {
                changedByNative = Math.abs(afterNative - before) > 0.0000001;
            } else {
                changedByNative = Number.isFinite(afterNative);
            }
        }
    } catch {
        changedByNative = false;
    }

    if (!changedByNative) {
        const current = toNumber(input.value, Number.isFinite(min) ? min : 0);
        const delta = step * (isIncrease ? absUnits : -absUnits);
        const next = clampValue(current + delta, min, max);
        input.value = formatStepValue(next, precision);
    }

    if (stepOverride) {
        if (originalStep === null) input.removeAttribute('step');
        else input.setAttribute('step', originalStep);
    }

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
    const stepperMode = String(input.dataset.stepperMode || 'full').toLowerCase();

    const leftGroup = document.createElement('div');
    leftGroup.className = 'equipment-step-group equipment-step-group-left';

    const rightGroup = document.createElement('div');
    rightGroup.className = 'equipment-step-group equipment-step-group-right';

    if (stepperMode === 'pair') {
        leftGroup.appendChild(createStepperButton('-', -1, input));
        rightGroup.appendChild(createStepperButton('+', 1, input));
    } else {
        leftGroup.appendChild(createStepperButton('--', -10, input));
        leftGroup.appendChild(createStepperButton('-', -1, input));
        rightGroup.appendChild(createStepperButton('+', 1, input));
        rightGroup.appendChild(createStepperButton('++', 10, input));
    }

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
