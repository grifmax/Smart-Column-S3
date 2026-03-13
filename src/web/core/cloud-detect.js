// ============================================================================
// Режим запуска UI: local (прямо на ESP32) vs cloud (через web-proxy кабинет)
// ============================================================================

export let isCloudProxyMode = false;

export function setIsCloudProxyMode(value) {
    isCloudProxyMode = value;
}

export async function detectCloudProxyMode() {
    // Идея: /api/web/* существует только на web-proxy (cloud_proxy).
    // На ESP32 этот путь обычно отдаст 404 -> local mode.
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 1200);

    try {
        const response = await fetch('/api/web/user', {
            credentials: 'same-origin',
            signal: controller.signal
        });

        // На cloud-proxy:
        // - 200: авторизован
        // - 401: не авторизован (но endpoint существует)
        // На ESP32:
        // - 404: endpoint отсутствует
        if (response.status === 404) return false;
        return response.status === 200 || response.status === 401 || response.status >= 400;
    } catch {
        // Network error / timeout — считаем локальным режимом, чтобы не дергать web API
        return false;
    } finally {
        clearTimeout(timeoutId);
    }
}

export function setCloudOnlyUiVisible(visible) {
    // Cloud-only элементы помечаем атрибутом data-cloud-only="1"
    document.querySelectorAll('[data-cloud-only="1"]').forEach(el => {
        el.style.display = visible ? '' : 'none';
    });
}
