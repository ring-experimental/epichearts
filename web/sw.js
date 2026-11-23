// Service worker of the web build.
//  - Cross-origin isolation: every same-origin response gets COOP / COEP headers, so the page can use
//    SharedArrayBuffer (the game's threads). GitHub Pages cannot send these headers itself.
//  - Offline: the page's own files are cached (network first, so a new build applies on the next visit).
//    The player's APK and the saves are in IndexedDB (app.js), never in this cache.
const CACHE = 'epichearts-__BUILD__';
const FILES = ['./', 'index.html', 'app.js', 'epichearts.js', 'epichearts.wasm', 'epichearts.data', 'manifest.webmanifest',
  'icon.svg', 'icon-192.png', 'icon-512.png'];

self.addEventListener('install', e => e.waitUntil(caches.open(CACHE).then(c => c.addAll(FILES)).catch(() => {}).then(() => self.skipWaiting())));
self.addEventListener('activate', e => e.waitUntil(
  caches.keys().then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k)))).then(() => self.clients.claim())));

function isolated(response) {
  if (response.status === 0) return response; // opaque
  const headers = new Headers(response.headers);
  headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
  headers.set('Cross-Origin-Opener-Policy', 'same-origin');
  headers.set('Cross-Origin-Resource-Policy', 'same-origin');
  return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
}

async function networkFirst(request) {
  try {
    const response = await fetch(request.url, { cache: 'no-cache' });
    if (response.ok) (await caches.open(CACHE)).put(request, response.clone());
    return isolated(response);
  } catch (e) {
    const hit = await caches.match(request, { ignoreSearch: true });
    if (hit) return isolated(hit);
    throw e;
  }
}

self.addEventListener('fetch', e => {
  if (e.request.method !== 'GET') return;
  const url = new URL(e.request.url);
  if (url.origin !== location.origin) return;
  e.respondWith(networkFirst(e.request));
});
