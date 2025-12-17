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

// Состояние процесса
let currentMode = 0;  // 0 = IDLE
let currentPaused = false;
let maxHeaterPower = 3000;  // Будет обновлено из настроек

// Инициализация при загрузке страницы
document.addEventListener('DOMContentLoaded', function () {
    initTabs();
    loadTheme();
    loadDemoMode();  // Загрузить состояние демо-режима
    initMiniChart();
    loadMemoryStatsPreference();
    loadPumpInfo();
    loadVersionInfo();
    loadStatus();  // Загрузить начальный статус
    connectWebSocket();

    // Периодический опрос статуса (резервный вариант если WebSocket отключён)
    setInterval(loadStatus, 2000);
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

        ws.onopen = function () {
            isConnected = true;
            updateConnectionStatus(true);
            addLog('✅ Подключено к контроллеру', 'info');

            // Остановить попытки переподключения
            if (reconnectInterval) {
                clearInterval(reconnectInterval);
                reconnectInterval = null;
            }
        };

        ws.onmessage = function (event) {
            try {
                const data = JSON.parse(event.data);
                updateUI(data);
            } catch (e) {
                console.error('Ошибка парсинга JSON:', e);
            }
        };

        ws.onerror = function (error) {
            console.error('WebSocket error:', error);
            addLog('❌ Ошибка подключения', 'error');
        };

        ws.onclose = function () {
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

async function startRectification() {
    try {
        addLog('📤 Отправка команды запуска авто-ректификации...', 'info');

        const response = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mode: 'rectification' })
        });

        if (response.ok) {
            const data = await response.json();
            addLog('✅ Авто-ректификация запущена', 'success');
            if (data.warning) {
                addLog('⚠️ ' + data.warning, 'warning');
            }
            setTimeout(loadStatus, 500); // Обновить статус
        } else {
            const error = await response.text();
            addLog('❌ Ошибка (' + response.status + '): ' + error, 'error');
        }
    } catch (e) {
        addLog('❌ Ошибка сети: ' + e.message, 'error');
        console.error('Start rectification error:', e);
    }
}

function startManual() {
    // Переход на страницу ручного управления
    window.location.href = 'manual.html';
}

async function startDistillation() {
    try {
        addLog('📤 Отправка команды запуска дистилляции...', 'info');

        const response = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mode: 'distillation' })
        });

        if (response.ok) {
            const data = await response.json();
            addLog('✅ Дистилляция запущена', 'success');
            if (data.warning) {
                addLog('⚠️ ' + data.warning, 'warning');
            }
            setTimeout(loadStatus, 500); // Обновить статус
        } else {
            const error = await response.text();
            addLog('❌ Ошибка (' + response.status + '): ' + error, 'error');
        }
    } catch (e) {
        addLog('❌ Ошибка сети: ' + e.message, 'error');
        console.error('Start distillation error:', e);
    }
}

async function stopProcess() {
    if (!confirm('Остановить процесс?')) return;

    try {
        const response = await fetch('/api/process/stop', {
            method: 'POST'
        });

        if (response.ok) {
            addLog('✅ Процесс остановлен', 'warning');
            setTimeout(loadStatus, 500); // Обновить статус
        } else {
            addLog('❌ Ошибка остановки', 'error');
        }
    } catch (e) {
        addLog('❌ Ошибка: ' + e.message, 'error');
    }
}

async function pauseProcess() {
    try {
        const response = await fetch('/api/process/pause', {
            method: 'POST'
        });

        if (response.ok) {
            addLog('✅ Процесс приостановлен', 'info');
            setTimeout(loadStatus, 500); // Обновить статус
        } else {
            addLog('❌ Ошибка паузы', 'error');
        }
    } catch (e) {
        addLog('❌ Ошибка: ' + e.message, 'error');
    }
}

