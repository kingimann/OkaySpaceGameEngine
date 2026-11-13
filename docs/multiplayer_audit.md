# Multiplayer audit

A read-through of the networking stack (`engine/include/okay/Net`, `engine/src/Net`,
`relay/`, the launcher's join / voice code) against the open issues, ranked by what a
player would notice. It says what works, what was wrong, what was fixed here, and what
is still open. Where something could not be run or checked, it says so.

## What exists and works

- Transport: UDP with optional libsodium encryption, a sealed join (key exchange
  before any game data), pinned host keys, per-peer rate limits and anti-cheat checks.
- Reliable messages, per-channel reliable-ordered and unreliable-sequenced streams
  (`NetChannels`), fragmentation of large payloads, a delta-compressed snapshot stream
  with acked baselines (`NetDelta`), interest management, rooms, lag compensation,
  client prediction with reconciliation and remote interpolation.
- NAT traversal through the relay (`relay/`, `RelayServer`), join links, the server
  browser, matchmaking, session tickets, the dedicated server, and launcher voice chat.
- A network simulator (`NetSim`) and many suites (`test_net_*`, `test_relay_security`).

## Fixed on this branch

Ranked by player-visible impact. Each has a regression test in
`tests/test_net_multiplayer_fixes.cpp` unless noted.

1. **A client never noticed a dead host.** The host dropped silent clients after five
   seconds, but a client whose host crashed or lost its connection stayed "connected"
   forever: frozen world, no error, no way for a game to react. A joined client now
   leaves the session after `serverTimeoutSeconds` (default 10 s; the client pings
   once a second and the host answers, so a live host is never that quiet) and sets
   `ConnectionLost()` / `ConnectionLostReason()`. A direct join to a host that never
   answers now gives up after `joinTimeoutSeconds` (default 15 s) with
   `JoinRejected()` and the reason "the host did not answer"; before, it retried
   forever. Both can be set to 0 to opt out. A long stall on the client's own machine
   does not count: datagrams that queued meanwhile are read first.
2. **`SendOrdered` silently lost messages past 32 in flight (#745).** "Reliable
   ordered" dropped every message beyond the 32-message window, with no error, even
   with no packet loss: a loot burst, chat flood or level-load sync lost its tail, and
   under loss it was worse than plain `SendReliable`. The window is still 32 on the
   wire, but messages beyond it now wait in a per-channel backlog (4096 messages or
   8 MiB, whichever comes first) and go out in order as acks free room. Only a runaway
   sender that fills the backlog is refused.
3. **Relay: a spoofed re-hello kept peers alive forever (#734).** A hello from an
   address that was already registered skipped the rate limit and the address cookie,
   so anyone who knew a peer's address could keep a dead peer in the table, flip its
   protocol version, take a vacant host slot or make the relay reply to a victim. A
   re-hello is now rate-limited, must carry this address's cookie (else the relay
   answers with a cookie, which a real peer picks up and retries), and a peer that
   proved its address cannot be re-helloed at an older version.
4. **Delta snapshots overflowed at the ends of the position range (#741).** Encoding a
   jump across the whole saturated range wrapped to a small delta (a wrong position,
   not just undefined behaviour); decoding a delta on a saturated baseline overflowed
   an `int`, reachable from an untrusted datagram. Both now use 64-bit arithmetic: the
   encoder sends a keyframe, the decoder clamps.
5. **Relay hygiene (part of #739).** A session code is peer-chosen and went into a log
   line verbatim (log forging): it is now escaped (`RelayLogSafe`). The relay's port
   argument was parsed with `atoi` and a cast (`70000` became `4464`, `abc` became 0):
   it is now range-checked and the relay exits with an error on a bad one. When a
   protected session's host moved to a new address, the per-address session count
   stayed charged to the old address; it now follows the host.

6. **No automatic reconnect or state resume.** A dropped session was reported
   (`ConnectionLost()`) but nothing rejoined, and a player who came back was a stranger:
   new peer id, score zero, no ready flag, position unknown to the move check. With
   `autoReconnect` on (off by default) a client that lost its host retries with bounded
   attempts and a doubling delay (`reconnectMaxAttempts`, `reconnectDelaySeconds`,
   `reconnectMaxDelaySeconds`); a refusal that cannot change ends it at once. The host keeps
   a dropped client's state for `resumeGraceSeconds` under a secret token its Welcome
   gave it, and a rejoin with that token gets the same peer id, score, ready flag and last
   position back; a connection the host has not yet noticed is dead is replaced, even on a
   full host; a resent Welcome (the first was lost) still says the state was resumed. Objects the player made with `SpawnOwned` are announced again. Tests:
   `tests/test_net_reconnect.cpp` (direct, relay, encrypted, parked, no-grace, bounded tries,
   hard refusal, cancel) and `tests/test_net_rejoin.cpp` (single-use, account-bound, not after
   a kick, bounded parked states).
7. **Respawns were not replicated.** The host respawned a player; other peers' copies stayed
   dead (a client-side copy of a Health Stat with respawn on also ran its own timer, at its own
   start point and on its own clock). The host now sends the respawn (health and position,
   reliable `__respawn`) when a Health Stat respawns or is revived; the copy comes back and
   snaps there (no glide, nothing older in the interpolation buffer), a client copy that died by
   the host's word waits for it, the owner's next position is allowed to jump once, and a client
   cannot forge one. Test: `tests/test_net_respawn_anim.cpp` (fails before, passes after).
8. **Dive, hit, vault and emotes were not sent to other players.** Death, roll, sit and swim were;
   the rest played only on the owner. The owner's Character now carries a one-shot event
   (`netEventSeq` / `netEventId` / `netEventParam`) appended to the animation sync message
   (an older peer stops reading before it); each proxy plays a new sequence once, with unknown
   ids and non-finite values dropped and out-of-range ones clamped. Hit direction and heavy,
   the traverse clip (vault, mantle low/high, ledge grab, climb up, let go) and the emote
   index travel with it. `HumanoidActions::PlayTraverse` plays a traverse clip without a probe.
   Test: `tests/test_net_respawn_anim.cpp`. (Two events inside one sync interval: see 15.)
9. **Launcher voice clips leaked their sound buffer (#718).** The buffer `SDL_LoadWAV` returns
   was never freed, and a clip converted to 16-bit dropped its only pointer to it. The clip
   now copies the samples into its own storage and frees SDL's buffer at once, so no path can
   leave it behind. The launcher target compiles; the UI was not run (see below).
10. **NetSim shared one queue between all sockets.** Held and delayed datagrams were not tied to
    a socket, so with two managers in a process (a test, a listen server with a local client) one
    manager's delayed packets were sent through the other's socket, and `Stop()` dropped
    everybody's. They are now keyed by socket. Test: `tests/test_net_lossy.cpp`.
11. **A player who left stayed on the others' screens.** A client never removed the stand-in
    it made for a remote peer (`SetRemoteFactory`); it froze where the player left. The roster the
    host sends every second now prunes stand-ins of peers it no longer lists (after two rosters
    without them, so a late, reordered one cannot remove someone who just joined). Test:
    `tests/test_net_rejoin.cpp`.

12. **Relay clients sent hellos a relay could not trust (#733, step 1).** The launcher's voice
    calls sent a version-3 hello with no welcome nonce (so a forged welcome could pair or end a
    call) and fell back to the unauthenticated version-1 hello after only three unanswered
    seconds, which a lossy link can cause. The game and the voice calls now build every hello
    with one helper (`okay/Net/RelayHello.hpp`: version 4, cookie, nonce) and check every
    welcome with it (the nonce must be echoed); the voice call falls back to version 1 only after
    six unanswered hellos and says "update the launcher" when a relay refuses it. The relay
    counts hellos per version and registered legacy peers (`HellosByVersion()`, `LegacyPeers()`),
    logged once a minute while legacy hellos arrive. The relay default is unchanged (legacy hellos
    stay on): flipping it is step 2, after players have updated and the log shows no legacy peer.
    Tests: `tests/test_net_relay_clients.cpp` (a relay with legacy off serves a voice-style client and
    a game host and client, with zero legacy hellos; forged and un-echoed welcomes are refused).
    The launcher's `VoiceChat.hpp` change was compiled, not run.
13. **Resume token: what is guaranteed.** The token is a bearer secret (64 random bits, single use,
    bound to the account when signed in). In a sealed (encrypted) session the Welcome that carries it
    and the Join that presents it are authenticated-encrypted, so it is never in a datagram in the
    clear, and a captured rejoin replayed from another address resumes nothing and evicts nobody. On
    an unencrypted session there is no key to bind it to: anyone who can read the datagrams has it, as
    they have the session cookie and every other byte. A keyed proof cannot fix that, so the option
    `resumeRequiresEncryption` (off by default) makes the host hand a token only to a sealed join;
    an unencrypted client then gets none and rejoins as a new peer. An off-path sender cannot guess a
    token, and a spent or invented one gives a new peer with none of the old state. Tests:
    `tests/test_net_resume_token.cpp` (wire capture through a recording UDP middlebox).
14. **Reliable messages in flight were lost when a session dropped.** A reliable or ordered message the
    peer had not acked at the drop vanished with the session. Both ends now keep them (a client its
    own, the host a dropped client's with its parked state) and send them again once the rejoin is a
    resume, oldest first and ordered channels in order, bounded to 256 messages and 256 KiB each way;
    what is cut is counted (`ResendDropped()`). A rejoin that is not a resume is a new peer and gets
    none. They go out as new messages, so one the peer had already received whose ack was lost
    arrives twice: reliable handlers should be idempotent. Tests: `tests/test_net_resume_resend.cpp`
    (both directions, ordering, both bounds, no-resume and give-up cases; run against a NetSim
    blackhole so every arrival is the re-send).

15. **Two one-shot animation events inside one sync interval dropped the older.** The owner's
    Character kept one event (`netEventSeq` / `Id` / `Param`), so two that started between two sync
    sends played only the newest on other peers. `Character::RecordNetEvent` now keeps the last four,
    the animation message carries the three before the newest (an older peer stops reading before
    them; garbage or a count over three drops the group), and a proxy queues what it has not played
    (at most four, the oldest dropped past that) and plays them in order, 0.35 s apart, each after
    the animator is back in locomotion (or 1.5 s). A single event still plays at once, and the
    repeated reports of the same state do not replay. A game that bumps `netEventSeq` by hand still
    sends the newest only. Tests: `tests/test_net_respawn_anim.cpp` (both play in order; six in one
    frame play the last four; parsing).
16. **Deaths no replicated hit caused were not replicated.** Only the respawn was. Every death on the
    host now goes through `NetNoteDeath`: the host sends a reliable `__death` (where the blow came
    from, the object's id) and the copy dies the same way a death by the server's health does and
    waits for the server's respawn. A death a replicated hit caused is not sent twice (the hit's
    `__dmg` carries it); a client cannot send one, and bad numbers or unknown objects are ignored.
    Tests: `tests/test_net_respawn_anim.cpp` (script kill, kill volume with direction, forged and
    malformed messages, a hit-caused death sends none).

17. **A client whose address changed mid-session on a relay hung for 10 s.** The relay knows a peer by its
    source address, so a NAT rebinding (or a phone changing network) makes a new peer with a new slot,
    and the host still lists the old slot, which now leads nowhere. The client only noticed after the
    silence timeout. It now reads its own slot from every welcome and, when it changes, reports the
    connection lost at once ("this connection's address changed on the relay"); with `autoReconnect` it
    resumes through the new address (same peer id and score) in about two seconds instead of eleven. A host
    whose address changes needed no fix: the relay keeps its slot (its host key proves it) and clients
    notice nothing. Tests: `tests/test_net_relay_nat.cpp` (a NAT middlebox that swaps the socket it
    sends from, clean and under NetSim's 20% loss, delay and duplication: host move, client move with and
    without autoReconnect, and a direct session, where nothing can tell the client and the 10 s silence
    timeout applies).

Also covered by new tests with no bug found: loss, reordering and duplication through `NetSim`
(`tests/test_net_lossy.cpp`: exactly-once reliable, in-order ordered, never-backwards sequenced,
join, convergence of a synced object and a resume on a bad link), a deterministic fuzz of every
decoder that parses network bytes plus random and mutated datagrams at a live host, client and
relay (`tests/test_net_fuzz.cpp`), and replayed Leave / duplicated Join datagrams
(`tests/test_net_rejoin.cpp`).

Also merged from `claude/fix-net-issues`: the relay protocol v4 welcome nonce and
timeout, the refusal codes, and the little-endian `Packet` (#701, #702, #704, #705,
#721) with their tests (`net_relay_hardening`, `net_packet_layout`).

## Still wrong or missing, ranked

1. **Legacy (v1/v2) relay hellos are still accepted by default (#733).** They carry no
   address cookie, so an off-path attacker can register spoofed peers and squat session
   codes. Step 1 is done (every client built here sends version-4 hellos, and the relay logs
   legacy peers and per-version counts); turning the default off is a rollout decision for
   when older games and launchers already out there have updated (`LegacyPeers()` zero),
   and needs a release note, not a silent change. `--legacy-hello=off` already refuses them.
2. **Build-mode placements and quest state are not synced**
   (`docs/unfinished_engine.md`, section 11 and neighbours). Players still diverge in games that
   rely on these. A player who joins while an object is dead sees it alive until its next respawn
   (`__death` and `__respawn` go to the peers connected at the time).
3. **Reconnect limits.** Messages in flight when the session dropped are re-sent after a resume
   (bounded, may arrive twice; see 14 above), not after a rejoin that is no resume. A rejoin that gets no resume (grace over, host restarted) is a
   new peer: objects made with `SpawnOwned` are not re-announced under the new id, the game
   respawns them (`Resumed()` says which case it is). Without encryption the resume token
   travels in the clear, like the rest of an unencrypted session (`resumeRequiresEncryption`
   turns it off there; see 13 above).
4. **Data races in `ServerBrowser::Game` and the voice session (PR #700,
   `claude/fix-thread-races`).** Already written and left to that branch so the two do
   not conflict.
5. **No Web networking** (browser builds have no matchmaking, server browser or
   transport) and **no replay or spectator stream**.

## What could not be verified

- Windows and macOS: everything here was built and run on Linux only. Winsock-specific
  behaviour (socket errors, `near`/`far` macros) is untested; the new code avoids those
  macro names and adds no `<mutex>` / `<thread>` to public headers.
- The launcher (server browser, matchmaking, party and invite flows, voice) was read,
  not run: the launcher target needs SDL2 windowing and live services (Supabase
  tables under `docs/sql`) that are not available here. The voice-clip fix (#718) compiles
  (the launcher's `main.cpp` builds) but playing a voice message was not run; no other bug
  was fixed there beyond what PR #700 already covers.
- Behaviour on a real WAN: loss, delay, reordering and duplication were simulated on loopback
  with `NetSim` (20% loss, 15% duplication, 30 ms +/- 25 ms), not measured on a real link.
  The reconnect delays are tested with a hung host (the client's clock advanced faster than
  the wall clock), not with a real network outage or a changing NAT address.
- Reconnect over a relay was tested against the in-process relay only; a real relay that
  changes a client's slot between sessions was not seen. Address changes were simulated with a
  middlebox that swaps its sending socket (a new source port); a real NAT, a change of IP address
  (Wi-Fi to mobile data), carrier-grade NAT behaviour and a relay behind a load balancer were not.
- Animation replication was checked by the state the proxy's animator reaches, not by looking
  at the pose on screen.
