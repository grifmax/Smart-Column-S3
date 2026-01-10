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
const MINI_CHART_MAX_POINTS = 60; // 5 РјРёРЅСѓС‚ РїСЂРё РѕР±РЅРѕРІР»РµРЅРёРё РєР°Р¶РґС‹Рµ 5 СЃРµРєСѓРЅРґ

// РЎРѕСЃС‚РѕСЏРЅРёРµ РїСЂРѕС†РµСЃСЃР°
let currentMode = 0;  // 0 = IDLE
let currentPaused = false;
let maxHeaterPower = 3000;  // Р‘СѓРґРµС‚ РѕР±РЅРѕРІР»РµРЅРѕ РёР· РЅР°СЃС‚СЂРѕРµРє

// РРЅРёС†РёР°Р»РёР·Р°С†РёСЏ РїСЂРё Р·Р°РіСЂСѓР·РєРµ СЃС‚СЂР°РЅРёС†С‹
document.addEventListener('DOMContentLoaded', function () {
    initTabs();
    loadTheme();
    loadDemoMode();  // Р—Р°РіСЂСѓР·РёС‚СЊ СЃРѕСЃС‚РѕСЏРЅРёРµ РґРµРјРѕ-СЂРµР¶РёРјР°
    initMiniChart();
    loadMemoryStatsPreference();
    loadPumpInfo();
    loadVersionInfo();
    loadUserInfo();  // Р—Р°РіСЂСѓР·РёС‚СЊ РёРЅС„РѕСЂРјР°С†РёСЋ Рѕ РїРѕР»СЊР·РѕРІР°С‚РµР»Рµ
    loadESP32Config();  // Р—Р°РіСЂСѓР·РёС‚СЊ РЅР°СЃС‚СЂРѕР№РєРё ESP32
    loadStatus();  // Р—Р°РіСЂСѓР·РёС‚СЊ РЅР°С‡Р°Р»СЊРЅС‹Р№ СЃС‚Р°С‚СѓСЃ
    connectWebSocket();

    // РџРµСЂРёРѕРґРёС‡РµСЃРєРёР№ РѕРїСЂРѕСЃ СЃС‚Р°С‚СѓСЃР° (СЂРµР·РµСЂРІРЅС‹Р№ РІР°СЂРёР°РЅС‚ РµСЃР»Рё WebSocket РѕС‚РєР»СЋС‡С‘РЅ)
    setInterval(loadStatus, 2000);
});

// ============================================================================
// WebSocket
// ============================================================================

