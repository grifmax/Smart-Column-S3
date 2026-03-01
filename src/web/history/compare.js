import { selectedProcesses } from './list.js';

// ============================================================================

// Сравнение процессов

// ============================================================================



export let compareTempChart = null;

export let comparePowerChart = null;



export async function compareSelected() {

    if (selectedProcesses.size < 2) {

        alert('Выберите минимум 2 процесса для сравнения');

        return;

    }



    if (selectedProcesses.size > 5) {

        alert('Можно сравнить максимум 5 процессов одновременно');

        return;

    }



    try {

        addLog(`📊 Загрузка ${selectedProcesses.size} процессов для сравнения...`, 'info');



        // Загрузить все выбранные процессы

        const processes = [];

        for (const processId of selectedProcesses) {

            const response = await fetch(`/api/history/${processId}`);

            if (response.ok) {

                const process = await response.json();

                processes.push(process);

            }

        }



        if (processes.length < 2) {

            alert('Не удалось загрузить процессы для сравнения');

            return;

        }



        showCompareModal(processes);

        addLog(`✓ Сравнение ${processes.length} процессов`, 'info');

    } catch (error) {

        console.error('Error comparing processes:', error);

        addLog('❌ Ошибка при сравнении процессов', 'error');

        alert('Ошибка при сравнении процессов');

    }

}



export function showCompareModal(processes) {

    // Заполнить список процессов

    const processList = document.getElementById('compare-process-list');

    processList.innerHTML = '';



    const colors = ['#dc3545', '#007bff', '#28a745', '#ffc107', '#6f42c1'];



    processes.forEach((process, index) => {

        const typeNames = {

            rectification: 'Ректификация',

            distillation: 'Дистилляция',

            mashing: 'Затирка',

            hold: 'Выдержка'

        };



        const badge = document.createElement('div');

        badge.style.cssText = `

            padding: 10px 15px;

            background: ${colors[index]};

            color: white;

            border-radius: 6px;

            font-weight: 600;

            font-size: 0.9em;

        `;

        badge.textContent = `${typeNames[process.process.type] || process.process.type} - ${new Date(process.metadata.startTime * 1000).toLocaleDateString('ru-RU')}`;

        processList.appendChild(badge);

    });



    // Построить графики сравнения

    renderCompareTempChart(processes, colors);

    renderComparePowerChart(processes, colors);

    renderCompareTable(processes);



    // Показать модальное окно

    document.getElementById('compare-modal').classList.add('active');

    document.body.style.overflow = 'hidden';

}



export function closeCompareModal() {

    document.getElementById('compare-modal').classList.remove('active');

    document.body.style.overflow = '';



    // Уничтожить графики

    if (compareTempChart) {

        compareTempChart.destroy();

        compareTempChart = null;

    }

    if (comparePowerChart) {

        comparePowerChart.destroy();

        comparePowerChart = null;

    }

}



