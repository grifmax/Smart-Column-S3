import {
    currentMode,
    currentPaused,
    maxHeaterPower,
    runtimeMonitorState,
    MODE_IDLE,
    MODE_RECT,
    MODE_DIST,
    MODE_MANUAL,
    MODE_MASH,
    MODE_HOLD,
    MODE_NBK,
    MODE_FERMENTATION,
    resolveMode,
    getModeLabel,
    getModeCssClass,
    setCurrentMode,
    setCurrentPaused,
    setMaxHeaterPower
} from '../globals.js';
import { updateRuntimeStateFromStatus } from '../runtime/state.js';
import { getEffectiveAbvForCalculations, renderAbvValue } from '../runtime/abv.js';
import { renderModeRuntimeCard, getStartAvailabilityState } from '../runtime/bars.js';
import { updateInteractiveScheme } from '../ui/scheme.js';
import { updateLandingUi } from '../ui/landing.js';
import { syncOperatorViewAuto } from '../ui/operator-view.js';
import { updateCloudUiFromStatus } from '../cloud/cloud-config.js';
import { formatUptime } from './utils.js';
import { addLog } from './logs.js';
import { updateProcessNotifications } from '../runtime/process-notifications.js';
import { syncStirrerUi } from '../settings/equipment.js';

// ============================================================================

// Загрузка статуса и обновление кнопок

// ============================================================================



export async function loadStatus() {

    try {

        const response = await fetch('/api/status');

        if (!response.ok) {
            // Если статус недоступен (401/404/5xx) — не оставляем UI в "случайном" состоянии.
            // Делаем безопасный фолбэк: считаем процесс остановленным и отключаем управляющие кнопки по state.
            const msg = `✗ Статус недоступен (/api/status): HTTP ${response.status}`;
            addLog(msg, 'error');

            // Сбросить состояние, чтобы кнопки не выглядели как "процесс запущен"
            setCurrentMode(MODE_IDLE);
            setCurrentPaused(false);
            updateButtonStates();
            return;
        }



        const data = await response.json();



        // Обновить состояние процесса

        // Нормализуем mode: ожидаем число (0=IDLE), но на прокси/кастомных сборках
        // может прилететь строка. Для кнопок достаточно корректно определить IDLE.
        setCurrentMode(resolveMode(data.mode, data.modeStr));
        setCurrentPaused(Boolean(data.paused));



        // Сохранить мощность ТЭНа из настроек

        if (data.equipment && data.equipment.heaterPowerW) {

            setMaxHeaterPower(data.equipment.heaterPowerW);

            updateHeaterSlider();

        }



        // Обновить UI с новым форматом данных (не должен ломать обновление кнопок)
        try {
            updateUIFromStatus(data);
            document.dispatchEvent(new CustomEvent('runtime-status-updated'));
        } catch (e) {
            console.error('updateUIFromStatus error:', e);
        }



        // Обновить состояние кнопок
        updateButtonStates();



    } catch (e) {

        console.error('Ошибка загрузки статуса:', e);

    }

}



