# Supabase backend for OkaySpace

> **What this is:** OkaySpace's online features — accounts, **cloud saves**, and **leaderboards** — run on a [Supabase](https://supabase.com) project.
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `online/`, paths and links checked; the text itself was not re-checked against the code).

OkaySpace's online features — accounts, **cloud saves**, and **leaderboards** — run
on a [Supabase](https://supabase.com) project. Auth works out of the box once the
project URL + anon key are configured (see [accounts.md](accounts.md)). Cloud saves
and leaderboards need two tables, which you create once by running the SQL below.

## 1. Configure the project

Set the project URL and **anon/publishable** key (safe to ship — Row-Level Security
protects the data). Any of:

```bash
export OKAY_ACCOUNT_SERVER=https://YOUR-PROJECT.supabase.co
export OKAY_ACCOUNT_API_KEY=YOUR-ANON-PUBLIC-KEY
```

…or `account_server.txt` / `account_apikey.txt` next to the binary, or bake them in
at build time with `-DOKAY_DEFAULT_ACCOUNT_URL=… -DOKAY_DEFAULT_ACCOUNT_KEY=…`.

## 2. Create the tables (SQL editor → run once)

Open your project's **SQL Editor** and run this. It creates the tables, turns on
Row-Level Security, and adds policies so each player can only touch their own rows
(leaderboards are world-readable). A trigger keeps each player's *best* score.

```sql
-- Moved (OkayBackend round 3, 2026-11-26): the definitions now live in docs/sql/cloud_saves.sql and
-- docs/sql/leaderboards.sql, unchanged. Run those two files.
```

## 3. Use it

From C++:

```cpp
okay::Account::CloudSave("slot1", saveBlob);     // upsert
std::string blob = okay::Account::CloudLoad("slot1");
okay::Account::LeaderboardSubmit("arcade", 1250); // keeps your best
for (auto& e : okay::Account::LeaderboardTop("arcade", 10))
    printf("#%d %s %ld\n", e.rank, e.name.c_str(), e.score);
```

From OkayScript:

```javascript
cloud_save("slot1", save_blob);
var blob = cloud_load("slot1");
leaderboard_submit("arcade", 1250);
var top = leaderboard_top("arcade", 10);   // ["1,alice,1250", ...]
```

These call the Supabase REST API (`/rest/v1/cloud_saves`, `/rest/v1/leaderboards`)
with the signed-in player's token, so RLS enforces who can read/write what. A player
must be signed in (`account_login`) first.

## How the client maps to REST

| Call | Request |
|------|---------|
| `CloudSave(key,data)` | `POST /rest/v1/cloud_saves` upsert `[{key,data}]` (`Prefer: resolution=merge-duplicates`) |
| `CloudLoad(key)` | `GET /rest/v1/cloud_saves?select=data&key=eq.<key>` |
| `CloudDelete(key)` | `DELETE /rest/v1/cloud_saves?key=eq.<key>` |
| `CloudList()` | `GET /rest/v1/cloud_saves?select=key` |
| `LeaderboardSubmit(board,score)` | `POST /rest/v1/leaderboards` upsert `[{board,name,score}]` |
| `LeaderboardTop(board,n)` | `GET /rest/v1/leaderboards?select=name,score&board=eq.<board>&order=score.desc&limit=<n>` |

A custom (non-Supabase) account server keeps using the simpler `/cloud/*` and
`/leaderboard/*` routes (see the reference server in `examples/account-server/`).

## Matchmaking / server browser (optional)

`okay::Matchmaking` advertises and discovers multiplayer sessions through a
`game_sessions` table. A host registers its session; clients list the open ones and
connect over the normal UDP transport. Supabase provides **discovery, not relay** —
the host must be reachable at the advertised `host_addr:port` (LAN, port-forward, or
a public address). Run this SQL once to add the table:

```sql
create table if not exists public.game_sessions (
  id          uuid        not null default gen_random_uuid() primary key,
  host_id     uuid        not null default auth.uid() references auth.users on delete cascade,
  name        text        not null default '',
  room        text        not null default '',
  host_addr   text        not null default '',
  region      text        not null default '',
  port        int         not null default 0,
  players     int         not null default 0,
  max_players int         not null default 8,
  updated_at  timestamptz not null default now()
);
alter table public.game_sessions enable row level security;

-- Anyone signed in can browse; a host may only write its own session rows.
create policy "sessions - browse"     on public.game_sessions for select using (auth.uid() is not null);
create policy "sessions - host write" on public.game_sessions for insert with check (auth.uid() = host_id);
create policy "sessions - host edit"  on public.game_sessions for update using (auth.uid() = host_id) with check (auth.uid() = host_id);
create policy "sessions - host drop"  on public.game_sessions for delete using (auth.uid() = host_id);

-- Refresh updated_at on every change so heartbeats keep a session "live".
create or replace function public.touch_session() returns trigger as $$
begin new.updated_at := now(); return new; end;
$$ language plpgsql;
drop trigger if exists touch on public.game_sessions;
create trigger touch before update on public.game_sessions
  for each row execute function public.touch_session();
```

Optionally purge sessions from crashed hosts with a scheduled job (Supabase →
Database → Cron), e.g. `delete from public.game_sessions where updated_at < now() - interval '2 minutes';`.

## Authenticated multiplayer (verify a Supabase identity on join)

`NetworkManager` can require each joining client to present its Supabase access
token and bind it to a verified account id. The engine never calls the auth backend
itself — you hand it a verifier, so wire it to `okay::Account`:

```cpp
// Host: verify each client's token against Supabase (auth/v1/user) and bind its id.
net->SetTokenVerifier([](const std::string& token, std::string& outUserId) {
    return okay::Account::VerifyToken(token, outUserId);
});
net->StartServer(45000);

// Client: present our signed-in token when joining.
net->SetAuthToken(okay::Account::Token());
net->StartClient(host, 45000);
```

A client with no/invalid token is refused with `"authentication failed"`; an
accepted peer's id is available to the host via `net->PeerUserId(peerId)` (and to the
client via `net->LocalUserId()`). With no verifier set the server stays open
(anonymous joins), so this is opt-in. Combine with `net->encryption = true` for an
authenticated **and** encrypted session.

No verifier needed for the requests above to be safe: tokens are validated
server-side by Supabase, and all table access is gated by the RLS policies in this
doc — the anon key being public is fine.

Usage (C++):

```cpp
// Host: advertise, then heartbeat while running, then remove on quit.
std::string id = okay::Matchmaking::Host("Bob's game", myPublicAddr, 45000, /*max*/8, "arena");
// ...each ~30s: okay::Matchmaking::Heartbeat(id, net->PeerCount() + 1);
// on stop: okay::Matchmaking::Unregister(id);

// Client: browse and join.
for (const okay::GameSession& s : okay::Matchmaking::List("arena"))
    printf("%s  %d/%d  @%s:%d\n", s.name.c_str(), s.players, s.maxPlayers, s.hostAddr.c_str(), s.port);
// pick one: net->StartClient(chosen.hostAddr, chosen.port);
```

From OkayScript:

```javascript
// Host
var id = mm_host("My Game", my_addr, 45000, 8, "arena");  // -> session id
// each ~30s: mm_heartbeat(id, net_peers() + 1);
// on quit:  mm_unregister(id);

// Client: browse then join by index
var n = mm_refresh("arena");          // fetch open sessions, returns the count
for (var i = 0; i < n; i = i + 1)
    print(mm_name(i) + "  " + mm_players(i) + "/" + mm_max(i) + "  @" + mm_addr(i));
if (n > 0) mm_join(0);                // connects over UDP to that session
```
