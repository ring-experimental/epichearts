// Page side of the web build: cross-origin isolation, the player's original APK, saves, starting the game.
//
// The game runs in threads (the original's game thread and its own pthreads), which need SharedArrayBuffer, which
// needs a cross-origin isolated page. GitHub Pages cannot send the COOP / COEP headers, so the service worker
// (sw.js) adds them to every response; the first visit reloads once when the worker has taken over.
//
// Storage (IndexedDB):
//  - "epichearts-apk": the APK the player chose, as it is. It never leaves the device. It is kept apart from the
//    game's file system so its 34 MB are not loaded twice; at start its bytes are copied into wasm memory
//    (eh_set_apk, src/main.c), where the game reads them.
//  - "/persist" (Emscripten IDBFS): save/ (the original's save files, the port's settings).
'use strict';
const $ = id => document.getElementById(id);
const PERSIST = '/persist';
// SHA-256 of lib/armeabi/libtar-android.so of the supported original (the interpreter's hooks and native routines
// address that build). Other builds are refused.
const SO_PATH = 'lib/armeabi/libtar-android.so';
const SO_SHA256 = '36eb21b899b58b9a59412c95f3aac73794a1f970fa61d1385f885d24ebfc75f3';

function status(text, error) {
  $('status').textContent = text;
  $('status').classList.toggle('error', !!error);
}

// ---- cross-origin isolation ----
async function isolate() {
  if (self.crossOriginIsolated) return true;
  if (!('serviceWorker' in navigator)) return false;
  const reg = await navigator.serviceWorker.register('sw.js');
  await navigator.serviceWorker.ready;
  // the worker controls the page only after a reload; avoid a loop if isolation still fails
  if (!sessionStorage.getItem('eh-coi-reload')) {
    sessionStorage.setItem('eh-coi-reload', '1');
    location.reload();
    return new Promise(() => {});
  }
  return !!reg && self.crossOriginIsolated;
}

// ---- APK storage ----
function apkDb() {
  return new Promise((done, fail) => {
    const open = indexedDB.open('epichearts-apk', 1);
    open.onupgradeneeded = () => open.result.createObjectStore('files');
    open.onsuccess = () => done(open.result);
    open.onerror = () => fail(open.error);
  });
}
async function apkGet() {
  const db = await apkDb();
  return new Promise(done => {
    const get = db.transaction('files').objectStore('files').get('apk');
    get.onsuccess = () => { db.close(); done(get.result || null); };
    get.onerror = () => { db.close(); done(null); };
  });
}
async function apkPut(blob) {
  const db = await apkDb();
  return new Promise((done, fail) => {
    const tx = db.transaction('files', 'readwrite');
    if (blob) tx.objectStore('files').put(blob, 'apk'); else tx.objectStore('files').delete('apk');
    tx.oncomplete = () => { db.close(); done(); };
    tx.onerror = () => { db.close(); fail(tx.error); };
  });
}

