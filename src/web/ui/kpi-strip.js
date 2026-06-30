import {
    getStatusTemperature,
    getStatusPower,
    getStatusPressure,
    getStatusPump,
    isStatusTempInvalid
} from '../core/status-values.js';
import { runtimeMonitorState } from '../globals.js';

function setHidden(id, hidden) {
    const el = document.getElementById(id);
    if (el) el.hidden = Boolean(hidden);
}

function renderKpiValue(id, value, digits, unit) {
    const el = document.getElementById(id);
    if (!el) return;

    if (value === undefined || value === null) {
        el.innerHTML = `--<span class="toolbar-kpi-unit">${unit}</span>`;
        return;
    }

    el.innerHTML = `${value.toFixed(digits)}<span class="toolbar-kpi-unit">${unit}</span>`;
}

function isTempChannelVisible(key) {
    const channel = runtimeMonitorState.temperatureChannels?.[key];
    return Boolean(
        channel?.installed ||
        channel?.assigned ||
        channel?.detected ||
        channel?.valid
    );
}

function hasPressureSignal() {
    const pressure = runtimeMonitorState.pressure || {};
    return Boolean(
        pressure.ok ||
        pressure.ads1115Available ||
        (Number.isFinite(Number(pressure.cube)) && Number(pressure.cube) > 0)
    );
}

function hasPowerSignal(data) {
    if (data?.power?.available === false || data?.pzem_ok === false) return false;
    return Boolean(
        runtimeMonitorState.power?.available ||
        runtimeMonitorState.equipment?.modules?.pzem004t?.available
    );
}

export function updateKpiStrip(data) {
    const cubeTemp = isStatusTempInvalid(data, 'cube')
        ? undefined
        : getStatusTemperature(data, 'cube', 't_cube');
    const columnTopVisible = isTempChannelVisible('columnTop');
    const columnTopTemp = columnTopVisible && !isStatusTempInvalid(data, 'columnTop')
        ? getStatusTemperature(data, 'columnTop', 't_column_top')
        : undefined;
    const powerVisible = hasPowerSignal(data);
    const powerValue = powerVisible
        ? getStatusPower(data, 'power', 'power')
        : undefined;
    const pressureVisible = hasPressureSignal();
    const pressureValue = pressureVisible
        ? getStatusPressure(data, 'cube', 'p_cube')
        : undefined;
    const pumpSpeed = getStatusPump(data, 'speedMlH', 'pump_speed');

    renderKpiValue('kpi-temp-cube', cubeTemp, 1, '°C');
    renderKpiValue('kpi-tsarga', columnTopTemp, 1, '°C');
    renderKpiValue('kpi-power', powerValue, 0, 'W');
    renderKpiValue('kpi-pressure', pressureValue, 1, 'мм');
    renderKpiValue('kpi-pump', pumpSpeed, 0, 'мл/ч');

    setHidden('kpi-card-temp-cube', cubeTemp === undefined);
    setHidden('kpi-card-tsarga', !columnTopVisible);
    setHidden('kpi-card-power', !powerVisible);
    setHidden('kpi-card-pressure', !pressureVisible);
    setHidden('kpi-card-pump', pumpSpeed === undefined || pumpSpeed === null);
}
