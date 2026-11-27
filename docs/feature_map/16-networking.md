# 16. Networking & multiplayer

Everything that lets two or more players share a game: the UDP transport and its encryption, the host or dedicated
server that stays in charge, snapshots, prediction, lag compensation, rooms, matchmaking, the server browser, the relay
for players behind home routers, voice, and the spawning of remote players. About 11,200 lines in
`engine/include/okay/Net` and `engine/src/Net`, plus `relay/` and the network components. Anti-cheat has its own
cross-cutting page (39); here it appears only as the server-side part (16.6). Nobody is touching it right now; the
networking pull requests (net plan 5a to 5e, anti-cheat phases 1 to 5, hardening items 1 to 14) are all on main.

## 16. Networking & multiplayer

### 16.1 Transport, sealed channels, packets
- **System:**
  - Networking - Sends game state between players over the internet: packets, sync, prediction, remote calls and networked players. Code: `Net/UdpSocket`, `Net/Packet`, `Net/SecureChannel`, `Net/NetChannels`, `Net/NetTransport`, `Net/NetworkManager`
    Home: `engine/include/okay/Net/`, `engine/src/Net/`. Entry: `engine/include/okay/Net/NetworkManager.hpp`. Also called: NetworkManager, multiplayer, netcode, replication, RPC, SyncVar.
  - Cryptography - Hashing, signing and encryption (SHA-256, ChaCha20-Poly1305, X25519, Ed25519) for network traffic, accounts and file checks; libsodium stays the default until an outside review (CLAUDE.md rule 18). Code: `Crypto/Crypto`, `Crypto/Sha256`, `Net/NetCrypto`, `engine/src/Crypto/*.cpp`
    Home: `engine/include/okay/Crypto/`, `engine/src/Crypto/`. Entry: `engine/include/okay/Crypto/Crypto.hpp`. Also called: okay::crypto, own crypto, OKAY_OWN_CRYPTO, libsodium, Sha256, ChaCha20.
- **What:** The UDP layer under all multiplayer: sockets, byte packets that are split when large, authenticated encryption with libsodium (or, opt-in and unreviewed, our own okay::crypto), and two per-channel delivery modes on top of plain UDP (reliable ordered, and unreliable where only the newest counts).
- **Parts:**
  - 16.1.1 UDP socket and endpoints - `Net/UdpSocket` - non-blocking UDP, an `Endpoint` (IPv4 address and port) that can key a table, wait-until-readable for the relay loop
  - 16.1.2 Packet buffer and fragmentation - `Net/Packet` - little-endian byte buffer (byte layout pinned by `net_packet_layout`); sends over 1200 bytes are split and reassembled; receive buffer is 64 KB; a reliable message in pieces is acknowledged piece by piece (`FragAck`) and its resend sends only the missing pieces (16.15.19)
  - 16.1.3 Sealed channel - `Net/SecureChannel` - X25519 key pair per peer, one key per direction, XChaCha20-Poly1305 sealing, opt-in sequence numbers (replay window of 64); a harmless stub when built without a crypto backend
  - 16.1.4 Reliable ordered and sequenced channels - `Net/NetChannels` - one ordered stream per named channel (resent until a cumulative ack) and unreliable-sequenced (newer replaces older); wire types 30 to 32
  - 16.1.5 Transport interface - `Net/NetTransport` - `INetTransport` seam with a Native provider that wraps NetworkManager (Photon provider: 16.10)
  - 16.1.6 Own crypto, side by side (UNREVIEWED, off by default) - `Crypto/Crypto`, `Net/NetCrypto` - our own ChaCha20, Poly1305, ChaCha20-Poly1305 and XChaCha20-Poly1305, BLAKE2b, SHA-256/512 (SHA-256, HMAC-SHA256 and PBKDF2 are one header, `Crypto/Sha256.hpp`, also used by account passwords, file checksums and the anti-cheat tag; always on), X25519, Ed25519, Kx session keys and OS random; `-DOKAY_OWN_CRYPTO=ON` makes the net layer use them instead of libsodium; never the default until an outside expert has reviewed them
