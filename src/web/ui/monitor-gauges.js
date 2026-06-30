import { runtimeMonitorState } from '../globals.js';

const TEMPERATURE_ROWS = [
    { key: 'columnBottom', rowId: 'temp-column-bottom-row' },
    { key: 'columnTop', rowId: 'temp-column-top-row' },
    { key: 'reflux', rowId: 'temp-reflux-row' },
    { key: 'tsa', rowId: 'temp-tsa-row' },
    { key: 'waterIn', rowId: 'landing-water-in-row' },
    { key: 'waterOut', rowId: 'landing-water-out-row' }
];

function setHidden(id, hidden) {
    const el = document.getElementById(id);
    if (el) el.hidden = Boolean(hidden);
}

function isTemperatureChannelVisible(key) {
    const channel = runtimeMonitorState.temperatureChannels?.[key];
    return Boolean(
        channel?.installed ||
        channel?.assigned ||
        channel?.detected ||
        channel?.valid
    );
}

function hasVisibleRows() {
    return TEMPERATURE_ROWS.some((row) => isTemperatureChannelVisible(row.key));
}

function hasPressureSignal() {
    const pressure = runtimeMonitorState.pressure || {};
    return Boolean(
        pressure.ok ||
        pressure.ads1115Available ||
        (Number.isFinite(Number(pressure.cube)) && Number(pressure.cube) > 0) ||
        (Number.isFinite(Number(pressure.atm)) && Number(pressure.atm) > 0)
    );
}

function hasPressureMargin() {
    const margin = Number(runtimeMonitorState?.v2?.indicators?.distPressureMargin);
    return Number.isFinite(margin);
}

function isStirrerVisible() {
    return Boolean(
        runtimeMonitorState?.stirrer?.available ||
        runtimeMonitorState?.equipment?.modules?.mcp4725?.available
    );
}

export function syncMonitorGaugeVisibility() {
    TEMPERATURE_ROWS.forEach((row) => {
        setHidden(row.rowId, !isTemperatureChannelVisible(row.key));
    });

    const temperaturesVisible = hasVisibleRows();
    setHidden('dashboard-temperatures-card', !temperaturesVisible);

    const powerAvailable = Boolean(
        runtimeMonitorState.power?.available ||
        runtimeMonitorState.equipment?.modules?.pzem004t?.available
    );
    setHidden('dashboard-power-card', !powerAvailable);

    const pressureVisible = hasPressureSignal();
    const pressureMarginVisible = pressureVisible && hasPressureMargin();
    setHidden('pressure-atm-row', !pressureVisible);
    setHidden('pressure-flood-row', !pressureMarginVisible);

    setHidden('monitor-stirrer-card', !isStirrerVisible());
    setHidden('dashboard-mini-chart-card', !temperaturesVisible);
}
