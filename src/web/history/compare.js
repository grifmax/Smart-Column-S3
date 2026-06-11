import { selectedProcesses } from './list.js';
import {
    buildBaselineComparisonSummary,
    closeHistoryModal,
    evaluateRunDelta,
    findPreviousSuccessfulProcessSummary,
    getEnergyPerLiter,
    getIndicatorShares,
    getProcessProfile
} from './details.js';

// ============================================================================
// Сравнение процессов
// ============================================================================

const COMPARE_COLORS = ['#dc3545', '#007bff', '#28a745', '#ffc107', '#6f42c1'];

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

        const processes = [];
        for (const processId of selectedProcesses) {
            const response = await fetch(`/api/history/${processId}`);
            if (response.ok) {
                processes.push(await response.json());
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

export async function compareProcessWithBaseline(process, options = {}) {

    try {
        const currentProcess = process && typeof process === 'object'
            ? process
            : await loadProcessById(process);

        if (!currentProcess?.id) {
            alert('Не удалось загрузить текущий прогон для сравнения');
            return;
        }

        let previousSummary = options.previousSummary || findPreviousSuccessfulProcessSummary(currentProcess);
        let previousSuccessfulProcess = options.previousSuccessfulProcess || null;

        if (!previousSuccessfulProcess && previousSummary?.id) {
            previousSuccessfulProcess = await loadProcessById(previousSummary.id);
        }

        if (!previousSuccessfulProcess?.id) {
            alert('Для этого профиля пока нет прошлого успешного baseline');
            return;
        }

        closeHistoryModal();

        const profileName = getProcessProfile(currentProcess) || 'без профиля';
        showCompareModal([previousSuccessfulProcess, currentProcess], {
            title: `Профиль "${profileName}": текущий прогон vs baseline`,
            compareMode: 'baseline',
            baselineProcessId: String(previousSuccessfulProcess.id || '').trim(),
            currentProcessId: String(currentProcess.id || '').trim(),
            previousSummary,
            profileName
        });

        addLog(`📈 Baseline compare для профиля "${profileName}"`, 'info');
    } catch (error) {
        console.error('Error comparing process with baseline:', error);
        addLog('❌ Ошибка при загрузке baseline сравнения', 'error');
        alert('Ошибка при подготовке baseline сравнения');
    }

}

async function loadProcessById(processId) {

    const id = String(processId || '').trim();
    if (!id) {
        return null;
    }

    const response = await fetch(`/api/history/${id}`);
    if (!response.ok) {
        return null;
    }

    return response.json();

}

function getCompareTypeName(process) {

    const typeNames = {
        rectification: 'Ректификация',
        distillation: 'Дистилляция',
        mashing: 'Затирка',
        hold: 'Пастеризация',
        nbk: 'НБК',
        fermentation: 'Ферментация'
    };

    return typeNames[process?.process?.type] || process?.process?.type || 'Процесс';

}

function getCompareRole(process, options = {}) {

    const processId = String(process?.id || '').trim();
    if (!processId) {
        return '';
    }

    if (processId === String(options.baselineProcessId || '').trim()) {
        return 'baseline';
    }

    if (processId === String(options.currentProcessId || '').trim()) {
        return 'current';
    }

    return '';

}

function getCompareSeriesName(process, index, options = {}) {

    const role = getCompareRole(process, options);
    const startDate = new Date(Number(process?.metadata?.startTime || 0) * 1000).toLocaleDateString('ru-RU');

    if (role === 'baseline') {
        return `Baseline (${startDate})`;
    }

    if (role === 'current') {
        return `Текущий (${startDate})`;
    }

    return `Процесс ${index + 1} (${startDate})`;

}

function formatDurationHours(seconds) {

    return `${(Number(seconds || 0) / 3600).toFixed(1)} ч`;

}

function formatPercent(value) {

    return `${(Number(value || 0) * 100).toFixed(0)}%`;

}

function formatSignedPercent(value, digits = 0) {

    const numeric = Number(value || 0);
    const sign = numeric > 0 ? '+' : '';
    return `${sign}${(numeric * 100).toFixed(digits)}%`;

}

function formatSignedNumber(value, digits = 1, suffix = '') {

    const numeric = Number(value || 0);
    const sign = numeric > 0 ? '+' : '';
    return `${sign}${numeric.toFixed(digits)}${suffix}`;

}

function formatCompareBadgeValue(value, empty = '—') {

    const text = String(value ?? '').trim();
    return text || empty;

}

function renderCompareOverview(processes, options = {}) {

    const sectionEl = document.getElementById('compare-overview-section');
    const titleEl = document.getElementById('compare-overview-title');
    const contentEl = document.getElementById('compare-overview');

    if (!sectionEl || !titleEl || !contentEl) {
        return;
    }

    const isBaselineCompare = options.compareMode === 'baseline' && processes.length === 2;
    if (!isBaselineCompare) {
        sectionEl.style.display = 'none';
        contentEl.innerHTML = '';
        return;
    }

    const baselineProcess = processes.find((item) => getCompareRole(item, options) === 'baseline') || processes[0];
    const currentProcess = processes.find((item) => getCompareRole(item, options) === 'current') || processes[1];
    const profileName = options.profileName || getProcessProfile(currentProcess) || 'без профиля';
    const summaryItems = buildBaselineComparisonSummary(currentProcess, baselineProcess, options.previousSummary || null);
    const delta = evaluateRunDelta(currentProcess, baselineProcess);
    const baselineIndicators = getIndicatorShares(baselineProcess);
    const currentIndicators = getIndicatorShares(currentProcess);
    const baselineEnergy = getEnergyPerLiter(baselineProcess);
    const currentEnergy = getEnergyPerLiter(currentProcess);
    const baselineDate = new Date(Number(baselineProcess?.metadata?.startTime || 0) * 1000).toLocaleString('ru-RU');
    const currentDate = new Date(Number(currentProcess?.metadata?.startTime || 0) * 1000).toLocaleString('ru-RU');

    titleEl.textContent = `🧭 Baseline профиля: ${profileName}`;
    sectionEl.style.display = '';

    const metricRows = [
        {
            label: 'Длительность',
            baseline: formatDurationHours(baselineProcess?.metadata?.duration || 0),
            current: formatDurationHours(currentProcess?.metadata?.duration || 0),
            delta: Number(baselineProcess?.metadata?.duration || 0) > 0
                ? formatSignedPercent((Number(currentProcess?.metadata?.duration || 0) - Number(baselineProcess?.metadata?.duration || 0)) / Number(baselineProcess?.metadata?.duration || 0))
                : '—'
        },
        {
            label: 'Энергия на литр',
            baseline: baselineEnergy !== null ? `${baselineEnergy.toFixed(2)} кВт·ч/л` : '—',
            current: currentEnergy !== null ? `${currentEnergy.toFixed(2)} кВт·ч/л` : '—',
            delta: baselineEnergy !== null && currentEnergy !== null && baselineEnergy > 0
                ? formatSignedPercent((currentEnergy - baselineEnergy) / baselineEnergy)
                : '—'
        },
        {
            label: 'Стабильность',
            baseline: baselineIndicators ? formatPercent(baselineIndicators.avgStability) : '—',
            current: currentIndicators ? formatPercent(currentIndicators.avgStability) : '—',
            delta: delta ? formatSignedPercent(delta.stabilityDelta) : '—'
        },
        {
            label: 'Окно отбора',
            baseline: baselineIndicators ? formatPercent(baselineIndicators.takeoffShare) : '—',
            current: currentIndicators ? formatPercent(currentIndicators.takeoffShare) : '—',
            delta: delta ? formatSignedPercent(delta.takeoffDelta) : '—'
        },
        {
            label: 'Flood risk max',
            baseline: baselineIndicators ? formatPercent(baselineIndicators.maxFloodRisk) : '—',
            current: currentIndicators ? formatPercent(currentIndicators.maxFloodRisk) : '—',
            delta: delta ? formatSignedPercent(delta.floodDelta) : '—'
        },
        {
            label: 'Cooling margin min',
            baseline: baselineIndicators ? `${baselineIndicators.minCoolingMargin.toFixed(1)}°C` : '—',
            current: currentIndicators ? `${currentIndicators.minCoolingMargin.toFixed(1)}°C` : '—',
            delta: delta ? formatSignedNumber(delta.coolingDelta, 1, '°C') : '—'
        }
    ];

    contentEl.innerHTML = `
        <div class="compare-overview-grid">
            <div class="compare-overview-card is-baseline">
                <div class="compare-overview-label">Эталонный прогон</div>
                <strong>${getCompareTypeName(baselineProcess)}</strong>
                <p>${baselineDate}</p>
                <p>Профиль: ${formatCompareBadgeValue(getProcessProfile(baselineProcess), 'без профиля')}</p>
                <p>Статус: ${baselineProcess?.metadata?.completedSuccessfully ? '✅ успешный' : '⚠️ неуспешный'}</p>
            </div>
            <div class="compare-overview-card is-current">
                <div class="compare-overview-label">Текущий прогон</div>
                <strong>${getCompareTypeName(currentProcess)}</strong>
                <p>${currentDate}</p>
                <p>Профиль: ${formatCompareBadgeValue(getProcessProfile(currentProcess), 'без профиля')}</p>
                <p>Итоговый score: ${delta ? formatSignedNumber(delta.weightedScore, 2) : '—'}</p>
            </div>
        </div>
        <div class="compare-overview-metrics">
            <table class="compare-overview-table">
                <thead>
                    <tr>
                        <th>Метрика</th>
                        <th>Baseline</th>
                        <th>Текущий</th>
                        <th>Δ</th>
                    </tr>
                </thead>
                <tbody>
                    ${metricRows.map((row) => `
                        <tr>
                            <td>${row.label}</td>
                            <td>${row.baseline}</td>
                            <td>${row.current}</td>
                            <td>${row.delta}</td>
                        </tr>
                    `).join('')}
                </tbody>
            </table>
        </div>
        <div class="compare-overview-insights">
            ${summaryItems.map((item) => `
                <div class="modal-history-insight is-${String(item?.tone || 'muted').trim() || 'muted'}">
                    <div class="modal-history-insight-head">
                        <strong>${String(item?.title || '').trim()}</strong>
                    </div>
                    <p class="modal-history-insight-text">${String(item?.detail || '').trim()}</p>
                    <p class="modal-history-insight-action">${String(item?.action || '').trim()}</p>
                </div>
            `).join('')}
        </div>
    `;

}

export function showCompareModal(processes, options = {}) {

    closeCompareModal();

    const modalTitleEl = document.getElementById('compare-modal-title');
    const processList = document.getElementById('compare-process-list');
    if (!processList || !modalTitleEl) {
        return;
    }

    modalTitleEl.textContent = options.title || `Сравнение процессов (${processes.length})`;
    processList.innerHTML = '';

    processes.forEach((process, index) => {
        const role = getCompareRole(process, options);
        const badge = document.createElement('div');
        badge.className = `compare-process-badge ${role ? `is-${role}` : ''}`;
        badge.style.setProperty('--compare-accent', COMPARE_COLORS[index % COMPARE_COLORS.length]);

        const badgeRole = role === 'baseline'
            ? 'BASELINE'
            : (role === 'current' ? 'CURRENT' : `#${index + 1}`);

        badge.innerHTML = `
            <div class="compare-process-badge-role">${badgeRole}</div>
            <strong>${getCompareTypeName(process)}</strong>
            <span>${new Date(Number(process?.metadata?.startTime || 0) * 1000).toLocaleString('ru-RU')}</span>
            <span>Профиль: ${formatCompareBadgeValue(getProcessProfile(process), 'без профиля')}</span>
        `;
        processList.appendChild(badge);
    });

    renderCompareOverview(processes, options);
    renderCompareTempChart(processes, COMPARE_COLORS, options);
    renderComparePowerChart(processes, COMPARE_COLORS, options);
    renderCompareTable(processes, options);

    document.getElementById('compare-modal').classList.add('active');
    document.body.style.overflow = 'hidden';

}

export function closeCompareModal() {

    const modalEl = document.getElementById('compare-modal');
    if (modalEl) {
        modalEl.classList.remove('active');
    }

    document.body.style.overflow = '';

    if (compareTempChart) {
        compareTempChart.destroy();
        compareTempChart = null;
    }

    if (comparePowerChart) {
        comparePowerChart.destroy();
        comparePowerChart = null;
    }

}

export function renderCompareTempChart(processes, colors, options = {}) {

    const chartEl = document.getElementById('compare-temp-chart');
    chartEl.innerHTML = '';

    const series = [];

    processes.forEach((process, index) => {
        if (process.timeseries && process.timeseries.data && process.timeseries.data.length > 0) {
            series.push({
                name: getCompareSeriesName(process, index, options),
                data: process.timeseries.data.map((point) => ({
                    x: point.time * 1000,
                    y: point.cube
                }))
            });
        }
    });

    if (series.length === 0) {
        chartEl.innerHTML = '<p style="text-align: center; padding: 20px;">Нет данных для сравнения</p>';
        return;
    }

    compareTempChart = new ApexCharts(chartEl, {
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
        series,
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
        colors,
        legend: {
            show: true,
            position: 'top'
        },
        tooltip: {
            x: {
                format: 'dd MMM HH:mm'
            }
        }
    });

    compareTempChart.render();

}

export function renderComparePowerChart(processes, colors, options = {}) {

    const chartEl = document.getElementById('compare-power-chart');
    chartEl.innerHTML = '';

    const series = [];

    processes.forEach((process, index) => {
        if (process.timeseries && process.timeseries.data && process.timeseries.data.length > 0) {
            series.push({
                name: getCompareSeriesName(process, index, options),
                data: process.timeseries.data.map((point) => ({
                    x: point.time * 1000,
                    y: point.power
                }))
            });
        }
    });

    if (series.length === 0) {
        chartEl.innerHTML = '<p style="text-align: center; padding: 20px;">Нет данных для сравнения</p>';
        return;
    }

    comparePowerChart = new ApexCharts(chartEl, {
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
        series,
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
        colors,
        legend: {
            show: true,
            position: 'top'
        },
        tooltip: {
            x: {
                format: 'dd MMM HH:mm'
            }
        }
    });

    comparePowerChart.render();

}

export function renderCompareTable(processes, options = {}) {

    const tableEl = document.getElementById('compare-table');
    if (!tableEl) {
        return;
    }

    const rows = [
        { label: 'Длительность', getValue: (process) => formatDurationHours(process?.metadata?.duration || 0) },
        { label: 'Средняя мощность', getValue: (process) => `${Math.round(Number(process?.metrics?.power?.avgPower || 0))} Вт` },
        { label: 'Потреблено энергии', getValue: (process) => `${Number(process?.metrics?.power?.energyUsed || 0).toFixed(2)} кВт·ч` },
        { label: 'Энергия на литр', getValue: (process) => {
            const value = getEnergyPerLiter(process);
            return value !== null ? `${value.toFixed(2)} кВт·ч/л` : '—';
        } },
        { label: 'Головы', getValue: (process) => `${Number(process?.results?.headsCollected || 0)} мл` },
        { label: 'Тело', getValue: (process) => `${Number(process?.results?.bodyCollected || 0)} мл` },
        { label: 'Хвосты', getValue: (process) => `${Number(process?.results?.tailsCollected || 0)} мл` },
        { label: 'Всего собрано', getValue: (process) => `${Number(process?.results?.totalCollected || 0)} мл` },
        { label: 'Стабильность', getValue: (process) => {
            const indicators = getIndicatorShares(process);
            return indicators ? formatPercent(indicators.avgStability) : '—';
        } },
        { label: 'Окно отбора', getValue: (process) => {
            const indicators = getIndicatorShares(process);
            return indicators ? formatPercent(indicators.takeoffShare) : '—';
        } },
        { label: 'Flood risk max', getValue: (process) => {
            const indicators = getIndicatorShares(process);
            return indicators ? formatPercent(indicators.maxFloodRisk) : '—';
        } },
        { label: 'Cooling margin min', getValue: (process) => {
            const indicators = getIndicatorShares(process);
            return indicators ? `${indicators.minCoolingMargin.toFixed(1)}°C` : '—';
        } },
        { label: 'Статус', getValue: (process) => process?.metadata?.completedSuccessfully ? '✅ Успешно' : '⚠️ Прервано' }
    ];

    tableEl.innerHTML = `
        <table class="compare-table">
            <thead>
                <tr>
                    <th>Параметр</th>
                    ${processes.map((process, index) => `
                        <th>
                            ${getCompareSeriesName(process, index, options)}
                            <br>
                            <span>${getCompareTypeName(process)}</span>
                        </th>
                    `).join('')}
                </tr>
            </thead>
            <tbody>
                ${rows.map((row) => `
                    <tr>
                        <td>${row.label}</td>
                        ${processes.map((process) => `<td>${row.getValue(process)}</td>`).join('')}
                    </tr>
                `).join('')}
            </tbody>
        </table>
    `;

}
