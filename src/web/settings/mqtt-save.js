import { addLog } from '../core/logs.js';
import { toggleMqttFields } from './mqtt.js';

function getMqttElements() {
    return {
        enabled: document.getElementById('mqtt-enabled'),
        server: document.getElementById('mqtt-server'),
        port: document.getElementById('mqtt-port'),
        username: document.getElementById('mqtt-username'),
        password: document.getElementById('mqtt-password'),
        baseTopic: document.getElementById('mqtt-base-topic'),
        discovery: document.getElementById('mqtt-discovery'),
        publishInterval: document.getElementById('mqtt-publish-interval'),
        state: document.getElementById('mqtt-config-state')
    };
}

export async function loadMqttSettings() {
    const el = getMqttElements();
    if (!el.enabled || !el.server || !el.port || !el.username || !el.password || !el.baseTopic || !el.publishInterval) {
        return;
    }

    try {
        const response = await fetch('/api/settings/mqtt');
        if (!response.ok) {
            if (el.state) el.state.textContent = 'Статус: ошибка загрузки';
            return;
        }

        const data = await response.json();
        el.enabled.checked = Boolean(data.enabled);
        el.server.value = data.server || '';
        el.port.value = Number.isFinite(Number(data.port)) ? String(data.port) : '1883';
        el.username.value = data.username || '';
        el.password.value = data.password || '';
        el.baseTopic.value = data.baseTopic || 'smart-column';
        if (el.discovery) el.discovery.checked = data.discovery !== false;
        el.publishInterval.value = Number.isFinite(Number(data.publishInterval))
            ? String(data.publishInterval)
            : '10000';
        toggleMqttFields();

        if (el.state) {
            const connectedText = data.connected ? 'подключен' : 'не подключен';
            el.state.textContent = `Статус: ${connectedText}`;
        }
    } catch (error) {
        if (el.state) el.state.textContent = 'Статус: ошибка сети';
        console.error('MQTT settings load error:', error);
    }
}

export async function saveMqtt() {
    const el = getMqttElements();
    if (!el.enabled || !el.server || !el.port || !el.username || !el.password || !el.baseTopic || !el.publishInterval) {
        return;
    }

    const enabled = Boolean(el.enabled.checked);
    const server = (el.server.value || '').trim();
    const port = Number.parseInt(el.port.value, 10) || 1883;
    const username = (el.username.value || '').trim();
    const password = el.password.value || '';
    const baseTopic = (el.baseTopic.value || '').trim() || 'smart-column';
    const discovery = Boolean(el.discovery?.checked);
    const publishInterval = Number.parseInt(el.publishInterval.value, 10) || 10000;

    if (enabled && !server) {
        alert('Укажите адрес MQTT сервера');
        return;
    }

    try {
        const response = await fetch('/api/settings/mqtt', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                enabled,
                server,
                port,
                username,
                password,
                baseTopic,
                discovery,
                publishInterval
            })
        });

        if (!response.ok) {
            const text = await response.text();
            if (el.state) el.state.textContent = 'Статус: ошибка сохранения';
            addLog(`MQTT save failed: ${text}`, 'error');
            alert('Ошибка сохранения MQTT настроек');
            return;
        }

        if (el.state) el.state.textContent = 'Статус: сохранено';
        addLog('MQTT settings saved', 'success');
        await loadMqttSettings();
    } catch (error) {
        if (el.state) el.state.textContent = 'Статус: ошибка сети';
        addLog(`MQTT save network error: ${error.message}`, 'error');
        console.error('MQTT settings save error:', error);
    }
}

export async function sendMqttTest() {
    const el = getMqttElements();
    try {
        const response = await fetch('/api/settings/mqtt/test', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ message: 'Smart-Column S3: MQTT test from Web UI' })
        });

        if (!response.ok) {
            const text = await response.text();
            if (el.state) el.state.textContent = 'Статус: тест не отправлен';
            addLog(`MQTT test failed: ${text}`, 'error');
            alert('Не удалось отправить MQTT тест');
            return;
        }

        if (el.state) el.state.textContent = 'Статус: тест отправлен';
        addLog('MQTT test published', 'success');
    } catch (error) {
        if (el.state) el.state.textContent = 'Статус: ошибка сети';
        addLog(`MQTT test network error: ${error.message}`, 'error');
        console.error('MQTT test error:', error);
    }
}

