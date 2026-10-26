// Supabase Edge function: issue an OkaySpace session ticket (anti-cheat P2).
//
//   POST /functions/v1/session-ticket
//   Authorization: Bearer <the player's Supabase access token>
//   body: {"aud": "<game server id>"}
//   -> 200 {"ticket": "okt1....", "exp": <unix seconds>}
//
// The player's login is checked with Supabase Auth; the ticket names their
// account id and the server it's for, and lives OKAY_TICKET_TTL seconds.
// Game servers verify it with the PUBLIC key only (`ticketKey` in .okayac).
//
// Secrets (set by the project owner, never committed; see docs/anticheat.md):
//   OKAY_TICKET_SEED        base64url 32-byte ed25519 seed (the private key)
//   OKAY_TICKET_PUBLIC_KEY  base64url 32-byte ed25519 public key (= ticketKey)
//   OKAY_TICKET_TTL         optional, seconds (default 600, at most 86400)
// SUPABASE_URL and SUPABASE_ANON_KEY are provided by the Edge runtime.

import { importSigningKey, MAX_LIFETIME_SECONDS, signTicket } from "./ticket.ts";

const AUD = /^[A-Za-z0-9._:-]{1,64}$/; // a concrete server id ("*" tickets aren't issued here)

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

Deno.serve(async (req: Request) => {
  if (req.method !== "POST") return json(405, { error: "use POST" });
  const auth = req.headers.get("authorization") ?? "";
  const m = /^Bearer\s+(\S+)$/i.exec(auth);
  if (!m) return json(401, { error: "sign in first" });

  let aud: unknown;
  try { aud = (await req.json())?.aud; } catch { return json(400, { error: "body must be JSON" }); }
  if (typeof aud !== "string" || !AUD.test(aud)) return json(400, { error: "aud must be a game server id" });

  let sub: string | null;
  try { sub = await userIdFor(m[1]); } catch { return json(500, { error: "auth check failed" }); }
  if (!sub) return json(401, { error: "sign in first" });

  const ttl = Math.min(MAX_LIFETIME_SECONDS, Math.max(30, Number(Deno.env.get("OKAY_TICKET_TTL") ?? 600) || 600));
  const iat = Math.floor(Date.now() / 1000);
  try {
    const ticket = await signTicket({ sub, aud, iat, exp: iat + ttl }, await signingKey());
    return json(200, { ticket, exp: iat + ttl });
  } catch {
    return json(500, { error: "could not issue a ticket" });
  }
});
