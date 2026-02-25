export async function viewHistoryDetails(id) {

    try {

        const response = await fetch(`/api/history/${id}`);

        if (!response.ok) {

            throw new Error('Failed to load history details');

        }



        const process = await response.json();



        // Создать модальное окно с деталями

        showHistoryDetailsModal(process);



        addLog(`👁️ Просмотр процесса ${id}`, 'info');

    } catch (error) {

        console.error('Error loading history details:', error);

        addLog('❌ Ошибка загрузки деталей процесса', 'error');

        alert('Ошибка загрузки деталей процесса');

    }

}



export let tempChart = null;

export let powerChart = null;



export function showHistoryDetailsModal(process) {

    const typeNames = {

        rectification: 'Ректификация',

        distillation: 'Дистилляция',

        mashing: 'Затирка',

        hold: 'Выдержка'

    };



    const startDate = new Date(process.metadata.startTime * 1000);

    const endDate = new Date(process.metadata.endTime * 1000);

    const typeName = typeNames[process.process.type] || process.process.type;



    // Установить заголовок

    document.getElementById('modal-title').textContent = `${typeName} - ${startDate.toLocaleDateString('ru-RU')}`;



    // Заполнить основную информацию

    const infoGrid = document.getElementById('modal-info-grid');

    infoGrid.innerHTML = `

        <div class="modal-info-item">

            <div class="modal-info-label">Тип процесса</div>

            <div class="modal-info-value">${typeName}</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Режим</div>

            <div class="modal-info-value">${process.process.mode === 'auto' ? 'Авто' : 'Ручной'}</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Начало</div>

            <div class="modal-info-value">${startDate.toLocaleString('ru-RU')}</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Окончание</div>

            <div class="modal-info-value">${endDate.toLocaleString('ru-RU')}</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Длительность</div>

            <div class="modal-info-value">${(process.metadata.duration / 3600).toFixed(1)} ч</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Статус</div>

            <div class="modal-info-value">${process.metadata.completedSuccessfully ? '✅ Успешно' : '⚠️ Прервано'}</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Средняя мощность</div>

            <div class="modal-info-value">${process.metrics?.power?.avgPower || 0} Вт</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Потреблено энергии</div>

            <div class="modal-info-value">${(process.metrics?.power?.energyUsed || 0).toFixed(2)} кВт·ч</div>

        </div>

    `;



    // Построить график температур

    renderTempChart(process);



    // Построить график мощности

    renderPowerChart(process);



    // Заполнить фазы

    renderPhases(process);



    // Заполнить результаты

    const resultsGrid = document.getElementById('modal-results-grid');

    resultsGrid.innerHTML = `

        <div class="modal-info-item">

            <div class="modal-info-label">Головы</div>

            <div class="modal-info-value">${process.results.headsCollected || 0} мл</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Тело</div>

            <div class="modal-info-value">${process.results.bodyCollected || 0} мл</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Хвосты</div>

            <div class="modal-info-value">${process.results.tailsCollected || 0} мл</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Всего собрано</div>

            <div class="modal-info-value">${process.results.totalCollected || 0} мл</div>

        </div>

    `;



    // Привязать обработчики к кнопкам экспорта

    const exportCsvBtn = document.getElementById('modal-export-csv');

    const exportJsonBtn = document.getElementById('modal-export-json');



    if (exportCsvBtn) {

        exportCsvBtn.onclick = () => exportHistoryCSV(process.id);

    }



    if (exportJsonBtn) {

        exportJsonBtn.onclick = () => exportHistoryJSON(process.id);

    }



    // Показать модальное окно

    document.getElementById('history-modal').classList.add('active');

    document.body.style.overflow = 'hidden';

}



export function closeHistoryModal() {

    document.getElementById('history-modal').classList.remove('active');

    document.body.style.overflow = '';



    // Уничтожить графики

    if (tempChart) {

        tempChart.destroy();

        tempChart = null;

    }

    if (powerChart) {

        powerChart.destroy();

        powerChart = null;

    }

}



