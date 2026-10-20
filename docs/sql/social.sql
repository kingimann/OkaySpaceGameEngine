-- OkaySpace social backend (Supabase).
-- Run this once in your Supabase project's SQL editor. It creates the tables,
-- row-level-security policies and the signup trigger that the launcher's
-- Friends and Workshop/Marketplace tabs use. Nothing in the app requires this
-- until you run it; achievements work locally without any of it.
--
-- Columns that identify "me" (profiles.id, friendships.requester,
-- workshop_items.author) DEFAULT to auth.uid(), so the client never sends its
-- own user id — Postgres fills it from the signed-in JWT, and RLS enforces it.

-- ---------------------------------------------------------------------------
-- profiles: a public username <-> auth user id map (Supabase auth hides the
-- auth.users table from the API, so we mirror the bits we can show publicly).
-- ---------------------------------------------------------------------------
create table if not exists public.profiles (
  id          uuid primary key default auth.uid() references auth.users on delete cascade,
  username    text unique not null,
  last_seen   timestamptz,
  friend_code text unique,              -- short shareable code (set by the client)
  playing     text,                     -- what I'm doing right now (presence)
  session_code text,                    -- relay room code when hosting a joinable game
  created_at  timestamptz default now()
);
-- Add the presence/code columns to a profiles table created before this version.
alter table public.profiles add column if not exists friend_code  text;
alter table public.profiles add column if not exists playing      text;
alter table public.profiles add column if not exists session_code text;
alter table public.profiles add column if not exists display_name text;  -- friendly name (vs the unique username handle)
alter table public.profiles add column if not exists avatar_url   text;  -- public URL of the player's avatar image
alter table public.profiles add column if not exists bio          text;  -- short "about me" shown on the profile card
-- Steam-style profile customization.
alter table public.profiles add column if not exists accent        int  default 0;  -- profile theme-color index (client palette)
alter table public.profiles add column if not exists status        text;  -- custom status / tagline under the name
alter table public.profiles add column if not exists favorite_game text;  -- a game the player wants to show off
alter table public.profiles add column if not exists location      text;  -- optional free-form location
do $$ begin
  if not exists (select 1 from pg_constraint where conname = 'profiles_friend_code_key') then
    alter table public.profiles add constraint profiles_friend_code_key unique (friend_code);
  end if;
end $$;
alter table public.profiles enable row level security;
drop policy if exists "profiles are readable"      on public.profiles;
drop policy if exists "insert own profile"         on public.profiles;
drop policy if exists "update own profile"         on public.profiles;
create policy "profiles are readable" on public.profiles for select using (true);
create policy "insert own profile"    on public.profiles for insert with check (auth.uid() = id);
create policy "update own profile"    on public.profiles for update using (auth.uid() = id);

-- Auto-create a profile row on signup, taking the username from the signup
-- metadata (the launcher/editor send it), else the email local-part.
create or replace function public.handle_new_user() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  insert into public.profiles (id, username)
  values (new.id, coalesce(nullif(new.raw_user_meta_data->>'username',''),
                           split_part(new.email, '@', 1)))
  on conflict (id) do nothing;
  return new;
end; $$;
drop trigger if exists on_auth_user_created on auth.users;
create trigger on_auth_user_created
  after insert on auth.users for each row execute procedure public.handle_new_user();

-- ---------------------------------------------------------------------------
-- friendships: one row per request/relationship (requester -> addressee).
-- ---------------------------------------------------------------------------
create table if not exists public.friendships (
  id         bigint generated always as identity primary key,
  requester  uuid not null default auth.uid() references auth.users on delete cascade,
  addressee  uuid not null references auth.users on delete cascade,
  status     text not null default 'pending',        -- pending | accepted
  created_at timestamptz default now(),
  unique (requester, addressee)
);
alter table public.friendships enable row level security;
drop policy if exists "see own friendships"   on public.friendships;
drop policy if exists "send friend request"   on public.friendships;
drop policy if exists "respond to request"    on public.friendships;
drop policy if exists "remove friendship"     on public.friendships;
create policy "see own friendships" on public.friendships for select
  using (auth.uid() = requester or auth.uid() = addressee);
create policy "send friend request" on public.friendships for insert
  with check (auth.uid() = requester);
create policy "respond to request"  on public.friendships for update
  using (auth.uid() = addressee or auth.uid() = requester);
create policy "remove friendship"   on public.friendships for delete
  using (auth.uid() = requester or auth.uid() = addressee);

