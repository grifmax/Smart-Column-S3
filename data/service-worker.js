const CACHE_NAME = 'smart-column-v7';
const ASSETS = [
  '/',
  '/index.html',
  '/style.css',
  '/column-animation.css',
  '/column-animation.js',
  '/schemes-animation.css',
  '/manifest.json',
  '/schemes/column-animated.svg',
  'https://cdn.jsdelivr.net/npm/apexcharts',
  'https://fonts.googleapis.com/css2?family=Exo+2:wght@500;600;700;800&family=Manrope:wght@400;500;600;700;800&display=swap'
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => {
      // Прекэшируем основные ассеты (не прерываемся при ошибках загрузки внешних)
      return Promise.allSettled(
        ASSETS.map(asset => cache.add(asset).catch(e => console.warn('SW: cache add error', asset, e)))
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
  // Не кэшируем API, WebSocket и расширения Chrome
  if (event.request.url.includes('/api/') ||
    event.request.url.includes('/ws') ||
    event.request.url.startsWith('chrome-extension://')) {
    return;
  }

  const requestUrl = new URL(event.request.url);
  const isAppShellFile =
    requestUrl.origin === self.location.origin &&
    (requestUrl.pathname === '/' ||
      requestUrl.pathname === '/index.html' ||
      requestUrl.pathname === '/app.js' ||
      requestUrl.pathname === '/style.css');

  // Для app shell используем network-first, чтобы не застревать на старом app.js
  if (isAppShellFile) {
    event.respondWith(
      fetch(event.request).then((networkResponse) => {
        if (networkResponse && networkResponse.status === 200 && networkResponse.type !== 'opaque') {
          const responseToCache = networkResponse.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(event.request, responseToCache));
        }
        return networkResponse;
      }).catch(() => caches.match(event.request))
    );
    return;
  }

  // Стратегия: Stale-While-Revalidate с динамическим кэшированием
  event.respondWith(
    caches.match(event.request).then((cachedResponse) => {
      const fetchPromise = fetch(event.request).then((networkResponse) => {
        // Кэшируем успешные ответы
        if (networkResponse && networkResponse.status === 200 && networkResponse.type !== 'opaque') {
          const responseToCache = networkResponse.clone();
          caches.open(CACHE_NAME).then((cache) => {
            cache.put(event.request, responseToCache);
          });
        }
        return networkResponse;
      }).catch(() => {
        // При ошибке сети просто игнорим, отдастся кэш, если есть
      });

      return cachedResponse || fetchPromise;
    })
  );
});
