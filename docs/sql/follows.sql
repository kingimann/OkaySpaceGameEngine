-- OkaySpace follows (Supabase). Run once in the SQL editor AFTER social.sql.
-- Idempotent: safe to re-run. Until it runs, the launcher hides Follow buttons
-- and follower counts.
--
-- What it adds:
--   * follows: one-way follows (no request / accept, unlike friendships).
--     Anyone can follow a player; the follow graph is public like the profile.
--     You can unfollow, and you can remove someone who follows you.
--   * profiles.followers_see_activity: when true (the default), the people who
--     follow you also see your activity feed (came online, playing,
--     achievements, published). Friends always see it.
--   * follow_counts(p_uid): follower / following counts for a profile.

create table if not exists public.follows (
  follower   uuid not null default auth.uid() references auth.users on delete cascade,
  followee   uuid not null references auth.users on delete cascade,
  created_at timestamptz default now(),
  primary key (follower, followee),
  constraint follows_not_self check (follower <> followee)
);
create index if not exists follows_followee on public.follows (followee);

alter table public.follows enable row level security;
drop policy if exists "follows are readable"  on public.follows;
drop policy if exists "follow someone"        on public.follows;
drop policy if exists "unfollow or remove"    on public.follows;
create policy "follows are readable" on public.follows for select using (true);
create policy "follow someone"       on public.follows for insert with check (auth.uid() = follower);
-- Unfollow (I'm the follower) or remove a follower (I'm the one followed).
create policy "unfollow or remove"   on public.follows for delete using (auth.uid() = follower or auth.uid() = followee);

create or replace function public.follow_counts(p_uid uuid)
returns table (followers bigint, following bigint)
language sql stable security definer set search_path = public as $$
  select (select count(*) from public.follows where followee = p_uid),
         (select count(*) from public.follows where follower = p_uid);
$$;

-- Followers see your activity unless you turn it off (Account > Privacy).
alter table public.profiles add column if not exists followers_see_activity boolean not null default true;

drop policy if exists "see friends activity" on public.activity;
create policy "see friends activity" on public.activity for select using (
  actor = auth.uid()
  or actor in (
    select case when requester = auth.uid() then addressee else requester end
    from public.friendships
    where status = 'accepted' and (requester = auth.uid() or addressee = auth.uid())
  )
  or (actor in (select followee from public.follows where follower = auth.uid())
      and exists (select 1 from public.profiles p where p.id = actor and p.followers_see_activity))
);
