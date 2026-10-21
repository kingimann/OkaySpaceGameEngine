-- OkaySpace profiles: playtime, social links, verified tag, staff badges,
-- achievement points (Supabase). Run once in the SQL editor AFTER social.sql
-- and profile_showcase.sql. Idempotent: safe to re-run. Until it runs, the
-- launcher keeps playtime and points on this device and hides the rest.
--
-- What it adds:
--   * user_playtime: seconds played per game (and "editor" for time in the
--     editor), written by the owner's launcher, readable by everyone.
--   * profiles.social_links: {"x": "...", "youtube": "...", ...} shown on the
--     public profile.
--   * profiles.verified: the blue check. Players can't set it on themselves
--     (a trigger keeps the old value); flip it here in the SQL editor:
--       update public.profiles set verified = true where username = 'someone';
--   * profile_badges: staff-awarded badges ("staff", "developer", "moderator",
--     "beta", "partner"). Read by everyone, written only from the SQL editor:
--       insert into public.profile_badges (owner, badge)
--       select id, 'beta' from public.profiles where username = 'someone';
--   * user_achievements.points: what each unlocked achievement was worth.

-- ---------------------------------------------------------------------------
-- Playtime
-- ---------------------------------------------------------------------------
create table if not exists public.user_playtime (
  owner       uuid not null default auth.uid() references auth.users on delete cascade,
  game_key    text not null check (char_length(game_key) between 1 and 200),
  title       text check (char_length(title) <= 120),
  seconds     bigint not null default 0 check (seconds >= 0),
  last_played timestamptz default now(),
  primary key (owner, game_key)
);
alter table public.user_playtime enable row level security;
drop policy if exists "playtime is readable"  on public.user_playtime;
drop policy if exists "record own playtime"   on public.user_playtime;
drop policy if exists "update own playtime"   on public.user_playtime;
drop policy if exists "delete own playtime"   on public.user_playtime;
create policy "playtime is readable" on public.user_playtime for select using (true);
create policy "record own playtime"  on public.user_playtime for insert with check (auth.uid() = owner);
create policy "update own playtime"  on public.user_playtime for update using (auth.uid() = owner) with check (auth.uid() = owner);
create policy "delete own playtime"  on public.user_playtime for delete using (auth.uid() = owner);

-- ---------------------------------------------------------------------------
-- Social links + verified
-- ---------------------------------------------------------------------------
alter table public.profiles add column if not exists social_links jsonb   not null default '{}'::jsonb;
alter table public.profiles add column if not exists verified     boolean not null default false;
do $$ begin
  if not exists (select 1 from pg_constraint where conname = 'profiles_social_links_size') then
    alter table public.profiles add constraint profiles_social_links_size
      check (jsonb_typeof(social_links) = 'object' and pg_column_size(social_links) <= 4096);
  end if;
end $$;

-- Only the service role / SQL editor may change `verified` ("update own
-- profile" would otherwise let anyone verify themselves).
create or replace function public.profiles_keep_verified() returns trigger
language plpgsql as $$
begin
  if coalesce(auth.role(), '') in ('authenticated', 'anon') then
    if tg_op = 'INSERT' then
      new.verified := false;
    elsif new.verified is distinct from old.verified then
      new.verified := old.verified;
    end if;
  end if;
  return new;
end $$;
drop trigger if exists profiles_keep_verified on public.profiles;
create trigger profiles_keep_verified before insert or update on public.profiles
  for each row execute function public.profiles_keep_verified();

-- ---------------------------------------------------------------------------
-- Staff badges (no insert/update/delete policy: players can't award them)
-- ---------------------------------------------------------------------------
create table if not exists public.profile_badges (
  owner      uuid not null references auth.users on delete cascade,
  badge      text not null check (badge in ('staff', 'developer', 'moderator', 'beta', 'partner')),
  awarded_at timestamptz default now(),
  primary key (owner, badge)
);
alter table public.profile_badges enable row level security;
drop policy if exists "badges are readable" on public.profile_badges;
create policy "badges are readable" on public.profile_badges for select using (true);

-- ---------------------------------------------------------------------------
-- Achievement points
-- ---------------------------------------------------------------------------
alter table public.user_achievements add column if not exists points int not null default 10;
do $$ begin
  if not exists (select 1 from pg_constraint where conname = 'user_achievements_points_range') then
    alter table public.user_achievements add constraint user_achievements_points_range
      check (points between 0 and 100);
  end if;
end $$;