function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.hostname}/ws`;

    addLog('РџРѕРґРєР»СЋС‡РµРЅРёРµ Рє WebSocket...');

    try {
        ws = new WebSocket(wsUrl);

        ws.onopen = function () {
            isConnected = true;
            updateConnectionStatus(true);
            addLog('вњ… РџРѕРґРєР»СЋС‡РµРЅРѕ Рє РєРѕРЅС‚СЂРѕР»Р»РµСЂСѓ', 'info');

            // РћСЃС‚Р°РЅРѕРІРёС‚СЊ РїРѕРїС‹С‚РєРё РїРµСЂРµРїРѕРґРєР»СЋС‡РµРЅРёСЏ
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
                console.error('РћС€РёР±РєР° РїР°СЂСЃРёРЅРіР° JSON:', e);
            }
        };

        ws.onerror = function (error) {
            console.error('WebSocket error:', error);
            addLog('вќЊ РћС€РёР±РєР° РїРѕРґРєР»СЋС‡РµРЅРёСЏ', 'error');
        };

        ws.onclose = function () {
            isConnected = false;
            updateConnectionStatus(false);
            addLog('вљ пёЏ РЎРѕРµРґРёРЅРµРЅРёРµ СЂР°Р·РѕСЂРІР°РЅРѕ. РџРµСЂРµРїРѕРґРєР»СЋС‡РµРЅРёРµ...', 'warning');

            // РџРѕРїС‹С‚РєР° РїРµСЂРµРїРѕРґРєР»СЋС‡РµРЅРёСЏ РєР°Р¶РґС‹Рµ 5 СЃРµРєСѓРЅРґ
            if (!reconnectInterval) {
                reconnectInterval = setInterval(() => {
                    if (!isConnected) {
                        connectWebSocket();
                    }
                }, 5000);
            }
        };
    } catch (e) {
        console.error('РћС€РёР±РєР° СЃРѕР·РґР°РЅРёСЏ WebSocket:', e);
        updateConnectionStatus(false);
    }
}

function sendCommand(action, param = '', value = 0) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        const cmd = { action, param, value };
        ws.send(JSON.stringify(cmd));
        addLog(`рџ“¤ РљРѕРјР°РЅРґР°: ${action} ${param} ${value}`);
    } else {
        addLog('вќЊ РќРµС‚ РїРѕРґРєР»СЋС‡РµРЅРёСЏ Рє РєРѕРЅС‚СЂРѕР»Р»РµСЂСѓ', 'error');
    }
}

function updateConnectionStatus(connected) {
    const statusDot = document.getElementById('connection-status');
    const statusText = document.getElementById('connection-text');

    if (connected) {
        statusDot.className = 'status-dot online';
        statusText.textContent = 'РџРѕРґРєР»СЋС‡РµРЅРѕ';
    } else {
        statusDot.className = 'status-dot offline';
        statusText.textContent = 'РћС‚РєР»СЋС‡РµРЅРѕ';
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
                name: 'РљСѓР±',
                data: []
            },
            {
                name: 'Р¦Р°СЂРіР° РІРµСЂС…',
                data: []
            },
            {
                name: 'Р”РµС„Р»РµРіРјР°С‚РѕСЂ',
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
                text: 'В°C'
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

    // Р”РѕР±Р°РІРёС‚СЊ РЅРѕРІС‹Рµ РґР°РЅРЅС‹Рµ
    if (data.t_cube !== undefined) {
        miniChartData.timestamps.push(now);
        miniChartData.cube.push(data.t_cube);
        miniChartData.columnTop.push(data.t_column_top || null);
        miniChartData.reflux.push(data.t_reflux || null);

        // РћРіСЂР°РЅРёС‡РёС‚СЊ РєРѕР»РёС‡РµСЃС‚РІРѕ С‚РѕС‡РµРє
        if (miniChartData.timestamps.length > MINI_CHART_MAX_POINTS) {
            miniChartData.timestamps.shift();
            miniChartData.cube.shift();
            miniChartData.columnTop.shift();
            miniChartData.reflux.shift();
        }

        // РћР±РЅРѕРІРёС‚СЊ РіСЂР°С„РёРє
        miniChart.updateSeries([
            {
                name: 'РљСѓР±',
                data: miniChartData.timestamps.map((t, i) => ({
                    x: t,
                    y: miniChartData.cube[i]
                }))
            },
            {
                name: 'Р¦Р°СЂРіР° РІРµСЂС…',
                data: miniChartData.timestamps.map((t, i) => ({
                    x: t,
                    y: miniChartData.columnTop[i]
                }))
            },
            {
                name: 'Р”РµС„Р»РµРіРјР°С‚РѕСЂ',
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
    // Р РµР¶РёРј
    if (data.mode !== undefined) {
        const modeNames = ['IDLE', 'RECT', 'MANUAL', 'DIST', 'MASH', 'HOLD'];
        const modeName = modeNames[data.mode] || 'UNKNOWN';
        const modeEl = document.getElementById('mode');
        modeEl.textContent = modeName;
        modeEl.className = `value mode-${modeName.toLowerCase()}`;
    }

    // Р¤Р°Р·Р°
    if (data.phase !== undefined) {
        const phaseNames = ['IDLE', 'HEATING', 'STABIL', 'HEADS', 'PURGE', 'BODY', 'TAILS', 'FINISH', 'ERROR'];
        document.getElementById('phase').textContent = phaseNames[data.phase] || 'вЂ”';
    }

    // РўРµРјРїРµСЂР°С‚СѓСЂС‹
    if (data.t_cube !== undefined) {
        document.getElementById('temp-cube').textContent = data.t_cube.toFixed(1) + 'В°C';
    }
    if (data.t_column_bottom !== undefined) {
        document.getElementById('temp-column-bottom').textContent = data.t_column_bottom.toFixed(1) + 'В°C';
    }
    if (data.t_column_top !== undefined) {
        document.getElementById('temp-column-top').textContent = data.t_column_top.toFixed(1) + 'В°C';
    }
    if (data.t_reflux !== undefined) {
        document.getElementById('temp-reflux').textContent = data.t_reflux.toFixed(1) + 'В°C';
    }
    if (data.t_tsa !== undefined) {
        document.getElementById('temp-tsa').textContent = data.t_tsa.toFixed(1) + 'В°C';
    }

    // Р”Р°РІР»РµРЅРёРµ
    if (data.p_cube !== undefined) {
        document.getElementById('pressure-cube').textContent = data.p_cube.toFixed(1) + ' РјРј СЂС‚.СЃС‚.';
    }
    if (data.p_atm !== undefined) {
        document.getElementById('pressure-atm').textContent = data.p_atm.toFixed(1) + ' РіРџР°';
    }
    if (data.p_flood !== undefined) {
        document.getElementById('pressure-flood').textContent = data.p_flood.toFixed(1) + ' РјРј';
    }

    // РњРѕС‰РЅРѕСЃС‚СЊ (PZEM-004T)
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
        document.getElementById('power-energy').textContent = data.energy.toFixed(3) + ' РєР’С‚В·С‡';
    }
    if (data.frequency !== undefined) {
        document.getElementById('power-frequency').textContent = data.frequency.toFixed(1) + ' Р“С†';
    }
    if (data.pf !== undefined) {
        document.getElementById('power-pf').textContent = data.pf.toFixed(2);
    }

    // РќР°СЃРѕСЃ
    if (data.pump_speed !== undefined) {
        document.getElementById('pump-speed').textContent = data.pump_speed.toFixed(0) + ' РјР»/С‡';
    }
    if (data.pump_volume !== undefined) {
        document.getElementById('pump-volume').textContent = data.pump_volume.toFixed(0) + ' РјР»';
    }

    // РћР±СЉС‘РјС‹ С„СЂР°РєС†РёР№
    if (data.volume_heads !== undefined) {
        document.getElementById('volume-heads').textContent = data.volume_heads.toFixed(0) + ' РјР»';
    }
    if (data.volume_body !== undefined) {
        document.getElementById('volume-body').textContent = data.volume_body.toFixed(0) + ' РјР»';
    }
    if (data.volume_tails !== undefined) {
        document.getElementById('volume-tails').textContent = data.volume_tails.toFixed(0) + ' РјР»';
    }

    // РђСЂРµРѕРјРµС‚СЂ
    if (data.abv !== undefined) {
        document.getElementById('abv').textContent = data.abv.toFixed(1) + '%';
    }

    // Uptime
    if (data.uptime !== undefined) {
        document.getElementById('uptime').textContent = formatUptime(data.uptime);
    }

    // РЎРѕР±С‹С‚РёСЏ
    if (data.type === 'event') {
        addLog(data.message, data.level || 'info');
    }

    // РћР±РЅРѕРІРёС‚СЊ РјРёРЅРё-РіСЂР°С„РёРє
    updateMiniChart(data);

    // РћР±РЅРѕРІРёС‚СЊ СЃС‚Р°С‚РёСЃС‚РёРєСѓ РїР°РјСЏС‚Рё
    if (data.memory !== undefined) {
        updateMemoryStats(data.memory);
    }

    // РћР±РЅРѕРІРёС‚СЊ Р°РЅРёРјР°С†РёСЋ РєРѕР»РѕРЅРЅС‹
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

            // РЈР±СЂР°С‚СЊ Р°РєС‚РёРІРЅС‹Р№ РєР»Р°СЃСЃ СЃРѕ РІСЃРµС… РІРєР»Р°РґРѕРє
            tabs.forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(content => {
                content.classList.remove('active');
            });

            // Р”РѕР±Р°РІРёС‚СЊ Р°РєС‚РёРІРЅС‹Р№ РєР»Р°СЃСЃ Рє РІС‹Р±СЂР°РЅРЅРѕР№ РІРєР»Р°РґРєРµ
            tab.classList.add('active');
            document.getElementById(targetId).classList.add('active');

            // Р—Р°РіСЂСѓР·РёС‚СЊ РёСЃС‚РѕСЂРёСЋ РїСЂРё РїРµСЂРµРєР»СЋС‡РµРЅРёРё РЅР° РІРєР»Р°РґРєСѓ "РСЃС‚РѕСЂРёСЏ"
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
        addLog('рџ“¤ РћС‚РїСЂР°РІРєР° РєРѕРјР°РЅРґС‹ Р·Р°РїСѓСЃРєР° Р°РІС‚Рѕ-СЂРµРєС‚РёС„РёРєР°С†РёРё...', 'info');

        const response = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mode: 'rectification' })
        });

        if (response.ok) {
            const data = await response.json();
            addLog('вњ… РђРІС‚Рѕ-СЂРµРєС‚РёС„РёРєР°С†РёСЏ Р·Р°РїСѓС‰РµРЅР°', 'success');
            if (data.warning) {
                addLog('вљ пёЏ ' + data.warning, 'warning');
            }
            setTimeout(loadStatus, 500); // РћР±РЅРѕРІРёС‚СЊ СЃС‚Р°С‚СѓСЃ
        } else {
            const error = await response.text();
            addLog('вќЊ РћС€РёР±РєР° (' + response.status + '): ' + error, 'error');
        }
    } catch (e) {
        addLog('вќЊ РћС€РёР±РєР° СЃРµС‚Рё: ' + e.message, 'error');
        console.error('Start rectification error:', e);
    }
}

function startManual() {
    // РџРµСЂРµС…РѕРґ РЅР° СЃС‚СЂР°РЅРёС†Сѓ СЂСѓС‡РЅРѕРіРѕ СѓРїСЂР°РІР»РµРЅРёСЏ
    window.location.href = 'manual.html';
}

async function startDistillation() {
    try {
        addLog('рџ“¤ РћС‚РїСЂР°РІРєР° РєРѕРјР°РЅРґС‹ Р·Р°РїСѓСЃРєР° РґРёСЃС‚РёР»Р»СЏС†РёРё...', 'info');

        const response = await fetch('/api/process/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mode: 'distillation' })
        });

        if (response.ok) {
            const data = await response.json();
            addLog('вњ… Р”РёСЃС‚РёР»Р»СЏС†РёСЏ Р·Р°РїСѓС‰РµРЅР°', 'success');
            if (data.warning) {
                addLog('вљ пёЏ ' + data.warning, 'warning');
            }
            setTimeout(loadStatus, 500); // РћР±РЅРѕРІРёС‚СЊ СЃС‚Р°С‚СѓСЃ
        } else {
            const error = await response.text();
            addLog('вќЊ РћС€РёР±РєР° (' + response.status + '): ' + error, 'error');
        }
    } catch (e) {
        addLog('вќЊ РћС€РёР±РєР° СЃРµС‚Рё: ' + e.message, 'error');
        console.error('Start distillation error:', e);
    }
}

async function stopProcess() {
    if (!confirm('РћСЃС‚Р°РЅРѕРІРёС‚СЊ РїСЂРѕС†РµСЃСЃ?')) return;

    try {
        const response = await fetch('/api/process/stop', {
            method: 'POST'
        });

        if (response.ok) {
            addLog('вњ… РџСЂРѕС†РµСЃСЃ РѕСЃС‚Р°РЅРѕРІР»РµРЅ', 'warning');
            setTimeout(loadStatus, 500); // РћР±РЅРѕРІРёС‚СЊ СЃС‚Р°С‚СѓСЃ
        } else {
            addLog('вќЊ РћС€РёР±РєР° РѕСЃС‚Р°РЅРѕРІРєРё', 'error');
        }
    } catch (e) {
        addLog('вќЊ РћС€РёР±РєР°: ' + e.message, 'error');
    }
}

async function pauseProcess() {
    try {
        const response = await fetch('/api/process/pause', {
            method: 'POST'
        });

        if (response.ok) {
            addLog('вњ… РџСЂРѕС†РµСЃСЃ РїСЂРёРѕСЃС‚Р°РЅРѕРІР»РµРЅ', 'info');
            setTimeout(loadStatus, 500); // РћР±РЅРѕРІРёС‚СЊ СЃС‚Р°С‚СѓСЃ
        } else {
            addLog('вќЊ РћС€РёР±РєР° РїР°СѓР·С‹', 'error');
        }
    } catch (e) {
        addLog('вќЊ РћС€РёР±РєР°: ' + e.message, 'error');
    }
}

async function resumeProcess() {
    try {
        const response = await fetch('/api/process/resume', {
            method: 'POST'
        });

        if (response.ok) {
            addLog('вњ… РџСЂРѕС†РµСЃСЃ РІРѕР·РѕР±РЅРѕРІР»РµРЅ', 'info');
            setTimeout(loadStatus, 500); // РћР±РЅРѕРІРёС‚СЊ СЃС‚Р°С‚СѓСЃ
        } else {
            addLog('вќЊ РћС€РёР±РєР° РІРѕР·РѕР±РЅРѕРІР»РµРЅРёСЏ', 'error');
        }
    } catch (e) {
        addLog('вќЊ РћС€РёР±РєР°: ' + e.message, 'error');
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
    addLog(`рџ”„ РџРµСЂРµРєР»СЋС‡РµРЅРёРµ РєР»Р°РїР°РЅР°: ${name}`);
}

// ============================================================================
// Р—Р°РіСЂСѓР·РєР° СЃС‚Р°С‚СѓСЃР° Рё РѕР±РЅРѕРІР»РµРЅРёРµ РєРЅРѕРїРѕРє
// ============================================================================

async function loadStatus() {
    try {
        const response = await fetch('/api/status');
        if (!response.ok) return;

        const data = await response.json();

        // РћР±РЅРѕРІРёС‚СЊ СЃРѕСЃС‚РѕСЏРЅРёРµ РїСЂРѕС†РµСЃСЃР°
        currentMode = data.mode || 0;
        currentPaused = data.paused || false;

        // РЎРѕС…СЂР°РЅРёС‚СЊ РјРѕС‰РЅРѕСЃС‚СЊ РўР­РќР° РёР· РЅР°СЃС‚СЂРѕРµРє
        if (data.equipment && data.equipment.heaterPowerW) {
            maxHeaterPower = data.equipment.heaterPowerW;
            updateHeaterSlider();
        }

        // РћР±РЅРѕРІРёС‚СЊ UI СЃ РЅРѕРІС‹Рј С„РѕСЂРјР°С‚РѕРј РґР°РЅРЅС‹С…
        updateUIFromStatus(data);

        // РћР±РЅРѕРІРёС‚СЊ СЃРѕСЃС‚РѕСЏРЅРёРµ РєРЅРѕРїРѕРє
        updateButtonStates();

    } catch (e) {
        console.error('РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё СЃС‚Р°С‚СѓСЃР°:', e);
    }
}

function updateUIFromStatus(data) {
    // Р РµР¶РёРј
    if (data.modeStr !== undefined) {
        const modeEl = document.getElementById('mode');
        if (modeEl) {
            modeEl.textContent = data.modeStr.toUpperCase();
            modeEl.className = `value mode-${data.modeStr}`;
        }
    }

    // Р¤Р°Р·Р°
    if (data.phaseStr !== undefined) {
        const phaseEl = document.getElementById('phase');
        if (phaseEl) {
            phaseEl.textContent = data.phaseStr.toUpperCase() || 'вЂ”';
        }
    }

    // РўРµРјРїРµСЂР°С‚СѓСЂС‹
    if (data.temps) {
        if (data.temps.cube !== undefined) {
            const el = document.getElementById('temp-cube');
            if (el) el.textContent = data.temps.cube.toFixed(1) + 'В°C';
        }
        if (data.temps.columnBottom !== undefined) {
            const el = document.getElementById('temp-column-bottom');
            if (el) el.textContent = data.temps.columnBottom.toFixed(1) + 'В°C';
        }
        if (data.temps.columnTop !== undefined) {
            const el = document.getElementById('temp-column-top');
            if (el) el.textContent = data.temps.columnTop.toFixed(1) + 'В°C';
        }
        if (data.temps.reflux !== undefined) {
            const el = document.getElementById('temp-reflux');
            if (el) el.textContent = data.temps.reflux.toFixed(1) + 'В°C';
        }
        if (data.temps.tsa !== undefined) {
            const el = document.getElementById('temp-tsa');
            if (el) el.textContent = data.temps.tsa.toFixed(1) + 'В°C';
        }
    }

    // Р”Р°РІР»РµРЅРёРµ
    if (data.pressure) {
        if (data.pressure.cube !== undefined) {
            const el = document.getElementById('pressure-cube');
            if (el) el.textContent = data.pressure.cube.toFixed(1) + ' РјРј СЂС‚.СЃС‚.';
        }
        if (data.pressure.atm !== undefined) {
            const el = document.getElementById('pressure-atm');
            if (el) el.textContent = data.pressure.atm.toFixed(1) + ' РіРџР°';
        }
    }

    // РњРѕС‰РЅРѕСЃС‚СЊ
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
            if (el) el.textContent = data.power.energy.toFixed(3) + ' РєР’С‚В·С‡';
        }
        if (data.power.frequency !== undefined) {
            const el = document.getElementById('power-frequency');
            if (el) el.textContent = data.power.frequency.toFixed(1) + ' Р“С†';
        }
        if (data.power.pf !== undefined) {
            const el = document.getElementById('power-pf');
            if (el) el.textContent = data.power.pf.toFixed(2);
        }
    }

    // РќР°СЃРѕСЃ
    if (data.pump) {
        if (data.pump.speedMlH !== undefined) {
            const el = document.getElementById('pump-speed');
            if (el) el.textContent = data.pump.speedMlH.toFixed(0) + ' РјР»/С‡';
        }
        if (data.pump.totalMl !== undefined) {
            const el = document.getElementById('pump-volume');
            if (el) el.textContent = data.pump.totalMl.toFixed(0) + ' РјР»';
        }
    }

    // РћР±СЉС‘РјС‹ С„СЂР°РєС†РёР№
    if (data.volumes) {
        if (data.volumes.heads !== undefined) {
            const el = document.getElementById('volume-heads');
            if (el) el.textContent = data.volumes.heads.toFixed(0) + ' РјР»';
        }
        if (data.volumes.body !== undefined) {
            const el = document.getElementById('volume-body');
            if (el) el.textContent = data.volumes.body.toFixed(0) + ' РјР»';
        }
        if (data.volumes.tails !== undefined) {
            const el = document.getElementById('volume-tails');
            if (el) el.textContent = data.volumes.tails.toFixed(0) + ' РјР»';
        }
    }

    // РђСЂРµРѕРјРµС‚СЂ
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

    // РљРЅРѕРїРєРё Р·Р°РїСѓСЃРєР° СЂРµР¶РёРјРѕРІ
    const btnRect = document.querySelector('button[onclick="startRectification()"]');
    const btnManual = document.querySelector('button[onclick="startManual()"]');
    const btnDist = document.querySelector('button[onclick="startDistillation()"]');

    // РљРЅРѕРїРєРё СѓРїСЂР°РІР»РµРЅРёСЏ
    const btnStop = document.querySelector('button[onclick="stopProcess()"]');
    const btnPause = document.querySelector('button[onclick="pauseProcess()"]');
    const btnResume = document.querySelector('button[onclick="resumeProcess()"]');

    // РќР°СЃС‚СЂРѕР№РєР° СЃРѕСЃС‚РѕСЏРЅРёР№
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
        slider.step = 50;  // РЁР°Рі 50 Р’С‚
    }

    if (label) {
        label.innerHTML = `РњРѕС‰РЅРѕСЃС‚СЊ РЅР°РіСЂРµРІР°: <span id="heater-value">0</span> Р’С‚ (РјР°РєСЃ ${maxHeaterPower})`;
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
        addLog('рџ’ѕ WiFi РЅР°СЃС‚СЂРѕР№РєРё СЃРѕС…СЂР°РЅРµРЅС‹', 'info');
        alert('WiFi РЅР°СЃС‚СЂРѕР№РєРё СЃРѕС…СЂР°РЅРµРЅС‹. РџРµСЂРµР·Р°РіСЂСѓР·РёС‚Рµ РєРѕРЅС‚СЂРѕР»Р»РµСЂ.');
    }
}

async function saveEquipment() {
    const heaterPower = document.getElementById('heater-power-w').value;
    const columnHeight = document.getElementById('column-height').value;
    const mlPerRev = parseFloat(document.getElementById('pump-ml-per-rev').value);
    const stepsPerRev = parseInt(document.getElementById('pump-steps-per-rev').value);

    // РџСЂРѕРІРµСЂРєР° Рё СЃРѕС…СЂР°РЅРµРЅРёРµ РїР°СЂР°РјРµС‚СЂРѕРІ РЅР°СЃРѕСЃР°
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
                let msg = 'вњ“ РџР°СЂР°РјРµС‚СЂС‹ РЅР°СЃРѕСЃР° СЃРѕС…СЂР°РЅРµРЅС‹:';
                if (pumpData.mlPerRev) msg += ' ' + pumpData.mlPerRev.toFixed(3) + ' РјР»/РѕР±';
                if (pumpData.stepsPerRev) msg += ', ' + pumpData.stepsPerRev + ' С€Р°РіРѕРІ/РѕР±';
                addLog(msg, 'success');
            } else {
                addLog('вњ— РћС€РёР±РєР° СЃРѕС…СЂР°РЅРµРЅРёСЏ РїР°СЂР°РјРµС‚СЂРѕРІ РЅР°СЃРѕСЃР°', 'error');
            }
        } catch (error) {
            addLog('вњ— РћС€РёР±РєР° СЃРѕРµРґРёРЅРµРЅРёСЏ РїСЂРё СЃРѕС…СЂР°РЅРµРЅРёРё РЅР°СЃРѕСЃР°', 'error');
        }
    }

    // РЎРѕС…СЂР°РЅРµРЅРёРµ РґСЂСѓРіРёС… РїР°СЂР°РјРµС‚СЂРѕРІ РѕР±РѕСЂСѓРґРѕРІР°РЅРёСЏ (С‡РµСЂРµР· WebSocket)
    sendCommand('equipment', 'save', 0);
    addLog('рџ’ѕ РќР°СЃС‚СЂРѕР№РєРё РѕР±РѕСЂСѓРґРѕРІР°РЅРёСЏ СЃРѕС…СЂР°РЅРµРЅС‹', 'info');
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
        alert('РЈРєР°Р¶РёС‚Рµ Р°РґСЂРµСЃ MQTT СЃРµСЂРІРµСЂР°');
        return;
    }

    sendCommand('mqtt', 'save', 0);
    addLog('рџ’ѕ MQTT РЅР°СЃС‚СЂРѕР№РєРё СЃРѕС…СЂР°РЅРµРЅС‹', 'info');
    alert('MQTT РЅР°СЃС‚СЂРѕР№РєРё СЃРѕС…СЂР°РЅРµРЅС‹. РџРµСЂРµР·Р°РіСЂСѓР·РёС‚Рµ РєРѕРЅС‚СЂРѕР»Р»РµСЂ.');
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
        alert('РЈРєР°Р¶РёС‚Рµ РёРјСЏ РїРѕР»СЊР·РѕРІР°С‚РµР»СЏ Рё РїР°СЂРѕР»СЊ');
        return;
    }

    sendCommand('security', 'save', 0);
    addLog('рџ’ѕ РќР°СЃС‚СЂРѕР№РєРё Р±РµР·РѕРїР°СЃРЅРѕСЃС‚Рё СЃРѕС…СЂР°РЅРµРЅС‹', 'info');
    alert('РќР°СЃС‚СЂРѕР№РєРё Р±РµР·РѕРїР°СЃРЅРѕСЃС‚Рё СЃРѕС…СЂР°РЅРµРЅС‹. РџРµСЂРµР·Р°РіСЂСѓР·РёС‚Рµ РєРѕРЅС‚СЂРѕР»Р»РµСЂ.');
}

function toggleDemoMode() {
    const enabled = document.getElementById('demo-mode-enabled').checked;

    // РЎРѕС…СЂР°РЅРёС‚СЊ РІ localStorage
    localStorage.setItem('demoMode', enabled ? 'true' : 'false');

    // РћС‚РїСЂР°РІРёС‚СЊ РЅР° СЃРµСЂРІРµСЂ
    fetch('/api/settings/demo', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ enabled: enabled })
    }).then(response => {
        if (response.ok) {
            addLog(enabled ? 'рџ§Є Р”РµРјРѕ-СЂРµР¶РёРј Р’РљР›Р®Р§РЃРќ' : 'вњ… Р”РµРјРѕ-СЂРµР¶РёРј РѕС‚РєР»СЋС‡С‘РЅ', 'info');
        } else {
            addLog('вљ пёЏ РћС€РёР±РєР° СЃРѕС…СЂР°РЅРµРЅРёСЏ РґРµРјРѕ-СЂРµР¶РёРјР° РЅР° СЃРµСЂРІРµСЂ', 'warning');
        }
    }).catch(err => {
        addLog('вљ пёЏ Р”РµРјРѕ-СЂРµР¶РёРј СЃРѕС…СЂР°РЅС‘РЅ Р»РѕРєР°Р»СЊРЅРѕ (СЃРµСЂРІРµСЂ РЅРµРґРѕСЃС‚СѓРїРµРЅ)', 'warning');
    });
}

function loadDemoMode() {
    const saved = localStorage.getItem('demoMode');
    const checkbox = document.getElementById('demo-mode-enabled');
    if (checkbox && saved === 'true') {
        checkbox.checked = true;
    }
}

// РџРµСЂРµР·Р°РіСЂСѓР·РєР° РєРѕРЅС‚СЂРѕР»Р»РµСЂР°
function rebootController() {
    if (!confirm('РџРµСЂРµР·Р°РіСЂСѓР·РёС‚СЊ РєРѕРЅС‚СЂРѕР»Р»РµСЂ ESP32?\n\nР’СЃРµ С‚РµРєСѓС‰РёРµ РїСЂРѕС†РµСЃСЃС‹ Р±СѓРґСѓС‚ РѕСЃС‚Р°РЅРѕРІР»РµРЅС‹!')) {
        return;
    }

    addLog('рџ”„ РћС‚РїСЂР°РІРєР° РєРѕРјР°РЅРґС‹ РїРµСЂРµР·Р°РіСЂСѓР·РєРё...', 'warning');

    fetch('/api/reboot', {
        method: 'POST'
    }).then(response => {
        if (response.ok) {
            addLog('вњ… РљРѕРЅС‚СЂРѕР»Р»РµСЂ РїРµСЂРµР·Р°РіСЂСѓР¶Р°РµС‚СЃСЏ...', 'success');
            // РџРѕРєР°Р·Р°С‚СЊ СЃРѕРѕР±С‰РµРЅРёРµ Рё РїРѕРїСЂРѕР±РѕРІР°С‚СЊ РїРµСЂРµРїРѕРґРєР»СЋС‡РёС‚СЊСЃСЏ С‡РµСЂРµР· 5 СЃРµРє
            setTimeout(() => {
                addLog('рџ”Њ РџРѕРїС‹С‚РєР° РїРµСЂРµРїРѕРґРєР»СЋС‡РµРЅРёСЏ...', 'info');
                window.location.reload();
            }, 5000);
        } else {
            addLog('вќЊ РћС€РёР±РєР° РїРµСЂРµР·Р°РіСЂСѓР·РєРё', 'error');
        }
    }).catch(err => {
        addLog('вќЊ РћС€РёР±РєР° СЃРµС‚Рё: ' + err.message, 'error');
    });
}

function setTheme(theme) {
    document.body.setAttribute('data-theme', theme);
    localStorage.setItem('theme', theme);

    // РћР±РЅРѕРІРёС‚СЊ С‚РµРјСѓ РјРёРЅРё-РіСЂР°С„РёРєР°
    if (miniChart) {
        miniChart.updateOptions({
            theme: {
                mode: theme
            }
        });
    }

    addLog(`рџЋЁ РўРµРјР° РёР·РјРµРЅРµРЅР°: ${theme}`, 'info');
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

    // РђРІС‚РѕСЃРєСЂРѕР»Р» РІРЅРёР·
    logContainer.scrollTop = logContainer.scrollHeight;

    // РћРіСЂР°РЅРёС‡РёС‚СЊ РєРѕР»РёС‡РµСЃС‚РІРѕ Р·Р°РїРёСЃРµР№ (РїРѕСЃР»РµРґРЅРёРµ 100)
    const entries = logContainer.querySelectorAll('.log-entry');
    if (entries.length > 100) {
        entries[0].remove();
    }
}

function clearLogs() {
    if (confirm('РћС‡РёСЃС‚РёС‚СЊ Р»РѕРіРё?')) {
        document.getElementById('log-container').innerHTML = '';
        addLog('Р›РѕРіРё РѕС‡РёС‰РµРЅС‹', 'info');
    }
}

function downloadLogs() {
    addLog('рџ“Ґ Р—Р°РїСЂРѕСЃ СЌРєСЃРїРѕСЂС‚Р° Р»РѕРіРѕРІ...', 'info');
    window.open('/api/export', '_blank');
}

// ============================================================================
// Memory Statistics
// ============================================================================

function updateMemoryStats(mem) {
    const memStatsDiv = document.getElementById('memory-stats');
    if (memStatsDiv.style.display === 'none') return;

    // Р¤РѕСЂРјР°С‚РёСЂРѕРІР°РЅРёРµ Р±Р°Р№С‚РѕРІ РІ KB/MB
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
// Р—Р°РіСЂСѓР·РєР° РёРЅС„РѕСЂРјР°С†РёРё Рѕ РЅР°СЃРѕСЃРµ
// ============================================================================

async function loadPumpInfo() {
    try {
        const response = await fetch('/api/calibration');
        if (!response.ok) {
            throw new Error('Failed to load calibration data');
        }

        const data = await response.json();

        // РћР±РЅРѕРІРёС‚СЊ РёРЅС„РѕСЂРјР°С†РёСЋ Рѕ РЅР°СЃРѕСЃРµ
        const mlPerRevEl = document.getElementById('pump-ml-per-rev');
        const stepsPerRevEl = document.getElementById('pump-steps-per-rev');

        if (mlPerRevEl && data.pump) {
            // РўРµРїРµСЂСЊ СЌС‚Рѕ input РїРѕР»Рµ, СѓСЃС‚Р°РЅР°РІР»РёРІР°РµРј value
            mlPerRevEl.value = data.pump.mlPerRev.toFixed(3);
        }

        if (stepsPerRevEl && data.pump) {
            // РџРѕРєР°Р·С‹РІР°РµРј РѕР±С‰РµРµ РєРѕР»РёС‡РµСЃС‚РІРѕ С€Р°РіРѕРІ
            const totalSteps = data.pump.stepsPerRev * data.pump.microsteps;
            stepsPerRevEl.value = totalSteps;
        }
    } catch (error) {
        console.error('Error loading pump info:', error);
        const mlPerRevEl = document.getElementById('pump-ml-per-rev');
        const stepsPerRevEl = document.getElementById('pump-steps-per-rev');

        if (mlPerRevEl) mlPerRevEl.placeholder = 'РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё';
        if (stepsPerRevEl) stepsPerRevEl.placeholder = 'РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё';
    }
}

// Р—Р°РіСЂСѓР·РєР° РёРЅС„РѕСЂРјР°С†РёРё Рѕ РІРµСЂСЃРёСЏС…
async function loadVersionInfo() {
    try {
        const response = await fetch('/api/version');
        if (!response.ok) {
            throw new Error('Failed to load version info');
        }

        const data = await response.json();

        // РћР±РЅРѕРІРёС‚СЊ РёРЅС„РѕСЂРјР°С†РёСЋ Рѕ РїСЂРѕС€РёРІРєРµ
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

        // РћР±РЅРѕРІРёС‚СЊ РёРЅС„РѕСЂРјР°С†РёСЋ Рѕ С„СЂРѕРЅС‚РµРЅРґРµ
        if (data.frontend) {
            document.getElementById('frontend-build-date').textContent =
                data.frontend.buildDate || data.frontend.note || 'Unknown';
            document.getElementById('frontend-build-time').textContent =
                data.frontend.buildTime || '-';
        }

        addLog('вњ“ РРЅС„РѕСЂРјР°С†РёСЏ Рѕ РІРµСЂСЃРёСЏС… РѕР±РЅРѕРІР»РµРЅР°', 'success');
    } catch (error) {
        console.error('Error loading version info:', error);
        document.getElementById('firmware-version').textContent = 'РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё';
        document.getElementById('frontend-build-date').textContent = 'РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё';
        addLog('вњ— РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё РІРµСЂСЃРёР№', 'error');
    }
}

// ============================================================================
// РСЃС‚РѕСЂРёСЏ РїСЂРѕС†РµСЃСЃРѕРІ
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

        // РџСЂРёРјРµРЅРёС‚СЊ С„РёР»СЊС‚СЂС‹
        applyHistoryFilters();

        addLog(`рџ“љ Р—Р°РіСЂСѓР¶РµРЅРѕ РїСЂРѕС†РµСЃСЃРѕРІ: ${historyData.length}`, 'info');
    } catch (error) {
        console.error('Error loading history:', error);
        addLog('вќЊ РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё РёСЃС‚РѕСЂРёРё', 'error');

        // РџРѕРєР°Р·Р°С‚СЊ РїСѓСЃС‚РѕР№ СЃРїРёСЃРѕРє
        const historyListEl = document.getElementById('history-list');
        if (historyListEl) {
            historyListEl.innerHTML = '<div style="text-align: center; padding: 20px; color: var(--text-secondary);">РќРµС‚ РґР°РЅРЅС‹С… РёР»Рё РѕС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё</div>';
        }
    }
}

function applyHistoryFilters() {
    const typeFilter = document.getElementById('history-filter-type')?.value || 'all';
    const sortBy = document.getElementById('history-sort')?.value || 'date-desc';

    let filtered = [...historyData];

    // Р¤РёР»СЊС‚СЂ РїРѕ С‚РёРїСѓ
    if (typeFilter !== 'all') {
        filtered = filtered.filter(p => p.type === typeFilter);
    }

    // РЎРѕСЂС‚РёСЂРѕРІРєР°
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

    // РћС‚СЂРёСЃРѕРІР°С‚СЊ СЃРїРёСЃРѕРє
    renderHistoryList(filtered);

    // РћР±РЅРѕРІРёС‚СЊ СЃС‚Р°С‚РёСЃС‚РёРєСѓ
    updateHistoryStats(filtered);
}

function renderHistoryList(processes) {
    const historyListEl = document.getElementById('history-list');
    if (!historyListEl) return;

    if (processes.length === 0) {
        historyListEl.innerHTML = '<div style="text-align: center; padding: 20px; color: var(--text-secondary);">РќРµС‚ РїСЂРѕС†РµСЃСЃРѕРІ РґР»СЏ РѕС‚РѕР±СЂР°Р¶РµРЅРёСЏ</div>';
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
        rectification: 'Р РµРєС‚РёС„РёРєР°С†РёСЏ',
        distillation: 'Р”РёСЃС‚РёР»Р»СЏС†РёСЏ',
        mashing: 'Р—Р°С‚РёСЂРєР°',
        hold: 'Р’С‹РґРµСЂР¶РєР°'
    };

    const statusNames = {
        completed: 'Р—Р°РІРµСЂС€РµРЅ',
        stopped: 'РћСЃС‚Р°РЅРѕРІР»РµРЅ',
        error: 'РћС€РёР±РєР°'
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
                <span class="metric-label">вЏ±пёЏ Р”Р»РёС‚РµР»СЊРЅРѕСЃС‚СЊ:</span>
                <span class="metric-value">${durationHours} С‡</span>
            </div>
            <div class="history-metric">
                <span class="metric-label">рџ’§ РћР±СЉС‘Рј:</span>
                <span class="metric-value">${process.totalVolume || 0} РјР»</span>
            </div>
        </div>
        <div class="history-actions">
            <button class="btn-secondary" onclick="viewHistoryDetails('${process.id}')">рџ‘ЃпёЏ РџРѕРґСЂРѕР±РЅРѕ</button>
            <button class="btn-secondary" onclick="exportHistory('${process.id}')">рџ“Ґ Р­РєСЃРїРѕСЂС‚</button>
            <button class="btn-danger" onclick="deleteHistoryItem('${process.id}')">рџ—‘пёЏ РЈРґР°Р»РёС‚СЊ</button>
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
        compareBtn.textContent = `рџ“Љ РЎСЂР°РІРЅРёС‚СЊ РІС‹Р±СЂР°РЅРЅС‹Рµ (${selectedProcesses.size})`;
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
    const totalEnergy = 0; // Р‘СѓРґРµС‚ СЂРµР°Р»РёР·РѕРІР°РЅРѕ РїРѕР·Р¶Рµ, РєРѕРіРґР° РїРѕСЏРІРёС‚СЃСЏ РїРѕР»Рµ energy РІ РїСЂРѕС†РµСЃСЃР°С…

    totalEl.textContent = total;
    completedEl.textContent = completed;
    timeEl.textContent = (totalTime / 3600).toFixed(1) + ' С‡';
    energyEl.textContent = totalEnergy.toFixed(1) + ' РєР’С‚В·С‡';
}

async function clearHistory() {
    if (!confirm('РЈРґР°Р»РёС‚СЊ Р’РЎР® РёСЃС‚РѕСЂРёСЋ РїСЂРѕС†РµСЃСЃРѕРІ? Р­С‚Рѕ РґРµР№СЃС‚РІРёРµ РЅРµРѕР±СЂР°С‚РёРјРѕ!')) {
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

        addLog('рџ—‘пёЏ РСЃС‚РѕСЂРёСЏ РїРѕР»РЅРѕСЃС‚СЊСЋ РѕС‡РёС‰РµРЅР°', 'info');
        await loadHistoryList();
    } catch (error) {
        console.error('Error clearing history:', error);
        addLog('вќЊ РћС€РёР±РєР° РїСЂРё РѕС‡РёСЃС‚РєРµ РёСЃС‚РѕСЂРёРё', 'error');
        alert('РћС€РёР±РєР° РїСЂРё РѕС‡РёСЃС‚РєРµ РёСЃС‚РѕСЂРёРё');
    }
}

async function deleteHistoryItem(id) {
    if (!confirm('РЈРґР°Р»РёС‚СЊ СЌС‚РѕС‚ РїСЂРѕС†РµСЃСЃ РёР· РёСЃС‚РѕСЂРёРё?')) {
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

        addLog(`рџ—‘пёЏ РџСЂРѕС†РµСЃСЃ ${id} СѓРґР°Р»РµРЅ РёР· РёСЃС‚РѕСЂРёРё`, 'info');
        await loadHistoryList();
    } catch (error) {
        console.error('Error deleting history item:', error);
        addLog('вќЊ РћС€РёР±РєР° РїСЂРё СѓРґР°Р»РµРЅРёРё РїСЂРѕС†РµСЃСЃР°', 'error');
        alert('РћС€РёР±РєР° РїСЂРё СѓРґР°Р»РµРЅРёРё РїСЂРѕС†РµСЃСЃР°');
    }
}

async function viewHistoryDetails(id) {
    try {
        const response = await fetch(`/api/history/${id}`);
        if (!response.ok) {
            throw new Error('Failed to load history details');
        }

        const process = await response.json();

        // РЎРѕР·РґР°С‚СЊ РјРѕРґР°Р»СЊРЅРѕРµ РѕРєРЅРѕ СЃ РґРµС‚Р°Р»СЏРјРё
        showHistoryDetailsModal(process);

        addLog(`рџ‘ЃпёЏ РџСЂРѕСЃРјРѕС‚СЂ РїСЂРѕС†РµСЃСЃР° ${id}`, 'info');
    } catch (error) {
        console.error('Error loading history details:', error);
        addLog('вќЊ РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё РґРµС‚Р°Р»РµР№ РїСЂРѕС†РµСЃСЃР°', 'error');
        alert('РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё РґРµС‚Р°Р»РµР№ РїСЂРѕС†РµСЃСЃР°');
    }
}

let tempChart = null;
let powerChart = null;

function showHistoryDetailsModal(process) {
    const typeNames = {
        rectification: 'Р РµРєС‚РёС„РёРєР°С†РёСЏ',
        distillation: 'Р”РёСЃС‚РёР»Р»СЏС†РёСЏ',
        mashing: 'Р—Р°С‚РёСЂРєР°',
        hold: 'Р’С‹РґРµСЂР¶РєР°'
    };

    const startDate = new Date(process.metadata.startTime * 1000);
    const endDate = new Date(process.metadata.endTime * 1000);
    const typeName = typeNames[process.process.type] || process.process.type;

    // РЈСЃС‚Р°РЅРѕРІРёС‚СЊ Р·Р°РіРѕР»РѕРІРѕРє
    document.getElementById('modal-title').textContent = `${typeName} - ${startDate.toLocaleDateString('ru-RU')}`;

    // Р—Р°РїРѕР»РЅРёС‚СЊ РѕСЃРЅРѕРІРЅСѓСЋ РёРЅС„РѕСЂРјР°С†РёСЋ
    const infoGrid = document.getElementById('modal-info-grid');
    infoGrid.innerHTML = `
        <div class="modal-info-item">
            <div class="modal-info-label">РўРёРї РїСЂРѕС†РµСЃСЃР°</div>
            <div class="modal-info-value">${typeName}</div>
        </div>
        <div class="modal-info-item">
            <div class="modal-info-label">Р РµР¶РёРј</div>
            <div class="modal-info-value">${process.process.mode === 'auto' ? 'РђРІС‚Рѕ' : 'Р СѓС‡РЅРѕР№'}</div>
        </div>
        <div class="modal-info-item">
            <div class="modal-info-label">РќР°С‡Р°Р»Рѕ</div>
            <div class="modal-info-value">${startDate.toLocaleString('ru-RU')}</div>
        </div>
        <div class="modal-info-item">
            <div class="modal-info-label">РћРєРѕРЅС‡Р°РЅРёРµ</div>
            <div class="modal-info-value">${endDate.toLocaleString('ru-RU')}</div>
        </div>
        <div class="modal-info-item">
            <div class="modal-info-label">Р”Р»РёС‚РµР»СЊРЅРѕСЃС‚СЊ</div>
            <div class="modal-info-value">${(process.metadata.duration / 3600).toFixed(1)} С‡</div>
        </div>
        <div class="modal-info-item">
            <div class="modal-info-label">РЎС‚Р°С‚СѓСЃ</div>
            <div class="modal-info-value">${process.metadata.completedSuccessfully ? 'вњ… РЈСЃРїРµС€РЅРѕ' : 'вљ пёЏ РџСЂРµСЂРІР°РЅРѕ'}</div>
        </div>
        <div class="modal-info-item">
            <div class="modal-info-label">РЎСЂРµРґРЅСЏСЏ РјРѕС‰РЅРѕСЃС‚СЊ</div>
            <div class="modal-info-value">${process.metrics?.power?.avgPower || 0} Р’С‚</div>
        </div>
        <div class="modal-info-item">
            <div class="modal-info-label">РџРѕС‚СЂРµР±Р»РµРЅРѕ СЌРЅРµСЂРіРёРё</div>
            <div class="modal-info-value">${(process.metrics?.power?.energyUsed || 0).toFixed(2)} РєР’С‚В·С‡</div>
        </div>
    `;

    // РџРѕСЃС‚СЂРѕРёС‚СЊ РіСЂР°С„РёРє С‚РµРјРїРµСЂР°С‚СѓСЂ
    renderTempChart(process);

    // РџРѕСЃС‚СЂРѕРёС‚СЊ РіСЂР°С„РёРє РјРѕС‰РЅРѕСЃС‚Рё
    renderPowerChart(process);

    // Р—Р°РїРѕР»РЅРёС‚СЊ С„Р°Р·С‹
    renderPhases(process);

    // Р—Р°РїРѕР»РЅРёС‚СЊ СЂРµР·СѓР»СЊС‚Р°С‚С‹
    const resultsGrid = document.getElementById('modal-results-grid');
    resultsGrid.innerHTML = `
        <div class="modal-info-item">
            <div class="modal-info-label">Р“РѕР»РѕРІС‹</div>
            <div class="modal-info-value">${process.results.headsCollected || 0} РјР»</div>
        </div>
        <div class="modal-info-item">
            <div class="modal-info-label">РўРµР»Рѕ</div>
            <div class="modal-info-value">${process.results.bodyCollected || 0} РјР»</div>
        </div>
        <div class="modal-info-item">
            <div class="modal-info-label">РҐРІРѕСЃС‚С‹</div>
            <div class="modal-info-value">${process.results.tailsCollected || 0} РјР»</div>
        </div>
        <div class="modal-info-item">
            <div class="modal-info-label">Р’СЃРµРіРѕ СЃРѕР±СЂР°РЅРѕ</div>
            <div class="modal-info-value">${process.results.totalCollected || 0} РјР»</div>
        </div>
    `;

    // РџСЂРёРІСЏР·Р°С‚СЊ РѕР±СЂР°Р±РѕС‚С‡РёРєРё Рє РєРЅРѕРїРєР°Рј СЌРєСЃРїРѕСЂС‚Р°
    const exportCsvBtn = document.getElementById('modal-export-csv');
    const exportJsonBtn = document.getElementById('modal-export-json');

    if (exportCsvBtn) {
        exportCsvBtn.onclick = () => exportHistoryCSV(process.id);
    }

    if (exportJsonBtn) {
        exportJsonBtn.onclick = () => exportHistoryJSON(process.id);
    }

    // РџРѕРєР°Р·Р°С‚СЊ РјРѕРґР°Р»СЊРЅРѕРµ РѕРєРЅРѕ
    document.getElementById('history-modal').classList.add('active');
    document.body.style.overflow = 'hidden';
}

function closeHistoryModal() {
    document.getElementById('history-modal').classList.remove('active');
    document.body.style.overflow = '';

    // РЈРЅРёС‡С‚РѕР¶РёС‚СЊ РіСЂР°С„РёРєРё
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
        chartEl.innerHTML = '<p style="text-align: center; color: var(--text-secondary); padding: 20px;">РќРµС‚ РґР°РЅРЅС‹С… РІСЂРµРјРµРЅРЅРѕРіРѕ СЂСЏРґР°</p>';
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
                name: 'РљСѓР±',
                data: data.map(p => ({ x: p.time * 1000, y: p.cube }))
            },
            {
                name: 'Р¦Р°СЂРіР° РІРµСЂС…',
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
                text: 'РўРµРјРїРµСЂР°С‚СѓСЂР° (В°C)'
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
        chartEl.innerHTML = '<p style="text-align: center; color: var(--text-secondary); padding: 20px;">РќРµС‚ РґР°РЅРЅС‹С… РІСЂРµРјРµРЅРЅРѕРіРѕ СЂСЏРґР°</p>';
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
                name: 'РњРѕС‰РЅРѕСЃС‚СЊ',
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
                text: 'РњРѕС‰РЅРѕСЃС‚СЊ (Р’С‚)'
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
        phasesEl.innerHTML = '<p style="text-align: center; color: var(--text-secondary); padding: 20px;">РќРµС‚ РёРЅС„РѕСЂРјР°С†РёРё Рѕ С„Р°Р·Р°С…</p>';
        return;
    }

    const phaseNames = {
        heating: 'РќР°РіСЂРµРІ',
        stabilization: 'РЎС‚Р°Р±РёР»РёР·Р°С†РёСЏ',
        heads: 'РћС‚Р±РѕСЂ РіРѕР»РѕРІ',
        body: 'РћС‚Р±РѕСЂ С‚РµР»Р°',
        tails: 'РћС‚Р±РѕСЂ С…РІРѕСЃС‚РѕРІ',
        purge: 'РћС‡РёСЃС‚РєР°',
        finish: 'Р—Р°РІРµСЂС€РµРЅРёРµ'
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
                <div class="modal-phase-detail">РќР°С‡Р°Р»Рѕ: <strong>${startDate.toLocaleTimeString('ru-RU')}</strong></div>
                <div class="modal-phase-detail">РћРєРѕРЅС‡Р°РЅРёРµ: <strong>${endDate.toLocaleTimeString('ru-RU')}</strong></div>
                <div class="modal-phase-detail">Р”Р»РёС‚РµР»СЊРЅРѕСЃС‚СЊ: <strong>${(phase.duration / 60).toFixed(0)} РјРёРЅ</strong></div>
                <div class="modal-phase-detail">РћР±СЉС‘Рј: <strong>${phase.volume || 0} РјР»</strong></div>
                <div class="modal-phase-detail">РЎСЂРµРґРЅСЏСЏ СЃРєРѕСЂРѕСЃС‚СЊ: <strong>${phase.avgSpeed || 0} РјР»/С‡</strong></div>
            </div>
        `;

        phasesEl.appendChild(phaseEl);
    });
}

