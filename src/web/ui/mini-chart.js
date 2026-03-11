import { miniChart, setMiniChart, miniChartData, MINI_CHART_MAX_POINTS } from '../globals.js';
import { addLog } from '../core/logs.js';

// ============================================================================

// Mini Chart

// ============================================================================



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

}



export function updateMiniChart(data) {

    if (!miniChart) return;



    const now = new Date().getTime();



    // Добавить новые данные

    if (data.t_cube !== undefined) {

        miniChartData.timestamps.push(now);

        miniChartData.cube.push(data.t_cube);

        miniChartData.columnTop.push(data.t_column_top || null);

        miniChartData.reflux.push(data.t_reflux || null);



        // Ограничить количество точек

        if (miniChartData.timestamps.length > MINI_CHART_MAX_POINTS) {

            miniChartData.timestamps.shift();

            miniChartData.cube.shift();

            miniChartData.columnTop.shift();

            miniChartData.reflux.shift();

        }



        // Обновить график

        miniChart.updateSeries([

            {

                name: 'Куб',

                data: miniChartData.timestamps.map((t, i) => ({

                    x: t,

                    y: miniChartData.cube[i]

                }))

            },

            {

                name: 'Царга верх',

                data: miniChartData.timestamps.map((t, i) => ({

                    x: t,

                    y: miniChartData.columnTop[i]

                }))

            },

            {

                name: 'Дефлегматор',

                data: miniChartData.timestamps.map((t, i) => ({

                    x: t,

                    y: miniChartData.reflux[i]

                }))

            }

        ]);

    }

}
