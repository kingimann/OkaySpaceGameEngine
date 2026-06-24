# Supabase backend for OkaySpace

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
-- Per-account cloud saves: progress that follows the player across devices.
create table if not exists public.cloud_saves (
  user_id    uuid        not null default auth.uid() references auth.users on delete cascade,
  key        text        not null,
  data       text        not null,
  updated_at timestamptz not null default now(),
  primary key (user_id, key)
);
alter table public.cloud_saves enable row level security;

create policy "own cloud saves - read"   on public.cloud_saves for select using (auth.uid() = user_id);
create policy "own cloud saves - write"  on public.cloud_saves for insert with check (auth.uid() = user_id);
create policy "own cloud saves - update" on public.cloud_saves for update using (auth.uid() = user_id) with check (auth.uid() = user_id);
create policy "own cloud saves - delete" on public.cloud_saves for delete using (auth.uid() = user_id);

-- Global leaderboards: one best row per (board, player). Anyone may read the
-- ranking; a player may only write their own row.
create table if not exists public.leaderboards (
  user_id    uuid        not null default auth.uid() references auth.users on delete cascade,
  board      text        not null,
  name       text        not null,
  score      bigint      not null default 0,
  updated_at timestamptz not null default now(),
  primary key (board, user_id)
);
alter table public.leaderboards enable row level security;

create policy "leaderboards - read all"  on public.leaderboards for select using (true);
create policy "leaderboards - write own" on public.leaderboards for insert with check (auth.uid() = user_id);
create policy "leaderboards - update own" on public.leaderboards for update using (auth.uid() = user_id) with check (auth.uid() = user_id);

-- Keep only the player's highest score: on an upsert that would lower it, keep the
-- old value. So the client can always just submit; the table never regresses.
create or replace function public.leaderboards_keep_best() returns trigger as $$
begin
  if new.score < old.score then new.score := old.score; end if;
  new.updated_at := now();
  return new;
end;
$$ language plpgsql;

drop trigger if exists keep_best on public.leaderboards;
create trigger keep_best before update on public.leaderboards
  for each row execute function public.leaderboards_keep_best();
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

Usage (C++):

```cpp
// Host: advertise, then heartbeat while running, then remove on quit.
std::string id = okay::Matchmaking::Host("Bob's game", myPublicAddr, 45000, /*max*/8, "arena");
// ...each ~15s: okay::Matchmaking::Heartbeat(id, net->PeerCount() + 1);
// on stop: okay::Matchmaking::Unregister(id);

// Client: browse and join.
for (const okay::GameSession& s : okay::Matchmaking::List("arena"))
    printf("%s  %d/%d  @%s:%d\n", s.name.c_str(), s.players, s.maxPlayers, s.hostAddr.c_str(), s.port);
// pick one: net->StartClient(chosen.hostAddr, chosen.port);
```
