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

## Server authority (anti-cheat basics)

The server decides; clients only ask.

| | Script (server) | Dedicated server flag |
| --- | --- | --- |
| Reject moves faster than N units/s and snap the player back | `net_max_speed(N)` | `--max-speed N` |
| Drop a player's custom messages beyond N per second | `net_rate_limit(N)` | `--rate-limit N` |
| Kick after N rejected moves in a minute | (`kickAfterViolations`) | `--kick-after N` |
| Move a player legitimately (respawn, portal) | `net_teleport(id, x, y[, z])` | |
| Server-owned score everyone sees | `net_set_score(id, n)`, `net_score(id)` | |

Every peer gets the **roster** once a second: names, scores and time
played. Read it with `net_roster_count()`, `net_roster_id(i)`,
`net_peer_name(id)` and `net_score(id)`, which makes a scoreboard easy. Clients
can't change scores.

The server browser asks a server who's playing with a two-step challenge, like
Steam's A2S_PLAYER, so the player list can't be aimed at a spoofed address.
