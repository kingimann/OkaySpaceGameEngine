# OkayAntiCheat: audit and design

> **What this is:** the audit of every anti-cheat piece we have today and the design of **OkayAntiCheat**, one independent
> module that grows out of them, with the plan to build it in rounds. Owner request, 2026-11-26 (verbatim):
> "Develop OkayAntiCheat as an independent, modular system within our ecosystem. Integrate its administration dashboard
> into the OkaySpace Launcher, but ensure its runtime operates independently of the launcher and engine editor. Provide
> client-side and server-side components, with native OkayBackend integration for detection reports, evidence,
> enforcement, player records and appeals. Allow developers to enable and configure anti-cheat protection when exporting
> their games. Prioritize server-authoritative validation, privacy, performance, compatibility and minimizing false
> positives. Avoid kernel-level drivers for now. Ensure OkayAntiCheat can eventually support applications and games
> developed outside our engine through a documented SDK and API."
>
> **Status:** round 1 (audit and design, docs only). Nothing below marked "will" or "planned" exists yet. Owner decisions
> are in section 9, each with the default this plan uses until the owner says otherwise. The reference for what exists
> today stays [anticheat.md](anticheat.md); this page is the plan.
>
> **Last checked:** 2026-11-26 against main 378f24f1e (branch `claude/okay-anticheat-1`).

## Contents

