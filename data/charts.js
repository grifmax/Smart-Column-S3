// Smart-Column S3 - Charts with ApexCharts

let ws = null;
let reconnectInterval = null;
let isConnected = false;
let chartsPaused = false;
let currentPeriod = 60; // секунды
const DATA_RETENTION_SECONDS = 6 * 60 * 60; // 6 часов буфера в памяти браузера
const chartLogs = [];

// Anomaly detection settings
let anomalyDetectionEnabled = false;
let anomalyThreshold = 15; // процент отклонения от среднего
let anomalyWindow = 300; // секунды для расчета среднего (5 минут)
let anomalyMarkers = {}; // хранилище маркеров для каждого графика

// Chart instances
let chartsInstances = {
    temperatures: null,
    pressure: null,
    power: null,
    pump: null,
    abv: null,
    fractions: null
};

// Data buffers
let chartData = {
    temperatures: { time: [], cube: [], columnBottom: [], columnTop: [], reflux: [], tsa: [], waterIn: [], waterOut: [] },
    pressure: { time: [], cube: [], atm: [], flood: [] },
    power: { time: [], voltage: [], current: [], power: [], energy: [], frequency: [], pf: [] },
    pump: { time: [], speed: [], volume: [] },
    abv: { time: [], value: [] },
    fractions: { heads: 0, body: 0, tails: 0 }
};

// ============================================================================
// Initialization
// ============================================================================

document.addEventListener('DOMContentLoaded', function() {
    loadChartPreferences();
    setupCheckboxListeners();
    setupPeriodButtons();
    setupAnomalyControls();
    loadAnomalySettings();
    connectWebSocket();
    if (initCharts()) {
        addChartLog('Страница графиков готова', 'info');
    } else {
        addChartLog('Страница графиков готова, но библиотека графиков не загрузилась', 'warning');
    }
});

// ============================================================================
// Logs
// ============================================================================

function addChartLog(message, type = 'info') {
    const container = document.getElementById('log-container');
    const timestamp = new Date().toLocaleTimeString();
    const line = `[${timestamp}] ${message}`;
    chartLogs.push(line);
    if (chartLogs.length > 500) chartLogs.shift();

    if (!container) return;
    const entry = document.createElement('div');
    entry.className = `log-entry ${type}`;
    entry.textContent = line;
    container.appendChild(entry);
    container.scrollTop = container.scrollHeight;

    const entries = container.querySelectorAll('.log-entry');
    if (entries.length > 300) entries[0].remove();
}

function clearLogs() {
    if (!confirm('Очистить логи?')) return;
    chartLogs.length = 0;
    const container = document.getElementById('log-container');
    if (container) container.innerHTML = '';
    addChartLog('Логи очищены', 'info');
}

