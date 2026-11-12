-- OkaySpace server-side blocks + enforced privacy (Supabase).
-- Run once in the SQL editor AFTER social.sql (and after follows.sql / profile_extras.sql
-- / profile_showcase.sql / group_chats.sql if you use them: it reads the profiles
-- columns that exist when it runs). Idempotent: safe to re-run, and re-run it any
-- time a later file adds a column to public.profiles (see "profiles columns").
--
-- Until this file runs the launcher keeps blocking and privacy on the PC only
-- and says so in Settings > Privacy & Safety.
--
-- What it adds
--   * blocks: who blocked whom. You can create and delete only your own blocks
--     and you can read the blocks you are part of (blocker or blocked).
--   * privacy_settings: one row per player (dm_from, profile_visibility,
--     online_visibility, friend_requests, calls_from). Only you can read or
--     change your row; other people's rows are used by the rules below, never
--     exposed.
--   * Rules the DATABASE enforces (a modified client or a direct API call gets
--     the same answer as the launcher):
--       - DMs (public.messages): nobody can DM someone who blocked them or whom
--         they blocked; recipients can limit DMs to friends or nobody.
--       - Calls are DMs starting "[Call] ": same, plus the separate calls_from.
--       - Friend requests (public.friendships): refused when blocked either way
--         or when the addressee set friend_requests = 'nobody'.
--       - Presence (profiles.last_seen / playing / session_code) and the
--         private profile fields (bio, status, favorite_game, location) are
--         hidden from blocked people and from anyone the owner's visibility
--         setting excludes (everyone | friends | nobody).
--       - Activity feed rows are hidden from blocked people.
--     Refusals are errors named "okay_rule:<reason>" (HTTP 403, code PT403) so
--     the launcher can say exactly which rule said no. A block from the other
--     side is reported as the same "does not accept" text as a closed setting,
--     so a block does not reveal itself.
--   * Blocking someone also removes any friendship or pending request between
--     you.
--
-- profiles columns: the sensitive columns can no longer be read straight from
-- public.profiles by the API roles. Read them through public.profiles_visible
-- (same columns, private ones are null when you may not see them). The launcher
-- does this; it falls back to the table if this file has not been run.
--
-- Service role / SQL editor / SECURITY DEFINER code (auth.uid() is null) is not
-- restricted, so server-side jobs keep working.

-- ---------------------------------------------------------------------------
-- Private helper schema. Not exposed through the REST API (Supabase exposes
-- only the schemas listed in API settings; keep this one off that list), so
-- nobody can probe "did A block B?" with an RPC call.
-- ---------------------------------------------------------------------------
create schema if not exists okay_private;
grant usage on schema okay_private to anon, authenticated, service_role;

-- ---------------------------------------------------------------------------
-- blocks
-- ---------------------------------------------------------------------------
create table if not exists public.blocks (
  blocker      uuid not null default auth.uid() references auth.users on delete cascade,
  blocked      uuid not null references auth.users on delete cascade,
  blocked_name text,                                  -- name shown in the blocker's list
  created_at   timestamptz not null default now(),
  primary key (blocker, blocked),
  constraint blocks_not_self check (blocker <> blocked)
);
create index if not exists blocks_blocked_idx on public.blocks (blocked);
alter table public.blocks enable row level security;
revoke all on public.blocks from anon;
drop policy if exists "see blocks I am part of" on public.blocks;
drop policy if exists "block as myself"          on public.blocks;
drop policy if exists "unblock myself"           on public.blocks;
create policy "see blocks I am part of" on public.blocks for select
  using (auth.uid() = blocker or auth.uid() = blocked);
create policy "block as myself" on public.blocks for insert
  with check (auth.uid() = blocker);
create policy "unblock myself" on public.blocks for delete
  using (auth.uid() = blocker);
-- (no update policy: a block is created or deleted, never edited)

-- ---------------------------------------------------------------------------
-- privacy_settings
-- ---------------------------------------------------------------------------
create table if not exists public.privacy_settings (
  user_id            uuid primary key default auth.uid() references auth.users on delete cascade,
  dm_from            text not null default 'everyone' check (dm_from in ('everyone', 'friends', 'nobody')),
  profile_visibility text not null default 'everyone' check (profile_visibility in ('everyone', 'friends', 'nobody')),
  online_visibility  text not null default 'everyone' check (online_visibility in ('everyone', 'friends', 'nobody')),
  friend_requests    text not null default 'everyone' check (friend_requests in ('everyone', 'nobody')),
  calls_from         text not null default 'friends'  check (calls_from in ('everyone', 'friends', 'nobody')),
  updated_at         timestamptz not null default now()
);
alter table public.privacy_settings enable row level security;
revoke all on public.privacy_settings from anon;
drop policy if exists "read own privacy"   on public.privacy_settings;
drop policy if exists "insert own privacy" on public.privacy_settings;
drop policy if exists "update own privacy" on public.privacy_settings;
drop policy if exists "delete own privacy" on public.privacy_settings;
create policy "read own privacy"   on public.privacy_settings for select using (auth.uid() = user_id);
create policy "insert own privacy" on public.privacy_settings for insert with check (auth.uid() = user_id);
create policy "update own privacy" on public.privacy_settings for update using (auth.uid() = user_id) with check (auth.uid() = user_id);
create policy "delete own privacy" on public.privacy_settings for delete using (auth.uid() = user_id);

