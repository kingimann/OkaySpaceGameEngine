-- OkaySpace public profiles: achievement + games showcase (Supabase).
-- Run once in the Supabase SQL editor AFTER social.sql. Idempotent: safe to
-- re-run. Until it runs, the launcher still shows public profiles; it just
-- hides other players' showcases and keeps your own picks on this device.
--
-- What it adds:
--   * profiles.showcase_achievements / showcase_games / banner_accent:
--     the achievements (up to 6) and Workshop games (up to 4) a player pins
--     to their profile, plus an optional banner colour.
--   * user_achievements: a public mirror of each player's unlocked
--     achievements (achievements are stored locally first; the launcher
--     uploads them here so other players can see them). Owner writes,
--     everyone reads.
--   * profile_friend_count(uuid): friend count for any profile (friendships
--     are private under RLS, so a SECURITY DEFINER count is exposed instead).

-- ---------------------------------------------------------------------------
-- profiles: showcase columns
-- ---------------------------------------------------------------------------
alter table public.profiles add column if not exists showcase_achievements text[]   default '{}';
alter table public.profiles add column if not exists showcase_games        bigint[] default '{}';
alter table public.profiles add column if not exists banner_accent         int;    -- null = use profiles.accent
do $$ begin
  if not exists (select 1 from pg_constraint where conname = 'profiles_showcase_ach_max') then
    alter table public.profiles add constraint profiles_showcase_ach_max
      check (showcase_achievements is null or cardinality(showcase_achievements) <= 6);
  end if;
  if not exists (select 1 from pg_constraint where conname = 'profiles_showcase_games_max') then
    alter table public.profiles add constraint profiles_showcase_games_max
      check (showcase_games is null or cardinality(showcase_games) <= 4);
  end if;
end $$;
-- (profiles already has "profiles are readable" for select and
--  "update own profile" for update in social.sql, which cover these columns.)

-- ---------------------------------------------------------------------------
-- user_achievements: one row per (player, achievement id).
-- ---------------------------------------------------------------------------
create table if not exists public.user_achievements (
  owner       uuid not null default auth.uid() references auth.users on delete cascade,
  ach_id      text not null,
  label       text,
  unlocked_at timestamptz default now(),
  created_at  timestamptz default now(),
  primary key (owner, ach_id)
);
alter table public.user_achievements enable row level security;
drop policy if exists "achievements are readable" on public.user_achievements;
drop policy if exists "record own achievement"    on public.user_achievements;
drop policy if exists "edit own achievement"      on public.user_achievements;
drop policy if exists "delete own achievement"    on public.user_achievements;
create policy "achievements are readable" on public.user_achievements for select using (true);
create policy "record own achievement"    on public.user_achievements for insert with check (auth.uid() = owner);
create policy "edit own achievement"      on public.user_achievements for update using (auth.uid() = owner);
create policy "delete own achievement"    on public.user_achievements for delete using (auth.uid() = owner);

-- ---------------------------------------------------------------------------
-- Friend count for any profile (the stats row on a public profile).
-- ---------------------------------------------------------------------------
create or replace function public.profile_friend_count(p_uid uuid) returns int
  language sql security definer stable set search_path = public as $$
    select count(*)::int from public.friendships
     where status = 'accepted' and (requester = p_uid or addressee = p_uid);
  $$;
grant execute on function public.profile_friend_count(uuid) to anon, authenticated;

-- Profiles list a player's published games: make the author lookup cheap.
create index if not exists workshop_items_author_idx on public.workshop_items (author);