// Р—Р°РєСЂС‹С‚РёРµ РјРѕРґР°Р»СЊРЅРѕРіРѕ РѕРєРЅР° РїСЂРё РєР»РёРєРµ РЅР° overlay
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
        // Р•СЃР»Рё С„РѕСЂРјР°С‚ РЅРµ СѓРєР°Р·Р°РЅ, СЃРїСЂРѕСЃРёС‚СЊ Сѓ РїРѕР»СЊР·РѕРІР°С‚РµР»СЏ
        if (!format) {
            const choice = confirm('Р’С‹Р±РµСЂРёС‚Рµ С„РѕСЂРјР°С‚ СЌРєСЃРїРѕСЂС‚Р°:\n\nРћРљ - CSV (С‚Р°Р±Р»РёС†Р°)\nРћС‚РјРµРЅР° - JSON (РґР°РЅРЅС‹Рµ)');
            format = choice ? 'csv' : 'json';
        }

        addLog(`рџ“Ґ Р­РєСЃРїРѕСЂС‚ РїСЂРѕС†РµСЃСЃР° ${id} РІ С„РѕСЂРјР°С‚Рµ ${format.toUpperCase()}...`, 'info');

        // РћС‚РєСЂС‹С‚СЊ СЌРєСЃРїРѕСЂС‚ РІ РЅРѕРІРѕР№ РІРєР»Р°РґРєРµ
        window.open(`/api/history/${id}/export?format=${format}`, '_blank');

        addLog(`вњ… Р­РєСЃРїРѕСЂС‚ РїСЂРѕС†РµСЃСЃР° ${id} РЅР°С‡Р°С‚`, 'info');
    } catch (error) {
        console.error('Error exporting history:', error);
        addLog('вќЊ РћС€РёР±РєР° СЌРєСЃРїРѕСЂС‚Р°', 'error');
    }
}

