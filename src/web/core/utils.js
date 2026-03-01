// ============================================================================
// Утилиты
// ============================================================================

export function pad(num, size = 2) {
    let s = String(num);
    while (s.length < size) s = '0' + s;
    return s;
}

export function formatUptime(seconds) {
    if (!Number.isFinite(seconds) || seconds < 0) return '0:00:00';

    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = Math.floor(seconds % 60);

    return `${h}:${pad(m)}:${pad(s)}`;
}
