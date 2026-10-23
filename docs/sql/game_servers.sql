-- Server browser listings (the launcher's Multiplayer > Internet tab).
-- Dedicated servers started with `--advertise` (and games that call the
-- matchmaking API) list themselves here and heartbeat every ~30s; the launcher
-- shows rows updated in the last few minutes and pings each one directly.
-- Safe to run more than once.

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

-- Browser details: which game, current map, and flags for the filters.
alter table public.game_sessions add column if not exists game         text    not null default '';
alter table public.game_sessions add column if not exists map          text    not null default '';
alter table public.game_sessions add column if not exists has_password boolean not null default false;
alter table public.game_sessions add column if not exists dedicated    boolean not null default false;

create index if not exists game_sessions_fresh on public.game_sessions (updated_at desc);
create index if not exists game_sessions_game  on public.game_sessions (game, updated_at desc);

alter table public.game_sessions enable row level security;

-- Anyone signed in can browse; a host may only write its own rows.
drop policy if exists "sessions - browse"     on public.game_sessions;
drop policy if exists "sessions - host write" on public.game_sessions;
drop policy if exists "sessions - host edit"  on public.game_sessions;
drop policy if exists "sessions - host drop"  on public.game_sessions;
create policy "sessions - browse"     on public.game_sessions for select using (auth.uid() is not null);
create policy "sessions - host write" on public.game_sessions for insert with check (auth.uid() = host_id);
create policy "sessions - host edit"  on public.game_sessions for update using (auth.uid() = host_id) with check (auth.uid() = host_id);
create policy "sessions - host drop"  on public.game_sessions for delete using (auth.uid() = host_id);

-- Heartbeats refresh updated_at.
create or replace function public.touch_session() returns trigger
language plpgsql set search_path = public as $$
begin new.updated_at := now(); return new; end;
$$;
drop trigger if exists touch on public.game_sessions;
create trigger touch before update on public.game_sessions
  for each row execute function public.touch_session();

-- Listings from crashed servers fall out of the browser on their own (it only
-- shows fresh rows). To also delete them, schedule this with pg_cron:
--   select cron.schedule('purge-game-sessions', '*/10 * * * *',
--     $$delete from public.game_sessions where updated_at < now() - interval '15 minutes'$$);