async function exportHistoryCSV(id) {
    await exportHistory(id, 'csv');
}

async function exportHistoryJSON(id) {
    await exportHistory(id, 'json');
}

// ============================================================================
// РЎСЂР°РІРЅРµРЅРёРµ РїСЂРѕС†РµСЃСЃРѕРІ
// ============================================================================

let compareTempChart = null;
let comparePowerChart = null;

async function compareSelected() {
    if (selectedProcesses.size < 2) {
        alert('Р’С‹Р±РµСЂРёС‚Рµ РјРёРЅРёРјСѓРј 2 РїСЂРѕС†РµСЃСЃР° РґР»СЏ СЃСЂР°РІРЅРµРЅРёСЏ');
        return;
    }

    if (selectedProcesses.size > 5) {
        alert('РњРѕР¶РЅРѕ СЃСЂР°РІРЅРёС‚СЊ РјР°РєСЃРёРјСѓРј 5 РїСЂРѕС†РµСЃСЃРѕРІ РѕРґРЅРѕРІСЂРµРјРµРЅРЅРѕ');
        return;
    }

    try {
        addLog(`рџ“Љ Р—Р°РіСЂСѓР·РєР° ${selectedProcesses.size} РїСЂРѕС†РµСЃСЃРѕРІ РґР»СЏ СЃСЂР°РІРЅРµРЅРёСЏ...`, 'info');

        // Р—Р°РіСЂСѓР·РёС‚СЊ РІСЃРµ РІС‹Р±СЂР°РЅРЅС‹Рµ РїСЂРѕС†РµСЃСЃС‹
        const processes = [];
        for (const processId of selectedProcesses) {
            const response = await fetch(`/api/history/${processId}`);
            if (response.ok) {
                const process = await response.json();
                processes.push(process);
            }
        }

        if (processes.length < 2) {
            alert('РќРµ СѓРґР°Р»РѕСЃСЊ Р·Р°РіСЂСѓР·РёС‚СЊ РїСЂРѕС†РµСЃСЃС‹ РґР»СЏ СЃСЂР°РІРЅРµРЅРёСЏ');
            return;
        }

        showCompareModal(processes);
        addLog(`вњ… РЎСЂР°РІРЅРµРЅРёРµ ${processes.length} РїСЂРѕС†РµСЃСЃРѕРІ`, 'info');
    } catch (error) {
        console.error('Error comparing processes:', error);
        addLog('вќЊ РћС€РёР±РєР° РїСЂРё СЃСЂР°РІРЅРµРЅРёРё РїСЂРѕС†РµСЃСЃРѕРІ', 'error');
        alert('РћС€РёР±РєР° РїСЂРё СЃСЂР°РІРЅРµРЅРёРё РїСЂРѕС†РµСЃСЃРѕРІ');
    }
}

