// Supabase Edge function: issue an OkaySpace session ticket (anti-cheat P2).
//
//   POST /functions/v1/session-ticket
//   Authorization: Bearer <the player's Supabase access token>
//   body: {"aud": "<game server id>", "cnf": "<optional key binding>",
//          "hw_hash": "<optional machine hash>"}
//   -> 200 {"ticket": "okt1....", "exp": <unix seconds>}
//   -> 403 {"error": "banned"}   the account (or that machine) has an active anti-cheat ban
//
// The player's login is checked with Supabase Auth, then their anti-cheat bans
// (public.ac_is_banned, docs/sql/anticheat.sql): a banned player gets no ticket,
// so no ticket-checking server lets them in. The ticket names their account id
// and the server it's for, and lives OKAY_TICKET_TTL seconds. With "cnf"
// (NetworkManager::TicketBinding(): base64url SHA-256 of the client's session
// key) the ticket works only for that one connection. Game servers verify it
// with the PUBLIC key only (`ticketKey` in .okayac); they never need a
// Supabase key.
//
// Secrets (set by the project owner, never committed; see docs/anticheat.md):
//   OKAY_TICKET_SEED        base64url 32-byte ed25519 seed (the private key)
//   OKAY_TICKET_PUBLIC_KEY  base64url 32-byte ed25519 public key (= ticketKey)
//   OKAY_TICKET_TTL         optional, seconds (default 600, at most 3600)
// SUPABASE_URL, SUPABASE_ANON_KEY and SUPABASE_SERVICE_ROLE_KEY are provided by
// the Edge runtime; the service key is used only for the ban lookup and never
// leaves the function.

import { importSigningKey, KEY_BINDING, MAX_LIFETIME_SECONDS, signTicket } from "./ticket.ts";

const AUD = /^[A-Za-z0-9._:-]{1,64}$/; // a concrete server id ("*" tickets aren't issued here)
const HW = /^[0-9a-f]{64}$/;            // shared/AntiCheatHardware.hpp's machine hash

function json(status: number, body: unknown): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json", "cache-control": "no-store" },
  });
}

let keyPromise: Promise<CryptoKey> | null = null;
function signingKey(): Promise<CryptoKey> {
  if (!keyPromise) {
    const seed = Deno.env.get("OKAY_TICKET_SEED"), pub = Deno.env.get("OKAY_TICKET_PUBLIC_KEY");
    if (!seed || !pub) return Promise.reject(new Error("ticket key secrets are not set"));
    keyPromise = importSigningKey(seed, pub);
    keyPromise.catch(() => { keyPromise = null; });
  }
  return keyPromise;
}

async function userIdFor(accessToken: string): Promise<string | null> {
  const url = Deno.env.get("SUPABASE_URL"), anon = Deno.env.get("SUPABASE_ANON_KEY");
  if (!url || !anon) throw new Error("Supabase environment is missing");
  const r = await fetch(`${url}/auth/v1/user`, {
    headers: { apikey: anon, authorization: `Bearer ${accessToken}` },
  });
  if (!r.ok) return null;
  const user = await r.json().catch(() => null);
  return typeof user?.id === "string" ? user.id : null;
}

// Does this account, or the machine it says it's on, have an active anti-cheat
// ban? The hash comes from the player, so it's only a hint: it can find a
// machine ban, never hide an account ban. A project without anticheat.sql has
// no ac_is_banned and nobody is banned; any other failure refuses the ticket.
async function isBanned(sub: string, hw: string | null): Promise<boolean> {
  const url = Deno.env.get("SUPABASE_URL"), key = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY");
  if (!url || !key) throw new Error("Supabase environment is missing");
  const r = await fetch(`${url}/rest/v1/rpc/ac_is_banned`, {
    method: "POST",
    headers: { apikey: key, authorization: `Bearer ${key}`, "content-type": "application/json" },
    body: JSON.stringify({ p_user: sub, p_hw_hash: hw }),
  });
  if (r.status === 404) {
    const e = await r.json().catch(() => null);
    if (e?.code === "PGRST202") return false;
  }
  if (!r.ok) throw new Error(`ban lookup failed (${r.status})`);
  return (await r.json()) === true;
}

Deno.serve(async (req: Request) => {
  if (req.method !== "POST") return json(405, { error: "use POST" });
  const auth = req.headers.get("authorization") ?? "";
  const m = /^Bearer\s+(\S+)$/i.exec(auth);
  if (!m) return json(401, { error: "sign in first" });

  let body: { aud?: unknown; cnf?: unknown; hw_hash?: unknown } | null;
  try { body = await req.json(); } catch { return json(400, { error: "body must be JSON" }); }
  const aud = body?.aud, cnf = body?.cnf, hwIn = body?.hw_hash;
  if (typeof aud !== "string" || !AUD.test(aud)) return json(400, { error: "aud must be a game server id" });
  if (cnf !== undefined && (typeof cnf !== "string" || !KEY_BINDING.test(cnf)))
    return json(400, { error: "cnf must be the client's key binding" });
  const hw = typeof hwIn === "string" && HW.test(hwIn) ? hwIn : null;

  let sub: string | null;
  try { sub = await userIdFor(m[1]); } catch { return json(500, { error: "auth check failed" }); }
  if (!sub) return json(401, { error: "sign in first" });

  try { if (await isBanned(sub, hw)) return json(403, { error: "banned" }); }
  catch { return json(503, { error: "ban check failed" }); }

  const ttl = Math.min(MAX_LIFETIME_SECONDS, Math.max(30, Number(Deno.env.get("OKAY_TICKET_TTL") ?? 600) || 600));
  const iat = Math.floor(Date.now() / 1000);
  try {
    const ticket = await signTicket({ sub, aud, iat, exp: iat + ttl, cnf: cnf as string | undefined }, await signingKey());
    return json(200, { ticket, exp: iat + ttl });
  } catch {
    return json(500, { error: "could not issue a ticket" });
  }
});
