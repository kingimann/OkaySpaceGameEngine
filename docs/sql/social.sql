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
  id         uuid primary key default auth.uid() references auth.users on delete cascade,
  username   text unique not null,
  last_seen  timestamptz,
  created_at timestamptz default now()
);
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

-- Storage bucket for workshop uploads (public read). If the dashboard is
-- easier, create a public bucket named 'workshop' there instead.
insert into storage.buckets (id, name, public) values ('workshop','workshop', true)
  on conflict (id) do nothing;
drop policy if exists "workshop files readable" on storage.objects;
drop policy if exists "upload own workshop file" on storage.objects;
create policy "workshop files readable" on storage.objects for select
  using (bucket_id = 'workshop');
create policy "upload own workshop file" on storage.objects for insert
  with check (bucket_id = 'workshop' and auth.role() = 'authenticated');
