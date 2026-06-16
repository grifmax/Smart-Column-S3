import { miniChart, setMiniChart, miniChartData, MINI_CHART_MAX_POINTS } from '../globals.js';
import { addLog } from '../core/logs.js';
import { runtimeMonitorState } from '../globals.js';

// ============================================================================

// Mini Chart

// ============================================================================

function isTempChannelVisible(key) {
    const channel = runtimeMonitorState.temperatureChannels?.[key];
    return Boolean(
        channel?.installed ||
        channel?.assigned ||
        channel?.detected ||
        channel?.valid
    );
}

function normalizeProcessTemperature(value) {
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) return null;
    if (numeric < 10 || numeric > 150) return null;
    return numeric;
}

function rebuildMiniChartSeries() {
    if (!miniChart) return;
    miniChart.updateSeries([
        {
            name: 'Куб',
            data: miniChartData.timestamps.map((t, i) => ({ x: t, y: miniChartData.cube[i] }))
        },
        {
            name: 'Царга верх',
            data: miniChartData.timestamps.map((t, i) => ({ x: t, y: miniChartData.columnTop[i] }))
        },
        {
            name: 'Дефлегматор',
            data: miniChartData.timestamps.map((t, i) => ({ x: t, y: miniChartData.reflux[i] }))
        }
    ]);
}

async function loadMiniChartHistory() {
    try {
        const response = await fetch('/api/charts/live');
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(payload?.error || `HTTP ${response.status}`);

        const deviceNowMs = Number(payload?.generatedAtMs || 0);
        const minutePoints = Array.isArray(payload?.minute) ? payload.minute : [];
        const tempMeta = payload?.meta?.temperatures && typeof payload.meta.temperatures === 'object'
            ? payload.meta.temperatures
            : {};
        const isVisibleFromMeta = (key) => {
            const channel = tempMeta?.[key];
            return Boolean(
                channel?.installed ||
                channel?.assigned ||
                channel?.detected ||
                isTempChannelVisible(key)
            );
        };
        const timestamps = [];
        const cube = [];
        const columnTop = [];
        const reflux = [];

        minutePoints.forEach((point) => {
            const sampleMs = Number(point?.ms || 0);
            const clientTs = deviceNowMs > 0
                ? (Date.now() - Math.max(0, deviceNowMs - sampleMs))
                : Date.now();
            timestamps.push(clientTs);
            cube.push(normalizeProcessTemperature(point.t_cube));
            columnTop.push(
                isVisibleFromMeta('columnTop')
                    ? normalizeProcessTemperature(point.t_column_top)
                    : null
            );
            reflux.push(
                isVisibleFromMeta('reflux')
                    ? normalizeProcessTemperature(point.t_reflux)
                    : null
            );
        });

        miniChartData.timestamps.splice(0, miniChartData.timestamps.length, ...timestamps.slice(-MINI_CHART_MAX_POINTS));
        miniChartData.cube.splice(0, miniChartData.cube.length, ...cube.slice(-MINI_CHART_MAX_POINTS));
        miniChartData.columnTop.splice(0, miniChartData.columnTop.length, ...columnTop.slice(-MINI_CHART_MAX_POINTS));
        miniChartData.reflux.splice(0, miniChartData.reflux.length, ...reflux.slice(-MINI_CHART_MAX_POINTS));
        rebuildMiniChartSeries();
    } catch (error) {
        addLog(`⚠ Не удалось загрузить историю мини-графика: ${error.message}`, 'warning');
    }
}



export function initMiniChart() {

    const miniChartContainer = document.querySelector("#mini-chart");
    if (!miniChartContainer) return;
    const isMobile = window.matchMedia('(max-width: 768px)').matches;

    // Graceful fallback for offline/AP mode when CDN script is not available.
    if (typeof window.ApexCharts === 'undefined') {
        miniChartContainer.innerHTML = '<div class="info-display">Мини-график временно недоступен</div>';
        addLog('⚠ Мини-график недоступен: библиотека графика не загружена', 'warning');
        setMiniChart(null);
        return;
    }

    const options = {

        chart: {
            type: 'line',
            height: isMobile ? 280 : 220,
            animations: {
                enabled: true,
                dynamicAnimation: { speed: 500 }
            },
            toolbar: {
                show: true,
                tools: {
                    download: !isMobile,
                    selection: !isMobile,
                    zoom: true,
                    zoomin: !isMobile,
                    zoomout: !isMobile,
                    pan: !isMobile,
                    reset: true
                },
                autoSelected: 'zoom',
                offsetX: isMobile ? -2 : 0,
                offsetY: isMobile ? -2 : 0
            },
            zoom: {
                enabled: true,
                type: 'x'
            },
            background: 'transparent'
        },

        theme: {

            mode: document.body.getAttribute('data-theme') || 'light'

        },

        series: [

            {

                name: 'Куб',

                data: []

            },

            {

                name: 'Царга верх',

                data: []

            },

            {

                name: 'Дефлегматор',

                data: []

            }

        ],

        xaxis: {

            type: 'datetime',

            labels: {

                datetimeFormatter: {

                    minute: 'HH:mm'

                }

            }

        },

        yaxis: {

            title: {

                text: '°C'

            },

            decimalsInFloat: 1

        },

        stroke: {

            curve: 'smooth',

            width: 2

        },

        colors: ['#dc3545', '#007bff', '#17a2b8'],

        legend: {

            show: true,

            position: isMobile ? 'bottom' : 'top',
            horizontalAlign: isMobile ? 'left' : 'center',
            fontSize: isMobile ? '11px' : '12px',
            itemMargin: {
                horizontal: isMobile ? 10 : 12,
                vertical: isMobile ? 6 : 4
            }

        },

        tooltip: {

            x: {

                format: 'HH:mm:ss'

            }

        },

        responsive: [
            {
                breakpoint: 768,
                options: {
                    chart: {
                        height: 280
                    },
                    legend: {
                        position: 'bottom',
                        horizontalAlign: 'left',
                        fontSize: '11px',
                        itemMargin: {
                            horizontal: 10,
                            vertical: 6
                        }
                    }
                }
            }
        ]

    };



    setMiniChart(new ApexCharts(miniChartContainer, options));
    miniChart.render();
    void loadMiniChartHistory();

}



export function updateMiniChart(data) {

    if (!miniChart) return;



    const now = new Date().getTime();



    // Добавить новые данные

    if (data.t_cube !== undefined) {

        miniChartData.timestamps.push(now);

        miniChartData.cube.push(
            data.tempValid?.cube === false ? null : normalizeProcessTemperature(data.t_cube)
        );

        miniChartData.columnTop.push(
            isTempChannelVisible('columnTop') && data.tempValid?.columnTop !== false
                ? normalizeProcessTemperature(data.t_column_top)
                : null
        );

        miniChartData.reflux.push(
            isTempChannelVisible('reflux') && data.tempValid?.reflux !== false
                ? normalizeProcessTemperature(data.t_reflux)
                : null
        );



        // Ограничить количество точек

        if (miniChartData.timestamps.length > MINI_CHART_MAX_POINTS) {

            miniChartData.timestamps.shift();

            miniChartData.cube.shift();

            miniChartData.columnTop.shift();

            miniChartData.reflux.shift();

        }



        // Обновить график
        rebuildMiniChartSeries();

    }

}