function downloadLogs() {
    const rows = ['timestamp,message'];
    chartLogs.forEach((line) => {
        const match = line.match(/^\[(.*?)\]\s*(.*)$/);
        const ts = match ? match[1] : '';
        const msg = match ? match[2] : line;
        rows.push(`"${ts.replace(/"/g, '""')}","${msg.replace(/"/g, '""')}"`);
    });

    const blob = new Blob([rows.join('\n')], { type: 'text/csv;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `smart-column-logs-${Date.now()}.csv`;
    link.click();
    URL.revokeObjectURL(url);
    addChartLog('Экспорт логов выполнен', 'success');
}

// ============================================================================
// WebSocket
// ============================================================================

function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;

    try {
        ws = new WebSocket(wsUrl);

        ws.onopen = function() {
            isConnected = true;
            updateConnectionStatus(true);
            addChartLog('WebSocket подключен', 'success');
            if (reconnectInterval) {
                clearInterval(reconnectInterval);
                reconnectInterval = null;
            }
        };

        ws.onmessage = function(event) {
            if (chartsPaused) return;

            try {
                const data = JSON.parse(event.data);
                updateChartData(data);
            } catch (e) {
                console.error('JSON parse error:', e);
                addChartLog(`Ошибка JSON: ${e.message}`, 'error');
            }
        };

        ws.onerror = function(error) {
            console.error('WebSocket error:', error);
            addChartLog('Ошибка WebSocket', 'error');
        };

        ws.onclose = function() {
            isConnected = false;
            updateConnectionStatus(false);
            addChartLog('WebSocket отключен, ожидаю переподключение', 'warning');

            if (!reconnectInterval) {
                reconnectInterval = setInterval(() => {
                    if (!isConnected) connectWebSocket();
                }, 5000);
            }
        };
    } catch (e) {
        console.error('WebSocket creation error:', e);
        updateConnectionStatus(false);
        addChartLog(`Ошибка создания WebSocket: ${e.message}`, 'error');
    }
}

function updateConnectionStatus(connected) {
    const statusDot = document.getElementById('connection-status');
    const statusText = document.getElementById('connection-text');
    if (!statusDot || !statusText) return;

    if (connected) {
        statusDot.className = 'status-dot online';
        statusText.textContent = 'Подключено';
    } else {
        statusDot.className = 'status-dot offline';
        statusText.textContent = 'Отключено';
    }
}

// ============================================================================
// Chart Initialization
// ============================================================================

function initCharts() {
    if (typeof window.ApexCharts === 'undefined') {
        document.querySelectorAll('.chart, .chart-bar').forEach((el) => {
            if (!el) return;
            el.innerHTML = '<div class="info-display">Графики временно недоступны: библиотека ApexCharts не загрузилась</div>';
        });
        return false;
    }

    // Общие настройки для всех графиков
    const commonOptions = {
        chart: {
            type: 'line',
            height: 350,
            animations: {
                enabled: true,
                easing: 'linear',
                dynamicAnimation: { speed: 1000 }
            },
            toolbar: { show: true },
            zoom: { enabled: true }
        },
        stroke: { curve: 'smooth', width: 2 },
        xaxis: {
            type: 'datetime',
            labels: { datetimeUTC: false }
        },
        tooltip: {
            x: { format: 'HH:mm:ss' }
        },
        legend: {
            show: true,
            position: 'top'
        }
    };

    // График температур
    chartsInstances.temperatures = new ApexCharts(document.querySelector("#chart-temperatures"), {
        ...commonOptions,
        series: [
            { name: 'Куб', data: [] },
            { name: 'Царга низ', data: [] },
            { name: 'Царга верх', data: [] },
            { name: 'Дефлегматор', data: [] },
            { name: 'ТСА', data: [] },
            { name: 'Вода вход', data: [] },
            { name: 'Вода выход', data: [] }
        ],
        colors: ['#dc3545', '#007bff', '#28a745', '#17a2b8', '#ffc107', '#6c757d', '#6610f2'],
        yaxis: {
            title: { text: 'Температура (°C)' },
            decimalsInFloat: 1
        }
    });
    chartsInstances.temperatures.render();

    // График давления (две оси Y)
    chartsInstances.pressure = new ApexCharts(document.querySelector("#chart-pressure"), {
        ...commonOptions,
        series: [
            { name: 'Куб (мм рт.ст.)', data: [] },
            { name: 'Атмосферное (гПа)', data: [] },
            { name: 'Порог захлёба', data: [] }
        ],
        colors: ['#007bff', '#28a745', '#dc3545'],
        yaxis: [
            {
                title: { text: 'Давление куба (мм рт.ст.)' },
                decimalsInFloat: 1
            },
            {
                opposite: true,
                title: { text: 'Атм. давление (гПа)' },
                decimalsInFloat: 1
            },
            {
                show: false
            }
        ],
        stroke: {
            width: [2, 2, 1],
            dashArray: [0, 0, 5]
        }
    });
    chartsInstances.pressure.render();

    // График мощности
    chartsInstances.power = new ApexCharts(document.querySelector("#chart-power"), {
        ...commonOptions,
        series: [
            { name: 'Напряжение (V)', data: [] },
            { name: 'Ток (A)', data: [] },
            { name: 'Мощность (W)', data: [] },
            { name: 'Энергия (кВт·ч)', data: [] },
            { name: 'Частота (Гц)', data: [] },
            { name: 'PF', data: [] }
        ],
        colors: ['#ffc107', '#dc3545', '#007bff', '#28a745', '#17a2b8', '#6610f2'],
        yaxis: [
            { title: { text: 'Напряжение (V)' }, decimalsInFloat: 1 },
            { show: false },
            { show: false },
            { show: false },
            { show: false },
            { show: false }
        ]
    });
    chartsInstances.power.render();

    // График насоса
    chartsInstances.pump = new ApexCharts(document.querySelector("#chart-pump"), {
        ...commonOptions,
        series: [
            { name: 'Скорость (мл/ч)', data: [] },
            { name: 'Объём (мл)', data: [] }
        ],
        colors: ['#007bff', '#28a745'],
        yaxis: [
            { title: { text: 'Скорость (мл/ч)' }, decimalsInFloat: 0 },
            { opposite: true, title: { text: 'Объём (мл)' }, decimalsInFloat: 0 }
        ]
    });
    chartsInstances.pump.render();

    // График крепости
    chartsInstances.abv = new ApexCharts(document.querySelector("#chart-abv"), {
        ...commonOptions,
        series: [{ name: 'ABV (%)', data: [] }],
        colors: ['#6610f2'],
        yaxis: {
            title: { text: 'Крепость (%)' },
            decimalsInFloat: 1,
            min: 0,
            max: 100
        }
    });
    chartsInstances.abv.render();

    // График фракций (столбчатый)
    chartsInstances.fractions = new ApexCharts(document.querySelector("#chart-fractions"), {
        chart: {
            type: 'bar',
            height: 300,
            toolbar: { show: true }
        },
        series: [{
            name: 'Объём (мл)',
            data: [0, 0, 0]
        }],
        colors: ['#dc3545', '#28a745', '#ffc107'],
        xaxis: {
            categories: ['Головы', 'Тело', 'Хвосты']
        },
        yaxis: {
            title: { text: 'Объём (мл)' },
            decimalsInFloat: 0
        },
        plotOptions: {
            bar: {
                distributed: true,
                horizontal: false
            }
        },
        dataLabels: {
            enabled: true,
            formatter: function(val) {
                return val.toFixed(0) + ' мл';
            }
        },
        legend: { show: false }
    });
    chartsInstances.fractions.render();
    return true;
}

// ============================================================================
// Data Update
// ============================================================================

function updateChartData(data) {
    const now = new Date().getTime();

    // Температуры
    if (data.t_cube !== undefined) {
        appendData(chartData.temperatures, 'time', now);
        appendData(chartData.temperatures, 'cube', data.t_cube);
        appendData(chartData.temperatures, 'columnBottom', data.t_column_bottom || 0);
        appendData(chartData.temperatures, 'columnTop', data.t_column_top || 0);
        appendData(chartData.temperatures, 'reflux', data.t_reflux || 0);
        appendData(chartData.temperatures, 'tsa', data.t_tsa || 0);
        appendData(chartData.temperatures, 'waterIn', data.t_water_in || 0);
        appendData(chartData.temperatures, 'waterOut', data.t_water_out || 0);

        updateChart('temperatures', [
            chartData.temperatures.cube,
            chartData.temperatures.columnBottom,
            chartData.temperatures.columnTop,
            chartData.temperatures.reflux,
            chartData.temperatures.tsa,
            chartData.temperatures.waterIn,
            chartData.temperatures.waterOut
        ]);

        // Детекция аномалий для температур
        detectAnomalies('temperatures', 'Куб', chartData.temperatures.cube, chartData.temperatures.time);
        detectAnomalies('temperatures', 'Царга верх', chartData.temperatures.columnTop, chartData.temperatures.time);
        detectAnomalies('temperatures', 'Дефлегматор', chartData.temperatures.reflux, chartData.temperatures.time);
    }

    // Давление
    if (data.p_cube !== undefined) {
        appendData(chartData.pressure, 'time', now);
        appendData(chartData.pressure, 'cube', data.p_cube);
        appendData(chartData.pressure, 'atm', data.p_atm || 1013);
        appendData(chartData.pressure, 'flood', data.p_flood || 0);

        updateChart('pressure', [
            chartData.pressure.cube,
            chartData.pressure.atm,
            chartData.pressure.flood
        ]);

        // Детекция аномалий для давления
        detectAnomalies('pressure', 'Куб', chartData.pressure.cube, chartData.pressure.time);
    }

    // Мощность
    if (data.voltage !== undefined) {
        appendData(chartData.power, 'time', now);
        appendData(chartData.power, 'voltage', data.voltage);
        appendData(chartData.power, 'current', data.current || 0);
        appendData(chartData.power, 'power', data.power || 0);
        appendData(chartData.power, 'energy', data.energy || 0);
        appendData(chartData.power, 'frequency', data.frequency || 50);
        appendData(chartData.power, 'pf', data.pf || 0);

        updateChart('power', [
            chartData.power.voltage,
            chartData.power.current,
            chartData.power.power,
            chartData.power.energy,
            chartData.power.frequency,
            chartData.power.pf
        ]);

        // Детекция аномалий для мощности
        detectAnomalies('power', 'Мощность', chartData.power.power, chartData.power.time);
        detectAnomalies('power', 'Ток', chartData.power.current, chartData.power.time);
    }

    // Насос
    if (data.pump_speed !== undefined) {
        appendData(chartData.pump, 'time', now);
        appendData(chartData.pump, 'speed', data.pump_speed);
        appendData(chartData.pump, 'volume', data.pump_volume || 0);

        updateChart('pump', [
            chartData.pump.speed,
            chartData.pump.volume
        ]);
    }

    // Крепость
    if (data.abv !== undefined) {
        appendData(chartData.abv, 'time', now);
        appendData(chartData.abv, 'value', data.abv);

        updateChart('abv', [chartData.abv.value]);
    }

    // Фракции (обновляем только значения)
    if (data.volume_heads !== undefined) {
        chartData.fractions.heads = data.volume_heads;
        chartData.fractions.body = data.volume_body || 0;
        chartData.fractions.tails = data.volume_tails || 0;

        if (chartsInstances.fractions) {
            chartsInstances.fractions.updateSeries([{
                data: [
                    chartData.fractions.heads,
                    chartData.fractions.body,
                    chartData.fractions.tails
                ]
            }]);
        }
    }

    // Uptime
    if (data.uptime !== undefined) {
        document.getElementById('uptime').textContent = formatUptime(data.uptime);
    }

    // Очистка устаревших данных (независимо от выбранного окна просмотра)
    pruneRetainedData();
}

function appendData(buffer, key, value) {
    if (!buffer[key]) buffer[key] = [];
    buffer[key].push(value);
}

function updateChart(chartName, seriesData) {
    const chart = chartsInstances[chartName];
    if (!chart) return;

    const timeArray = chartData[chartName].time;
    const cutoffTime = currentPeriod === 0 ? 0 : Date.now() - (currentPeriod * 1000);
    const startIndex = getStartIndexByCutoff(timeArray, cutoffTime);
    const newSeries = seriesData.map((data) => ({
        data: buildSeriesPoints(timeArray, data, startIndex)
    }));

    chart.updateSeries(newSeries, false);
}

function getStartIndexByCutoff(timeArray, cutoffTime) {
    if (!Array.isArray(timeArray) || timeArray.length === 0 || cutoffTime <= 0) {
        return 0;
    }
    for (let i = 0; i < timeArray.length; i++) {
        if (timeArray[i] >= cutoffTime) return i;
    }
    return timeArray.length;
}

function buildSeriesPoints(timeArray, valuesArray, startIndex) {
    const points = [];
    for (let i = startIndex; i < timeArray.length; i++) {
        points.push({
            x: timeArray[i],
            y: valuesArray[i]
        });
    }
    return points;
}

function pruneRetainedData() {
    if (DATA_RETENTION_SECONDS <= 0) return;

    const cutoffTime = Date.now() - (DATA_RETENTION_SECONDS * 1000);

    Object.keys(chartData).forEach(chartName => {
        if (chartName === 'fractions') return; // Пропускаем столбчатый график

        const buffer = chartData[chartName];
        if (!buffer.time || buffer.time.length === 0) return;

        const cutoffIndex = getStartIndexByCutoff(buffer.time, cutoffTime);

        if (cutoffIndex > 0 && cutoffIndex < buffer.time.length) {
            Object.keys(buffer).forEach(key => {
                buffer[key] = buffer[key].slice(cutoffIndex);
            });
        } else if (cutoffIndex >= buffer.time.length) {
            Object.keys(buffer).forEach(key => {
                buffer[key] = [];
            });
        }
    });
}

function refreshVisibleWindow() {
    if (chartData.temperatures.time.length > 0) {
        updateChart('temperatures', [
            chartData.temperatures.cube,
            chartData.temperatures.columnBottom,
            chartData.temperatures.columnTop,
            chartData.temperatures.reflux,
            chartData.temperatures.tsa,
            chartData.temperatures.waterIn,
            chartData.temperatures.waterOut
        ]);
    }

    if (chartData.pressure.time.length > 0) {
        updateChart('pressure', [
            chartData.pressure.cube,
            chartData.pressure.atm,
            chartData.pressure.flood
        ]);
    }

    if (chartData.power.time.length > 0) {
        updateChart('power', [
            chartData.power.voltage,
            chartData.power.current,
            chartData.power.power,
            chartData.power.energy,
            chartData.power.frequency,
            chartData.power.pf
        ]);
    }

    if (chartData.pump.time.length > 0) {
        updateChart('pump', [
            chartData.pump.speed,
            chartData.pump.volume
        ]);
    }

    if (chartData.abv.time.length > 0) {
        updateChart('abv', [chartData.abv.value]);
    }
}

// ============================================================================
// Checkboxes
// ============================================================================

function setupCheckboxListeners() {
    // Температуры
    ['cube', 'column-bottom', 'column-top', 'reflux', 'tsa', 'water-in', 'water-out'].forEach((name, index) => {
        const checkbox = document.getElementById(`check-temp-${name}`);
        if (checkbox) {
            checkbox.addEventListener('change', () => {
                toggleSeries('temperatures', index, checkbox.checked);
                saveChartPreferences();
            });
        }
    });

    // Давление
    ['cube', 'atm', 'flood'].forEach((name, index) => {
        const checkbox = document.getElementById(`check-pressure-${name}`);
        if (checkbox) {
            checkbox.addEventListener('change', () => {
                toggleSeries('pressure', index, checkbox.checked);
                saveChartPreferences();
            });
        }
    });

    // Мощность
    ['voltage', 'current', 'power', 'energy', 'frequency', 'pf'].forEach((name, index) => {
        const checkbox = document.getElementById(`check-power-${name}`);
        if (checkbox) {
            checkbox.addEventListener('change', () => {
                toggleSeries('power', index, checkbox.checked);
                saveChartPreferences();
            });
        }
    });

    // Насос
    ['speed', 'volume'].forEach((name, index) => {
        const checkbox = document.getElementById(`check-pump-${name}`);
        if (checkbox) {
            checkbox.addEventListener('change', () => {
                toggleSeries('pump', index, checkbox.checked);
                saveChartPreferences();
            });
        }
    });
}

function toggleSeries(chartName, seriesIndex, show) {
    const chart = chartsInstances[chartName];
    if (!chart) return;

    if (show) {
        chart.showSeries(chart.w.config.series[seriesIndex].name);
    } else {
        chart.hideSeries(chart.w.config.series[seriesIndex].name);
    }
}

function saveChartPreferences() {
    const prefs = {};
    document.querySelectorAll('.chart-checkboxes input[type="checkbox"]').forEach(cb => {
        prefs[cb.id] = cb.checked;
    });
    localStorage.setItem('chartPreferences', JSON.stringify(prefs));
}

function loadChartPreferences() {
    const saved = localStorage.getItem('chartPreferences');
    if (!saved) return;

    try {
        const prefs = JSON.parse(saved);
        Object.keys(prefs).forEach(id => {
            const checkbox = document.getElementById(id);
            if (checkbox) {
                checkbox.checked = prefs[id];
                // Применить состояние
                const match = id.match(/check-([\w-]+)-([\w-]+)/);
                if (match) {
                    const chartName = match[1];
                    const seriesName = match[2];
                    const index = getSeriesIndex(chartName, seriesName);
                    if (index !== -1) {
                        setTimeout(() => toggleSeries(chartName, index, prefs[id]), 100);
                    }
                }
            }
        });
    } catch (e) {
        console.error('Error loading preferences:', e);
    }
}

function getSeriesIndex(chartName, seriesName) {
    const mapping = {
        'temperatures': { 'cube': 0, 'column-bottom': 1, 'column-top': 2, 'reflux': 3, 'tsa': 4, 'water-in': 5, 'water-out': 6 },
        'pressure': { 'cube': 0, 'atm': 1, 'flood': 2 },
        'power': { 'voltage': 0, 'current': 1, 'power': 2, 'energy': 3, 'frequency': 4, 'pf': 5 },
        'pump': { 'speed': 0, 'volume': 1 }
    };
    return mapping[chartName]?.[seriesName] ?? -1;
}

// ============================================================================
// Period Buttons
// ============================================================================

function setupPeriodButtons() {
    document.querySelectorAll('.period-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const period = parseInt(btn.getAttribute('data-period'));
            currentPeriod = Number.isFinite(period) ? period : 60;

            // Update active button
            document.querySelectorAll('.period-btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');

            // Период влияет только на отображение, данные в буфере не удаляются.
            refreshVisibleWindow();
        });
    });
}

