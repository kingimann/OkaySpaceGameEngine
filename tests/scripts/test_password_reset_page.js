// Pure-logic test of supabase/auth-pages/reset-password/index.html (no browser, node only).
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
const html = fs.readFileSync(path.join(__dirname, '../../supabase/auth-pages/reset-password/index.html'), 'utf8');
const m = html.match(/\/\/ LOGIC-BEGIN([\s\S]*?)\/\/ LOGIC-END/);
assert(m, 'logic markers not found');
const ctx = {}; vm.createContext(ctx);
vm.runInContext(m[1] + '\nthis.L={parseFragment,validatePassword,configProblem,buildUpdateRequest,describeFailure,describeFragment,MIN_LEN,MAX_LEN};', ctx);
const L = ctx.L;
let n = 0; const t = (name, fn) => { fn(); n++; };

t('fragment', () => {
  const f = L.parseFragment('#access_token=abc.def-ghi&expires_in=3600&refresh_token=r&type=recovery');
  assert.strictEqual(f.token, 'abc.def-ghi'); assert.strictEqual(f.type, 'recovery');
  assert.strictEqual(L.describeFragment(f), '');
  assert.strictEqual(L.parseFragment('#access_token=a%2Bb%3D').token, 'a+b=');
  assert.strictEqual(L.parseFragment('').token, ''); assert.strictEqual(L.parseFragment(undefined).token, '');
  assert.strictEqual(L.parseFragment('#access_token=%E0%A4%A').token, '');   // bad escape: ignored, no throw
  const e = L.parseFragment('#error=access_denied&error_code=otp_expired&error_description=Email+link+is+invalid');
  assert.strictEqual(e.errorCode, 'otp_expired'); assert.strictEqual(e.errorDescription, 'Email link is invalid');
  assert(/expired/.test(L.describeFragment(e)));
  assert(/only works from the link/.test(L.describeFragment(L.parseFragment(''))));
  assert(/only works from the link/.test(L.describeFragment(L.parseFragment('#access_token=a&type=signup'))));
});
t('password', () => {
  assert(L.validatePassword('short', 'short'));
  assert(L.validatePassword('x'.repeat(73), 'x'.repeat(73)));
  assert(/match/.test(L.validatePassword('longenough1', 'longenough2')));
  assert.strictEqual(L.validatePassword('x'.repeat(8), 'x'.repeat(8)), '');
  assert.strictEqual(L.validatePassword('x'.repeat(72), 'x'.repeat(72)), '');
  assert(L.validatePassword(undefined, undefined));
});
t('config', () => {
  assert(L.configProblem({ supabaseUrl: '', anonKey: '' }));
  assert(L.configProblem({ supabaseUrl: 'http://evil.example', anonKey: 'k' }));
  assert(L.configProblem({ supabaseUrl: 'https://a.b/path', anonKey: 'k' }));
  assert.strictEqual(L.configProblem({ supabaseUrl: 'https://x.supabase.co/', anonKey: 'k' }), '');
  assert.strictEqual(L.configProblem({ supabaseUrl: 'http://127.0.0.1:8123', anonKey: 'k' }), '');
});
t('request', () => {
  const r = L.buildUpdateRequest({ supabaseUrl: 'https://x.supabase.co/', anonKey: 'ANON' }, 'TOK', 'p"w\\1');
  assert.strictEqual(r.url, 'https://x.supabase.co/auth/v1/user'); assert.strictEqual(r.method, 'PUT');
  assert.strictEqual(r.headers.Authorization, 'Bearer TOK'); assert.strictEqual(r.headers.apikey, 'ANON');
  assert.deepStrictEqual(JSON.parse(r.body), { password: 'p"w\\1' });
  assert.deepStrictEqual(Object.keys(JSON.parse(r.body)), ['password']);
});
t('failure', () => {
  assert(/expired/.test(L.describeFailure(401, '{}')));
  assert.strictEqual(L.describeFailure(422, '{"msg":"New password should be different from the old password."}'), 'New password should be different from the old password.');
  assert(/HTTP 500/.test(L.describeFailure(500, 'oops')));
  assert(/reach/.test(L.describeFailure(0, '')));
  assert(/Too many/.test(L.describeFailure(429, '')));
});
t('no logging or storage', () => {
  assert(!/console\.|localStorage|sessionStorage|document\.cookie/.test(html), 'page must not log or store anything');
});
console.log('password_reset_page: ' + n + ' groups passed');
