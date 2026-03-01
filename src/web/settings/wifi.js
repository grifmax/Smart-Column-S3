import { addLog } from '../core/logs.js';

let selectedNetwork = null;

function byId(id) {
    return document.getElementById(id);
}

function setWifiMessage(message, type = 'info') {
    const el = byId('wifi-inline-message');
    if (!el) return;
    el.textContent = message || '';
    el.style.color = type === 'error'
        ? 'var(--danger, #dc3545)'
        : type === 'success'
            ? 'var(--success, #28a745)'
            : 'var(--text-secondary)';
}

function signalLevel(rssi) {
    const value = Number(rssi);
    if (!Number.isFinite(value)) return '░░░░';
    if (value >= -50) return '▂▄▆█';
    if (value >= -60) return '▂▄▆░';
    if (value >= -70) return '▂▄░░';
    return '▂░░░';
}

function renderNetworkList(networks = []) {
    const list = byId('wifi-inline-network-list');
    if (!list) return;

    if (!networks.length) {
        list.innerHTML = '<p class="info-text">Сети не найдены</p>';
        return;
    }

    list.innerHTML = '';
    networks.forEach((network) => {
        const btn = document.createElement('button');
        btn.type = 'button';
        btn.className = 'btn';
        btn.style.width = '100%';
        btn.style.textAlign = 'left';
        btn.style.marginBottom = '8px';

        const ssid = String(network?.ssid || '').trim();
        const lock = network?.encryption === 'open' ? '🔓' : '🔒';
        btn.textContent = `${lock} ${ssid || '(hidden)'}  ${signalLevel(network?.rssi)}  (${network?.rssi ?? '--'} dBm)`;
        btn.onclick = () => {
            selectedNetwork = ssid;
            const ssidInput = byId('wifi-inline-ssid');
            if (ssidInput) ssidInput.value = ssid;
            setWifiMessage(`Выбрана сеть: ${ssid}`, 'info');
        };

        list.appendChild(btn);
    });
}

export async function loadWiFiStatus() {
    const statusEl = byId('wifi-inline-status');
    if (!statusEl) return;

    try {
        const response = await fetch('/api/wifi/status');
        const data = await response.json();

        if (data.connected) {
            statusEl.textContent = `Подключено: ${data.ssid} | IP: ${data.ip} | RSSI: ${data.rssi} dBm`;
            statusEl.style.color = 'var(--success, #28a745)';
        } else if (data.apMode) {
            statusEl.textContent = `Режим AP: ${data.apSSID} | IP: ${data.apIP}`;
            statusEl.style.color = 'var(--warning, #ffc107)';
        } else {
            statusEl.textContent = 'WiFi не подключен';
            statusEl.style.color = 'var(--danger, #dc3545)';
        }
    } catch (error) {
        statusEl.textContent = 'Ошибка загрузки статуса WiFi';
        statusEl.style.color = 'var(--danger, #dc3545)';
        console.error('loadWiFiStatus error:', error);
    }
}

export async function scanWiFiNetworks() {
    const list = byId('wifi-inline-network-list');
    if (list) list.innerHTML = '<p class="info-text">Сканирование...</p>';

    try {
        const response = await fetch('/api/wifi/scan');
        const data = await response.json();
        if (!response.ok) throw new Error(data?.error || `HTTP ${response.status}`);

        renderNetworkList(Array.isArray(data.networks) ? data.networks : []);
        addLog(`Сканирование WiFi: найдено ${Number(data.count) || 0} сетей`, 'info');
    } catch (error) {
        if (list) list.innerHTML = `<p class="info-text" style="color:var(--danger,#dc3545)">Ошибка сканирования: ${error.message}</p>`;
        addLog(`Ошибка сканирования WiFi: ${error.message}`, 'error');
    }
}

export function cancelWiFiSelection() {
    selectedNetwork = null;
    const ssidInput = byId('wifi-inline-ssid');
    const passInput = byId('wifi-inline-password');
    if (ssidInput) ssidInput.value = '';
    if (passInput) passInput.value = '';
    setWifiMessage('', 'info');
}

export async function connectWiFiNetwork() {
    const ssid = String(byId('wifi-inline-ssid')?.value || selectedNetwork || '').trim();
    const password = String(byId('wifi-inline-password')?.value || '');

    if (!ssid) {
        setWifiMessage('Укажите SSID сети', 'error');
        return;
    }

    try {
        const response = await fetch('/api/wifi/connect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssid, password })
        });

        const data = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(data?.error || `HTTP ${response.status}`);

        setWifiMessage('Команда подключения отправлена. Подождите 10 секунд.', 'success');
        addLog(`WiFi: отправлена команда подключения к ${ssid}`, 'success');

        setTimeout(() => {
            loadWiFiStatus();
        }, 10000);
    } catch (error) {
        setWifiMessage(`Ошибка подключения: ${error.message}`, 'error');
        addLog(`WiFi: ошибка подключения (${error.message})`, 'error');
    }
}

// Legacy wrapper
export function saveWiFi() {
    connectWiFiNetwork();
}

export function initWiFiSettings() {
    if (!byId('wifi-inline-status')) return;
    loadWiFiStatus();
}
