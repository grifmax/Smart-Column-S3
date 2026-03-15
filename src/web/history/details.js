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

function formatReasonCode(reasonCode) {

    const raw = String(reasonCode || '').trim();

    if (!raw || raw === 'RC_NONE') {

        return '';

    }

    return raw.replace(/^RC_/, '').replace(/_/g, ' ').toLowerCase();

}

function appendInfoItem(container, label, value) {

    const item = document.createElement('div');

    item.className = 'modal-info-item';

    const labelEl = document.createElement('div');

    labelEl.className = 'modal-info-label';

    labelEl.textContent = label;

    const valueEl = document.createElement('div');

    valueEl.className = 'modal-info-value';

    valueEl.textContent = value;

    item.appendChild(labelEl);

    item.appendChild(valueEl);

    container.appendChild(item);

}

function appendPhaseDetail(container, label, value) {

    const detail = document.createElement('div');

    detail.className = 'modal-phase-detail';

    detail.appendChild(document.createTextNode(`${label}: `));

    const strong = document.createElement('strong');

    strong.textContent = value;

    detail.appendChild(strong);

    container.appendChild(detail);

}

function appendEventSection(container, title, events, tone) {

    if (!Array.isArray(events) || events.length === 0) {

        return;

    }

    const section = document.createElement('div');

    section.className = 'modal-info-item modal-info-item-wide';

    const titleEl = document.createElement('div');

    titleEl.className = 'modal-info-label';

    titleEl.textContent = title;

    const countEl = document.createElement('div');

    countEl.className = 'modal-info-value';

    countEl.textContent = `${events.length} ${events.length === 1 ? 'событие' : (events.length < 5 ? 'события' : 'событий')}`;

    const listEl = document.createElement('div');

    listEl.className = 'modal-event-list';

    events.forEach((eventItem) => {

        const row = document.createElement('div');

        row.className = `modal-event-item ${tone === 'error' ? 'is-error' : 'is-warning'}`;

        const timestamp = Number(eventItem?.time || 0);

        if (timestamp > 0) {

            const metaEl = document.createElement('div');

            metaEl.className = 'modal-event-meta';

            metaEl.textContent = new Date(timestamp * 1000).toLocaleString('ru-RU');

            row.appendChild(metaEl);

        }

        const messageEl = document.createElement('div');

        messageEl.className = 'modal-event-message';

        messageEl.textContent = String(eventItem?.message || 'Без текста');

        row.appendChild(messageEl);

        const reasonCode = formatReasonCode(eventItem?.reasonCode);

        if (reasonCode) {

            const reasonEl = document.createElement('div');

            reasonEl.className = 'modal-event-extra';

            reasonEl.textContent = `Причина: ${reasonCode}`;

            row.appendChild(reasonEl);

        }

        const operatorMessage = String(eventItem?.operatorMessage || '').trim();

        if (operatorMessage) {

            const operatorEl = document.createElement('div');

            operatorEl.className = 'modal-event-extra';

            operatorEl.textContent = `Комментарий: ${operatorMessage}`;

            row.appendChild(operatorEl);

        }

        listEl.appendChild(row);

    });

    section.appendChild(titleEl);

    section.appendChild(countEl);

    section.appendChild(listEl);

    container.appendChild(section);

}

function appendNotesSection(container, notes) {

    const text = String(notes || '').trim();

    if (!text) {

        return;

    }

    const section = document.createElement('div');

    section.className = 'modal-info-item modal-info-item-wide';

    const titleEl = document.createElement('div');

    titleEl.className = 'modal-info-label';

    titleEl.textContent = 'Заметки';

    const textEl = document.createElement('div');

    textEl.className = 'modal-note-text';

    textEl.textContent = text;

    section.appendChild(titleEl);

    section.appendChild(textEl);

    container.appendChild(section);

}



export function showHistoryDetailsModal(process) {

    const typeNames = {

        rectification: 'Ректификация',

        distillation: 'Дистилляция',

        mashing: 'Затирка',

        hold: 'Выдержка',

        nbk: 'НБК',

        fermentation: 'Ферментация'

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



    const resultsGrid = document.getElementById('modal-results-grid');

    resultsGrid.innerHTML = '';

    appendInfoItem(resultsGrid, 'Головы', `${process.results.headsCollected || 0} мл`);
    appendInfoItem(resultsGrid, 'Тело', `${process.results.bodyCollected || 0} мл`);
    appendInfoItem(resultsGrid, 'Хвосты', `${process.results.tailsCollected || 0} мл`);
    appendInfoItem(resultsGrid, 'Всего собрано', `${process.results.totalCollected || 0} мл`);
    appendEventSection(resultsGrid, 'Ошибки и аварии', process.results?.errors || [], 'error');
    appendEventSection(resultsGrid, 'Предупреждения', process.results?.warnings || [], 'warning');
    appendNotesSection(resultsGrid, process.notes);



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



        const nameEl = document.createElement('div');

        nameEl.className = 'modal-phase-name';

        nameEl.textContent = phaseName;

        const detailsEl = document.createElement('div');

        detailsEl.className = 'modal-phase-details';

        appendPhaseDetail(detailsEl, 'Начало', startDate.toLocaleTimeString('ru-RU'));
        appendPhaseDetail(detailsEl, 'Окончание', endDate.toLocaleTimeString('ru-RU'));
        appendPhaseDetail(detailsEl, 'Длительность', `${(phase.duration / 60).toFixed(0)} мин`);
        appendPhaseDetail(detailsEl, 'Объём', `${phase.volume || 0} мл`);
        appendPhaseDetail(detailsEl, 'Средняя скорость', `${phase.avgSpeed || 0} мл/ч`);

        const reasonCode = formatReasonCode(phase.reasonCode);
        if (reasonCode) {
            appendPhaseDetail(detailsEl, 'Причина', reasonCode);
        }

        const operatorMessage = String(phase.operatorMessage || '').trim();
        if (operatorMessage) {
            appendPhaseDetail(detailsEl, 'Комментарий', operatorMessage);
        }

        phaseEl.appendChild(nameEl);
        phaseEl.appendChild(detailsEl);



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
