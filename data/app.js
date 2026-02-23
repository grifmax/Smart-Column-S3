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

const MODE_IDLE = 0;
const MODE_RECT = 1;
const MODE_MANUAL = 2;
const MODE_DIST = 3;
const MODE_MASH = 4;
const MODE_HOLD = 5;

function getModeLabel(mode) {
    switch (mode) {
        case MODE_IDLE: return 'Idle';
        case MODE_RECT: return 'Rectification';
        case MODE_MANUAL: return 'Manual';
        case MODE_DIST: return 'Distillation';
        case MODE_MASH: return 'Mashing';
        case MODE_HOLD: return 'Hold';
        default: return 'Unknown';
    }
}

function getModeCssClass(mode) {
    switch (mode) {
        case MODE_IDLE: return 'mode-idle';
        case MODE_RECT: return 'mode-rectification';
        case MODE_MANUAL: return 'mode-manual';
        case MODE_DIST: return 'mode-distillation';
        case MODE_MASH: return 'mode-mashing';
        case MODE_HOLD: return 'mode-hold';
        default: return 'mode-idle';
    }
}

function resolveMode(modeValue, modeStrValue) {
    const modeNum = Number(modeValue);
    if (Number.isFinite(modeNum)) return modeNum;
    if (typeof modeStrValue !== 'string') return MODE_IDLE;

    const modeMap = {
        idle: MODE_IDLE,
        rect: MODE_RECT,
        rectification: MODE_RECT,
        manual: MODE_MANUAL,
        dist: MODE_DIST,
        distillation: MODE_DIST,
        mash: MODE_MASH,
        mashing: MODE_MASH,
        hold: MODE_HOLD
    };
    return modeMap[modeStrValue.toLowerCase()] ?? MODE_IDLE;
}

function updateLandingUi(snapshot) {
    const modeChip = document.getElementById('landing-mode-chip');
    if (modeChip && snapshot.mode !== undefined) {
        const modeNum = resolveMode(snapshot.mode, snapshot.modeStr);
        modeChip.textContent = getModeLabel(modeNum).toUpperCase();
        modeChip.className = `landing-chip ${getModeCssClass(modeNum)}`;
    }

    const phaseChip = document.getElementById('landing-phase-chip');
    if (phaseChip && snapshot.phaseText !== undefined) {
        phaseChip.textContent = `PHASE ${snapshot.phaseText || '-'}`;
    }

    const safetyChip = document.getElementById('landing-safety-chip');
    if (safetyChip && snapshot.safetyOk !== undefined) {
        if (snapshot.safetyOk) {
            safetyChip.textContent = 'SAFETY OK';
            safetyChip.className = 'landing-chip landing-chip-ok';
        } else {
            safetyChip.textContent = 'SAFETY ALERT';
            safetyChip.className = 'landing-chip landing-chip-warn';
        }
    }

    if (snapshot.tCube !== undefined) {
        const el = document.getElementById('landing-cube-value');
        if (el) el.textContent = `${snapshot.tCube.toFixed(1)}°C`;
    }
    if (snapshot.power !== undefined) {
        const el = document.getElementById('landing-power-value');
        if (el) el.textContent = `${snapshot.power.toFixed(0)} W`;
    }
    if (snapshot.pressureCube !== undefined) {
        const el = document.getElementById('landing-pressure-value');
        if (el) el.textContent = `${snapshot.pressureCube.toFixed(1)} мм`;
    }
    if (snapshot.pumpSpeed !== undefined) {
        const el = document.getElementById('landing-pump-value');
        if (el) el.textContent = `${snapshot.pumpSpeed.toFixed(0)} мл/ч`;
    }
    if (snapshot.abv !== undefined) {
        const el = document.getElementById('landing-abv-value');
        if (el) el.textContent = `${snapshot.abv.toFixed(1)} %`;
    }
    if (snapshot.waterIn !== undefined) {
        const el = document.getElementById('landing-water-in');
        if (el) el.textContent = `${snapshot.waterIn.toFixed(1)}°C`;
    }
    if (snapshot.waterOut !== undefined) {
        const el = document.getElementById('landing-water-out');
        if (el) el.textContent = `${snapshot.waterOut.toFixed(1)}°C`;
    }
    if (snapshot.voltage !== undefined) {
        const el = document.getElementById('landing-voltage');
        if (el) el.textContent = `${snapshot.voltage.toFixed(0)} V`;
    }

    const upd = document.getElementById('landing-updated');
    if (upd) {
        upd.textContent = new Date().toLocaleTimeString('ru-RU', { hour12: false });
    }
}

const STATUS_POLL_INTERVAL_MS = 2000;
let statusPollTimer = null;

function startStatusPolling(immediate = false) {
    if (statusPollTimer) return;
    if (immediate) loadStatus();
    statusPollTimer = setInterval(loadStatus, STATUS_POLL_INTERVAL_MS);
}

function stopStatusPolling() {
    if (!statusPollTimer) return;
    clearInterval(statusPollTimer);
    statusPollTimer = null;
}


// ============================================================================
// Режим запуска UI: local (прямо на ESP32) vs cloud (через web-proxy кабинет)
// ============================================================================

let isCloudProxyMode = false;

async function detectCloudProxyMode() {
    // Идея: /api/web/* существует только на web-proxy (cloud_proxy).
    // На ESP32 этот путь обычно отдаст 404 -> local mode.
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 1200);

    try {
        const response = await fetch('/api/web/user', {
            credentials: 'same-origin',
            signal: controller.signal
        });

        // На cloud-proxy:
        // - 200: авторизован
        // - 401: не авторизован (но endpoint существует)
        // На ESP32:
        // - 404: endpoint отсутствует
        if (response.status === 404) return false;
        return response.status === 200 || response.status === 401 || response.status >= 400;
    } catch (e) {
        // Network error / timeout — считаем локальным режимом, чтобы не дергать web API
        return false;
    } finally {
        clearTimeout(timeoutId);
    }
}

function setCloudOnlyUiVisible(visible) {
    // Cloud-only элементы помечаем атрибутом data-cloud-only="1"
    document.querySelectorAll('[data-cloud-only="1"]').forEach(el => {
        el.style.display = visible ? '' : 'none';
    });
}

function closeTopMenu() {
    const dropdown = document.getElementById('top-menu-dropdown');
    const toggle = document.getElementById('top-menu-toggle');
    if (dropdown) dropdown.classList.remove('open');
    if (toggle) toggle.setAttribute('aria-expanded', 'false');
}

function toggleTopMenu(event) {
    if (event) event.stopPropagation();
    const dropdown = document.getElementById('top-menu-dropdown');
    const toggle = document.getElementById('top-menu-toggle');
    if (!dropdown || !toggle) return;
    const willOpen = !dropdown.classList.contains('open');
    dropdown.classList.toggle('open', willOpen);
    toggle.setAttribute('aria-expanded', willOpen ? 'true' : 'false');
}