// ============================================================================
// Control Functions
// ============================================================================

function pauseCharts() {
    chartsPaused = true;
}

function resumeCharts() {
    chartsPaused = false;
}

function clearCharts() {
    if (confirm('Очистить все графики?')) {
        // Очистить данные
        Object.keys(chartData).forEach(key => {
            if (key === 'fractions') {
                chartData[key] = { heads: 0, body: 0, tails: 0 };
            } else {
                Object.keys(chartData[key]).forEach(subkey => {
                    chartData[key][subkey] = [];
                });
            }
        });

        // Обновить графики
        Object.keys(chartsInstances).forEach(name => {
            const chart = chartsInstances[name];
            if (chart && name !== 'fractions') {
                const seriesCount = chart.w.config.series.length;
                chart.updateSeries(Array(seriesCount).fill({ data: [] }));
            }
        });

        if (chartsInstances.fractions) {
            chartsInstances.fractions.updateSeries([{ data: [0, 0, 0] }]);
        }
    }
}

function exportCharts() {
    // Экспорт всех графиков в PNG
    Object.keys(chartsInstances).forEach(name => {
        const chart = chartsInstances[name];
        if (chart) {
            setTimeout(() => {
                chart.dataURI().then(({ imgURI }) => {
                    const link = document.createElement('a');
                    link.href = imgURI;
                    link.download = `smart-column-${name}-${Date.now()}.png`;
                    link.click();
                });
            }, 200);
        }
    });
}

