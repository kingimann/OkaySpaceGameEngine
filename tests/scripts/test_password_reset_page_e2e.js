// Browser check of the reset page against a local fake Supabase (needs playwright + Chromium; not run in CI).
// usage: node test_password_reset_page_e2e.js [screenshot-dir]
const http = require('http'), fs = require('fs'), path = require('path'), assert = require('assert');
const { chromium } = require('playwright');
const shotDir = process.argv[2] || '';
const page0 = fs.readFileSync(path.join(__dirname, '../../supabase/auth-pages/reset-password/index.html'), 'utf8');
const seen = [];
let mode = 'ok';
const srv = http.createServer((req, res) => {
  const u = req.url.split('?')[0];
  if (u === '/reset/') {
    const html = page0.replace('supabaseUrl: ""', `supabaseUrl: "http://127.0.0.1:${srv.address().port}"`).replace('anonKey: ""', 'anonKey: "ANON"');
    res.writeHead(200, { 'content-type': 'text/html' }); return res.end(html);
  }
  if (u === '/auth/v1/user') {
    let b = ''; req.on('data', d => b += d); req.on('end', () => {
      seen.push({ method: req.method, auth: req.headers.authorization, apikey: req.headers.apikey, body: b });
      const hdr = { 'content-type': 'application/json', 'access-control-allow-origin': '*', 'access-control-allow-headers': '*', 'access-control-allow-methods': '*' };
      if (req.method === 'OPTIONS') { res.writeHead(204, hdr); return res.end(); }
      if (mode === 'expired') { res.writeHead(401, hdr); return res.end('{"msg":"token expired"}'); }
      if (mode === 'same') { res.writeHead(422, hdr); return res.end('{"msg":"New password should be different from the old password."}'); }
      res.writeHead(200, hdr); res.end('{"id":"u1"}');
    }); return;
  }
  res.writeHead(404); res.end();
});
const shot = async (p, n) => { if (shotDir) await p.screenshot({ path: path.join(shotDir, n + '.png') }); };
(async () => {
  await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${srv.address().port}/reset/`;
  const br = await chromium.launch();
  const ctx = await br.newContext({ viewport: { width: 520, height: 560 } });
  const logs = []; 
  const open = async (hash) => { const p = await ctx.newPage(); p.on('console', m => logs.push(m.text())); await p.goto(base + hash); return p; };

  // form + token removed from the address bar
  let p = await open('#access_token=SECRETTOK&type=recovery&refresh_token=R');
  assert.strictEqual(new URL(p.url()).hash, '');
  await shot(p, '1-form');
  // too short
  await p.fill('#pw', 'abc'); await p.fill('#pw2', 'abc'); await p.click('#submit');
  assert(/at least 8/.test(await p.textContent('#error')));
  // mismatch
  await p.fill('#pw', 'correct-horse-1'); await p.fill('#pw2', 'correct-horse-2'); await p.click('#submit');
  assert(/do not match/.test(await p.textContent('#error')));
  await shot(p, '2-mismatch');
  assert.strictEqual(seen.length, 0, 'no request on invalid input');
  // show/hide
  assert.strictEqual(await p.getAttribute('#pw', 'type'), 'password');
  await p.click('#toggle'); assert.strictEqual(await p.getAttribute('#pw', 'type'), 'text');
  await p.click('#toggle'); assert.strictEqual(await p.getAttribute('#pw', 'type'), 'password');
  // server says same password (422)
  mode = 'same'; await p.fill('#pw2', 'correct-horse-1'); await p.click('#submit');
  await p.waitForSelector('#error:not([hidden])'); await p.waitForFunction(() => /different/.test(document.getElementById('error').textContent));
  await shot(p, '3-server-error');
  // success
  mode = 'ok'; await p.click('#submit'); await p.waitForSelector('#done:not([hidden])');
  await shot(p, '4-success');
  const last = seen[seen.length - 1];
  assert.strictEqual(last.method, 'PUT'); assert.strictEqual(last.auth, 'Bearer SECRETTOK'); assert.strictEqual(last.apikey, 'ANON');
  assert.deepStrictEqual(JSON.parse(last.body), { password: 'correct-horse-1' });
  assert.strictEqual(await p.inputValue('#pw'), '');
  const ls = await p.evaluate(() => JSON.stringify([Object.keys(localStorage), Object.keys(sessionStorage), document.cookie]));
  assert.strictEqual(ls, '[[],[],""]');
  // expired token on submit
  p = await open('#access_token=T2&type=recovery'); mode = 'expired';
  await p.fill('#pw', 'correct-horse-1'); await p.fill('#pw2', 'correct-horse-1'); await p.click('#submit');
  await p.waitForFunction(() => /expired/.test(document.getElementById('error').textContent));
  assert(await p.isDisabled('#submit'));
  await shot(p, '5-expired-on-submit');
  // no token / error fragment
  p = await open(''); assert(/only works from the link/.test(await p.textContent('#fatal'))); await shot(p, '6-no-token');
  p = await open('#error=access_denied&error_code=otp_expired&error_description=x'); assert(/expired/.test(await p.textContent('#fatal')));
  assert(!logs.some(l => /SECRETTOK|correct-horse/.test(l)), 'nothing sensitive logged');
  await br.close(); srv.close();
  console.log('password_reset_page e2e: OK');
})().catch(e => { console.error(e); process.exit(1); });
