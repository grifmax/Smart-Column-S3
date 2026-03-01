export function toggleTelegramFields() {

    const enabledEl = document.getElementById('telegram-enabled');

    const fields = document.getElementById('telegram-fields');

    if (!enabledEl || !fields) return;

    fields.style.display = enabledEl.checked ? 'block' : 'none';

}


export async function loadTelegramSettings() {

    const statusEl = document.getElementById('telegram-config-state');

    const enabledEl = document.getElementById('telegram-enabled');

    const tokenEl = document.getElementById('telegram-token');

    const chatIdEl = document.getElementById('telegram-chat-id');

    if (!statusEl || !enabledEl || !tokenEl || !chatIdEl) return;

    try {

        const response = await fetch('/api/settings/telegram');

        if (!response.ok) {

            statusEl.textContent = 'Статус: ошибка загрузки';

            return;

        }

        const data = await response.json();

        enabledEl.checked = !!data.enabled;

        tokenEl.value = data.token || '';

        chatIdEl.value = data.chatId || '';

        toggleTelegramFields();

        if (data.configured) {

            statusEl.textContent = data.active
                ? 'Статус: настроено и активно'
                : 'Статус: настроено';

        } else {

            statusEl.textContent = 'Статус: не настроено';

        }

    } catch (error) {

        console.error('Telegram settings load error:', error);

        statusEl.textContent = 'Статус: ошибка сети';

    }

}


export async function saveTelegramSettings() {

    const enabledEl = document.getElementById('telegram-enabled');

    const tokenEl = document.getElementById('telegram-token');

    const chatIdEl = document.getElementById('telegram-chat-id');

    const statusEl = document.getElementById('telegram-config-state');

    if (!enabledEl || !tokenEl || !chatIdEl || !statusEl) return;

    const enabled = !!enabledEl.checked;

    const token = (tokenEl.value || '').trim();

    const chatId = (chatIdEl.value || '').trim();

    if (enabled && (!token || !chatId)) {

        alert('Для включения Telegram укажите Bot Token и Chat ID');

        return;

    }

    try {

        const response = await fetch('/api/settings/telegram', {

            method: 'POST',

            headers: { 'Content-Type': 'application/json' },

            body: JSON.stringify({ enabled, token, chatId })

        });

        if (!response.ok) {

            const text = await response.text();

            statusEl.textContent = 'Статус: ошибка сохранения';

            addLog('✗ Telegram: ошибка сохранения: ' + text, 'error');

            alert('Ошибка сохранения Telegram настроек');

            return;

        }

        statusEl.textContent = enabled
            ? 'Статус: сохранено'
            : 'Статус: отключено';

        addLog('✓ Telegram настройки сохранены', 'success');

    } catch (error) {

        console.error('Telegram settings save error:', error);

        statusEl.textContent = 'Статус: ошибка сети';

        addLog('✗ Telegram: ошибка сети при сохранении', 'error');

    }

}


export async function sendTelegramTest() {

    const statusEl = document.getElementById('telegram-config-state');

    if (!statusEl) return;

    try {

        const response = await fetch('/api/settings/telegram/test', {

            method: 'POST',

            headers: { 'Content-Type': 'application/json' },

            body: JSON.stringify({ message: 'Smart-Column S3: test from Web UI' })

        });

        if (!response.ok) {

            const text = await response.text();

            addLog('✗ Telegram: тест не отправлен: ' + text, 'error');

            statusEl.textContent = 'Статус: тест не отправлен';

            alert('Не удалось отправить тестовое сообщение');

            return;

        }

        statusEl.textContent = 'Статус: тест отправлен';

        addLog('✓ Telegram: тестовое сообщение отправлено', 'success');

    } catch (error) {

        console.error('Telegram test send error:', error);

        statusEl.textContent = 'Статус: ошибка сети';

        addLog('✗ Telegram: ошибка сети при отправке теста', 'error');

    }

}
