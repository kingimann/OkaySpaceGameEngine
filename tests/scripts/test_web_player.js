// Headless Chromium check of the packaged web player (needs playwright + Chromium; CI: the web-build job).
//
// usage: node tests/scripts/test_web_player.js <site-dir> [--measure] [--json out.json]
//   <site-dir>  scripts/web_package.py output for tests/web/webtest_game (index.html, okay-player.js, .wasm.gz, pack)
//   --measure   only load the page and report download bytes and the time until the test game's orange square is on
//               screen (any web build, ours or a plain emcc one: used for the before / after numbers)
//   --throttle MBIT  emulate a slower line (with --measure)
//
// Checks: the page loads with no error box; the first frame is drawn (the canvas is not one flat colour and shows the
// test game's orange square); a Space press reaches the game script; the game's save (save()) and prefs (prefs_set())
// survive a reload through IndexedDB; right-click reaches the game and opens no browser menu; Space and arrows do not
// scroll a page that embeds the game in an iframe; audio contexts are running after the first click; a gamepad press
// (a fake through the Gamepad API) reaches the game; a missing program file is reported in plain words; the canvas has
// device pixels at a pixel ratio of 2 and follows a resized page; the full-screen button shows once the game runs.
const http = require('http'), fs = require('fs'), path = require('path'), zlib = require('zlib');
const { chromium } = require('playwright');

const args = process.argv.slice(2);
const site = args.find(a => !a.startsWith('--'));
const measureOnly = args.includes('--measure');
const jsonOut = args.includes('--json') ? args[args.indexOf('--json') + 1] : '';
// --throttle MBIT: a slower line (Chromium's network emulation, 40 ms round trip), for load times closer to a real host.
const throttle = args.includes('--throttle') ? parseFloat(args[args.indexOf('--throttle') + 1]) : 0;
if (!site || !fs.existsSync(site)) { console.error('usage: node test_web_player.js <site-dir> [--measure] [--json out.json]'); process.exit(2); }

// ---- a static server (Node's own http module) with the types a host sends ----
const types = { '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm', '.json': 'application/json',
                '.data': 'application/octet-stream', '.gz': 'application/octet-stream' };
let bytesServed = 0;
const srv = http.createServer((req, res) => {
  let u = decodeURIComponent(req.url.split('?')[0]);
  if (u === '/embed.html') {   // a long page that embeds the game, for the scroll check
    res.writeHead(200, { 'content-type': 'text/html' });
    return res.end('<!doctype html><body style="margin:0"><iframe id="g" src="/index.html" style="width:640px;height:360px;border:0"></iframe>' +
                   '<div style="height:4000px">long page</div></body>');
  }
  if (u === '/broken/index.html' || u === '/broken/okay-player.js') u = u.substring(7);   // a copy whose program is missing
  else if (u.startsWith('/broken/')) { res.writeHead(404); return res.end('not found'); }
  if (u === '/') u = '/index.html';
  const root = path.resolve(site), file = path.resolve(root, '.' + u);
  if (!file.startsWith(root + path.sep)) { res.writeHead(403); return res.end(); }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); return res.end('not found'); }
    bytesServed += data.length;
    res.writeHead(200, { 'content-type': types[path.extname(file)] || 'application/octet-stream', 'content-length': data.length, 'cache-control': 'no-store' });
    res.end(data);
  });
});

