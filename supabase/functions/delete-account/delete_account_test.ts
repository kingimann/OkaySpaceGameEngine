// Runs under Node 22+ (`node --experimental-strip-types delete_account_test.ts`)
// and Deno (`deno test --allow-all`). A fake Deps records every call.
import test from "node:test";
import assert from "node:assert/strict";
import { BUCKETS, type Deps, handleDeleteAccount, STEPS } from "./delete_account.ts";

const UID = "11111111-2222-3333-4444-555555555555";
const VICTIM = "99999999-8888-7777-6666-555555555555";

function fake(over: Partial<Deps> = {}, opts: { username?: string; banned?: boolean } = {}) {
  const calls: string[] = [];
  const deps: Deps = {
    async userFromJwt(jwt) { calls.push(`jwt:${jwt}`); return jwt === "good" ? { id: UID, email: "me@x.io" } : null; },
    async passwordOk(email, pw) { calls.push(`pw:${email}`); return pw === "hunter22"; },
    async rest(method, path, body) {
      calls.push(`${method} ${path}${body === undefined ? "" : " " + JSON.stringify(body)}`);
      if (path.startsWith("profiles?select=username")) return { ok: true, status: 200, body: [{ username: opts.username ?? "Alice" }] };
      if (path.startsWith("site_bans?select")) return { ok: true, status: 200, body: opts.banned ? [{ user_id: UID }] : [] };
      return { ok: true, status: 204 };
    },
    async purgeBucket(b, uid) { calls.push(`purge ${b}/${uid}`); return { ok: true, removed: 0 }; },
    async deleteAuthUser(uid) { calls.push(`auth.delete ${uid}`); return { ok: true }; },
    ...over,
  };
  return { deps, calls };
}
const mut = (calls: string[]) => calls.filter((c) => /^(DELETE|PATCH|purge|auth\.delete)/.test(c));
const req = (headers: Record<string, string>, body: unknown, method = "POST") =>
  new Request("https://x.test/functions/v1/delete-account", { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
const good = { authorization: "Bearer good" };
const okBody = { confirm_username: "alice", password: "hunter22" };

test("no or wrong JWT -> 401 and nothing is deleted", async () => {
  for (const h of [{}, { authorization: "Bearer bad" }, { authorization: "Basic abc" }, { authorization: "Bearer" }]) {
    const { deps, calls } = fake();
    const r = await handleDeleteAccount(req(h, okBody), deps);
    assert.equal(r.status, 401);
    assert.deepEqual(mut(calls), []);
    assert.ok(!calls.some((c) => c.startsWith("GET")), "no data is even read");
  }
});

test("a JWT whose user id is not a uuid is refused", async () => {
  const { deps, calls } = fake({ async userFromJwt() { return { id: "1; drop table", email: "a@b.c" }; } });
  assert.equal((await handleDeleteAccount(req(good, okBody), deps)).status, 401);
  assert.deepEqual(mut(calls), []);
});

test("non-POST is 405", async () => {
  const { deps, calls } = fake();
  assert.equal((await handleDeleteAccount(req(good, undefined, "GET"), deps)).status, 405);
  assert.deepEqual(calls, []);
});

test("user id comes only from the JWT, never from the body", async () => {
  const { deps, calls } = fake();
  const r = await handleDeleteAccount(req(good, { ...okBody, user_id: VICTIM, id: VICTIM, uid: VICTIM, sub: VICTIM }), deps);
  assert.equal(r.status, 200);
  assert.ok(calls.some((c) => c.includes(UID)));
  assert.ok(!calls.some((c) => c.includes(VICTIM)), "the body id never reaches a query");
  assert.ok(calls.includes(`auth.delete ${UID}`));
});

test("missing fields, wrong username and wrong password delete nothing", async () => {
  for (const [body, status] of [
    [{ password: "hunter22" }, 400], [{ confirm_username: "alice" }, 400], [{ confirm_username: "bob", password: "hunter22" }, 400],
    [{ confirm_username: "alice", password: "nope" }, 403], [undefined, 400],
  ] as const) {
    const { deps, calls } = fake();
    const r = await handleDeleteAccount(req(good, body), deps);
    assert.equal(r.status, status, JSON.stringify(body));
    assert.deepEqual(mut(calls), []);
  }
});

test("a site-banned account cannot delete itself", async () => {
  const { deps, calls } = fake({}, { banned: true });
  const r = await handleDeleteAccount(req(good, okBody), deps);
  assert.equal(r.status, 403);
  assert.deepEqual(mut(calls), []);
});

test("every table step, both buckets and the auth user are issued, in that order", async () => {
  const { deps, calls } = fake();
  const r = await handleDeleteAccount(req(good, okBody), deps);
  assert.equal(r.status, 200);
  const j = await r.json();
  assert.equal(j.ok, true);
  const m = mut(calls);
  for (const s of STEPS) {
    const q = `${s.table}?${s.filter(UID)}`;
    const want = s.kind === "delete" ? `DELETE ${q}` : `PATCH ${q} ${JSON.stringify(s.patch)}`;
    assert.ok(m.includes(want), `missing ${want}`);
  }
  for (const b of BUCKETS) assert.ok(m.includes(`purge ${b}/${UID}`));
  assert.equal(m[m.length - 1], `auth.delete ${UID}`);
  assert.ok(m.indexOf(`DELETE profiles?id=eq.${UID}`) < m.indexOf(`purge chat/${UID}`));
  assert.equal(j.steps.length, STEPS.length + BUCKETS.length + 1);
});

test("STEPS covers every user-owned table in docs/sql (guards against drift)", async () => {
  const fs = await import("node:fs");
  const path = await import("node:path");
  const dir = path.resolve(import.meta.dirname!, "../../../docs/sql");
  const tables = new Set<string>();
  for (const f of fs.readdirSync(dir)) {
    const t = fs.readFileSync(path.join(dir, f), "utf8");
    for (const m of t.matchAll(/create table if not exists public\.(\w+)\s*\(([\s\S]*?)\n\);/g))
      if (/references auth\.users/.test(m[2])) tables.add(m[1]);
  }
  const doc = fs.readFileSync(path.resolve(dir, "../supabase-backend.md"), "utf8");
  for (const m of doc.matchAll(/create table if not exists public\.(\w+)\s*\(([\s\S]*?)\n\);/g))
    if (/references auth\.users/.test(m[2])) tables.add(m[1]);
  const covered = new Set(STEPS.map((s) => s.table));
  // site_bans is guarded (refuse) instead of deleted; party_members etc. are listed.
  const missing = [...tables].filter((t) => !covered.has(t) && t !== "site_bans");
  assert.deepEqual(missing, []);
});

test("partial failure names the failing step, stops, and keeps the auth user", async () => {
  const failing = STEPS[10]; // an arbitrary middle step
  const { deps, calls } = fake({
    async rest(method, path, body) {
      calls.push(`${method} ${path}${body === undefined ? "" : " " + JSON.stringify(body)}`);
      if (path.startsWith("profiles?select=username")) return { ok: true, status: 200, body: [{ username: "Alice" }] };
      if (path.startsWith("site_bans?select")) return { ok: true, status: 200, body: [] };
      if (path === `${failing.table}?${failing.filter(UID)}`) return { ok: false, status: 500 };
      return { ok: true, status: 204 };
    },
  });
  const r = await handleDeleteAccount(req(good, okBody), deps);
  assert.equal(r.status, 500);
  const j = await r.json();
  assert.equal(j.failed_step, failing.id);
  assert.deepEqual(j.completed, STEPS.slice(0, 10).map((s) => s.id));
  assert.ok(!calls.some((c) => c.startsWith("auth.delete")), "user is kept so the person can retry");
  assert.ok(!calls.some((c) => c.startsWith("purge")));
});

test("auth-user deletion failure is reported as step auth_user", async () => {
  const { deps } = fake({ async deleteAuthUser() { return { ok: false, error: "HTTP 500" }; } });
  const j = await (await handleDeleteAccount(req(good, okBody), deps)).json();
  assert.equal(j.failed_step, "auth_user");
});

test("storage failure is reported with the bucket", async () => {
  const { deps } = fake({ async purgeBucket(b) { return b === "workshop" ? { ok: false, removed: 0, error: "boom" } : { ok: true, removed: 1 }; } });
  const j = await (await handleDeleteAccount(req(good, okBody), deps)).json();
  assert.equal(j.failed_step, "storage.workshop");
});

test("tables that were never created are skipped, not failures", async () => {
  const { deps } = fake({
    async rest(method, path) {
      if (path.startsWith("profiles?select=username")) return { ok: true, status: 200, body: [{ username: "Alice" }] };
      if (path.startsWith("site_bans?select")) return { ok: false, status: 404, missing: true };
      if (path.startsWith("follows?")) return { ok: false, status: 404, missing: true };
      return { ok: true, status: 204 };
    },
  });
  const r = await handleDeleteAccount(req(good, okBody), deps);
  assert.equal(r.status, 200);
  const j = await r.json();
  assert.equal(j.steps.find((s: { step: string }) => s.step === "follows.followers").status, "skipped");
});
