import { addLog } from '../core/logs.js';
import { loadStatus } from '../core/status.js';

// ============================================================================
// Cloud (IoT tunnel)
// ============================================================================

export function updateCloudUiFromStatus(data) {
    const deviceIdEl = document.getElementById('device-id');
    if (deviceIdEl && data.deviceId) {
        deviceIdEl.textContent = String(data.deviceId);
    }

    const enabledEl = document.getElementById('cloud-enabled');
    const urlEl = document.getElementById('cloud-tunnel-url');
    const connEl = document.getElementById('cloud-conn-status');
    const authEl = document.getElementById('cloud-auth-status');
    const claimEl = document.getElementById('cloud-claim-status');

    if (data.cloud) {
        if (enabledEl && typeof data.cloud.enabled === 'boolean') {
            enabledEl.checked = data.cloud.enabled;
        }
        if (urlEl && typeof data.cloud.tunnelUrl === 'string' && document.activeElement !== urlEl) {
            if (!urlEl.value) urlEl.value = data.cloud.tunnelUrl;
        }
        if (connEl) connEl.textContent = data.cloud.connected ? 'online' : 'offline';
        if (authEl) authEl.textContent = data.cloud.authenticated ? 'ok' : 'no';

        if (claimEl) {
            if (data.cloud.claimActive && data.cloud.claimCode) {
                let remaining = null;
                if (data.cloud.claimExpiresAt !== undefined && data.uptime !== undefined) {
                    remaining = Math.max(0, Math.round(Number(data.cloud.claimExpiresAt) - Number(data.uptime)));
                }
                claimEl.textContent = remaining !== null
                    ? `${data.cloud.claimCode} (ещё ~${remaining}с)`
                    : String(data.cloud.claimCode);
            } else {
                claimEl.textContent = 'нет';
            }
        }
    }
}

export async function saveCloudConfig() {
    const enabledEl = document.getElementById('cloud-enabled');
    const urlEl = document.getElementById('cloud-tunnel-url');

    const enabled = !!enabledEl?.checked;
    const tunnelUrl = (urlEl?.value || '').trim();

    try {
        addLog('📤 Сохранение настроек облака...', 'info');
        const resp = await fetch('/api/cloud/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ enabled, tunnelUrl })
        });
        if (!resp.ok) {
            const t = await resp.text();
            addLog('✗ Ошибка сохранения облака: ' + t, 'error');
            return;
        }
        addLog('✓ Настройки облака сохранены', 'success');
        setTimeout(loadStatus, 500);
    } catch (e) {
        addLog('✗ Ошибка сети: ' + e.message, 'error');
    }
}

export async function generateCloudClaim() {
    try {
        addLog('📤 Генерация PIN для привязки...', 'info');
        const resp = await fetch('/api/cloud/claim', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ttlSeconds: 600 })
        });
        if (!resp.ok) {
            const t = await resp.text();
            addLog('✗ Ошибка генерации PIN: ' + t, 'error');
            return;
        }
        const data = await resp.json();
        addLog('✓ PIN сгенерирован: ' + (data.claimCode || ''), 'success');
        setTimeout(loadStatus, 200);
    } catch (e) {
        addLog('✗ Ошибка сети: ' + e.message, 'error');
    }
}
