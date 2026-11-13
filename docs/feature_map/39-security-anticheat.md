# 39. Security & anti-cheat

A cross-cutting area: every place where the engine, launcher or backend defends itself against cheaters, forged data, unsafe
paths or tampered files. It pulls together the anti-cheat in the network layer (details in 16.6), the `shared/AntiCheat*`
headers, session tickets and their Edge function, trusted-key checks at build time, relay and account hardening, script path
safety and data obfuscation. It is big enough to stand alone (about 3,000 lines in `shared/`, 1,000 in `Net/AntiCheat`,
`SessionTicket`, `ClockCheck`, 770 lines of `docs/anticheat.md`, 443 of SQL). The anti-cheat plan (phases 1 to 5) and all 14
hardening items are merged. An opt-in client hardening layer (39.12) was added afterwards on its own branch.

## 39. Security & anti-cheat

### 39.1 Server authority checks and gameplay validation
- **What:** What the server refuses from a client: moving, despawning or writing something it doesn't own, spawning prefabs off a list, flooding SyncVars or RPCs, speed hacks, forged hit claims, and writing server-only variables. Opt-in per scene; a new Anti-Cheat Config starts safe.
- **Parts:**
  - 39.1.1 Rules and kicks - `Net/AntiCheat` - `AntiCheatSettings`, the `.okayac` text format, refusal counts, kick after N in a minute, a rejoin cooldown that doubles up to an hour, budgets that survive a reconnect for 5 minutes
  - 39.1.2 Scene component - `Components/AntiCheatConfig` - inline rules or a `.okayac` file; On Cheat Detected and On Validate Rpc events; fails closed (a rules file that won't load refuses every join)
  - 39.1.3 Per-RPC validators - `Components/NetValidator` - size, calls per second, cooldown, ammo-style budget, host-only
  - 39.1.4 Hit claims and lag compensation - `Net/NetworkManager` - six server checks before any damage; the server alone applies damage (16.6.4)
  - 39.1.5 Unowned world objects - `Net/AntiCheat` - objects nobody owns belong to the server; "Players can claim it" opts in; refusals there are logged once and don't count as violations
  - 39.1.6 Kick filing - `Net/DedicatedServer` - `KickReporter` files `ac_reports` rows, nothing is banned automatically (16.5.4)
- **Code:** `Net/AntiCheat`, `Components/AntiCheatConfig`, `Components/NetValidator`, `Net/NetworkManager`, `Net/DedicatedServer`
- **API:** `AntiCheatSettings`, `NetworkManager::anticheat`, `SetCheatHandler`, `ClaimHit`, `ValidateHit`, `NetValidator`, scripts `net_anticheat`, `net_anticheat_load`, `net_violations`, `net_hit`, `net_budget`, server flags `--anticheat`, `--report-kicks`
- **Editor:** Inspector: Anti-Cheat Config and Net Validator; File > Build Settings > Server page
- **Docs:** `docs/anticheat.md` (P1, Network hardening, Gameplay validation), `docs/launcher-multiplayer.md`
- **Tests:** `net_anticheat`, `net_anticheat_ph1`, `net_anticheat_ph4`, `net_anticheat_components`, `net_authority`, `net_lagcomp`
- **Status:** Works - one test file per plan phase; every item of the 2026-10-26 hardening audit is on main (#351 to #394 per `git log`)
- **Open:** respawns are not replicated; checks only apply when `enabled`; defaults for scenes saved before the safe defaults keep encryption off
- **Related:** 16.6, 16.11, 39.2
- **Words:** anti-cheat, cheat, authority, allowlist, forged syncvar, hit claim, NetValidator, .okayac, violation, kick, speed hack, fire rate, ammo budget
- **Seen as:** "forged syncvar", "rpc rejected", "hit rejected" in the log; players kicked for speed; clients can't spawn a prefab
- **Ask me like:** "which messages does the anti-cheat refuse?", "allow clients to spawn only crates"

### 39.2 Transport security: encryption, replay protection, sealed join, host keys
- **What:** Making the network trustworthy even on an open connection: every packet sealed and numbered, replays refused, the join (name, password, token) sealed behind a key exchange, and a server that can prove its identity to pinned clients.
- **Parts:**
  - 39.2.1 Authenticated encryption - `Net/SecureChannel` - X25519 plus XChaCha20-Poly1305 (libsodium); a host with `encryption` on in a build without libsodium refuses to start
  - 39.2.2 Require encryption and replay window - `Net/NetworkManager` - `requireEncryption`: plaintext join refused, 8-byte sequence inside the ciphertext, 64-packet window, plaintext from joined clients dropped
  - 39.2.3 Sealed join - `Net/NetworkManager` - key hello (padded so it can't amplify), `JoinKey`, sealed Join; envelope byte and protocol version are authenticated data; an old server just gets a plaintext join after 2 s
  - 39.2.4 Pinned host keys - `Net/NetworkManager` - `SetHostIdentity(seed)` / `--host-key FILE`; a client with `pinnedHostKey` joins only a server signing with it and never falls back to plaintext
  - 39.2.5 Session cookie without encryption - `Net/NetworkManager` - a random 64-bit cookie on Leave, State, Message and SyncVar so a spoofed Leave can't kick a player
  - 39.2.6 Join gating - `Net/NetworkManager` - encryption first, rate-limited password and sign-in checks (#365)
  - 39.2.7 Account token policy - `Net/NetworkManager` - the token goes only to hosts that say they check sign-ins (`authTokenToAnyHost` false by default, #320)
- **Code:** `Net/SecureChannel`, `Net/Packet`, `Net/NetworkManager`
- **API:** `NetworkManager::encryption`, `requireEncryption`, `pinnedHostKey`, `SetHostIdentity`, `authTokenToAnyHost`, scripts `net_encrypt`, `net_pin_host`, `net_encrypted`, flags `--no-encryption`, `--host-key`
- **Editor:** Inspector: Anti-Cheat Config (Require encryption); the Network Manager inspector has no encryption field
- **Docs:** `docs/anticheat.md` (P2, Network hardening), `docs/dedicated-server.md`
- **Tests:** `net_crypto`, `net_anticheat_p2`, `net_anticheat_ph3`, `net_token_policy`, `net_auth`, `net_hardening`
- **Status:** Works - phases 2 and 3 have dedicated tests; throwaway keys are made at test time
- **Open:** possible gap: the Ubuntu CI job installs no libsodium-dev, so these tests may only run their "built without libsodium" branch there (the MinGW release build and the Linux server job do have libsodium; unverified for the runner image); without a pinned key a sealed join stops listeners but not a man in the middle (documented)
- **Related:** 16.1, 39.3, 18.3
- **Words:** encryption, libsodium, sealed, replay, sequence number, key exchange, pinned host key, man in the middle, session cookie, requireEncryption, join gating
- **Seen as:** "encryption required"; a join refused with "no encryption" or a pinned-key mismatch; an old client can't join a server with replay protection
- **Ask me like:** "make the server require encryption", "pin my server's key in the client"

### 39.3 Session tickets and the ticket service
- **What:** A short-lived signed "this account may join this server" pass. A Supabase Edge function signs it after checking the login and anti-cheat bans; the game server checks it with only a public key, so it needs no Supabase key and a banned player gets no ticket.
- **Parts:**
  - 39.3.1 Ticket format and verify - `Net/SessionTicket` - `okt1.<payload>.<signature>` (ed25519), `sub`, `aud`, `iat`, `exp` (at most 1 h, 60 s skew), optional `cnf` key binding; `SessionTicket::Sign` is for tests and tools only
  - 39.3.2 Server side policy - `Net/AntiCheat` - `ticketKey`, `ticketAudience` (required with a key), `ticketAnyAudience`; one connection per account (a second join replaces the first)
  - 39.3.3 Edge function - `supabase/functions/session-ticket/index.ts` - checks login (`/auth/v1/user`), calls `ac_is_banned` with its own service key, answers 403 banned, 503 if the ban check fails, else a ticket with `OKAY_TICKET_TTL` (600 s default, 3600 max)
  - 39.3.4 Signing code - `supabase/functions/session-ticket/ticket.ts` - Web Crypto only; must match `SessionTicket::Verify` byte for byte
  - 39.3.5 Key maker and self-test - `supabase/functions/session-ticket/make_key.ts`, `supabase/functions/session-ticket/ticket_test.ts` - the owner runs `make_key.ts`; the seed is the private key and never goes in git, a build or chat
  - 39.3.6 Client request - `shared/AntiCheatBackend.hpp` - builds the POST and parses the answer (`ParseTicket`); `NetworkManager::TicketBinding()` makes the key binding
- **Code:** `Net/SessionTicket`, `supabase/functions/session-ticket/index.ts`, `supabase/functions/session-ticket/ticket.ts`, `supabase/functions/session-ticket/make_key.ts`, `supabase/functions/session-ticket/ticket_test.ts`, `shared/AntiCheatBackend.hpp`
- **API:** `SessionTicket::Verify`, `NetworkManager::SetAuthToken`, `TicketBinding`, `.okayac` keys `ticketKey`, `ticketAudience`, `ticketAnyAudience`, env `OKAY_TICKET_SEED`, `OKAY_TICKET_PUBLIC_KEY`, `OKAY_TICKET_TTL`
- **Editor:** none
- **Docs:** `docs/anticheat.md` (P2: session tickets, The ticket service)
- **Tests:** `net_anticheat_p2` (verifies tickets the TypeScript signer made, per docs), `anticheat_backend`
- **Status:** Partial - server verification is tested; the function is not deployed (per docs), and no launcher or game code calls the ticket request before joining
- **Open:** deploy the function and create the key pair (the owner's step, ROADMAP "Waiting on Iman"); wire the launcher or game to fetch a ticket before `StartClient` and refresh `ac_my_bans` on a `banned` answer; `ticket_test.ts` is run by hand, not in CI
- **Related:** 39.6, 39.7, 17.2, 41.6
- **Words:** session ticket, okt1, ed25519, ticket service, Edge function, Supabase, ticketKey, ticketAudience, cnf, key binding, ban at ticket issue, OKAY_TICKET_SEED
- **Seen as:** "server misconfigured: ticketKey without ticketAudience"; a banned player can still join a server that doesn't require tickets; a ticket rejected as expired
- **Ask me like:** "set up the ticket service", "why can't I join a ticket server?"

### 39.4 Client integrity: signed manifest and launch check
- **What:** Builds carry `Data/manifest.json` listing every shipped file's size and SHA-256, signed with ed25519 when the owner has a key. The launcher and the player check the install against it before a game starts. Off by default; Report logs, Enforce refuses.
- **Parts:**
  - 39.4.1 Manifest writer - `shared/AntiCheatIntegrity.hpp` - `WriteBuildManifest` (called by the editor's Build Game), `SignIntegrity`, `LoadSigningKey` from `OKAY_AC_SIGNING_KEY_FILE` or `OKAY_AC_SIGNING_KEY`
  - 39.4.2 Verifier - `shared/AntiCheatIntegrity.hpp` - `VerifyInstall` fails on a missing or changed file, an unlisted program file (`.exe`, `.dll`, `.asi`, hidden ones too), a linked file or folder, a bad signature, or an untrusted signer
  - 39.4.3 Start decision - `shared/AntiCheatIntegrity.hpp` - `DecideStart` and `CheckLaunch`: Off, Report or Enforce from the build (`anticheat=` in `game.okayconfig`), `OKAY_ANTICHEAT` (stricter only) and trusted keys
  - 39.4.4 Ed25519 and SHA-512 - `shared/AntiCheatCrypto.hpp` - libsodium when present, otherwise a portable TweetNaCl port with libsodium's extra checks (canonical S, no small-order R or key); tested against RFC 8032 vectors
  - 39.4.5 File hashing and safe paths - `shared/OfficialGame.hpp` - `okayofficial::Verify` (size plus SHA-256) and path checks shared with the official-game updater
  - 39.4.6 Launcher and player hooks - `launcher/main.cpp` (L1181), `player/main.cpp` (L1405) - launcher checks the official game and library games with a `game.okayconfig`; the player checks before anything loads; a player built with trusted keys needs a valid signed manifest and takes its mode from it
  - 39.4.7 Build Settings option - `editor/main.cpp` (Output > Anti-cheat) - Off, Report, Enforce; writes `anticheat=`
- **Code:** `shared/AntiCheatIntegrity.hpp`, `shared/AntiCheatCrypto.hpp`, `shared/OfficialGame.hpp`, `launcher/main.cpp`, `player/main.cpp`
- **API:** `okayac::Mode`, `VerifyInstall`, `CheckLaunch`, `DecideStart`, `WriteBuildManifest`, `ed25519::Verify`, env `OKAY_ANTICHEAT`, `OKAY_AC_TRUSTED_KEY` (local test builds only)
- **Editor:** File > Build Settings > Output > Anti-cheat
- **Docs:** `docs/anticheat.md` (P3: client integrity, The manifest, Keys, Crypto, Limits)
- **Tests:** `anticheat_integrity`, `official_game`
- **Status:** Partial - the code and its tests are complete; whether the released launcher trusts any key depends on the repository variable `OKAY_AC_TRUSTED_KEYS` (the release only warns when it is empty), so by default this is telemetry; client-side checks can always be patched out (docs "Limits")
- **Open:** set `OKAY_AC_TRUSTED_KEYS` so the check becomes a failure (planned in `release.yml`); hashing every file costs time on big games; the launcher always uses the portable crypto (it doesn't link the engine)
- **Related:** 17.13, 17.14, 18.6, 39.7
- **Words:** integrity, manifest.json, signed manifest, ed25519, SHA-256, tampered build, launch check, Report, Enforce, trusted keys, game.okayconfig, anticheat=
- **Seen as:** "The game refuses to start" after editing a file; a launcher warning in report mode; a proxy `version.dll` next to the exe is flagged
- **Ask me like:** "sign my build and enforce the check in the launcher", "why does the launcher refuse my modded game?"

### 39.5 Runtime checks and the clock heartbeat
- **What:** Checks that run while a game plays on Windows: a debugger, an unknown loaded module, and a game clock that runs fast or slow (speed hacks). The decision logic is plain code tested everywhere; the probes are Windows-only.
- **Parts:**
  - 39.5.1 Debugger check - `shared/AntiCheatChecks.hpp` - `IsDebuggerPresent` and `CheckRemoteDebuggerPresent` every 5 s
  - 39.5.2 Module allowlist - `shared/AntiCheatChecks.hpp` - Windows folder, the game's own manifest-listed files and the Steam and Discord overlays are allowed; anything else is reported once (false positives from overlays and recorders, so Report first)
  - 39.5.3 Local clock heartbeat - `shared/AntiCheatChecks.hpp` - `ClockHeartbeat` with an HMAC challenge against a clock NTP can't step; 5 s windows, flagged past 5%, three in a row
  - 39.5.4 Runtime guard - `shared/AntiCheatChecks.hpp` - `RuntimeGuard` runs the probes from the player's frame loop
  - 39.5.5 Server-verified heartbeat - `Net/ClockCheck` - the server as verifier over the network (`maxClockDrift`); a client that never answers is flagged after 10 s
- **Code:** `shared/AntiCheatChecks.hpp`, `Net/ClockCheck`
- **API:** `okayac::ClockHeartbeat`, `AnswerChallenge`, `RuntimeGuard`, `ModuleAllowlist`, `.okayac` key `maxClockDrift`
- **Editor:** Build Settings tooltip warns about false positives
- **Docs:** `docs/anticheat.md` (The runtime checks, Limits)
- **Tests:** `anticheat_checks`, `net_anticheat_ph3`
- **Status:** Partial - logic tested on every platform; the Windows probes themselves can't run in CI; local checks are bypassable, the server-verified version is the strong one
- **Open:** no kernel driver, memory scanning or obfuscation (by design; the opt-in extras are in 39.12); older clients never answer the heartbeat, so turn `maxClockDrift` on only when every player has the update
- **Related:** 16.6.6, 39.4, 39.6
- **Words:** debugger, loaded module, DLL injection, speedhack, clock drift, heartbeat, RuntimeGuard, allowlist, ClockHeartbeat, challenge response
- **Seen as:** the game closes in Enforce mode with "debugger detected"; an overlay is reported as an unknown module; "clock drift +50%" for a player
- **Ask me like:** "why was RTSS flagged?", "turn on the server clock check"

### 39.6 Reports, bans, appeals and the hardware hash
- **What:** The human side of anti-cheat: players report cheaters, games report what their checks found, admins ban an account or a machine, players see and appeal their bans. Everything is enforced by Supabase row-level security and logged to `admin_log`.
- **Parts:**
  - 39.6.1 Backend calls - `shared/AntiCheatBackend.hpp` - every call is built as a `Request` and its answer parsed by a plain function, so both are unit tested without a server
  - 39.6.2 SQL schema - `docs/sql/anticheat.sql` - `ac_reports`, `ac_report_servers`, `ac_bans`, `ac_appeals`; `ac_is_banned` (ticket service and admins only), `ac_my_bans`, `ac_file_appeal` (one waiting, 3 per ban), `ac_purge_old()` (90-day retention, `pg_cron` job if available)
  - 39.6.3 Hardware hash - `shared/AntiCheatHardware.hpp` - HMAC-SHA256 of machine id, volume serial and CPU brand with a salt (`OKAY_AC_HW_SALT`); no machine id means no hash, so such a player is banned by account only
  - 39.6.4 Launcher admin pages - `launcher/main.cpp` (L12335-12860) - Admin > Anti-cheat: Reports, Bans (Ban, Unban with a reason), Appeals (Accept, Reject with a note)
  - 39.6.5 Player side - `launcher/main.cpp` (L12724-12860) - ban banner on every page, Appeal box, Report cheating in a chat's menu; a game with anti-cheat on files one `client_check` report per run
  - 39.6.6 Local SQL test - `tests/scripts/test_anticheat_sql.sh`, `tests/sql/test_anticheat.sql`, `tests/sql/supabase_stub.sql` - runs `anticheat.sql` on a throwaway local Postgres with a stand-in for Supabase's `auth` schema; never point it at a live database
- **Code:** `shared/AntiCheatBackend.hpp`, `shared/AntiCheatHardware.hpp`, `docs/sql/anticheat.sql`, `launcher/main.cpp`, `tests/scripts/test_anticheat_sql.sh`, `tests/sql/test_anticheat.sql`, `tests/sql/supabase_stub.sql`
- **API:** `okayac::backend::Request`, `okayac::HardwareHash`, SQL `ac_is_banned`, `ac_my_bans`, `ac_file_appeal`, `ac_purge_old`, env `OKAY_SQL_TEST_PGHOST`, demo hooks `OKAY_UI_DEMO_ACVIEW`, `OKAY_UI_DEMO_ACBAN`
- **Editor:** none (launcher: Admin > Anti-cheat, ban banner, Report cheating...)
- **Docs:** `docs/anticheat.md` (The backend, In the launcher, Hardware hash, Retention, Setting it up)
- **Tests:** `anticheat_backend`, `anticheat_sql`
- **Status:** Works - request and parse code is tested; the SQL is tested on local Postgres when one is available
- **Open:** possible gap: `anticheat_sql` is skipped (exit 77) in CI because no job sets `OKAY_SQL_TEST_PGHOST`, so the row-level-security rules are never checked there; the launcher admin and appeal UI has no automated test; a "server_check" report kind is a one-word SQL change waiting on the owner (ROADMAP)
- **Related:** 17.9, 39.3, 16.5.4, 41.6
- **Words:** report, ban, unban, appeal, ac_reports, ac_bans, hardware hash, hw_hash, machine ban, admin_log, ban banner, Report cheating, retention
- **Seen as:** a ban banner on the launcher with an Appeal button; a new account on the same PC still banned; reports never appear for admins
- **Ask me like:** "ban this machine for a week", "what does an appeal look like for an admin?"

### 39.7 Trusted keys and signing keys at build time
- **What:** How public keys get into binaries and how private ones stay out. The launcher is built with the owner's trusted public keys; the editor signs with a private key from the environment; the CMake check refuses a malformed key list.
- **Parts:**
  - 39.7.1 Key list check - `cmake/OkayAntiCheatKeys.cmake` - `okay_ac_check_trusted_keys()` needs exactly 64 hex digits per key, comma separated, no empty entries; a bad list stops the configure instead of building a program that trusts nothing
  - 39.7.2 CMake options - `CMakeLists.txt` - `OKAY_AC_TRUSTED_KEYS` (launcher, official game only), `OKAY_AC_PLAYER_TRUSTED_KEYS` (a player of your own signed games), `OKAY_AC_HW_SALT`
  - 39.7.3 CI check of the check - `tests/anticheat_keys_check.cmake`, `tests/CMakeLists.txt` - `anticheat_keys_ok`, `anticheat_keys_two_ok` and five `anticheat_keys_bad_*` tests that must fail (`WILL_FAIL`)
  - 39.7.4 Release warning - `.github/workflows/release.yml` - warns in the log and job summary when the repository variable `OKAY_AC_TRUSTED_KEYS` is empty
  - 39.7.5 Key hygiene - `docs/anticheat.md` - two key pairs (dev and release), private halves outside the repo, `OKAY_AC_SIGNING_KEY_FILE`; small-order keys are refused
- **Code:** `cmake/OkayAntiCheatKeys.cmake`, `tests/anticheat_keys_check.cmake`, `CMakeLists.txt`, `.github/workflows/release.yml`
- **API:** `-DOKAY_AC_TRUSTED_KEYS=<hex>,<hex>`, `-DOKAY_AC_PLAYER_TRUSTED_KEYS`, `-DOKAY_AC_HW_SALT`, `OKAY_AC_SIGNING_KEY_FILE`, repository variable `OKAY_AC_TRUSTED_KEYS`
- **Editor:** none (the editor signs at Build Game when a key is in the environment)
- **Docs:** `docs/anticheat.md` (Keys: setup for the owner)
- **Tests:** `anticheat_keys_ok`, `anticheat_keys_two_ok`, `anticheat_keys_bad_*` (five generated names)
- **Status:** Works - the check and its tests are small and complete
- **Open:** the release only warns on an empty variable ("a warning for now; once the variable is set this becomes a failure"); no key has been created in the repo by design, so whether the owner set one is outside the repo
- **Related:** 39.4, 18.4, 41.2
- **Words:** trusted keys, OKAY_AC_TRUSTED_KEYS, public key, signing key, ed25519, hex, CMake check, key rotation, salt
- **Seen as:** CMake configure fails with "isn't an ed25519 public key"; the release log warns "No trusted anti-cheat keys"
- **Ask me like:** "add a second trusted key for rotation", "why does my configure fail on the key list?"

### 39.8 Relay hardening
- **What:** The relay is reachable from the internet, so it trusts nothing: it forwards only host to client within one session, hands out unguessable slots, makes a spoofed hello useless, and caps everything.
- **Parts:**
  - 39.8.1 Session isolation - `Net/RelayServer` - data flows only between a session's host and its other peers, never peer to peer or across sessions (#254)
  - 39.8.2 Host secret - `Net/RelayProtocol` - a version-2 host gets a secret key; only a hello carrying it can host the code; version-1 keeps the old rule (voice calls)
  - 39.8.3 Address cookie - `Net/RelayServer` - a version-3 new peer first gets an HMAC cookie of its address and the minute and gets a slot only when it echoes it, so a spoofed source can't fill quotas
  - 39.8.4 Caps and rate limits - `Net/RelayServer` - 4,096 peers, 2,048 sessions, 32 peers per session, 64 peers and 16 sessions per address, 1,000 packets and 1 MB per second per peer, hellos 10 per second per address, random slots
  - 39.8.5 Legacy hellos - `relay/main.cpp` - `--legacy-hello=off` refuses version-1 and -2 hellos; leave on until the log's legacy count is zero
  - 39.8.6 Welcome nonce - `Net/RelayProtocol` - a version-4 game puts a random nonce in its hellos and ignores any welcome that doesn't echo it, so a forged welcome can't pair, redirect or end a session; an older relay needs `allowUnverifiedRelay`
- **Code:** `Net/RelayServer`, `Net/RelayProtocol`, `relay/main.cpp`
- **API:** `RelayServer::SetLegacyHello`, `kMaxPeers`, `kPacketsPerSecond`, `RelayRefused`, flag `--legacy-hello`
- **Editor:** none
- **Docs:** `docs/relay.md` (What the relay enforces), `docs/anticheat.md` (relay hosted games)
- **Tests:** `relay_security`, `net_relay_hardening`, `net_anticheat_ph2`, `net_relay`
- **Status:** Works - flood, isolation, cookie and slot cases are in `relay_security`; limits read in the header
- **Open:** whether the live relay runs the cookie version can't be told from the repo (ROADMAP item 7); launcher voice calls now send v3 hellos with a fallback to v1 (read in `VoiceChat.hpp`), so `--legacy-hello=off` is possible once the log's legacy count is zero
- **Related:** 16.14, 16.7
- **Words:** relay, hardening, cookie, hello, slot, flood, rate limit, session isolation, legacy hello, spoofed source
- **Seen as:** "relay refused"; a flood fills the relay; a stranger can't take over a code
- **Ask me like:** "turn legacy hellos off on the relay", "what limits does the relay enforce?"

### 39.9 Script and file path safety
- **What:** Scripts and visual scripts come with downloaded game content, so every file name they pass to save, prefs, data, build or scene calls must stay inside the game's folders: no absolute paths, no `..`; saves land in the game's save folder. Prefabs that the network can spawn must be safe and registered.
- **Parts:**
  - 39.9.1 Safe relative path - `Core/SaveData` - `Save::IsSafeRelative(name)`
  - 39.9.2 OkayScript builtins - `Scripting/OkayScriptVM` - file-taking builtins call the check (L144)
  - 39.9.3 Visual script nodes - `VisualScript/NodesExtra` - the load-scene node refuses unsafe names; `Components/ActionList` does the same for `save_prefs` and scene actions
  - 39.9.4 Safe network spawns - `Net/AntiCheat` - only registered prefabs may be network-spawned (#351)
  - 39.9.5 Launcher shell guard - `launcher/main.cpp` (L233) - `ShellSafeUrl` allows only plain http(s) URL characters before a URL reaches curl, wget, PowerShell or cmd
- **Code:** `Core/SaveData`, `Scripting/OkayScriptVM`, `VisualScript/NodesExtra`, `Components/ActionList`, `launcher/main.cpp`
- **API:** `Save::IsSafeRelative`, `ShellSafeUrl`
- **Editor:** none
- **Docs:** `docs/anticheat.md` (spawn allowlist)
- **Tests:** `script_paths`, `net_anticheat`
- **Status:** Works - `script_paths` runs scripts against absolute and `..` names in a temp folder
- **Open:** no test names `ShellSafeUrl` (launcher code, no unit test); paths inside built-in components other than scripts and actions were not audited here
- **Related:** 17.11, 13.1, 1.7
- **Words:** path traversal, absolute path, .., IsSafeRelative, script safety, sandbox, save folder, safe path, ShellSafeUrl
- **Seen as:** a script's file call returns false or does nothing for an absolute path; a downloaded game tries to write outside its folder
- **Ask me like:** "can a script read my home folder?", "add the path check to the new node"

### 39.10 Data packs and obfuscation
- **What:** An optional light obfuscation of shipped scenes and config so players can't casually read or edit them in a text editor. Explicitly not DRM: the key is in the binary.
- **Parts:**
  - 39.10.1 Pack format - `Core/DataPack` - `OKP1` magic, nonce, FNV-1a 64-bit checksum, splitmix64 keystream XOR; the key is baked into the engine (`DefaultKey`)
  - 39.10.2 Use in Build Game - `editor/main.cpp` (`builder::Build`) - optional pack step; the player unpacks transparently at load
- **Code:** `Core/DataPack`, `editor/main.cpp`
- **API:** `DataPack::Pack`, `Unpack`, `DefaultKey`
- **Editor:** File > Build Settings (obfuscate data option)
- **Docs:** `docs/save_games.md` (data packs are listed with saves in the old map)
- **Tests:** `datapack`
- **Status:** Basic - works as obfuscation only; the header says so
- **Open:** the key is the same in every build and the checksum is not cryptographic; do not describe this as encryption to users
- **Related:** 1.7, 18.6
- **Words:** data pack, DataPack, obfuscation, OKP1, scrambled scenes, tamper, xor, not DRM
- **Seen as:** scene files in a built game aren't readable as text; a modified pack fails its checksum
- **Ask me like:** "is the data pack encrypted?", "turn on data obfuscation for the build"

### 39.11 Account, backend and updater hardening
- **What:** The smaller defences around sign-in, Supabase and downloads, and the places where they are weak.
- **Parts:**
  - 39.11.1 Row-level security first - `docs/accounts.md`, `docs/sql/social.sql` - the Supabase key in builds is public, so every table is protected by RLS and SECURITY DEFINER functions; reactions are gated by `can_see_reaction_target`
  - 39.11.2 Never give a server the service key - `docs/anticheat.md` - servers hold only the ticket service's public key; `ac_is_banned` is callable by the ticket service and admins only
  - 39.11.3 Local account passwords - `Platform/Account/AccountService` - local backend stores a salted hash (50,000 chained SHA-256 rounds) and compares in constant time; remote auth sends secrets to curl through files, not argv
  - 39.11.4 Official game downloads - `shared/OfficialGame.hpp` - manifest is untrusted input, every path checked before joining to the install folder, size and SHA-256 verified, journaled swap
  - 39.11.5 Safe project delete - `shared/ProjectDelete.hpp` - never a drive root, home, launcher or projects folder
  - 39.11.6 Reference account server - `examples/account-server/server.py` - PBKDF2-HMAC-SHA256 with 200,000 rounds; says "not production-ready"
  - 39.11.7 Updater trust - `launcher/main.cpp` (L136-326) - downloads over HTTPS with a version string and URL-character check only
- **Code:** `Platform/Account/AccountService`, `shared/OfficialGame.hpp`, `shared/ProjectDelete.hpp`, `examples/account-server/server.py`, `docs/sql/social.sql`, `docs/sql/admin.sql`
- **API:** `okayofficial::Verify`, `AccountService::VerifyToken`, `SecureEquals`
- **Editor:** none
- **Docs:** `docs/accounts.md` (Security: the key is public), `docs/supabase-backend.md`
- **Tests:** `account`, `official_game`, `project_delete`
- **Status:** Partial - several solid pieces, but the update path is not signed (17.11) and some weak spots remain
- **Open:** possible weaknesses: the local password hash is a hand-made chain of SHA-256 rather than a standard KDF, and its salt comes from a Mersenne Twister seeded with two `random_device` words; `server.py` has no request size cap, no rate limit and keeps tokens only in memory; the updater has no signature check; the default Supabase URL and key sit in `CMakeLists.txt`
- **Related:** 17.2, 17.11, 17.14, 42.4
- **Words:** RLS, row level security, service_role, password hash, PBKDF2, SHA-256, constant time, updater signing, SECURITY DEFINER, secrets, curl
- **Seen as:** a token or key in a log; a download from a hostile server being trusted; weak local passwords
- **Ask me like:** "list the weak spots in the account code", "sign the updater with the anti-cheat key"

### 39.12 Client hardening: protected values, code and import integrity, attestation, input analysis (opt-in)
- **What:** User-mode, opt-in extras on top of 39.4 and 39.5, all client side and all producing weighted signals (weight 1 to 6, never a ban or a forced exit). A value type that hides health or ammo from naive memory editors, a signed hash of the player's own code sections checked at run time, inline-hook and import-table checks on a short list of system functions, a signed module blocklist/allowlist file that changes without a rebuild, extra debugger and hypervisor hints, a QPC / tick / server-time clock cross-check, a server-challenged heartbeat attestation, and pure-logic input analysis (turbo, macros, impossible mouse moves, camera not explained by input) a server can call.
- **Parts:**
  - 39.12.1 Protected values - `shared/AntiCheatProtected.hpp` - `okay::Protected<T>`: keyed XOR, a new key on every write, a keyed canary, a plain decoy copy that detects an edited address; thread safe; tamper handler
  - 39.12.2 Code baseline and scanner - `shared/AntiCheatPe.hpp` - PE parsing from byte buffers, relocation-aware section hash, `CodeBaseline` JSON (`okayspace-code/1`, ed25519 signed like the manifest), `CodeScanner` hashing 64 KiB per 100 ms
  - 39.12.3 Build tool - `tools/okay_ac_tool.cpp` - `baseline`, `sign-rules`, `verify`; built with `-DOKAY_BUILD_TOOLS=ON`
  - 39.12.4 Hook, import, rules, hints, clocks - `shared/AntiCheatHardening.hpp` - `CheckApiEntries` (decode the entry bytes, judge where a jump lands), `CheckImports` (IAT), `ModuleRules` (`okayspace-modules/1`, name and signer rules, higher version wins), `CheckHints`, `ClockCrossCheck`, the `Hardening` scheduler, Windows probes (`winprobe`, `SetupHardening`)
  - 39.12.5 Guard hook-up - `shared/AntiCheatChecks.hpp`, `player/main.cpp` - `Finding::weight`, `GuardProbes::hardening` / `moduleOk`, `RuntimeGuard::Score()`; signals are logged and filed in the one `client_check` report; enforce mode never exits for them
  - 39.12.6 Heartbeat attestation - `shared/AntiCheatAttest.hpp` - nonce challenge, HMAC tag under a key derived from the session key, a hash of eight integrity facts, replay and expiry rules, wire format (messages `0xA1` / `0xA2`)
  - 39.12.7 Input analysis - `shared/AntiCheatInput.hpp` - `AnalyzeIntervals`, `AnalyzeMouse`, `AnalyzeInputVsCamera`, `InputAnalyzer`; standard library only
  - 39.12.8 Windows self-test - `tests/win/ac_selftest.cpp`, `scripts/ac_wine_selftest.sh` - cross-compiled with MinGW, run under wine at the preferred and at a relocated base
- **Code:** `shared/AntiCheatProtected.hpp`, `shared/AntiCheatPe.hpp`, `shared/AntiCheatHardening.hpp`, `shared/AntiCheatAttest.hpp`, `shared/AntiCheatInput.hpp`, `shared/AntiCheatChecks.hpp`, `tools/okay_ac_tool.cpp`, `player/main.cpp`
- **API:** `okay::Protected<T>`, `okay::SetProtectedTamperHandler`, `okayac::pe::CodeBaseline`, `okayac::pe::CodeScanner`, `okayac::ModuleRules`, `okayac::Hardening`, `okayac::SetupHardening`, `okayac::attest::Verifier` / `Client`, `okayac::input::InputAnalyzer`, env `OKAY_AC_HARDENING`, files `Data/codebaseline.json`, `.anticheat/modules.json`
- **Editor:** none (no Build Settings switch yet; the tool runs by hand after Build Game)
- **Docs:** `docs/anticheat.md` (Client hardening), `docs/anticheat_comparison.md`
- **Tests:** `anticheat_protected`, `anticheat_hardening`, `anticheat_attest`, `anticheat_input`; `scripts/ac_wine_selftest.sh` (by hand)
- **Status:** Partial - the logic and its tests run everywhere; the Windows probes ran under wine on a MinGW build, not on real Windows; the attestation is not wired into the network layer; the whole layer is user mode and can be patched out
- **Open:** run it on real Windows in Report mode and look at the false positives (overlays, recorders, antivirus); wire the attestation messages into `NetworkManager` and feed `NoteServerTime`; an unbacked-executable-memory check, hash rules and a delivery channel for the module rules were not built; no Build Settings option runs the baseline tool; no TPM or Secure Boot attestation (self-reported values would be weak, real attestation is large)
- **Related:** 39.4, 39.5, 39.6, 16.6
- **Words:** protected value, Protected<T>, memory editor, code baseline, .text hash, relocation, inline hook, IAT hook, module blocklist, okayspace-modules, signer, hypervisor, debugger hint, clock cross-check, attestation, heartbeat, replay, turbo, macro, input analysis, camera mismatch, okay_ac_tool
- **Seen as:** `code(w6)`, `hook(w5)`, `iat(w4)`, `module-blocked`, `clock-cross` lines in `anticheat.log`; "the code baseline is signed, but this build has no trusted keys: it is telemetry, not proof"
- **Ask me like:** "make health harder to edit with Cheat Engine", "block this cheat DLL without a new build", "how does the server know the client is the real build?"