- [1. The short version](#1-the-short-version)
- [2. Names and homes](#2-names-and-homes)
- [3. Audit: what exists](#3-audit-what-exists)
- [4. Audit: privacy, false positives, cost, problems](#4-audit-privacy-false-positives-cost-problems)
- [5. Design](#5-design)
- [6. The admin dashboard and the OkayBackend dashboard](#6-the-admin-dashboard-and-the-okaybackend-dashboard)
- [7. The SDK and the HTTP API for games outside the engine](#7-the-sdk-and-the-http-api-for-games-outside-the-engine)
- [8. Implementation rounds](#8-implementation-rounds)
- [9. Owner decisions](#9-owner-decisions)
- [10. Where the code comes from, licences and IP](#10-where-the-code-comes-from-licences-and-ip)
- [11. Risks](#11-risks)

## 1. The short version

- **Most of it exists.** We already have server checks (authority, rates, hit claims), a behavioural detection engine with
  a risk score and evidence bundles, encryption and replay protection, session tickets, a signed file manifest, Windows
  runtime checks, a hardware hash, reports, bans, appeals, an audit trail, admin pages in the launcher and an export
  option. About 20 test programs cover it (section 3). It is spread over three registry systems ("Server Anti-Cheat", "Network
  Security", "Game File Safety"), two code homes (`engine/.../Net/` and `shared/`) and two editor places.
- **OkayAntiCheat is that code, gathered and finished** (rules 19d and 19e). No second copy. The registry system "Server
  Anti-Cheat" is renamed **OkayAntiCheat** in this round; the code moves into one home in round 2.
- **The runtime is a library with no launcher or editor code in it.** Games, the player and the dedicated server link it.
  It talks to the backend only through OkayBackend (`okay::backend`), never with hand-written Supabase routes.
- **The server decides; the client only adds evidence.** Every ban-worthy decision rests on what the server saw. Client
  signals (debugger, unknown module, local clock) are capped so they can never kick or ban on their own.
- **People decide permanent bans.** Automatic responses stop at a kick and a short temporary ban. A permanent ban needs an
  admin, and a second admin for a ban that rests on one detector. Every ban can be appealed.
- **Privacy by default.** Pseudonyms instead of names and addresses, hashes instead of hardware ids, size caps, retention
  limits, a notice the developer ships, and the player's export and delete covering all of it.
- **The admin views live in the launcher** (Admin > Anti-cheat, extended into a full queue). The OkayBackend admin dashboard
  links to them and shares the same roles and the same admin functions; it does not copy them.
- **Developers turn it on at export** (one Anti-cheat section in Build Settings), and **games outside the engine** use a
  C SDK (a DLL with a stable C interface) and a versioned HTTP API.
- **No kernel driver, no admin rights, no memory scanning of other programs.** Works under Wine and Proton, and leaves
  screen readers, input methods and overlays alone.

## 2. Names and homes

Rule 19e: one system, one name, one home. The name is the owner's. "Okay" in a system name is allowed where the code
already uses it (architecture.md, Naming); this code already does (`okayac` namespace, `cmake/OkayAntiCheatKeys.cmake`).

| Thing | Name | Home |
|---|---|---|
| The system | **OkayAntiCheat** | `engine/include/okay/AntiCheat/`, `engine/src/AntiCheat/` (planned, round 2). Today: `engine/include/okay/Net/AntiCheat*`, `Net/CheatDetect*`, `Net/CheatSim*`, `Net/ClockCheck*`, `Net/DetectInspector.hpp`, `shared/AntiCheat*.hpp`, `shared/TicketClient.hpp` |
| Entry header | `okay/AntiCheat/AntiCheat.hpp` (planned; today `okay/Net/AntiCheat.hpp`) | |
| C++ namespace | `okay::anticheat`; `okayac` stays as a frozen alias | |
| Library target | `okay_anticheat` (planned), linked by the engine, the player and the dedicated server | `engine/CMakeLists.txt` |
| Engine module | `engine/modules/anticheat/module.okaymodule` (exists; its `sources=` follow the move) | |
| C SDK | `okay_anticheat.h` + `okay_anticheat.dll` (planned, round 11) | `sdk/anticheat/` (planned) |
| HTTP API | `anticheat` v1 routes behind OkayBackend Functions (planned, round 11) | `supabase/functions/anticheat/` (planned) |
| Editor parts | Anti-Cheat Config inspector, presets, checklist, Build Settings section | `editor/AntiCheatSetup*` (stay in `editor/`: an editor window is not a second home) |
| Launcher parts | Admin > Anti-cheat, ban banner, appeal, Report cheating, ticket check before a join | `launcher/AntiCheatPages.*` (stay in `launcher/`) |
| Backend | the `ac_*` tables and functions, the `session-ticket` function | `docs/sql/anticheat.sql`, `supabase/functions/session-ticket/` |
| Frozen names (aliases) | `.okayac`, `AntiCheatConfig`, `NetValidator`, `anticheat=` in `game.okayconfig`, `OKAY_ANTICHEAT`, `OKAY_AC_*` CMake options and variables, `okay_tickets.cfg`, `okt1` tickets, `okayspace-integrity/1`, `ac_*` tables, script builtins `net_anticheat*` | stay working |

What stays its own system: **Network Security** (encryption, sealed join, host keys: every networked game uses it, with
or without anti-cheat) and the path-safety part of **Game File Safety** (scripts and saves). OkayAntiCheat uses them. The
signed integrity manifest (39.4) and the trusted-key checks (39.7) are anti-cheat only, so they move into OkayAntiCheat in
round 2, and Game File Safety keeps path safety (39.9). Session tickets (39.3) are the join pass of OkayAntiCheat's ban
check; the ticket format stays in Network Security, the ticket client and ban check move in.

## 3. Audit: what exists

Read on main 378f24f1e. "Tests" are ctest names (the part after `test_`). Nothing in this table was re-run for this audit;
status words come from the code, the tests that exist and the feature map.

### 3.1 Server side (the strong part)

| Piece | Where | What it does | Quality | Tests |
|---|---|---|---|---|
| Authority rules | `Net/AntiCheat` (504 lines), `Components/AntiCheatConfig`, `.okayac` | refuses moving, despawning or writing what a client does not own; spawn allowlist; SyncVar and RPC limits; server-only and client-writable SyncVar prefixes; unowned objects belong to the server; refusal counts, kick after N a minute, rejoin cooldown doubling to an hour, counts kept 5 minutes across a reconnect; fails closed on a bad rules file | Good. Opt-in, safe defaults, one test file per plan phase | `net_anticheat`, `net_anticheat_ph1`..`ph4`, `net_anticheat_components`, `net_authority`, `anticheat_setup` |
| Per-RPC validators | `Components/NetValidator` | size, rate, cooldown, ammo-style budget, host-only | Good | `net_anticheat_components` |
| Hit claims | `Net/NetworkManager` (`HandleHitClaim`) | six server checks before damage: shooter alive, fire rate and ammo, origin, range and lag-compensated reach, line of sight, the game's own rule; damage on the server only | Good. This is server-authoritative combat | `net_anticheat_ph4`, `net_lagcomp` |
| Authoritative movement | `Net/NetworkManager` (`ServerMovementTick`) | the server moves the pawn from inputs, so speed hacks have nothing to send | Good where a game uses it; optional | `net_auth_move`, `net_predict` |
| Detection engine | `Net/CheatDetect` (458 + 1,255 lines) | 25 detector kinds (movement, combat, economy, network, refusals), evidence records, weights, half-life decay, confirmations, a response ladder (log, flag, rate limit, shadow restrict, kick, ban request), grace windows, ping tolerance, `EvidenceBundle` JSON with a hostile-input parser | Strong for its kind. Depends only on `Math/Vec3`, so it can be the core of an SDK. In the simulator 880/880 cheating runs flagged, 0/410 honest runs; tuned on a network model, not real players | `cheat_detect`, `anticheat_sim`, `anticheat_wiring`, `anticheat_hard` |
| Detection inspector | `Net/DetectInspector.hpp`, editor | about 30 numbers, Observe mode, a text box of every `detect.*` line | Good; unit tested, not run in the real editor | `anticheat_wiring` |
| Server clock check | `Net/ClockCheck` | HMAC challenge, three drifting windows in a row | Good; needs every client updated | `net_anticheat_ph3` |
| Packet ceiling | `Net/NetworkManager` | every packet type from a joined client counts (600/s on a dedicated server) | Good | `anticheat_hard` |
| Ban list and audit log (server) | `Net/AntiCheatLog` | local `okay_bans.txt` (account or address), `okay_anticheat_audit.log` with a salted one-way player tag, 4 MiB rotation | Good; local only, read at the first join | `anticheat_hard` |
| Kick reports | `Net/DedicatedServer` (`KickReporter`) | files a `server_kick` row with the evidence bundle (identifiers stripped, 16 KiB) | Good; only from listed server accounts | `anticheat_backend`, `anticheat_sql` |
| Cheat simulator | `Net/CheatSim` | a rogue raw-packet client with eight cheats against a loopback server | Good self-test; plain packets only | `anticheat_hard` |
| Relay | `Net/RelayServer` | forwards only host to client, cookies, caps; every relayed player still reaches the host's checks | Good | `relay_security`, `net_relay_hardening` |

### 3.2 Transport and identity

| Piece | Where | What it does | Quality | Tests |
|---|---|---|---|---|
| Encryption, replay window, sealed join, pinned host keys | `Net/SecureChannel`, `Net/NetworkManager` | libsodium X25519 + XChaCha20-Poly1305, 64-packet replay window, a sealed join, optional host identity | Good; libsodium (rule 18) | `net_crypto`, `net_anticheat_p2`, `net_anticheat_ph3` |
| Session tickets | `Net/SessionTicket`, `supabase/functions/session-ticket/`, `shared/TicketClient.hpp`, `shared/TicketJoin.hpp` | `okt1` ed25519 passes bound to account, server and connection; the service refuses banned accounts and machines | Code complete and tested against a fake service. **Never deployed** (owner's key step) | `net_anticheat_p2`, `ticket_client`, `anticheat_backend` |

### 3.3 Client side (supporting evidence)

| Piece | Where | What it does | Quality | Tests |
|---|---|---|---|---|
| Signed integrity manifest | `shared/AntiCheatIntegrity.hpp` (589 lines), `editor/BuildGame.cpp` | size + SHA-256 of every shipped file, ed25519 signature, launch check in the launcher and the player, refuses unlisted program files (proxy DLLs) and links | Good code. **Telemetry only** until the owner sets `OKAY_AC_TRUSTED_KEYS` | `anticheat_integrity`, `official_game`, `anticheat_keys_*` |
| Runtime checks (Windows) | `shared/AntiCheatChecks.hpp` | debugger (5 s), loaded-module allowlist (10 s), local clock heartbeat (1 s) | Logic tested everywhere; the Windows probes never ran in CI. In **Enforce** a runtime finding closes the game (see problem P2) | `anticheat_checks` |
| Hardware hash | `shared/AntiCheatHardware.hpp` | HMAC-SHA256 of machine GUID, volume serial and CPU brand, salted; empty without a machine id | Good minimisation; a lead, not proof | `anticheat_backend` |
| Client crypto | `shared/AntiCheatCrypto.hpp` | ed25519 via libsodium, our `okay::crypto` only in builds without it | Correct split for rule 18 | `anticheat_golden`, `crypto` |

### 3.4 Backend, launcher, editor and export

| Piece | Where | What it does | Quality | Tests |
|---|---|---|---|---|
| Tables and functions | `docs/sql/anticheat.sql` (730 lines) | `ac_reports` (player, `client_check`, `server_kick`, evidence jsonb), `ac_report_servers`, `ac_bans` (account and/or machine), `ac_appeals`, `ac_devices`, `ac_audit` (append-only), `ac_rate_events`; `ac_is_banned`, `ac_my_bans`, `ac_file_appeal`, `ac_linked_accounts`, `ac_ban_with_devices`, `ac_purge_old`, `ac_purge_audit`; row-level security, abuse limits | Good. Tested on a local Postgres with a Supabase stand-in; **never run on the real project** | `anticheat_sql` (77 checks) |
| Request builders | `shared/AntiCheatBackend.hpp`, `shared/AntiCheatAdminTools.hpp` | every call is a request struct and a parse function | Good shape; still writes Supabase routes by hand (OkayBackend round 4 moves them) | `anticheat_backend`, `anticheat_wiring` |
| Launcher pages | `launcher/AntiCheatPages.cpp` (661 lines) | Admin > Anti-cheat (Reports, Bans, Appeals, linked accounts, ban with devices, evidence), ban banner, Appeal, Report cheating, ticket check before a join | Works on demo data; **no automated UI test**, admin screens compile-checked only | none for the UI |
| Editor setup | `editor/AntiCheatSetup*` | three presets, one-click Add Anti-Cheat, a checklist with safe fixes | Unit tested, not run in the real editor | `anticheat_setup` |
| Export option | `editor/BuildGame.*`, `editor/BuildSettingsModel.hpp` | Build Settings > Output > Anti-cheat (Off / Report / Enforce) writes `anticheat=` and the manifest; the Server page writes basic `server.cfg` limits; the `.okayac` file and its prefabs ship | Works, but split over three places (the scene component, Output, Server page) | `anticheat_setup`, `build_settings` |
| Account delete and export | `supabase/functions/delete-account/delete_account.ts`, `Platform/Account/AccountData.hpp` | delete removes reports, appeals, devices and account-only bans, keeps machine bans with the account cleared; export includes appeals only | Delete good; **export incomplete** (problem P5) | `account_data` |

## 4. Audit: privacy, false positives, cost, problems

### 4.1 What personal data exists today

| Data | Where it is stored | Who reads it | How long |
|---|---|---|---|
| Account id of a reporter and of the reported player, free text, machine hash | `ac_reports` | admins; a reporter not even their own | 90 days (`ac_purge_old`) |
| Evidence bundle (positions, view angles, ping, detector values; no account or address by default) | `ac_reports.evidence` (16 KiB cap) | admins | with the report, 90 days |
| Bans (account id, machine hash, reason, admin id) | `ac_bans` | the player (own), admins | active bans forever; ended bans 90 days |
| Appeals (account id, text, admin note) | `ac_appeals` | the player (own), admins | decided appeals 90 days |
| Machine hashes per account (newest 20) | `ac_devices` | admins | 90 days unseen; deleted with the account |
| Change history of bans, appeals, reviews | `ac_audit` | admins | 1 year, append-only (keeps ids of deleted accounts until then) |
| Server ban list (`u:<account>` or `a:<address number>`) | `okay_bans.txt` on the game server | the server operator | until the ban ends or the line is removed |
| Server audit log (salted tag, no names or addresses) | `okay_anticheat_audit.log` on the server | the server operator | rotated at 4 MiB, one old file kept |
| Client findings with full paths | `anticheat.log` in the player's save folder | the player | no limit (local file) |
| Rate-limit events | `ac_rate_events` | nobody through the API | short windows (checked in the SQL) |

Good: hashes instead of hardware ids, a salted tag in the server log, identifiers stripped from evidence, paths cut to file
names before upload, size caps, retention jobs, delete coverage. Gaps: see P5 to P7.

### 4.2 False-positive risks

| Source | Risk | What protects today |
|---|---|---|
| Detection engine thresholds | tuned on a model and loopback; real networks and level collision that differs from what players see can trip movement and no-clip checks | grace windows, ping tolerance, confirmations, Observe mode preset, parameter-gated detectors |
| Accuracy and headshot ratios | a statistic: a very good player can look like an aimbot | wide defaults (95% / 80%), light weights that decay |
| Module allowlist | antivirus, OBS, RTSS / Afterburner, input-method DLLs, screen readers and other accessibility tools, GPU tools all show as unknown modules | "Report first" advice only; Enforce closes the game |
| Debugger check | developers debugging their own game; some security tools | same |
| Local clock heartbeat | sleep/resume and time sync (handled: single windows never flag); Wine's clock emulation is unknown | three windows in a row |
| Machine hash | cloned PCs share a GUID; family PCs and internet cafes share one machine | "a lead, not proof"; links are never automatic bans |
| Address bans (anonymous players) | shared addresses (carrier NAT, schools) | short cooldowns; signed-in servers ban accounts instead |
| Automatic bans | `banAfterKicks 3` and `banOnRequest` write a local week-long ban with no human in the loop | the backend never bans automatically; the local file is hand-editable |

### 4.3 Performance cost

Nothing here has been measured on a real game; no test sets a budget.

- **Server:** the detection engine does a few windows and buckets per sample, plus a linecast per position step for
  no-clip and a raycast per hit claim. Per-player memory is bounded by `evidenceCap`, `snapshotCap` and ring sizes. Not
  profiled.
- **Client:** the integrity check hashes every shipped file at each launch, twice (launcher and player). For a large game
  this is seconds. The runtime probes are cheap (a module walk every 10 s, two API calls every 5 s, a heartbeat every 1 s).
- **Network:** the clock heartbeat is one small packet a second each way, only with `maxClockDrift`.

### 4.4 Problems found (each is fixed in a round in section 8)

- **P1. Spread out.** Three registry systems, two code homes (`Net/` and `shared/`), and the kicks, bans and audit logic
  live partly inside `NetworkManager*.cpp` (about 100 call sites). Round 2.
- **P2. A client signal can end a game.** In Enforce, a debugger or unknown-module finding closes the game. With
  accessibility tools and overlays in the unknown list, that is a false positive that punishes the player, and it is the
  client deciding on its own. The design makes runtime findings evidence only (round 4); Enforce keeps refusing a start
  for changed files.
- **P3. An automatic week-long ban without a person.** The server's `banAfterKicks` and `banOnRequest` write bans with no
  review. The design caps automatic bans at a short temporary ban and reports them for review (round 5).
- **P4. Hand-written Supabase routes.** `AntiCheatBackend.hpp`, `AntiCheatAdminTools.hpp` and `TicketClient.hpp` write
  routes by hand; OkayBackend plans to move them in its round 4. Round 3 here is that move, done once, together.
- **P5. The player's data export is incomplete.** `AccountData` exports appeals only. Own bans, the machine hashes linked
  to the account and the reports filed by the player are not in the export. Round 6.
- **P6. No notice to players.** Nothing tells a player what the anti-cheat collects. Round 6 (developer text at export).
- **P7. Local files with no limit.** `anticheat.log` in the save folder keeps full paths forever; the server ban file is
  never trimmed of ended bans. Round 6.
- **P8. Two report tables.** `ac_reports` (cheating) and `reports` (abuse, `docs/sql/reports.sql`) both hold player
  reports with different review screens. Different reviewers and rules justify two queues, but the "report a player"
  path should be one call with a kind. Compared in round 5; merged only if the owner agrees (decision D7).
- **P9. Not deployed.** The ticket function, the SQL on the real project, the trusted keys and the update signing keys are
  owner steps not yet taken, so bans are not enforced at joins and the manifest check is telemetry.
- **P10. Untested surfaces.** The launcher admin UI and the editor setup windows have no test that runs them; the Windows
  probes never ran in CI; nothing ran on a real network.
- **P11. Not usable outside the engine.** The detection engine is plain C++ with no engine dependency beyond `Vec3`, but
  there is no C interface, no versioning and no HTTP API for a non-engine server.

## 5. Design

### 5.1 Shape of the module

```
 game (engine)      dedicated server      player       external game (C SDK)     external server (HTTP API)
      \                   |                 /                  |                          |
       +------- okay_anticheat (library) --+------- okay_anticheat.dll (C ABI) -----------+
       |   core:     rules, detectors, risk, evidence, enforcement ladder, ban list, audit log   (no engine types)
       |   server:   NetworkManager adapter (feeds the core, applies its answers)
       |   client:   integrity manifest, runtime signals, hardware hash, ticket client
       |   online:   reports, evidence upload, bans, appeals, player record   -> okay::backend only
       +----------------------------------------------------------------------------------------+
                                        |
                         OkayBackend (Database + RPC, Storage, Functions)  -> Supabase today / Local backend
                                        |
                launcher: Admin > Anti-cheat (admin dashboard), ban banner, appeals     (links the library's
                editor:   Anti-Cheat Config, presets, checklist, Build Settings section   online part, never the reverse)
```

- **core** has no dependency on the network layer, the scene, the launcher or the editor. It takes plain numbers and
  strings and returns decisions. `CheatDetect` already is this; `AntiCheat` (rules) becomes it in round 2 by passing
  positions and ids instead of engine objects.
- **server** is the thin adapter that today is spread through `NetworkManager*.cpp`. It moves next to the core; the
  network layer calls it through one interface (`ServerHooks`).
- **client** is today's `shared/AntiCheatIntegrity.hpp`, `AntiCheatChecks.hpp`, `AntiCheatHardware.hpp`,
  `TicketClient.hpp`. Integrity keeps sharing the file hash and path checks with the updater (`shared/OfficialGame.hpp`);
  round 2 moves those helpers to the shared helper list (`docs/engine/shared_code.md`) rather than copying them.
- **online** is today's `AntiCheatBackend.hpp` and `AntiCheatAdminTools.hpp`, moved onto `okay::backend` (round 3).
- **A check list in CI** (round 2): a script check that nothing under the new home includes `launcher/` or `editor/`
  headers, run by `branch-lint`.
- Threads: no `<mutex>` or `<thread>` (rule 21a); work runs on the caller's tick, the online part uses the existing
  worker wrappers.

### 5.2 Server-authoritative validation first

Order of trust, from strongest to weakest. A decision may only use a weaker level as support for a stronger one.

1. **The server does it itself.** Authoritative movement, server-applied damage, server-owned SyncVars, server grants.
   Nothing to detect; the cheat has no effect. The presets turn these on where a game can use them.
2. **The server refuses the impossible.** Authority rules, rate limits, hit-claim checks, packet ceiling. Exists.
3. **The server judges the unlikely over time.** The detection engine. Exists.
4. **Input replay (planned, round 4).** For games with authoritative movement the server already re-runs inputs. For games
   without it, the server keeps the last few seconds of a player's inputs and reported positions and, when movement
   evidence appears, re-runs the same controller step on its own copy to see whether those inputs could have produced that
   path. A mismatch is strong evidence; a match clears the movement evidence. This extends `ServerMovementTick`, it is not
   a second movement system.
5. **Client signals** (integrity findings, debugger, modules, local clock): evidence of kind `client_signal`, with a weight
   cap so that client signals alone reach at most the `flag` rung. They can never kick, rate-limit, shadow-restrict or ban
   by themselves (round 4, a test proves the cap).

Economy and rate checks stay on the server: `serverVar` prefixes, the economy ledger (`OnGrant`, `SetBalance`), item
uniqueness, Net Validator budgets. Round 4 adds a preset check that warns when a game writes a watched counter from a
client at all.

### 5.3 Detection report format

One format, extended from today's `EvidenceBundle` v1 (not a second format). Version 2 adds fields; v1 still parses.

```
{"v":2,
 "game":"<game id>", "build":"<build id>", "server":"<server id>", "sdk":"engine|c-sdk|http",
 "subject":"<pseudonym>",                       // salted tag, as in the server audit log; never a name or address
 "risk":62.5, "action":"flag", "t":812.4, "trunc":false,
 "counts":{"speed_window":4}, "thr":{"maxSpeed":8},
 "ev":[{"k":"teleport","t":801.2,"tick":2210,"val":91.4,"thr":24.5,"w":30,"d":"moved 91 units in 50 ms","s":[...]}],
 "client":[{"k":"module","d":"hook.dll","t":790.0}],     // client signals, kept apart from server evidence
 "snap":[...], "retention":"standard"}
```

- The account id is attached by the backend function from the authenticated server account and the ticket's `sub`, not
  written by the game into the bundle. That keeps identity out of every file a server writes.
- Same limits as today: ASCII free text, tokens redacted, 16 KiB inline; larger attachments go to evidence storage.
- The parser stays the hostile-input parser (`EvidenceBundle::Parse`), extended and fuzzed.

### 5.4 Evidence storage and retention

| Kind | Where (through OkayBackend) | Size cap | Kept |
|---|---|---|---|
| Report and inline evidence | `ac_reports` + `evidence jsonb` (exists) | 16 KiB | 90 days, or until the ban it supports ends + 90 days |
| Large evidence (input and position recording for a replay) | private Storage bucket `ac-evidence`, path `<report id>/<n>` (planned) | 1 MiB per report, 5 files | 30 days, or while a ban that cites it is active or under appeal |
| Decisions, bans, appeals | `ac_bans`, `ac_appeals` (exist) | - | active bans while active; then 90 days |
| Change history | `ac_audit` (exists) | - | 1 year |

- The bucket has no public read; only admins and game admins of that game (5.7) get short-lived signed links through a
  function. `ac_purge_old` also removes evidence objects; a test proves an expired object is gone.
- Defaults are the owner's to change (D3). They are stored in one settings row so a deployment can shorten them.

### 5.5 Enforcement levels

| Level | What happens | Who may apply it | Exists? |
|---|---|---|---|
| Log / flag | evidence kept; a report filed for review | automatic | yes (ladder rungs) |
| Warn | the player sees a message in game ("unusual movement detected; if this is a mistake, nothing happens") | automatic, once per match | planned (round 5) |
| Rate limit, shadow restrict | message budget halved; hits do no damage, silently | automatic, this match only | yes |
| Kick | removed from this match | automatic | yes |
| Temporary ban | refused at join for up to 24 h (default) | automatic only from server evidence above `kick`, and always filed for review; admins up to any length | partly (local file, a week; backend bans are admin-only) |
| Shadow queue | matchmaking puts the account with other flagged accounts | admin, or automatic after a reviewed temp ban (owner decision D5) | planned (round 5) |
| Permanent ban | refused at join for this game, or for every game (scope, D4) | admins only; a second admin confirms when one detector is the only evidence | partly (admins can ban; no second review) |

- Escalation is per game by default; a ban across games is an admin choice with its own scope field.
- Every level above warn writes `ac_audit`, and every ban carries the report id that justifies it.
- The local server ban file stays for servers without a backend, with the same 24 h automatic cap.

### 5.6 Player records and appeals

- **Player record** (planned, round 5): not a new table. One admin function `ac_player_record(user)` returns, from the tables
  that exist: reports about the player, bans and their reasons, appeals, linked machines (count only unless asked), and
  risk events per detector. The launcher's evidence viewer opens from it.
- **The player sees their own record** through `ac_my_bans` (exists) extended with a plain summary of why ("movement
  checks", "file changes"), never the raw evidence or other players' data.
- **Appeals** (exist: one waiting at a time, three per ban). Additions: the appeal shows the player the summary above; the
  admin sees the full evidence; a decision within 14 days (default) or the appeal is shown as overdue in the queue; an
  accepted appeal on a detector-only ban lowers that detector's weight suggestion (5.10).

### 5.7 Row-level rules

| Role | How it is known | Can |
|---|---|---|
| Player | signed in | file reports (rate-limited), read own bans, file appeals, read own appeal decisions, export and delete own data |
| Game server | account in `ac_report_servers` (exists), scoped to a game id (planned) | file `server_kick` / `server_check` reports with evidence for its game only; never read |
| Game admin (planned) | `ac_game_admins(game_id, user_id)` | read reports, evidence and appeals for that game; warn, kick, temporary ban for that game; propose permanent bans |
| Admin | `public.is_admin` (exists) | everything, except banning themselves (exists) |
| Ticket service | its own key inside the function | `ac_is_banned`, `ac_note_device` |

All in SQL policies and SECURITY DEFINER functions, like today. No client ever holds a service key; a game server holds
only the ticket public key and its own account session.

### 5.8 The developer side: one Anti-cheat section at export

Today three places hold the settings (the scene's Anti-Cheat Config, Build Settings > Output > Anti-cheat, Build Settings >
Server page). Planned (round 8): one **Anti-cheat** section in Build Settings that reads and writes the same data, so
nothing is stored twice.

- **Enable** and **preset** (Casual co-op, Competitive, Economy game: the existing presets).
- **Checks:** server rules (from the scene's Anti-Cheat Config or its `.okayac`), the detection engine and Observe mode,
  file integrity (Off / Report / Enforce), runtime signals (on / off), session tickets (off / optional / required).
- **Server rules:** the existing checklist with its fix buttons.
- **Backend reporting:** file reports and evidence to OkayBackend (needs a server account).
- **Privacy notice text:** a short default text the developer can edit, shipped as `Data/anticheat_notice.txt` and shown
  by the player once, before the first online match, with a link to the game's own privacy page if the developer gives
  one. A game with anti-cheat on and an empty notice gets a checklist warning.
- The build writes all of it as it does today (`game.okayconfig`, the signed manifest, `server.cfg`, the scene). The
  Build window is Claude 3's lane: round 8 needs `Agree:` from Claude 3.

### 5.9 Privacy

- **Minimise.** Collect only what a decision uses. No names, emails or raw addresses in any report or evidence. No file
  paths beyond the file name. No screenshots, no process lists beyond the unknown module names, no keystrokes.
- **Pseudonyms.** Server logs use the salted tag; the backend links a report to an account inside the function. Hardware
  ids leave the machine only as a salted HMAC.
- **Retention.** The table in 5.4, plus: the local `anticheat.log` keeps 7 days or 1 MiB; the server ban file drops ended
  bans at start.
- **Notice.** 5.8. The launcher's own privacy page (if any) gets one paragraph on anti-cheat (owner text).
- **Export and delete.** Round 6 adds to `AccountData` export: own bans (`ac_my_bans`), own reports filed, own appeals
  (exists), machine hashes linked to the account (the hash itself, not other accounts). Reports about the player are shown
  as a count and the summary; the reporter is never shown. Delete is already covered (`delete_account.ts`); round 6 adds
  evidence objects in the bucket.
- **Legal points (for the owner, not legal advice):**
  - **GDPR.** The lawful basis is likely legitimate interest (fair play); a short balancing note should be written. Players
    must be told (Art. 13): the notice in 5.8. Decisions with a significant effect made only by a machine are limited (Art.
    22): the rule that people decide permanent bans, and that every ban can be appealed to a person, is designed for this.
    The hardware hash and the pseudonymous tag are still personal data. A data protection impact assessment is likely
    expected for systematic monitoring of players. Supabase is a processor: its data processing terms apply.
  - **Children.** If games allow players under 13 (US, COPPA) or under the age set in each EU country (13 to 16, GDPR
    Art. 8), collecting a hardware hash or evidence about them may need parental consent or may not be allowed at all.
    The UK Age Appropriate Design Code asks for high privacy by default for children. Safe default in this plan: a game
    can mark itself as "may have children"; then the client sends no hardware hash and no runtime signals, and only server
    evidence about gameplay is kept (decision D6).
  - **Other places** (for example California's CCPA) have their own rules on access and deletion; export and delete cover
    the usual requests.

### 5.10 Minimising false positives

- **Observe mode first.** A new game's anti-cheat starts in Observe mode (exists as a preset; planned as the default at
  export) and the checklist asks for a number of observed matches before kicks are turned on.
- **Several signals.** A kick needs evidence from the server; a ban needs evidence from at least two detectors or one
  impossible-by-construction fact (damage above the weapon maximum, a duplicated unique item), or a person's decision.
- **Human review before permanent bans**, a second reviewer for single-detector bans, and appeals (5.5, 5.6).
- **Measure it.** Per detector, count reviewed reports that were dismissed and appeals that were accepted. The admin
  dashboard shows the detectors with the most dismissed reports and suggests a higher threshold or lower weight; it never
  changes a game's settings by itself.
- **Grace and tolerance** (exist): teleports, respawns, lag, host migration, join, ping, boosts.

### 5.11 Performance budgets

Budgets are checked by a test that counts work (samples, raycasts, bytes), not by a wall-clock limit (a time limit cannot
fail reliably, bug class 7). A separate benchmark prints times for information.

| Where | Budget |
|---|---|
| Server, per player per tick | at most 2 raycasts or linecasts and a fixed number of window updates; no allocation after warm-up |
| Server, memory per player | at most 64 KiB (evidence ring, snapshots, buckets) |
| Client, runtime signals | at most one probe per frame, spread over frames; none in a frame that is already late |
| Client, launch check | executables and the manifest hashed at every launch; data files hashed when the install changes (size and time differ) or once a day, with the full check before a ranked or ticket-required join (owner decision D8) |
| Network | at most one heartbeat packet a second each way; reports only on events, never per tick |

### 5.12 Compatibility

- **No kernel driver, no service, no admin rights.** Everything runs in the game's own process with the player's rights.
- **No reading other programs' memory, no hooking other programs.** The module check lists modules in our own process only.
- **Wine and Proton:** the runtime probes detect Wine (a documented export of its `ntdll`) and then report only; integrity
  checks work unchanged (plain files). A test feeds the probe answers Wine gives.
- **Accessibility tools, input methods and overlays:** a signed allowlist data file (`anticheat_allow.json`) by category:
  screen readers and magnifiers, input methods (IME), voice and caption tools, overlays and recorders, GPU and monitoring
  tools. Allowed modules are not reported at all. Unknown modules are reported, never acted on (5.2). The list ships with
  the player and can be extended by the game.
- **Developers debugging their own game** are never affected: the runtime probes run only in a built game's player, never in
  the editor or its Play mode (to be checked by a test in round 9).

### 5.13 Cryptography

No new cryptography (rule 18). Signatures stay ed25519 through libsodium (`shared/AntiCheatCrypto.hpp`), encryption stays
`SecureChannel` on libsodium, hashes and HMAC stay the engine's one SHA-256 (`okay/Crypto/Sha256.hpp`). `okay::crypto`
stays off by default and is used only in builds without libsodium, as today.

## 6. The admin dashboard and the OkayBackend dashboard

The OkayBackend admin dashboard is being designed at the same time (`docs/platform/okay_backend_admin.md`, branch
`claude/okay-backend-admin-1`; this page does not change it). How the two fit:

- **One place for anti-cheat work: the launcher.** Launcher > Admin > Anti-cheat is the OkayAntiCheat dashboard. It grows
  from today's three lists (Reports, Bans, Appeals) into:
  - **Reports queue:** open reports sorted by risk and age, filters by game, detector, kind and status, "assign to me".
  - **Evidence viewer:** the bundle as a timeline of evidence records with values and thresholds, a top-down plot of the
    position snapshots, the client signals listed apart, and the large evidence files when there are any.
  - **Decisions:** warn, temporary ban, permanent ban (with the second-reviewer step), shadow queue, dismiss; each with a
    reason template and the report it rests on.
  - **Appeals:** the appeal beside the ban, its evidence and the player's summary; overdue appeals first.
  - **Player record:** 5.6.
  - **Audit log:** `ac_audit` read-only, filtered by admin, player and action.
  - **Detector health:** dismissed and accepted counts per detector (5.10).
- **The OkayBackend dashboard links to it and does not copy it.** Its anti-cheat tile shows counts only (open reports,
  overdue appeals, bans this week) from one function, `ac_admin_summary()`, and a button that opens the launcher at Admin >
  Anti-cheat (or the same view inside the dashboard if it is itself a launcher page; either way it is one view, one code).
- **Same roles, same admin API.** Both use `public.is_admin` and the planned `ac_game_admins`; both call the same SQL
  functions through `okay::backend` (`Database::Rpc`), and both write `admin_log` and `ac_audit` through the same
  triggers. Nothing anti-cheat-specific is added to the backend dashboard's own tables.
- **Order:** round 7 here uses whatever role and navigation pieces the backend dashboard defines first; if this round goes
  first, it defines `ac_admin_summary()` and the backend dashboard reuses it.

## 7. The SDK and the HTTP API for games outside the engine

### 7.1 C SDK (`okay_anticheat.h`, `okay_anticheat.dll`)

A plain C interface over the core and online parts, so any language that can call C can use it.

```c
#define OKAYAC_API_VERSION_MAJOR 1
#define OKAYAC_API_VERSION_MINOR 0

typedef struct okayac_server okayac_server;          /* opaque */

typedef struct okayac_config {
    uint32_t struct_size;                             /* sizeof(okayac_config); lets later versions add fields */
    const char* rules_text;                           /* the .okayac text, the same format as in the engine */
    void (*on_response)(void* user, uint32_t player, int level, const char* evidence_json);
    void* user;
} okayac_config;

uint32_t okayac_version(void);                        /* (major << 16) | minor of the DLL */
int  okayac_server_create(const okayac_config* cfg, okayac_server** out);   /* 0 = ok, else an OKAYAC_E_* code */
void okayac_server_destroy(okayac_server* s);
int  okayac_player_join(okayac_server* s, uint32_t player, const char* pseudonym);
int  okayac_player_leave(okayac_server* s, uint32_t player);
int  okayac_feed_position(okayac_server* s, uint32_t player, double t, const float pos[3], float yaw, float pitch, float rtt);
int  okayac_feed_shot(okayac_server* s, uint32_t player, double t, const char* weapon, int hit, int headshot, float damage);
int  okayac_feed_grant(okayac_server* s, uint32_t player, double t, const char* counter, double amount);
int  okayac_note(okayac_server* s, uint32_t player, int what, double until);   /* teleport, respawn, boost, ... */
int  okayac_tick(okayac_server* s, double t);
int  okayac_evidence(okayac_server* s, uint32_t player, char* buf, size_t cap, size_t* written);
```

ABI rules: C only at the boundary (no C++ types, no exceptions across it); every struct starts with `struct_size` and only
grows at the end; functions are only added, never changed; all symbols start with `okayac_`; the caller owns every buffer;
the default calling convention of the platform (cdecl on Windows x64 is the only one). A major version change is a new
DLL name. The engine itself keeps using the C++ API; the C layer is a wrapper, not a second implementation.

### 7.2 HTTP API (v1)

For servers that cannot link the DLL, and for the SDK's online part. Served by one OkayBackend function (`anticheat`, on
Supabase today, on the Local backend later), versioned in the path.

| Route | Who | What |
|---|---|---|
| `POST /functions/v1/anticheat/v1/reports` | game server account | file a report with a v2 bundle |
| `POST /functions/v1/anticheat/v1/evidence/<report>` | game server account | attach a large evidence file (5.4) |
| `POST /functions/v1/anticheat/v1/ban-check` | game server account | is this ticket's `sub` banned for this game (for servers that cannot verify tickets themselves) |
| `POST /functions/v1/session-ticket` | player | exists today; unchanged |

- Auth: a per-game server key that an admin or game admin creates (stored hashed; it can file reports for that game only),
  never the service key.
- Versioning: `v1` stays until at least two releases after a `v2`; responses carry `"api":"1.N"`; unknown fields are
  ignored by both sides.
- The API reference is written from the code in the round that ships it, as an internal record (rule 5); the public
  documentation for outside developers is the owner's decision (D9), since outside-facing docs are not written today.

## 8. Implementation rounds

Each round is one PR (rule 9). Each starts from `python3 scripts/find_existing.py`, a claim on #422, and a failing test.
"Exists" and "gap" are from the audit above.

| # | Round | Exists | Gap | Files | Tests |
|---|---|---|---|---|---|
| 1 | Audit and design (this page) | everything in section 3 | one plan, one name | this page, the feature map System line, pointers in `anticheat.md` and the indexes | `gen_feature_map.py --validate`, precheck |
| 2 | One home | code in `Net/` and `shared/` | P1 | move `Net/AntiCheat*`, `CheatDetect*`, `CheatSim*`, `ClockCheck*`, `DetectInspector.hpp`, `shared/AntiCheat*.hpp`, `TicketClient.hpp` into `okay/AntiCheat/`; `okay_anticheat` target; `ServerHooks` adapter out of `NetworkManager*.cpp`; module file; registry (Game File Safety 39.4 and 39.7 move in) | every existing anti-cheat test unchanged and passing; new `anticheat_deps` check (no launcher or editor include) |
| 3 | On OkayBackend | request builders with hand-written routes | P4 | `online/` part on `okay::backend` (`Database`, `Rpc`, `Functions`); done as OkayBackend round 4's anti-cheat rows | golden request tests (same bytes to Supabase as before) for `anticheat_backend`, `anticheat_wiring`, `ticket_client` |
| 4 | Server first, client as evidence | detectors, client checks that can end a game | P2; input replay; report format v2 | `client_signal` kind with a weight cap; runtime findings never close the game; replay check on movement evidence; `EvidenceBundle` v2 | a test that client signals alone never pass `flag`; v1 and v2 parse; replay clears an honest path and confirms a speed path; fuzz of v2 |
| 5 | Enforcement and records | ladder, admin bans, appeals | P3, P8 (compare); warn, temp-ban cap, shadow queue, game scope, second reviewer, player record | `anticheat.sql` (game id, `ac_game_admins`, `ac_player_record`, `ac_admin_summary`, second review), server ban cap | `anticheat_sql` extended (each role's allowed and refused actions), `anticheat_hard` (24 h cap) |
| 6 | Evidence storage and privacy | evidence jsonb, delete function, purge | P5, P6, P7; bucket, notice, export | Storage bucket rules, `ac_purge_old`, `AccountData` export rows, `delete_account.ts`, local log limits, child-safe mode | export contains own bans, reports filed and hashes; purge removes expired objects; child-safe mode sends no hash |
| 7 | Launcher admin dashboard | three lists, evidence button | queue, evidence viewer, decisions, appeals, player record, audit log, detector health; the backend dashboard link | `launcher/AntiCheatPages.*` and the plain request/parse functions | request and parse tests; a demo-data render check of each view (`OKAY_UI_DEMO_*`) |
| 8 | Export section | three places | one Anti-cheat section, notice text | `editor/AntiCheatSetup*`, Build Settings files (**Claude 3's lane: needs `Agree:`**) | `anticheat_setup`, `build_settings` extended: settings round trip, notice shipped |
| 9 | Compatibility | module allowlist with two overlays | allowlist file by category, Wine detection, editor skip | `client/` probes, `anticheat_allow.json` | allowlisted modules never reported; Wine answers give report only |
| 10 | Performance | nothing measured | budgets | counters in the core, a benchmark | work-count budget tests; benchmark prints times (information only) |
| 11 | SDK and HTTP API v1 | plain C++ core | P11 | `sdk/anticheat/`, `supabase/functions/anticheat/` | a C test program built against the header only; ABI checks (struct sizes, symbol list); HTTP route tests with a fake backend |
| 12 | Local backend | `anticheat.sql` on Supabase | anti-cheat tables on the Local backend | OkayBackend round 14 (shared) | the oracle from `test_anticheat.sql` |

Owner steps that no round can do (P9): create the ticket key pair and deploy `session-ticket`, run `anticheat.sql` on the
real project, set `OKAY_AC_TRUSTED_KEYS`, add server accounts to `ac_report_servers`.

## 9. Owner decisions

Each has the safe default this plan uses until the owner decides.

| # | Decision | Default used |
|---|---|---|
| D1 | Name and home: system "OkayAntiCheat", home `engine/include/okay/AntiCheat/` | as written |
| D2 | Runtime findings (debugger, modules) stop closing the game in Enforce | yes, evidence only |
| D3 | Retention: reports and evidence 90 days, large evidence 30 days, audit 1 year, local log 7 days | as written |
| D4 | Ban scope: per game, or across every game on OkaySpace | per game; across games only by an admin's explicit choice |
| D5 | Shadow queue: may it start automatically after a reviewed temporary ban, or only by an admin | only by an admin |
| D6 | Children: a "may have children" game mode that sends no hardware hash and no runtime signals | on for any game that says it may have children; the age rules themselves need the owner's legal advice |
| D7 | Merge the two player-report tables (`ac_reports`, `reports`) into one with a kind | keep both; one shared "report a player" call |
| D8 | Launch check: full hash every launch, or executables every launch and data files on change | executables every launch, data on change, full check before a ticket-required join |
| D9 | Public documentation for outside developers using the SDK and API | none until the owner says so (rule 5); internal reference only |
| D10 | Automatic temporary ban length cap | 24 hours, always filed for review |
| D11 | Who may be a game admin (studio accounts) and who grants it | only admins grant it |

## 10. Where the code comes from, licences and IP

- **Written from:** our own existing code (section 3), the GDPR text (Regulation (EU) 2016/679), COPPA (15 U.S.C. 6501-6506
  and its rule), the UK Age Appropriate Design Code, RFC 8032 (ed25519), RFC 2104 (HMAC), FIPS 180-4 (SHA-256), Microsoft's
  public Win32 documentation (debugger and module APIs, already used), the PostgREST and Supabase route shapes already in
  use (OkayBackend section 11), and general ideas in public talks and papers on server-side cheat detection (thresholds,
  windows, risk scores, human review).
- **Commercial anti-cheat products** (EasyAntiCheat, BattlEye, Vanguard, VAC) were not read or imitated. Their public
  descriptions are known only in general terms (for example that some use kernel drivers, which this design does not); no
  code, file format, protocol or closed internal behaviour of theirs is used or copied.
- **No new third-party code or service.** libsodium stays (rule 18, existing). Supabase stays the production backend
  through OkayBackend.

**IP flags** (rule 19a):

1. **Patents (possible, not checked).** Behavioural cheat detection (statistical aim analysis, server replay of inputs,
   delayed "ban waves", matchmaking flagged players together) is an area where companies have filed patents. We cannot
   tell from here whether any active patent covers a specific method in this plan. Before shipping round 4 (input replay)
   and round 5 (shadow queue) widely, the owner may want a patent search. Until then these are flagged, not cleared.
2. **Trademarks in the allowlist.** The allowlist file (round 9) names other products (screen readers, overlays such as
   Steam, Discord, OBS). Naming them to identify them is normally fine (nominative use); the file uses names and module
   file names only, no logos.
3. **Product names in this page.** EasyAntiCheat, BattlEye, Vanguard and VAC are named only to say we do not copy them.
4. **Shared terms.** "Shadow ban", "shadow queue" and "ban wave" are common words, not names of a product.
5. **Legal text.** The default privacy notice text (round 6) is written by us; the owner should have it checked.

No copied code was found or added in this round (docs only).

## 11. Risks

- **Client checks are bypassable.** This is by design: the client is evidence, the server decides.
- **False positives hurt honest players most.** Observe mode, caps, human review and appeals are the defence; tuning
  needs real matches, which we do not have yet.
- **Not deployed means not enforced.** Until the owner steps in section 8 are done, bans do not stop joins and the
  manifest check only reports.
- **Privacy and children's law vary by country** and are the owner's to confirm (section 5.9).
- **Two designs at once.** The backend dashboard is being designed now; section 6 keeps one view and one API, and round 7
  waits for whichever piece lands first.
- **Lane edges.** Round 8 touches Claude 3's Build window; round 12 depends on OkayBackend's Local backend rounds.
