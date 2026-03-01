// ============================================================================

// Logs

// ============================================================================



const memoryLogs = [];

function getLogContainer() {
    return document.getElementById('log-container');
}

export function addLog(message, type = 'info') {

    const timestamp = new Date().toLocaleTimeString();
    const text = `[${timestamp}] ${message}`;
    const logContainer = getLogContainer();

    if (!logContainer) {
        memoryLogs.push({ text, type });
        if (memoryLogs.length > 100) memoryLogs.shift();
        return;
    }

    const entry = document.createElement('div');
    entry.className = `log-entry ${type}`;
    entry.textContent = text;
    logContainer.appendChild(entry);

    // Автоскролл вниз
    logContainer.scrollTop = logContainer.scrollHeight;

    // Ограничить количество записей (последние 100)
    const entries = logContainer.querySelectorAll('.log-entry');
    if (entries.length > 100) {
        entries[0].remove();
    }

}



export function clearLogs() {

    if (confirm('Очистить логи?')) {
        const logContainer = getLogContainer();
        if (logContainer) logContainer.innerHTML = '';
        memoryLogs.length = 0;
        addLog('Логи очищены', 'info');

    }

}



export function downloadLogs() {

    addLog('📥 Запрос экспорта логов...', 'info');

    window.open('/api/export', '_blank');

}