function showCompareModal(processes) {
    // Р—Р°РїРѕР»РЅРёС‚СЊ СЃРїРёСЃРѕРє РїСЂРѕС†РµСЃСЃРѕРІ
    const processList = document.getElementById('compare-process-list');
    processList.innerHTML = '';

    const colors = ['#dc3545', '#007bff', '#28a745', '#ffc107', '#6f42c1'];

    processes.forEach((process, index) => {
        const typeNames = {
            rectification: 'Р РµРєС‚РёС„РёРєР°С†РёСЏ',
            distillation: 'Р”РёСЃС‚РёР»Р»СЏС†РёСЏ',
            mashing: 'Р—Р°С‚РёСЂРєР°',
            hold: 'Р’С‹РґРµСЂР¶РєР°'
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

    // РџРѕСЃС‚СЂРѕРёС‚СЊ РіСЂР°С„РёРєРё СЃСЂР°РІРЅРµРЅРёСЏ
    renderCompareTempChart(processes, colors);
    renderComparePowerChart(processes, colors);
    renderCompareTable(processes);

    // РџРѕРєР°Р·Р°С‚СЊ РјРѕРґР°Р»СЊРЅРѕРµ РѕРєРЅРѕ
    document.getElementById('compare-modal').classList.add('active');
    document.body.style.overflow = 'hidden';
}

function closeCompareModal() {
    document.getElementById('compare-modal').classList.remove('active');
    document.body.style.overflow = '';

    // РЈРЅРёС‡С‚РѕР¶РёС‚СЊ РіСЂР°С„РёРєРё
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
                name: `РџСЂРѕС†РµСЃСЃ ${index + 1} (${startDate})`,
                data: process.timeseries.data.map(p => ({
                    x: p.time * 1000,
                    y: p.cube
                }))
            });
        }
    });

    if (series.length === 0) {
        chartEl.innerHTML = '<p style="text-align: center; padding: 20px;">РќРµС‚ РґР°РЅРЅС‹С… РґР»СЏ СЃСЂР°РІРЅРµРЅРёСЏ</p>';
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
                text: 'РўРµРјРїРµСЂР°С‚СѓСЂР° РєСѓР±Р° (В°C)'
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
                name: `РџСЂРѕС†РµСЃСЃ ${index + 1} (${startDate})`,
                data: process.timeseries.data.map(p => ({
                    x: p.time * 1000,
                    y: p.power
                }))
            });
        }
    });

    if (series.length === 0) {
        chartEl.innerHTML = '<p style="text-align: center; padding: 20px;">РќРµС‚ РґР°РЅРЅС‹С… РґР»СЏ СЃСЂР°РІРЅРµРЅРёСЏ</p>';
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
                text: 'РњРѕС‰РЅРѕСЃС‚СЊ (Р’С‚)'
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
    html += '<th style="padding: 10px; border: 1px solid var(--border-color);">РџР°СЂР°РјРµС‚СЂ</th>';

    processes.forEach((process, index) => {
        const startDate = new Date(process.metadata.startTime * 1000).toLocaleDateString('ru-RU');
        html += `<th style="padding: 10px; border: 1px solid var(--border-color);">РџСЂРѕС†РµСЃСЃ ${index + 1}<br><span style="font-size: 0.8em; font-weight: normal;">${startDate}</span></th>`;
    });

    html += '</tr></thead><tbody>';

    // РЎС‚СЂРѕРєРё С‚Р°Р±Р»РёС†С‹
    const rows = [
        { label: 'Р”Р»РёС‚РµР»СЊРЅРѕСЃС‚СЊ', getValue: (p) => (p.metadata.duration / 3600).toFixed(1) + ' С‡' },
        { label: 'РЎСЂРµРґРЅСЏСЏ РјРѕС‰РЅРѕСЃС‚СЊ', getValue: (p) => (p.metrics?.power?.avgPower || 0) + ' Р’С‚' },
        { label: 'РџРѕС‚СЂРµР±Р»РµРЅРѕ СЌРЅРµСЂРіРёРё', getValue: (p) => (p.metrics?.power?.energyUsed || 0).toFixed(2) + ' РєР’С‚В·С‡' },
        { label: 'Р“РѕР»РѕРІС‹', getValue: (p) => (p.results?.headsCollected || 0) + ' РјР»' },
        { label: 'РўРµР»Рѕ', getValue: (p) => (p.results?.bodyCollected || 0) + ' РјР»' },
        { label: 'РҐРІРѕСЃС‚С‹', getValue: (p) => (p.results?.tailsCollected || 0) + ' РјР»' },
        { label: 'Р’СЃРµРіРѕ СЃРѕР±СЂР°РЅРѕ', getValue: (p) => (p.results?.totalCollected || 0) + ' РјР»' },
        { label: 'РЎС‚Р°С‚СѓСЃ', getValue: (p) => p.metadata.completedSuccessfully ? 'вњ… РЈСЃРїРµС€РЅРѕ' : 'вљ пёЏ РџСЂРµСЂРІР°РЅРѕ' }
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
// PROFILES - РЈРїСЂР°РІР»РµРЅРёРµ РїСЂРѕС„РёР»СЏРјРё РїСЂРѕС†РµСЃСЃРѕРІ
// ============================================================================

let currentProfileId = null; // ID РїСЂРѕС„РёР»СЏ РґР»СЏ РїСЂРѕСЃРјРѕС‚СЂР°/СЂРµРґР°РєС‚РёСЂРѕРІР°РЅРёСЏ

// Р—Р°РіСЂСѓР·РєР° СЃРїРёСЃРєР° РїСЂРѕС„РёР»РµР№
function loadProfilesList() {
    const listEl = document.getElementById('profiles-list');
    if (!listEl) return;

    listEl.innerHTML = '<p class="info-text">Р—Р°РіСЂСѓР·РєР° РїСЂРѕС„РёР»РµР№...</p>';

    fetch('/api/profiles')
        .then(response => response.json())
        .then(data => {
            if (data.profiles && data.profiles.length > 0) {
                renderProfilesList(data.profiles);
                updateProfilesStats(data.profiles);
            } else {
                listEl.innerHTML = '<p class="info-text">рџ“Ѓ РџСЂРѕС„РёР»Рё РЅРµ РЅР°Р№РґРµРЅС‹. РЎРѕР·РґР°Р№С‚Рµ РїРµСЂРІС‹Р№ РїСЂРѕС„РёР»СЊ!</p>';
            }
        })
        .catch(error => {
            console.error('РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё РїСЂРѕС„РёР»РµР№:', error);
            listEl.innerHTML = '<p class="error-text">вќЊ РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё РїСЂРѕС„РёР»РµР№</p>';
        });
}

// РћС‚СЂРёСЃРѕРІРєР° СЃРїРёСЃРєР° РїСЂРѕС„РёР»РµР№
function renderProfilesList(profiles) {
    const listEl = document.getElementById('profiles-list');
    const filter = document.getElementById('profile-filter-category').value;

    // РџСЂРёРјРµРЅРёС‚СЊ С„РёР»СЊС‚СЂ
    const filtered = filter === 'all'
        ? profiles
        : profiles.filter(p => p.category === filter);

    if (filtered.length === 0) {
        listEl.innerHTML = '<p class="info-text">рџ“Ѓ РџСЂРѕС„РёР»Рё РЅРµ РЅР°Р№РґРµРЅС‹ РґР»СЏ РІС‹Р±СЂР°РЅРЅРѕР№ РєР°С‚РµРіРѕСЂРёРё</p>';
        return;
    }

    let html = '';
    filtered.forEach(profile => {
        html += renderProfileItem(profile);
    });

    listEl.innerHTML = html;
}

// РћС‚СЂРёСЃРѕРІРєР° СЌР»РµРјРµРЅС‚Р° РїСЂРѕС„РёР»СЏ
function renderProfileItem(profile) {
    const categoryIcons = {
        'rectification': 'рџЊЂ',
        'distillation': 'рџ”Ґ',
        'mashing': 'рџЊѕ'
    };

    const categoryNames = {
        'rectification': 'Р РµРєС‚РёС„РёРєР°С†РёСЏ',
        'distillation': 'Р”РёСЃС‚РёР»Р»СЏС†РёСЏ',
        'mashing': 'Р—Р°С‚РёСЂРєР°'
    };

    const icon = categoryIcons[profile.category] || 'рџ“Ѓ';
    const catName = categoryNames[profile.category] || profile.category;
    const builtinBadge = profile.isBuiltin ? '<span style="background: #2196F3; color: white; padding: 2px 8px; border-radius: 12px; font-size: 0.75em; margin-left: 8px;">Р’СЃС‚СЂРѕРµРЅРЅС‹Р№</span>' : '';

    const lastUsed = profile.lastUsed > 0
        ? new Date(profile.lastUsed * 1000).toLocaleDateString('ru-RU')
        : 'РќРµ РёСЃРїРѕР»СЊР·РѕРІР°Р»СЃСЏ';

    return `
        <div class="profile-item" style="background: var(--bg-primary); padding: 15px; margin-bottom: 10px; border-radius: 8px; border-left: 4px solid var(--accent-color);">
            <div style="display: flex; justify-content: space-between; align-items: start; margin-bottom: 10px;">
                <div style="flex: 1;">
                    <div style="font-weight: 600; font-size: 1.1em; margin-bottom: 5px;">
                        ${icon} ${profile.name}${builtinBadge}
                    </div>
                    <div style="color: var(--text-secondary); font-size: 0.9em;">
                        ${catName} вЂў РСЃРїРѕР»СЊР·РѕРІР°РЅРёР№: ${profile.useCount} вЂў РџРѕСЃР»РµРґРЅРµРµ: ${lastUsed}
                    </div>
                </div>
                <div style="display: flex; gap: 5px;">
                    <button class="btn-icon" onclick="viewProfile('${profile.id}')" title="РџСЂРѕСЃРјРѕС‚СЂ">рџ‘ЃпёЏ</button>
                    <button class="btn-icon btn-success" onclick="quickLoadProfile('${profile.id}')" title="Р—Р°РіСЂСѓР·РёС‚СЊ">рџ“Ґ</button>
                    <button class="btn-icon" onclick="exportProfile('${profile.id}')" title="Р­РєСЃРїРѕСЂС‚">рџ“¤</button>
                    ${!profile.isBuiltin ? `<button class="btn-icon btn-danger" onclick="deleteProfile('${profile.id}')" title="РЈРґР°Р»РёС‚СЊ">рџ—‘пёЏ</button>` : ''}
                </div>
            </div>
        </div>
    `;
}

// РћР±РЅРѕРІР»РµРЅРёРµ СЃС‚Р°С‚РёСЃС‚РёРєРё РїСЂРѕС„РёР»РµР№
function updateProfilesStats(profiles) {
    document.getElementById('prof-stat-total').textContent = profiles.length;

    const builtin = profiles.filter(p => p.isBuiltin).length;
    const user = profiles.length - builtin;

    document.getElementById('prof-stat-builtin').textContent = builtin;
    document.getElementById('prof-stat-user').textContent = user;

    // РЎР°РјС‹Р№ РёСЃРїРѕР»СЊР·СѓРµРјС‹Р№
    if (profiles.length > 0) {
        const mostUsed = profiles.reduce((prev, current) =>
            (prev.useCount > current.useCount) ? prev : current
        );
        document.getElementById('prof-stat-popular').textContent =
            mostUsed.useCount > 0 ? mostUsed.name : 'вЂ”';
    } else {
        document.getElementById('prof-stat-popular').textContent = 'вЂ”';
    }
}

// РџРѕРєР°Р·Р°С‚СЊ РјРѕРґР°Р»СЊРЅРѕРµ РѕРєРЅРѕ СЃРѕР·РґР°РЅРёСЏ РїСЂРѕС„РёР»СЏ
function showCreateProfileModal() {
    currentProfileId = null;
    document.getElementById('profile-modal-title').textContent = 'РЎРѕР·РґР°РЅРёРµ РїСЂРѕС„РёР»СЏ';
    document.getElementById('profile-name').value = '';
    document.getElementById('profile-description').value = '';
    document.getElementById('profile-category').value = 'rectification';
    document.getElementById('profile-tags').value = '';
    document.getElementById('profile-modal').style.display = 'flex';
}

// Р—Р°РєСЂС‹С‚СЊ РјРѕРґР°Р»СЊРЅРѕРµ РѕРєРЅРѕ СЃРѕР·РґР°РЅРёСЏ
function closeProfileModal() {
    document.getElementById('profile-modal').style.display = 'none';
}

// РЎРѕС…СЂР°РЅРёС‚СЊ РїСЂРѕС„РёР»СЊ
function saveProfile() {
    const name = document.getElementById('profile-name').value.trim();
    const description = document.getElementById('profile-description').value.trim();
    const category = document.getElementById('profile-category').value;
    const tagsStr = document.getElementById('profile-tags').value.trim();
    const tags = tagsStr ? tagsStr.split(',').map(t => t.trim()).filter(t => t) : [];

    if (!name) {
        alert('РџРѕР¶Р°Р»СѓР№СЃС‚Р°, РІРІРµРґРёС‚Рµ РЅР°Р·РІР°РЅРёРµ РїСЂРѕС„РёР»СЏ');
        return;
    }

    // TODO: РџРѕР»СѓС‡РёС‚СЊ С‚РµРєСѓС‰РёРµ РїР°СЂР°РјРµС‚СЂС‹ РёР· С„РѕСЂРјС‹ СѓРїСЂР°РІР»РµРЅРёСЏ
    // РџРѕРєР° РёСЃРїРѕР»СЊР·СѓРµРј Р·РЅР°С‡РµРЅРёСЏ РїРѕ СѓРјРѕР»С‡Р°РЅРёСЋ
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
                alert('вњ… РџСЂРѕС„РёР»СЊ СѓСЃРїРµС€РЅРѕ СЃРѕР·РґР°РЅ!');
            } else {
                alert('вќЊ РћС€РёР±РєР° СЃРѕР·РґР°РЅРёСЏ РїСЂРѕС„РёР»СЏ: ' + (data.error || 'РќРµРёР·РІРµСЃС‚РЅР°СЏ РѕС€РёР±РєР°'));
            }
        })
        .catch(error => {
            console.error('РћС€РёР±РєР° СЃРѕС…СЂР°РЅРµРЅРёСЏ РїСЂРѕС„РёР»СЏ:', error);
            alert('вќЊ РћС€РёР±РєР° СЃРѕС…СЂР°РЅРµРЅРёСЏ РїСЂРѕС„РёР»СЏ');
        });
}

