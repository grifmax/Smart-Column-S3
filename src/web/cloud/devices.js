// ============================================================================
// Облако: привязка устройства по ID + PIN (cloud-proxy кабинет)
// ============================================================================

export async function loadDiscoveredDevices() {
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

export async function claimDeviceToAccount() {
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
        try {
            payload = JSON.parse(text);
        } catch {
            payload = null;
        }

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



export let currentDeviceId = null;

export let devicesList = [];



// Загрузить список устройств

export async function loadESP32Devices() {

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

export async function loadESP32Device() {

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

export function showAddDeviceForm() {

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

export async function loadESP32Config() {

    await loadESP32Devices();

}



export function toggleESP32Fields() {

    const enabled = document.getElementById('esp32-enabled').checked;

    const fields = document.getElementById('esp32-fields');

    if (fields) {

        fields.style.display = enabled ? 'block' : 'none';

    }

}



// Сохранить устройство ESP32

export async function saveESP32Device() {

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



        await response.json();

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

export async function saveESP32Config() {

    await saveESP32Device();

}



// Активировать устройство

export async function activateESP32Device() {

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

export async function deleteESP32Device() {

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



export async function testESP32Connection() {

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
