export function toggleMqttFields() {

    const enabled = document.getElementById('mqtt-enabled').checked;

    const fields = document.getElementById('mqtt-fields');

    fields.style.display = enabled ? 'block' : 'none';

}
