export function toggleAuthFields() {

    const enabled = document.getElementById('auth-enabled').checked;

    const fields = document.getElementById('auth-fields');

    fields.style.display = enabled ? 'block' : 'none';

}



export function saveSecurity() {

    const authEnabled = document.getElementById('auth-enabled').checked;

    const username = document.getElementById('web-username').value;

    const password = document.getElementById('web-password').value;

    const rateLimitEnabled = document.getElementById('rate-limit-enabled').checked;



    if (authEnabled && (!username || !password)) {

        alert('Укажите имя пользователя и пароль');

        return;

    }



    sendCommand('security', 'save', 0);

    addLog('💾 Настройки безопасности сохранены', 'info');

    alert('Настройки безопасности сохранены. Перезагрузите контроллер.');

}
