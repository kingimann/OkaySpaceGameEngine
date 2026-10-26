# Multiplayer in the launcher (Steam-style)

The OkaySpace launcher does for OkaySpace games what the Steam client does for
Steam games: friends see what you're playing and join you in one click, you
invite people from the launcher or from inside the game, and a server browser
lists public, friends' and LAN games. It works with any game whose scene has a
`NetworkManager`, or whose scripts use `net_host` / `net_join`. You don't
write any extra code.

## What players get

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

## How it fits together

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

## In-game server browser

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

## For game makers

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

## Public servers

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

## Matchmaking ("Find match")

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

## Match results, stats and leaderboards

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

## Fixed network tick

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

## Server-authoritative movement

By default each client moves its own player and sends the position, which the
host can only speed-check. Tick **Authoritative movement** on the host's Network
Manager (`authoritativeMovement`, `net_authoritative(1)`) to have the host move
players itself:

- Each client names the object its inputs drive: its own SpawnOwned player
  (`SetPawn(obj)`, `net_set_pawn("Player")`). The object needs a controller that
  can be driven by inputs: Character Controller 3D, First Person Controller,
  Third Person Controller, Character Controller 2D and Top Down Controller 2D
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

## Lag compensation

With a fixed tick, the host keeps where every synced object was on each tick
for about a second. A shot a client claims (`ClaimHit`) says which moment it
saw (its view: now minus its interpolation delay), and the host checks the
claim against where everything was *then*: the target on the shot's line,
nothing solid in between. So a shot at what you saw counts, even though the
target has moved on by the time the claim arrives; one aimed at where the
target is now (but wasn't seen) doesn't. The host rewinds no further than
200 ms plus that player's round trip, so claiming to have seen something long
ago doesn't work. Game code on the host can do the same with
`NetworkManager::RewindRaycast(origin, direction, maxDistance, viewTick)`.

Without a fixed tick, claims are checked against where things are now, with
the target's top speed times the shooter's ping as slack, as before.

## Server authority (anti-cheat basics)

The server decides; clients only ask.

| | Script (server) | Dedicated server flag |
| --- | --- | --- |
| Reject moves faster than N units/s and snap the player back | `net_max_speed(N)` | `--max-speed N` |
| Drop a player's custom messages beyond N per second | `net_rate_limit(N)` | `--rate-limit N` |
| Kick after N rejected moves in a minute | (`kickAfterViolations`) | `--kick-after N` |
| Move a player legitimately (respawn, portal) | `net_teleport(id, x, y[, z])` | |
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
  someone else.
- A join is checked for room, then encryption (when the server requires it),
  then the password, then the sign-in: nothing secret is checked in the
  clear. One address gets 5 join attempts in 10 s (a refused join or a new
  key exchange; a wrong password counts two; a join that gets in costs
  nothing), then it's ignored for 10 s, doubling up to 5 minutes. A dedicated
  server checks at most 64 new sign-ins at a time and tells the rest it's
  busy.
- A network spawn must name a `.okayprefab` by a path relative to the game
  folder: no `..`, drive letters, UNC paths, device names or links leading
  outside it, and at most 4 MB. Every peer checks the spawns it receives and
  ignores any other path.
- Clients may only spawn the prefabs the host allows: the Network Manager's
  **Spawnable prefabs** and the Player Spawner's prefab, or, with the
  anti-cheat on, its spawn allowlist. The host may spawn any safe prefab.

### Anti-cheat (opt-in)

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
