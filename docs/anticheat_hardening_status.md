# Anti-cheat hardening: what is done and what is not

Written 2026-11-16 for the anti-cheat hardening task (branch `claude/anticheat-1`). It lists every item of the
2026-10-26 audit (`ROADMAP.md`, "Anti-cheat hardening, from the 2026-10-26 audit") and every server-side hardening
topic the owner named, says whether each is done on main, on this branch or not done, and points at the proof.
Nothing here ran on Windows or on a real network; "tested" means a headless test on Linux over loopback.

How the status was found: the code and the tests on `origin/main` (de36f7f1) were read. The git history on this
machine is shallow (56 commits), so the PR numbers (#350 to #394) could not be checked with `git log`; the code
symbols below were checked instead.

## Part 1. The 14 audit items

| # | Item | Status | Evidence on main |
|---|---|---|---|
| 1 | Only safe, registered prefabs can be network-spawned | Done on main | `AntiCheat::SpawnAllowed`, `ParseSpawnMessage`, spawn allowlist; `net_anticheat` (`SpawnNotAllowed`) |
| 2 | Host-key pinning can't be skipped by a plaintext Welcome, Reject or Kicked | Done on main | `pinnedHostKey` handling in `NetworkManager`; `net_anticheat_ph3` |
| 3 | Hit claims need a known weapon and are rate-limited | Done on main | `HandleHitClaim`, `kHitClaimsPerSecond`, `weapon` rules; `net_anticheat_ph4`, `anticheat_sim` |
| 4 | Hit origin isn't taken from positions the client can bank | Done on main | `hitOriginTolerance`, quarter-second move cap; `net_anticheat_ph4` |
| 5 | Bans enforced when a join ticket is issued; no service_role key on game servers | Code on main, not deployed | `supabase/functions/session-ticket/index.ts` calls `ac_is_banned`; `anticheat_sql` (local Postgres). The Edge function has never run on the real Supabase project: the owner's step (ROADMAP "Waiting on Iman") |
| 6 | Tickets bound to one server, one client key and one connection | Code on main, not deployed | `SessionTicket` claims `aud` and `cnf` (`KeyBinding`), checked at the join; `ticket_client`, `net_anticheat_p2`. Same deploy wait as 5 |
| 7 | Relay hellos need a cookie; caps per destination | Done on main (live relay needs redeploying) | `RelayHello`, `kRelayCookieBytes`; `net_relay_hardening`, `relay_security` |
| 8 | Budgets and strikes survive a reconnect; rejoin cooldown after a kick | Done on main; cooldown test added here | `AntiCheat::ParkPeer` / `RestorePeer`, `m_kickBans` (60 s doubling to 1 h); parking tested in `cheat_detect`; the kick cooldown had no test, `anticheat_hard` adds one |
| 9 | Join checks: encryption first, rate-limited password and sign-in checks | Done on main | `CountJoinAttempt`, the join gate order in `NetworkManager`; `net_anticheat_p2` |
| 10 | SyncVar, message and spawn limits | Done on main; SyncVar key limits test added here | `kMaxVarKey`, `kMaxVarValue`, `kMaxClientVars`, `maxSpawnsPerSecond`, `maxLiveSpawnsPerPeer`; `anticheat_setup`; `anticheat_hard` adds a key-flood test |
| 11 | Unowned world objects belong to the server | Done on main | `AntiCheat::MayDrive`, `serverOwns`; `net_authority` |
| 12 | Lag-compensation inputs the client can't stretch | Done on main | `maxRewindRtt`, ping floor; `net_lagcomp` |
| 13 | Safer defaults and a config that fails closed | Done on main; fail-closed test added here | `AntiCheatConfig` defaults, `configError` refuses every join; `anticheat_setup`, `anticheat_hard` adds the refusal test |
| 14 | Small fixes (self-unban, trusted server reports, pings, press intervals, CI key check) | Done on main | SQL `ac_report_servers`, "pings only from players", `minPressInterval`; `anticheat_sql` |

`ROADMAP.md` still showed items 5, 6, 8 to 14 unchecked although the code is on main (the feature map says so too).
This change ticks them and keeps the deploy wait on 5 and 6.

## Part 2. The server-side hardening topics

| Topic | Status before | What this change did | Proof |
|---|---|---|---|
| Authoritative movement with speed / teleport / fly checks using the controller step | Done on main: `ServerMovementTick`, `maxMoveSpeed`, detector speed, teleport, fly, hover, no-clip | nothing | `net_auth_move`, `net_predict`, `mp_controllers`, `cheat_detect`, `anticheat_sim` |
| Rate limits on every client message type | Partial: custom messages, RPCs, SyncVars and hit claims had limits; State, input, ping, clock and ack packets had none | New packet ceiling `maxPacketsPerSecond` (default 600): any packet type from a joined client counts, extras are dropped, a flood is reported and kicked by the normal rules | `anticheat_hard` ("the packet ceiling") |
| Bounds and sanity on every numeric / string field | Mostly done: finite checks on State and inputs, `PlayerInput::Clamp`, SyncVar and message size limits, blind byte fuzz (`net_fuzz`) | A typed hostile-field fuzz: every message type with NaN, infinity, huge, negative, empty, 70 KB and odd strings against a live anti-cheat server; asserts it survives, stays bounded and serves the honest client | `anticheat_hard` ("a hostile client") |
| Replay / sequence protection and ordering | Done on main: sealed packets carry a sequence number and a 64-packet window (`requireEncryption`), reliable de-dup window, input `seq` ordering and tick window | nothing (plaintext sessions have only the cookie, documented) | `net_anticheat_p2`, `net_reconnect`, `net_resume_resend`, `anticheat_sim` (replay) |
| Join / identity binding (no impersonating another player id) | Done on main: the sender id inside a packet is ignored, the server uses the id it gave that address; tickets name the account; one connection per account | A test that forges another player's id in a message and a position packet | `anticheat_hard` ("identity binding"), `net_anticheat_p2` |
| Inventory / score / currency only through server commands | Done on main: `serverVar`, `clientVar`, `srv.` keys, detector economy ledger | nothing | `net_anticheat_ph4`, `cheat_detect`, `anticheat_sim` (`servervar`) |
| Damage / hit validation (range, line of sight, fire rate, ammo) | Done on main: six checks in `HandleHitClaim`, Net Validator cooldown and budget | nothing | `net_anticheat_ph4`, `net_lagcomp`, `cheat_detect` |
| Cheat-flag reporting with escalating responses (warn, kick, ban list file) | Partial: refusals count, kick after N, rejoin cooldown doubling, the detector ladder (log, flag, rate limit, shadow, kick, ban request), kick reports to the backend. A ban request only kicked: no ban list | New **ban list file** (`banFile`): a ban request, or `banAfterKicks` kicks, writes a ban; the join refuses banned identities, also after a restart; an admin can edit the file | `anticheat_hard` ("ban list", "cheat simulator") |
| Audit log without personal data in outside services | Missing (the backend `ac_audit` is for backend bans and needs Supabase) | New **audit log** (`auditLog`): a local file, players as a salted one-way tag, never a name, address or account id; rotates at 4 MiB; nothing is sent anywhere | `anticheat_hard` ("audit log") |
| Cheap bot / macro heuristics | Missing (jump rate and aim snap exist; nothing for inhuman regularity) | New detector `input_regularity`: the same button pressed at one steady gap (spread under 4%) 20 times in a row, timed on the player's own tick clock | `anticheat_hard` ("input regularity") |
| Time speed / clock skew | Done on main: `ClockCheck`, `maxClockDrift`, detector `TimeManipulation` | nothing | `net_anticheat_ph3`, `anticheat_sim` |
| Malicious-client fuzzer test (headless) | Partial: random bytes at a live host (`net_fuzz`), malformed channels (`anticheat_sim`) | The typed fuzz above, plus the cheat simulator (a rogue raw-packet client with eight cheats) | `anticheat_hard` |
| Configurable per game, defaults on for dedicated servers | Partial: every rule is in `.okayac`; a dedicated server runs the rules only when `--anticheat FILE` is given | With the anti-cheat on, a dedicated server now also keeps `okay_bans.txt` and `okay_anticheat_audit.log` (`--no-ban-file`, `--no-audit-log` to turn off); new keys `banFile`, `auditLog`, `banAfterKicks`, `banHours`, `banOnRequest`, `maxPacketsPerSecond`, `detect.regularity*` | `anticheat_hard` (settings) |
| The owner can try it (CLAUDE.md rule 4a) | Missing | `OkaySpacePlayer.exe --server --cheat-sim`, see `docs/try_it.md` | `anticheat_hard` runs the same code |

## A bug found while writing the tests

The detection engine's kick (risk past `detect.kickAt`) was acted on only by some message paths (inputs, RPCs, hit
claims, SyncVars). A player who sent only position (State) packets could reach the kick rung and stay in the game; the
server corrected its position but kicked only when `NetworkManager::kickAfterViolations` was set (default 0). The server
now checks every player once a tick and acts on a kick or ban request whatever the player sent. Test: `anticheat_hard`
("the detection engine's kick is acted on"); it failed before the change (the cheat simulator's `ban` step failed the same way).

## What is not done

* Items 5 and 6 are on main but the Edge function and SQL were never deployed or run on the real Supabase project.
* The bot heuristic judges button presses only. It is fed from the server-authoritative input path (the player's
  `InputCmd` packets); a game that does not use authoritative movement has no per-press data. The "macro" step of the
  simulator feeds the detector directly instead of sending real input packets, because real input needs a player pawn.
* A ban is keyed by account (`u:<id>`) when the player signed in, else by address (`a:<number>`). An anonymous cheater
  changes address to get around it; this is the same limit as the existing kick cooldown. Bans by machine hash exist
  only in the backend (`ac_ban_with_devices`).
* The ban list is a local file read at the first join; changing it by hand while the server runs is picked up only after
  a restart.
* Not run on Windows, not run against a real network, no outside review. `okay::crypto` is unchanged and libsodium stays
  the default (CLAUDE.md rule 18); nothing here adds cryptography beyond one SHA-256 call (the engine's own) for the log tag.
