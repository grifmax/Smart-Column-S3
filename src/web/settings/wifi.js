import { addLog } from '../core/logs.js';

let selectedNetwork = null;
let currentSsid = '';
let savedProfiles = [];
const scannedNetworks = new Map();

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

function profileBySsid(ssid) {
    return savedProfiles.find((profile) => String(profile?.ssid || '').trim() === ssid) || null;
}

function toggleHidden(el, hidden) {
    if (!el) return;
    el.hidden = !!hidden;
}

function buildProfilePayload() {
    const ssid = String(byId('wifi-inline-ssid')?.value || selectedNetwork || '').trim();
    const useStaticIp = !!byId('wifi-inline-static-enabled')?.checked;

    return {
        ssid,
        password: String(byId('wifi-inline-password')?.value || ''),
        makePreferred: !!byId('wifi-inline-make-preferred')?.checked,
        useStaticIp,
        ip: useStaticIp ? String(byId('wifi-inline-ip')?.value || '').trim() : '',
        gateway: useStaticIp ? String(byId('wifi-inline-gateway')?.value || '').trim() : '',
        subnet: useStaticIp ? String(byId('wifi-inline-subnet')?.value || '').trim() : '',
        dns1: useStaticIp ? String(byId('wifi-inline-dns1')?.value || '').trim() : '',
        dns2: useStaticIp ? String(byId('wifi-inline-dns2')?.value || '').trim() : ''
    };
}

function fillProfileForm(profile = null) {
    const ssidInput = byId('wifi-inline-ssid');
    const passInput = byId('wifi-inline-password');
    const preferredInput = byId('wifi-inline-make-preferred');
    const staticInput = byId('wifi-inline-static-enabled');
    const ipInput = byId('wifi-inline-ip');
    const gatewayInput = byId('wifi-inline-gateway');
    const subnetInput = byId('wifi-inline-subnet');
    const dns1Input = byId('wifi-inline-dns1');
    const dns2Input = byId('wifi-inline-dns2');

    if (ssidInput) ssidInput.value = profile?.ssid || selectedNetwork || '';
    if (passInput) passInput.value = '';
    if (preferredInput) preferredInput.checked = !!profile && Number(profile.priority) === 1;
    if (staticInput) staticInput.checked = !!profile?.useStaticIp;
    if (ipInput) ipInput.value = profile?.ip || '';
    if (gatewayInput) gatewayInput.value = profile?.gateway || '';
    if (subnetInput) subnetInput.value = profile?.subnet || '255.255.255.0';
    if (dns1Input) dns1Input.value = profile?.dns1 || '';
    if (dns2Input) dns2Input.value = profile?.dns2 || '';

    toggleWiFiStaticFields();
}

function resetProfileForm() {
    selectedNetwork = null;
    fillProfileForm(null);
    setWifiMessage('', 'info');
}

function renderSavedProfiles() {
    const container = byId('wifi-saved-list');
    if (!container) return;

    if (!savedProfiles.length) {
        container.innerHTML = '<p class="info-text">Сохраненных сетей пока нет</p>';
        return;
    }

    container.innerHTML = '';
    savedProfiles.forEach((profile, index) => {
        const item = document.createElement('div');
        item.className = `wifi-saved-item${profile.connected ? ' is-connected' : ''}`;

        const top = document.createElement('div');
        top.className = 'wifi-item-top';

        const titleWrap = document.createElement('div');
        const title = document.createElement('h4');
        title.className = 'wifi-item-title';
        title.textContent = profile.ssid || '(hidden)';
        const meta = document.createElement('div');
        meta.className = 'wifi-item-meta';
        meta.textContent = profile.useStaticIp
            ? `Статический IP: ${profile.ip || 'не задан'}`
            : 'DHCP';
        titleWrap.append(title, meta);

        const badges = document.createElement('div');
        badges.className = 'wifi-item-badges';

        const priority = document.createElement('span');
        priority.className = 'wifi-badge is-priority';
        priority.textContent = `Приоритет ${index + 1}`;
        badges.appendChild(priority);

        if (profile.connected) {
            const connected = document.createElement('span');
            connected.className = 'wifi-badge is-connected';
            connected.textContent = 'Подключено';
            badges.appendChild(connected);
        }

        top.append(titleWrap, badges);

        const actions = document.createElement('div');
        actions.className = 'wifi-item-actions';

        const editBtn = document.createElement('button');
        editBtn.type = 'button';
        editBtn.className = 'btn btn-sm';
        editBtn.textContent = 'Изменить';
        editBtn.onclick = () => editWiFiProfile(profile.ssid);

        const connectBtn = document.createElement('button');
        connectBtn.type = 'button';
        connectBtn.className = 'btn btn-sm btn-success';
        connectBtn.textContent = 'Подключить';
        connectBtn.onclick = () => connectSavedWiFiProfile(profile.ssid);

        const upBtn = document.createElement('button');
        upBtn.type = 'button';
        upBtn.className = 'btn btn-sm';
        upBtn.textContent = '↑';
        upBtn.disabled = index === 0;
        upBtn.onclick = () => moveWiFiProfile(profile.ssid, 'up');

        const downBtn = document.createElement('button');
        downBtn.type = 'button';
        downBtn.className = 'btn btn-sm';
        downBtn.textContent = '↓';
        downBtn.disabled = index === savedProfiles.length - 1;
        downBtn.onclick = () => moveWiFiProfile(profile.ssid, 'down');

        const deleteBtn = document.createElement('button');
        deleteBtn.type = 'button';
        deleteBtn.className = 'btn btn-sm btn-danger';
        deleteBtn.textContent = 'Удалить';
        deleteBtn.onclick = () => deleteWiFiProfile(profile.ssid);

        actions.append(editBtn, connectBtn, upBtn, downBtn, deleteBtn);
        item.append(top, actions);
        container.appendChild(item);
    });
}