// ============================================================================
// Anomaly Detection
// ============================================================================

function setupAnomalyControls() {
    // Переключатель маркеров
    const toggleBtn = document.getElementById('anomaly-toggle');
    if (toggleBtn) {
        toggleBtn.addEventListener('change', (e) => {
            anomalyDetectionEnabled = e.target.checked;
            saveAnomalySettings();
            if (!anomalyDetectionEnabled) {
                clearAllMarkers();
            }
        });
    }

    // Порог чувствительности
    const thresholdSlider = document.getElementById('anomaly-threshold');
    const thresholdValue = document.getElementById('anomaly-threshold-value');
    if (thresholdSlider) {
        thresholdSlider.addEventListener('input', (e) => {
            anomalyThreshold = parseInt(e.target.value);
            if (thresholdValue) {
                thresholdValue.textContent = anomalyThreshold + '%';
            }
            saveAnomalySettings();
        });
    }

    // Окно времени
    const windowSelect = document.getElementById('anomaly-window');
    if (windowSelect) {
        windowSelect.addEventListener('change', (e) => {
            anomalyWindow = parseInt(e.target.value);
            saveAnomalySettings();
        });
    }
}

function saveAnomalySettings() {
    const settings = {
        enabled: anomalyDetectionEnabled,
        threshold: anomalyThreshold,
        window: anomalyWindow
    };
    localStorage.setItem('anomalySettings', JSON.stringify(settings));
}