function initTopMenu() {
    document.addEventListener('click', (event) => {
        const dropdown = document.getElementById('top-menu-dropdown');
        const toggle = document.getElementById('top-menu-toggle');
        if (!dropdown || !toggle) return;
        const target = event.target;
        if (dropdown.contains(target) || toggle.contains(target)) return;
        closeTopMenu();
    });

    document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') closeTopMenu();
    });
}

const OPERATOR_VIEW_STORAGE_KEY = 'ui.operatorView';

function setOperatorView(view) {
    const screen = document.querySelector('#monitor .operator-screen');
    const button = document.getElementById('operator-view-toggle');
    if (!screen) return;

    const normalizedView = view === 'compact' ? 'compact' : 'instrument';
    const isInstrument = normalizedView === 'instrument';
    screen.setAttribute('data-view', normalizedView);
    screen.classList.toggle('operator-screen-instrument', isInstrument);
    screen.classList.toggle('operator-screen-compact', !isInstrument);

    if (button) {
        button.textContent = `View: ${isInstrument ? 'Instrument' : 'Compact'}`;
        button.classList.toggle('btn-info', isInstrument);
        button.classList.toggle('btn-warning', !isInstrument);
    }
}

function toggleOperatorView() {
    const screen = document.querySelector('#monitor .operator-screen');
    if (!screen) return;

    const nextView = screen.classList.contains('operator-screen-instrument') ? 'compact' : 'instrument';
    setOperatorView(nextView);
    try {
        localStorage.setItem(OPERATOR_VIEW_STORAGE_KEY, nextView);
    } catch (e) {
        console.warn('operator view save failed:', e);
    }
}

function initOperatorViewToggle() {
    const screen = document.querySelector('#monitor .operator-screen');
    if (!screen) return;
    const button = document.getElementById('operator-view-toggle');

    // If the toggle control is not present in layout, keep stable instrument view.
    if (!button) {
        setOperatorView('instrument');
        return;
    }

    let saved = 'instrument';
    try {
        saved = localStorage.getItem(OPERATOR_VIEW_STORAGE_KEY) || 'instrument';
    } catch (e) {
        console.warn('operator view load failed:', e);
    }
    setOperatorView(saved);
}


// Инициализация при загрузке страницы