// РџСЂРѕСЃРјРѕС‚СЂ РїСЂРѕС„РёР»СЏ
function viewProfile(id) {
    fetch(`/api/profiles/${id}`)
        .then(response => response.json())
        .then(profile => {
            showProfileViewModal(profile);
        })
        .catch(error => {
            console.error('РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё РїСЂРѕС„РёР»СЏ:', error);
            alert('вќЊ РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё РїСЂРѕС„РёР»СЏ');
        });
}

// РџРѕРєР°Р·Р°С‚СЊ РјРѕРґР°Р»СЊРЅРѕРµ РѕРєРЅРѕ РїСЂРѕСЃРјРѕС‚СЂР° РїСЂРѕС„РёР»СЏ
function showProfileViewModal(profile) {
    currentProfileId = profile.id;
    document.getElementById('profile-view-title').textContent = profile.metadata.name;

    const body = document.getElementById('profile-view-body');
    const catNames = {
        'rectification': 'Р РµРєС‚РёС„РёРєР°С†РёСЏ',
        'distillation': 'Р”РёСЃС‚РёР»Р»СЏС†РёСЏ',
        'mashing': 'Р—Р°С‚РёСЂРєР°'
    };

    let html = `
        <div class="modal-section">
            <div class="modal-section-title">рџ“‹ РњРµС‚Р°РґР°РЅРЅС‹Рµ</div>
            <div class="modal-info-grid">
                <div><strong>РќР°Р·РІР°РЅРёРµ:</strong> ${profile.metadata.name}</div>
                <div><strong>РљР°С‚РµРіРѕСЂРёСЏ:</strong> ${catNames[profile.metadata.category] || profile.metadata.category}</div>
                <div><strong>РћРїРёСЃР°РЅРёРµ:</strong> ${profile.metadata.description || 'вЂ”'}</div>
                <div><strong>РђРІС‚РѕСЂ:</strong> ${profile.metadata.author}</div>
                <div><strong>РўРµРіРё:</strong> ${profile.metadata.tags.join(', ') || 'вЂ”'}</div>
                <div><strong>Р’СЃС‚СЂРѕРµРЅРЅС‹Р№:</strong> ${profile.metadata.isBuiltin ? 'Р”Р°' : 'РќРµС‚'}</div>
            </div>
        </div>

        <div class="modal-section">
            <div class="modal-section-title">вљ™пёЏ РџР°СЂР°РјРµС‚СЂС‹ СЂРµРєС‚РёС„РёРєР°С†РёРё</div>
            <div class="modal-info-grid">
                <div><strong>РЎС‚Р°Р±РёР»РёР·Р°С†РёСЏ:</strong> ${profile.parameters.rectification.stabilizationMin} РјРёРЅ</div>
                <div><strong>РћР±СЉС‘Рј РіРѕР»РѕРІ:</strong> ${profile.parameters.rectification.headsVolume} РјР»</div>
                <div><strong>РћР±СЉС‘Рј С‚РµР»Р°:</strong> ${profile.parameters.rectification.bodyVolume} РјР»</div>
                <div><strong>РћР±СЉС‘Рј С…РІРѕСЃС‚РѕРІ:</strong> ${profile.parameters.rectification.tailsVolume} РјР»</div>
                <div><strong>РЎРєРѕСЂРѕСЃС‚СЊ РіРѕР»РѕРІ:</strong> ${profile.parameters.rectification.headsSpeed} РјР»/С‡/РєР’С‚</div>
                <div><strong>РЎРєРѕСЂРѕСЃС‚СЊ С‚РµР»Р°:</strong> ${profile.parameters.rectification.bodySpeed} РјР»/С‡/РєР’С‚</div>
            </div>
        </div>

        <div class="modal-section">
            <div class="modal-section-title">рџЊЎпёЏ РўРµРјРїРµСЂР°С‚СѓСЂРЅС‹Рµ РїРѕСЂРѕРіРё</div>
            <div class="modal-info-grid">
                <div><strong>РњР°РєСЃ. РєСѓР±:</strong> ${profile.parameters.temperatures.maxCube}В°C</div>
                <div><strong>РњР°РєСЃ. РєРѕР»РѕРЅРЅР°:</strong> ${profile.parameters.temperatures.maxColumn}В°C</div>
                <div><strong>РћРєРѕРЅС‡Р°РЅРёРµ РіРѕР»РѕРІ:</strong> ${profile.parameters.temperatures.headsEnd}В°C</div>
                <div><strong>РќР°С‡Р°Р»Рѕ С‚РµР»Р°:</strong> ${profile.parameters.temperatures.bodyStart}В°C</div>
                <div><strong>РћРєРѕРЅС‡Р°РЅРёРµ С‚РµР»Р°:</strong> ${profile.parameters.temperatures.bodyEnd}В°C</div>
            </div>
        </div>

        <div class="modal-section">
            <div class="modal-section-title">рџ“Љ РЎС‚Р°С‚РёСЃС‚РёРєР° РёСЃРїРѕР»СЊР·РѕРІР°РЅРёСЏ</div>
            <div class="modal-info-grid">
                <div><strong>РСЃРїРѕР»СЊР·РѕРІР°РЅРёР№:</strong> ${profile.statistics.useCount}</div>
                <div><strong>РЎСЂРµРґРЅСЏСЏ РґР»РёС‚РµР»СЊРЅРѕСЃС‚СЊ:</strong> ${Math.round(profile.statistics.avgDuration / 60)} РјРёРЅ</div>
                <div><strong>РЎСЂРµРґРЅРёР№ РІС‹С…РѕРґ:</strong> ${profile.statistics.avgYield} РјР»</div>
                <div><strong>РЈСЃРїРµС€РЅРѕСЃС‚СЊ:</strong> ${profile.statistics.successRate.toFixed(1)}%</div>
            </div>
        </div>
    `;

    body.innerHTML = html;
    document.getElementById('profile-view-modal').style.display = 'flex';
}

