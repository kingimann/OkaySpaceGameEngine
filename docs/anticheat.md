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

The source is in `supabase/functions/session-ticket/`:

- **`index.ts`** is the Edge function. It takes
  `POST {"aud": "<game server id>"}` with `Authorization: Bearer <player's
  Supabase access token>`.
  1. It checks the login with Supabase Auth (`/auth/v1/user`).
  2. It returns `{"ticket": "okt1…", "exp": …}`, signed for that account and
     server. The ticket lives `OKAY_TICKET_TTL` seconds: 600 by default, at most
     86400.
- **`ticket.ts`** does the signing, using Web Crypto only. It must match the
  engine's `SessionTicket::Verify`. `tests/test_net_anticheat_p2.cpp` verifies
  tickets this code signed, so a change on either side that breaks the other
  fails CI.
- **`ticket_test.ts`** is its self-test. Run `deno run ticket_test.ts`, or
  `node --experimental-strip-types ticket_test.ts`.
- **`make_key.ts`** makes the key pair. It's for you to run.

Nothing is deployed. To go live:

1. **Make the key pair on your own machine**, never in CI or in the repo:
   `deno run supabase/functions/session-ticket/make_key.ts`. It prints
   `OKAY_TICKET_SEED=…` (the **private** key) and `OKAY_TICKET_PUBLIC_KEY=…`.
2. **Store both as function secrets:**
   `supabase secrets set OKAY_TICKET_SEED=<seed> OKAY_TICKET_PUBLIC_KEY=<public>`.
   The seed never goes in a game build, a `.okayac` file, chat or git.
3. **Deploy the function:** `supabase functions deploy session-ticket`.
   `SUPABASE_URL` and `SUPABASE_ANON_KEY` come from the Edge runtime.
4. **Point each game server at the public key** in its rules:
   `ticketKey <public>` and `ticketAudience <that server's id>`. The id is the
   same `aud` that clients ask for.
5. **Fetch a ticket before joining** (still to do in the launcher or game):
   POST to the function with the player's access token and the server id, then
   `SetAuthToken(ticket)` on the client's NetworkManager.

To rotate the key, run step 1 again, update the secrets and every server's
`ticketKey` together, and redeploy. Tickets signed with the old key stop working
when the servers switch.