- **Code:** `Net/UdpSocket`, `Net/Packet`, `Net/SecureChannel`, `Net/NetChannels`, `Net/NetTransport`, `Net/NetworkManager`, `Net/NetCrypto`, `Crypto/Crypto` (sources `engine/src/Crypto/*.cpp`), CMake option `OKAY_OWN_CRYPTO`
- **API:** `NetworkManager::StartServer`, `StartClient`, `Stop`, `encryption`, `SendOrdered`, `SendSequenced`, `SecureChannel::Available`, `INetTransport`, `NetTransport::Get`, `okay::crypto::XChaCha20Poly1305Seal`, `Ed25519Sign`, `X25519`, `RandomBytes`
- **Editor:** Inspector: Network Manager has no encryption field (set `encryption` in code or script; Anti-Cheat Config has Require encryption); View > Panels > Services has Host Game, Join and Disconnect for a quick test
- **Docs:** `docs/online/relay.md`, `docs/online/anticheat.md` (Network hardening), `docs/online/multiplayer.md`, `docs/platform/crypto.md` (own crypto: status, tests, what is not done)
- **Tests:** `net`, `net_crypto`, `net_crypto_own`, `net_crypto_sodium`, `crypto`, `crypto_sodium_xcheck`, `crypto_python_xcheck`, `crypto_fuzz_smoke`, `net_auth`, `net_hardening`, `net_races`, `net_anticheat_p2`, `net_anticheat_ph3`, `net_backend`, `net_reconnect`, `net_rejoin`, `net_lossy`, `net_fuzz`; sources `tests/test_net.cpp`, `tests/test_net_crypto.cpp`, `tests/test_crypto.cpp`, `tests/crypto_rfc_vectors.hpp`, `tests/test_crypto_sodium_xcheck.cpp`, `tests/scripts/crypto_xcheck.py`, `tests/fuzz_crypto.cpp`
- **Connection loss:** a joined client leaves the session when the host sends nothing for `serverTimeoutSeconds` (10; `ConnectionLost()` says why), and a direct join the host never answers gives up after `joinTimeoutSeconds` (15; `JoinRejected()`); 0 turns either off. `SendOrdered` keeps messages past its 32-message window in a per-channel backlog (4096 messages / 8 MiB) instead of dropping them. Tests: `net_multiplayer_fixes`; see `docs/online/multiplayer.md`
- **Reconnect and resume:** `autoReconnect` (off by default) makes a client that lost its host rejoin by itself: `reconnectMaxAttempts` tries (5), waiting `reconnectDelaySeconds` (1) then doubling up to `reconnectMaxDelaySeconds` (15); a refusal that cannot change ("wrong password", "authentication failed", a kick) ends it at once. `Reconnecting()`, `ReconnectAttempt()`, `ReconnectGaveUp()`, `Reconnects()`, `Resumed()` and `SetReconnectHandler` report it; direct, relay and join-link sessions are remembered. The host keeps a dropped client's state for `resumeGraceSeconds` (30; at most 64) under a secret token its Welcome gave it, and a rejoin with that token gets the same peer id, score, ready flag and last position back (`SetPeerResumedHandler` on the host); objects the player made with `SpawnOwned` are announced again. Tests: `net_reconnect`
- **Status:** Works - client and server talk over loopback in `net`; sealing, replay and malformed-packet cases have tests. NetChannels (#423) is wired into NetworkManager but no test names it. Our own crypto (16.1.6) passes every RFC vector and agrees with libsodium and Python on random inputs, but it is unreviewed and off by default
- **Open:** possible gap: the Ubuntu CI job installs no libsodium-dev, so `net_crypto` and `net_anticheat_p2` may only run their "no libsodium" branch there (unverified: depends on the runner image); `net_races` says it is meant for ThreadSanitizer but CI runs it only under ASan/UBSan; no test names `NetChannels` or `UdpSocket` directly; own crypto (16.1.6): no outside review, no timing measurement of the built code, no Windows or macOS run yet, no libFuzzer run in CI, X25519/Ed25519 are slower than libsodium's
- **Related:** 16.2, 16.3, 16.10, 39.2
- **Words:** UDP, socket, datagram, packet, fragment, MTU, encryption, libsodium, SecureChannel, XChaCha20, X25519, crypto_kx, okay::crypto, OKAY_OWN_CRYPTO, own crypto, Ed25519, BLAKE2b, Poly1305, sealed, replay window, reliable, ordered, unreliable, sequenced, channel, SendOrdered, SendSequenced, OrderedEnd, SequencedRx
- **Seen as:** a client can't connect or says "encryption required"; messages arrive late or out of order; a big message never arrives; a build says "built without libsodium"
- **Ask me like:** "make this event reliable and ordered but let position updates drop", "why does the join say encryption required?"

### 16.2 Server authority, prediction, lag compensation
- **System:** Networking
- **What:** How the server stays in charge of movement: the move-speed check, a fixed network tick, movement run from the player's inputs on the server, the client predicting its own player and being corrected, other players shown from an interpolation buffer, and shots checked against where things were when the shooter fired.
- **Parts:**
  - 16.2.1 Move check and corrections - `Net/NetworkManager` - `maxMoveSpeed` snaps a too-fast client back, `ServerTeleport` for legit jumps, `kickAfterViolations`, `PeerViolations`, `Corrections`
  - 16.2.2 Fixed network tick - `Net/NetworkManager` - `netTickRate` (script `net_tick_rate`, server `--fixed-tick`): snapshots carry the tick, clients send once a tick, `NetTick`, `OnNetTick`
  - 16.2.3 Authoritative movement - `Net/PlayerInput`, `Net/NetworkManager` - clients send inputs; the server runs the controller's `Simulate` at the tick; `IPlayerSim`, `minPressInterval` drops a Jump or Fire pressed every tick
  - 16.2.4 Prediction and reconciliation - `Net/NetworkManager` - the local pawn moves at once; the host acks each input; `Predicting`, `PredictionCorrections`, `PredictionSnaps`
  - 16.2.5 Interpolation buffer - `Net/NetworkManager` - other players are shown 3 ticks (100 ms at 30 Hz) in the past; the delay grows to what the packets need (never past a second)
  - 16.2.6 Lag compensation - `Net/NetworkManager` - the host keeps about 1 s of poses; a hit claim names the tick the shooter saw; rewind is clamped to 200 ms plus the shooter's round trip (`maxRewindRtt`); `RewindRaycast`
  - 16.2.7 Ownership gating - `Net/NetOwnership` - `IsLocallyControlled()` lets only the owning peer's input drive a controller; remote copies are left to NetworkSync
  - 16.2.8 Host-owned inventory - `Components/NetInventory` - clients ask with inv/* requests, the host checks, applies and sends a versioned snapshot (source is `engine/src/Inventory/NetInventory.cpp`)
- **Code:** `Net/NetworkManager`, `Net/PlayerInput`, `Net/NetOwnership`, `Components/NetInventory`
- **API:** `NetworkManager::authoritativeMovement`, `netTickRate`, `maxMoveSpeed`, `interpolationRate`, `ServerTeleport`, `IPlayerSim`, `PlayerInput`, `IsLocallyControlled`, scripts `net_tick_rate`, `net_authoritative`, `net_set_pawn`, `net_max_speed`, `net_teleport`
- **Editor:** Inspector: Network Manager (Fixed Net Tick, Authoritative movement, Correction easing, Snap distance, Smoothing); max move speed has no field (script `net_max_speed`); Net panel to simulate lag (16.13)
- **Docs:** `docs/online/multiplayer.md` (Fixed network tick, Server-authoritative movement, Lag compensation, Server authority), `docs/online/dedicated-server.md`
- **Tests:** `net_authority`, `net_tick`, `net_auth_move`, `net_predict`, `net_interp`, `net_lagcomp`, `mp_controllers`
- **Status:** Works - one test per stage (5a tick, 5b authoritative moves, 5c prediction, 5d interpolation, 5e lag compensation), each through a proxy that delays packets; shipped in releases 2.825 to 2.836 per ROADMAP
- **Open:** no replay or demo recording exists (only packet "replay protection"); `NetInventory` has no test that names it; the old map's "Anti-cheat items 12-13" are merged (#376, #384), so that note is stale; `NetworkManager.cpp` is split into five files by job (batch N1, see 19.4)
- **Related:** 16.3, 16.6, 4.1, 39.1
- **Words:** authority, authoritative movement, server-authoritative, prediction, reconciliation, rubber-banding, interpolation, lag compensation, rewind, netTickRate, NetTick, OnNetTick, IPlayerSim, PlayerInput, ServerTeleport, maxMoveSpeed, minPressInterval, pawn
- **Seen as:** rubber-banding or snapping back when I move online; other players stutter or teleport; shots miss although I aimed right (lag); my player moves a beat late; a player moves faster than allowed
- **Ask me like:** "turn on server-authoritative movement for my shooter", "other players jitter at 100 ms ping, check the interpolation buffer"

### 16.3 Snapshots and sync, interest
- **System:** Networking
- **What:** How object state reaches other peers: the drop-in Network Sync component, shared ids and variables, quantized binary delta snapshots, culling by distance, and an ECS world with replication built in.
- **Parts:**
  - 16.3.1 Network Sync - `Components/NetworkSync` - put it on any object and its position and rotation replicate; authority Host, Mine or Manual; same `netId` on every peer; "Players can claim it" for shared objects like a ball
  - 16.3.2 NetworkObject, NetworkTransform, SyncVar - `Components/NetworkObject` - shared id plus owner, named RPCs "<netId>/<name>", a one-string synced field; sits on the same NetworkManager calls
  - 16.3.3 Binary delta snapshots - `Net/NetDelta` - poses in millimetres, quaternions in 15 bits per component, sent as changes from the baseline the receiver acknowledged; `binarySnapshots` is on by default
  - 16.3.4 Interest management - `Net/NetworkManager` - `interestRadius` sends a client only peers and owned objects near its avatar (grid cell plus radius); 0 = everyone in the room
  - 16.3.5 ECS replication - `ECS/NetWorld`, `ECS/NetWorldSync` - an ECS `World` with networking built in, and a `WorldReplicator`
- **Code:** `Components/NetworkSync`, `Components/NetworkObject`, `Net/NetDelta`, `ECS/NetWorld`, `ECS/NetWorldSync`
- **API:** `NetworkSync` (component), `NetworkObject`, `NetworkTransform`, `SyncVar`, `NetworkManager::RegisterSync`, `syncRate`, `binarySnapshots`, `interestRadius`, `ecs::NetWorld`, `WorldReplicator`
- **Editor:** Inspector: Network Sync (authority, id, claimable), Add Component menu; Net panel shows bytes per object (16.13); `interestRadius` has no inspector field
- **Docs:** `docs/online/multiplayer.md`
- **Tests:** `net_sync`, `net_aoi`, `net_interp`, `net_anim_actions`, `ecs`, `ecs_commands`
- **Status:** Works - transform replication, interest culling and the delta path are exercised by `net_sync`, `net_aoi`, `net_interp`; read `SendSnap` and `OnSnapDelta` in NetworkManagerSync.cpp
- **Open:** the old map listed "Delta snapshots" as open, but they exist and are on by default; no test names `NetDelta` or `QPose` directly; PR #571 (merged after this map was started) gave `ECS/NetWorld.hpp` delta snapshots against the newest acknowledged tick, a command buffer and `docs/engine/ecs.md`; the header's top comment still says "full-state snapshots", and the `ecs_commands` test covers the new code
- **Related:** 16.2, 16.8, 1.9, 16.13
- **Words:** NetworkSync, netId, authority, snapshot, delta snapshot, quantized, baseline, ack, interest management, area of interest, culling, syncRate, NetWorld, WorldReplicator, replicate, SyncVar, claimable
- **Seen as:** an object moves on the host but not on clients; two players see different object positions; too much bandwidth with many objects; a far-away player still pops in; a shared ball is owned by nobody
- **Ask me like:** "make this platform move the same on every client", "only send players who are within 50 units"

### 16.4 Matchmaking, queues, server browser, join links
- **System:** Matchmaking and Lobbies - Finding and joining games, rooms and lobbies, match flow, results and leaderboards.
  Home: `engine/include/okay/Net/`. Entry: `engine/include/okay/Net/Matchmaking.hpp`. Also called: Matchmaker, server browser, lobby, rooms, join links.
- **What:** Finding and joining a game: one-string join links, asking a server who it is without joining, the server browser (internet list, friends, LAN, pings), listing servers in Supabase, and "Find match" queues that put players in a match by game, mode, skill and region.
- **Parts:**
  - 16.4.1 Join links - `Net/JoinLink` - `okay://join?game=...&addr=...` or relay plus code, with room, name and optional password; shared by launcher, chat invites and presence
  - 16.4.2 Server queries and LAN scan - `Net/ServerQuery` - an "OKQ1" UDP query answered by every listen or dedicated server (name, game, map, players, flags, ping); queries are padded so answers are never larger
  - 16.4.3 Server browser data - `Net/ServerBrowser` - background fetch of the public list, LAN scan and pings, filtered to this game; feeds the player's "Find a server" window and `net_browser_*` scripts
  - 16.4.4 Public server listing - `Net/Matchmaking` - `GameSession` rows in the Supabase `game_sessions` table: advertise, heartbeat, list, remove
  - 16.4.5 Matchmaking queue - `Net/Matchmaker` - join a queue on a background thread, poll until matched, `Pump()` connects the scene's NetworkManager to a listed server or to a player who hosts
  - 16.4.6 Rich presence from a session - `Net/SessionPresence` - `DescribeSession` turns a live NetworkManager into what friends see ("Hosting a match 3/8") plus the join link
- **Code:** `Net/JoinLink`, `Net/ServerQuery`, `Net/ServerBrowser`, `Net/Matchmaking`, `Net/Matchmaker`, `Net/SessionPresence`, `player/main.cpp`
- **API:** `JoinLink`, `NetworkManager::JoinByLink`, `QueryServers`, `ServerBrowser::Shared`, `Matchmaker`, `DescribeSession`, scripts `net_browser_open`, `net_browser_refresh`, `net_browser_join`, `net_queue_join`, `net_queue_state`, `net_join_link`, action Open Server Browser
- **Editor:** none; the player draws the "Find a server" window and a "Finding a match" banner (`player/main.cpp`)
- **Docs:** `docs/online/multiplayer.md` (In-game server browser, Public servers, Matchmaking), `docs/online/supabase-backend.md`
- **Tests:** `launcher_join`, `matchmaker`, `matchmaking`, `net_token_policy`
- **Status:** Works - join links, queries, LAN scan, presence and a joining game are in `launcher_join`; the queue uses an in-memory stand-in for the SQL in `matchmaker`
- **Open:** the live Supabase queue and `game_sessions` listing are only checked by hand per the test comments; an `allocate_match` Edge Function (start a server per match) is not written (docs/online/dedicated-server.md)
- **Related:** 16.5, 16.9, 17.8, 17.15, 17.3
- **Words:** matchmaking, queue, Find match, server browser, Quick Play, LAN, ping, join link, okay://join, game_sessions, A2S, ServerBrowser, Matchmaker, MatchRequest, region, skill, JoinByLink
- **Seen as:** the server list is empty; a LAN game doesn't show up; Find match never finds anyone; a pasted join link does nothing; the browser shows the wrong game's servers
- **Ask me like:** "add a Find match button for 4-player deathmatch", "why doesn't my LAN server show in the browser?"

### 16.5 Dedicated server, export, relay
- **System:** Dedicated Server - Runs a game with no window as the server everyone connects to, and exports it.
  Home: `engine/include/okay/Net/DedicatedServer.hpp`. Entry: `engine/include/okay/Net/DedicatedServer.hpp`. Also called: headless server, DedicatedServer, server export.
- **What:** Running any game as a headless authoritative server (no window, GPU or audio) with the same player executable, configured by flags, `server.cfg` or environment variables, with sign-in checks that never stall the tick. Exporting it is 18.7; the relay is 16.14.
- **Parts:**
  - 16.5.1 Headless run loop - `Net/DedicatedServer`, `Core/TickPacer` - `RunDedicatedServer(scene, opts)` steps the scene at a fixed tick (one tick of `Time::Step(1 / tickRate)` per tick, never by the real elapsed time; the pacing arithmetic is `TickPacer`, #581), logs joins and a status line, stops on Ctrl+C or SIGTERM; exit code 2 = could not open the port; it still calls `Game::Reset()` at start (line 548), which keeps a script's time scale (see 1.1)
  - 16.5.2 Options, server.cfg and environment - `Net/DedicatedServer` - `ParseDedicatedServerArgs`, `LoadDedicatedServerConfig`; precedence: config file, then `OKAY_SERVER_*` variables, then flags
  - 16.5.3 Non-blocking sign-in check - `Net/DedicatedServer` - `AsyncTokenCheck` verifies Supabase tokens on a worker, caches 5 minutes, refuses when 64 are already queued
  - 16.5.4 Kick reports - `Net/DedicatedServer` - `KickReporter` files `ac_reports` rows for anti-cheat kicks, at most one per account per hour and 20 an hour
  - 16.5.5 Listing and login - `player/main.cpp` - `--advertise` lists the server in the browser; `--login NAME` signs the machine in (password typed or `OKAY_LOGIN_PASSWORD`), `--logout`
- **Code:** `Net/DedicatedServer`, `Core/TickPacer`, `player/main.cpp`
- **API:** `DedicatedServerOptions`, `RunDedicatedServer`, `ParseDedicatedServerArgs`, `AsyncTokenCheck`, `KickReporter`, flags `--server --port --scene --max-players --tick --name --password --auth --no-encryption --status --run-for --log --advertise --public-addr --region --max-speed --rate-limit --kick-after --anticheat --host-key --report-kicks --fixed-tick --authoritative --config`
- **Editor:** File > Build Settings > Platform: Server (see 18.7)
- **Docs:** `docs/online/dedicated-server.md`
- **Tests:** `dedicated_server`, `net_anticheat_ph1`, `net_anticheat_ph3`, `net_anticheat_ph4`, `net_tick`, `tick_pacer`
- **Status:** Works - argument parsing, async token check and the headless loop players join are in `dedicated_server`; the Linux server is built by the release workflow
- **Open:** `allocate_match` (a server per match on demand) is not written; hosting beyond one VPS (Fly.io, Edgegap, Hathora) is only a docs idea
- **Related:** 16.6, 16.14, 18.7, 41.5, 17.2
- **Words:** dedicated server, headless, server.cfg, --server, okay-player, okay-server-linux-x64, tickRate, maxPlayers, advertise, publicAddr, AsyncTokenCheck, KickReporter, SIGTERM, status line
- **Seen as:** the server exits right away with a misconfigured error; "server is full"; the server isn't in the browser; the server console prints nothing on Windows (needs `--log`)
- **Ask me like:** "start my game as a dedicated server with anti-cheat on", "list my server in the browser"

### 16.6 Anti-cheat (server side)
- **System:** OkayAntiCheat - The server checks what players send, spots cheaters, and handles reports, evidence, bans and appeals; the game's own checks add evidence.
  Home: `engine/include/okay/Net/AntiCheat.hpp`, `shared/AntiCheatBackend.hpp`, `engine/include/okay/AntiCheat/` (planned). Entry: `engine/include/okay/Net/AntiCheat.hpp`. Also called: Server Anti-Cheat, anti-cheat, AntiCheat, okayac, bans, cheat detection. Fix: round 2 of `docs/online/okay_anticheat.md` moves `Net/AntiCheat*`, `Net/CheatDetect*` and `shared/AntiCheat*` into `engine/include/okay/AntiCheat/`.
- **What:** The opt-in checks the server makes on everything a client sends: who may move or spawn what, SyncVar and RPC limits, hit claims, the clock heartbeat and join tickets. The full cross-cutting picture (client integrity, bans, keys) is area 39.
- **Parts:**
  - 16.6.1 Rules, violations and kicks - `Net/AntiCheat` - `AntiCheatSettings` (parsed from `.okayac`), refusal counts, kick after N in a minute, rejoin cooldown that doubles up to an hour, budgets that survive a reconnect for 5 minutes
  - 16.6.2 Anti-Cheat Config component - `Components/AntiCheatConfig` - scene component with inline rules or a `.okayac` file; raises On Cheat Detected; starts safe (encryption required, `maxSyncSpeed 20`, `maxRpcsPerSecond 30`)
  - 16.6.3 Net Validator - `Components/NetValidator` - per-RPC rule: size, calls per second, cooldown, a token-bucket budget (ammo), host-only; `TopUp`, `net_budget`, Server Budget node
  - 16.6.4 Hit claims - `Net/NetworkManager` - `ClaimHit` is checked for shooter alive, fire rate and ammo, origin, range, line of sight and the game's own Validate Hit rule; damage is applied on the server only
  - 16.6.5 Spawn and SyncVar limits - `Net/AntiCheat` - prefab allowlist, spawns per second, live spawns per peer, spawn radius, `serverVar` and `clientVar` key prefixes
  - 16.6.6 Clock heartbeat verifier - `Net/ClockCheck` - the server's side of the HMAC challenge: three off windows in a row, all one way, is drift
  - 16.6.7 Session ticket check - `Net/SessionTicket` - verifies a signed ticket with the public key (details in 39.3)
- **Code:** `Net/AntiCheat`, `Net/ClockCheck`, `Net/SessionTicket`, `Components/AntiCheatConfig`, `Components/NetValidator`
- **API:** `AntiCheat`, `AntiCheatSettings`, `NetworkManager::anticheat`, `SetCheatHandler`, `SetAntiCheatKickHandler`, `ClaimHit`, `ValidateHit`, `NetValidator::TopUp`, scripts `net_anticheat`, `net_anticheat_load`, `net_violations`, `net_hit`, `net_budget`, `on_cheat_detected`, `on_validate_rpc`, nodes On Cheat Detected, On Validate Rpc, Reject Rpc, Server Budget
- **Editor:** Inspector: Anti-Cheat Config and Net Validator (Add Component menu); File > Build Settings > Server page for the basics
- **Docs:** `docs/online/anticheat.md`, `docs/online/multiplayer.md` (Server authority, Anti-cheat), `docs/online/okay_anticheat.md` (OkayAntiCheat audit and plan)
- **Tests:** `net_anticheat`, `net_anticheat_ph1`, `net_anticheat_ph2`, `net_anticheat_ph3`, `net_anticheat_ph4`, `net_anticheat_components`, `net_anticheat_p2`, `net_token_policy`, `net_lagcomp`
- **Status:** Works - nine test files cover each phase; all 14 hardening items are on main per `git log` (#350, #357, #360, #363, #365, #368, #372, #376, #384, #392, #394)
- **Open:** the audit table is `docs/online/anticheat.md` (items 5 and 6 are code on main but not deployed); respawns are not replicated (docs: the server's Health respawns on its own); `maxClockDrift` needs every player updated (older clients never answer)
- **Related:** 39.1, 39.3, 39.5, 16.2, 17.9
- **Words:** anti-cheat, cheat, speed hack, kick, violation, refusal, hit claim, ClaimHit, NetValidator, AntiCheatConfig, .okayac, allowlist, forged syncvar, rpc rejected, hit rejected, clock drift, budget, cooldown
- **Seen as:** a player is kicked for "speed hack"; "forged syncvar" or "rpc rejected" in the log; players can't shoot (no known weapon); a shared ball kicks players; a kicked player can't rejoin for a minute
- **Ask me like:** "limit my rifle to 10 shots then a reload", "why was the player kicked for forged syncvar?"

### 16.7 Voice chat server
- **System:** Voice Chat - Voice calls between friends, groups and server voice rooms.
  Home: `launcher/VoiceChat.hpp`. Entry: `launcher/VoiceChat.hpp`. Also called: voice, VoiceChat, voice server, calls.
- **What:** Voice calls (friend calls, group calls, server voice rooms) from the launcher. Audio goes peer to the relay to the peers over UDP, encrypted with a room key, so nobody opens ports. The "server" is the relay program.
- **Parts:**
  - 16.7.1 Voice engine - `launcher/VoiceChat.hpp` - 16 kHz mono capture, 20 ms frames, voice-activity gate, IMA ADPCM (about 64 kbit/s), ChaCha20 with the room key, per-speaker jitter buffer and mixer; classes `Session`, `MicTest`; no echo cancellation
  - 16.7.2 Relay as hub - `Net/RelayServer` - whoever holds the relay's host slot fans frames out; version-3 hellos with a cookie (#393); the relay never decodes audio
  - 16.7.3 Default voice server - `docs/sql/app_config.sql` - site setting the launcher reads at startup; `OKAY_DEFAULT_VOICE_RELAY` at build time; players can also set one in Settings > Chat
  - 16.7.4 Hosting kit - `deploy/voice-server/Dockerfile` - Docker, compose, systemd and an install script for the relay (see 16.14)
- **Code:** `launcher/VoiceChat.hpp`, `docs/sql/app_config.sql`
- **API:** launcher Voice & Audio settings, call buttons in chat, `okay::voice::Session`, relay flag `--legacy-hello`
- **Editor:** none (launcher: Settings > Voice & Audio, call panel in the chat window)
- **Docs:** `docs/audio/voice-server.md`, `docs/online/relay.md` (Voice calls in the launcher)
- **Tests:** none: no test file includes `VoiceChat.hpp`; the relay side is covered by `relay_security`
- **Status:** Partial - implemented in the launcher and used for calls (read the header), but no automated test and no echo cancellation (headphones advised)
- **Open:** no tests for ADPCM, jitter buffer or room-key handling; launcher voice calls now send version-3 hellos with the relay cookie and fall back to version 1 after 3 unanswered hellos (read in `VoiceChat.hpp`; the ROADMAP note that they still send old hellos predates #393)
- **Related:** 16.14, 17.4, 41.5
- **Words:** voice, voice chat, call, voice room, mic, microphone, ADPCM, jitter buffer, VoiceChat, voice server, relay, speaking, mute, deafen
- **Seen as:** no sound in a call; a voice call won't connect; echo in the call; calls work for me but not for a friend behind a strict router
- **Ask me like:** "add push-to-talk to calls", "set up my own voice server"

### 16.8 Network player spawning and avatar motion
- **System:** Networking
- **What:** Drop-in networked players: clone a player template into one local player and into stripped-down remote copies that turn and animate from the movement the network already replicates.
- **Parts:**
  - 16.8.1 Network Player Spawner - `Components/NetworkPlayerSpawner` - template (inactive object or prefab) becomes the local player (controllers, camera, hand) and a proxy for every remote peer, no input or camera on proxies
  - 16.8.2 Avatar motion - `Components/NetworkAvatarMotion` - turns a remote avatar to face its travel direction and drives idle, walk, run from its speed so feet do not skate
  - 16.8.3 Proxy gait and action sync - `Components/NetworkSync` - the animation message carries gait, the owner's death and where they look (older peers still read the first four fields)
  - 16.8.4 Spawn and despawn - `Net/NetworkManager` - `Spawn`, `SpawnOwned`, `Despawn` give each spawned prefab a follower NetworkSync (ids "sp/<peer>/<n>")
  - 16.8.5 Respawn and one-shot animation replication - `Net/NetworkManager`, `Components/NetworkSync`, `Components/SurvivalComponents`, `Animation/HumanoidActions` - a Health Stat respawn or `Revive()` on the host sends `__respawn` (health, position) so other peers' copies come back; dive, hit, traverse (vault, mantle, ledge) and emotes ride the animation message as a one-shot event
- **Code:** `Components/NetworkPlayerSpawner`, `Components/NetworkAvatarMotion`
- **API:** `NetworkPlayerSpawner`, `NetworkAvatarMotion`, `NetworkManager::SpawnOwned`, `Despawn`, scripts `net_spawn`, `net_spawn_owned`, `net_despawn`
- **Editor:** Inspector: Network Player Spawner (Add Component menu)
- **Docs:** `docs/online/multiplayer.md` ("For game makers")
- **Tests:** `net_player_spawn`, `net_proxy_gait`, `net_anim_actions`, `net_spawn`, `mp_controllers`, `net_respawn_anim`
- **Status:** Works - five tests cover local versus proxy setup, gait, death and look sync, and replicated spawns
- **Open:** object holding in multiplayer was PR 4 of the holding plan (branch claude/holding-net, an older branch; not checked against main); a death that no replicated hit caused (a kill volume, a script) is not sent to other players, only the respawn is; two one-shot animations inside one sync interval play only the newest
- **Related:** 16.2, 16.3, 4.1, 5.3, 5.9
- **Words:** remote player, proxy, avatar, NetworkPlayerSpawner, NetworkAvatarMotion, local player, template, SpawnOwned, despawn, gait, skating feet
- **Seen as:** other players slide without walking animation; remote players are sprites instead of characters; two copies of my player; a remote player can be driven by my keys
- **Ask me like:** "show other players as my character model", "other players' feet slide"

### 16.9 Match results, stats and leaderboards
- **System:** Matchmaking and Lobbies
- **What:** When a match ends, the server reports who played, scores and who won; the database keeps per-game stats that feed leaderboards and "Recently played with".
- **Parts:**
  - 16.9.1 Match report - `Net/MatchResults` - `MatchPlayer` rows (account id, score, place), the report JSON, time played; `BeginMatch` and `EndMatch` on NetworkManager keep the match clock
  - 16.9.2 Backend tables and functions - `docs/sql/matches.sql` - `matches`, `match_players`, `player_stats`; clients can't write them, only `report_match()` (SECURITY DEFINER) and read-only functions
  - 16.9.3 Launcher views - `launcher/main.cpp` - Recent players and Leaderboards on the Multiplayer page; a local "played with" log written by games through GameIpc
  - 16.9.4 Script access - `Net/MatchResults` - `net_match_end`, `net_match_status`, `net_stats_top`, `net_stats_mine`
- **Code:** `Net/MatchResults`, `docs/sql/matches.sql`
- **API:** `MatchPlayer`, `NetworkManager::BeginMatch`, `EndMatch`, `MatchSeconds`, scripts `net_match_start`, `net_match_end`, `net_match_seconds`, `net_stats_top`, `net_stats_mine`
- **Editor:** none (launcher: Multiplayer > Recent players, Leaderboards)
- **Docs:** `docs/online/multiplayer.md` (Match results, stats and leaderboards)
- **Tests:** `match_results`
- **Status:** Works - `match_results` covers the report built from a roster, its JSON, the match clock and the stats rows read back
- **Open:** the SQL itself has no local test (only `anticheat.sql` has one, see 39.6)
- **Related:** 16.4, 17.15, 17.7, 41.6
- **Words:** match results, leaderboard, stats, player_stats, report_match, recently played with, wins, score, place, net_match_end
- **Seen as:** leaderboard empty; stats not updating after a match; "Recently played with" missing people
- **Ask me like:** "report scores when my round ends", "show the top 10 by wins"

### 16.10 Provider seams: Photon transport and online-services backend
- **System:** Networking
- **What:** Two plug-in points so the engine isn't tied to its own stack: `INetBackend` for online services (sign-in, cloud saves, leaderboards) and `INetTransport` for the realtime link. Native is the default; PlayFab and Photon are opt-in.
- **Parts:**
  - 16.10.1 Services interface and Native provider - `Net/NetBackend` - `INetBackend`, `NetBackend` facade, Native forwards to `okay::Account` and `Matchmaking`
  - 16.10.2 PlayFab backend - `Net/NetBackend` - `PlayFabNetBackend` does sign-in, cloud saves and leaderboards through `Platform/PlayFab/PlayFab`; PlayFab's own matchmaking is not used
  - 16.10.3 Photon adapter - `Net/PhotonTransport` - empty unless built with `-DOKAY_WITH_PHOTON=ON` and a Photon SDK; maps host, join, send and RPC onto Photon rooms
- **Code:** `Net/NetBackend`, `Net/PhotonTransport`
- **API:** `INetBackend`, `NetBackend::Use`, `NetBackendProvider`, `NetTransport::Use(NetTransportProvider::Photon)`, `OKAY_PHOTON_APPID`
- **Editor:** View > Panels > Services has a PlayFab test section (login, set stat, top 10)
- **Docs:** `docs/online/photon.md`
- **Tests:** `net_backend`
- **Status:** Basic - the seams and the Native provider are tested; the Photon adapter cannot be built here (needs a closed SDK) so it is unexercised; PlayFab is a REST client without tests
- **Open:** possible gap: no CI job builds with `OKAY_WITH_PHOTON`, so the adapter can rot; PlayFab matchmaking not covered
- **Related:** 17.10, 16.1, 41.1
- **Words:** Photon, PlayFab, INetBackend, INetTransport, provider, transport, backend, OKAY_WITH_PHOTON, online services, cloud save
- **Seen as:** Photon provider reports unavailable; game can't use PlayFab login
- **Ask me like:** "switch my game to Photon rooms", "log players in with PlayFab"

### 16.11 RPC, SyncVars, spawning and chat
- **System:** Networking
- **What:** The messaging API games build on: send to everyone or one peer, reliable and ordered variants, named remote calls, server-owned shared variables, replicated spawns and a chat log.
- **Parts:**
  - 16.11.1 Messages - `Net/NetworkManager` - `Send`, `SendTo` (channel plus string), `SendReliable` (resend until ack; de-dup window of 4096)
  - 16.11.2 RPC by name - `Net/NetworkManager` - `Rpc`, `RpcTo`, `RpcReliable`, `OnRpc`, `SetRpcObserver` (routes "<netId>/<name>" onto objects)
  - 16.11.3 Synced variables - `Net/NetworkManager` - `SetVar`, `GetVar`: the server applies and rebroadcasts; keys "<netId>.<name>" belong to the object's owner; every change carries a version, the receiver acknowledges it (`VarAck`, a frame's receipts in one datagram) and ignores an older version arriving late, and the sender sends the newest value again every 0.25 s until acknowledged (24 times at most; an older peer that never acknowledges gets them that often and no more); `VarsUnacked()`
  - 16.11.4 Chat and moderation - `Net/NetworkManager` - `Chat`, `ChatLog`, `OnChat` (lines cut at 512 bytes), `Kick`
- **Code:** `Net/NetworkManager`
- **API:** `NetworkManager::Send`, `SendTo`, `SendReliable`, `Rpc`, `OnRpc`, `SetVar`, `GetVar`, `Chat`, `Kick`, scripts `net_send`, `net_rpc`, `net_poll`, `net_set`, `net_get`, `net_chat`, `net_kick`, `net_syncvar_set`, `net_obj_rpc`
- **Editor:** Inspector: Network Manager; View > Panels > Services (Multiplayer section: host, join, a chat box); visual-script net nodes
- **Docs:** `docs/scripting/scripting.md` (networking), `docs/online/multiplayer.md`
- **Tests:** `net_rpc`, `script_net`, `net_sync`, `net_spawn`
- **Status:** Works - `net_rpc` covers RPC dispatch and chat fan-out; `script_net` drives a host and a client from two scripts
- **Open:** `NetworkManager.hpp` is 1,101 lines with all of 16.1 to 16.12 in one class (split candidate)
- **Related:** 16.3, 16.6, 13.6
- **Words:** RPC, remote procedure call, Send, SendTo, SendReliable, SyncVar, SetVar, GetVar, chat, message, channel, event, net_send, net_rpc
- **Seen as:** a message arrives twice or not at all; a variable differs between players; a chat line is cut off; an RPC is ignored (rate limited)
- **Ask me like:** "sync the round timer to all players", "send a private message to one player"

### 16.12 Rooms, lobby and match flow
- **System:** Matchmaking and Lobbies
- **What:** Several independent matches on one server (rooms), ready-up, starting a match for a room, scores and the roster everyone sees.
- **Parts:**
  - 16.12.1 Rooms - `Net/NetworkManager` - `SetRoom`, `startRoom`: peers only see avatars and broadcast messages from their own room
  - 16.12.2 Lobby ready-up - `Net/NetworkManager` - `SetReady`, `ReadyCount`, `AllReady`, `StartMatch`, `MatchStarted`
  - 16.12.3 Scores and roster - `Net/NetworkManager` - server-owned scores, `net_set_score`, a roster of names and ids refreshed each second
  - 16.12.4 Server info for browsers - `Net/NetworkManager` - name, game, map, flags answered to queries (16.4.2)
- **Code:** `Net/NetworkManager`
- **API:** `SetRoom`, `SetReady`, `AllReady`, `StartMatch`, `MatchStarted`, scripts `net_room`, `net_ready`, `net_all_ready`, `net_start_match`, `net_set_score`, `net_score`, `net_roster_count`, `net_roster_id`
- **Editor:** Inspector: Network Manager (Auto Start, Room (lobby), Max Players, Server Name, Password)
- **Docs:** `docs/online/multiplayer.md`, `docs/online/dedicated-server.md`
- **Tests:** `net_rooms`, `net_authority`
- **Status:** Works - `net_rooms` proves two clients in different rooms do not see each other
- **Open:** none known
- **Related:** 16.4, 16.5, 16.9
- **Words:** room, lobby, ready, start match, roster, scoreboard, net_room, SetRoom, StartMatch, AllReady
- **Seen as:** players from another match show up; the match won't start; scores reset
- **Ask me like:** "make a lobby where the host starts the match when all are ready"

### 16.13 Network simulator and traffic panel
- **System:** Networking
- **What:** Fake bad network in the editor (delay, jitter, loss, duplicates) and per-object traffic counters, so lag handling can be tried without a bad connection.
- **Parts:**
  - 16.13.1 Simulator - `Net/NetSim` - `SimSettings` applied once per datagram: on the way out, or, when it goes to another socket of this program that reads through the simulator, when that one reads it (two managers in one process see the setting, not twice it); up to 4096 datagrams waiting per socket and direction (`SimulatorOverflows()` counts any beyond, apart from loss); off by default so packets go straight to the socket
  - 16.13.2 Traffic counter - `Net/NetSim` - `TrafficRow` counts logical messages by object id or RPC name and raw bytes; does not change what is sent
  - 16.13.3 Net panel - `editor/NetSimPanel.*`, `okayui/screens/OkayUIEditorPanels.cpp` (drawn by OkayUI only) - View > Panels > Net: toggles, sliders and the traffic table
- **Code:** `Net/NetSim`, `editor/NetSimPanel.*`
- **API:** `okay::net::Simulator()`, `SimSettings`, `TrafficRow`
- **Editor:** View > Panels > Net
- **Docs:** -
- **Tests:** none: no test names `NetSim`; the lag tests use their own delaying proxy
- **Status:** Basic - implemented and wired to the editor panel (read the panel), no test
- **Open:** possible gap: no test and no doc page
- **Related:** 16.2, 15.9
- **Words:** net simulator, latency, jitter, packet loss, lag, traffic, bandwidth, Net panel, NetSim
- **Seen as:** I want to test my game at 150 ms; I can't tell which object uses the bandwidth
- **Ask me like:** "turn on 120 ms lag with 5 percent loss in the editor"

### 16.14 Relay server (NAT traversal)
- **System:** Relay - A small server that connects players behind home routers to each other.
  Home: `engine/include/okay/Net/RelayServer.hpp`, `engine/include/okay/Net/RelayProtocol.hpp`. Entry: `engine/include/okay/Net/RelayServer.hpp`. Also called: relay server, NAT traversal, RelayProtocol.
- **What:** A small program players behind home routers meet at: both connect out to it, share a code, and it forwards datagrams by slot without reading them. It is exposed to the internet, so it caps and trusts nothing. Also carries voice (16.7).
- **Parts:**
  - 16.14.1 Relay protocol - `Net/RelayProtocol` - outer frames Hello (0x90), Welcome, Data, Bye, Refused, Cookie, PeerAddr (0x96), DirectAllow (0x97, host to relay: the players its game accepted, with its host key), and the peer-to-peer Punch (0x98); versions 1 to 5 (4 adds the welcome nonce; 5 adds the hello flag byte `RelayHelloDirect`, PeerAddr, DirectAllow and the host's successor key); little-endian throughout; the relay strips its header so inner messages are untouched; hello and welcome are built and checked in `Net/RelayHello.hpp` (also `ParseRelayPeerAddr`, `BuildRelayDirectAllow`, `BuildRelayPunch`)
  - 16.14.2 Relay server core - `Net/RelayServer` - sessions by code, slot ids, host-secret key for version-2 hosts, cookie round trip for version-3, per-address and global caps, rate limits, idle expiry
  - 16.14.3 Standalone program - `relay/main.cpp` - `okayspace-relay [port] [--legacy-hello=on|off]`, default port 45100, legacy hellos off; target `okayspace-relay` (always built except for Emscripten)
  - 16.14.4 Using it from a game - `Net/NetworkManager` - `HostViaRelay`, `JoinViaRelay`, `RelayReady`, `RelayError`; welcome timeout (`relayTimeoutSeconds`) and nonce check (`allowUnverifiedRelay`); scripts `net_host_relay`, `net_join_relay`
  - 16.14.6 Direct paths (NAT hole punching) - `Net/RelayServer`, `engine/src/Net/NetworkManagerOnline.cpp` - when a client and its host BOTH set `RelayHelloDirect` (`NetworkManager::directConnect`, on by default) and the host has named that client in DirectAllow (only players its game accepted: password, ban list, join checks; `SendDirectAllow`, again with every hello), the relay tells each the other's public address and one random token (PeerAddr, echoing the hello nonce; never between two clients, never to anyone who only knows the room code; sent once, again when it changes, and at most every `kRelayPeerAddrResendSeconds` (3 s) while the client asks, so keepalives never bring back more bytes than they carry); both send Punch probes at once (10 a second), a probe with the token from the address the relay gave is answered; a path never moves on a punch alone (its source can be forged): only before it opens, to another port at the same address, and only after a probe sent there is answered from there (one probe for one, at most 10 a second); punches from anywhere else get nothing; an answer opens the path and game datagrams go straight there, keyed by the relay slot as before (`Transmit`, `ReceiveFrom`); no answer in `punchTimeoutSeconds` (5 s) stays on the relay; an open path quiet for 3 s goes back to the relay; the host drops a path as soon as its player is gone (at most one per accepted player); `DirectToHost`, `DirectPeers`, `RelayedPeers`, `PathText`, `PunchesOpened`, `PunchesFailed`, `Stats().direct/relayed/directPeers/relayedPeers`, the overlay's "Path:" line; relay counter `PeerAddrsSent`
  - 16.14.7 Host succession on the relay - `Net/RelayServer` - the host's version-5 welcome carries a successor key; a client of that session presenting it in a host-role hello becomes the host (`TakeSuccession`), only once the old host is gone: a bye, or `kRelaySuccessionQuietSeconds` (3 s) without a word, which is six of the host's hellos (a host holding a successor key says hello every `kRelayHostHelloSeconds`, 0.5 s) and three of the old one-a-second ones, so a lost hello or a stall of a second or two can't let a player push a live host out; host key and successor key are made new each time; the code stays; `Successions`, `HostSlotOf`; used by host migration (16.15.15)
  - 16.14.8 NAT model for tests - `Net/NetSim` - `SetSimulatedNat(socket, NatType::FullCone / PortRestricted / Symmetric)`, `NetworkManager::simulateNat`: every datagram leaves from a mapping socket (its port is the public address), arrivals pass the RFC 4787-style filter or are dropped (`SimulatedNatDrops`), the private port is unreachable; `SimulatedNatPort`, `RebindSimulatedNat`, `ClearSimulatedNats`, `SendThroughNat`
  - 16.14.5 Hosting kits - `deploy/voice-server/Dockerfile` - Dockerfile, docker-compose, systemd unit and `install.sh` for a VPS (built without libsodium, port 45100/udp)
- **Code:** `Net/RelayProtocol`, `Net/RelayHello.hpp`, `Net/RelayServer`, `engine/src/Net/NetworkManagerOnline.cpp`, `Net/NetSim`, `relay/main.cpp`, `relay/CMakeLists.txt`, `deploy/voice-server/Dockerfile`, `deploy/voice-server/docker-compose.yml`, `deploy/voice-server/install.sh`, `deploy/voice-server/okayspace-voice.service`
- **API:** `RelayServer`, `okayspace-relay`, `NetworkManager::HostViaRelay`, `JoinViaRelay`, `RelayAddress`, `RelayCode`, `directConnect`, `punchTimeoutSeconds`, `DirectToHost`, `PathText`, `simulateNat`, `net::SetSimulatedNat`, `net::NatType`, `RelayServer::PeerAddrsSent`, `Successions`
- **Editor:** none (launcher uses it for calls; the Windows dist ships `Tools/okayspace-relay.exe`)
- **Docs:** `docs/online/relay.md`, `docs/audio/voice-server.md`
- **Tests:** `net_relay`, `relay_security` (version 5: no takeover of a live host after a lost hello or a stall with a key holder asking ten times a second, takeover after a bye or real silence, one winner of two; no address for a client the host did not name or for a forged DirectAllow, the pair at once when named and none after it is dropped; keepalive bytes out no more than in), `net_relay_hardening`, `net_anticheat_ph2`, `net_online` (punching on port-restricted cone routers, then the game with the relay stopped; fallback on symmetric routers; only the player behind a symmetric router (the moved port is probed first); no address passed on with `directConnect` off; three players, one direct and one relayed; a player with the successor key cannot take a live host's place across a 0.6 s or 1.5 s host stall; forged punches from another address get nothing from the host and do not move its path; a bare relay client and a wrong-password player get no address, an accepted player does, and the host forgets the path when it leaves; succession on the relay)
- **Status:** Works - `net_relay` joins through an in-process relay, `relay_security` covers isolation, unguessable slots, floods and random datagrams; direct paths and succession are tested over loopback with the simulated routers only (`net_online`), not across real home routers
- **Open:** whether the live relay was redeployed with the cookie hardening can't be told from the repo (ROADMAP item 7); a relay older than version 5 gives no direct paths and no relay migration (the game still works through it); `--legacy-hello` now defaults to off (#733); `=on` re-allows old games; no deploy automation (manual `install.sh` or Docker); hole punching never ran across real routers; two players behind the same router (hairpinning) are only offered the public address; a client whose direct path failed or went quiet does not try again in that session; the launcher's voice calls do not punch (they still go through the relay); `test_net_relay_nat` keeps its own small router (`NatBox`) for rebinding, which the NetSim model (`RebindSimulatedNat`) could now replace
- **Related:** 16.5, 16.7, 39.8, 41.5
- **Words:** relay, NAT, TURN, hole punching, NAT punch, direct path, symmetric NAT, cone NAT, STUN, peer to peer, port forwarding, okayspace-relay, session code, slot, hello, cookie, legacy hello, HostViaRelay, JoinViaRelay, RelayServer
- **Seen as:** friends can't join without port forwarding; "relay refused" or the code is hosted by someone else; the relay is flooded or runs out of slots
- **Ask me like:** "host my game through the relay so friends can join", "deploy the relay to my VPS"

### 16.15 The easy layer: one call or one node per action
- **System:** Networking
- **What:** The front door to multiplayer on top of 16.1 to 16.14, the same on the host and on every client: host, join or "host or join" (two copies on one PC become a two-player test), the player list, join and leave events on every peer, named events sent reliably and in order (a client's go through the host, which stamps the real sender), the network clock, and every failure in plain words ("Could not reach host 1.2.3.4:45000 (no answer after 15 s). ..."). The same calls in C++ (`okay::Net`), Visual Script and OkayScript (`net.*`).
- **Parts:**
  - 16.15.1 Players and join / leave events - `Net/NetworkManagerEvents.cpp` - `Players()` (you included, sorted by id, host = 0), `OnPlayerJoined`, `OnPlayerLeft` from the roster on both sides (a joining client hears one join per player already there; a client counts a player gone only after two rosters without them); raised on every graph (`OnPlayerJoined`, `OnPlayerLeft`: Event Value = id, Event Name = name) and every script (`on_player_joined(id, name)`, `on_player_left(id)`)
  - 16.15.2 Named network events - `Net/NetworkManagerEvents.cpp` - `SendEvent(name, data, includeSelf = true)`, `SendEventTo`, `OnEvent`, `OnAnyEvent`, `RemoveListener`; one reliable ordered channel `__ev` (16.1.4), so events arrive once and in send order; names 1 to 64 letters, digits, space, `_ - .`; data up to 16 KB (split and rejoined by 16.1.2); the host checks size and name (`ClientMessageOk`) and the anti-cheat sees it like any client message; graphs get `OnNetworkEvent:<name>` (Data, Event Sender), scripts `on_network_event(name, data, from)` and `net.on(name, fn)`
  - 16.15.3 Host or join - `Net/NetworkManager` - `HostOrJoin(port)` probes the port: free means host, taken means join 127.0.0.1; `AutoStart::Auto` does it on Play (a new Network Manager from Add Component starts as Auto)
  - 16.15.4 Plain errors - `Net/NetworkManager`, `Net/NetworkManagerEvents.cpp` - `LastError()` for a port in use, a name that does not resolve, a host that never answers (with the address and the wait), a refused join, a lost connection, a kick, an event that could not be sent; each is logged once
  - 16.15.5 Network clock - `Net/NetworkManagerEvents.cpp` - `NetworkTime()`: the host's clock, sent to clients about once a second (`__time`) plus half the round trip, eased so it never jumps back
  - 16.15.6 Front door - `Net/Net.hpp` - `Net::Use(scene)`, `Host`, `Join("addr[:port]")`, `HostOrJoin`, `Leave`, `IsHost`, `IsConnected`, `MyId`, `Players`, `Send`, `SendTo`, `On`, `OnPlayerJoined`, `OnPlayerLeft`, `Off`, `Spawn`, `SetVar`, `GetVar`, `Time`, `Ping`, `LastError`, `FindLanGames`, `JoinLanGame` (LAN scan: 16.4)
  - 16.15.7 Visual Script nodes - `VisualScript/Nodes.hpp` - Host or Join, On Player Joined, On Player Left, Send Network Event, On Network Event, Event Sender, Player Count, Player Id At, Player Name, Network Error, Network Time, Ping (with the existing Host Game, Join Game, Disconnect, Is Host, My Player Id, Net Spawn, Set Net Var); the Host and Join nodes now report `LastError()`
  - 16.15.8 OkayScript - `Scripting/OkayScriptVM_Network.cpp` - `net.host`, `net.join`, `net.host_or_join`, `net.join_lan`, `net.leave`, `net.is_host`, `net.connected`, `net.id`, `net.players`, `net.player_count`, `net.player_name`, `net.send`, `net.send_to`, `net.on`, `net.off`, `net.spawn`, `net.set`, `net.get`, `net.time`, `net.ping`, `net.error` (the older `net_*` calls stay)
  - 16.15.9 Networked tick - `editor/main.cpp` - the Inspector shows "Networked" under the object header once the scene has a Network Manager: it adds or removes the object's Network Sync (16.3)
  - 16.15.11 Ownership handoff - `Net/NetworkManagerEvents.cpp` - the host decides: `GiveOwnership(netId, player)`, a client's `RequestOwnership(netId)` (granted when the Network Sync has "Players can claim it" and no other client holds it, or by `allowOwnershipRequest`), `OwnerOf`, `IsMine`, `OnOwnershipChanged`; sent ordered (`__own`, `__ownreq`), replayed to late joiners, back to the host when the owner leaves; the host takes that object's transform from its owner only (`ClientMaySendXform`) and tells the anti-cheat (`AntiCheat::SetOwner`); nodes Give Ownership, Request Ownership, Owner, Is Mine, On Ownership Changed; scripts `net.give`, `net.request`, `net.owner`, `net.is_mine`, `on_ownership_changed(owner, netId)`
  - 16.15.12 Stats - `Net/NetworkManagerEvents.cpp` - `Stats()`: round trip, loss (pings never answered, last 16, measured on clients), bytes in and out per second for this manager's socket, players, network time; node Network Stats (four outputs), scripts `net.ping`, `net.loss`, `net.bytes_in`, `net.bytes_out`; `Net::Stats()`
  - 16.15.13 Bad-network settings - `Net/NetworkManager` - `simulateLatencyMs`, `simulateJitterMs`, `simulateLossPercent` drive the simulator (16.13) while above 0; Inspector "Test a bad network" under Smoothing; node Simulate Bad Network, script `net.simulate(latency, jitter, loss)`
  - 16.15.14 Is Locally Predicted - node and `net.is_predicted()` over `NetworkManager::Predicted` (16.2)
  - 16.15.15 Host migration - `engine/src/Net/NetworkManagerMigration.cpp` - `hostMigration` (`Off`, `Lan` = the default: only when every player's address is private or this computer, `Always`); the host sends each client in its room the same list of clients and addresses (`__mig`, ordered, again when it changes); a host that quits says goodbye (`__bye`, three copies), a host that crashes is noticed after `serverTimeoutSeconds`; every client takes the lowest id on the list, which hosts on the local port it already used, the others join it from their old port and get their old ids back (`TakeExpectedId`, by address); a new host that does not answer in `migrationJoinTimeoutSeconds` is struck off and the next is tried; a player hearing a join from someone on the list knows the host is gone, but only once its own host has been quiet a while (`KnockHeeded`: half `migrationJoinTimeoutSeconds`, at least 1.5 s, at most `serverTimeoutSeconds`), so a forged join from another player cannot pull it off a live host; a player on the host's computer is listed as 127.0.0.1 and reached at the host's address from other computers (`MigrationAddress`); shared variables, the ownership table (the old host's and the new host's objects become 0's, so do those of players who are gone), synced objects and stand-ins carry on; through a relay (version 5) the list names relay slots (never addresses) after an `R|<successor key>` line, the lowest id takes the host's place on the relay without leaving it (`RelaySwitch`, the relay's succession 16.14.7, the room code stays), the others stay registered and join whoever the relay names, and direct paths are punched to the new host again; through a relay `Lan` and `Always` behave the same (no address is shared, so the LAN-only rule does not apply); the old host's stand-in and `sp/0/...` objects go; players are kept in the list for `migrationGraceSeconds`; newcomers get ids above every old one; `OnHostMigrated(previousId)`, `Migrating()`, `HostMigrations()`, `MigrationOffered()`; graph event On Host Migrated (Event Value = the id the new host had), node Is Migrating, script `on_host_migrated(prev)`, `net.on("host_migrated", fn)`, `net.migration("off"/"lan"/"always")`, `net.migrations()`, `net.migrating()`; `Net::OnHostMigrated`; never through a relay older than version 5, for a dedicated server, with anti-cheat rules or a token check on the host, with a pinned host key, or with one client
  - 16.15.16 Rollback mode - `Net/Rollback` - `NetworkManager::Rollback()`: `Start(settings, save, load, input, step)` with every player in the session; fixed tick (`tickRate`), `inputDelay`, `maxPrediction` (waits past it: `Stalls()`); inputs only, unreliable on `__rb` through the host, each packet repeating what the others have not acknowledged; a missing input is predicted (that player's last), a late one that differs sends it back to that frame's saved state and runs the frames again (`Resimulating()`, `Rollbacks()`, `FramesResimulated()`); a checksum of a final frame's state goes along (`Desyncs()`, `ChecksumsCompared()`); `ConfirmedFrame()`, `StateAt(frame)`; a player leaving stops it with `LastError()`; every packet carries the input delay, tick rate, input size and the player set, and a peer that started with others stops it with a plain `LastError()`; inputs for frames further ahead than a player can be (2 x input delay + `maxPrediction` + 16) are not kept (`HeldInputs()`); scripts `net.rollback_start(delay, rate, max)` with `rollback_save`, `rollback_load`, `rollback_input`, `rollback_step(frame, inputs)`, and `net.rollback_stop`, `net.rollback_frame`, `net.rollback_confirmed`, `net.rollback_resimulating`, `net.rollback_players`, `net.rollback_running` (in Visual Script as their `Fn.net.*` nodes); off until a game starts it
  - 16.15.17 Network stats overlay - `Net/NetworkManagerEvents.cpp`, `player/main.cpp`, `editor/EditorState.cpp` - `showStatsOverlay`, `StatsLines()` (role, id, players, ping, loss, KB/s in and out, a line per client on the host, rollback frame and stalls, migrations), `OverlayManager()`; the player draws it as an OkayUI window top right, the editor adds the lines to the Game view's stats; node Show Network Stats, scripts `net.show_stats(on)`, `net.stats_text()`, `Net::ShowStats`
  - 16.15.18 Host-side loss - `Net/NetworkManagerServer.cpp` - the host pings each client once a second (clients answer with `Pong`), so `Stats()` on the host has the average round trip and loss of its clients, and `PeerLink(player, rtt, loss)` one client's
  - 16.15.19 Per-piece resend - `Net/NetworkManagerSync.cpp` - a reliable message (`ReliableMsg`, ordered channels) sent in pieces is kept (`FragOut`, 8 per peer, up to 256 KB each); the far side acknowledges which pieces it has on every piece (`FragAck`, plaintext, only to a peer it knows), and a resend of the same bytes sends only the missing pieces (the same sealed pieces and id); all acknowledged, or 12 partial resends, sends a fresh copy (so a forged receipt only forces resends: nothing grows); what was put together and kept for a peer goes when it leaves (`FragDonePeers()`); `PiecesResent()`, `PieceResends()`
  - 16.15.20 Small gaps closed - the host sends the player list at once (three frames running) when someone joins, so a client's list and join events fill in within a frame of joining; Net Validator rules and `on_validate_rpc` see network events by their event name (`AntiCheat::ValidateMessage` for `__ev`)
  - 16.15.21 Online play by room code - `engine/src/Net/NetworkManagerOnline.cpp` - `HostOnline(code = "")` (makes a 6-letter code with no look-alikes when empty, and another if a made-up one is taken), `JoinCode("k7q-29x")` (case, spaces and dashes do not matter; it goes through `JoinByLink`, the launcher's path), `RoomCode()`, `OnlineJoinLink()`, `MakeRoomCode`, `NormalizeRoomCode`; the relay is `relayServer` (saved in the scene as `netrelay`), else `OKAY_RELAY`, else the build's `OKAY_DEFAULT_RELAY` / `OKAY_DEFAULT_VOICE_RELAY` (both empty unless set at build time), never a GitHub address (any case, with or without a trailing dot; raw GitHub IP addresses are not recognised); a code of one's own must have 6 to 12 letters and digits to host (4 or 5 are too easy to guess; `JoinCode` still takes 4); plain errors: no relay set, not a room code, a code too short to host, the relay name does not resolve, the relay never answers, no game with that code, the code is in use, another player took over; `Net::HostOnline`, `Net::JoinCode`, `Net::RoomCode`; nodes Host Online (Room in, Room Code out) and Join By Code; scripts `net.host_online([code])` (returns the code), `net.join_code(code)`, `net.room_code()`, `net.relay([addr])`, `net.path()`
  - 16.15.10 Two-player sample - `examples/multiplayer/two_players.okayscene` - only the easy layer: a Network Manager on Host or Join, a player spawner, a Networked ball the host drives, and a script with `on_player_joined`, `net.on("wave")`, `net.send`; play it in two copies on one PC
- **Code:** `Net/NetworkManager`, `engine/src/Net/NetworkManagerEvents.cpp`, `engine/src/Net/NetworkManagerMigration.cpp`, `engine/src/Net/NetworkManagerOnline.cpp`, `Net/Rollback`, `Net/Net`, `VisualScript/Nodes`, `engine/src/Scripting/OkayScriptVM_Network.cpp`
- **API:** `okay::Net`, `NetworkManager::Players`, `OnPlayerJoined`, `OnPlayerLeft`, `OnEvent`, `OnAnyEvent`, `SendEvent`, `SendEventTo`, `HostOrJoin`, `LastError`, `NetworkTime`, `Ensure`, `Current`, `AutoStart::Auto`, `hostMigration`, `OnHostMigrated`, `Rollback()`, `net::Rollback`, `showStatsOverlay`, `StatsLines`, `PeerLink`, `PiecesResent`, `HostOnline`, `JoinCode`, `RoomCode`, `relayServer`, `directConnect`, `PathText`, scripts `net.host_online`, `net.join_code`, `net.room_code`, `net.path`, `net.host`, `net.join`, `net.on`, `net.send`, `on_player_joined`, `on_network_event`, `on_host_migrated`, `net.rollback_start`, `net.show_stats`
- **Editor:** Inspector: "Networked" tick; Network Manager > Auto Start "Host or Join on Play"; Visual Script palette, Multiplayer
- **Docs:** none (rule 5); the header comments in `Net/Net.hpp` and `Net/NetworkManager.hpp`
- **Tests:** `net_easy` (`tests/test_net_easy.cpp`): the whole flow in C++ (host, join, a player spawned per side, the moved player and a Networked crate follow, events both ways and to one player, player list, join and leave, the clock), plain errors, a forged sender, host-or-join, 40 events in order and a 12 KB event under 40 ms latency, 15 ms jitter, 15% loss and 5% duplicates, the same Visual Script graph on two peers, two `.okay` scripts on two peers, and the two-player sample loaded twice (host and client, a player each, the ball follows); `net_session` (`tests/test_net_session.cpp`): a 12 KB event at 15% loss eight times (per-piece resend), the client's list on joining, a Net Validator refusing an event by name, loss measured on the host, the overlay's lines and its script and node switches, host migration with four peers (the host quits: ids, variables, ownership, the crate, events go on, a newcomer, On Host Migrated in C++, a graph and a script), failover under 40 ms latency with jitter and loss (the host and the next host crash), migration off, a forged join knock that changes nothing (then a real migration), the host-computer address on the list, forged piece receipts and a gone peer's piece state, 80 shared variable changes and two fast-changing ones at 40% loss, the simulator's loss and latency applied once and a 1000-datagram burst kept, rollback refusing other settings or players and capping future inputs, rollback between two peers at 100 ms latency with jitter and 10% loss (the same state at every final frame compared, no desync, a non-deterministic game caught, a player leaving stops it), and rollback from OkayScript; `net_online` (`tests/test_net_online.cpp`): punching on cone routers and falling back on symmetric ones (16.14), three players over the relay (one direct, one relayed), host migration through the relay with four players (the host leaves, then the new host crashes: same room code, ids and the shared variable kept, direct paths punched again), the room-code flow in C++ and through `Net::`, the Host Online and Join By Code nodes, an `.okay` script (`net.relay`, `net.host_online`, `net.join_code`, `net.room_code`, `net.path`), and the plain errors
- **Status:** Partial - built and tested on Linux over loopback; not run on Windows, between two computers, or in the editor (the Networked tick, the palette entries and the overlay window are not built here; the player's overlay window is not built either)
- **Open:** online play, hole punching and migration through the relay have run only over loopback with simulated routers (`net_online`), never between real computers or across real home routers, and no public relay is configured by default (a game must set `relayServer` or OKAY_RELAY, or be built with OKAY_DEFAULT_RELAY); a made-up room code that is taken is replaced, but that retry has no test; if two players take the host's place on the relay at once, the one the relay refuses joins the other (untested); host migration without a relay: only between computers that can reach each other directly (a LAN; over the internet a player behind a home router usually cannot be joined: use the relay); build mode, quest and anti-cheat state are kept as each player last saw them, not checked; a split network can elect two hosts; game code that keyed things by the new host's old id must move them to 0 itself (On Host Migrated says which); the old host's `sp/0/...` objects are removed, not handed over. Rollback: every player must start it, with the same settings, after the player list is complete; players who join later do not take part and a player leaving stops it; the game must be deterministic (the checksums only find out); state and inputs are strings the game packs itself; no rollback state saving for Visual Script graphs (only the `Fn.net.rollback_*` readers). The overlay window has not been seen on screen. The simulator settings are program-wide and not saved in the scene; no per-event rate limit beyond the per-client message limit and Net Validators; no Visual Script loop node for the player list (use Player Id At)
- **Related:** 16.1, 16.3, 16.4, 16.8, 16.11, 16.12, 14
- **Words:** easy multiplayer, Net::Host, Net::Join, host or join, host online, join by code, room code, online play, play over the internet, net.on, net.send, on_player_joined, player joined, player left, network event, send event, player list, network time, LastError, LAN test, two copies on one PC, Networked, host migration, new host, host left, rollback, rollback netcode, input delay, resimulation, deterministic lockstep, desync, network stats overlay, ping display, packet loss, per-piece resend, big message
- **Seen as:** "I don't know how to start a multiplayer game"; joining does nothing and says nothing; an event arrives on the host but not on the other players; events come in the wrong order; the game ends for everyone when the host quits; a fighting game feels laggy; a big message takes seconds on a bad connection
- **Ask me like:** "let friends join my game over the internet with a code", "make my game multiplayer with one node", "tell every player when someone scores", "run two copies on my PC to test", "keep the game going when the host leaves", "add rollback netcode to my fighting game", "show ping on screen"
