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

The listing is removed when the server stops. A server that crashed drops out
of the browser after a couple of minutes. See [dedicated-server.md](dedicated-server.md).
