-- Leaderboards: one best score per (board, player) (AccountService::LeaderboardSubmit / LeaderboardTop, sent through
-- OkayBackend's Database: okay/Backend/Database.hpp). Anyone may read; a player writes only their own row, and a trigger
-- keeps the higher score on an upsert. (Per-game match stats are a different thing: docs/sql/matches.sql.)
-- NOT YET CHECKED AGAINST THE LIVE PROJECT. These definitions were copied unchanged (OkayBackend round 3, 2026-11-26) from
-- docs/online/supabase-backend.md, the only place they were written down; nobody compared them with what the production
-- Supabase project has. Not safe to run twice as written: its policy statements fail when the policy already exists.

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
