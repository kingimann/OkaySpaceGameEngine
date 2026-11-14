// Supabase Edge Function: delete the CALLER's account and data.
//
//   POST /functions/v1/delete-account
//   Authorization: Bearer <the player's Supabase access token>
//   body: {"confirm_username": "...", "password": "..."}
//
// See delete_account.ts for the contract and docs/account_data.md for what is
// deleted vs anonymised and the owner deploy steps. SUPABASE_URL,
// SUPABASE_ANON_KEY and SUPABASE_SERVICE_ROLE_KEY come from the Edge runtime;
// the service key is used only in this file and never leaves the function.

import { type Deps, handleDeleteAccount, type Rest } from "./delete_account.ts";

function env(name: string): string {
  const v = Deno.env.get(name);
  if (!v) throw new Error(`${name} is not set`);
  return v;
}

function realDeps(): Deps {
  const url = env("SUPABASE_URL").replace(/\/$/, "");
  const anon = env("SUPABASE_ANON_KEY");
  const svc = env("SUPABASE_SERVICE_ROLE_KEY");
  const svcHeaders = { apikey: svc, authorization: `Bearer ${svc}`, "content-type": "application/json" };

  return {
    async userFromJwt(jwt) {
      const r = await fetch(`${url}/auth/v1/user`, { headers: { apikey: anon, authorization: `Bearer ${jwt}` } });
      if (!r.ok) return null;
      const u = await r.json().catch(() => null);
      return typeof u?.id === "string" ? { id: u.id, email: typeof u.email === "string" ? u.email : null } : null;
    },
    async passwordOk(email, password) {
      const r = await fetch(`${url}/auth/v1/token?grant_type=password`, {
        method: "POST",
        headers: { apikey: anon, "content-type": "application/json" },
        body: JSON.stringify({ email, password }),
      });
      await r.body?.cancel();
      return r.ok;
    },
    async rest(method, path, body): Promise<Rest> {
      const r = await fetch(`${url}/rest/v1/${path}`, {
        method,
        headers: { ...svcHeaders, prefer: "return=minimal" },
        body: body === undefined ? undefined : JSON.stringify(body),
      });
      const text = await r.text();
      let parsed: unknown = text;
      try { parsed = text ? JSON.parse(text) : null; } catch { /* keep text */ }
      // Table / column not created in this project (PGRST205, 42P01, 42703).
      const code = (parsed as { code?: string } | null)?.code ?? "";
      const missing = !r.ok && (code === "PGRST205" || code === "42P01" || code === "42703" || code === "PGRST204");
      return { ok: r.ok, status: r.status, missing, body: parsed };
    },
    async purgeBucket(bucket, uid) {
      // List recursively under "<uid>/" and remove in batches.
      const files: string[] = [];
      const walk = async (prefix: string, depth: number): Promise<number> => {
        for (let offset = 0; ; offset += 100) {
          const r = await fetch(`${url}/storage/v1/object/list/${bucket}`, {
            method: "POST", headers: svcHeaders,
            body: JSON.stringify({ prefix, limit: 100, offset }),
          });
          if (r.status === 404 || r.status === 400) return 404;
          if (!r.ok) return r.status;
          const items = await r.json() as Array<{ name: string; id: string | null }>;
          for (const it of items) {
            if (it.id === null) { if (depth < 5) { const s = await walk(`${prefix}${it.name}/`, depth + 1); if (s) return s; } }
            else files.push(`${prefix}${it.name}`);
          }
          if (items.length < 100) return 0;
        }
      };
      const st = await walk(`${uid}/`, 0);
      if (st === 404) return { ok: true, missing: true, removed: 0 };
      if (st) return { ok: false, removed: 0, error: `list failed (HTTP ${st})` };
      for (let i = 0; i < files.length; i += 100) {
        const batch = files.slice(i, i + 100);
        const r = await fetch(`${url}/storage/v1/object/${bucket}`, {
          method: "DELETE", headers: svcHeaders, body: JSON.stringify({ prefixes: batch }),
        });
        if (!r.ok) return { ok: false, removed: i, error: `remove failed (HTTP ${r.status})` };
      }
      return { ok: true, removed: files.length };
    },
    async deleteAuthUser(uid) {
      const r = await fetch(`${url}/auth/v1/admin/users/${uid}`, { method: "DELETE", headers: svcHeaders });
      await r.body?.cancel();
      return r.ok ? { ok: true } : { ok: false, error: `HTTP ${r.status}` };
    },
  };
}

Deno.serve(async (req) => {
  try {
    return await handleDeleteAccount(req, realDeps());
  } catch (e) {
    console.error("delete-account:", e);
    return new Response(JSON.stringify({ error: "server is not configured" }), {
      status: 500, headers: { "content-type": "application/json" },
    });
  }
});