document.addEventListener('DOMContentLoaded', async function () {

    initTabs();
    initTopMenu();
    initOperatorViewToggle();

    initMashingHoldControls();

    loadTheme();

    loadDemoMode();  // Загрузить состояние демо-режима

    initMiniChart();

    loadMemoryStatsPreference();

    loadPumpInfo();

    loadVersionInfo();
    loadTelegramSettings();

    // Определяем режим: локально на ESP32 или через cloud-proxy кабинет
    isCloudProxyMode = await detectCloudProxyMode();
    setCloudOnlyUiVisible(isCloudProxyMode);

    if (isCloudProxyMode) {
        loadUserInfo();  // Загрузить информацию о пользователе
        loadESP32Devices();  // Загрузить список устройств ESP32
        loadDiscoveredDevices(); // Устройства с активным PIN
        setInterval(loadDiscoveredDevices, 30000);
    }

    // Запускаем fallback polling сразу, после подключения WS он будет остановлен.
    startStatusPolling(true);
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



        ws.onopen = function () {

            isConnected = true;
            stopStatusPolling();

            updateConnectionStatus(true);

            addLog('✓ Подключено к контроллеру', 'info');



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

            addLog('✗ Ошибка подключения', 'error');

        };



        ws.onclose = function () {

            isConnected = false;
            startStatusPolling(true);

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
        startStatusPolling(true);

    }

}



function sendCommand(action, param = '', value = 0) {

    if (ws && ws.readyState === WebSocket.OPEN) {

        const cmd = { action, param, value };

        ws.send(JSON.stringify(cmd));

        addLog(`📤 Команда: ${action} ${param} ${value}`);

    } else {

        addLog('✗ Нет подключения к контроллеру', 'error');

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

    const miniChartContainer = document.querySelector("#mini-chart");
    if (!miniChartContainer) return;

    // Graceful fallback for offline/AP mode when CDN script is not available.
    if (typeof window.ApexCharts === 'undefined') {
        miniChartContainer.innerHTML = '<div class="info-display">Мини-график временно недоступен</div>';
        addLog('⚠ Мини-график недоступен: библиотека графика не загружена', 'warning');
        miniChart = null;
        return;
    }

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



    miniChart = new ApexCharts(miniChartContainer, options);

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
    let phaseText = undefined;

    // Режим

    if (data.mode !== undefined) {

        const modeNum = Number(data.mode);
        const modeLabel = Number.isFinite(modeNum)
            ? getModeLabel(modeNum).toUpperCase()
            : 'UNKNOWN';
        const modeClass = Number.isFinite(modeNum)
            ? getModeCssClass(modeNum)
            : 'mode-idle';
        const modeEl = document.getElementById('mode');

        modeEl.textContent = modeLabel;

        modeEl.className = `value ${modeClass}`;
        if (Number.isFinite(modeNum)) {
            currentMode = modeNum;
        }

    }

    if (data.paused !== undefined) {
        currentPaused = Boolean(data.paused);
    }



    // Фаза

    if (data.phase !== undefined) {

        const phaseNames = ['IDLE', 'HEATING', 'STABIL', 'HEADS', 'PURGE', 'BODY', 'TAILS', 'FINISH', 'ERROR'];

        phaseText = phaseNames[data.phase] || '-';
        document.getElementById('phase').textContent = phaseText;

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
        const opUptime = document.getElementById('operator-uptime');
        if (opUptime) opUptime.textContent = formatUptime(data.uptime);

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

    updateLandingUi({
        mode: data.mode,
        phaseText,
        tCube: data.t_cube,
        power: data.power,
        pressureCube: data.p_cube,
        pumpSpeed: data.pump_speed,
        abv: data.abv,
        waterIn: data.t_water_in,
        waterOut: data.t_water_out,
        voltage: data.voltage
    });

    updateButtonStates();

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

    // В навигации есть и кнопки-вкладки (data-tab), и ссылки на другие страницы (<a>).
    // Для вкладок работаем только с элементами, у которых есть data-tab.
    const tabs = document.querySelectorAll('.tab');

    tabs.forEach(tab => {

        tab.addEventListener('click', () => {

            const targetId = tab.getAttribute('data-tab');

            // Это ссылка на другую страницу (charts/manual/...), не трогаем.
            if (!targetId) {
                closeTopMenu();
                return;
            }



            // Убрать активный класс со всех вкладок

            tabs.forEach(t => t.classList.remove('active'));

            document.querySelectorAll('.tab-content').forEach(content => {

                content.classList.remove('active');

            });



            // Добавить активный класс к выбранной вкладке

            tab.classList.add('active');

            const targetEl = document.getElementById(targetId);
            if (targetEl) {
                targetEl.classList.add('active');
            }



            // Загрузить историю при переключении на вкладку "История"

            if (targetId === 'history') {

                loadHistoryList();

            }

            closeTopMenu();

        });

    });

}

function activateTabById(targetId) {
    const tab = document.querySelector(`.tab[data-tab="${targetId}"]`);
    if (tab) tab.click();
}



// ============================================================================

// Control Functions

// ============================================================================



function confirmModeSwitch(targetModeId, targetModeName) {

    const targetLabel = targetModeName || getModeLabel(targetModeId);

    if (currentMode === MODE_IDLE) return true;

    if (currentMode === targetModeId) {
        addLog(`Mode "${targetLabel}" is already running`, 'warning');
        return false;
    }

    const currentModeLabel = getModeLabel(currentMode);

    return confirm(
        `Current mode "${currentModeLabel}" is running.\\n` +
        `Switch to "${targetLabel}"?\\n\\n` +
        `Current process will be stopped.`
    );

}



async function startRectification() {

    if (!confirmModeSwitch(MODE_RECT, 'Auto-rectification')) return;

    try {

        addLog('📤 Отправка команды запуска авто-ректификации...', 'info');



        const response = await fetch('/api/process/start', {

            method: 'POST',

            headers: { 'Content-Type': 'application/json' },

            body: JSON.stringify({ mode: 'rectification' })

        });



        if (response.ok) {

            const data = await response.json();

            addLog('✓ Авто-ректификация запущена', 'success');

            if (data.warning) {

                addLog('⚠️ ' + data.warning, 'warning');

            }

            setTimeout(loadStatus, 500); // Обновить статус

        } else {

            const error = await response.text();

            addLog('✗ Ошибка (' + response.status + '): ' + error, 'error');

        }

    } catch (e) {

        addLog('✗ Ошибка сети: ' + e.message, 'error');

        console.error('Start rectification error:', e);

    }

}



function startManual() {

    // Переход на страницу ручного управления

    if (!confirmModeSwitch(MODE_MANUAL, 'Manual rectification')) return;
    window.location.href = 'manual.html';

}



async function startDistillation() {

    if (!confirmModeSwitch(MODE_DIST, 'Distillation')) return;

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

            addLog('✗ Ошибка (' + response.status + '): ' + error, 'error');

        }

    } catch (e) {

        addLog('✗ Ошибка сети: ' + e.message, 'error');

        console.error('Start distillation error:', e);

    }

}



// ============================================================================
// Дополнительные режимы: Затирка / Hold
// ============================================================================

function initMashingHoldControls() {
    try {
        const select = document.getElementById('extra-mode-select');
        const mashingControls = document.getElementById('mashing-controls');
        const holdControls = document.getElementById('hold-controls');

        const setExtraMode = (mode) => {
            const normalized = mode || 'none';

            if (mashingControls) {
                mashingControls.style.display = normalized === 'mashing' ? '' : 'none';
            }
            if (holdControls) {
                holdControls.style.display = normalized === 'hold' ? '' : 'none';
            }

            try {
                localStorage.setItem('control.extraMode', normalized);
            } catch (_) {
                // ignore
            }
        };

        const mashName = document.getElementById('mash-profile-name');
        if (mashName && !mashName.value) {
            mashName.value = 'Default Mashing';
        }

        const mashStepsEl = document.getElementById('mash-steps');
        if (mashStepsEl && mashStepsEl.children.length === 0) {
            // По умолчанию — шаги как в backend-дефолте
            addMashStep({ temperature: 38.0, duration: 20, name: 'Кислотная пауза' });
            addMashStep({ temperature: 52.0, duration: 20, name: 'Белковая пауза' });
            addMashStep({ temperature: 63.0, duration: 40, name: 'Мальтозная пауза' });
            addMashStep({ temperature: 72.0, duration: 20, name: 'Осахаривание' });
            addMashStep({ temperature: 78.0, duration: 10, name: 'Мэш-аут' });
        }

        const holdStepsEl = document.getElementById('hold-steps');
        if (holdStepsEl && holdStepsEl.children.length === 0) {
            // Дефолт — одна ступень 65°C на 60 минут
            addHoldStep({ temperature: 65.0, duration: 60 });
        }

        if (select) {
            select.addEventListener('change', () => setExtraMode(select.value));

            let saved = 'none';
            try {
                saved = localStorage.getItem('control.extraMode') || 'none';
            } catch (_) {
                // ignore
            }

            // Если разметка обновилась и select ещё не выставлен — восстановим
            if (!select.value || select.value === 'none') {
                select.value = saved;
            }
            setExtraMode(select.value);
        } else {
            // Если селектора нет (старый HTML), просто скрываем блоки по умолчанию
            if (mashingControls) mashingControls.style.display = 'none';
            if (holdControls) holdControls.style.display = 'none';
        }
    } catch (e) {
        console.error('initMashingHoldControls error:', e);
    }
}

function createStepRow({ mode, temperature, duration, name }) {
    const row = document.createElement('div');
    row.dataset.stepRow = mode;
    row.style.display = 'flex';
    row.style.gap = '10px';
    row.style.flexWrap = 'wrap';
    row.style.alignItems = 'center';
    row.style.marginBottom = '8px';

    const tempInput = document.createElement('input');
    tempInput.type = 'number';
    tempInput.step = '0.1';
    tempInput.min = '0';
    tempInput.placeholder = 'Темп, °C';
    tempInput.value = (temperature ?? '') === '' ? '' : String(temperature);
    tempInput.dataset.field = 'temperature';
    tempInput.style.width = '140px';

    const durInput = document.createElement('input');
    durInput.type = 'number';
    durInput.step = '1';
    durInput.min = '1';
    durInput.placeholder = 'Мин';
    durInput.value = (duration ?? '') === '' ? '' : String(duration);
    durInput.dataset.field = 'duration';
    durInput.style.width = '110px';

    row.appendChild(tempInput);
    row.appendChild(durInput);

    if (mode === 'mash') {
        const nameInput = document.createElement('input');
        nameInput.type = 'text';
        nameInput.placeholder = 'Имя шага (опц.)';
        nameInput.value = name || '';
        nameInput.dataset.field = 'name';
        nameInput.style.flex = '1';
        nameInput.style.minWidth = '180px';
        row.appendChild(nameInput);
    }

    const removeBtn = document.createElement('button');
    removeBtn.className = 'btn btn-sm';
    removeBtn.textContent = '✖';
    removeBtn.title = 'Удалить шаг';
    removeBtn.onclick = () => row.remove();
    row.appendChild(removeBtn);

    return row;
}

function addMashStep(step = {}) {
    const el = document.getElementById('mash-steps');
    if (!el) return;
    el.appendChild(createStepRow({
        mode: 'mash',
        temperature: step.temperature,
        duration: step.duration,
        name: step.name
    }));
}

function addHoldStep(step = {}) {
    const el = document.getElementById('hold-steps');
    if (!el) return;
    el.appendChild(createStepRow({
        mode: 'hold',
        temperature: step.temperature,
        duration: step.duration
    }));
}

function readStepsFromUI(containerId, mode) {
    const el = document.getElementById(containerId);
    if (!el) return [];

    const rows = Array.from(el.querySelectorAll(`div[data-step-row="${mode}"]`));
    const steps = [];

    for (const row of rows) {
        const tempStr = row.querySelector('input[data-field="temperature"]')?.value ?? '';
        const durStr = row.querySelector('input[data-field="duration"]')?.value ?? '';
        const temperature = Number.parseFloat(tempStr);
        const duration = Number.parseInt(durStr, 10);

        if (!Number.isFinite(temperature) || temperature <= 0) continue;
        if (!Number.isFinite(duration) || duration <= 0) continue;

        const step = { temperature, duration };
        if (mode === 'mash') {
            const name = (row.querySelector('input[data-field="name"]')?.value ?? '').trim();
            if (name) step.name = name;
        }
        steps.push(step);
    }

    return steps;
}

async function startMashing() {
    if (!confirmModeSwitch(MODE_MASH, 'Mashing')) return;
    try {
        const profileName = (document.getElementById('mash-profile-name')?.value ?? '').trim() || 'Mashing';
        const steps = readStepsFromUI('mash-steps', 'mash');

        if (!steps.length) {
            addLog('✗ Затирка: добавьте хотя бы один корректный шаг (температура и длительность)', 'error');
            return;
        }

        addLog('📤 Отправка команды запуска затирки...', 'info');

        const response = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                mode: 'mashing',
                params: {
                    profile: {
                        name: profileName,
                        steps
                    }
                }
            })
        });

        if (response.ok) {
            const data = await response.json();
            addLog('✓ Затирка запущена', 'success');
            if (data.warning) addLog('⚠️ ' + data.warning, 'warning');
            setTimeout(loadStatus, 500);
        } else {
            const error = await response.text();
            addLog('✗ Ошибка (' + response.status + '): ' + error, 'error');
        }
    } catch (e) {
        addLog('✗ Ошибка сети: ' + e.message, 'error');
        console.error('Start mashing error:', e);
    }
}

