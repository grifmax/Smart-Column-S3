import { addLog } from '../core/logs.js';

function getSecurityElements() {
    return {
        authEnabled: document.getElementById('auth-enabled'),
        authFields: document.getElementById('auth-fields'),
        username: document.getElementById('web-username'),
        password: document.getElementById('web-password'),
        rateLimitEnabled: document.getElementById('rate-limit-enabled')
    };
}

export function toggleAuthFields() {
    const el = getSecurityElements();
    if (!el.authFields || !el.authEnabled) {
        return;
    }

    el.authFields.style.display = el.authEnabled.checked ? 'block' : 'none';
}

export async function loadSecuritySettings() {
    const el = getSecurityElements();
    if (!el.authEnabled || !el.username || !el.rateLimitEnabled) {
        return;
    }

    try {
        const response = await fetch('/api/settings/security');
        if (!response.ok) {
            return;
        }

        const data = await response.json();
        el.authEnabled.checked = Boolean(data.authEnabled);
        el.rateLimitEnabled.checked = data.rateLimitEnabled !== false;
        el.username.value = data.username || 'admin';
        if (el.password) {
            el.password.value = '';
            el.password.dataset.passwordConfigured = data.passwordConfigured ? '1' : '0';
            el.password.placeholder = data.passwordConfigured
                ? 'оставьте пустым, чтобы не менять'
                : 'новый пароль';
        }
        toggleAuthFields();
    } catch (error) {
        console.error('Security settings load error:', error);
    }
}

export async function saveSecurity() {
    const el = getSecurityElements();
    if (!el.authEnabled || !el.username || !el.rateLimitEnabled) {
        return;
    }

    const authEnabled = Boolean(el.authEnabled.checked);
    const username = (el.username.value || '').trim();
    const password = el.password?.value || '';
    const hasStoredPassword = el.password?.dataset.passwordConfigured === '1';
    const rateLimitEnabled = Boolean(el.rateLimitEnabled.checked);

    if (authEnabled && (!username || (!password && !hasStoredPassword))) {
        alert('Укажите имя пользователя и пароль');
        return;
    }

    const payload = {
        authEnabled,
        username,
        rateLimitEnabled
    };

    if (password) {
        payload.password = password;
    }

    try {
        const response = await fetch('/api/settings/security', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });

        if (!response.ok) {
            const text = await response.text();
            addLog(`Security settings save failed: ${text}`, 'error');
            alert('Ошибка сохранения настроек безопасности');
            return;
        }

        if (el.password) {
            el.password.value = '';
        }
        addLog('Security settings saved', 'success');
        alert('Настройки безопасности сохранены');
        await loadSecuritySettings();
    } catch (error) {
        addLog(`Security settings network error: ${error.message}`, 'error');
        console.error('Security settings save error:', error);
    }
}