-- ---------------------------------------------------------------------------
-- activity: a lightweight feed (came online, started a game, unlocked an
-- achievement, published to the marketplace). Each player writes their own
-- rows; friends can read them. Kept small — the client only reads the most
-- recent handful.
-- ---------------------------------------------------------------------------
create table if not exists public.activity (
  id         bigint generated always as identity primary key,
  actor      uuid not null default auth.uid() references auth.users on delete cascade,
  actor_name text,
  kind       text not null,              -- online | playing | achievement | publish
  detail     text,
  created_at timestamptz default now()
);
alter table public.activity enable row level security;
drop policy if exists "see friends activity" on public.activity;
drop policy if exists "post own activity"    on public.activity;
drop policy if exists "prune own activity"   on public.activity;
-- Readable when the actor is me or an accepted friend of mine.
create policy "see friends activity" on public.activity for select using (
  actor = auth.uid() or actor in (
    select case when requester = auth.uid() then addressee else requester end
    from public.friendships
    where status = 'accepted' and (requester = auth.uid() or addressee = auth.uid())
  )
);
create policy "post own activity"  on public.activity for insert with check (auth.uid() = actor);
create policy "prune own activity" on public.activity for delete using (auth.uid() = actor);

-- ---------------------------------------------------------------------------
-- messages: 1:1 direct messages between players. RLS limits reads to the two
-- parties. (Placed here, but the original file already had a prune policy
-- below for activity — kept idempotent with drop-if-exists.)
-- ---------------------------------------------------------------------------
create table if not exists public.messages (
  id         bigint generated always as identity primary key,
  sender     uuid not null default auth.uid() references auth.users on delete cascade,
  recipient  uuid not null references auth.users on delete cascade,
  body       text not null,
  created_at timestamptz default now()
);
alter table public.messages enable row level security;
drop policy if exists "see own messages"  on public.messages;
drop policy if exists "send own messages" on public.messages;
create policy "see own messages"  on public.messages for select
  using (auth.uid() = sender or auth.uid() = recipient);
create policy "send own messages" on public.messages for insert with check (auth.uid() = sender);

-- ---------------------------------------------------------------------------
-- forums: public discussion threads + posts (the Community "Forums" section).
-- ---------------------------------------------------------------------------
create table if not exists public.forum_threads (
  id          bigint generated always as identity primary key,
  author      uuid not null default auth.uid() references auth.users on delete cascade,
  author_name text,
  title       text not null,
  created_at  timestamptz default now()
);
alter table public.forum_threads enable row level security;
drop policy if exists "threads readable" on public.forum_threads;
drop policy if exists "start thread"     on public.forum_threads;
drop policy if exists "delete own thread" on public.forum_threads;
create policy "threads readable"  on public.forum_threads for select using (true);
create policy "start thread"      on public.forum_threads for insert with check (auth.uid() = author);
create policy "delete own thread" on public.forum_threads for delete using (auth.uid() = author);

create table if not exists public.forum_posts (
  id          bigint generated always as identity primary key,
  thread_id   bigint not null references public.forum_threads on delete cascade,
  author      uuid not null default auth.uid() references auth.users on delete cascade,
  author_name text,
  body        text not null,
  created_at  timestamptz default now()
);
alter table public.forum_posts enable row level security;
drop policy if exists "posts readable" on public.forum_posts;
drop policy if exists "write post"     on public.forum_posts;
drop policy if exists "delete own post" on public.forum_posts;
create policy "posts readable"  on public.forum_posts for select using (true);
create policy "write post"      on public.forum_posts for insert with check (auth.uid() = author);
create policy "delete own post" on public.forum_posts for delete using (auth.uid() = author);

-- ---------------------------------------------------------------------------
-- workshop_items: shared games / scenes / templates (the Marketplace tab).
-- The actual files live in a public Storage bucket named 'workshop'; this row
-- holds the metadata + the bucket path.
-- ---------------------------------------------------------------------------
create table if not exists public.workshop_items (
  id           bigint generated always as identity primary key,
  author       uuid not null default auth.uid() references auth.users on delete cascade,
  author_name  text,
  title        text not null,
  description  text,
  kind         text default 'game',        -- game | scene | template
  storage_path text,                        -- path within the 'workshop' bucket
  downloads    bigint default 0,
  created_at   timestamptz default now()
);
alter table public.workshop_items enable row level security;
drop policy if exists "workshop is readable" on public.workshop_items;
drop policy if exists "publish own item"     on public.workshop_items;
drop policy if exists "edit own item"        on public.workshop_items;
drop policy if exists "delete own item"      on public.workshop_items;
create policy "workshop is readable" on public.workshop_items for select using (true);
create policy "publish own item"     on public.workshop_items for insert with check (auth.uid() = author);
create policy "edit own item"        on public.workshop_items for update using (auth.uid() = author);
create policy "delete own item"      on public.workshop_items for delete using (auth.uid() = author);

