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
    return Boolean(channel?.assigned || channel?.detected);
}

function hasVisibleRows() {
    return TEMPERATURE_ROWS.some((row) => isTemperatureChannelVisible(row.key));
}

export function syncMonitorGaugeVisibility() {
    TEMPERATURE_ROWS.forEach((row) => {
        setHidden(row.rowId, !isTemperatureChannelVisible(row.key));
    });

    setHidden('dashboard-temperatures-card', !hasVisibleRows());

    const powerAvailable = Boolean(
        runtimeMonitorState.power?.available ||
        runtimeMonitorState.equipment?.modules?.pzem004t?.available
    );
    setHidden('dashboard-power-card', !powerAvailable);
}