export function updateUIFromStatus(data) {
    let phaseText = '-';
    updateRuntimeStateFromStatus(data);
    syncOperatorViewAuto();
    updateProcessNotifications();

    // Режим


    if (data.modeStr !== undefined || data.mode !== undefined) {

        const modeEl = document.getElementById('mode');

        if (modeEl) {
            const resolvedMode = resolveMode(data.mode, data.modeStr);

            modeEl.textContent = getModeLabel(resolvedMode).toUpperCase();
            modeEl.className = `value ${getModeCssClass(resolvedMode)}`;

        }

    }



    // Фаза

    if (data.phaseStr !== undefined) {

        const phaseEl = document.getElementById('phase');

        if (phaseEl) {

            phaseText = data.phaseStr.toUpperCase() || '-';
            phaseEl.textContent = phaseText;

        }

    }

    // Cloud (IoT tunnel)
    updateCloudUiFromStatus(data);



    // Температуры

    if (data.temps) {

        if (data.temps.cube !== undefined) {

            const el = document.getElementById('temp-cube');

            if (el) el.textContent = data.temps.cube.toFixed(1) + '°C';

        }

        if (data.temps.columnBottom !== undefined) {

            const el = document.getElementById('temp-column-bottom');

            if (el) el.textContent = data.temps.columnBottom.toFixed(1) + '°C';

        }

        if (data.temps.columnTop !== undefined) {

            const el = document.getElementById('temp-column-top');

            if (el) el.textContent = data.temps.columnTop.toFixed(1) + '°C';

        }

        if (data.temps.reflux !== undefined) {

            const el = document.getElementById('temp-reflux');

            if (el) el.textContent = data.temps.reflux.toFixed(1) + '°C';

        }

        if (data.temps.tsa !== undefined) {

            const el = document.getElementById('temp-tsa');

            if (el) el.textContent = data.temps.tsa.toFixed(1) + '°C';

        }

    }



    // Давление

    if (data.pressure) {

        if (data.pressure.cube !== undefined) {

            const el = document.getElementById('pressure-cube');

            if (el) el.textContent = data.pressure.cube.toFixed(1) + ' мм рт.ст.';

        }

        if (data.pressure.atm !== undefined) {

            const el = document.getElementById('pressure-atm');

            if (el) el.textContent = data.pressure.atm.toFixed(1) + ' гПа';

        }

    }



    // Мощность

    if (data.power) {

        if (data.power.voltage !== undefined) {

            const el = document.getElementById('power-voltage');

            if (el) el.textContent = data.power.voltage.toFixed(1) + ' V';

        }

        if (data.power.current !== undefined) {

            const el = document.getElementById('power-current');

            if (el) el.textContent = data.power.current.toFixed(2) + ' A';

        }

        if (data.power.power !== undefined) {

            const el = document.getElementById('power-power');

            if (el) el.textContent = data.power.power.toFixed(0) + ' W';

            const distPowerFact = document.getElementById('dist-start-power-fact');
            if (distPowerFact) distPowerFact.value = data.power.power.toFixed(0);

        }

        if (data.power.energy !== undefined) {

            const el = document.getElementById('power-energy');

            if (el) el.textContent = data.power.energy.toFixed(3) + ' кВт·ч';

        }

        if (data.power.frequency !== undefined) {

            const el = document.getElementById('power-frequency');

            if (el) el.textContent = data.power.frequency.toFixed(1) + ' Гц';

        }

        if (data.power.pf !== undefined) {

            const el = document.getElementById('power-pf');

            if (el) el.textContent = data.power.pf.toFixed(2);

        }

    }

    if (data.distillation && (data.distillation.powerW !== undefined || data.distillation.powerPercent !== undefined)) {
        const distPowerSet = document.getElementById('dist-start-power-percent');
        if (distPowerSet && document.activeElement !== distPowerSet) {
            const heaterMax = Math.max(1, Number(maxHeaterPower) || 3000);
            const distPowerWatts = data.distillation.powerW !== undefined
                ? Math.max(0, Math.min(heaterMax, Number(data.distillation.powerW) || 0))
                : Math.round((Math.max(0, Math.min(100, Number(data.distillation.powerPercent) || 0)) / 100) * heaterMax);
            distPowerSet.value = String(distPowerWatts);
            distPowerSet.max = String(heaterMax);
            distPowerSet.step = '50';
        }
    }



    // Насос

    if (data.pump) {

        if (data.pump.speedMlH !== undefined) {

            const el = document.getElementById('pump-speed');

            if (el) el.textContent = data.pump.speedMlH.toFixed(0) + ' мл/ч';

            const tbSpeed = document.getElementById('toolbar-pump-speed');

            if (tbSpeed) tbSpeed.textContent = data.pump.speedMlH.toFixed(0) + ' мл/ч';

        }

        if (data.pump.totalMl !== undefined) {

            const el = document.getElementById('pump-volume');

            if (el) el.textContent = data.pump.totalMl.toFixed(0) + ' мл';

        }

    }



    // Объёмы фракций

    if (data.volumes) {

        if (data.volumes.heads !== undefined) {

            const el = document.getElementById('volume-heads');

            if (el) el.textContent = data.volumes.heads.toFixed(0) + ' мл';

        }

        if (data.volumes.body !== undefined) {

            const el = document.getElementById('volume-body');

            if (el) el.textContent = data.volumes.body.toFixed(0) + ' мл';

        }

        if (data.volumes.tails !== undefined) {

            const el = document.getElementById('volume-tails');

            if (el) el.textContent = data.volumes.tails.toFixed(0) + ' мл';

        }

    }



    // Ареометр

    renderAbvValue();



    // Uptime

    if (data.uptime !== undefined) {

        const el = document.getElementById('uptime');

        if (el) el.textContent = formatUptime(data.uptime);
        const opUptime = document.getElementById('operator-uptime');
        if (opUptime) opUptime.textContent = formatUptime(data.uptime);

    }

    updateLandingUi({
        mode: data.mode,
        modeStr: data.modeStr,
        phaseText,
        safetyOk: data.safetyOk,
        alarm: data.alarm,
        currentAlarm: data.currentAlarm,
        v2: data.v2,
        tCube: data.temps?.cube,
        power: data.power?.power,
        pressureCube: data.pressure?.cube,
        pumpSpeed: data.pump?.speedMlH,
        abv: getEffectiveAbvForCalculations().value,
        waterIn: data.temps?.waterIn,
        waterOut: data.temps?.waterOut,
        voltage: data.power?.voltage
    });
    syncStirrerUi({ syncSpeedInput: true });

    // Обновить интерактивную схему (SVG) — также в polling-режиме
    updateInteractiveScheme(data);

    renderModeRuntimeCard();

}



