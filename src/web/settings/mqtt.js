export function toggleMqttFields() {

    const enabledEl = document.getElementById('mqtt-enabled');
    const fields = document.getElementById('mqtt-fields');

    if (!enabledEl || !fields) return;

    fields.style.display = enabledEl.checked ? 'block' : 'none';

}
