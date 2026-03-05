// KPI strip — update toolbar dashboard values

export function updateKpiStrip(data) {
    if (data.t_cube !== undefined) {
        const el = document.getElementById('kpi-temp-cube');
        if (el) el.innerHTML = `${data.t_cube.toFixed(1)}<span class="toolbar-kpi-unit">°C</span>`;
    }
    if (data.t_column_top !== undefined) {
        const el = document.getElementById('kpi-tsarga');
        if (el) el.innerHTML = `${data.t_column_top.toFixed(1)}<span class="toolbar-kpi-unit">°C</span>`;
    }
    if (data.power !== undefined) {
        const el = document.getElementById('kpi-power');
        if (el) el.innerHTML = `${data.power.toFixed(0)}<span class="toolbar-kpi-unit">W</span>`;
    }
    if (data.p_cube !== undefined) {
        const el = document.getElementById('kpi-pressure');
        if (el) el.innerHTML = `${data.p_cube.toFixed(1)}<span class="toolbar-kpi-unit">мм</span>`;
    }
    if (data.pump_speed !== undefined) {
        const el = document.getElementById('kpi-pump');
        if (el) el.innerHTML = `${data.pump_speed.toFixed(0)}<span class="toolbar-kpi-unit">мл/ч</span>`;
    }
}