// one entry of a zip (the APK): its bytes, stored or deflated
async function zipEntry(bytes, name) {
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let eocd = -1;
  for (let i = bytes.length - 22; i >= Math.max(0, bytes.length - 65557); i--)
    if (v.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
  if (eocd < 0) return null;
  const count = v.getUint16(eocd + 10, true);
  let p = v.getUint32(eocd + 16, true);
  const dec = new TextDecoder();
  for (let k = 0; k < count && v.getUint32(p, true) === 0x02014b50; k++) {
    const method = v.getUint16(p + 10, true), csize = v.getUint32(p + 20, true);
    const nl = v.getUint16(p + 28, true), el = v.getUint16(p + 30, true), cl = v.getUint16(p + 32, true);
    const lho = v.getUint32(p + 42, true);
    if (dec.decode(bytes.subarray(p + 46, p + 46 + nl)) === name) {
      const data = lho + 30 + v.getUint16(lho + 26, true) + v.getUint16(lho + 28, true);
      const raw = bytes.subarray(data, data + csize);
      if (method === 0) return raw;
      if (method !== 8) return null;
      const out = new Blob([raw]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
      return new Uint8Array(await new Response(out).arrayBuffer());
    }
    p += 46 + nl + el + cl;
  }
  return null;
}
async function sha256(bytes) {
  const h = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
  return Array.from(h, b => b.toString(16).padStart(2, '0')).join('');
}

async function choose(file) {
  $('need').hidden = true;
  try {
    status('원본을 확인하는 중…');
    const bytes = new Uint8Array(await file.arrayBuffer());
    const so = await zipEntry(bytes, SO_PATH);
    if (!so) throw new Error('EpicHearts Android APK가 아닙니다 (' + SO_PATH + ' 없음).');
    if (await sha256(so) !== SO_SHA256) throw new Error('지원하지 않는 판의 APK입니다. 이 페이지는 한 가지 판만 실행합니다.');
    status('저장하는 중…');
    await apkPut(new Blob([bytes]));
    show(true);
  } catch (e) {
    $('need').hidden = false;
    status(e.message || String(e), true);
  }
}

function show(haveApk) {
  $('need').hidden = haveApk;
  $('ready').hidden = !haveApk;
  status('');
}

// ---- the game ----
let store, syncing = false, again = false;
function saveSync() {
  if (!store) return;
  if (syncing) { again = true; return; }
  syncing = true;
  store.syncfs(false, err => {
    if (err) console.warn('storage write failed', err);
    syncing = false;
    if (again) { again = false; saveSync(); }
  });
}

var Module = {
  noInitialRun: true,
  canvas: $('canvas'),
  print: text => console.log(text),
  printErr: text => console.warn(text),
  onAbort: what => { $('setup').hidden = false; $('need').hidden = $('ready').hidden = true; status('오류로 멈췄습니다: ' + what, true); },
  // save export (src/ui/saveio.cpp): the file the game wrote into /tmp becomes a download
  downloadFile: (path, name) => {
    const fs = Module.FS;
    if (!fs.analyzePath(path).exists) return;
    const url = URL.createObjectURL(new Blob([fs.readFile(path)], { type: 'application/octet-stream' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
    fs.unlink(path);
  },
  // save import: the page's picker screen; the chosen file goes to /tmp and the game reads it from there
  pickSaveFile: () => { $('picker').hidden = false; },
  onGameQuit: () => { saveSync(); document.body.classList.remove('playing'); $('ended').hidden = false; },
};
// test switches of the desktop build (EH_BOT, EH_FPS, ...): ?env=EH_BOT=1,EH_FPS=1
const testEnv = new URLSearchParams(location.search).get('env');
if (testEnv) Module.preRun = [() => { for (const kv of testEnv.split(',')) { const i = kv.indexOf('='); if (i > 0) Module.ENV[kv.slice(0, i)] = kv.slice(i + 1); } }];
let runtimeReady;
const runtime = new Promise(done => { runtimeReady = done; });
Module.onRuntimeInitialized = () => runtimeReady();

async function start() {
  $('ready').hidden = true;
  status('시작하는 중…');
  const blob = await apkGet();
  if (!blob) { show(false); return; }
  await runtime;
  store = Module.FS;
  store.mkdir(PERSIST);
  store.mount(Module.IDBFS, {}, PERSIST);
  const ok = await new Promise(done => store.syncfs(true, err => done(!err)));
  if (!ok) { status('브라우저 저장소를 열지 못했습니다. 사생활 보호 창에서는 실행할 수 없습니다.', true); return; }
  const bytes = new Uint8Array(await blob.arrayBuffer());
  const ptr = Module._malloc(bytes.length);
  Module.HEAPU8.set(bytes, ptr);
  Module._eh_set_apk(ptr, bytes.length);
  // touch devices: full screen and landscape when the game starts (needs this click)
  if (matchMedia('(pointer: coarse)').matches && document.documentElement.requestFullscreen) {
    try { await document.documentElement.requestFullscreen({ navigationUI: 'hide' }); await screen.orientation.lock('landscape'); } catch (e) {}
  }
  $('setup').hidden = true;
  document.body.classList.add('playing');
  $('canvas').focus();
  // saves: written to IndexedDB every few seconds and when the page is left
  setInterval(saveSync, 3000);
  document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden') saveSync(); });
  addEventListener('pagehide', saveSync);
  Module.callMain([]);
}

async function boot() {
  if (!(await isolate())) {
    status('이 브라우저에서는 실행할 수 없습니다 (교차 출처 격리·SharedArrayBuffer 필요). 최신 Chrome, Edge, Firefox, Safari에서 열어 주세요.', true);
    return;
  }
  sessionStorage.removeItem('eh-coi-reload');
  const script = document.createElement('script');
  script.src = 'epichearts.js';
  document.body.appendChild(script);
  show(!!(await apkGet()));
}

$('canvas').addEventListener('webglcontextlost', () => console.warn('[page] WebGL context lost'));
$('canvas').addEventListener('webglcontextrestored', () => console.warn('[page] WebGL context restored'));
document.addEventListener('visibilitychange', () => console.warn('[page] visibility ' + document.visibilityState));
$('source').addEventListener('change', e => { if (e.target.files[0]) choose(e.target.files[0]); e.target.value = ''; });
$('start').addEventListener('click', start);
$('forget').addEventListener('click', async () => { await apkPut(null); show(false); });
$('again').addEventListener('click', () => location.reload());
const IMPORT_PATH = '/tmp/import.bin';
function picked(path) {
  $('picker').hidden = true;
  $('canvas').focus();
  if (!path) { Module._eh_save_picked(0); return; }
  const p = Module.stringToNewUTF8(path);
  Module._eh_save_picked(p);
  Module._free(p);
}
$('savefile').addEventListener('change', async e => {
  const f = e.target.files[0];
  e.target.value = '';
  if (!f) { picked(''); return; }
  Module.FS.writeFile(IMPORT_PATH, new Uint8Array(await f.arrayBuffer()));
  picked(IMPORT_PATH);
});
$('cancel').addEventListener('click', () => picked(''));
boot().catch(e => status(String(e), true));
