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

Also merged from `claude/fix-net-issues`: the relay protocol v4 welcome nonce and
timeout, the refusal codes, and the little-endian `Packet` (#701, #702, #704, #705,
#721) with their tests (`net_relay_hardening`, `net_packet_layout`).

## Still wrong or missing, ranked

1. **Legacy (v1/v2) relay hellos are still accepted by default (#733).** They carry no
   address cookie, so an off-path attacker can register spoofed peers and squat session
   codes. Turning the default off is a rollout decision (older games and the launcher's
   voice calls speak them); the relay logs how many it sees (`LegacyHellos()`), and
   `--legacy-hello=off` already refuses them. Left as it is on purpose; it needs a
   release note, not a silent change.
2. **Respawns are not replicated**, and **dive, hit, vault and emotes are not sent to
   other players**; build-mode placements and quest state are not synced
   (`docs/unfinished_engine.md`, section 11 and neighbours). Players see each other
   diverge in games that use these.
3. **The client does not reconnect.** With this branch a dropped session is reported
   (`ConnectionLost()`), but nothing rejoins automatically or keeps the player's state;
   a game has to call `StartClient` again.
4. **Voice-clip memory leak in the launcher (#718):** `SDL_LoadWAV` buffers are never
   freed. Not touched here: the launcher UI is not built or testable in this
   environment.
5. **Data races in `ServerBrowser::Game` and the voice session (PR #700,
   `claude/fix-thread-races`).** Already written and left to that branch so the two do
   not conflict.
6. **No Web networking** (browser builds have no matchmaking, server browser or
   transport) and **no replay or spectator stream**.

## What could not be verified

- Windows and macOS: everything here was built and run on Linux only. Winsock-specific
  behaviour (socket errors, `near`/`far` macros) is untested; the new code avoids those
  macro names and adds no `<mutex>` / `<thread>` to public headers.
- The launcher (server browser, matchmaking, party and invite flows, voice) was read,
  not run: the launcher target needs SDL2 windowing and live services (Supabase
  tables under `docs/sql`) that are not available here. No bug was fixed there beyond
  what PR #700 already covers.
- Behaviour on a real lossy WAN: the timeouts are tested over loopback with a mute or
  stalled peer, and the ordered backlog with a simulated lossless link, not with
  `NetSim` at high loss.
- Sanitizer runs: the delta overflow is shown by a wrong result before the fix, not by
  an UBSan report.
