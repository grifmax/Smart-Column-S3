const CACHE_NAME = 'smart-column-v2.4.21';
const ASSETS = [
  '/',
  '/index.html',
  '/logs.html',
  '/charts.html',
  '/app.js?v=2.4.21',
  '/style.css?v=2.4.21',
  '/charts.js?v=2.4.21',
  '/charts-style.css?v=2.4.21',
  '/schemes-animation.css',
  '/manifest.json',
  '/schemes/column-tesla.svg',
  'https://cdn.jsdelivr.net/npm/apexcharts',
  'https://fonts.googleapis.com/css2?family=Exo+2:wght@500;600;700;800&family=Manrope:wght@400;500;600;700;800&display=swap'
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => {
      // Cache core assets; do not fail install if external CDN fails.
      return Promise.allSettled(
        ASSETS.map((asset) => cache.add(asset).catch((e) => console.warn('SW: cache add error', asset, e)))
      );
    })
  );
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((keys) => {
      return Promise.all(
        keys.filter((key) => key !== CACHE_NAME).map((key) => caches.delete(key))
      );
    })
  );
  self.clients.claim();
});

self.addEventListener('fetch', (event) => {
  // Do not cache API/WebSocket and browser extensions.
  if (
    event.request.url.includes('/api/') ||
    event.request.url.includes('/ws') ||
    event.request.url.startsWith('chrome-extension://')
  ) {
    return;
  }

  const requestUrl = new URL(event.request.url);
  const isAppShellFile =
    requestUrl.origin === self.location.origin &&
    (requestUrl.pathname === '/' ||
      requestUrl.pathname === '/index.html' ||
      requestUrl.pathname === '/logs.html' ||
      requestUrl.pathname === '/app.js' ||
      requestUrl.pathname === '/style.css' ||
      requestUrl.pathname === '/charts.html' ||
      requestUrl.pathname === '/charts.js' ||
      requestUrl.pathname === '/charts-style.css');

  // For app shell force real network first (no HTTP cache), fallback to SW cache.
  if (isAppShellFile) {
    const networkRequest = new Request(event.request, { cache: 'no-store' });
    event.respondWith(
      fetch(networkRequest)
        .then((networkResponse) => {
          if (networkResponse && networkResponse.status === 200 && networkResponse.type !== 'opaque') {
            const responseToCache = networkResponse.clone();
            caches.open(CACHE_NAME).then((cache) => cache.put(event.request, responseToCache));
          }
          return networkResponse;
        })
        .catch(() => caches.match(event.request))
    );
    return;
  }

  // Stale-While-Revalidate for the rest of static assets.
  event.respondWith(
    caches.match(event.request).then((cachedResponse) => {
      const fetchPromise = fetch(event.request)
        .then((networkResponse) => {
          if (networkResponse && networkResponse.status === 200 && networkResponse.type !== 'opaque') {
            const responseToCache = networkResponse.clone();
            caches.open(CACHE_NAME).then((cache) => {
              cache.put(event.request, responseToCache);
            });
          }
          return networkResponse;
        })
        .catch(() => {
          // Ignore network errors; cached response (if any) is returned.
        });

      return cachedResponse || fetchPromise;
    })
  );
});
