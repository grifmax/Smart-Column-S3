import { addLog } from '../core/logs.js';

export async function toggleDemoMode() {
    const checkbox = document.getElementById('demo-mode-enabled');
    if (!checkbox) return;

    const enabled = Boolean(checkbox.checked);
    localStorage.setItem('demoMode', enabled ? 'true' : 'false');

    try {
        const response = await fetch('/api/settings/demo', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ enabled })
        });

        if (!response.ok) {
            addLog('⚠️ Ошибка сохранения демо-режима на сервер', 'warning');
            return;
        }

        addLog(enabled ? '🧪 Демо-режим ВКЛЮЧЁН' : '✅ Демо-режим отключён', 'info');
    } catch {
        addLog('⚠️ Демо-режим сохранён локально (сервер недоступен)', 'warning');
    }
}

export async function loadDemoMode() {
    const checkbox = document.getElementById('demo-mode-enabled');
    if (!checkbox) return;

    try {
        const response = await fetch('/api/settings/demo');
        if (response.ok) {
            const data = await response.json();
            const enabled = Boolean(data?.demoMode);
            checkbox.checked = enabled;
            localStorage.setItem('demoMode', enabled ? 'true' : 'false');
            return;
        }
    } catch {
        // fallback to localStorage
    }

    checkbox.checked = localStorage.getItem('demoMode') === 'true';
}

export function rebootController() {
    if (!confirm('Перезагрузить контроллер ESP32?\n\nВсе текущие процессы будут остановлены!')) {
        return;
    }

    addLog('🔄 Отправка команды перезагрузки...', 'warning');

    fetch('/api/reboot', {
        method: 'POST'
    }).then((response) => {
        if (response.ok) {
            addLog('✓ Контроллер перезагружается...', 'success');
            setTimeout(() => {
                addLog('📌 Попытка переподключения...', 'info');
                window.location.reload();
            }, 5000);
        } else {
            addLog('✗ Ошибка перезагрузки', 'error');
        }
    }).catch((err) => {
        addLog(`❌ Ошибка сети: ${err.message}`, 'error');
    });
}