async function startHold() {
    if (!confirmModeSwitch(MODE_HOLD, 'Hold')) return;
    try {
        const steps = readStepsFromUI('hold-steps', 'hold');

        if (!steps.length) {
            addLog('✗ Hold: добавьте хотя бы один корректный шаг (температура и длительность)', 'error');
            return;
        }

        addLog('📤 Отправка команды запуска Hold...', 'info');

        const response = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                mode: 'hold',
                params: { steps }
            })
        });

        if (response.ok) {
            const data = await response.json();
            addLog('✓ Hold запущен', 'success');
            if (data.warning) addLog('⚠️ ' + data.warning, 'warning');
            setTimeout(loadStatus, 500);
        } else {
            const error = await response.text();
            addLog('✗ Ошибка (' + response.status + '): ' + error, 'error');
        }
    } catch (e) {
        addLog('✗ Ошибка сети: ' + e.message, 'error');
        console.error('Start hold error:', e);
    }
}

// ============================================================================
// Cloud (IoT tunnel)
// ============================================================================

function updateCloudUiFromStatus(data) {
    const deviceIdEl = document.getElementById('device-id');
    if (deviceIdEl && data.deviceId) {
        deviceIdEl.textContent = String(data.deviceId);
    }

    const enabledEl = document.getElementById('cloud-enabled');
    const urlEl = document.getElementById('cloud-tunnel-url');
    const connEl = document.getElementById('cloud-conn-status');
    const authEl = document.getElementById('cloud-auth-status');
    const claimEl = document.getElementById('cloud-claim-status');

    if (data.cloud) {
        if (enabledEl && typeof data.cloud.enabled === 'boolean') {
            enabledEl.checked = data.cloud.enabled;
        }
        if (urlEl && typeof data.cloud.tunnelUrl === 'string' && document.activeElement !== urlEl) {
            if (!urlEl.value) urlEl.value = data.cloud.tunnelUrl;
        }
        if (connEl) connEl.textContent = data.cloud.connected ? 'online' : 'offline';
        if (authEl) authEl.textContent = data.cloud.authenticated ? 'ok' : 'no';

        if (claimEl) {
            if (data.cloud.claimActive && data.cloud.claimCode) {
                let remaining = null;
                if (data.cloud.claimExpiresAt !== undefined && data.uptime !== undefined) {
                    remaining = Math.max(0, Math.round(Number(data.cloud.claimExpiresAt) - Number(data.uptime)));
                }
                claimEl.textContent = remaining !== null
                    ? `${data.cloud.claimCode} (ещё ~${remaining}с)`
                    : String(data.cloud.claimCode);
            } else {
                claimEl.textContent = 'нет';
            }
        }
    }
}

async function saveCloudConfig() {
    const enabledEl = document.getElementById('cloud-enabled');
    const urlEl = document.getElementById('cloud-tunnel-url');

    const enabled = !!enabledEl?.checked;
    const tunnelUrl = (urlEl?.value || '').trim();

    try {
        addLog('📤 Сохранение настроек облака...', 'info');
        const resp = await fetch('/api/cloud/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ enabled, tunnelUrl })
        });
        if (!resp.ok) {
            const t = await resp.text();
            addLog('✗ Ошибка сохранения облака: ' + t, 'error');
            return;
        }
        addLog('✓ Настройки облака сохранены', 'success');
        setTimeout(loadStatus, 500);
    } catch (e) {
        addLog('✗ Ошибка сети: ' + e.message, 'error');
    }
}

async function generateCloudClaim() {
    try {
        addLog('📤 Генерация PIN для привязки...', 'info');
        const resp = await fetch('/api/cloud/claim', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ttlSeconds: 600 })
        });
        if (!resp.ok) {
            const t = await resp.text();
            addLog('✗ Ошибка генерации PIN: ' + t, 'error');
            return;
        }
        const data = await resp.json();
        addLog('✓ PIN сгенерирован: ' + (data.claimCode || ''), 'success');
        setTimeout(loadStatus, 200);
    } catch (e) {
        addLog('✗ Ошибка сети: ' + e.message, 'error');
    }
}

async function stopProcess() {

    if (!confirm('Остановить процесс?')) return;



    try {

        const response = await fetch('/api/process/stop', {

            method: 'POST'

        });



        if (response.ok) {

            addLog('✓ Процесс остановлен', 'warning');

            setTimeout(loadStatus, 500); // Обновить статус

        } else {

            addLog('✗ Ошибка остановки', 'error');

        }

    } catch (e) {

        addLog('✗ Ошибка: ' + e.message, 'error');

    }

}



async function pauseProcess() {

    try {

        const response = await fetch('/api/process/pause', {

            method: 'POST'

        });



        if (response.ok) {

            addLog('✓ Процесс приостановлен', 'info');

            setTimeout(loadStatus, 500); // Обновить статус

        } else {

            addLog('✗ Ошибка паузы', 'error');

        }

    } catch (e) {

        addLog('✗ Ошибка: ' + e.message, 'error');

    }

}



