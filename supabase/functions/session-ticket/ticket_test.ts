// Self-test for ticket.ts, runs on Deno (`deno run ticket_test.ts`) or Node 22+
// (`node --experimental-strip-types ticket_test.ts`). A throwaway key pair is
// made at run time; the engine side of the contract is checked by
// tests/test_net_anticheat_p2.cpp (it verifies tickets this code signed).
import { base64UrlDecode, base64UrlEncode, importSigningKey, signTicket, TICKET_PREFIX } from "./ticket.ts";

let failures = 0;
function check(ok: boolean, what: string) { if (!ok) { failures++; console.error("FAIL:", what); } }
async function throws(f: () => Promise<unknown>): Promise<boolean> { try { await f(); return false; } catch { return true; } }

const kp = await crypto.subtle.generateKey({ name: "Ed25519" }, true, ["sign", "verify"]) as CryptoKeyPair;
const jwk = await crypto.subtle.exportKey("jwk", kp.privateKey);
const key = await importSigningKey(jwk.d!, jwk.x!);
const iat = 1800000000;

const t = await signTicket({ sub: "user-1", aud: "arena-1", iat, exp: iat + 600 }, key);
const [prefixAndPayload, sigPart] = [t.slice(0, t.lastIndexOf(".")), t.slice(t.lastIndexOf(".") + 1)];
check(t.startsWith(TICKET_PREFIX), "prefix");
check(new TextDecoder().decode(base64UrlDecode(prefixAndPayload.slice(TICKET_PREFIX.length))) ===
      "sub=user-1&aud=arena-1&iat=1800000000&exp=1800000600", "payload text");
const ok = await crypto.subtle.verify({ name: "Ed25519" }, kp.publicKey, base64UrlDecode(sigPart),
                                      new TextEncoder().encode(prefixAndPayload));
check(ok, "signature verifies with the public key");
check(!/[=+/]/.test(t), "base64url, no padding");
const cnf = base64UrlEncode(new Uint8Array(32).fill(7));
const bound = await signTicket({ sub: "user-1", aud: "arena-1", iat, exp: iat + 600, cnf }, key);
check(new TextDecoder().decode(base64UrlDecode(bound.slice(TICKET_PREFIX.length, bound.lastIndexOf(".")))).endsWith(`&cnf=${cnf}`),
      "cnf in the payload");
check(await throws(() => signTicket({ sub: "a", aud: "x", iat, exp: iat + 10, cnf: "short" }, key)), "malformed cnf refused");

check(await throws(() => signTicket({ sub: "a&aud=*", aud: "x", iat, exp: iat + 10 }, key)), "unsafe sub refused");
check(await throws(() => signTicket({ sub: "a", aud: "x=y", iat, exp: iat + 10 }, key)), "unsafe aud refused");
check(await throws(() => signTicket({ sub: "a", aud: "x", iat, exp: iat }, key)), "empty lifetime refused");
check(await throws(() => signTicket({ sub: "a", aud: "x", iat, exp: iat + 2 * 3600 }, key)), "long lifetime refused");
check(await throws(() => importSigningKey("AAAA", jwk.x!)), "short seed refused");
for (let n = 0; n < 40; n++) {
  const b = Uint8Array.from({ length: n }, (_, i) => (i * 37 + 11) & 255);
  check(base64UrlEncode(base64UrlDecode(base64UrlEncode(b))) === base64UrlEncode(b), `base64url round trip ${n}`);
}
console.log(failures ? `${failures} failures` : "ticket.ts: all checks passed");
if (failures) throw new Error("ticket_test failed");
