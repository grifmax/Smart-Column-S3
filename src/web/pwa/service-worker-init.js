// ============================================================================
// PWA & Service Worker
// ============================================================================

export function initServiceWorker() {
    if ('serviceWorker' in navigator) {
        window.addEventListener('load', () => {
            navigator.serviceWorker.register('/service-worker.js', { updateViaCache: 'none' })
                .then((registration) => {
                    console.log('ServiceWorker registration successful with scope: ', registration.scope);
                    // Force an update check on each page load to reduce stale shell issues.
                    registration.update().catch(() => { });
                })
                .catch((err) => {
                    console.log('ServiceWorker registration failed: ', err);
                });
        });
    }
}
