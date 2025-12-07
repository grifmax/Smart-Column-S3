// Smart-Column S3 - Web UI JavaScript

let ws = null;
let reconnectInterval = null;
let isConnected = false;
let miniChart = null;
let miniChartData = {
    timestamps: [],
    cube: [],
    columnTop: [],
    reflux: []
};
const MINI_CHART_MAX_POINTS = 60; // 5 минут при обновлении каждые 5 секунд

// Инициализация при загрузке страницы
document.addEventListener('DOMContentLoaded', function() {
    initTabs();
    loadTheme();
    initMiniChart();
    loadMemoryStatsPreference();
    loadPumpInfo();
    connectWebSocket();
});

// ============================================================================
// WebSocket
// ============================================================================

function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.hostname}/ws`;

    addLog('Подключение к WebSocket...');

    try {
        ws = new WebSocket(wsUrl);

        ws.onopen = function() {
            isConnected = true;
            updateConnectionStatus(true);
            addLog('✅ Подключено к контроллеру', 'info');

            // Остановить попытки переподключения
            if (reconnectInterval) {
                clearInterval(reconnectInterval);
                reconnectInterval = null;
            }
        };

        ws.onmessage = function(event) {
            try {
                const data = JSON.parse(event.data);
                updateUI(data);
            } catch (e) {
                console.error('Ошибка парсинга JSON:', e);
            }
        };

        ws.onerror = function(error) {
            console.error('WebSocket error:', error);
            addLog('❌ Ошибка подключения', 'error');
        };

        ws.onclose = function() {
            isConnected = false;
            updateConnectionStatus(false);
            addLog('⚠️ Соединение разорвано. Переподключение...', 'warning');

            // Попытка переподключения каждые 5 секунд
            if (!reconnectInterval) {
                reconnectInterval = setInterval(() => {
                    if (!isConnected) {
                        connectWebSocket();
                    }
                }, 5000);
            }
        };
    } catch (e) {
        console.error('Ошибка создания WebSocket:', e);
        updateConnectionStatus(false);
    }
}

function sendCommand(action, param = '', value = 0) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        const cmd = { action, param, value };
        ws.send(JSON.stringify(cmd));
        addLog(`📤 Команда: ${action} ${param} ${value}`);
    } else {
        addLog('❌ Нет подключения к контроллеру', 'error');
    }
}

function updateConnectionStatus(connected) {
    const statusDot = document.getElementById('connection-status');
    const statusText = document.getElementById('connection-text');

    if (connected) {
        statusDot.className = 'status-dot online';
        statusText.textContent = 'Подключено';
    } else {
        statusDot.className = 'status-dot offline';
        statusText.textContent = 'Отключено';
    }
}

// ============================================================================
// Mini Chart
// ============================================================================

function initMiniChart() {
    const options = {
        chart: {
            type: 'line',
            height: 200,
            animations: {
                enabled: true,
                dynamicAnimation: {
                    speed: 500
                }
            },
            toolbar: {
                show: false
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
            position: 'top'
        },
        tooltip: {
            x: {
                format: 'HH:mm:ss'
            }
        }
    };

    miniChart = new ApexCharts(document.querySelector("#mini-chart"), options);
    miniChart.render();
}

function updateMiniChart(data) {
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

// ============================================================================
// UI Updates
// ============================================================================

function updateUI(data) {
    // Режим
    if (data.mode !== undefined) {
        const modeNames = ['IDLE', 'RECT', 'MANUAL', 'DIST', 'MASH', 'HOLD'];
        const modeName = modeNames[data.mode] || 'UNKNOWN';
        const modeEl = document.getElementById('mode');
        modeEl.textContent = modeName;
        modeEl.className = `value mode-${modeName.toLowerCase()}`;
    }

    // Фаза
    if (data.phase !== undefined) {
        const phaseNames = ['IDLE', 'HEATING', 'STABIL', 'HEADS', 'PURGE', 'BODY', 'TAILS', 'FINISH', 'ERROR'];
        document.getElementById('phase').textContent = phaseNames[data.phase] || '—';
    }

    // Температуры
    if (data.t_cube !== undefined) {
        document.getElementById('temp-cube').textContent = data.t_cube.toFixed(1) + '°C';
    }
    if (data.t_column_bottom !== undefined) {
        document.getElementById('temp-column-bottom').textContent = data.t_column_bottom.toFixed(1) + '°C';
    }
    if (data.t_column_top !== undefined) {
        document.getElementById('temp-column-top').textContent = data.t_column_top.toFixed(1) + '°C';
    }
    if (data.t_reflux !== undefined) {
        document.getElementById('temp-reflux').textContent = data.t_reflux.toFixed(1) + '°C';
    }
    if (data.t_tsa !== undefined) {
        document.getElementById('temp-tsa').textContent = data.t_tsa.toFixed(1) + '°C';
    }

    // Давление
    if (data.p_cube !== undefined) {
        document.getElementById('pressure-cube').textContent = data.p_cube.toFixed(1) + ' мм рт.ст.';
    }
    if (data.p_atm !== undefined) {
        document.getElementById('pressure-atm').textContent = data.p_atm.toFixed(1) + ' гПа';
    }
    if (data.p_flood !== undefined) {
        document.getElementById('pressure-flood').textContent = data.p_flood.toFixed(1) + ' мм';
    }

    // Мощность (PZEM-004T)
    if (data.voltage !== undefined) {
        document.getElementById('power-voltage').textContent = data.voltage.toFixed(1) + ' V';
    }
    if (data.current !== undefined) {
        document.getElementById('power-current').textContent = data.current.toFixed(2) + ' A';
    }
    if (data.power !== undefined) {
        document.getElementById('power-power').textContent = data.power.toFixed(0) + ' W';
    }
    if (data.energy !== undefined) {
        document.getElementById('power-energy').textContent = data.energy.toFixed(3) + ' кВт·ч';
    }
    if (data.frequency !== undefined) {
        document.getElementById('power-frequency').textContent = data.frequency.toFixed(1) + ' Гц';
    }
    if (data.pf !== undefined) {
        document.getElementById('power-pf').textContent = data.pf.toFixed(2);
    }

    // Насос
    if (data.pump_speed !== undefined) {
        document.getElementById('pump-speed').textContent = data.pump_speed.toFixed(0) + ' мл/ч';
    }
    if (data.pump_volume !== undefined) {
        document.getElementById('pump-volume').textContent = data.pump_volume.toFixed(0) + ' мл';
    }

    // Объёмы фракций
    if (data.volume_heads !== undefined) {
        document.getElementById('volume-heads').textContent = data.volume_heads.toFixed(0) + ' мл';
    }
    if (data.volume_body !== undefined) {
        document.getElementById('volume-body').textContent = data.volume_body.toFixed(0) + ' мл';
    }
    if (data.volume_tails !== undefined) {
        document.getElementById('volume-tails').textContent = data.volume_tails.toFixed(0) + ' мл';
    }

    // Ареометр
    if (data.abv !== undefined) {
        document.getElementById('abv').textContent = data.abv.toFixed(1) + '%';
    }

    // Uptime
    if (data.uptime !== undefined) {
        document.getElementById('uptime').textContent = formatUptime(data.uptime);
    }

    // События
    if (data.type === 'event') {
        addLog(data.message, data.level || 'info');
    }

    // Обновить мини-график
    updateMiniChart(data);

    // Обновить статистику памяти
    if (data.memory !== undefined) {
        updateMemoryStats(data.memory);
    }

    // Обновить анимацию колонны
    if (typeof updateColumnAnimation === 'function') {
        updateColumnAnimation(data);
    }
}

function formatUptime(seconds) {
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;
    return `${pad(hours)}:${pad(minutes)}:${pad(secs)}`;
}

function pad(num) {
    return num.toString().padStart(2, '0');
}

// ============================================================================
// Tabs
// ============================================================================

function initTabs() {
    const tabs = document.querySelectorAll('.tab');
    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            const targetId = tab.getAttribute('data-tab');

            // Убрать активный класс со всех вкладок
            tabs.forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(content => {
                content.classList.remove('active');
            });

            // Добавить активный класс к выбранной вкладке
            tab.classList.add('active');
            document.getElementById(targetId).classList.add('active');

            // Загрузить историю при переключении на вкладку "История"
            if (targetId === 'history') {
                loadHistoryList();
            }
        });
    });
}

// ============================================================================
// Control Functions
// ============================================================================

function startRectification() {
    sendCommand('start', 'rectification');
    addLog('▶️ Запуск авто-ректификации', 'info');
}

function startManual() {
    sendCommand('start', 'manual');
    addLog('▶️ Запуск ручного режима', 'info');
}

function startDistillation() {
    sendCommand('start', 'distillation');
    addLog('▶️ Запуск дистилляции', 'info');
}

function stopProcess() {
    if (confirm('Остановить процесс?')) {
        sendCommand('stop');
        addLog('⏹️ Остановка процесса', 'warning');
    }
}

function pauseProcess() {
    sendCommand('pause');
    addLog('⏸️ Пауза', 'info');
}

function resumeProcess() {
    sendCommand('resume');
    addLog('⏯️ Продолжение', 'info');
}

function updateHeater(value) {
    document.getElementById('heater-value').textContent = value;
    sendCommand('heater', 'power', parseInt(value));
}

function updatePump(value) {
    document.getElementById('pump-value').textContent = value;
    sendCommand('pump', 'speed', parseInt(value));
}

function toggleValve(name) {
    sendCommand('valve', name, 1);
    addLog(`🔄 Переключение клапана: ${name}`);
}

// ============================================================================
// Settings
// ============================================================================

function saveWiFi() {
    const ssid = document.getElementById('wifi-ssid').value;
    const password = document.getElementById('wifi-password').value;

    if (ssid) {
        sendCommand('wifi', 'save', 0);
        addLog('💾 WiFi настройки сохранены', 'info');
        alert('WiFi настройки сохранены. Перезагрузите контроллер.');
    }
}

function saveEquipment() {
    const heaterPower = document.getElementById('heater-power-w').value;
    const columnHeight = document.getElementById('column-height').value;

    sendCommand('equipment', 'save', 0);
    addLog('💾 Настройки оборудования сохранены', 'info');
}

function toggleMqttFields() {
    const enabled = document.getElementById('mqtt-enabled').checked;
    const fields = document.getElementById('mqtt-fields');
    fields.style.display = enabled ? 'block' : 'none';
}

function saveMqtt() {
    const enabled = document.getElementById('mqtt-enabled').checked;
    const server = document.getElementById('mqtt-server').value;
    const port = document.getElementById('mqtt-port').value;
    const username = document.getElementById('mqtt-username').value;
    const password = document.getElementById('mqtt-password').value;
    const baseTopic = document.getElementById('mqtt-base-topic').value;
    const discovery = document.getElementById('mqtt-discovery').checked;
    const publishInterval = document.getElementById('mqtt-publish-interval').value;

    if (enabled && !server) {
        alert('Укажите адрес MQTT сервера');
        return;
    }

    sendCommand('mqtt', 'save', 0);
    addLog('💾 MQTT настройки сохранены', 'info');
    alert('MQTT настройки сохранены. Перезагрузите контроллер.');
}

function toggleAuthFields() {
    const enabled = document.getElementById('auth-enabled').checked;
    const fields = document.getElementById('auth-fields');
    fields.style.display = enabled ? 'block' : 'none';
}

function saveSecurity() {
    const authEnabled = document.getElementById('auth-enabled').checked;
    const username = document.getElementById('web-username').value;
    const password = document.getElementById('web-password').value;
    const rateLimitEnabled = document.getElementById('rate-limit-enabled').checked;

    if (authEnabled && (!username || !password)) {
        alert('Укажите имя пользователя и пароль');
        return;
    }

    sendCommand('security', 'save', 0);
    addLog('💾 Настройки безопасности сохранены', 'info');
    alert('Настройки безопасности сохранены. Перезагрузите контроллер.');
}

function setTheme(theme) {
    document.body.setAttribute('data-theme', theme);
    localStorage.setItem('theme', theme);

    // Обновить тему мини-графика
    if (miniChart) {
        miniChart.updateOptions({
            theme: {
                mode: theme
            }
        });
    }

    addLog(`🎨 Тема изменена: ${theme}`, 'info');
}

function loadTheme() {
    const savedTheme = localStorage.getItem('theme') || 'light';
    document.body.setAttribute('data-theme', savedTheme);
}

// ============================================================================
// Logs
// ============================================================================

function addLog(message, type = 'info') {
    const logContainer = document.getElementById('log-container');
    const timestamp = new Date().toLocaleTimeString();
    const entry = document.createElement('div');
    entry.className = `log-entry ${type}`;
    entry.textContent = `[${timestamp}] ${message}`;

    logContainer.appendChild(entry);

    // Автоскролл вниз
    logContainer.scrollTop = logContainer.scrollHeight;

    // Ограничить количество записей (последние 100)
    const entries = logContainer.querySelectorAll('.log-entry');
    if (entries.length > 100) {
        entries[0].remove();
    }
}

function clearLogs() {
    if (confirm('Очистить логи?')) {
        document.getElementById('log-container').innerHTML = '';
        addLog('Логи очищены', 'info');
    }
}

function downloadLogs() {
    addLog('📥 Запрос экспорта логов...', 'info');
    window.open('/api/export', '_blank');
}

// ============================================================================
// Memory Statistics
// ============================================================================

function updateMemoryStats(mem) {
    const memStatsDiv = document.getElementById('memory-stats');
    if (memStatsDiv.style.display === 'none') return;

    // Форматирование байтов в KB/MB
    const formatBytes = (bytes) => {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
    };

    // SRAM (Heap)
    const heapUsed = mem.heap_total - mem.heap_free;
    document.getElementById('mem-heap-used').textContent = formatBytes(heapUsed);
    document.getElementById('mem-heap-total').textContent = formatBytes(mem.heap_total);
    document.getElementById('mem-heap-pct').textContent = mem.heap_used_pct.toFixed(1) + '%';

    // PSRAM
    document.getElementById('mem-psram-free').textContent = formatBytes(mem.psram_free);
    document.getElementById('mem-psram-total').textContent = formatBytes(mem.psram_total);

    // Flash
    document.getElementById('mem-flash-pct').textContent = mem.flash_used_pct.toFixed(1) + '%';
}

function toggleMemoryStats() {
    const checkbox = document.getElementById('show-memory-stats');
    const memStatsDiv = document.getElementById('memory-stats');

    if (checkbox.checked) {
        memStatsDiv.style.display = 'block';
        localStorage.setItem('showMemoryStats', 'true');
    } else {
        memStatsDiv.style.display = 'none';
        localStorage.setItem('showMemoryStats', 'false');
    }
}

function loadMemoryStatsPreference() {
    const showMemoryStats = localStorage.getItem('showMemoryStats') === 'true';
    const checkbox = document.getElementById('show-memory-stats');
    const memStatsDiv = document.getElementById('memory-stats');

    if (checkbox) {
        checkbox.checked = showMemoryStats;
    }

    if (memStatsDiv) {
        memStatsDiv.style.display = showMemoryStats ? 'block' : 'none';
    }
}

// ============================================================================
// Загрузка информации о насосе
// ============================================================================

async function loadPumpInfo() {
    try {
        const response = await fetch('/api/calibration');
        if (!response.ok) {
            throw new Error('Failed to load calibration data');
        }

        const data = await response.json();

        // Обновить информацию о насосе
        const mlPerRevEl = document.getElementById('pump-ml-per-rev');
        const stepsPerRevEl = document.getElementById('pump-steps-per-rev');

        if (mlPerRevEl && data.pump) {
            mlPerRevEl.textContent = `${data.pump.mlPerRev.toFixed(3)} мл/оборот`;
        }

        if (stepsPerRevEl && data.pump) {
            const totalSteps = data.pump.stepsPerRev * data.pump.microsteps;
            stepsPerRevEl.textContent = `${totalSteps} шагов (${data.pump.stepsPerRev} × ${data.pump.microsteps} микрошагов)`;
        }
    } catch (error) {
        console.error('Error loading pump info:', error);
        const mlPerRevEl = document.getElementById('pump-ml-per-rev');
        const stepsPerRevEl = document.getElementById('pump-steps-per-rev');

        if (mlPerRevEl) mlPerRevEl.textContent = 'Ошибка загрузки';
        if (stepsPerRevEl) stepsPerRevEl.textContent = 'Ошибка загрузки';
    }
}

// ============================================================================
// История процессов
// ============================================================================

let historyData = [];

async function loadHistoryList() {
    try {
        const response = await fetch('/api/history');
        if (!response.ok) {
            throw new Error('Failed to load history');
        }

        const data = await response.json();
        historyData = data.processes || [];

        // Применить фильтры
        applyHistoryFilters();

        addLog(`📚 Загружено процессов: ${historyData.length}`, 'info');
    } catch (error) {
        console.error('Error loading history:', error);
        addLog('❌ Ошибка загрузки истории', 'error');

        // Показать пустой список
        const historyListEl = document.getElementById('history-list');
        if (historyListEl) {
            historyListEl.innerHTML = '<div style="text-align: center; padding: 20px; color: var(--text-secondary);">Нет данных или ошибка загрузки</div>';
        }
    }
}

function applyHistoryFilters() {
    const typeFilter = document.getElementById('history-filter-type')?.value || 'all';
    const sortBy = document.getElementById('history-sort')?.value || 'date-desc';

    let filtered = [...historyData];

    // Фильтр по типу
    if (typeFilter !== 'all') {
        filtered = filtered.filter(p => p.type === typeFilter);
    }

    // Сортировка
    filtered.sort((a, b) => {
        switch (sortBy) {
            case 'date-desc':
                return b.startTime - a.startTime;
            case 'date-asc':
                return a.startTime - b.startTime;
            case 'duration-desc':
                return b.duration - a.duration;
            case 'duration-asc':
                return a.duration - b.duration;
            default:
                return 0;
        }
    });

    // Отрисовать список
    renderHistoryList(filtered);

    // Обновить статистику
    updateHistoryStats(filtered);
}

function renderHistoryList(processes) {
    const historyListEl = document.getElementById('history-list');
    if (!historyListEl) return;

    if (processes.length === 0) {
        historyListEl.innerHTML = '<div style="text-align: center; padding: 20px; color: var(--text-secondary);">Нет процессов для отображения</div>';
        return;
    }

    historyListEl.innerHTML = '';

    processes.forEach(process => {
        const itemEl = renderHistoryItem(process);
        historyListEl.appendChild(itemEl);
    });
}

function renderHistoryItem(process) {
    const div = document.createElement('div');
    div.className = 'history-item';

    const typeNames = {
        rectification: 'Ректификация',
        distillation: 'Дистилляция',
        mashing: 'Затирка',
        hold: 'Выдержка'
    };

    const statusNames = {
        completed: 'Завершен',
        stopped: 'Остановлен',
        error: 'Ошибка'
    };

    const typeName = typeNames[process.type] || process.type;
    const statusName = statusNames[process.status] || process.status;

    const startDate = new Date(process.startTime * 1000);
    const durationHours = (process.duration / 3600).toFixed(1);

    div.innerHTML = `
        <div class="history-header">
            <div>
                <span class="history-type history-type-${process.type}">${typeName}</span>
                <span class="history-status history-status-${process.status}">${statusName}</span>
            </div>
            <div class="history-date">${startDate.toLocaleString('ru-RU')}</div>
        </div>
        <div class="history-info">
            <div class="history-metric">
                <span class="metric-label">⏱️ Длительность:</span>
                <span class="metric-value">${durationHours} ч</span>
            </div>
            <div class="history-metric">
                <span class="metric-label">💧 Объём:</span>
                <span class="metric-value">${process.totalVolume || 0} мл</span>
            </div>
        </div>
        <div class="history-actions">
            <button class="btn-secondary" onclick="viewHistoryDetails('${process.id}')">👁️ Подробно</button>
            <button class="btn-secondary" onclick="exportHistory('${process.id}')">📥 Экспорт</button>
            <button class="btn-danger" onclick="deleteHistoryItem('${process.id}')">🗑️ Удалить</button>
        </div>
    `;

    return div;
}

function updateHistoryStats(processes) {
    const totalEl = document.getElementById('hist-stat-total');
    const completedEl = document.getElementById('hist-stat-completed');
    const timeEl = document.getElementById('hist-stat-time');
    const energyEl = document.getElementById('hist-stat-energy');

    if (!totalEl) return;

    const total = processes.length;
    const completed = processes.filter(p => p.status === 'completed').length;
    const totalTime = processes.reduce((sum, p) => sum + (p.duration || 0), 0);
    const totalEnergy = 0; // Будет реализовано позже, когда появится поле energy в процессах

    totalEl.textContent = total;
    completedEl.textContent = completed;
    timeEl.textContent = (totalTime / 3600).toFixed(1) + ' ч';
    energyEl.textContent = totalEnergy.toFixed(1) + ' кВт·ч';
}

async function clearHistory() {
    if (!confirm('Удалить ВСЮ историю процессов? Это действие необратимо!')) {
        return;
    }

    try {
        const response = await fetch('/api/history', {
            method: 'DELETE',
            headers: {
                'Content-Type': 'application/json'
            }
        });

        if (!response.ok) {
            throw new Error('Failed to clear history');
        }

        addLog('🗑️ История полностью очищена', 'info');
        await loadHistoryList();
    } catch (error) {
        console.error('Error clearing history:', error);
        addLog('❌ Ошибка при очистке истории', 'error');
        alert('Ошибка при очистке истории');
    }
}

async function deleteHistoryItem(id) {
    if (!confirm('Удалить этот процесс из истории?')) {
        return;
    }

    try {
        const response = await fetch(`/api/history/${id}`, {
            method: 'DELETE',
            headers: {
                'Content-Type': 'application/json'
            }
        });

        if (!response.ok) {
            throw new Error('Failed to delete history item');
        }

        addLog(`🗑️ Процесс ${id} удален из истории`, 'info');
        await loadHistoryList();
    } catch (error) {
        console.error('Error deleting history item:', error);
        addLog('❌ Ошибка при удалении процесса', 'error');
        alert('Ошибка при удалении процесса');
    }
}

async function viewHistoryDetails(id) {
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

function showHistoryDetailsModal(process) {
    // TODO: Реализовать модальное окно с графиками и деталями
    // Пока используем alert с основной информацией
    const startDate = new Date(process.metadata.startTime * 1000);
    const endDate = new Date(process.metadata.endTime * 1000);

    const details = `
Процесс: ${process.process.type}
Начало: ${startDate.toLocaleString('ru-RU')}
Окончание: ${endDate.toLocaleString('ru-RU')}
Длительность: ${(process.metadata.duration / 3600).toFixed(1)} ч

Собрано:
- Головы: ${process.results.headsCollected} мл
- Тело: ${process.results.bodyCollected} мл
- Хвосты: ${process.results.tailsCollected} мл
- Всего: ${process.results.totalCollected} мл

Статус: ${process.results.status}
    `.trim();

    alert(details);
}

async function exportHistory(id) {
    try {
        addLog(`📥 Экспорт процесса ${id}...`, 'info');

        // Запрос на экспорт в CSV
        window.open(`/api/history/${id}/export?format=csv`, '_blank');

        addLog(`✅ Экспорт процесса ${id} начат`, 'info');
    } catch (error) {
        console.error('Error exporting history:', error);
        addLog('❌ Ошибка экспорта', 'error');
    }
}