function renderNetworkList(networks = []) {
    const list = byId('wifi-inline-network-list');
    if (!list) return;
    scannedNetworks.clear();

    if (!networks.length) {
        list.innerHTML = '<p class="info-text">Сети не найдены</p>';
        return;
    }

    list.innerHTML = '';
    networks.forEach((network) => {
        const btn = document.createElement('button');
        btn.type = 'button';
        btn.className = 'wifi-scan-item';

        const ssid = String(network?.ssid || '').trim();
        const lock = network?.encryption === 'open' ? '🔓' : '🔒';
        const saved = profileBySsid(ssid);

        scannedNetworks.set(ssid, { encryption: network?.encryption || 'secured' });
        btn.textContent = `${lock} ${ssid || '(hidden)'}  ${signalLevel(network?.rssi)}  (${network?.rssi ?? '--'} dBm)${saved ? ' • сохранена' : ''}`;
        btn.onclick = () => {
            selectedNetwork = ssid;
            fillProfileForm(saved);
            const ssidInput = byId('wifi-inline-ssid');
            if (ssidInput) ssidInput.value = ssid;
            setWifiMessage(`Выбрана сеть: ${ssid}`, 'info');
        };

        list.appendChild(btn);
    });
}

async function fetchJson(url, options) {
    const response = await fetch(url, options);
    const data = await response.json().catch(() => ({}));
    if (!response.ok) {
        throw new Error(data?.error || data?.message || `HTTP ${response.status}`);
    }
    return data;
}

export function toggleWiFiStaticFields() {
    const enabled = !!byId('wifi-inline-static-enabled')?.checked;
    toggleHidden(byId('wifi-static-fields'), !enabled);
}

export async function loadWiFiProfiles() {
    try {
        const data = await fetchJson('/api/wifi/profiles');
        savedProfiles = Array.isArray(data.profiles) ? data.profiles : [];
        renderSavedProfiles();
    } catch (error) {
        renderSavedProfiles();
        addLog(`WiFi: ошибка загрузки профилей (${error.message})`, 'error');
    }
}

export async function loadWiFiStatus() {
    const statusEl = byId('wifi-inline-status');
    if (!statusEl) return;

    try {
        const data = await fetchJson('/api/wifi/status');

        if (data.connected) {
            currentSsid = String(data.ssid || '').trim();
            statusEl.textContent = `Подключено: ${data.ssid} | IP: ${data.ip} | RSSI: ${data.rssi} dBm | Профилей: ${data.savedProfiles ?? savedProfiles.length}`;
            statusEl.style.color = 'var(--success, #28a745)';
        } else if (data.apMode) {
            currentSsid = '';
            statusEl.textContent = `Режим AP: ${data.apSSID} | IP: ${data.apIP} | Профилей: ${data.savedProfiles ?? savedProfiles.length}`;
            statusEl.style.color = 'var(--warning, #ffc107)';
        } else {
            currentSsid = '';
            statusEl.textContent = `WiFi не подключен | Профилей: ${data.savedProfiles ?? savedProfiles.length}`;
            statusEl.style.color = 'var(--danger, #dc3545)';
        }

        renderSavedProfiles();
    } catch (error) {
        statusEl.textContent = 'Ошибка загрузки статуса WiFi';
        statusEl.style.color = 'var(--danger, #dc3545)';
        addLog(`WiFi: ошибка загрузки статуса (${error.message})`, 'error');
    }
}

export async function scanWiFiNetworks() {
    const list = byId('wifi-inline-network-list');
    if (list) list.innerHTML = '<p class="info-text">Сканирование...</p>';

    try {
        const data = await fetchJson('/api/wifi/scan');
        renderNetworkList(Array.isArray(data.networks) ? data.networks : []);
        addLog(`Сканирование WiFi: найдено ${Number(data.count) || 0} сетей`, 'info');
    } catch (error) {
        if (list) {
            list.innerHTML = `<p class="info-text" style="color:var(--danger,#dc3545)">Ошибка сканирования: ${error.message}</p>`;
        }
        addLog(`Ошибка сканирования WiFi: ${error.message}`, 'error');
    }
}

export function cancelWiFiSelection() {
    resetProfileForm();
}