-- ---------------------------------------------------------------------------
-- Workshop reviews: a 1-5 star rating + optional text, one per player per item.
-- Ratings are public (anyone can read + aggregate them client-side); a player
-- may write/edit/delete only their own. The unique (item,reviewer) key means a
-- re-review replaces the old one (the launcher deletes-then-inserts). Kept in a
-- separate table so the existing workshop_items select can never break if this
-- migration hasn't been run — the launcher just shows "no ratings yet".
-- ---------------------------------------------------------------------------
create table if not exists public.workshop_reviews (
  id            bigint generated by default as identity primary key,
  item_id       bigint not null references public.workshop_items(id) on delete cascade,
  reviewer      uuid not null default auth.uid() references auth.users(id) on delete cascade,
  reviewer_name text,
  rating        int  not null check (rating between 1 and 5),
  body          text default '',
  created_at    timestamptz default now(),
  unique (item_id, reviewer)
);
alter table public.workshop_reviews enable row level security;
drop policy if exists "reviews are readable" on public.workshop_reviews;
drop policy if exists "write own review"     on public.workshop_reviews;
drop policy if exists "edit own review"       on public.workshop_reviews;
drop policy if exists "delete own review"     on public.workshop_reviews;
create policy "reviews are readable" on public.workshop_reviews for select using (true);
create policy "write own review"     on public.workshop_reviews for insert with check (auth.uid() = reviewer);
create policy "edit own review"      on public.workshop_reviews for update using (auth.uid() = reviewer);
create policy "delete own review"    on public.workshop_reviews for delete using (auth.uid() = reviewer);

-- Storage bucket for workshop uploads (public read). Guarded with a DO block so
-- this migration still succeeds on a freshly-created project whose Storage
-- schema hasn't finished provisioning yet (storage.buckets not created). Re-run
-- this file once the project is fully up, or just create a public bucket named
-- 'workshop' in the dashboard. The current link-based Marketplace stores a URL
-- per item and does not require this bucket.
do $$
begin
  if to_regclass('storage.buckets') is not null then
    insert into storage.buckets (id, name, public) values ('workshop','workshop', true)
      on conflict (id) do nothing;
    drop policy if exists "workshop files readable" on storage.objects;
    drop policy if exists "upload own workshop file" on storage.objects;
    create policy "workshop files readable" on storage.objects for select
      using (bucket_id = 'workshop');
    create policy "upload own workshop file" on storage.objects for insert
      with check (bucket_id = 'workshop' and auth.role() = 'authenticated');
  end if;
end $$;

