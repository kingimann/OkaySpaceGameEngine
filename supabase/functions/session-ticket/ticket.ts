// Session tickets for OkaySpace game servers (anti-cheat P2): the signing half.
// The engine's SessionTicket::Verify (engine/src/Net/SessionTicket.cpp) is the
// other half; the two must agree byte for byte:
//
//   okt1.<payload>.<signature>        (base64url, no padding)
//   payload   = "sub=<user id>&aud=<server id or *>&iat=<unix s>&exp=<unix s>[&cnf=<key binding>]"
//   signature = ed25519 over the ASCII text "okt1.<payload part>"
//
// Web Crypto only (no imports), so the same file runs in Supabase Edge (Deno)
// and in Node for the tests.

export const TICKET_PREFIX = "okt1.";
export const MAX_LIFETIME_SECONDS = 3600; // the engine refuses longer

export function base64UrlEncode(bytes: Uint8Array): string {
  let bin = "";
  for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

export function base64UrlDecode(text: string): Uint8Array {
  if (!/^[A-Za-z0-9_-]*$/.test(text) || text.length % 4 === 1) throw new Error("bad base64url");
  const pad = text.length % 4 === 0 ? "" : "=".repeat(4 - (text.length % 4));
  const bin = atob(text.replace(/-/g, "+").replace(/_/g, "/") + pad);
  return Uint8Array.from(bin, (c) => c.charCodeAt(0));
}

// A claim value can't carry the payload's own separators.
const SAFE_CLAIM = /^[A-Za-z0-9._:@*-]{1,128}$/;

export interface TicketClaims {
  sub: string; // the account id
  aud: string; // the game server id the ticket is for ("*" = any)
  iat: number; // unix seconds
  exp: number; // unix seconds
  cnf?: string; // base64url SHA-256 of the client's session key: only that connection may use it
}

/** A key binding (cnf): base64url of 32 bytes. */
export const KEY_BINDING = /^[A-Za-z0-9_-]{43}$/;

/** The signing key from its 32-byte seed and 32-byte public key (base64url each). */
export async function importSigningKey(seedB64: string, publicB64: string): Promise<CryptoKey> {
  const d = base64UrlDecode(seedB64), x = base64UrlDecode(publicB64);
  if (d.length !== 32 || x.length !== 32) throw new Error("ticket key: seed and public key must be 32 bytes each");
  return await crypto.subtle.importKey(
    "jwk",
    { kty: "OKP", crv: "Ed25519", d: seedB64, x: publicB64 },
    { name: "Ed25519" },
    false,
    ["sign"],
  );
}

export async function signTicket(claims: TicketClaims, key: CryptoKey): Promise<string> {
  if (!SAFE_CLAIM.test(claims.sub) || !SAFE_CLAIM.test(claims.aud)) throw new Error("ticket claims contain unsafe characters");
  if (claims.cnf !== undefined && !KEY_BINDING.test(claims.cnf)) throw new Error("cnf must be a base64url SHA-256");
  if (!Number.isSafeInteger(claims.iat) || !Number.isSafeInteger(claims.exp)) throw new Error("ticket times must be integers");
  if (claims.exp <= claims.iat || claims.exp - claims.iat > MAX_LIFETIME_SECONDS) throw new Error("ticket lifetime is invalid");
  const payload = `sub=${claims.sub}&aud=${claims.aud}&iat=${claims.iat}&exp=${claims.exp}` +
    (claims.cnf !== undefined ? `&cnf=${claims.cnf}` : "");
  const signedPart = TICKET_PREFIX + base64UrlEncode(new TextEncoder().encode(payload));
  const sig = new Uint8Array(await crypto.subtle.sign({ name: "Ed25519" }, key, new TextEncoder().encode(signedPart)));
  return `${signedPart}.${base64UrlEncode(sig)}`;
}