export function renderCompareTempChart(processes, colors) {

    const chartEl = document.getElementById('compare-temp-chart');

    chartEl.innerHTML = '';



    const series = [];



    processes.forEach((process, index) => {

        if (process.timeseries && process.timeseries.data && process.timeseries.data.length > 0) {

            const startDate = new Date(process.metadata.startTime * 1000).toLocaleDateString('ru-RU');

            series.push({

                name: `Процесс ${index + 1} (${startDate})`,

                data: process.timeseries.data.map(p => ({

                    x: p.time * 1000,

                    y: p.cube

                }))

            });

        }

    });



    if (series.length === 0) {

        chartEl.innerHTML = '<p style="text-align: center; padding: 20px;">Нет данных для сравнения</p>';

        return;

    }



    const options = {

        chart: {

            type: 'line',

            height: 400,

            animations: {

                enabled: false

            },

            toolbar: {
                show: true,
                tools: {
                    download: true,
                    selection: true,
                    zoom: true,
                    zoomin: true,
                    zoomout: true,
                    pan: true,
                    reset: true
                },
                autoSelected: 'zoom'
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

        series: series,

        xaxis: {

            type: 'datetime',

            labels: {

                datetimeFormatter: {

                    hour: 'HH:mm'

                }

            }

        },

        yaxis: {

            title: {

                text: 'Температура куба (°C)'

            },

            decimalsInFloat: 1

        },

        stroke: {

            curve: 'smooth',

            width: 2

        },

        colors: colors,

        legend: {

            show: true,

            position: 'top'

        },

        tooltip: {

            x: {

                format: 'dd MMM HH:mm'

            }

        }

    };



    compareTempChart = new ApexCharts(chartEl, options);

    compareTempChart.render();

}



export function renderComparePowerChart(processes, colors) {

    const chartEl = document.getElementById('compare-power-chart');

    chartEl.innerHTML = '';



    const series = [];



    processes.forEach((process, index) => {

        if (process.timeseries && process.timeseries.data && process.timeseries.data.length > 0) {

            const startDate = new Date(process.metadata.startTime * 1000).toLocaleDateString('ru-RU');

            series.push({

                name: `Процесс ${index + 1} (${startDate})`,

                data: process.timeseries.data.map(p => ({

                    x: p.time * 1000,

                    y: p.power

                }))

            });

        }

    });



    if (series.length === 0) {

        chartEl.innerHTML = '<p style="text-align: center; padding: 20px;">Нет данных для сравнения</p>';

        return;

    }



    const options = {

        chart: {

            type: 'line',

            height: 300,

            animations: {

                enabled: false

            },

            toolbar: {
                show: true,
                tools: {
                    download: true,
                    selection: true,
                    zoom: true,
                    zoomin: true,
                    zoomout: true,
                    pan: true,
                    reset: true
                },
                autoSelected: 'zoom'
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

        series: series,

        xaxis: {

            type: 'datetime',

            labels: {

                datetimeFormatter: {

                    hour: 'HH:mm'

                }

            }

        },

        yaxis: {

            title: {

                text: 'Мощность (Вт)'

            },

            decimalsInFloat: 0

        },

        stroke: {

            curve: 'smooth',

            width: 2

        },

        colors: colors,

        legend: {

            show: true,

            position: 'top'

        },

        tooltip: {

            x: {

                format: 'dd MMM HH:mm'

            }

        }

    };



    comparePowerChart = new ApexCharts(chartEl, options);

    comparePowerChart.render();

}



export function renderCompareTable(processes) {

    const tableEl = document.getElementById('compare-table');



    let html = '<table style="width: 100%; border-collapse: collapse; margin-top: 10px;">';

    html += '<thead><tr style="background: var(--bg-secondary);">';

    html += '<th style="padding: 10px; border: 1px solid var(--border-color);">Параметр</th>';



    processes.forEach((process, index) => {

        const startDate = new Date(process.metadata.startTime * 1000).toLocaleDateString('ru-RU');

        html += `<th style="padding: 10px; border: 1px solid var(--border-color);">Процесс ${index + 1}<br><span style="font-size: 0.8em; font-weight: normal;">${startDate}</span></th>`;

    });



    html += '</tr></thead><tbody>';



    // Строки таблицы

    const rows = [

        { label: 'Длительность', getValue: (p) => (p.metadata.duration / 3600).toFixed(1) + ' ч' },

        { label: 'Средняя мощность', getValue: (p) => (p.metrics?.power?.avgPower || 0) + ' Вт' },

        { label: 'Потреблено энергии', getValue: (p) => (p.metrics?.power?.energyUsed || 0).toFixed(2) + ' кВт·ч' },

        { label: 'Головы', getValue: (p) => (p.results?.headsCollected || 0) + ' мл' },

        { label: 'Тело', getValue: (p) => (p.results?.bodyCollected || 0) + ' мл' },

        { label: 'Хвосты', getValue: (p) => (p.results?.tailsCollected || 0) + ' мл' },

        { label: 'Всего собрано', getValue: (p) => (p.results?.totalCollected || 0) + ' мл' },

        { label: 'Статус', getValue: (p) => p.metadata.completedSuccessfully ? '✅ Успешно' : '⚠️ Прервано' }

    ];



    rows.forEach(row => {

        html += '<tr>';

        html += `<td style="padding: 10px; border: 1px solid var(--border-color); font-weight: 600;">${row.label}</td>`;

        processes.forEach(process => {

            html += `<td style="padding: 10px; border: 1px solid var(--border-color);">${row.getValue(process)}</td>`;

        });

        html += '</tr>';

    });



    html += '</tbody></table>';

    tableEl.innerHTML = html;

}