-- ---------------------------------------------------------------------------
-- Servers (Discord-style): a server has members, channels, and per-channel
-- messages. Membership drives all reads. To avoid RLS self-recursion we gate
-- reads on a SECURITY DEFINER helper, and do create/join via SECURITY DEFINER
-- RPCs (a non-member can't SELECT a server to find it by code otherwise).
-- ---------------------------------------------------------------------------
create table if not exists public.servers (
  id          bigint generated always as identity primary key,
  name        text not null,
  owner       uuid not null default auth.uid() references auth.users on delete cascade,
  invite_code text unique,
  created_at  timestamptz default now()
);
create table if not exists public.server_members (
  server_id  bigint not null references public.servers on delete cascade,
  member     uuid not null default auth.uid() references auth.users on delete cascade,
  role       text default 'member',
  created_at timestamptz default now(),
  primary key (server_id, member)
);
create table if not exists public.channels (
  id         bigint generated always as identity primary key,
  server_id  bigint not null references public.servers on delete cascade,
  name       text not null,
  position   int default 0,
  created_at timestamptz default now()
);
create table if not exists public.channel_messages (
  id          bigint generated always as identity primary key,
  channel_id  bigint not null references public.channels on delete cascade,
  author      uuid not null default auth.uid() references auth.users on delete cascade,
  author_name text,
  body        text not null,
  created_at  timestamptz default now()
);

-- SECURITY DEFINER membership checks (bypass RLS -> no recursion).
create or replace function public.is_server_member(sid bigint) returns boolean
  language sql security definer stable set search_path = public as $$
    select exists (select 1 from public.server_members m
                   where m.server_id = sid and m.member = auth.uid());
  $$;
create or replace function public.is_channel_member(cid bigint) returns boolean
  language sql security definer stable set search_path = public as $$
    select public.is_server_member((select server_id from public.channels where id = cid));
  $$;

alter table public.servers         enable row level security;
alter table public.server_members  enable row level security;
alter table public.channels        enable row level security;
alter table public.channel_messages enable row level security;

drop policy if exists "see my servers"      on public.servers;
drop policy if exists "owner edits server"  on public.servers;
create policy "see my servers"     on public.servers for select
  using (owner = auth.uid() or public.is_server_member(id));
create policy "owner edits server" on public.servers for update using (owner = auth.uid());

drop policy if exists "see server members" on public.server_members;
drop policy if exists "join self"          on public.server_members;
drop policy if exists "leave or kick"      on public.server_members;
create policy "see server members" on public.server_members for select
  using (member = auth.uid() or public.is_server_member(server_id));
create policy "join self"     on public.server_members for insert with check (member = auth.uid());
create policy "leave or kick" on public.server_members for delete
  using (member = auth.uid() or exists (select 1 from public.servers s where s.id = server_id and s.owner = auth.uid()));

drop policy if exists "see channels"   on public.channels;
drop policy if exists "owner channels" on public.channels;
create policy "see channels"   on public.channels for select using (public.is_server_member(server_id));
create policy "owner channels" on public.channels for insert
  with check (exists (select 1 from public.servers s where s.id = server_id and s.owner = auth.uid()));

drop policy if exists "read channel messages" on public.channel_messages;
drop policy if exists "post channel messages" on public.channel_messages;
create policy "read channel messages" on public.channel_messages for select
  using (public.is_channel_member(channel_id));
create policy "post channel messages" on public.channel_messages for insert
  with check (author = auth.uid() and public.is_channel_member(channel_id));

-- Create a server (owner joins + a #general channel), returning its id + code.
create or replace function public.create_server(p_name text)
  returns public.servers language plpgsql security definer set search_path = public as $$
  declare s public.servers;
  begin
    insert into public.servers (name, owner, invite_code)
      values (p_name, auth.uid(), substr(md5(random()::text), 1, 8))
      returning * into s;
    insert into public.server_members (server_id, member, role) values (s.id, auth.uid(), 'owner');
    insert into public.channels (server_id, name, position) values (s.id, 'general', 0);
    return s;
  end; $$;

-- Join a server by its invite code; returns the server (null if the code is bad).
create or replace function public.join_server(p_code text)
  returns public.servers language plpgsql security definer set search_path = public as $$
  declare s public.servers;
  begin
    select * into s from public.servers where invite_code = p_code;
    if s.id is null then return null; end if;
    insert into public.server_members (server_id, member) values (s.id, auth.uid())
      on conflict do nothing;
    return s;
  end; $$;

grant execute on function public.create_server(text) to anon, authenticated;
grant execute on function public.join_server(text)   to anon, authenticated;
grant execute on function public.is_server_member(bigint)  to anon, authenticated;
grant execute on function public.is_channel_member(bigint) to anon, authenticated;

-- ---------------------------------------------------------------------------
-- email_for_login: lets players sign in with their username instead of email.
-- Returns the account's email ONLY when the password is correct (verified with
-- pgcrypto crypt against the stored bcrypt hash), so it can't enumerate emails.
-- The launcher calls this, then does the normal Supabase password sign-in.
-- ---------------------------------------------------------------------------
create or replace function public.email_for_login(p_username text, p_password text)
  returns text language plpgsql security definer
  set search_path = public, auth, extensions as $fn$
  declare e text; pw text;
  begin
    select u.email, u.encrypted_password into e, pw
      from auth.users u join public.profiles p on p.id = u.id
      where lower(p.username) = lower(p_username) limit 1;
    if e is null or pw is null then return null; end if;
    if pw = crypt(p_password, pw) then return e; else return null; end if;
  end; $fn$;
revoke all on function public.email_for_login(text, text) from public;
grant execute on function public.email_for_login(text, text) to anon, authenticated;