async function resumeProcess() {

    try {

        const response = await fetch('/api/process/resume', {

            method: 'POST'

        });



        if (response.ok) {

            addLog('✓ Процесс возобновлен', 'info');

            setTimeout(loadStatus, 500); // Обновить статус

        } else {

            addLog('✗ Ошибка возобновления', 'error');

        }

    } catch (e) {

        addLog('✗ Ошибка: ' + e.message, 'error');

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

        if (!response.ok) {
            // Если статус недоступен (401/404/5xx) — не оставляем UI в "случайном" состоянии.
            // Делаем безопасный фолбэк: считаем процесс остановленным и отключаем управляющие кнопки по state.
            const msg = `✗ Статус недоступен (/api/status): HTTP ${response.status}`;
            addLog(msg, 'error');

            // Сбросить состояние, чтобы кнопки не выглядели как "процесс запущен"
            currentMode = 0;
            currentPaused = false;
            updateButtonStates();
            return;
        }



        const data = await response.json();



        // Обновить состояние процесса

        // Нормализуем mode: ожидаем число (0=IDLE), но на прокси/кастомных сборках
        // может прилететь строка. Для кнопок достаточно корректно определить IDLE.
        currentMode = resolveMode(data.mode, data.modeStr);

        currentPaused = Boolean(data.paused);



        // Сохранить мощность ТЭНа из настроек

        if (data.equipment && data.equipment.heaterPowerW) {

            maxHeaterPower = data.equipment.heaterPowerW;

            updateHeaterSlider();

        }



        // Обновить UI с новым форматом данных (не должен ломать обновление кнопок)
        try {
            updateUIFromStatus(data);
        } catch (e) {
            console.error('updateUIFromStatus error:', e);
        }



        // Обновить состояние кнопок
        updateButtonStates();



    } catch (e) {

        console.error('Ошибка загрузки статуса:', e);

    }

}



function updateUIFromStatus(data) {
    let phaseText = '-';

    // Режим

    if (data.modeStr !== undefined || data.mode !== undefined) {

        const modeEl = document.getElementById('mode');

        if (modeEl) {
            const resolvedMode = resolveMode(data.mode, data.modeStr);

            modeEl.textContent = getModeLabel(resolvedMode).toUpperCase();
            modeEl.className = `value ${getModeCssClass(resolvedMode)}`;

        }

    }



        // Фаза

    if (data.phaseStr !== undefined) {

        const phaseEl = document.getElementById('phase');

        if (phaseEl) {

            phaseText = data.phaseStr.toUpperCase() || '-';
            phaseEl.textContent = phaseText;

        }

    }

    // Cloud (IoT tunnel)
    updateCloudUiFromStatus(data);



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
        const opUptime = document.getElementById('operator-uptime');
        if (opUptime) opUptime.textContent = formatUptime(data.uptime);

    }

    updateLandingUi({
        mode: data.mode,
        modeStr: data.modeStr,
        phaseText,
        safetyOk: data.safetyOk,
        tCube: data.temps?.cube,
        power: data.power?.power,
        pressureCube: data.pressure?.cube,
        pumpSpeed: data.pump?.speedMlH,
        abv: data.hydrometer?.abv,
        waterIn: data.temps?.waterIn,
        waterOut: data.temps?.waterOut,
        voltage: data.power?.voltage
    });

}



