export function saveMqtt() {

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
