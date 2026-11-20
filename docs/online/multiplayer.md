# Multiplayer

> **What this is:** Multiplayer in the launcher (Steam-style) and the multiplayer audit.
>
> **Status:** combined on 2026-11-20 from 2 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
>
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Multiplayer in the launcher (Steam-style)](#multiplayer-in-the-launcher-steam-style) (was `docs/launcher-multiplayer.md`)
- [Multiplayer audit](#multiplayer-audit) (was `docs/multiplayer_audit.md`)

## Multiplayer in the launcher (Steam-style)

*Was `docs/launcher-multiplayer.md` until 2026-11-20.*

The OkaySpace launcher does for OkaySpace games what the Steam client does for
Steam games: friends see what you're playing and join you in one click, you
invite people from the launcher or from inside the game, and a server browser
lists public, friends' and LAN games. It works with any game whose scene has a
`NetworkManager`, or whose scripts use `net_host` / `net_join`. You don't
write any extra code.

### What players get

- **Join game.** If a friend is in a joinable match, the Friends page shows a
  green Join button. Their profile and the server browser's Friends tab offer
  the same. Clicking it starts the game and drops you into their session. If
  the game is already running, it switches to their session instead.
- **Invites.** **Invite friends** sends a game invite in chat. It shows as a
  card with a **Join game** button. You can invite from:
  - the Multiplayer page;
  - a friend's right-click menu;
  - a chat's menu;
  - inside the game, with **Shift+Tab**.

  If you're hosting with a password, the invite carries it, so invited
  friends get straight in.
- **Server browser** (Multiplayer page) has five tabs:
  - **Internet:** public servers.
  - **Friends:** your friends' games.
  - **LAN:** games on your network, found automatically.
  - **Favorites** and **History.**

  It shows players, map and live ping, and has filters: Not full, Has players,
  No password. **Connect to server…** joins by address, or by a pasted join
  link.
- **Rich presence.** Friends see what you're doing, e.g. "Playing Star Arena ·
  Hosting Maya's lobby (2/8)". **Invisible** and the "share what I'm playing"
  setting are respected.
- **Quick Play** finds the best open server for the game you pick, or for any
  game in your library, and joins it:
  - It skips servers that are full or need a password.
  - It prefers low ping and servers that already have players.
- **Server info** shows who's playing on a server: names, scores and how long
  they've been on. Open it with **Players** in the browser footer, or
  right-click a server → **Server info…**.
- **In chat:**
  - **Game bar.** While a friend is in a game, their DM shows a bar with
    "Playing X" and **Join game**. If their game isn't open to join, it shows
    **Ask to join**, which sends them a card with an **Invite them** button.
  - **Invites from chat.** The composer's **+** menu has **Invite to
    \<game>**. Typing `/invite` does the same, and works in group chats too.
  - **Pop-up.** An invite that arrives while the chat is closed shows a
    "Maya invited you to play" pop-up with **Join game**.
- **Recent players.** Everyone you've been in a game with, newest first, with
  the game, when, and how many matches you played together. **Profile** opens
  their profile so you can add them as a friend, and **Invite** asks them into
  what you're playing. Your games log who you play with on this PC, so the list
  works in any session. Matches that servers report add people from your other
  PCs too.
- **Leaderboards.** Pick a game and rank by wins, total score, best score,
  matches or time played. Your own line and rank are shown on top. The numbers
  come from match results (see below).
- **Parties.** Friends who play together:
  - **Starting one.** Create a party on the Multiplayer page, or pick **Start
    a party with them** / **Invite to party** from a friend's menu, a chat's
    menu, the composer's **+** menu, or `/party`.
  - **Following the leader.** When the leader hosts or joins a match, everyone
    in the party follows into it. Members can turn **Follow the leader** off and
    use **Join the leader** instead.
  - **The party card** shows members, a crown on the leader, and ready
    checks. Right-click a member to make them leader or remove them.
  - **Rules:** one party at a time, up to 8 people, and the lead passes on
    when the leader leaves.
  - **Setup:** run `docs/sql/parties.sql` once.

### How it fits together

```
 Launcher ──starts──▶  game  --join "okay://join?game=…&addr=…"
    ▲                    │
    │   <config>/OkaySpace/ipc/game-<pid>.presence   (what I'm doing + join link)
    │◀───────────────────┤   game-<pid>.invite     (Shift+Tab: open Invite friends)
    │────────────────────▶   game-<pid>.join       (launcher: switch to this session)
    │
    └──▶ Supabase: profiles.playing / session_code (friends see it),
                   direct messages (invites), game_sessions (public servers)
```

- **Join links** (`okay/Net/JoinLink.hpp`) hold everything needed to get in:
  - the game;
  - a direct `addr`, or a `relay` + `code`;
  - the room and server name;
  - a password flag. Only private invites carry the password itself.
- **The game** (`okay-player`):
  - accepts `--join LINK`, or `--connect HOST:PORT [--password P] [--room R]`;
  - reports its session once a second (`okay/Net/SessionPresence.hpp`);
  - picks up joins the launcher posts while it's running.

  The next `NetworkManager` to start joins the session instead of running its
  own auto-start.
- **Server queries** (`okay/Net/ServerQuery.hpp`) work like Steam's A2S_INFO.
  Every server answers a small UDP "who are you?" with its name, game, map,
  players, and password / dedicated / sign-in flags, without anyone joining.
  The LAN tab broadcasts the same query on ports 45000–45015.

### Joining a game in progress

A player who joins (or rejoins after a drop) is sent what it missed, so the world matches the others': the
pieces players built in build mode, the quest log and story variables (the shared ones, and that
player's own), who is dead and who is hurt. Nothing for the game to do: it comes with the join, after the
welcome, in order. Details: `docs/online/multiplayer.md` 18-21, `docs/gameplay/build_mode.md` "Multiplayer",
`docs/gameplay/dialogue_quests.md` "Multiplayer". The launcher's join, invite and server-browser screens were not
run for this (they need live services); the engine side is covered by the `net_*` test suites.

### In-game server browser

Games get a ready-made **Find a server** window, in addition to the
launcher's Multiplayer page. It has:
- tabs for All, Internet and LAN, with servers filtered to this game;
- search, plus Not full and No password filters;
- live ping and player counts;
- a password prompt for locked servers;
- **Address / Connect** for a direct IP or a pasted join link.

Double-click a server, or pick one and press **Join**. The window closes once
you're in.

To open it, call `net_browser_open()` from a menu button script, or use the
**Open Server Browser** action in visual scripting. It searches in the
background, so the game never stutters.

To draw your own list instead:

| Script | What it does |
| --- | --- |
| `net_browser_refresh(["all"\|"internet"\|"lan"])` | start a search |
| `net_browser_busy()` | 1 while it's searching |
| `net_browser_count()` | results |
| `net_browser_name(i)`, `_players(i)`, `_max(i)`, `_ping(i)`, `_map(i)`, `_locked(i)`, `_address(i)` | details of result `i` |
| `net_browser_join(i[, "password"])` | join result `i` |

The Internet tab needs the player signed in to OkaySpace, plus the
`game_sessions` listing (`docs/sql/game_servers.sql`). LAN servers on ports
45000–45015 always show.

### For game makers

Nothing is required. If you want more:

| Script | What it does |
| --- | --- |
| `net_launch_join()` | 1 if the launcher started the game to join someone. Skip your main menu. |
| `net_invite_friends()` | Open the launcher's Invite friends window (same as Shift+Tab) |
| `net_join_link("okay://join?…")` | Join from a link |

Set `serverName` and `maxPlayers` on the `NetworkManager`. Friends and the
browser see them. Friends over the internet can reach a game hosted with
`net_host_relay` (no port forwarding). They can also reach a directly hosted
game on a public or port-forwarded address. A game hosted directly behind a
home router is only reachable on the LAN.

### Public servers

```bash
okay-player --server --scene arena.okayscene --name "EU 1" \
            --advertise --public-addr play.example.com --region EU
```

`--advertise` lists the server in the Internet tab and heartbeats every 30
seconds. It needs:
- the OkaySpace account signed in on that machine;
- the `game_sessions` table: run `docs/sql/game_servers.sql` once.

The easiest way to get there is the editor's **Build Settings → Server**
export. It writes these settings to `server.cfg` and adds a one-command deploy
script.

The listing is removed when the server stops. A server that crashed drops out
of the browser after a couple of minutes. See [dedicated-server.md](dedicated-server.md).

### Matchmaking ("Find match")

Players don't have to pick a server. **Find match** puts them in a queue, and
the game connects them when a match is ready. You can start it from:
- the launcher's Multiplayer page (pick a game, players and an optional mode);
- the in-game **Find a server** window;
- a script or the **Find Match** visual-scripting action.

While it searches, the game shows a **Finding a match** banner with a timer,
the queue size and **Cancel**. A party leader brings the whole party along.

How players are grouped:
- Players are matched by game, mode and match size.
- **Skill** (0–1000, from wins in match results) must be close at first. The
  allowed gap widens every second, so nobody waits forever.
- **Region:** the same region is preferred for the first 30 seconds.
- **Smaller matches:** with a minimum set, a match can start with fewer players
  after 45 seconds.

Where the match is played:
1. A **dedicated server** listed for that game with room for everyone. Each
   match gets its own room.
2. Otherwise **one of the players hosts**:
   - If the site has a relay (`app_config` key `game_relay`, an
     `okayspace-relay` address), everyone connects through it with a fresh code.
     No port forwarding is needed.
   - With no relay, the host posts its address and the others join it. Without
     a relay, this only works on a LAN or with a forwarded port.

| Script | What it does |
| --- | --- |
| `net_queue_join("mode", players[, minPlayers[, "region"]])` | start searching |
| `net_queue_cancel()` | leave the queue |
| `net_queue_state()` | `"idle"`, `"searching"`, `"waiting"` (found, the host is starting), `"found"` or `"failed"` |
| `net_queue_time()`, `net_queue_count()`, `net_queue_error()` | seconds searching, players in the queue, why it failed |
| `net_queue_auto(0)` + `net_queue_accept()` | show your own "Match found: Accept" step |

Command line: `okay-player game.okayscene --find-match ranked --match-players 4`.

Run `docs/sql/matchmaking.sql` once. Skill uses `matches.sql` when it's there.

### Match results, stats and leaderboards

When a match ends, the server (a dedicated server, or the player hosting)
reports it. That records everyone's score, place, win and time. Signed-in
players get a per-game stats row, which feeds the launcher's **Leaderboards**
and **Recent players** tabs.

| Script (server) | Visual scripting | What it does |
| --- | --- | --- |
| `net_match_start()` | **Start Match** | scores back to 0, start the match clock |
| `net_match_end()` | **End Match** | save the results; first place wins |
| `net_match_end(3, 7)` | **End Match** `3 7` | save the results; these player ids win (teams) |
| `net_match_seconds()` | | how long the match has run |
| `net_match_status()` | | `""` until saved, then `"ok"` or why it failed |

Any game can read its leaderboard:

| Script | What it does |
| --- | --- |
| `net_stats_top("wins"\|"score"\|"best"\|"matches"\|"time", n)` | the top `n` as `"rank,name,value"` strings |
| `net_stats_mine(stat)` | my value (`"rank"` gives my rank by wins) |

Requirements:
- The reporting server must be signed in (`--login` on a dedicated server).
- Run `docs/sql/matches.sql` once.

Only the match host can report. A host counts if it is in the match itself, or
it's a server listed in the browser for that game. Reports are limited to one
every 15 seconds per host, and players can't write stats directly. A dedicated
server prints `match results saved` when a report lands.

### Fixed network tick

By default the network follows the frame rate: clients send their position
every frame, and the host sends snapshots at its Tick Rate (`snapshotRate`).
Set **Fixed Net Tick** on the host's Network Manager (`netTickRate`,
`net_tick_rate(30)`, or the dedicated server's `--fixed-tick`) to run it on a
fixed tick instead. 30 Hz is a good rate.

- The host counts ticks at exactly that rate, whatever its frame rate. Every
  snapshot carries the tick number and goes out once a tick, even when nothing
  moved, so clients keep up.
- The Welcome tells clients the rate. They then send their state once a tick
  instead of once a frame, so a client running at 240 fps sends 30 a second,
  with the tick it's on.
- `NetTick()` / `net_tick()` is the current tick: the host's own, or on a client
  its estimate of the host's. `OnNetTick(fn)` runs fixed-step game code on
  every tick, on the host and on clients.
- Off (0, the default), nothing changes. Older clients ignore the tick numbers
  and keep sending every frame.

With a fixed tick, other players and their objects (anything with a Network
Sync) are shown from an **interpolation buffer**: each state they send carries
its tick, and your copy shows them 3 ticks in the past (100 ms at 30 Hz),
between the two states either side of that moment. They move at the speed they
really moved, however unevenly the packets arrive. When states come later or
further apart than that allows (a long trip, a client's object relayed by the
host, a low Sync Rate), the delay grows to what's needed (a step at a time,
never past a second) and eases back when it can. Interpolation Rate 0 still
snaps to the newest state; without a fixed tick the old easing is kept.

This is the base for server-authoritative movement, prediction and lag
compensation (anti-cheat plan phase 5), which build on it.

### Server-authoritative movement

By default each client moves its own player and sends the position, which the
host can only speed-check. Tick **Authoritative movement** on the host's Network
Manager (`authoritativeMovement`, `net_authoritative(1)`) to have the host move
players itself. A dedicated server can do the same with `--authoritative` or
`authoritative = true` in server.cfg, without editing the scene:

- Each client names the object its inputs drive: its own SpawnOwned player
  (`SetPawn(obj)`, `net_set_pawn("Player")`). The object needs a controller that
  can be driven by inputs: Character Controller 3D, Player Controller (first and
  third person), Character Controller 2D and Top Down Controller 2D
  can. Look (yaw, pitch), stance, lean, sprint, jump, dash, fire and aim/zoom
  all travel in the input. A Top Down Controller 2D facing the mouse sends its
  aim as an angle, so the host turns and shoots the same way.
- Once a tick, the client sends its input: the move stick, look, and buttons
  held and pressed. Each one is numbered, and the last 3 are repeated so a lost
  packet costs nothing.
- The host runs that player's controller with the input, one input a tick.
  Only the host decides where the player is, and everyone, the player included,
  gets that position. A client's own positions (`__xform`, its avatar state)
  are ignored.
- The host clamps every input: the move stick to length 1, pitch to ±89°.
  Inputs for ticks more than a second away from the host's are refused. A
  client that sends inputs faster than the tick (a speed hack) only fills a
  queue of 8, and the oldest are dropped. A late input is covered by the last
  one for 3 ticks, then the player stops.
- It runs on the fixed tick: Fixed Net Tick, or 30 Hz if that's off.
- The player's own copy is **predicted**: it moves at once, through the same
  controller the host runs, with the input it just sent (so a change of input
  shows within one tick), while the look turns every frame. For each input the
  host answers where it left the player, and the client compares that with
  where its prediction had it. Differences under 2 cm are ignored, up to 2 m
  are eased out over a few frames, larger ones (a teleport by the host, a wall
  the client walked through) snap. Tested at 100-167 ms round trips: no
  pull-backs while walking or stopping.
- A client that predicts needs a host that answers (this version or newer);
  with an older host it follows the host's position as before.
- Off (the default), nothing changes.

### Lag compensation

With a fixed tick, the host keeps where every synced object was on each tick
for about a second. A shot a client claims (`ClaimHit`) says which moment it
saw (its view: now minus its interpolation delay), and the host checks the
claim against where everything was *then*: the target on the shot's line,
nothing solid in between. So a shot at what you saw counts, even though the
target has moved on by the time the claim arrives; one aimed at where the
target is now (but wasn't seen) doesn't. The host rewinds no further than
200 ms plus that player's round trip (the quickest of its last 8 pings, at
most the anti-cheat's `maxRewindRtt`, 0.3 s), so claiming to have seen
something long ago, or answering pings late, doesn't work. A claim without
the moment it saw (an older client) is refused. Game code on the host uses the
same limit with `NetworkManager::RewindRaycast(origin, direction, maxDistance, viewTick, ignore, shooter)`:
200 ms when no shooter is passed, or 200 ms plus that client's round trip.

Without a fixed tick, claims are checked against where things are now, with
the target's top speed times the shooter's ping as slack, as before.

### Server authority (anti-cheat basics)

The server decides; clients only ask.

| | Script (server) | Dedicated server flag |
| --- | --- | --- |
| Reject moves faster than N units/s and snap the player back (also what lets the server check a hit's origin) | `net_max_speed(N)` | `--max-speed N` |
| Drop a player's custom messages beyond N per second | `net_rate_limit(N)` | `--rate-limit N` |
| Kick after N rejected moves in a minute | (`kickAfterViolations`) | `--kick-after N` |
| Move a player legitimately (respawn, portal) | `net_teleport(id, x, y[, z])` | |
| Server moves players from their inputs | `net_authoritative(1)` | `--authoritative` (or `authoritative = true` in server.cfg) |
| Server-owned score everyone sees | `net_set_score(id, n)`, `net_score(id)` | |
| Turn the anti-cheat on / off, or load its rules | `net_anticheat([on])`, `net_anticheat_load("rules.okayac")` | `--anticheat FILE` (or `anticheat = FILE` in server.cfg) |
| A player's refusals this minute | `net_violations(id)` | |
| Sign joins so players can pin this host | | `--host-key FILE` (or `host_key = FILE`) |
| Client: join only the host with this key | `net_pin_host("key")` | |
| Flag game clocks running too fast (speed hacks) | `maxClockDrift` in the rules | (in the `--anticheat` file) |
| Keys only the server may write (score, money) | `serverVar` in the rules | (in the `--anticheat` file) |
| Fire rate / ammo for an RPC | a Net Validator's cooldown and budget; `net_budget(id, rpc, add)` | |
| A hit the server checks and applies | `net_hit(target, weapon, ox, oy, oz, dx, dy, dz)`, `on_validate_hit(...)` | |
| File anti-cheat kicks for admin review | | `--report-kicks` (or `report_kicks = true`) |

These always hold, whether or not the anti-cheat is on:
- Only a joined player can change synced variables (`SetVar`). A packet from
  anyone else is ignored.
- A client with encryption on takes the host's welcome, refusal or kick only
  sealed under its key exchange (a pinned client always joins that way), so a
  forged plaintext one can't skip the pin or end the join. Only a host that
  doesn't do the sealed join (an older one, or one with encryption off) is
  joined in the clear, and never with a pin.
- Clients can't use the engine's reserved channels, except the ones they
  really use: `__spawn`, `__despawn`, `__xform`, `__chat` and `__rpc/…`. In
  particular, only the host starts a match.
- The server stamps chat with the sender's roster name, so nobody can post as
  someone else, and cuts a chat line to 512 bytes.
- A join is checked for room, then encryption (when the server requires it),
  then the password, then the sign-in: nothing secret is checked in the
  clear. One address gets 5 join attempts in 10 s (a refused join or a new
  key exchange; a wrong password counts two; a join that gets in costs
  nothing), then it's ignored for 10 s, doubling up to 5 minutes. A dedicated
  server checks at most 64 new sign-ins at a time and tells the rest it's
  busy.
- A client's message is applied and relayed only up to the Network Manager's
  `maxRelayedBytes` (16 KB), and a client gets four times that in bytes a
  second. A client's SyncVar has a key of at most 128 bytes and a value of
  at most 4 KB, a client can create at most 256 keys, and its writes reach
  its own room only.
- A spawn must be at a real position, and a player's SpawnOwned objects go
  when it leaves.
- A network spawn must name a `.okayprefab` by a path relative to the game
  folder: no `..`, drive letters, UNC paths, device names or links leading
  outside it, and at most 4 MB. Every peer checks the spawns it receives and
  ignores any other path.
- Clients may only spawn the prefabs the host allows: the Network Manager's
  **Spawnable prefabs** and the Player Spawner's prefab, or, with the
  anti-cheat on, its spawn allowlist. The host may spawn any safe prefab.

#### Anti-cheat (opt-in)

Add an **Anti-Cheat Config** component next to the Network Manager, set its
rules in the inspector or in a `.okayac` file, or start a dedicated server
with `--anticheat rules.okayac`. The full rule list is in
`engine/include/okay/Net/AntiCheat.hpp`.

- **RPC rate:** the component's default is at most **30 calls per second to
  any one RPC per player** (`maxRpcsPerSecond`; 0 = off). `maxRpcBytes` caps
  the payload size. A **Net Validator** with its own calls/s or max bytes for an
  RPC takes the place of these defaults for that RPC.
- **Reacting in OkayScript**, on the host:
  - `function on_cheat_detected(kind, peer, detail)` (or `OnCheatDetected`)
    runs for every refusal.
  - `function on_validate_rpc(name, data, sender)` (or `OnValidateRpc`) runs
    before each client RPC:
    - `return false` refuses it: the RPC doesn't run, isn't relayed, and counts
      against the sender;
    - returning nothing allows it, so a handler that only logs blocks nothing;
    - **an error in the handler refuses the RPC** (fail closed). The error is
      logged once.
- **Visual scripts** get the same through the On Cheat Detected and On Validate
  Rpc nodes.
- **Disabling or removing** the component, or deactivating its object, turns
  its checks off again and puts back what was there before. Turning it back on
  re-applies them.

Every peer gets the **roster** once a second: names, scores and time
played. Read it with `net_roster_count()`, `net_roster_id(i)`,
`net_peer_name(id)` and `net_score(id)`, which makes a scoreboard easy. Clients
can't change scores.

The server browser asks a server who's playing with a two-step challenge, like
Steam's A2S_PLAYER, so the player list can't be aimed at a spoofed address.

## Multiplayer audit

*Was `docs/multiplayer_audit.md` until 2026-11-20.*

A read-through of the networking stack (`engine/include/okay/Net`, `engine/src/Net`,
`relay/`, the launcher's join / voice code) against the open issues, ranked by what a
player would notice. It says what works, what was wrong, what was fixed here, and what
is still open. Where something could not be run or checked, it says so.

### What exists and works

- Transport: UDP with optional libsodium encryption, a sealed join (key exchange
  before any game data), pinned host keys, per-peer rate limits and anti-cheat checks.
- Reliable messages, per-channel reliable-ordered and unreliable-sequenced streams
  (`NetChannels`), fragmentation of large payloads, a delta-compressed snapshot stream
  with acked baselines (`NetDelta`), interest management, rooms, lag compensation,
  client prediction with reconciliation and remote interpolation.
- NAT traversal through the relay (`relay/`, `RelayServer`), join links, the server
  browser, matchmaking, session tickets, the dedicated server, and launcher voice chat.
- A network simulator (`NetSim`) and many suites (`test_net_*`, `test_relay_security`).

### Fixed on this branch

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

18. **Build mode placements were not synced.** Every player built in their own copy of the world. Now
    the server is the only one that changes what players built: a client's BuildMode sends place, remove,
    move, link, unlink and undo as requests on a server-only channel (`__req/build`; a client cannot reach
    `__bld`, the server's stream), and the server does each one only if it passes: the prefab is on the
    allowlist (the scene's palettes plus `extraPrefabs`) and a safe path, the pose is finite, a real rotation
    and inside the bounds, the player's avatar is within reach, the object cap and the per-player cap and rate
    limit hold, nothing solid is in the way, and the player may edit it (only what it placed, unless
    `ownerOnly` is off; the level's own objects only with `levelEditable`). What is built is sent to every
    client on one ordered stream; a refused move or removal sends the client the truth about the object so its
    copy snaps back. Undo is per player and goes through the same checks. A client that joins later is sent
    the whole state in chunks and reconciles (what the snapshot does not name is removed), so a rejoin or a
    loaded creation also works. New generic pieces in `NetworkManager`: `SetRequestHandler` (a `__req/`
    channel that reaches the server's handler only, on all four transports, never relayed or put in the
    inbox), `AddJoinHook` and `PeerPosition`. Code: `Net/NetBuildSync.*`, `BuildMode` (`Op`, `onChanged`,
    `remoteRequest`, `Apply...`). It is idle (registers nothing, sends nothing) while the scene has no BuildMode, and
    catches up the clients already connected if one appears mid-session. Test: `tests/test_net_build_sync.cpp` (request channel, replicate, late
    join, ownership, per-player undo, caps, bounds, reach, overlap, rate limit, 28 hostile request shapes,
    forged `__bld`, hostile server messages, level objects, session end). Not replicated: the state of doors,
    movers and logic outputs (each peer runs its own logic), pieces a script makes or destroys.
19. **Quest and story state were per client.** A kill or a dialogue ending counted on every client by itself, so
    clients disagreed about quests, and a joiner started from nothing. The server's Quest Log and Story State
    are now the truth (`Net/NetQuestSync.*`): changes go to the clients as they happen (ordered, `__qst`), a
    joiner is sent everything, a client's Quest Log is a read-only copy (`QuestLog::remoteAuthority`: no local
    start, progress or kill counting) and a write to its Story State is a request the server judges
    (`StoryState::interceptWrite`). Quests are shared unless listed in `perPlayerQuests`; story variables and
    flags are shared unless they start with a `perPlayerVars` prefix. Per-player state is kept by the server per
    player, sent to that player only, and survives a resume. A client may only write its own per-player names, or
    shared names under `clientWritableVars` (none by default); a client can never start a quest or add progress.
    Limits per player and for the shared story, and a request rate. Idle (nothing registered, nothing sent) while the
    scene has neither a QuestLog nor a StoryState: the first version sent every joiner an empty snapshot, which
    changed the datagram sequence of games that use neither, and `net_interp` (its jitter proxy draws one random
    delay per datagram, so one extra datagram shifted the whole pattern) and `net_resume_resend` (it counts re-sent
    messages) caught it. A client keeps a working local log until the
    server's first snapshot (a server that does not run this changes nothing). Test:
    `tests/test_net_quest_sync.cpp` (shared and per-player scope, late joiners, resume, client writes refused,
    about 30 hostile request shapes, hostile server lines, caps, rate, forged `__qst`, session end).
20. **A late joiner saw a dead object alive.** Deaths, respawns and health went to the peers connected at the
    time. A joiner is now told who is dead (with where the blow came from, so it falls the same way) and who is
    hurt, including objects that destroy themselves on death (`m_dead`, kept until a respawn). While looking at
    this: death, respawn and damage went as separate unordered reliable messages, so over a lossy link a
    respawn could be applied before the death it follows and leave the copy dead. They now travel on one
    ordered stream (`__life`, converted back to the old messages on arrival; the old channels are still accepted).
    Test: `tests/test_net_life_late.cpp`.
21. **The new messages under loss.** `tests/test_net_build_quest_lossy.cpp` runs them through `NetSim` (20% loss,
    15% duplication, 30 ms plus or minus 25 ms): 24 build requests arrive exactly once and in order, moves end on
    the last one and a removed piece is not resurrected by a later move, quest progress and a story counter never
    go backwards, a per-player variable ends on its last write, flags that flip end on the last value, a client
    that joins in the middle of all of it ends with the server's state, and a death and respawn in quick
    succession (and a join while dead) end consistent.
    All four new suites also ran clean under AddressSanitizer and UndefinedBehaviorSanitizer (leak check on; a
    separate RelWithDebInfo build of the engine and the suites with `-fsanitize=address,undefined`). They were
    repeated 8 to 25 times each in the normal build to look for flakes: one real bug (a joiner's quest snapshot
    could lag the diffs) turned up that way and is fixed with a deterministic test.

Also covered by new tests with no bug found: loss, reordering and duplication through `NetSim`
(`tests/test_net_lossy.cpp`: exactly-once reliable, in-order ordered, never-backwards sequenced,
join, convergence of a synced object and a resume on a bad link), a deterministic fuzz of every
decoder that parses network bytes plus random and mutated datagrams at a live host, client and
relay (`tests/test_net_fuzz.cpp`), and replayed Leave / duplicated Join datagrams
(`tests/test_net_rejoin.cpp`).

Also merged from `claude/fix-net-issues`: the relay protocol v4 welcome nonce and
timeout, the refusal codes, and the little-endian `Packet` (#701, #702, #704, #705,
#721) with their tests (`net_relay_hardening`, `net_packet_layout`).

### Still wrong or missing, ranked

1. **Legacy (v1/v2) relay hellos are still accepted by default (#733).** They carry no
   address cookie, so an off-path attacker can register spoofed peers and squat session
   codes. Step 1 is done (every client built here sends version-4 hellos, and the relay logs
   legacy peers and per-version counts); turning the default off is a rollout decision for
   when older games and launchers already out there have updated (`LegacyPeers()` zero),
   and needs a release note, not a silent change. `--legacy-hello=off` already refuses them.
2. **What round 4 left open in build mode, quests and late joins.** Build-mode and quest sync are done
   (18, 19, 20); what they do not cover: the state of doors, movers and logic outputs is not replicated (each
   peer runs its own logic from the replicated pieces and links), pieces a script makes or destroys are not
   replicated, per-player quests advance only through `nm->Quests()` on the server (the stock kill, talk and
   reach sources feed the server's own log), a networked game is not saved as one (the server's state is the
   thing to save; the clients are copies), and a scene object a script destroys (not a Health Stat death) is
   not removed on a late joiner. The request channels are reliable and ordered, so a request in flight when a
   session drops is sent again after a resume and can run twice (a place twice, within the caps and the rate
   limit); a build or quest request is not idempotent. Snapshots and requests are not room scoped (a dedicated
   server that runs several rooms in one scene would share build and quest state between them).
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

### What could not be verified

- Round 4: the new suites ran on loopback, with NetSim for loss, delay, reordering and duplication, with
  encryption off and without a relay in between (the relay and encryption suites were re-run and pass, but
  none of the new messages went through a relay or a sealed session in a test; they are ordinary game
  channels, so both are expected to carry them). A snapshot of a full world (250 pieces, a few thousand story
  variables) was sized by reasoning (chunks of at most 24 KB under the 48 KB channel limit), not measured on a
  real link. The quest and build code paths were exercised through the API and raw requests; no editor or
  player UI was run (the build-mode hotbar, ghost and link wires on a client were not looked at).

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