async function resumeProcess() {
    try {
        const response = await fetch('/api/process/resume', {
            method: 'POST'
        });

        if (response.ok) {
            addLog('✅ Процесс возобновлен', 'info');
            setTimeout(loadStatus, 500); // Обновить статус
        } else {
            addLog('❌ Ошибка возобновления', 'error');
        }
    } catch (e) {
        addLog('❌ Ошибка: ' + e.message, 'error');
    }
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
// Загрузка статуса и обновление кнопок
// ============================================================================

async function loadStatus() {
    try {
        const response = await fetch('/api/status');
        if (!response.ok) return;

        const data = await response.json();

        // Обновить состояние процесса
        currentMode = data.mode || 0;
        currentPaused = data.paused || false;

        // Сохранить мощность ТЭНа из настроек
        if (data.equipment && data.equipment.heaterPowerW) {
            maxHeaterPower = data.equipment.heaterPowerW;
            updateHeaterSlider();
        }

        // Обновить UI с новым форматом данных
        updateUIFromStatus(data);

        // Обновить состояние кнопок
        updateButtonStates();

    } catch (e) {
        console.error('Ошибка загрузки статуса:', e);
    }
}

function updateUIFromStatus(data) {
    // Режим
    if (data.modeStr !== undefined) {
        const modeEl = document.getElementById('mode');
        if (modeEl) {
            modeEl.textContent = data.modeStr.toUpperCase();
            modeEl.className = `value mode-${data.modeStr}`;
        }
    }

    // Фаза
    if (data.phaseStr !== undefined) {
        const phaseEl = document.getElementById('phase');
        if (phaseEl) {
            phaseEl.textContent = data.phaseStr.toUpperCase() || '—';
        }
    }

    // Температуры
    if (data.temps) {
        if (data.temps.cube !== undefined) {
            const el = document.getElementById('temp-cube');
            if (el) el.textContent = data.temps.cube.toFixed(1) + '°C';
        }
        if (data.temps.columnBottom !== undefined) {
            const el = document.getElementById('temp-column-bottom');
            if (el) el.textContent = data.temps.columnBottom.toFixed(1) + '°C';
        }
        if (data.temps.columnTop !== undefined) {
            const el = document.getElementById('temp-column-top');
            if (el) el.textContent = data.temps.columnTop.toFixed(1) + '°C';
        }
        if (data.temps.reflux !== undefined) {
            const el = document.getElementById('temp-reflux');
            if (el) el.textContent = data.temps.reflux.toFixed(1) + '°C';
        }
        if (data.temps.tsa !== undefined) {
            const el = document.getElementById('temp-tsa');
            if (el) el.textContent = data.temps.tsa.toFixed(1) + '°C';
        }
    }

    // Давление
    if (data.pressure) {
        if (data.pressure.cube !== undefined) {
            const el = document.getElementById('pressure-cube');
            if (el) el.textContent = data.pressure.cube.toFixed(1) + ' мм рт.ст.';
        }
        if (data.pressure.atm !== undefined) {
            const el = document.getElementById('pressure-atm');
            if (el) el.textContent = data.pressure.atm.toFixed(1) + ' гПа';
        }
    }

    // Мощность
    if (data.power) {
        if (data.power.voltage !== undefined) {
            const el = document.getElementById('power-voltage');
            if (el) el.textContent = data.power.voltage.toFixed(1) + ' V';
        }
        if (data.power.current !== undefined) {
            const el = document.getElementById('power-current');
            if (el) el.textContent = data.power.current.toFixed(2) + ' A';
        }
        if (data.power.power !== undefined) {
            const el = document.getElementById('power-power');
            if (el) el.textContent = data.power.power.toFixed(0) + ' W';
        }
        if (data.power.energy !== undefined) {
            const el = document.getElementById('power-energy');
            if (el) el.textContent = data.power.energy.toFixed(3) + ' кВт·ч';
        }
        if (data.power.frequency !== undefined) {
            const el = document.getElementById('power-frequency');
            if (el) el.textContent = data.power.frequency.toFixed(1) + ' Гц';
        }
        if (data.power.pf !== undefined) {
            const el = document.getElementById('power-pf');
            if (el) el.textContent = data.power.pf.toFixed(2);
        }
    }

    // Насос
    if (data.pump) {
        if (data.pump.speedMlH !== undefined) {
            const el = document.getElementById('pump-speed');
            if (el) el.textContent = data.pump.speedMlH.toFixed(0) + ' мл/ч';
        }
        if (data.pump.totalMl !== undefined) {
            const el = document.getElementById('pump-volume');
            if (el) el.textContent = data.pump.totalMl.toFixed(0) + ' мл';
        }
    }

    // Объёмы фракций
    if (data.volumes) {
        if (data.volumes.heads !== undefined) {
            const el = document.getElementById('volume-heads');
            if (el) el.textContent = data.volumes.heads.toFixed(0) + ' мл';
        }
        if (data.volumes.body !== undefined) {
            const el = document.getElementById('volume-body');
            if (el) el.textContent = data.volumes.body.toFixed(0) + ' мл';
        }
        if (data.volumes.tails !== undefined) {
            const el = document.getElementById('volume-tails');
            if (el) el.textContent = data.volumes.tails.toFixed(0) + ' мл';
        }
    }

    // Ареометр
    if (data.hydrometer && data.hydrometer.abv !== undefined) {
        const el = document.getElementById('abv');
        if (el) el.textContent = data.hydrometer.abv.toFixed(1) + '%';
    }

    // Uptime
    if (data.uptime !== undefined) {
        const el = document.getElementById('uptime');
        if (el) el.textContent = formatUptime(data.uptime);
    }
}

function updateButtonStates() {
    const isIdle = currentMode === 0;

    // Кнопки запуска режимов
    const btnRect = document.querySelector('button[onclick="startRectification()"]');
    const btnManual = document.querySelector('button[onclick="startManual()"]');
    const btnDist = document.querySelector('button[onclick="startDistillation()"]');

    // Кнопки управления
    const btnStop = document.querySelector('button[onclick="stopProcess()"]');
    const btnPause = document.querySelector('button[onclick="pauseProcess()"]');
    const btnResume = document.querySelector('button[onclick="resumeProcess()"]');

    // Настройка состояний
    if (btnRect) {
        btnRect.disabled = !isIdle;
        btnRect.classList.toggle('btn-disabled', !isIdle);
    }
    if (btnManual) {
        btnManual.disabled = !isIdle;
        btnManual.classList.toggle('btn-disabled', !isIdle);
    }
    if (btnDist) {
        btnDist.disabled = !isIdle;
        btnDist.classList.toggle('btn-disabled', !isIdle);
    }

    if (btnStop) {
        btnStop.disabled = isIdle;
        btnStop.classList.toggle('btn-disabled', isIdle);
    }
    if (btnPause) {
        btnPause.disabled = isIdle || currentPaused;
        btnPause.classList.toggle('btn-disabled', isIdle || currentPaused);
    }
    if (btnResume) {
        btnResume.disabled = isIdle || !currentPaused;
        btnResume.classList.toggle('btn-disabled', isIdle || !currentPaused);
    }
}

function updateHeaterSlider() {
    const slider = document.getElementById('heater-power');
    const label = document.querySelector('label[for="heater-power"]');

    if (slider) {
        slider.max = maxHeaterPower;
        slider.step = 50;  // Шаг 50 Вт
    }

    if (label) {
        label.innerHTML = `Мощность нагрева: <span id="heater-value">0</span> Вт (макс ${maxHeaterPower})`;
    }
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

async function saveEquipment() {
    const heaterPower = document.getElementById('heater-power-w').value;
    const columnHeight = document.getElementById('column-height').value;
    const mlPerRev = parseFloat(document.getElementById('pump-ml-per-rev').value);
    const stepsPerRev = parseInt(document.getElementById('pump-steps-per-rev').value);

    // Проверка и сохранение параметров насоса
    const pumpData = {};
    let hasPumpData = false;

    if (mlPerRev && mlPerRev > 0) {
        pumpData.mlPerRev = mlPerRev;
        hasPumpData = true;
    }

    if (stepsPerRev && stepsPerRev > 0) {
        pumpData.stepsPerRev = stepsPerRev;
        hasPumpData = true;
    }

    if (hasPumpData) {
        try {
            const response = await fetch('/api/calibration/pump', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(pumpData)
            });

            if (response.ok) {
                let msg = '✓ Параметры насоса сохранены:';
                if (pumpData.mlPerRev) msg += ' ' + pumpData.mlPerRev.toFixed(3) + ' мл/об';
                if (pumpData.stepsPerRev) msg += ', ' + pumpData.stepsPerRev + ' шагов/об';
                addLog(msg, 'success');
            } else {
                addLog('✗ Ошибка сохранения параметров насоса', 'error');
            }
        } catch (error) {
            addLog('✗ Ошибка соединения при сохранении насоса', 'error');
        }
    }

    // Сохранение других параметров оборудования (через WebSocket)
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

function toggleDemoMode() {
    const enabled = document.getElementById('demo-mode-enabled').checked;

    // Сохранить в localStorage
    localStorage.setItem('demoMode', enabled ? 'true' : 'false');

    // Отправить на сервер
    fetch('/api/settings/demo', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ enabled: enabled })
    }).then(response => {
        if (response.ok) {
            addLog(enabled ? '🧪 Демо-режим ВКЛЮЧЁН' : '✅ Демо-режим отключён', 'info');
        } else {
            addLog('⚠️ Ошибка сохранения демо-режима на сервер', 'warning');
        }
    }).catch(err => {
        addLog('⚠️ Демо-режим сохранён локально (сервер недоступен)', 'warning');
    });
}