function updateButtonStates() {

    const isIdle = currentMode === MODE_IDLE;

    // Buttons that start modes
    const btnRect = document.querySelector('button[onclick="startRectification()"]');
    const btnManual = document.querySelector('button[onclick="startManual()"]');
    const btnDist = document.querySelector('button[onclick="startDistillation()"]');
    const btnMashing = document.querySelector('button[onclick="startMashing()"]');
    const btnHold = document.querySelector('button[onclick="startHold()"]');

    const modeButtons = [
        { mode: MODE_RECT, button: btnRect },
        { mode: MODE_MANUAL, button: btnManual },
        { mode: MODE_DIST, button: btnDist },
        { mode: MODE_MASH, button: btnMashing },
        { mode: MODE_HOLD, button: btnHold }
    ];

    modeButtons.forEach(({ mode, button }) => {
        if (!button) return;

        if (!button.dataset.baseText) {
            button.dataset.baseText = button.textContent.trim();
        }

        const isCurrentMode = currentMode === mode;
        const shouldDisable = !isIdle && !isCurrentMode;

        button.disabled = shouldDisable;
        button.classList.toggle('btn-disabled', shouldDisable);
        button.classList.toggle('btn-active-mode', isCurrentMode);
        button.textContent = isCurrentMode
            ? `Running: ${button.dataset.baseText}`
            : button.dataset.baseText;
    });

    // Runtime controls
    const btnStop = document.querySelector('button[onclick="stopProcess()"]');
    const btnPause = document.querySelector('button[onclick="pauseProcess()"]');
    const btnResume = document.querySelector('button[onclick="resumeProcess()"]');

    if (btnStop) {
        btnStop.disabled = isIdle;
        btnStop.classList.toggle('btn-disabled', isIdle);
    }

    if (btnPause) {
        const disablePause = isIdle || currentPaused;
        btnPause.disabled = disablePause;
        btnPause.classList.toggle('btn-disabled', disablePause);
    }

    if (btnResume) {
        const disableResume = isIdle || !currentPaused;
        btnResume.disabled = disableResume;
        btnResume.classList.toggle('btn-disabled', disableResume);
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



function toggleTelegramFields() {

    const enabledEl = document.getElementById('telegram-enabled');

    const fields = document.getElementById('telegram-fields');

    if (!enabledEl || !fields) return;

    fields.style.display = enabledEl.checked ? 'block' : 'none';

}


async function loadTelegramSettings() {

    const statusEl = document.getElementById('telegram-config-state');

    const enabledEl = document.getElementById('telegram-enabled');

    const tokenEl = document.getElementById('telegram-token');

    const chatIdEl = document.getElementById('telegram-chat-id');

    if (!statusEl || !enabledEl || !tokenEl || !chatIdEl) return;

    try {

        const response = await fetch('/api/settings/telegram');

        if (!response.ok) {

            statusEl.textContent = 'Статус: ошибка загрузки';

            return;

        }

        const data = await response.json();

        enabledEl.checked = !!data.enabled;

        tokenEl.value = data.token || '';

        chatIdEl.value = data.chatId || '';

        toggleTelegramFields();

        if (data.configured) {

            statusEl.textContent = data.active
                ? 'Статус: настроено и активно'
                : 'Статус: настроено';

        } else {

            statusEl.textContent = 'Статус: не настроено';

        }

    } catch (error) {

        console.error('Telegram settings load error:', error);

        statusEl.textContent = 'Статус: ошибка сети';

    }

}


async function saveTelegramSettings() {

    const enabledEl = document.getElementById('telegram-enabled');

    const tokenEl = document.getElementById('telegram-token');

    const chatIdEl = document.getElementById('telegram-chat-id');

    const statusEl = document.getElementById('telegram-config-state');

    if (!enabledEl || !tokenEl || !chatIdEl || !statusEl) return;

    const enabled = !!enabledEl.checked;

    const token = (tokenEl.value || '').trim();

    const chatId = (chatIdEl.value || '').trim();

    if (enabled && (!token || !chatId)) {

        alert('Для включения Telegram укажите Bot Token и Chat ID');

        return;

    }

    try {

        const response = await fetch('/api/settings/telegram', {

            method: 'POST',

            headers: { 'Content-Type': 'application/json' },

            body: JSON.stringify({ enabled, token, chatId })

        });

        if (!response.ok) {

            const text = await response.text();

            statusEl.textContent = 'Статус: ошибка сохранения';

            addLog('✗ Telegram: ошибка сохранения: ' + text, 'error');

            alert('Ошибка сохранения Telegram настроек');

            return;

        }

        statusEl.textContent = enabled
            ? 'Статус: сохранено'
            : 'Статус: отключено';

        addLog('✓ Telegram настройки сохранены', 'success');

    } catch (error) {

        console.error('Telegram settings save error:', error);

        statusEl.textContent = 'Статус: ошибка сети';

        addLog('✗ Telegram: ошибка сети при сохранении', 'error');

    }

}


async function sendTelegramTest() {

    const statusEl = document.getElementById('telegram-config-state');

    if (!statusEl) return;

    try {

        const response = await fetch('/api/settings/telegram/test', {

            method: 'POST',

            headers: { 'Content-Type': 'application/json' },

            body: JSON.stringify({ message: 'Smart-Column S3: test from Web UI' })

        });

        if (!response.ok) {

            const text = await response.text();

            addLog('✗ Telegram: тест не отправлен: ' + text, 'error');

            statusEl.textContent = 'Статус: тест не отправлен';

            alert('Не удалось отправить тестовое сообщение');

            return;

        }

        statusEl.textContent = 'Статус: тест отправлен';

        addLog('✓ Telegram: тестовое сообщение отправлено', 'success');

    } catch (error) {

        console.error('Telegram test send error:', error);

        statusEl.textContent = 'Статус: ошибка сети';

        addLog('✗ Telegram: ошибка сети при отправке теста', 'error');

    }

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



// Перезагрузка контроллера

function rebootController() {

    if (!confirm('Перезагрузить контроллер ESP32?\n\nВсе текущие процессы будут остановлены!')) {

        return;

    }



    addLog('🔄 Отправка команды перезагрузки...', 'warning');



    fetch('/api/reboot', {

        method: 'POST'

    }).then(response => {

        if (response.ok) {

            addLog('✓ Контроллер перезагружается...', 'success');

            // Показать сообщение и попробовать переподключиться через 5 сек

            setTimeout(() => {

                addLog('📌 Попытка переподключения...', 'info');

                window.location.reload();

            }, 5000);

        } else {

            addLog('✗ Ошибка перезагрузки', 'error');

        }

    }).catch(err => {

        addLog('❌ Ошибка сети: ' + err.message, 'error');

    });

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



    addLog(`🌓 Тема изменена: ${theme}`, 'info');

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



        if (mlPerRevEl && data.pump && data.pump.mlPerRev !== undefined && data.pump.mlPerRev !== null) {

            // Теперь это input поле, устанавливаем value

            mlPerRevEl.value = data.pump.mlPerRev.toFixed(3);

            mlPerRevEl.placeholder = 'Загрузка...';

        } else if (mlPerRevEl) {

            mlPerRevEl.value = '';

            mlPerRevEl.placeholder = 'Загрузка...';

        }



        if (stepsPerRevEl && data.pump && data.pump.stepsPerRev !== undefined && data.pump.stepsPerRev !== null) {

            // Показываем общее количество шагов

            const totalSteps = (data.pump.stepsPerRev || 0) * (data.pump.microsteps || 1);

            stepsPerRevEl.value = totalSteps;

            stepsPerRevEl.placeholder = 'Загрузка...';

        } else if (stepsPerRevEl) {

            stepsPerRevEl.value = '';

            stepsPerRevEl.placeholder = 'Загрузка...';

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



        addLog('✔ Информация о версиях обновлена', 'success');

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



        addLog(`🗑️ Процесс ${id} удалён из истории`, 'info');

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

        addLog('✗ Ошибка экспорта', 'error');

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

        addLog(`✓ Сравнение ${processes.length} процессов`, 'info');

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

// ============================================================================

// Информация о пользователе

// ============================================================================



async function loadUserInfo() {

    try {

        const response = await fetch('/api/web/user', {

            credentials: 'same-origin' // Важно для отправки cookies/сессий

        });

        

        if (!response.ok) {

            const errorText = await response.text();

            console.error('Failed to load user info:', response.status, errorText);

            

            if (response.status === 401) {

                // Пользователь не авторизован - редирект на логин

                const usernameElement = document.getElementById('current-username');

                if (usernameElement) {

                    usernameElement.textContent = 'Не авторизован';

                }

                // Не редиректим автоматически, чтобы избежать зацикливания

                // Вместо этого показываем сообщение

                return;

            }

            throw new Error('Failed to load user info: ' + response.status);

        }

        

        const user = await response.json();

        const usernameElement = document.getElementById('current-username');

        if (usernameElement) {

            usernameElement.textContent = user.username || 'Неизвестно';

        }

    } catch (error) {

        console.error('Error loading user info:', error);

        const usernameElement = document.getElementById('current-username');

        if (usernameElement) {

            usernameElement.textContent = 'Ошибка загрузки';

        }

    }

}



// Показать/скрыть меню пользователя

function toggleUserMenu() {

    const menu = document.getElementById('user-menu');

    if (menu) {

        menu.style.display = menu.style.display === 'none' ? 'block' : 'none';

    }

}



// Закрыть меню при клике вне его

document.addEventListener('click', function(event) {

    const userInfo = document.getElementById('user-info');

    const menu = document.getElementById('user-menu');

    if (menu && userInfo && !userInfo.contains(event.target) && !menu.contains(event.target)) {

        menu.style.display = 'none';

    }

});



// Выход из системы

async function logout() {

    try {

        const response = await fetch('/api/web/user/logout');

        if (response.ok) {

            window.location.href = '/login';

        } else {

            alert('Ошибка выхода из системы');

        }

    } catch (error) {

        console.error('Error logging out:', error);

        alert('Ошибка выхода из системы');

    }

}



// Сменить аккаунт

async function switchAccount() {

    // Важно: просто перейти на /login недостаточно — login.php при активной сессии
    // сразу редиректит обратно на главную. Поэтому сначала выходим из текущего аккаунта.
    try {
        await fetch('/api/web/user/logout', { credentials: 'same-origin' });
    } catch (error) {
        console.warn('Switch account: logout request failed, redirecting to login anyway:', error);
    }

    window.location.href = '/login?switch=1';

}



// ============================================================================
// Облако: привязка устройства по ID + PIN (cloud-proxy кабинет)
// ============================================================================

async function loadDiscoveredDevices() {
    const container = document.getElementById('discovered-devices');
    if (!container) return;

    container.innerHTML = '<p class="info-text" style="margin: 0; color: var(--text-secondary);">Загрузка доступных устройств...</p>';

    try {
        const response = await fetch('/api/web/devices/discovered', { credentials: 'same-origin' });
        if (!response.ok) {
            const t = await response.text();
            container.innerHTML = `<p class="info-text" style="margin: 0; color: var(--text-secondary);">Ошибка: ${response.status}</p>`;
            console.error('Failed to load discovered devices:', response.status, t);
            return;
        }

        const data = await response.json();
        const devices = data.devices || [];

        if (!devices.length) {
            container.innerHTML = '<p class="info-text" style="margin: 0; color: var(--text-secondary);">Нет доступных устройств. Сгенерируйте PIN на устройстве и обновите.</p>';
            return;
        }

        const list = document.createElement('div');
        list.style.display = 'flex';
        list.style.flexDirection = 'column';
        list.style.gap = '8px';

        devices.forEach(d => {
            const row = document.createElement('div');
            row.style.display = 'flex';
            row.style.alignItems = 'center';
            row.style.justifyContent = 'space-between';
            row.style.gap = '10px';
            row.style.padding = '8px 10px';
            row.style.border = '1px solid var(--border-color)';
            row.style.borderRadius = '6px';
            row.style.background = 'var(--bg-primary)';

            const left = document.createElement('div');
            left.style.display = 'flex';
            left.style.flexDirection = 'column';

            const idLine = document.createElement('div');
            idLine.style.fontWeight = '600';
            idLine.textContent = d.deviceId;

            const meta = document.createElement('div');
            meta.style.fontSize = '0.85em';
            meta.style.color = 'var(--text-secondary)';
            const lastSeen = d.lastSeenAt ? new Date(d.lastSeenAt).toLocaleString('ru-RU') : '—';
            const expires = d.expiresAt ? new Date(d.expiresAt).toLocaleString('ru-RU') : '—';
            meta.textContent = `lastSeen: ${lastSeen} | expires: ${expires}`;

            left.appendChild(idLine);
            left.appendChild(meta);

            const btn = document.createElement('button');
            btn.className = 'btn btn-sm';
            btn.textContent = 'Выбрать';
            btn.onclick = () => {
                const idInput = document.getElementById('claim-device-id');
                const pinInput = document.getElementById('claim-device-pin');
                if (idInput) idInput.value = d.deviceId || '';
                if (pinInput) pinInput.focus();
            };

            row.appendChild(left);
            row.appendChild(btn);
            list.appendChild(row);
        });

        container.innerHTML = '';
        container.appendChild(list);
    } catch (e) {
        console.error('loadDiscoveredDevices error:', e);
        container.innerHTML = '<p class="info-text" style="margin: 0; color: var(--text-secondary);">Ошибка загрузки списка</p>';
    }
}

async function claimDeviceToAccount() {
    const idInput = document.getElementById('claim-device-id');
    const pinInput = document.getElementById('claim-device-pin');
    const deviceId = (idInput?.value || '').trim();
    const claimCode = (pinInput?.value || '').trim();

    if (!deviceId || !claimCode) {
        alert('Введите Device ID и PIN');
        return;
    }

    try {
        const response = await fetch('/api/web/devices/claim', {
            method: 'POST',
            credentials: 'same-origin',
            headers: { 'Content-Type': 'application/json; charset=utf-8' },
            body: JSON.stringify({ deviceId, claimCode })
        });

        const text = await response.text();
        let payload = null;
        try { payload = JSON.parse(text); } catch (_) {}

        if (!response.ok) {
            const msg = (payload && (payload.error || payload.message)) ? (payload.error || payload.message) : text;
            alert(`Ошибка привязки: ${msg}`);
            return;
        }

        alert('Устройство привязано и добавлено в аккаунт');
        if (pinInput) pinInput.value = '';
        await loadESP32Devices();
        // Если активное устройство обновилось — форма откроется сама (loadESP32Devices вызывает loadESP32Device).
        await loadDiscoveredDevices();
    } catch (e) {
        console.error('claimDeviceToAccount error:', e);
        alert('Ошибка сети при привязке');
    }
}


// ============================================================================

// Настройки ESP32 (поддержка нескольких устройств)

// ============================================================================



let currentDeviceId = null;

let devicesList = [];



// Загрузить список устройств

async function loadESP32Devices() {

    try {

        const response = await fetch('/api/web/esp32/devices', {

            credentials: 'same-origin' // Важно для отправки cookies/сессий

        });

        

        if (!response.ok) {

            const errorText = await response.text();

            console.error('Failed to load devices:', response.status, errorText);

            

            if (response.status === 401) {

                // Пользователь не авторизован

                const select = document.getElementById('esp32-device-select');

                if (select) {

                    select.innerHTML = '<option value="">Требуется авторизация</option>';

                }

                return;

            }

            throw new Error('Failed to load devices: ' + response.status);

        }

        

        const data = await response.json();

        devicesList = data.devices || [];

        

        const select = document.getElementById('esp32-device-select');

        if (!select) return;

        

        select.innerHTML = '<option value="">-- Выберите устройство --</option>';

        

        if (devicesList.length === 0) {

            // Нет устройств - это нормально, оставляем только кнопку "Добавить новое"

            return;

        }

        

        devicesList.forEach(device => {

            const option = document.createElement('option');

            option.value = device.id;

            const tunnelBadge = device.tunnelEnabled
                ? ` ☁️${device.tunnelStatus ? ' ' + device.tunnelStatus : ''}`
                : '';
            option.textContent = device.name + (device.is_active ? ' (активно)' : '') + tunnelBadge;

            select.appendChild(option);

        });

        

        // Если есть активное устройство, выбираем его

        const activeDevice = devicesList.find(d => d.is_active);

        if (activeDevice) {

            select.value = activeDevice.id;

            loadESP32Device();

        }

    } catch (error) {

        console.error('Error loading ESP32 devices:', error);

        const select = document.getElementById('esp32-device-select');

        if (select) {

            select.innerHTML = '<option value="">Ошибка загрузки</option>';

        }

    }

}



// Загрузить данные выбранного устройства

async function loadESP32Device() {

    const select = document.getElementById('esp32-device-select');

    if (!select || !select.value) {

        document.getElementById('esp32-device-form').style.display = 'none';

        return;

    }

    

    currentDeviceId = parseInt(select.value);

    const device = devicesList.find(d => d.id === currentDeviceId);

    

    if (device) {

        document.getElementById('esp32-device-name').value = device.name || '';

        document.getElementById('esp32-host').value = device.host || '';

        document.getElementById('esp32-port').value = device.port || 80;

        document.getElementById('esp32-use-https').checked = device.useHttps || false;

        document.getElementById('esp32-username').value = device.username || '';

        document.getElementById('esp32-password').value = '';

        document.getElementById('esp32-timeout').value = device.timeout || 5;

        document.getElementById('esp32-enabled').checked = true;

        

        toggleESP32Fields();

        document.getElementById('esp32-device-form').style.display = 'block';

        document.getElementById('esp32-activate-btn').style.display = device.is_active ? 'none' : 'inline-block';

        document.getElementById('esp32-delete-btn').style.display = 'inline-block';

    } else {

        // Загружаем с сервера если не найдено в списке

        try {

            const response = await fetch(`/api/web/esp32/devices/${currentDeviceId}`);

            if (!response.ok) {

                throw new Error('Failed to load device');

            }

            const data = await response.json();

            const loadedDevice = data.device;

            

            document.getElementById('esp32-device-name').value = loadedDevice.name || '';

            document.getElementById('esp32-host').value = loadedDevice.host || '';

            document.getElementById('esp32-port').value = loadedDevice.port || 80;

            document.getElementById('esp32-use-https').checked = loadedDevice.useHttps || false;

            document.getElementById('esp32-username').value = loadedDevice.username || '';

            document.getElementById('esp32-password').value = '';

            document.getElementById('esp32-timeout').value = loadedDevice.timeout || 5;

            document.getElementById('esp32-enabled').checked = true;

            

            toggleESP32Fields();

            document.getElementById('esp32-device-form').style.display = 'block';

            document.getElementById('esp32-activate-btn').style.display = loadedDevice.is_active ? 'none' : 'inline-block';

            document.getElementById('esp32-delete-btn').style.display = 'inline-block';

        } catch (error) {

            console.error('Error loading device:', error);

            alert('Ошибка загрузки устройства');

        }

    }

}



// Показать форму добавления нового устройства

function showAddDeviceForm() {

    currentDeviceId = null;

    document.getElementById('esp32-device-select').value = '';

    document.getElementById('esp32-device-name').value = '';

    document.getElementById('esp32-host').value = '';

    document.getElementById('esp32-port').value = '80';

    document.getElementById('esp32-use-https').checked = false;

    document.getElementById('esp32-username').value = '';

    document.getElementById('esp32-password').value = '';

    document.getElementById('esp32-timeout').value = '5';

    document.getElementById('esp32-enabled').checked = false;

    document.getElementById('esp32-device-form').style.display = 'block';

    document.getElementById('esp32-activate-btn').style.display = 'none';

    document.getElementById('esp32-delete-btn').style.display = 'none';

    toggleESP32Fields();

}



// Загрузить конфигурацию ESP32 (для обратной совместимости)

async function loadESP32Config() {

    await loadESP32Devices();

}



function toggleESP32Fields() {

    const enabled = document.getElementById('esp32-enabled').checked;

    const fields = document.getElementById('esp32-fields');

    if (fields) {

        fields.style.display = enabled ? 'block' : 'none';

    }

}



// Сохранить устройство ESP32

async function saveESP32Device() {

    const name = document.getElementById('esp32-device-name').value.trim();

    if (!name) {

        alert('Укажите название устройства');

        return;

    }

    

    const config = {

        name: name,

        enabled: document.getElementById('esp32-enabled').checked,

        host: document.getElementById('esp32-host').value.trim(),

        port: parseInt(document.getElementById('esp32-port').value) || 80,

        useHttps: document.getElementById('esp32-use-https').checked,

        username: document.getElementById('esp32-username').value.trim(),

        password: document.getElementById('esp32-password').value.trim(),

        timeout: parseInt(document.getElementById('esp32-timeout').value) || 5

    };

    

    // Валидация

    if (config.enabled && !config.host) {

        alert('Укажите адрес ESP32');

        return;

    }

    

    try {

        let response;

        if (currentDeviceId) {

            // Обновляем существующее устройство

            config.id = currentDeviceId;

            response = await fetch(`/api/web/esp32/devices/${currentDeviceId}`, {

                method: 'PUT',

                headers: {

                    'Content-Type': 'application/json'

                },

                body: JSON.stringify(config)

            });

        } else {

            // Создаем новое устройство

            config.is_active = true; // Делаем активным по умолчанию

            response = await fetch('/api/web/esp32/devices', {

                method: 'POST',

                headers: {

                    'Content-Type': 'application/json'

                },

                body: JSON.stringify(config)

            });

        }

        

        if (!response.ok) {

            const error = await response.json();

            throw new Error(error.error || 'Failed to save device');

        }

        

        const result = await response.json();

        alert('Устройство сохранено успешно!');

        

        // Обновляем список устройств

        await loadESP32Devices();

        

        // Если пароль был введен, очищаем поле

        if (config.password) {

            document.getElementById('esp32-password').value = '';

        }

    } catch (error) {

        console.error('Error saving ESP32 device:', error);

        alert('Ошибка сохранения устройства: ' + error.message);

    }

}



// Сохранить конфигурацию ESP32 (для обратной совместимости)

async function saveESP32Config() {

    await saveESP32Device();

}



// Активировать устройство

async function activateESP32Device() {

    if (!currentDeviceId) {

        alert('Выберите устройство');

        return;

    }

    

    try {

        const response = await fetch(`/api/web/esp32/devices/${currentDeviceId}/activate`, {

            method: 'POST'

        });

        

        if (!response.ok) {

            const error = await response.json();

            throw new Error(error.error || 'Failed to activate device');

        }

        

        alert('Устройство активировано!');

        await loadESP32Devices();

        loadESP32Device();

    } catch (error) {

        console.error('Error activating device:', error);

        alert('Ошибка активации устройства: ' + error.message);

    }

}



// Удалить устройство

async function deleteESP32Device() {

    if (!currentDeviceId) {

        alert('Выберите устройство');

        return;

    }

    

    if (!confirm('Удалить это устройство?')) {

        return;

    }

    

    try {

        const response = await fetch(`/api/web/esp32/devices/${currentDeviceId}`, {

            method: 'DELETE'

        });

        

        if (!response.ok) {

            const error = await response.json();

            throw new Error(error.error || 'Failed to delete device');

        }

        

        alert('Устройство удалено');

        currentDeviceId = null;

        document.getElementById('esp32-device-select').value = '';

        document.getElementById('esp32-device-form').style.display = 'none';

        await loadESP32Devices();

    } catch (error) {

        console.error('Error deleting device:', error);

        alert('Ошибка удаления устройства: ' + error.message);

    }

}



async function testESP32Connection() {

    const resultDiv = document.getElementById('esp32-test-result');

    if (!resultDiv) return;

    

    resultDiv.style.display = 'block';

    resultDiv.innerHTML = 'Проверка подключения...';

    resultDiv.style.background = 'var(--bg-secondary)';

    resultDiv.style.color = 'var(--text-primary)';

    

    try {

        const response = await fetch('/api/web/esp32/test', {

            method: 'POST',

            headers: {

                'Content-Type': 'application/json'

            },

            credentials: 'same-origin'

        });

        

        const result = await response.json();

        

        if (result.success) {

            resultDiv.style.background = 'rgba(40, 167, 69, 0.2)';

            resultDiv.style.color = '#28a745';

            resultDiv.style.border = '1px solid #28a745';

            resultDiv.innerHTML = '✓ ' + (result.message || 'Подключение успешно!');

        } else {

            resultDiv.style.background = 'rgba(220, 53, 69, 0.2)';

            resultDiv.style.color = '#dc3545';

            resultDiv.style.border = '1px solid #dc3545';

            resultDiv.innerHTML = '✗ ' + (result.error || 'Ошибка подключения');

        }

    } catch (error) {

        console.error('Error testing ESP32 connection:', error);

        resultDiv.style.background = 'rgba(220, 53, 69, 0.2)';

        resultDiv.style.color = '#dc3545';

        resultDiv.style.border = '1px solid #dc3545';

        resultDiv.innerHTML = '✗ Ошибка: ' + error.message;

    }

}