function loadAnomalySettings() {
    const saved = localStorage.getItem('anomalySettings');
    if (!saved) return;

    try {
        const settings = JSON.parse(saved);
        anomalyDetectionEnabled = settings.enabled || false;
        anomalyThreshold = settings.threshold || 15;
        anomalyWindow = settings.window || 300;

        // Обновить UI
        const toggleBtn = document.getElementById('anomaly-toggle');
        if (toggleBtn) toggleBtn.checked = anomalyDetectionEnabled;

        const thresholdSlider = document.getElementById('anomaly-threshold');
        const thresholdValue = document.getElementById('anomaly-threshold-value');
        if (thresholdSlider) {
            thresholdSlider.value = anomalyThreshold;
            if (thresholdValue) thresholdValue.textContent = anomalyThreshold + '%';
        }

        const windowSelect = document.getElementById('anomaly-window');
        if (windowSelect) windowSelect.value = anomalyWindow;
    } catch (e) {
        console.error('Error loading anomaly settings:', e);
    }
}

function detectAnomalies(chartName, seriesName, dataArray, timeArray) {
    if (!anomalyDetectionEnabled || dataArray.length < 10) return;

    const now = new Date().getTime();
    const windowStart = now - (anomalyWindow * 1000);

    // Получить данные за окно времени
    const windowData = [];
    for (let i = timeArray.length - 1; i >= 0; i--) {
        if (timeArray[i] >= windowStart) {
            windowData.push(dataArray[i]);
        } else {
            break;
        }
    }

    if (windowData.length < 3) return;

    // Рассчитать среднее
    const mean = windowData.reduce((sum, val) => sum + val, 0) / windowData.length;

    // Проверить последнее значение
    const lastValue = dataArray[dataArray.length - 1];
    const deviation = Math.abs((lastValue - mean) / mean * 100);

    if (deviation > anomalyThreshold) {
        // Обнаружено отклонение - добавить маркер
        addAnomalyMarker(chartName, seriesName, now, lastValue, mean, deviation);
    }
}

