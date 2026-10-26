// Make the ticket service's ed25519 key pair. The OWNER runs this on their own
// machine (Deno: `deno run make_key.ts`, or Node 22+:
// `node --experimental-strip-types make_key.ts`), then:
//   supabase secrets set OKAY_TICKET_SEED=<seed> OKAY_TICKET_PUBLIC_KEY=<public>
// and puts <public> in each game server's .okayac as `ticketKey`.
// The seed is the private key: never commit it, paste it into chat, or ship it.
const kp = await crypto.subtle.generateKey({ name: "Ed25519" }, true, ["sign", "verify"]) as CryptoKeyPair;
const jwk = await crypto.subtle.exportKey("jwk", kp.privateKey);
console.log(`OKAY_TICKET_SEED=${jwk.d}`);
console.log(`OKAY_TICKET_PUBLIC_KEY=${jwk.x}`);
