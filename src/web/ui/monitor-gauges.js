import { runtimeMonitorState } from '../globals.js';

const TEMPERATURE_ROWS = [
    { key: 'columnBottom', rowId: 'temp-column-bottom-row' },
    { key: 'columnTop', rowId: 'temp-column-top-row' },
    { key: 'reflux', rowId: 'temp-reflux-row' },
    { key: 'tsa', rowId: 'temp-tsa-row' },
    { key: 'waterIn', rowId: 'landing-water-in-row' },
    { key: 'waterOut', rowId: 'landing-water-out-row' }
];

const GAUGE_SECTION_LABELS = {
    temperatures: 'Температуры',
    power: 'Питание',
    volumes: 'Объёмы',
    stirrer: 'Мешалка',
    chart: 'График'
};

let gaugeFilterBindingsReady = false;
let activeGaugeFilter = 'temperatures';

function getGaugeSectionLabel(section) {
    switch (section) {
    case 'temperatures':
        return 'Температуры';
    case 'power':
        return 'Питание';
    case 'volumes':
        return 'Объёмы';
    case 'stirrer':
        return 'Мешалка';
    case 'chart':
        return 'График';
    default:
        return 'Показометры';
    }
}

function setHidden(id, hidden) {
    const el = document.getElementById(id);
    if (el) el.hidden = Boolean(hidden);
}

function getAvailableGaugeSections() {
    return Object.keys(GAUGE_SECTION_LABELS).filter((section) => isSectionAvailable(section));
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

function hasVolumeSectionData() {
    const volumes = runtimeMonitorState?.volumes || {};
    const pump = runtimeMonitorState?.pump || {};
    const lifecycle = String(runtimeMonitorState?.v2?.lifecycle || 'idle').toLowerCase();

    return Boolean(
        Number(toFiniteOrZero(volumes.heads)) > 0 ||
        Number(toFiniteOrZero(volumes.body)) > 0 ||
        Number(toFiniteOrZero(volumes.tails)) > 0 ||
        Number(toFiniteOrZero(pump.totalMl)) > 0 ||
        Number(toFiniteOrZero(pump.speedMlH)) > 0 ||
        hasPressureSignal() ||
        hasPressureMargin() ||
        lifecycle === 'starting' ||
        lifecycle === 'running' ||
        lifecycle === 'paused'
    );
}

function isStirrerVisible() {
    return Boolean(
        runtimeMonitorState?.stirrer?.available ||
        runtimeMonitorState?.equipment?.modules?.mcp4725?.available
    );
}

function toFiniteOrZero(value) {
    const num = Number(value);
    return Number.isFinite(num) ? num : 0;
}

function getGaugeSectionCards() {
    return Array.from(document.querySelectorAll('[data-gauge-section]'));
}

function isSectionAvailable(section) {
    return getGaugeSectionCards().some((card) => (
        card.dataset.gaugeSection === section &&
        !card.hidden
    ));
}

function getFirstAvailableSection() {
    return Object.keys(GAUGE_SECTION_LABELS).find((section) => isSectionAvailable(section)) || 'temperatures';
}

function updateGaugeSummaryBadge(section) {
    const badge = document.getElementById('monitor-gauges-badge');
    if (!badge) return;
    badge.textContent = getGaugeSectionLabel(section);
}

function applyGaugeFilter(section = activeGaugeFilter) {
    const nextSection = isSectionAvailable(section) ? section : getFirstAvailableSection();
    activeGaugeFilter = nextSection;

    getGaugeSectionCards().forEach((card) => {
        const matches = card.dataset.gaugeSection === nextSection;
        card.classList.toggle('is-gauge-filter-hidden', !matches);
    });

    document.querySelectorAll('[data-gauge-filter]').forEach((button) => {
        const selected = button.dataset.gaugeFilter === nextSection;
        button.classList.toggle('is-active', selected);
        button.setAttribute('aria-selected', selected ? 'true' : 'false');
        button.hidden = !isSectionAvailable(button.dataset.gaugeFilter || '');
    });

    updateGaugeSummaryBadge(nextSection);
}

function ensureGaugeFilterBindings() {
    if (gaugeFilterBindingsReady) return;
    gaugeFilterBindingsReady = true;

    document.querySelectorAll('[data-gauge-filter]').forEach((button) => {
        button.addEventListener('click', () => {
            activeGaugeFilter = button.dataset.gaugeFilter || 'temperatures';
            applyGaugeFilter(activeGaugeFilter);
        });
    });
}

export function syncMonitorGaugeVisibility() {
    ensureGaugeFilterBindings();

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

    setHidden('dashboard-volumes-card', !hasVolumeSectionData());

    const pressureVisible = hasPressureSignal();
    const pressureMarginVisible = pressureVisible && hasPressureMargin();
    setHidden('pressure-atm-row', !pressureVisible);
    setHidden('pressure-flood-row', !pressureMarginVisible);

    setHidden('monitor-stirrer-card', !isStirrerVisible());
    setHidden('dashboard-mini-chart-card', !temperaturesVisible);

    const filtersEl = document.getElementById('monitor-gauges-filters');
    if (filtersEl) {
        filtersEl.hidden = getAvailableGaugeSections().length <= 1;
    }

    applyGaugeFilter(activeGaugeFilter);
}
