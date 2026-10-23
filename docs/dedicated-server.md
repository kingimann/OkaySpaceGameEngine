# Dedicated servers

Any OkaySpace game can run as its own **headless, authoritative game server**:
the same player executable, with no window, GPU or audio. Players connect to it
directly over UDP. Supabase stays the backend for everything that isn't
real-time gameplay: sign-in, profiles, friends, lobbies, matchmaking, stats.

```
Players ──UDP──▶ okay-player --server (your scenes + scripts, authoritative)
   │                     │
   └──── HTTPS ────▶ Supabase (auth, lobbies/matchmaking, results, leaderboards)
```

## Run one

```bash
okay-player --server --scene arena.okayscene --port 45000 --max-players 16 --auth
```

| Flag | What |
| --- | --- |
| `--port N` | UDP port (default 45000) |
| `--scene PATH` | scene to run (default: the game's startup scene) |
| `--max-players N` | player limit (default 16) |
| `--tick HZ` | simulation + network rate (default 30) |
| `--name TEXT` | server name players see |
| `--password TEXT` | require a password |
| `--auth` | only signed-in OkaySpace accounts may join |
| `--no-encryption` | don't encrypt traffic |
| `--status SECONDS` | status line interval (default 60, 0 = off) |
| `--run-for SECONDS` | stop after this long |
| `--advertise` | list it in the launcher's server browser (sign in on this machine; run `docs/sql/game_servers.sql` once) |
| `--public-addr HOST` | the address players connect to (required with `--advertise`) |
| `--region TEXT` | region shown in the browser |

Container hosts can use environment variables instead: `OKAY_SERVER_PORT`,
`OKAY_SERVER_MAX_PLAYERS`, `OKAY_SERVER_TICK`, `OKAY_SERVER_NAME`,
`OKAY_SERVER_PASSWORD`, `OKAY_SERVER_AUTH=1`, `OKAY_SERVER_ADVERTISE=1`,
`OKAY_SERVER_PUBLIC_ADDR`, `OKAY_SERVER_REGION` (flags win). The server stops
cleanly on Ctrl+C or SIGTERM.

It logs one line per join/leave (with the verified account id when `--auth` is
on) and a periodic status line:

```
[server] "EU 1" listening on UDP 45000 · up to 16 players · 30 Hz · sign-in required · encrypted
[server] + maya joined (id 1, account 6f94…) · 1 online
[server] 1/16 players · up 5m 00s · tick 0.42 ms avg
```

## How it works

- The scene's `NetworkManager` (or a new one, if the scene has none) is put in
  **dedicated mode**: the server isn't a player (no avatar in snapshots) and it
  hears messages from every room, so one process can host many matches.
- **Sign-in check:** with `--auth`, each joining player's Supabase access token is
  verified (`auth/v1/user`) on a background thread. The network tick never
  waits on it. The player's join retry gets the answer a moment later, and
  answers are cached for 5 minutes.
- **Clients send their account automatically:** a signed-in player's game puts its
  token on every `NetworkManager` in the scene before it joins.
- Game logic in scripts runs on the server exactly as in the editor. Use
  `net_is_server()` to run authoritative logic only there.

## Hosting

Start with one small Linux VPS (Hetzner, OVH, DigitalOcean) running a few
server processes on different ports, plus `okayspace-relay` for player-hosted
games. Open the UDP ports in the firewall. Later, Fly.io Machines, Edgegap or
Hathora can start servers per match on demand.

## Roadmap

1. ✅ Headless server mode with Supabase sign-in on join
   - ✅ Launcher integration: join friends, invites, server browser
     ([launcher-multiplayer.md](launcher-multiplayer.md))
2. Server-side validation of player movement and important actions
3. `game_servers` / `matches` tables + an `allocate_match` Edge Function
4. Match tickets: request a match, get an address + ticket, join
5. "Dedicated server" export in Build Settings (Linux) + a deploy script
6. Match results → Supabase (stats, leaderboards, achievements)