-- ---------------------------------------------------------------------------
-- Helpers (SECURITY DEFINER so they can read other people's rows; they take the
-- viewer explicitly and never trust a client-supplied value: callers pass
-- auth.uid() or a row column that RLS already ties to auth.uid()).
-- ---------------------------------------------------------------------------
create or replace function okay_private.is_blocked_between(a uuid, b uuid)
  returns boolean language sql stable security definer set search_path = public, pg_temp as $$
  select exists (select 1 from public.blocks
                  where (blocker = a and blocked = b) or (blocker = b and blocked = a));
$$;

create or replace function okay_private.are_friends(a uuid, b uuid)
  returns boolean language sql stable security definer set search_path = public, pg_temp as $$
  select exists (select 1 from public.friendships
                  where status = 'accepted'
                    and ((requester = a and addressee = b) or (requester = b and addressee = a)));
$$;

-- p_key: dm_from | calls_from | profile_visibility | online_visibility | friend_requests.
-- A player who never saved settings has the column defaults.
create or replace function okay_private.setting(p_user uuid, p_key text)
  returns text language plpgsql stable security definer set search_path = public, pg_temp as $$
declare v text;
begin
  execute format('select %I from public.privacy_settings where user_id = $1', p_key) into v using p_user;
  if v is null then
    v := case p_key when 'calls_from' then 'friends' else 'everyone' end;
  end if;
  return v;
end $$;

-- May p_viewer see p_owner's presence ('presence') or private profile fields ('profile')?
create or replace function okay_private.can_see(p_owner uuid, p_viewer uuid, p_kind text)
  returns boolean language plpgsql stable security definer set search_path = public, pg_temp as $$
declare v text;
begin
  if p_viewer is not null and p_viewer = p_owner then return true; end if;
  if p_viewer is not null and okay_private.is_blocked_between(p_owner, p_viewer) then return false; end if;
  v := okay_private.setting(p_owner, case p_kind when 'presence' then 'online_visibility' else 'profile_visibility' end);
  if v = 'everyone' then return true; end if;
  if v = 'friends' then return p_viewer is not null and okay_private.are_friends(p_owner, p_viewer); end if;
  return false;
end $$;

-- The reason a DM / call from p_sender to p_recipient is refused, or null if allowed.
create or replace function okay_private.dm_rule(p_sender uuid, p_recipient uuid, p_body text)
  returns text language plpgsql stable security definer set search_path = public, pg_temp as $$
declare v text;
begin
  if p_sender is null or p_recipient is null or p_sender = p_recipient then return null; end if;
  if exists (select 1 from public.blocks where blocker = p_sender and blocked = p_recipient) then return 'you_blocked'; end if;
  if exists (select 1 from public.blocks where blocker = p_recipient and blocked = p_sender) then return 'dm_nobody'; end if;
  if left(coalesce(p_body, ''), 9) = '[CallEnd]' then return null; end if;   -- hang-up follows a call that was allowed
  if left(coalesce(p_body, ''), 7) = '[Call] ' then
    v := okay_private.setting(p_recipient, 'calls_from');
    if v = 'nobody' then return 'calls_nobody'; end if;
    if v = 'friends' and not okay_private.are_friends(p_sender, p_recipient) then return 'calls_friends'; end if;
    return null;
  end if;
  v := okay_private.setting(p_recipient, 'dm_from');
  if v = 'nobody' then return 'dm_nobody'; end if;
  if v = 'friends' and not okay_private.are_friends(p_sender, p_recipient) then return 'dm_friends'; end if;
  return null;
end $$;

-- The reason a friend request is refused, or null.
create or replace function okay_private.friend_rule(p_requester uuid, p_addressee uuid)
  returns text language plpgsql stable security definer set search_path = public, pg_temp as $$
begin
  if p_requester is null or p_addressee is null or p_requester = p_addressee then return null; end if;
  if exists (select 1 from public.blocks where blocker = p_requester and blocked = p_addressee) then return 'you_blocked'; end if;
  if exists (select 1 from public.blocks where blocker = p_addressee and blocked = p_requester) then return 'friend_requests_off'; end if;
  if okay_private.setting(p_addressee, 'friend_requests') = 'nobody' then return 'friend_requests_off'; end if;
  return null;