// Р—Р°РєСЂС‹С‚СЊ РјРѕРґР°Р»СЊРЅРѕРµ РѕРєРЅРѕ РїСЂРѕСЃРјРѕС‚СЂР°
function closeProfileViewModal() {
    document.getElementById('profile-view-modal').style.display = 'none';
    currentProfileId = null;
}

// Р‘С‹СЃС‚СЂР°СЏ Р·Р°РіСЂСѓР·РєР° РїСЂРѕС„РёР»СЏ
function quickLoadProfile(id) {
    if (!confirm('Р—Р°РіСЂСѓР·РёС‚СЊ СЌС‚РѕС‚ РїСЂРѕС„РёР»СЊ РІ С‚РµРєСѓС‰РёРµ РЅР°СЃС‚СЂРѕР№РєРё?')) return;

    fetch(`/api/profiles/${id}/load`, {
        method: 'POST'
    })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                alert('вњ… РџСЂРѕС„РёР»СЊ СѓСЃРїРµС€РЅРѕ Р·Р°РіСЂСѓР¶РµРЅ! РџСЂРѕРІРµСЂСЊС‚Рµ РЅР°СЃС‚СЂРѕР№РєРё РІ СЂР°Р·РґРµР»Рµ "РЈРїСЂР°РІР»РµРЅРёРµ".');
            } else {
                alert('вќЊ РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё РїСЂРѕС„РёР»СЏ: ' + (data.error || 'РќРµРёР·РІРµСЃС‚РЅР°СЏ РѕС€РёР±РєР°'));
            }
        })
        .catch(error => {
            console.error('РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё РїСЂРѕС„РёР»СЏ:', error);
            alert('вќЊ РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё РїСЂРѕС„РёР»СЏ');
        });
}

// Р—Р°РіСЂСѓР·РєР° РїСЂРѕС„РёР»СЏ РІ РЅР°СЃС‚СЂРѕР№РєРё (РёР· РјРѕРґР°Р»СЊРЅРѕРіРѕ РѕРєРЅР°)
function loadProfileToSettings() {
    if (!currentProfileId) return;
    closeProfileViewModal();
    quickLoadProfile(currentProfileId);
}

// РЈРґР°Р»РµРЅРёРµ РїСЂРѕС„РёР»СЏ
function deleteProfile(id) {
    if (!confirm('РЈРґР°Р»РёС‚СЊ СЌС‚РѕС‚ РїСЂРѕС„РёР»СЊ? Р”РµР№СЃС‚РІРёРµ РЅРµР»СЊР·СЏ РѕС‚РјРµРЅРёС‚СЊ.')) return;

    fetch(`/api/profiles/${id}`, {
        method: 'DELETE'
    })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                loadProfilesList();
                alert('вњ… РџСЂРѕС„РёР»СЊ СѓРґР°Р»С‘РЅ');
            } else {
                alert('вќЊ ' + (data.error || 'РћС€РёР±РєР° СѓРґР°Р»РµРЅРёСЏ РїСЂРѕС„РёР»СЏ'));
            }
        })
        .catch(error => {
            console.error('РћС€РёР±РєР° СѓРґР°Р»РµРЅРёСЏ РїСЂРѕС„РёР»СЏ:', error);
            alert('вќЊ РћС€РёР±РєР° СѓРґР°Р»РµРЅРёСЏ РїСЂРѕС„РёР»СЏ');
        });
}

// РћС‡РёСЃС‚РєР° РїРѕР»СЊР·РѕРІР°С‚РµР»СЊСЃРєРёС… РїСЂРѕС„РёР»РµР№
function clearUserProfiles() {
    if (!confirm('РЈРґР°Р»РёС‚СЊ Р’РЎР• РїРѕР»СЊР·РѕРІР°С‚РµР»СЊСЃРєРёРµ РїСЂРѕС„РёР»Рё? Р’СЃС‚СЂРѕРµРЅРЅС‹Рµ СЂРµС†РµРїС‚С‹ РѕСЃС‚Р°РЅСѓС‚СЃСЏ. Р”РµР№СЃС‚РІРёРµ РЅРµР»СЊР·СЏ РѕС‚РјРµРЅРёС‚СЊ!')) return;

    fetch('/api/profiles', {
        method: 'DELETE'
    })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                loadProfilesList();
                alert('вњ… Р’СЃРµ РїРѕР»СЊР·РѕРІР°С‚РµР»СЊСЃРєРёРµ РїСЂРѕС„РёР»Рё СѓРґР°Р»РµРЅС‹');
            } else {
                alert('вќЊ РћС€РёР±РєР° РѕС‡РёСЃС‚РєРё РїСЂРѕС„РёР»РµР№');
            }
        })
        .catch(error => {
            console.error('РћС€РёР±РєР° РѕС‡РёСЃС‚РєРё РїСЂРѕС„РёР»РµР№:', error);
            alert('вќЊ РћС€РёР±РєР° РѕС‡РёСЃС‚РєРё РїСЂРѕС„РёР»РµР№');
        });
}

