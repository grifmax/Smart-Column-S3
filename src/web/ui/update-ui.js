import { updateMemoryStats } from '../core/memory.js';
import { setCurrentMode, setCurrentPaused, getModeLabel, getModeCssClass } from '../globals.js';
import { updateRuntimeStateFromWs } from '../runtime/state.js';
import { getEffectiveAbvForCalculations } from '../runtime/abv.js';
import { renderModeRuntimeCard } from '../runtime/bars.js';
import { updateMiniChart } from './mini-chart.js';
import { updateInteractiveScheme } from './scheme.js';
import { updateLandingUi } from './landing.js';
import { updateButtonStates } from '../core/status.js';
import { formatUptime } from '../core/utils.js';
import { addLog } from '../core/logs.js';
import { renderAbvValue } from '../runtime/abv.js';
import { updateKpiStrip } from './kpi-strip.js';
import { updateSafetyModal } from '../runtime/safety-modal.js';
import { updateProcessNotifications } from '../runtime/process-notifications.js';
import { syncStirrerUi } from '../settings/equipment.js';

export function updateUI(data) {
    let phaseText = undefined;
    updateRuntimeStateFromWs(data);
    updateProcessNotifications();

    // Режим

    if (data.mode !== undefined) {

        const modeNum = Number(data.mode);
        const modeLabel = Number.isFinite(modeNum)
            ? getModeLabel(modeNum).toUpperCase()
            : 'UNKNOWN';
        const modeClass = Number.isFinite(modeNum)
            ? getModeCssClass(modeNum)
            : 'mode-idle';
        const modeEl = document.getElementById('mode');

        modeEl.textContent = modeLabel;

        modeEl.className = `value ${modeClass}`;
        if (Number.isFinite(modeNum)) {
            setCurrentMode(modeNum);
        }

    }

    if (data.paused !== undefined) {
        setCurrentPaused(Boolean(data.paused));
    }



    // Фаза

    if (data.phase !== undefined) {

        const phaseNames = ['IDLE', 'HEATING', 'STABIL', 'HEADS', 'PURGE', 'BODY', 'TAILS', 'FINISH', 'ERROR'];

        phaseText = phaseNames[data.phase] || '-';
        document.getElementById('phase').textContent = phaseText;

    }



    // Температуры

    if (data.t_cube !== undefined) {

        document.getElementById('temp-cube').textContent = data.t_cube.toFixed(1) + '°C';

    }

    if (data.t_column_bottom !== undefined) {

        document.getElementById('temp-column-bottom').textContent = data.t_column_bottom.toFixed(1) + '°C';

    }

    if (data.t_column_top !== undefined) {

        document.getElementById('temp-column-top').textContent = data.t_column_top.toFixed(1) + '°C';

    }

    if (data.t_reflux !== undefined) {

        document.getElementById('temp-reflux').textContent = data.t_reflux.toFixed(1) + '°C';

    }

    if (data.t_tsa !== undefined) {

        document.getElementById('temp-tsa').textContent = data.t_tsa.toFixed(1) + '°C';

    }



    // Давление

    if (data.p_cube !== undefined) {

        document.getElementById('pressure-cube').textContent = data.p_cube.toFixed(1) + ' мм рт.ст.';

    }

    if (data.p_atm !== undefined) {

        document.getElementById('pressure-atm').textContent = data.p_atm.toFixed(1) + ' гПа';

    }

    if (data.p_flood !== undefined) {

        document.getElementById('pressure-flood').textContent = data.p_flood.toFixed(1) + ' мм';

    }



    // Мощность (PZEM-004T)

    if (data.voltage !== undefined) {

        document.getElementById('power-voltage').textContent = data.voltage.toFixed(1) + ' V';

    }

    if (data.current !== undefined) {

        document.getElementById('power-current').textContent = data.current.toFixed(2) + ' A';

    }

    if (data.power !== undefined) {

        document.getElementById('power-power').textContent = data.power.toFixed(0) + ' W';
        const distPowerFact = document.getElementById('dist-start-power-fact');
        if (distPowerFact) distPowerFact.value = Number(data.power).toFixed(0);

    }

    if (data.energy !== undefined) {

        document.getElementById('power-energy').textContent = data.energy.toFixed(3) + ' кВт·ч';

    }

    if (data.frequency !== undefined) {

        document.getElementById('power-frequency').textContent = data.frequency.toFixed(1) + ' Гц';

    }

    if (data.pf !== undefined) {

        document.getElementById('power-pf').textContent = data.pf.toFixed(2);

    }

    if (data.distillation && data.distillation.powerPercent !== undefined) {
        const distPowerSet = document.getElementById('dist-start-power-percent');
        if (distPowerSet && document.activeElement !== distPowerSet) {
            distPowerSet.value = String(Math.max(0, Math.min(100, Number(data.distillation.powerPercent) || 0)));
        }
    }



    // Насос

    if (data.pump_speed !== undefined) {

        document.getElementById('pump-speed').textContent = data.pump_speed.toFixed(0) + ' мл/ч';

    }

    if (data.pump_volume !== undefined) {

        document.getElementById('pump-volume').textContent = data.pump_volume.toFixed(0) + ' мл';

    }



    // Объёмы фракций

    if (data.volume_heads !== undefined) {

        document.getElementById('volume-heads').textContent = data.volume_heads.toFixed(0) + ' мл';

    }

    if (data.volume_body !== undefined) {

        document.getElementById('volume-body').textContent = data.volume_body.toFixed(0) + ' мл';

    }

    if (data.volume_tails !== undefined) {

        document.getElementById('volume-tails').textContent = data.volume_tails.toFixed(0) + ' мл';

    }



    // Ареометр

    renderAbvValue();



    // Uptime

    if (data.uptime !== undefined) {

        document.getElementById('uptime').textContent = formatUptime(data.uptime);
        const opUptime = document.getElementById('operator-uptime');
        if (opUptime) opUptime.textContent = formatUptime(data.uptime);

    }



    // События

    if (data.type === 'event') {

        addLog(data.message, data.level || 'info');

    }



    // Обновить мини-график

    updateMiniChart(data);



    // Обновить статистику памяти

    if (data.memory !== undefined) {

        updateMemoryStats(data.memory);

    }



    // Обновить анимацию колонны

    if (typeof updateColumnAnimation === 'function') {

        updateColumnAnimation(data);

    }

    // Обновить интерактивную схему (SVG)
    updateInteractiveScheme(data);

    updateLandingUi({
        mode: data.mode,
        modeStr: data.modeStr,
        phaseText,
        safetyOk: data.safetyOk,
        alarm: data.alarm,
        currentAlarm: data.currentAlarm,
        v2: data.v2,
        tCube: data.t_cube,
        power: data.power,
        pressureCube: data.p_cube,
        pumpSpeed: data.pump_speed,
        abv: getEffectiveAbvForCalculations().value,
        waterIn: data.t_water_in,
        waterOut: data.t_water_out,
        voltage: data.voltage
    });
    syncStirrerUi({ syncSpeedInput: true });

    renderModeRuntimeCard();
    updateButtonStates();
    updateKpiStrip(data);
    updateSafetyModal(data);
}
