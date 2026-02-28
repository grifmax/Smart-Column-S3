export function toFinite(value, fallback = 0) {
    const num = Number(value);
    return Number.isFinite(num) ? num : fallback;
}

export function clampPercent(value) {
    const num = toFinite(value, 0);
    if (num < 0) return 0;
    if (num > 100) return 100;
    return num;
}

export function runtimeEscapeHtml(value) {
    return String(value ?? '')
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&#39;');
}

export function formatDurationSafe(totalSec) {
    const sec = Math.max(0, Math.round(toFinite(totalSec, 0)));
    return formatUptime(sec);
}

export function normalizeAbvPercent(value, fallback = 40.0) {
    const num = toFinite(value, fallback);
    if (num < 0) return 0;
    if (num > 100) return 100;
    return num;
}