// Р­РєСЃРїРѕСЂС‚ РѕРґРЅРѕРіРѕ РїСЂРѕС„РёР»СЏ
function exportProfile(id) {
    fetch(`/api/profiles/${id}/export`)
        .then(response => response.json())
        .then(data => {
            // РЎРѕР·РґР°РµРј blob Рё СЃРєР°С‡РёРІР°РµРј
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
            console.error('РћС€РёР±РєР° СЌРєСЃРїРѕСЂС‚Р° РїСЂРѕС„РёР»СЏ:', error);
            alert('вќЊ РћС€РёР±РєР° СЌРєСЃРїРѕСЂС‚Р° РїСЂРѕС„РёР»СЏ');
        });
}

// Р­РєСЃРїРѕСЂС‚ РІСЃРµС… РїСЂРѕС„РёР»РµР№
function exportAllProfiles() {
    const includeBuiltin = confirm('Р’РєР»СЋС‡РёС‚СЊ РІСЃС‚СЂРѕРµРЅРЅС‹Рµ СЂРµС†РµРїС‚С‹ РІ СЌРєСЃРїРѕСЂС‚?');

    fetch(`/api/profiles/export${includeBuiltin ? '?includeBuiltin=true' : ''}`)
        .then(response => response.json())
        .then(data => {
            if (!data || data.length === 0) {
                alert('РќРµС‚ РїСЂРѕС„РёР»РµР№ РґР»СЏ СЌРєСЃРїРѕСЂС‚Р°');
                return;
            }

            // РЎРѕР·РґР°РµРј blob Рё СЃРєР°С‡РёРІР°РµРј
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

            alert(`вњ… Р­РєСЃРїРѕСЂС‚РёСЂРѕРІР°РЅРѕ РїСЂРѕС„РёР»РµР№: ${data.length}`);
        })
        .catch(error => {
            console.error('РћС€РёР±РєР° СЌРєСЃРїРѕСЂС‚Р° РїСЂРѕС„РёР»РµР№:', error);
            alert('вќЊ РћС€РёР±РєР° СЌРєСЃРїРѕСЂС‚Р° РїСЂРѕС„РёР»РµР№');
        });
}

// РџРѕРєР°Р·Р°С‚СЊ РјРѕРґР°Р»СЊРЅРѕРµ РѕРєРЅРѕ РёРјРїРѕСЂС‚Р°
let importFileData = null;

function showImportModal() {
    importFileData = null;
    document.getElementById('import-file-input').value = '';
    document.getElementById('import-preview').style.display = 'none';
    document.getElementById('import-btn').disabled = true;
    document.getElementById('profile-import-modal').style.display = 'flex';

    // Р”РѕР±Р°РІР»СЏРµРј РѕР±СЂР°Р±РѕС‚С‡РёРє РІС‹Р±РѕСЂР° С„Р°Р№Р»Р°
    document.getElementById('import-file-input').onchange = function (e) {
        const file = e.target.files[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = function (event) {
            try {
                importFileData = JSON.parse(event.target.result);

                // РџРѕРєР°Р·С‹РІР°РµРј РїСЂРµРґРїСЂРѕСЃРјРѕС‚СЂ
                let previewText = '';
                if (Array.isArray(importFileData)) {
                    previewText = `РњР°СЃСЃРёРІ РёР· ${importFileData.length} РїСЂРѕС„РёР»РµР№`;
                } else if (importFileData.metadata) {
                    previewText = `РџСЂРѕС„РёР»СЊ: ${importFileData.metadata.name}`;
                } else {
                    throw new Error('РќРµРІРµСЂРЅС‹Р№ С„РѕСЂРјР°С‚ JSON');
                }

                document.getElementById('import-preview-text').textContent = previewText;
                document.getElementById('import-preview').style.display = 'block';
                document.getElementById('import-btn').disabled = false;
            } catch (error) {
                alert('вќЊ РћС€РёР±РєР° С‡С‚РµРЅРёСЏ С„Р°Р№Р»Р°: РЅРµРІРµСЂРЅС‹Р№ С„РѕСЂРјР°С‚ JSON');
                importFileData = null;
                document.getElementById('import-btn').disabled = true;
            }
        };
        reader.readAsText(file);
    };
}

// Р—Р°РєСЂС‹С‚СЊ РјРѕРґР°Р»СЊРЅРѕРµ РѕРєРЅРѕ РёРјРїРѕСЂС‚Р°
function closeImportModal() {
    document.getElementById('profile-import-modal').style.display = 'none';
    importFileData = null;
}

// Р’С‹РїРѕР»РЅРёС‚СЊ РёРјРїРѕСЂС‚ РїСЂРѕС„РёР»РµР№
function doImportProfiles() {
    if (!importFileData) {
        alert('Р’С‹Р±РµСЂРёС‚Рµ С„Р°Р№Р» РґР»СЏ РёРјРїРѕСЂС‚Р°');
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
                alert(`вњ… РРјРїРѕСЂС‚РёСЂРѕРІР°РЅРѕ РїСЂРѕС„РёР»РµР№: ${data.imported}`);
            } else {
                alert('вќЊ РћС€РёР±РєР° РёРјРїРѕСЂС‚Р°: ' + (data.error || 'РќРµРёР·РІРµСЃС‚РЅР°СЏ РѕС€РёР±РєР°'));
            }
        })
        .catch(error => {
            console.error('РћС€РёР±РєР° РёРјРїРѕСЂС‚Р° РїСЂРѕС„РёР»РµР№:', error);
            alert('вќЊ РћС€РёР±РєР° РёРјРїРѕСЂС‚Р° РїСЂРѕС„РёР»РµР№');
        });
}

// ============================================================================

// Р—Р°РєСЂС‹С‚РёРµ РјРѕРґР°Р»СЊРЅРѕРіРѕ РѕРєРЅР° СЃСЂР°РІРЅРµРЅРёСЏ РїСЂРё РєР»РёРєРµ РЅР° overlay
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
// РРЅС„РѕСЂРјР°С†РёСЏ Рѕ РїРѕР»СЊР·РѕРІР°С‚РµР»Рµ
// ============================================================================

async function loadUserInfo() {
    try {
        const response = await fetch('/api/web/user');
        if (!response.ok) {
            throw new Error('Failed to load user info');
        }
        const user = await response.json();
        const usernameElement = document.getElementById('current-username');
        if (usernameElement) {
            usernameElement.textContent = user.username || 'РќРµРёР·РІРµСЃС‚РЅРѕ';
        }
    } catch (error) {
        console.error('Error loading user info:', error);
        const usernameElement = document.getElementById('current-username');
        if (usernameElement) {
            usernameElement.textContent = 'РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё';
        }
    }
}

// ============================================================================
// РќР°СЃС‚СЂРѕР№РєРё ESP32
// ============================================================================

async function loadESP32Config() {
    try {
        const response = await fetch('/api/web/esp32/config');
        if (!response.ok) {
            throw new Error('Failed to load ESP32 config');
        }
        const config = await response.json();
        
        // Р—Р°РїРѕР»РЅСЏРµРј РїРѕР»СЏ С„РѕСЂРјС‹
        document.getElementById('esp32-enabled').checked = config.enabled || false;
        document.getElementById('esp32-host').value = config.host || '';
        document.getElementById('esp32-port').value = config.port || 80;
        document.getElementById('esp32-use-https').checked = config.useHttps || false;
        document.getElementById('esp32-username').value = config.username || '';
        document.getElementById('esp32-password').value = ''; // РќРµ РїРѕРєР°Р·С‹РІР°РµРј РїР°СЂРѕР»СЊ
        document.getElementById('esp32-timeout').value = config.timeout || 5;
        
        // РџРѕРєР°Р·С‹РІР°РµРј/СЃРєСЂС‹РІР°РµРј РїРѕР»СЏ РІ Р·Р°РІРёСЃРёРјРѕСЃС‚Рё РѕС‚ СЃРѕСЃС‚РѕСЏРЅРёСЏ
        toggleESP32Fields();
    } catch (error) {
        console.error('Error loading ESP32 config:', error);
    }
}

function toggleESP32Fields() {
    const enabled = document.getElementById('esp32-enabled').checked;
    const fields = document.getElementById('esp32-fields');
    if (fields) {
        fields.style.display = enabled ? 'block' : 'none';
    }
}

async function saveESP32Config() {
    const config = {
        enabled: document.getElementById('esp32-enabled').checked,
        host: document.getElementById('esp32-host').value.trim(),
        port: parseInt(document.getElementById('esp32-port').value) || 80,
        useHttps: document.getElementById('esp32-use-https').checked,
        username: document.getElementById('esp32-username').value.trim(),
        password: document.getElementById('esp32-password').value.trim(),
        timeout: parseInt(document.getElementById('esp32-timeout').value) || 5
    };
    
    // Р’Р°Р»РёРґР°С†РёСЏ
    if (config.enabled && !config.host) {
        alert('РЈРєР°Р¶РёС‚Рµ Р°РґСЂРµСЃ ESP32');
        return;
    }
    
    try {
        const response = await fetch('/api/web/esp32/config', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(config)
        });
        
        if (!response.ok) {
            const error = await response.json();
            throw new Error(error.error || 'Failed to save config');
        }
        
        const result = await response.json();
        alert('РќР°СЃС‚СЂРѕР№РєРё СЃРѕС…СЂР°РЅРµРЅС‹ СѓСЃРїРµС€РЅРѕ!');
        
        // Р•СЃР»Рё РїР°СЂРѕР»СЊ Р±С‹Р» РІРІРµРґРµРЅ, РѕС‡РёС‰Р°РµРј РїРѕР»Рµ
        if (config.password) {
            document.getElementById('esp32-password').value = '';
        }
    } catch (error) {
        console.error('Error saving ESP32 config:', error);
        alert('РћС€РёР±РєР° СЃРѕС…СЂР°РЅРµРЅРёСЏ РЅР°СЃС‚СЂРѕРµРє: ' + error.message);
    }
}

async function testESP32Connection() {
    const resultDiv = document.getElementById('esp32-test-result');
    if (!resultDiv) return;
    
    resultDiv.style.display = 'block';
    resultDiv.innerHTML = 'РџСЂРѕРІРµСЂРєР° РїРѕРґРєР»СЋС‡РµРЅРёСЏ...';
    resultDiv.style.background = 'var(--bg-secondary)';
    resultDiv.style.color = 'var(--text-primary)';
    
    try {
        const response = await fetch('/api/web/esp32/test', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            }
        });
        
        const result = await response.json();
        
        if (result.success) {
            resultDiv.style.background = 'rgba(40, 167, 69, 0.2)';
            resultDiv.style.color = '#28a745';
            resultDiv.style.border = '1px solid #28a745';
            resultDiv.innerHTML = 'вњ… ' + (result.message || 'РџРѕРґРєР»СЋС‡РµРЅРёРµ СѓСЃРїРµС€РЅРѕ!');
        } else {
            resultDiv.style.background = 'rgba(220, 53, 69, 0.2)';
            resultDiv.style.color = '#dc3545';
            resultDiv.style.border = '1px solid #dc3545';
            resultDiv.innerHTML = 'вќЊ ' + (result.error || 'РћС€РёР±РєР° РїРѕРґРєР»СЋС‡РµРЅРёСЏ');
        }
    } catch (error) {
        console.error('Error testing ESP32 connection:', error);
        resultDiv.style.background = 'rgba(220, 53, 69, 0.2)';
        resultDiv.style.color = '#dc3545';
        resultDiv.style.border = '1px solid #dc3545';
        resultDiv.innerHTML = 'вќЊ РћС€РёР±РєР°: ' + error.message;
    }
}