export function updateButtonStates() {

    const isIdle = currentMode === MODE_IDLE;

    const findModeButton = (modeAction, fallbackOnclick) =>
        document.querySelector(`button[data-mode-action="${modeAction}"]`) ||
        document.querySelector(`button[onclick="${fallbackOnclick}"]`);

    const btnRect = findModeButton('rectification', 'startRectification()');
    const btnManual = findModeButton('manual', 'startManual()');
    const btnDist = findModeButton('distillation', 'startDistillation()');
    const btnMashing = findModeButton('mashing', 'startMashing()');
    const btnHold = findModeButton('hold', 'startHold()');
    const btnNbk = findModeButton('nbk', 'startNbk()');
    const btnFermentation = findModeButton('fermentation', 'startFermentation()');

    const modeButtons = [
        { mode: MODE_RECT, button: btnRect },
        { mode: MODE_MANUAL, button: btnManual },
        { mode: MODE_DIST, button: btnDist },
        { mode: MODE_MASH, button: btnMashing },
        { mode: MODE_HOLD, button: btnHold },
        { mode: MODE_NBK, button: btnNbk },
        { mode: MODE_FERMENTATION, button: btnFermentation }
    ];

    modeButtons.forEach(({ mode, button }) => {
        if (!button) return;

        if (!button.dataset.baseText) {
            button.dataset.baseText = button.textContent.trim();
        }

        const isCurrentMode = currentMode === mode;
        const shouldDisable = !isIdle && !isCurrentMode;

        button.disabled = shouldDisable;
        button.classList.toggle('btn-disabled', shouldDisable);
        button.classList.toggle('btn-active-mode', isCurrentMode);
        button.textContent = isCurrentMode
            ? `Running: ${button.dataset.baseText}`
            : button.dataset.baseText;
    });

    const startAvailability = getStartAvailabilityState(runtimeMonitorState);
    const startButton = document.getElementById('mode-start-button');
    if (startButton) {
        startButton.disabled = startAvailability.disabled;
        startButton.classList.toggle('btn-disabled', startAvailability.disabled);
        startButton.title = startAvailability.disabled ? startAvailability.detail : '';
    }

    const startStatus = document.getElementById('mode-start-status');
    if (startStatus) {
        startStatus.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
        startStatus.classList.add(`is-${startAvailability.tone}`);
        startStatus.textContent = `${startAvailability.title}. ${startAvailability.detail}`;
    }

    // Runtime controls: toggle button (pause/resume) + stop
    const findRuntimeButtons = (action, fallbackOnclick) => {
        const byAction = Array.from(document.querySelectorAll(`button[data-runtime-action="${action}"]`));
        const byFallback = Array.from(document.querySelectorAll(`button[onclick="${fallbackOnclick}"]`));
        return Array.from(new Set([...byAction, ...byFallback]));
    };

    const btnStopList = findRuntimeButtons('stop', 'stopProcess()');
    btnStopList.forEach((button) => {
        button.disabled = isIdle;
        button.classList.toggle('btn-disabled', isIdle);
    });

    const pauseResumeBtn = document.getElementById('runtime-pause-resume-btn');
    if (pauseResumeBtn) {
        const iconEl = pauseResumeBtn.querySelector('.operator-scheme-btn-icon');
        const labelEl = pauseResumeBtn.querySelector('.operator-scheme-btn-label');

        pauseResumeBtn.disabled = isIdle;
        pauseResumeBtn.classList.toggle('btn-disabled', isIdle);

        pauseResumeBtn.classList.remove('operator-scheme-btn-pause', 'operator-scheme-btn-start');
        if (currentPaused && !isIdle) {
            pauseResumeBtn.classList.add('operator-scheme-btn-start');
            pauseResumeBtn.dataset.runtimeAction = 'resume';
            pauseResumeBtn.setAttribute('onclick', 'resumeProcess()');
            if (iconEl) iconEl.textContent = '▶';
            if (labelEl) labelEl.textContent = 'Запустить';
        } else {
            pauseResumeBtn.classList.add('operator-scheme-btn-pause');
            pauseResumeBtn.dataset.runtimeAction = 'pause';
            pauseResumeBtn.setAttribute('onclick', 'pauseProcess()');
            if (iconEl) iconEl.textContent = '⏸';
            if (labelEl) labelEl.textContent = 'Пауза';
        }
    }

    const tbPauseBtn = document.getElementById('toolbar-pause-btn');
    if (tbPauseBtn) {
        tbPauseBtn.disabled = isIdle;
        tbPauseBtn.classList.toggle('btn-disabled', isIdle);
        if (currentPaused && !isIdle) {
            tbPauseBtn.classList.replace('warning', 'success');
            tbPauseBtn.innerHTML = '▶ Пуск';
        } else {
            tbPauseBtn.classList.replace('success', 'warning');
            tbPauseBtn.innerHTML = '⏸ Пауза';
        }
    }

    const tbStopBtn = document.getElementById('toolbar-stop-btn');
    if (tbStopBtn) {
        tbStopBtn.disabled = isIdle;
        tbStopBtn.classList.toggle('btn-disabled', isIdle);
    }

    // Main screen: when IDLE, hide scheme and show CTA to mode selection
    const schemeWrap = document.querySelector('#monitor .operator-scheme-wrap');
    const schemeControls = document.querySelector('#monitor .operator-scheme-controls');
    const modeCta = document.getElementById('monitor-idle-mode-cta');
    const modeCtaTitle = document.getElementById('monitor-idle-cta-title');
    const modeCtaText = document.getElementById('monitor-idle-cta-text');
    const modeCtaButton = document.getElementById('monitor-idle-cta-button');

    if (schemeWrap) schemeWrap.style.display = isIdle ? 'none' : '';
    if (schemeControls) schemeControls.style.display = isIdle ? 'none' : '';
    if (modeCta) modeCta.style.display = isIdle ? 'flex' : 'none';
    if (modeCta) {
        modeCta.classList.remove('is-good', 'is-warn', 'is-danger', 'is-muted');
        modeCta.classList.add(`is-${startAvailability.tone}`);
    }
    if (modeCtaTitle) modeCtaTitle.textContent = startAvailability.title;
    if (modeCtaText) modeCtaText.textContent = startAvailability.detail;
    if (modeCtaButton) {
        modeCtaButton.textContent = startAvailability.disabled ? '🛠 Проверить условия старта' : `🧭 ${startAvailability.buttonLabel}`;
        modeCtaButton.classList.remove('btn-primary', 'btn-warning', 'btn-danger', 'btn-success');
        modeCtaButton.classList.add(
            startAvailability.tone === 'danger'
                ? 'btn-danger'
                : (startAvailability.tone === 'good' ? 'btn-success' : (startAvailability.tone === 'warn' ? 'btn-warning' : 'btn-primary'))
        );
    }

    if (typeof window.renderControlStartState === 'function') {
        window.renderControlStartState();
    }

}


export function updateHeaterSlider() {

    const slider = document.getElementById('heater-power');

    const label = document.querySelector('label[for="heater-power"]');



    if (slider) {

        slider.max = maxHeaterPower;

        slider.step = 50;  // Шаг 50 Вт

    }



    if (label) {

        label.innerHTML = `Мощность нагрева: <span id="heater-value">0</span> Вт (макс ${maxHeaterPower})`;

    }

}