// ---- a small PNG reader for the screenshots (Node's zlib inflates the pixel data) ----
function decodeShotPng(buf) {
  let off = 8, w = 0, h = 0, ct = 0; const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE(off), type = buf.toString('ascii', off + 4, off + 8), body = buf.subarray(off + 8, off + 8 + len);
    if (type === 'IHDR') { w = body.readUInt32BE(0); h = body.readUInt32BE(4); ct = body[9]; }
    if (type === 'IDAT') idat.push(body);
    off += 12 + len;
  }
  const bpp = ct === 6 ? 4 : 3, raw = zlib.inflateSync(Buffer.concat(idat)), stride = w * bpp, px = Buffer.alloc(h * stride);
  for (let y = 0; y < h; ++y) {
    const f = raw[y * (stride + 1)], src = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; ++x) {
      const a = x >= bpp ? px[y * stride + x - bpp] : 0, b = y ? px[(y - 1) * stride + x] : 0, c = x >= bpp && y ? px[(y - 1) * stride + x - bpp] : 0;
      let v = src[x];
      if (f === 1) v += a; else if (f === 2) v += b; else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
      px[y * stride + x] = v & 255;
    }
  }
  return { w, h, bpp, px };
}
function shotStats(png) {
  const colours = new Set(); let orange = 0;
  for (let i = 0; i < png.px.length; i += png.bpp) {
    const r = png.px[i], g = png.px[i + 1], b = png.px[i + 2];
    colours.add((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
    if (r > 100 && r > g * 1.3 && g > b * 2) ++orange;   // the test game's square (0.95, 0.55, 0.15), also under the "Click to play" shade
  }
  return { colours: colours.size, orange };
}

const results = []; let failed = 0;
function check(name, ok, detail) { results.push({ name, ok: !!ok, detail: detail || '' }); if (!ok) ++failed; console.log((ok ? 'PASS ' : 'FAIL ') + name + (detail ? ' - ' + detail : '')); }

(async () => {
  await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${srv.address().port}`;
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--autoplay-policy=user-gesture-required'] });
  const ctx = await browser.newContext({ viewport: { width: 640, height: 360 } });
  // A fake Gamepad API pad (index 0, standard mapping): button 0 is pressed when window.__padA is true.
  await ctx.addInitScript(() => {
    const mk = () => ({ id: 'Fake Pad (STANDARD GAMEPAD)', index: 0, connected: true, mapping: 'standard', timestamp: performance.now(),
                        axes: [0, 0, 0, 0], buttons: Array.from({ length: 17 }, (_, i) => ({ pressed: i === 0 && !!window.__padA, touched: false, value: i === 0 && window.__padA ? 1 : 0 })) });
    window.__padOn = false;
    navigator.getGamepads = () => window.__padOn ? [mk(), null, null, null] : [null, null, null, null];
    window.__connectPad = () => { window.__padOn = true; const e = new Event('gamepadconnected'); Object.defineProperty(e, 'gamepad', { value: mk() }); window.dispatchEvent(e); };
  });
  const logs = [];
  const page = await ctx.newPage();
  page.on('console', m => logs.push(m.text()));
  page.on('pageerror', e => logs.push('pageerror: ' + e.message));
  const waitLog = async (re, ms, from = 0) => {
    const end = Date.now() + ms;
    while (Date.now() < end) { const hit = logs.slice(from).find(l => re.test(l)); if (hit) return hit; await page.waitForTimeout(50); }
    return null;
  };
  const shot = async () => shotStats(decodeShotPng(await page.locator('#canvas').screenshot()));
  if (throttle > 0) {
    const cdp = await ctx.newCDPSession(page);
    await cdp.send('Network.emulateNetworkConditions', { offline: false, latency: 40, downloadThroughput: throttle * 125000, uploadThroughput: throttle * 125000 });
  }

  // ---- load and the first frame ----
  bytesServed = 0;
  const t0 = Date.now();
  await page.goto(base + '/index.html');
  let firstFrame = -1, info = { colours: 0, orange: 0 };
  while (Date.now() - t0 < 180000) {
    const err = await page.evaluate(() => { const e = document.getElementById('okay-error'); return e && e.style.display === 'block' ? e.textContent : ''; });
    if (err) { check('page shows no error', false, err); break; }
    const hidden = await page.evaluate(() => { const l = document.getElementById('okay-loader'); return !l || l.classList.contains('hidden'); });
    if (hidden) { info = await shot(); if (measureOnly ? info.orange > 200 : info.colours > 1) { firstFrame = Date.now() - t0; break; } }
    await page.waitForTimeout(100);
  }
  const stats = await page.evaluate(() => (window.Module && Module.okayWeb) ? Module.okayWeb.stats : {});
  const measure = { bytesDownloaded: bytesServed, firstFrameMsWall: firstFrame, firstFrameMsPage: stats.firstFrameMs || -1, stats };
  console.log('MEASURE ' + JSON.stringify(measure));
  if (measureOnly) { if (jsonOut) fs.writeFileSync(jsonOut, JSON.stringify(measure, null, 1)); await browser.close(); srv.close(); process.exit(firstFrame > 0 ? 0 : 1); }

  check('first frame drawn', firstFrame > 0, firstFrame > 0 ? firstFrame + ' ms, ' + info.colours + ' colours' : 'nothing drawn in 180 s; last log: ' + logs.slice(-3).join(' | '));
  const started = await waitLog(/WEBTEST start presses=/, 10000);
  check('game script started', !!started, started || logs.slice(-5).join(' | '));
  const startCount = started ? parseFloat(started.split('presses=')[1]) : 0;

  // ---- the first click: the start overlay goes, audio is unlocked ----
  await page.mouse.click(320, 180);
  await page.waitForTimeout(300);
  const after = await page.evaluate(() => ({ overlay: document.getElementById('okay-start').classList.contains('hidden'), audio: Module.okayWeb.audioState() }));
  check('click to play overlay hides on the first click', after.overlay);
  check('audio is running after the first click', after.audio.every(s => s === 'running'), 'contexts: ' + JSON.stringify(after.audio));
  info = await shot();
  check('the game\'s orange square is on screen', info.orange > 200, info.orange + ' orange pixels');

  // ---- keyboard ----
  let mark = logs.length;
  await page.keyboard.press('Space');
  const key = await waitLog(/WEBTEST key presses=/, 5000, mark);
  check('a Space press reaches the game', !!key, key || 'no WEBTEST key line');

  // ---- right-click ----
  await page.evaluate(() => { window.__ctx = null; window.addEventListener('contextmenu', e => { window.__ctx = e.defaultPrevented; }); });
  mark = logs.length;
  await page.mouse.click(320, 180, { button: 'right' });
  const rc = await waitLog(/WEBTEST mouse right/, 3000, mark);
  check('right-click reaches the game', !!rc);
  check('right-click opens no browser menu', (await page.evaluate(() => window.__ctx)) === true);

  // ---- gamepad ----
  mark = logs.length;
  await page.evaluate(() => window.__connectPad());
  await page.waitForTimeout(300);
  await page.evaluate(() => { window.__padA = true; });
  const pad = await waitLog(/WEBTEST pad a/, 3000, mark);
  await page.evaluate(() => { window.__padA = false; });
  check('a gamepad button reaches the game', !!pad, pad ? '' : 'no WEBTEST pad line; ' + logs.slice(mark).filter(l => /pad|controller|joy/i.test(l)).slice(0, 3).join(' | '));

  // ---- prefs (debounced write) and the save: stored in IndexedDB, back after a reload ----
  mark = logs.length;
  const syncs0 = await page.evaluate(() => Module.okayWeb.saveSyncs);
  await page.keyboard.press('p');
  await waitLog(/WEBTEST prefs pp=/, 3000, mark);
  let stored = false;
  for (let i = 0; i < 60 && !stored; ++i) { await page.waitForTimeout(100); stored = (await page.evaluate(() => Module.okayWeb.saveSyncs)) > syncs0; }
  await page.waitForTimeout(1200);   // the prefs write waits 0.5 s after the last change, then the page stores it
  check('the save folder was stored in IndexedDB', stored);
  mark = logs.length;
  await page.reload();
  const again = await waitLog(/WEBTEST start presses=/, 120000, mark);
  const againCount = again ? parseFloat(again.split('presses=')[1]) : -1;
  const keyLines = logs.filter(l => /WEBTEST key presses=/.test(l));
  const lastSaved = keyLines.length ? parseFloat(keyLines[keyLines.length - 1].split('presses=')[1]) : startCount;
  check('a save survives a reload', againCount === lastSaved && lastSaved > startCount, (again || 'no start line after the reload') + '; saved ' + lastSaved);
  check('prefs survive a reload', !!again && /prefs=[1-9]/.test(again), again || '');

  // ---- no page scroll when the game is embedded ----
  const outer = await ctx.newPage();
  await outer.goto(base + '/embed.html');
  const frame = await (await outer.waitForSelector('#g')).contentFrame();
  await frame.waitForFunction(() => window.Module && Module.okayWeb && Module.okayWeb.stats.firstFrameMs > 0, null, { timeout: 120000 });
  await outer.mouse.click(320, 180);
  for (const k of ['Space', 'ArrowDown', 'PageDown', 'ArrowDown']) await outer.keyboard.press(k);
  await outer.waitForTimeout(300);
  const scrollY = await outer.evaluate(() => window.scrollY);
  check('Space and arrows do not scroll the page around the game', scrollY === 0, 'scrollY=' + scrollY);

  // ---- a missing file is said in plain words on the loading screen ----
  const bp = await ctx.newPage();
  await bp.goto(base + '/broken/index.html');
  const errText = await bp.waitForFunction(() => { const e = document.getElementById('okay-error'); return e && e.style.display === 'block' && e.textContent; }, null, { timeout: 60000 })
                         .then(h => h.jsonValue()).catch(() => '');
  check('a missing program file is reported in plain words', /missing on the server: okay-player\.wasm/.test(errText), errText || 'no error shown');
  await bp.close();

  // ---- high-DPI and resize: the canvas keeps the page's size in device pixels ----
  const hi = await browser.newContext({ viewport: { width: 640, height: 360 }, deviceScaleFactor: 2 });
  const hp = await hi.newPage();
  await hp.goto(base + '/index.html');
  await hp.waitForFunction(() => window.Module && Module.okayWeb && Module.okayWeb.stats.firstFrameMs > 0, null, { timeout: 120000 });
  await hp.waitForTimeout(300);
  let size = await hp.evaluate(() => ({ w: canvas.width, h: canvas.height, cw: canvas.clientWidth, ch: canvas.clientHeight, fs: getComputedStyle(document.getElementById('okay-fs')).display }));
  check('the canvas has device pixels on a high-DPI screen', size.w === 1280 && size.h === 720, JSON.stringify(size));
  check('the full-screen button is shown once the game runs', size.fs === 'block', size.fs);
  await hp.setViewportSize({ width: 800, height: 500 });
  await hp.waitForTimeout(500);
  size = await hp.evaluate(() => ({ w: canvas.width, h: canvas.height }));
  check('the canvas follows a resized page', size.w === 1600 && size.h === 1000, JSON.stringify(size));
  await hi.close();

  if (failed || process.env.OKAY_WEB_TEST_LOG) console.log('--- page log ---\n' + logs.filter(l => process.env.OKAY_WEB_TEST_LOG || /WEBTEST|error|warn/i.test(l)).slice(-60).join('\n'));
  console.log(`${results.length - failed} passed, ${failed} failed`);
  if (jsonOut) fs.writeFileSync(jsonOut, JSON.stringify({ measure, results }, null, 1));
  await browser.close(); srv.close();
  process.exit(failed ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
