// ============================================================================
// Logs
// ============================================================================

const memoryLogs = [];
const renderedEventKeys = new Set();
const MAX_RENDERED_LOGS = 250;
const SYSTEM_LOG_POLL_INTERVAL_MS = 2000;

let lastSystemSeq = 0;
let systemLogPollTimer = null;
let systemLogHealthy = null;

function getLogContainer() {
    return document.getElementById('log-container');
}

function trimRenderedLogs(logContainer) {
    if (!logContainer) return;
    while (logContainer.children.length > MAX_RENDERED_LOGS) {
        logContainer.firstElementChild?.remove();
    }
}

function clearPlaceholder(logContainer) {
    if (!logContainer || logContainer.dataset.logsPlaceholderCleared === '1') {
        return;
    }

    const hasOnlyPlaceholder = logContainer.children.length === 1
        && logContainer.firstElementChild?.textContent?.includes('Инициализация');

    if (hasOnlyPlaceholder) {
        logContainer.innerHTML = '';
    }

    logContainer.dataset.logsPlaceholderCleared = '1';
}

function appendLogEntry(text, type = 'info', key = null) {
    const logContainer = getLogContainer();
    if (!logContainer) {
        return false;
    }

    if (key && renderedEventKeys.has(key)) {
        return false;
    }

    clearPlaceholder(logContainer);

    const entry = document.createElement('div');
    entry.className = `log-entry ${type}`;
    entry.textContent = text;
    logContainer.appendChild(entry);
    logContainer.scrollTop = logContainer.scrollHeight;
    trimRenderedLogs(logContainer);

    if (key) {
        renderedEventKeys.add(key);
    }

    return true;
}

function flushBufferedUiLogs() {
    if (!memoryLogs.length) return;

    const bufferedLogs = memoryLogs.splice(0, memoryLogs.length);
    bufferedLogs.forEach(({ text, type }) => {
        appendLogEntry(text, type);
    });
}

function normalizeLogType(level) {
    if (typeof level === 'number') {
        if (level >= 2) return 'error';
        if (level === 1) return 'warning';
        return 'info';
    }

    switch (String(level || '').toLowerCase()) {
        case 'error':
        case 'danger':
        case 'critical':
            return 'error';
        case 'warning':
        case 'warn':
            return 'warning';
        case 'success':
            return 'success';
        default:
            return 'info';
    }
}

function formatDeviceTimestamp(timestampMs) {
    const totalSec = Math.max(0, Math.floor((Number(timestampMs) || 0) / 1000));
    const hours = Math.floor(totalSec / 3600);
    const minutes = Math.floor((totalSec % 3600) / 60);
    const seconds = totalSec % 60;

    return `T+${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
}

function renderSystemEvents(events) {
    if (!Array.isArray(events) || events.length === 0) {
        return;
    }

    events.forEach((event) => {
        const message = String(event?.message || '').trim();
        if (!message) return;

        const sequence = Number(event?.seq) || 0;
        const timestamp = formatDeviceTimestamp(event?.timestamp);
        const type = normalizeLogType(event?.levelStr ?? event?.level);
        const key = sequence > 0 ? `sys:${sequence}` : `sys:${timestamp}:${message}`;
        appendLogEntry(`[${timestamp}] ${message}`, type, key);

        if (sequence > lastSystemSeq) {
            lastSystemSeq = sequence;
        }
    });
}

function handleSystemLogSyncError(error) {
    console.error('System log sync failed:', error);

    if (systemLogHealthy !== false) {
        systemLogHealthy = false;
        appendLogEntry(
            `[${new Date().toLocaleTimeString()}] Ошибка синхронизации системного журнала: ${error.message}`,
            'error',
            'system-log-sync-error'
        );
    }
}

async function loadSystemLogs() {
    const logContainer = getLogContainer();
    if (!logContainer) return;

    const query = lastSystemSeq > 0
        ? `?since=${encodeURIComponent(lastSystemSeq)}`
        : '?limit=120';

    const response = await fetch(`/api/logs/events${query}`, {
        cache: 'no-store'
    });

    if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
    }

    const payload = await response.json();
    renderSystemEvents(payload?.events);

    const nextSeq = Number(payload?.nextSeq);
    if (Number.isFinite(nextSeq) && nextSeq > lastSystemSeq) {
        lastSystemSeq = nextSeq;
    }

    flushBufferedUiLogs();

    if (systemLogHealthy === false) {
        appendLogEntry(
            `[${new Date().toLocaleTimeString()}] Системный журнал снова доступен`,
            'success',
            'system-log-sync-ok'
        );
    }

    systemLogHealthy = true;
}

export function initLogsPage() {
    const logContainer = getLogContainer();
    if (!logContainer) {
        return;
    }

    flushBufferedUiLogs();

    if (systemLogPollTimer) {
        return;
    }

    if (logContainer.dataset.systemLogsInit === '1') {
        return;
    }

    logContainer.dataset.systemLogsInit = '1';
    loadSystemLogs().catch(handleSystemLogSyncError);
    systemLogPollTimer = window.setInterval(() => {
        loadSystemLogs().catch(handleSystemLogSyncError);
    }, SYSTEM_LOG_POLL_INTERVAL_MS);
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

    appendLogEntry(text, type);
}

export function clearLogs() {
    if (!confirm('Очистить логи?')) {
        return;
    }

    const logContainer = getLogContainer();
    if (logContainer) {
        logContainer.innerHTML = '';
        logContainer.dataset.logsPlaceholderCleared = '1';
    }

    memoryLogs.length = 0;
    renderedEventKeys.clear();
    lastSystemSeq = 0;
    systemLogHealthy = null;

    fetch('/api/logs/events/clear', { method: 'POST' }).catch((error) => {
        console.error('Failed to clear system logs:', error);
    });

    addLog('Журнал очищен', 'info');
}

export function downloadLogs() {
    addLog('Запрос экспорта логов...', 'info');
    window.open('/api/export', '_blank');
}
