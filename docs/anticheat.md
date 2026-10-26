# Anti-cheat

OkaySpace's anti-cheat runs on the **server** and is **opt-in**. With it off,
nothing is checked and nothing is sent differently from before.

- **P1 (authority):** the server refuses what a client may not do.
  - moving or despawning another player's networked object;
  - spawning a prefab that isn't on the game's list;
  - writing another object's SyncVars, or flooding SyncVars;
  - moving a NetworkSync object faster than `maxSyncSpeed`.

  Every refusal is reported; repeat offenders are kicked.
- **P2 (identity and transport):** clients must encrypt, every packet is
  numbered and replays are refused, and a joining player can be required to
  hold a **session ticket** signed by your ticket service.

## Turning it on

Add an **Anti-Cheat Config** component to the scene (with a Network Manager), or
set `NetworkManager::anticheat.settings` in code. Its rules can also come from a
`.okayac` file (one `key value` per line, `#` comments):

```
enabled true
spawn prefabs/crate.okayprefab      # repeat for each prefab clients may spawn
maxSyncSpeed 8                      # u/s; 0 = no check
maxSyncVarsPerSecond 30             # 0 = unlimited
kickAfterViolations 5               # refusals within a minute; 0 = never kick
ownerOnlySyncVars true              # "<netId>.<name>" keys are the owner's
# P2
requireEncryption true              # only encrypted clients; numbered packets
ticketKey <base64url ed25519 public key>
ticketAudience arena-1              # this server's id in tickets ("" = any)
```

React in a visual script with **On Cheat Detected**:
- Event Name is what was caught;
- Event Value is the player's peer id;
- the `cheat_detail` variable names the object involved.

Check RPCs with **Net Validator** components (size, rate, host-only), or with an
**On Validate Rpc** graph plus **Reject Rpc**.

## P2: encryption required and replay protection

With `requireEncryption true`:

- The server turns its `encryption` on. A client that doesn't offer a key is
  refused with *"encryption required"*, so set `encryption = true` on the client
  too. Both ends need libsodium; a server built without it refuses every join
  and says so.
- Every sealed packet carries an 8-byte sequence number inside the ciphertext.
  A packet already seen, or more than 64 behind the newest, is dropped. The
  server switches this on through a flag at the end of its Welcome, so an older
  client can't play on such a server; update the game.
- Plaintext game packets from a joined client's address are dropped, because
  they can only be someone else's. Join and server-browser queries stay
  plaintext.

## P2: session tickets

A ticket is a short-lived pass that says *this account may join this server*.

```
okt1.<payload>.<signature>                 (base64url, no padding)
payload   = sub=<user id>&aud=<server id or *>&iat=<unix seconds>&exp=<unix seconds>
signature = ed25519 over the ASCII text "okt1.<payload part>"
```

The server checks it with the service's **public** key (`ticketKey`):
- the signature must match;
- `iat` and `exp` must hold, with ±60 s of clock skew, and the lifetime can be
  at most 24 h;
- `aud` must be this server or `*`.

When the check passes, the player is bound to `sub`: `PeerUserId()` on the
server, and `LocalUserId()` on the client. The client passes the ticket as its
auth token (`NetworkManager::SetAuthToken(ticket)`) before joining. A ticket
check takes the place of `SetTokenVerifier` / `SetJoinCheck`.

The format is implemented in `engine/include/okay/Net/SessionTicket.hpp`.
`SessionTicket::Sign` exists for tests and tools only.

### The ticket service (not deployed; you run these steps)

The service is a small HTTPS endpoint, for example a Supabase Edge function:

1. Check the caller's login (their Supabase access token).
2. Sign `sub=<their user id>&aud=<requested server>&iat=<now>&exp=<now + 10 min>`
   with an ed25519 **private** key, and return the ticket.

**The function's source isn't in the repo yet.** It's waiting for the owner's
go-ahead, because it touches the Supabase project. When it's added, deploying is
yours to do:

1. **Make a key pair on your own machine**, never in CI or in the repo. Any
   ed25519 tool works, as long as the public key ends up as 32 raw bytes in
   base64url. For example, a two-line libsodium or Deno script calling
   `crypto_sign_keypair` / `crypto.subtle.generateKey({name: "Ed25519"})`.
2. **Store the private key as a function secret:**
   `supabase secrets set OKAY_TICKET_SECRET=<base64url secret>`. It never goes
   in a game build, a `.okayac` file or git.
3. **Deploy the function:** `supabase functions deploy session-ticket`.
4. **Put the public key in the server's rules:** `ticketKey <base64url public key>`,
   with `ticketAudience` set to the server's id.
5. **Have the launcher or game fetch a ticket before joining**: call the function
   with the player's access token, then pass the result to `SetAuthToken`. That's
   still to do in the launcher.

To rotate the key, make a new pair, update the secret and every server's
`ticketKey` together, and redeploy. Tickets signed with the old key stop working
when the servers switch.