end $$;

revoke execute on all functions in schema okay_private from public;
grant execute on all functions in schema okay_private to anon, authenticated, service_role;

-- ---------------------------------------------------------------------------
-- Triggers: give a precise refusal reason (BEFORE INSERT runs before the policy
-- check below). Skipped when auth.uid() is null (service role / SQL editor).
-- ---------------------------------------------------------------------------
create or replace function okay_private.messages_rules() returns trigger
  language plpgsql security definer set search_path = public, pg_temp as $$
declare r text;
begin
  if auth.uid() is null then return new; end if;
  r := okay_private.dm_rule(new.sender, new.recipient, new.body);
  if r is not null then raise exception 'okay_rule:%', r using errcode = 'PT403'; end if;
  return new;
end $$;
drop trigger if exists okay_messages_rules on public.messages;
create trigger okay_messages_rules before insert on public.messages
  for each row execute function okay_private.messages_rules();

create or replace function okay_private.friendships_rules() returns trigger
  language plpgsql security definer set search_path = public, pg_temp as $$
declare r text;
begin
  if auth.uid() is null then return new; end if;
  r := okay_private.friend_rule(new.requester, new.addressee);
  if r is not null then raise exception 'okay_rule:%', r using errcode = 'PT403'; end if;
  return new;
end $$;
drop trigger if exists okay_friendships_rules on public.friendships;
create trigger okay_friendships_rules before insert on public.friendships
  for each row execute function okay_private.friendships_rules();

-- Blocking ends the friendship (or pending request) between the two.
create or replace function okay_private.blocks_cleanup() returns trigger
  language plpgsql security definer set search_path = public, pg_temp as $$
begin
  delete from public.friendships
   where (requester = new.blocker and addressee = new.blocked)
      or (requester = new.blocked and addressee = new.blocker);
  return new;
end $$;
drop trigger if exists okay_blocks_cleanup on public.blocks;
create trigger okay_blocks_cleanup after insert on public.blocks
  for each row execute function okay_private.blocks_cleanup();

-- ---------------------------------------------------------------------------
-- RESTRICTIVE policies: AND-ed with every other policy on the table, so the
-- rules still hold if social.sql is re-run and re-creates the permissive ones.
-- ---------------------------------------------------------------------------
drop policy if exists "okay rules: dm insert" on public.messages;
create policy "okay rules: dm insert" on public.messages as restrictive for insert
  with check (okay_private.dm_rule(sender, recipient, body) is null);

drop policy if exists "okay rules: friend request insert" on public.friendships;
create policy "okay rules: friend request insert" on public.friendships as restrictive for insert
  with check (okay_private.friend_rule(requester, addressee) is null);

-- Activity feed: never shown across a block (follows.sql lets followers read it).
drop policy if exists "okay rules: activity select" on public.activity;
create policy "okay rules: activity select" on public.activity as restrictive for select
  using (actor = auth.uid() or not okay_private.is_blocked_between(actor, auth.uid()));

-- ---------------------------------------------------------------------------
-- Profiles: sensitive columns are readable only through profiles_visible.
-- Presence: last_seen, playing, session_code.   Private fields: bio, status,
-- favorite_game, location.   Everything else (username, display_name, avatar,
-- friend code, ...) stays public so names and avatars keep rendering.
-- Built from the columns that exist now, so re-run this file after another
-- script adds a profiles column.
-- ---------------------------------------------------------------------------
do $$
declare
  sel  text;
  pub  text;
  presence text[] := array['last_seen', 'playing', 'session_code'];
  private_ text[] := array['bio', 'status', 'favorite_game', 'location'];
begin
  select string_agg(
           case when column_name = any(presence)
                  then format('case when okay_private.can_see(p.id, auth.uid(), %L) then p.%I end as %I', 'presence', column_name, column_name)
                when column_name = any(private_)
                  then format('case when okay_private.can_see(p.id, auth.uid(), %L) then p.%I end as %I', 'profile', column_name, column_name)
                else format('p.%I', column_name) end,
           ', ' order by ordinal_position),
         string_agg(case when column_name = any(presence) or column_name = any(private_) then null
                         else format('%I', column_name) end, ', ' order by ordinal_position)
    into sel, pub
    from information_schema.columns
   where table_schema = 'public' and table_name = 'profiles';

  drop view if exists public.profiles_visible;
  execute 'create view public.profiles_visible as select ' || sel || ' from public.profiles p';
  grant select on public.profiles_visible to anon, authenticated, service_role;

  -- Lock the base table's sensitive columns away from the API roles.
  revoke select on public.profiles from anon, authenticated;
  execute 'grant select (' || pub || ') on public.profiles to anon, authenticated';
end $$;

-- Let PostgREST pick up the new tables / view / grants right away.
notify pgrst, 'reload schema';
