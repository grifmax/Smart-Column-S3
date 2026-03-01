// Smart-Column S3 - ESP32 Settings (для добавления в app.js)

// ============================================================================
// ESP32 Settings для веб-интерфейса
// ============================================================================

// Загрузить настройки ESP32
async function loadESP32Settings() {
    try {
        const response = await fetch('/api/web/esp32/config');
        if (!response.ok) {
            console.error('Ошибка загрузки настроек ESP32');
            return;
        }
        
        const config = await response.json();
        
        // Заполнить форму
        document.getElementById('esp32-enabled').checked = config.enabled || false;
        document.getElementById('esp32-host').value = config.host || '';
        document.getElementById('esp32-port').value = config.port || 80;
        document.getElementById('esp32-use-https').checked = config.useHttps || false;
        document.getElementById('esp32-username').value = config.username || '';
        document.getElementById('esp32-password').value = ''; // Пароль не возвращается
        document.getElementById('esp32-timeout').value = config.timeout || 5;
        
        addLog('✅ Настройки ESP32 загружены', 'info');
    } catch (error) {
        console.error('Ошибка загрузки настроек ESP32:', error);
        addLog('❌ Ошибка загрузки настроек ESP32', 'error');
    }
}

// Сохранить настройки ESP32
async function saveESP32Settings() {
    const config = {
        enabled: document.getElementById('esp32-enabled').checked,
        host: document.getElementById('esp32-host').value.trim(),
        port: parseInt(document.getElementById('esp32-port').value) || 80,
        useHttps: document.getElementById('esp32-use-https').checked,
        username: document.getElementById('esp32-username').value.trim(),
        password: document.getElementById('esp32-password').value, // Может быть пустым (сохранится старый)
        timeout: parseInt(document.getElementById('esp32-timeout').value) || 5
    };
    
    if (config.enabled && !config.host) {
        alert('Укажите адрес ESP32');
        return;
    }
    
    try {
        const response = await fetch('/api/web/esp32/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });
        
        const result = await response.json();
        
        if (response.ok && result.success) {
            addLog('✅ Настройки ESP32 сохранены', 'success');
            alert('Настройки ESP32 сохранены!');
        } else {
            addLog('❌ Ошибка сохранения настроек ESP32: ' + (result.error || 'Неизвестная ошибка'), 'error');
            alert('Ошибка сохранения: ' + (result.error || 'Неизвестная ошибка'));
        }
    } catch (error) {
        console.error('Ошибка сохранения настроек ESP32:', error);
        addLog('❌ Ошибка соединения при сохранении настроек ESP32', 'error');
        alert('Ошибка соединения');
    }
}

// Проверить подключение к ESP32
async function testESP32Connection() {
    try {
        addLog('🔍 Проверка подключения к ESP32...', 'info');
        
        const response = await fetch('/api/web/esp32/test', {
            method: 'POST'
        });
        
        const result = await response.json();
        
        if (result.success) {
            addLog('✅ ' + result.message, 'success');
            alert('✅ ' + result.message);
        } else {
            addLog('❌ ' + (result.error || 'Ошибка подключения'), 'error');
            alert('❌ ' + (result.error || 'Ошибка подключения'));
        }
    } catch (error) {
        console.error('Ошибка проверки подключения:', error);
        addLog('❌ Ошибка проверки подключения', 'error');
        alert('Ошибка проверки подключения');
    }
}

// Добавить в DOMContentLoaded:
// loadESP32Settings();

