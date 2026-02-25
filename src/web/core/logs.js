// ============================================================================

// Logs

// ============================================================================



export function addLog(message, type = 'info') {

    const logContainer = document.getElementById('log-container');

    const timestamp = new Date().toLocaleTimeString();

    const entry = document.createElement('div');

    entry.className = `log-entry ${type}`;

    entry.textContent = `[${timestamp}] ${message}`;



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

        document.getElementById('log-container').innerHTML = '';

        addLog('Логи очищены', 'info');

    }

}



export function downloadLogs() {

    addLog('📥 Запрос экспорта логов...', 'info');

    window.open('/api/export', '_blank');

}