function addAnomalyMarker(chartName, seriesName, timestamp, value, mean, deviation) {
    const chart = chartsInstances[chartName];
    if (!chart) return;

    // Инициализировать массив маркеров для графика
    if (!anomalyMarkers[chartName]) {
        anomalyMarkers[chartName] = [];
    }

    // Проверить, есть ли уже маркер близко к этому времени (избежать дублирования)
    const existingMarker = anomalyMarkers[chartName].find(m =>
        Math.abs(m.timestamp - timestamp) < 10000 && m.series === seriesName
    );
    if (existingMarker) return;

    const marker = {
        series: seriesName,
        timestamp: timestamp,
        value: value,
        mean: mean,
        deviation: deviation
    };

    anomalyMarkers[chartName].push(marker);

    // Ограничить количество маркеров (последние 50)
    if (anomalyMarkers[chartName].length > 50) {
        anomalyMarkers[chartName].shift();
    }

    // Обновить аннотации на графике
    updateChartAnnotations(chartName);
}

function updateChartAnnotations(chartName) {
    const chart = chartsInstances[chartName];
    if (!chart || !anomalyMarkers[chartName]) return;

    const annotations = {
        points: anomalyMarkers[chartName].map(marker => ({
            x: marker.timestamp,
            y: marker.value,
            marker: {
                size: 6,
                fillColor: '#dc3545',
                strokeColor: '#fff',
                strokeWidth: 2
            },
            label: {
                borderColor: '#dc3545',
                offsetY: 0,
                style: {
                    color: '#fff',
                    background: '#dc3545'
                },
                text: `⚠ ${marker.series}: +${marker.deviation.toFixed(1)}%`
            }
        }))
    };

    chart.updateOptions({
        annotations: annotations
    });
}

function clearAllMarkers() {
    anomalyMarkers = {};
    Object.keys(chartsInstances).forEach(chartName => {
        const chart = chartsInstances[chartName];
        if (chart && chartName !== 'fractions') {
            chart.updateOptions({
                annotations: { points: [] }
            });
        }
    });
}

// ============================================================================
// Utilities
// ============================================================================

function formatUptime(seconds) {
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;
    return `${pad(hours)}:${pad(minutes)}:${pad(secs)}`;
}

function pad(num) {
    return num.toString().padStart(2, '0');
}
