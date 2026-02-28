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
            currentMode = 0;
            currentPaused = false;
            updateButtonStates();
            return;
        }



        const data = await response.json();



        // Обновить состояние процесса

        // Нормализуем mode: ожидаем число (0=IDLE), но на прокси/кастомных сборках
        // может прилететь строка. Для кнопок достаточно корректно определить IDLE.
        currentMode = resolveMode(data.mode, data.modeStr);

        currentPaused = Boolean(data.paused);



        // Сохранить мощность ТЭНа из настроек

        if (data.equipment && data.equipment.heaterPowerW) {

            maxHeaterPower = data.equipment.heaterPowerW;

            updateHeaterSlider();

        }



        // Обновить UI с новым форматом данных (не должен ломать обновление кнопок)
        try {
            updateUIFromStatus(data);
        } catch (e) {
            console.error('updateUIFromStatus error:', e);
        }



        // Обновить состояние кнопок
        updateButtonStates();



    } catch (e) {

        console.error('Ошибка загрузки статуса:', e);

    }

}



let prevModeForNotify = null;
let prevPhaseForNotify = null;

export function updateUIFromStatus(data) {
    let phaseText = '-';
    updateRuntimeStateFromStatus(data);

    const resolvedMode = (data.modeStr !== undefined || data.mode !== undefined) ? resolveMode(data.mode, data.modeStr) : currentMode;
    const currentPhase = data.phaseStr || '';

    // Отправка уведомлений о смене статуса
    if (typeof window.showNotification === 'function') {
        if (prevModeForNotify !== null && prevModeForNotify !== resolvedMode) {
            if (resolvedMode === MODE_IDLE) {
                window.showNotification('Процесс завершён', { body: `Режим: ${getModeLabel(prevModeForNotify)}` });
            } else if (prevModeForNotify === MODE_IDLE) {
                window.showNotification('Процесс запущен', { body: `Режим: ${getModeLabel(resolvedMode)}` });
            }
        } else if (prevPhaseForNotify !== null && prevPhaseForNotify !== currentPhase && resolvedMode !== MODE_IDLE && currentPhase && currentPhase !== '-') {
            window.showNotification('Смена этапа', { body: `Новый этап: ${currentPhase}` });
        }
    }
    prevModeForNotify = resolvedMode;
    prevPhaseForNotify = currentPhase;

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



    // Насос

    if (data.pump) {

        if (data.pump.speedMlH !== undefined) {

            const el = document.getElementById('pump-speed');

            if (el) el.textContent = data.pump.speedMlH.toFixed(0) + ' мл/ч';

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
        tCube: data.temps?.cube,
        power: data.power?.power,
        pressureCube: data.pressure?.cube,
        pumpSpeed: data.pump?.speedMlH,
        abv: getEffectiveAbvForCalculations().value,
        waterIn: data.temps?.waterIn,
        waterOut: data.temps?.waterOut,
        voltage: data.power?.voltage
    });

    // Обновить интерактивную схему (SVG) — также в polling-режиме
    updateInteractiveScheme(data);

    renderModeRuntimeCard();

}



export function updateButtonStates() {

    const isIdle = currentMode === MODE_IDLE;

    // Buttons that start modes
    const btnRect = document.querySelector('button[onclick="startRectification()"]');
    const btnManual = document.querySelector('button[onclick="startManual()"]');
    const btnDist = document.querySelector('button[onclick="startDistillation()"]');
    const btnMashing = document.querySelector('button[onclick="startMashing()"]');
    const btnHold = document.querySelector('button[onclick="startHold()"]');

    const modeButtons = [
        { mode: MODE_RECT, button: btnRect },
        { mode: MODE_MANUAL, button: btnManual },
        { mode: MODE_DIST, button: btnDist },
        { mode: MODE_MASH, button: btnMashing },
        { mode: MODE_HOLD, button: btnHold }
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

    // Runtime controls
    const btnStop = document.querySelector('button[onclick="stopProcess()"]');
    const btnPause = document.querySelector('button[onclick="pauseProcess()"]');
    const btnResume = document.querySelector('button[onclick="resumeProcess()"]');

    if (btnStop) {
        btnStop.disabled = isIdle;
        btnStop.classList.toggle('btn-disabled', isIdle);
    }

    if (btnPause) {
        const disablePause = isIdle || currentPaused;
        btnPause.disabled = disablePause;
        btnPause.classList.toggle('btn-disabled', disablePause);
    }

    if (btnResume) {
        const disableResume = isIdle || !currentPaused;
        btnResume.disabled = disableResume;
        btnResume.classList.toggle('btn-disabled', disableResume);
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