function loadDemoMode() {
    const saved = localStorage.getItem('demoMode');
    const checkbox = document.getElementById('demo-mode-enabled');
    if (checkbox && saved === 'true') {
        checkbox.checked = true;
    }
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
            // Теперь это input поле, устанавливаем value
            mlPerRevEl.value = data.pump.mlPerRev.toFixed(3);
        }

        if (stepsPerRevEl && data.pump) {
            // Показываем общее количество шагов
            const totalSteps = data.pump.stepsPerRev * data.pump.microsteps;
            stepsPerRevEl.value = totalSteps;
        }
    } catch (error) {
        console.error('Error loading pump info:', error);
        const mlPerRevEl = document.getElementById('pump-ml-per-rev');
        const stepsPerRevEl = document.getElementById('pump-steps-per-rev');

        if (mlPerRevEl) mlPerRevEl.placeholder = 'Ошибка загрузки';
        if (stepsPerRevEl) stepsPerRevEl.placeholder = 'Ошибка загрузки';
    }
}

// Загрузка информации о версиях
async function loadVersionInfo() {
    try {
        const response = await fetch('/api/version');
        if (!response.ok) {
            throw new Error('Failed to load version info');
        }

        const data = await response.json();

        // Обновить информацию о прошивке
        if (data.firmware) {
            document.getElementById('firmware-version').textContent = data.firmware.version || 'Unknown';
            document.getElementById('firmware-build-date').textContent = data.firmware.buildDate || 'Unknown';
            document.getElementById('firmware-build-time').textContent = data.firmware.buildTime || 'Unknown';
        }

        if (data.board) {
            const flashMB = (data.board.flashSize / (1024 * 1024)).toFixed(0);
            const psramMB = (data.board.psramSize / (1024 * 1024)).toFixed(0);
            document.getElementById('board-chip').textContent =
                `${data.board.chip} (Flash: ${flashMB}MB, PSRAM: ${psramMB}MB)`;
        }

        // Обновить информацию о фронтенде
        if (data.frontend) {
            document.getElementById('frontend-build-date').textContent =
                data.frontend.buildDate || data.frontend.note || 'Unknown';
            document.getElementById('frontend-build-time').textContent =
                data.frontend.buildTime || '-';
        }

        addLog('✓ Информация о версиях обновлена', 'success');
    } catch (error) {
        console.error('Error loading version info:', error);
        document.getElementById('firmware-version').textContent = 'Ошибка загрузки';
        document.getElementById('frontend-build-date').textContent = 'Ошибка загрузки';
        addLog('✗ Ошибка загрузки версий', 'error');
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

let selectedProcesses = new Set();

function renderHistoryItem(process) {
    const div = document.createElement('div');
    div.className = 'history-item';
    div.dataset.processId = process.id;

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
    const isSelected = selectedProcesses.has(process.id);

    div.innerHTML = `
        <div class="history-header">
            <div style="display: flex; align-items: center; gap: 10px;">
                <input type="checkbox"
                       class="history-checkbox"
                       data-process-id="${process.id}"
                       ${isSelected ? 'checked' : ''}
                       onchange="toggleProcessSelection('${process.id}')">
                <div>
                    <span class="history-type history-type-${process.type}">${typeName}</span>
                    <span class="history-status history-status-${process.status}">${statusName}</span>
                </div>
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

function toggleProcessSelection(processId) {
    if (selectedProcesses.has(processId)) {
        selectedProcesses.delete(processId);
    } else {
        selectedProcesses.add(processId);
    }
    updateCompareButton();
}

function updateCompareButton() {
    const compareBtn = document.getElementById('compare-processes-btn');
    if (compareBtn) {
        compareBtn.disabled = selectedProcesses.size < 2;
        compareBtn.textContent = `📊 Сравнить выбранные (${selectedProcesses.size})`;
    }
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

let tempChart = null;
let powerChart = null;

function showHistoryDetailsModal(process) {
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

function closeHistoryModal() {
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

function renderTempChart(process) {
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

function renderPowerChart(process) {
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

function renderPhases(process) {
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

async function exportHistory(id, format = null) {
    try {
        // Если формат не указан, спросить у пользователя
        if (!format) {
            const choice = confirm('Выберите формат экспорта:\n\nОК - CSV (таблица)\nОтмена - JSON (данные)');
            format = choice ? 'csv' : 'json';
        }

        addLog(`📥 Экспорт процесса ${id} в формате ${format.toUpperCase()}...`, 'info');

        // Открыть экспорт в новой вкладке
        window.open(`/api/history/${id}/export?format=${format}`, '_blank');

        addLog(`✅ Экспорт процесса ${id} начат`, 'info');
    } catch (error) {
        console.error('Error exporting history:', error);
        addLog('❌ Ошибка экспорта', 'error');
    }
}

async function exportHistoryCSV(id) {
    await exportHistory(id, 'csv');
}

async function exportHistoryJSON(id) {
    await exportHistory(id, 'json');
}

// ============================================================================
// Сравнение процессов
// ============================================================================

let compareTempChart = null;
let comparePowerChart = null;

async function compareSelected() {
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
        addLog(`✅ Сравнение ${processes.length} процессов`, 'info');
    } catch (error) {
        console.error('Error comparing processes:', error);
        addLog('❌ Ошибка при сравнении процессов', 'error');
        alert('Ошибка при сравнении процессов');
    }
}

function showCompareModal(processes) {
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

function closeCompareModal() {
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

function renderCompareTempChart(processes, colors) {
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
                show: true
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

function renderComparePowerChart(processes, colors) {
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
                show: true
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

function renderCompareTable(processes) {
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

// ============================================================================
// PROFILES - Управление профилями процессов
// ============================================================================

let currentProfileId = null; // ID профиля для просмотра/редактирования

// Загрузка списка профилей
function loadProfilesList() {
    const listEl = document.getElementById('profiles-list');
    if (!listEl) return;

    listEl.innerHTML = '<p class="info-text">Загрузка профилей...</p>';

    fetch('/api/profiles')
        .then(response => response.json())
        .then(data => {
            if (data.profiles && data.profiles.length > 0) {
                renderProfilesList(data.profiles);
                updateProfilesStats(data.profiles);
            } else {
                listEl.innerHTML = '<p class="info-text">📁 Профили не найдены. Создайте первый профиль!</p>';
            }
        })
        .catch(error => {
            console.error('Ошибка загрузки профилей:', error);
            listEl.innerHTML = '<p class="error-text">❌ Ошибка загрузки профилей</p>';
        });
}

// Отрисовка списка профилей
function renderProfilesList(profiles) {
    const listEl = document.getElementById('profiles-list');
    const filter = document.getElementById('profile-filter-category').value;

    // Применить фильтр
    const filtered = filter === 'all'
        ? profiles
        : profiles.filter(p => p.category === filter);

    if (filtered.length === 0) {
        listEl.innerHTML = '<p class="info-text">📁 Профили не найдены для выбранной категории</p>';
        return;
    }

    let html = '';
    filtered.forEach(profile => {
        html += renderProfileItem(profile);
    });

    listEl.innerHTML = html;
}

// Отрисовка элемента профиля
function renderProfileItem(profile) {
    const categoryIcons = {
        'rectification': '🌀',
        'distillation': '🔥',
        'mashing': '🌾'
    };

    const categoryNames = {
        'rectification': 'Ректификация',
        'distillation': 'Дистилляция',
        'mashing': 'Затирка'
    };

    const icon = categoryIcons[profile.category] || '📁';
    const catName = categoryNames[profile.category] || profile.category;
    const builtinBadge = profile.isBuiltin ? '<span style="background: #2196F3; color: white; padding: 2px 8px; border-radius: 12px; font-size: 0.75em; margin-left: 8px;">Встроенный</span>' : '';

    const lastUsed = profile.lastUsed > 0
        ? new Date(profile.lastUsed * 1000).toLocaleDateString('ru-RU')
        : 'Не использовался';

    return `
        <div class="profile-item" style="background: var(--bg-primary); padding: 15px; margin-bottom: 10px; border-radius: 8px; border-left: 4px solid var(--accent-color);">
            <div style="display: flex; justify-content: space-between; align-items: start; margin-bottom: 10px;">
                <div style="flex: 1;">
                    <div style="font-weight: 600; font-size: 1.1em; margin-bottom: 5px;">
                        ${icon} ${profile.name}${builtinBadge}
                    </div>
                    <div style="color: var(--text-secondary); font-size: 0.9em;">
                        ${catName} • Использований: ${profile.useCount} • Последнее: ${lastUsed}
                    </div>
                </div>
                <div style="display: flex; gap: 5px;">
                    <button class="btn-icon" onclick="viewProfile('${profile.id}')" title="Просмотр">👁️</button>
                    <button class="btn-icon btn-success" onclick="quickLoadProfile('${profile.id}')" title="Загрузить">📥</button>
                    <button class="btn-icon" onclick="exportProfile('${profile.id}')" title="Экспорт">📤</button>
                    ${!profile.isBuiltin ? `<button class="btn-icon btn-danger" onclick="deleteProfile('${profile.id}')" title="Удалить">🗑️</button>` : ''}
                </div>
            </div>
        </div>
    `;
}

// Обновление статистики профилей
function updateProfilesStats(profiles) {
    document.getElementById('prof-stat-total').textContent = profiles.length;

    const builtin = profiles.filter(p => p.isBuiltin).length;
    const user = profiles.length - builtin;

    document.getElementById('prof-stat-builtin').textContent = builtin;
    document.getElementById('prof-stat-user').textContent = user;

    // Самый используемый
    if (profiles.length > 0) {
        const mostUsed = profiles.reduce((prev, current) =>
            (prev.useCount > current.useCount) ? prev : current
        );
        document.getElementById('prof-stat-popular').textContent =
            mostUsed.useCount > 0 ? mostUsed.name : '—';
    } else {
        document.getElementById('prof-stat-popular').textContent = '—';
    }
}

// Показать модальное окно создания профиля
function showCreateProfileModal() {
    currentProfileId = null;
    document.getElementById('profile-modal-title').textContent = 'Создание профиля';
    document.getElementById('profile-name').value = '';
    document.getElementById('profile-description').value = '';
    document.getElementById('profile-category').value = 'rectification';
    document.getElementById('profile-tags').value = '';
    document.getElementById('profile-modal').style.display = 'flex';
}

// Закрыть модальное окно создания
function closeProfileModal() {
    document.getElementById('profile-modal').style.display = 'none';
}

// Сохранить профиль
function saveProfile() {
    const name = document.getElementById('profile-name').value.trim();
    const description = document.getElementById('profile-description').value.trim();
    const category = document.getElementById('profile-category').value;
    const tagsStr = document.getElementById('profile-tags').value.trim();
    const tags = tagsStr ? tagsStr.split(',').map(t => t.trim()).filter(t => t) : [];

    if (!name) {
        alert('Пожалуйста, введите название профиля');
        return;
    }

    // TODO: Получить текущие параметры из формы управления
    // Пока используем значения по умолчанию
    const profile = {
        metadata: {
            name: name,
            description: description,
            category: category,
            tags: tags,
            author: 'user'
        },
        parameters: {
            mode: category,
            model: 'classic',
            heater: {
                maxPower: 3000,
                autoMode: true,
                pidKp: 2.0,
                pidKi: 0.5,
                pidKd: 1.0
            },
            rectification: {
                stabilizationMin: 20,
                headsVolume: 50,
                bodyVolume: 2000,
                tailsVolume: 100,
                headsSpeed: 150,
                bodySpeed: 300,
                tailsSpeed: 400,
                purgeMin: 5
            },
            distillation: {
                headsVolume: 0,
                targetVolume: 3000,
                speed: 500,
                endTemp: 96.0
            },
            temperatures: {
                maxCube: 98.0,
                maxColumn: 82.0,
                headsEnd: 78.5,
                bodyStart: 78.0,
                bodyEnd: 85.0
            },
            safety: {
                maxRuntime: 720,
                waterFlowMin: 2.0,
                pressureMax: 150
            }
        }
    };

    fetch('/api/profiles', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(profile)
    })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                closeProfileModal();
                loadProfilesList();
                alert('✅ Профиль успешно создан!');
            } else {
                alert('❌ Ошибка создания профиля: ' + (data.error || 'Неизвестная ошибка'));
            }
        })
        .catch(error => {
            console.error('Ошибка сохранения профиля:', error);
            alert('❌ Ошибка сохранения профиля');
        });
}

// Просмотр профиля
function viewProfile(id) {
    fetch(`/api/profiles/${id}`)
        .then(response => response.json())
        .then(profile => {
            showProfileViewModal(profile);
        })
        .catch(error => {
            console.error('Ошибка загрузки профиля:', error);
            alert('❌ Ошибка загрузки профиля');
        });
}

// Показать модальное окно просмотра профиля
function showProfileViewModal(profile) {
    currentProfileId = profile.id;
    document.getElementById('profile-view-title').textContent = profile.metadata.name;

    const body = document.getElementById('profile-view-body');
    const catNames = {
        'rectification': 'Ректификация',
        'distillation': 'Дистилляция',
        'mashing': 'Затирка'
    };

    let html = `
        <div class="modal-section">
            <div class="modal-section-title">📋 Метаданные</div>
            <div class="modal-info-grid">
                <div><strong>Название:</strong> ${profile.metadata.name}</div>
                <div><strong>Категория:</strong> ${catNames[profile.metadata.category] || profile.metadata.category}</div>
                <div><strong>Описание:</strong> ${profile.metadata.description || '—'}</div>
                <div><strong>Автор:</strong> ${profile.metadata.author}</div>
                <div><strong>Теги:</strong> ${profile.metadata.tags.join(', ') || '—'}</div>
                <div><strong>Встроенный:</strong> ${profile.metadata.isBuiltin ? 'Да' : 'Нет'}</div>
            </div>
        </div>

        <div class="modal-section">
            <div class="modal-section-title">⚙️ Параметры ректификации</div>
            <div class="modal-info-grid">
                <div><strong>Стабилизация:</strong> ${profile.parameters.rectification.stabilizationMin} мин</div>
                <div><strong>Объём голов:</strong> ${profile.parameters.rectification.headsVolume} мл</div>
                <div><strong>Объём тела:</strong> ${profile.parameters.rectification.bodyVolume} мл</div>
                <div><strong>Объём хвостов:</strong> ${profile.parameters.rectification.tailsVolume} мл</div>
                <div><strong>Скорость голов:</strong> ${profile.parameters.rectification.headsSpeed} мл/ч/кВт</div>
                <div><strong>Скорость тела:</strong> ${profile.parameters.rectification.bodySpeed} мл/ч/кВт</div>
            </div>
        </div>

        <div class="modal-section">
            <div class="modal-section-title">🌡️ Температурные пороги</div>
            <div class="modal-info-grid">
                <div><strong>Макс. куб:</strong> ${profile.parameters.temperatures.maxCube}°C</div>
                <div><strong>Макс. колонна:</strong> ${profile.parameters.temperatures.maxColumn}°C</div>
                <div><strong>Окончание голов:</strong> ${profile.parameters.temperatures.headsEnd}°C</div>
                <div><strong>Начало тела:</strong> ${profile.parameters.temperatures.bodyStart}°C</div>
                <div><strong>Окончание тела:</strong> ${profile.parameters.temperatures.bodyEnd}°C</div>
            </div>
        </div>

        <div class="modal-section">
            <div class="modal-section-title">📊 Статистика использования</div>
            <div class="modal-info-grid">
                <div><strong>Использований:</strong> ${profile.statistics.useCount}</div>
                <div><strong>Средняя длительность:</strong> ${Math.round(profile.statistics.avgDuration / 60)} мин</div>
                <div><strong>Средний выход:</strong> ${profile.statistics.avgYield} мл</div>
                <div><strong>Успешность:</strong> ${profile.statistics.successRate.toFixed(1)}%</div>
            </div>
        </div>
    `;

    body.innerHTML = html;
    document.getElementById('profile-view-modal').style.display = 'flex';
}

// Закрыть модальное окно просмотра
function closeProfileViewModal() {
    document.getElementById('profile-view-modal').style.display = 'none';
    currentProfileId = null;
}

// Быстрая загрузка профиля
function quickLoadProfile(id) {
    if (!confirm('Загрузить этот профиль в текущие настройки?')) return;

    fetch(`/api/profiles/${id}/load`, {
        method: 'POST'
    })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                alert('✅ Профиль успешно загружен! Проверьте настройки в разделе "Управление".');
            } else {
                alert('❌ Ошибка загрузки профиля: ' + (data.error || 'Неизвестная ошибка'));
            }
        })
        .catch(error => {
            console.error('Ошибка загрузки профиля:', error);
            alert('❌ Ошибка загрузки профиля');
        });
}

// Загрузка профиля в настройки (из модального окна)
function loadProfileToSettings() {
    if (!currentProfileId) return;
    closeProfileViewModal();
    quickLoadProfile(currentProfileId);
}

// Удаление профиля
function deleteProfile(id) {
    if (!confirm('Удалить этот профиль? Действие нельзя отменить.')) return;

    fetch(`/api/profiles/${id}`, {
        method: 'DELETE'
    })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                loadProfilesList();
                alert('✅ Профиль удалён');
            } else {
                alert('❌ ' + (data.error || 'Ошибка удаления профиля'));
            }
        })
        .catch(error => {
            console.error('Ошибка удаления профиля:', error);
            alert('❌ Ошибка удаления профиля');
        });
}

// Очистка пользовательских профилей
function clearUserProfiles() {
    if (!confirm('Удалить ВСЕ пользовательские профили? Встроенные рецепты останутся. Действие нельзя отменить!')) return;

    fetch('/api/profiles', {
        method: 'DELETE'
    })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                loadProfilesList();
                alert('✅ Все пользовательские профили удалены');
            } else {
                alert('❌ Ошибка очистки профилей');
            }
        })
        .catch(error => {
            console.error('Ошибка очистки профилей:', error);
            alert('❌ Ошибка очистки профилей');
        });
}

// Экспорт одного профиля
function exportProfile(id) {
    fetch(`/api/profiles/${id}/export`)
        .then(response => response.json())
        .then(data => {
            // Создаем blob и скачиваем
            const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `profile_${data.metadata.name.replace(/\s+/g, '_')}_${id}.json`;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
        })
        .catch(error => {
            console.error('Ошибка экспорта профиля:', error);
            alert('❌ Ошибка экспорта профиля');
        });
}

// Экспорт всех профилей
function exportAllProfiles() {
    const includeBuiltin = confirm('Включить встроенные рецепты в экспорт?');

    fetch(`/api/profiles/export${includeBuiltin ? '?includeBuiltin=true' : ''}`)
        .then(response => response.json())
        .then(data => {
            if (!data || data.length === 0) {
                alert('Нет профилей для экспорта');
                return;
            }

            // Создаем blob и скачиваем
            const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            const timestamp = new Date().toISOString().split('T')[0];
            a.download = `profiles_export_${timestamp}.json`;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);

            alert(`✅ Экспортировано профилей: ${data.length}`);
        })
        .catch(error => {
            console.error('Ошибка экспорта профилей:', error);
            alert('❌ Ошибка экспорта профилей');
        });
}

// Показать модальное окно импорта
let importFileData = null;

function showImportModal() {
    importFileData = null;
    document.getElementById('import-file-input').value = '';
    document.getElementById('import-preview').style.display = 'none';
    document.getElementById('import-btn').disabled = true;
    document.getElementById('profile-import-modal').style.display = 'flex';

    // Добавляем обработчик выбора файла
    document.getElementById('import-file-input').onchange = function (e) {
        const file = e.target.files[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = function (event) {
            try {
                importFileData = JSON.parse(event.target.result);

                // Показываем предпросмотр
                let previewText = '';
                if (Array.isArray(importFileData)) {
                    previewText = `Массив из ${importFileData.length} профилей`;
                } else if (importFileData.metadata) {
                    previewText = `Профиль: ${importFileData.metadata.name}`;
                } else {
                    throw new Error('Неверный формат JSON');
                }

                document.getElementById('import-preview-text').textContent = previewText;
                document.getElementById('import-preview').style.display = 'block';
                document.getElementById('import-btn').disabled = false;
            } catch (error) {
                alert('❌ Ошибка чтения файла: неверный формат JSON');
                importFileData = null;
                document.getElementById('import-btn').disabled = true;
            }
        };
        reader.readAsText(file);
    };
}

// Закрыть модальное окно импорта
function closeImportModal() {
    document.getElementById('profile-import-modal').style.display = 'none';
    importFileData = null;
}

// Выполнить импорт профилей
function doImportProfiles() {
    if (!importFileData) {
        alert('Выберите файл для импорта');
        return;
    }

    fetch('/api/profiles/import', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(importFileData)
    })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                closeImportModal();
                loadProfilesList();
                alert(`✅ Импортировано профилей: ${data.imported}`);
            } else {
                alert('❌ Ошибка импорта: ' + (data.error || 'Неизвестная ошибка'));
            }
        })
        .catch(error => {
            console.error('Ошибка импорта профилей:', error);
            alert('❌ Ошибка импорта профилей');
        });
}

// ============================================================================

// Закрытие модального окна сравнения при клике на overlay
document.addEventListener('DOMContentLoaded', function () {
    const compareOverlay = document.getElementById('compare-modal');
    if (compareOverlay) {
        compareOverlay.addEventListener('click', function (e) {
            if (e.target === compareOverlay) {
                closeCompareModal();
            }
        });
    }
});