export function renderTempChart(process) {

    const chartEl = document.getElementById('modal-temp-chart');

    chartEl.innerHTML = '';



    if (!process.timeseries || process.timeseries.data.length === 0) {

        chartEl.innerHTML = '<p style="text-align: center; color: var(--text-secondary); padding: 20px;">Нет данных временного ряда</p>';

        return;

    }



    const data = process.timeseries.data;



    const options = {

        chart: {

            type: 'line',

            height: 350,

            animations: {

                enabled: false

            },

            toolbar: {

                show: true

            },

            background: 'transparent'

        },

        theme: {

            mode: document.body.getAttribute('data-theme') || 'light'

        },

        series: [

            {

                name: 'Куб',

                data: data.map(p => ({ x: p.time * 1000, y: p.cube }))

            },

            {

                name: 'Царга верх',

                data: data.map(p => ({ x: p.time * 1000, y: p.columnTop }))

            }

        ],

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

                text: 'Температура (°C)'

            },

            decimalsInFloat: 1

        },

        stroke: {

            curve: 'smooth',

            width: 2

        },

        colors: ['#dc3545', '#007bff'],

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



    tempChart = new ApexCharts(chartEl, options);

    tempChart.render();

}



export function renderPowerChart(process) {

    const chartEl = document.getElementById('modal-power-chart');

    chartEl.innerHTML = '';



    if (!process.timeseries || process.timeseries.data.length === 0) {

        chartEl.innerHTML = '<p style="text-align: center; color: var(--text-secondary); padding: 20px;">Нет данных временного ряда</p>';

        return;

    }



    const data = process.timeseries.data;



    const options = {

        chart: {

            type: 'area',

            height: 300,

            animations: {

                enabled: false

            },

            toolbar: {

                show: true

            },

            background: 'transparent'

        },

        theme: {

            mode: document.body.getAttribute('data-theme') || 'light'

        },

        series: [

            {

                name: 'Мощность',

                data: data.map(p => ({ x: p.time * 1000, y: p.power }))

            }

        ],

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

        fill: {

            type: 'gradient',

            gradient: {

                shadeIntensity: 1,

                opacityFrom: 0.7,

                opacityTo: 0.3

            }

        },

        colors: ['#28a745'],

        tooltip: {

            x: {

                format: 'dd MMM HH:mm'

            }

        }

    };



    powerChart = new ApexCharts(chartEl, options);

    powerChart.render();

}



export function renderPhases(process) {

    const phasesEl = document.getElementById('modal-phases');



    if (!process.phases || process.phases.length === 0) {

        phasesEl.innerHTML = '<p style="text-align: center; color: var(--text-secondary); padding: 20px;">Нет информации о фазах</p>';

        return;

    }



    const phaseNames = {

        heating: 'Нагрев',

        stabilization: 'Стабилизация',

        heads: 'Отбор голов',

        body: 'Отбор тела',

        tails: 'Отбор хвостов',

        purge: 'Очистка',

        finish: 'Завершение'

    };



    phasesEl.innerHTML = '';



    process.phases.forEach(phase => {

        const phaseEl = document.createElement('div');

        phaseEl.className = 'modal-phase-item';



        const phaseName = phaseNames[phase.name] || phase.name;

        const startDate = new Date(phase.startTime * 1000);

        const endDate = new Date(phase.endTime * 1000);



        phaseEl.innerHTML = `

            <div class="modal-phase-name">${phaseName}</div>

            <div class="modal-phase-details">

                <div class="modal-phase-detail">Начало: <strong>${startDate.toLocaleTimeString('ru-RU')}</strong></div>

                <div class="modal-phase-detail">Окончание: <strong>${endDate.toLocaleTimeString('ru-RU')}</strong></div>

                <div class="modal-phase-detail">Длительность: <strong>${(phase.duration / 60).toFixed(0)} мин</strong></div>

                <div class="modal-phase-detail">Объём: <strong>${phase.volume || 0} мл</strong></div>

                <div class="modal-phase-detail">Средняя скорость: <strong>${phase.avgSpeed || 0} мл/ч</strong></div>

            </div>

        `;



        phasesEl.appendChild(phaseEl);

    });

}



// Закрытие модального окна при клике на overlay

document.addEventListener('DOMContentLoaded', function () {

    const modalOverlay = document.getElementById('history-modal');

    if (modalOverlay) {

        modalOverlay.addEventListener('click', function (e) {

            if (e.target === modalOverlay) {

                closeHistoryModal();

            }

        });

    }

});