export function editWiFiProfile(ssid) {
    const profile = profileBySsid(String(ssid || '').trim());
    if (!profile) return;

    selectedNetwork = profile.ssid;
    fillProfileForm(profile);
    setWifiMessage(`Редактирование профиля: ${profile.ssid}`, 'info');
}

export async function saveWiFiProfile() {
    const payload = buildProfilePayload();
    if (!payload.ssid) {
        setWifiMessage('Укажите SSID сети', 'error');
        return;
    }
    if (payload.useStaticIp && (!payload.ip || !payload.gateway || !payload.subnet)) {
        setWifiMessage('Для фиксированного IP заполните IP, шлюз и маску', 'error');
        return;
    }

    try {
        const data = await fetchJson('/api/wifi/profile', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });

        savedProfiles = Array.isArray(data.profiles) ? data.profiles : savedProfiles;
        renderSavedProfiles();
        setWifiMessage(`Профиль ${payload.ssid} сохранен`, 'success');
        addLog(`WiFi: профиль ${payload.ssid} сохранен`, 'success');
        await loadWiFiStatus();
        fillProfileForm(profileBySsid(payload.ssid));
    } catch (error) {
        setWifiMessage(`Ошибка сохранения профиля: ${error.message}`, 'error');
        addLog(`WiFi: ошибка сохранения профиля (${error.message})`, 'error');
    }
}

export async function moveWiFiProfile(ssid, direction) {
    try {
        const data = await fetchJson('/api/wifi/profile/reorder', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssid, direction })
        });

        savedProfiles = Array.isArray(data.profiles) ? data.profiles : savedProfiles;
        renderSavedProfiles();
        fillProfileForm(profileBySsid(ssid));
        setWifiMessage(`Приоритет сети ${ssid} обновлен`, 'success');
        addLog(`WiFi: изменен приоритет сети ${ssid}`, 'info');
    } catch (error) {
        setWifiMessage(`Ошибка смены приоритета: ${error.message}`, 'error');
        addLog(`WiFi: ошибка смены приоритета (${error.message})`, 'error');
    }
}

export async function deleteWiFiProfile(ssid) {
    if (!window.confirm(`Удалить профиль WiFi "${ssid}"?`)) {
        return;
    }

    try {
        const data = await fetchJson('/api/wifi/profile/delete', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssid })
        });

        savedProfiles = Array.isArray(data.profiles) ? data.profiles : [];
        renderSavedProfiles();
        if (String(byId('wifi-inline-ssid')?.value || '').trim() === ssid) {
            resetProfileForm();
        }
        setWifiMessage(`Профиль ${ssid} удален`, 'success');
        addLog(`WiFi: профиль ${ssid} удален`, 'info');
        await loadWiFiStatus();
    } catch (error) {
        setWifiMessage(`Ошибка удаления профиля: ${error.message}`, 'error');
        addLog(`WiFi: ошибка удаления профиля (${error.message})`, 'error');
    }
}

export async function connectSavedWiFiProfile(ssid) {
    const profile = profileBySsid(String(ssid || '').trim());
    if (!profile) return;

    try {
        await fetchJson('/api/wifi/connect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssid: profile.ssid, saveProfile: true })
        });

        setWifiMessage(`Подключение к ${profile.ssid} запущено. Подождите 10 секунд.`, 'success');
        addLog(`WiFi: запущено подключение к ${profile.ssid}`, 'success');
        setTimeout(() => {
            loadWiFiStatus();
            loadWiFiProfiles();
        }, 10000);
    } catch (error) {
        setWifiMessage(`Ошибка подключения: ${error.message}`, 'error');
        addLog(`WiFi: ошибка подключения (${error.message})`, 'error');
    }
}

export async function connectWiFiNetwork() {
    const payload = buildProfilePayload();

    if (!payload.ssid) {
        setWifiMessage('Укажите SSID сети', 'error');
        return;
    }

    const scanned = scannedNetworks.get(payload.ssid);
    const isSecured = scanned && scanned.encryption !== 'open';
    const saved = profileBySsid(payload.ssid);
    const hasStoredPassword = !!saved?.hasPassword;
    if (isSecured && !payload.password && !hasStoredPassword && payload.ssid !== currentSsid) {
        setWifiMessage('Для защищенной сети укажите пароль', 'error');
        return;
    }
    if (payload.useStaticIp && (!payload.ip || !payload.gateway || !payload.subnet)) {
        setWifiMessage('Для фиксированного IP заполните IP, шлюз и маску', 'error');
        return;
    }

    try {
        await fetchJson('/api/wifi/connect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                ...payload,
                saveProfile: true
            })
        });

        setWifiMessage(`Команда подключения к ${payload.ssid} отправлена. Подождите 10 секунд.`, 'success');
        addLog(`WiFi: отправлена команда подключения к ${payload.ssid}`, 'success');
        setTimeout(() => {
            loadWiFiStatus();
            loadWiFiProfiles();
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
    toggleWiFiStaticFields();
    loadWiFiStatus();
    loadWiFiProfiles();
}
