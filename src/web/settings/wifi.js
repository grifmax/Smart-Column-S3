// ============================================================================

// Settings

// ============================================================================



export function saveWiFi() {

    const ssid = document.getElementById('wifi-ssid').value;

    const password = document.getElementById('wifi-password').value;



    if (ssid) {

        sendCommand('wifi', 'save', 0);

        addLog('💾 WiFi настройки сохранены', 'info');

        alert('WiFi настройки сохранены. Перезагрузите контроллер.');

    }

}
