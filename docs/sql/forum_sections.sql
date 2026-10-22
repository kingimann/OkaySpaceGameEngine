-- OkaySpace forum sections (Reddit-style communities) + admin tools (Supabase).
-- Run once in the SQL editor AFTER social.sql and profile_extras.sql.
-- Idempotent: safe to re-run.
--
-- What it adds:
--   * forum_sections: anyone can create one (o/<slug>); the creator becomes its
--     admin automatically.
--   * forum_section_members: join/leave a section; role is member, moderator
--     or admin. Admins appoint moderators; moderators and admins moderate.
--   * forum_section_bans: section admins/mods ban people from posting there.
--   * forum_threads.section_id / pinned / locked: posts live in a section;
--     mods pin, lock (no new replies) or remove them. Mods can only change
--     those flags, never the author's text.
--   * Staff (profile_badges 'staff'): moderate every section, verify players
--     and award badges from the launcher's Staff tools. Make someone staff
--     here in the SQL editor:
--       insert into public.profile_badges (owner, badge)
--       select id, 'staff' from public.profiles where username = 'you';

-- ---------------------------------------------------------------------------
-- Staff helper
-- ---------------------------------------------------------------------------
create or replace function public.is_staff(p_user uuid) returns boolean
language sql stable security definer set search_path = public as $$
  select exists (select 1 from public.profile_badges where owner = p_user and badge = 'staff');
$$;

-- Staff may verify players (the trigger from profile_extras.sql otherwise
-- keeps `verified` unchanged for players).
create or replace function public.profiles_keep_verified() returns trigger
language plpgsql as $$
begin
  if coalesce(auth.role(), '') in ('authenticated', 'anon') and not public.is_staff(auth.uid()) then
    if tg_op = 'INSERT' then
      new.verified := false;
    elsif new.verified is distinct from old.verified then
      new.verified := old.verified;
    end if;
  end if;
  return new;
end $$;
drop policy if exists "staff verify players" on public.profiles;
create policy "staff verify players" on public.profiles for update using (public.is_staff(auth.uid()));

-- Staff award / remove badges (except making new staff: SQL editor only).
drop policy if exists "staff award badges"  on public.profile_badges;
drop policy if exists "staff remove badges" on public.profile_badges;
create policy "staff award badges"  on public.profile_badges for insert
  with check (public.is_staff(auth.uid()) and badge <> 'staff');
create policy "staff remove badges" on public.profile_badges for delete
  using (public.is_staff(auth.uid()) and badge <> 'staff');

-- ---------------------------------------------------------------------------
-- Sections
-- ---------------------------------------------------------------------------
create table if not exists public.forum_sections (
  id          bigint generated always as identity primary key,
  slug        text not null unique check (slug ~ '^[a-z0-9_]{3,24}$'),
  name        text not null check (char_length(name) between 3 and 40),
  description text check (char_length(description) <= 300),
  rules       text check (char_length(rules) <= 2000),
  color       int  not null default 0 check (color between 0 and 31),
  owner       uuid not null default auth.uid() references auth.users on delete cascade,
  created_at  timestamptz default now()
);

create table if not exists public.forum_section_members (
  section_id  bigint not null references public.forum_sections on delete cascade,
  user_id     uuid   not null default auth.uid() references auth.users on delete cascade,
  user_name   text,
  role        text   not null default 'member' check (role in ('member', 'moderator', 'admin')),
  joined_at   timestamptz default now(),
  primary key (section_id, user_id)
);

create table if not exists public.forum_section_bans (
  section_id  bigint not null references public.forum_sections on delete cascade,
  user_id     uuid   not null references auth.users on delete cascade,
  user_name   text,
  reason      text check (char_length(reason) <= 200),
  banned_by   uuid default auth.uid(),
  created_at  timestamptz default now(),
  primary key (section_id, user_id)
);

-- Role lookups for policies (security definer: no recursive RLS).
create or replace function public.forum_section_role(p_section bigint, p_user uuid) returns text
language sql stable security definer set search_path = public as $$
  select role from public.forum_section_members where section_id = p_section and user_id = p_user;
$$;
create or replace function public.is_section_admin(p_section bigint, p_user uuid) returns boolean
language sql stable security definer set search_path = public as $$
  select coalesce(public.forum_section_role(p_section, p_user) = 'admin', false) or public.is_staff(p_user);
$$;
create or replace function public.is_section_mod(p_section bigint, p_user uuid) returns boolean
language sql stable security definer set search_path = public as $$
  select coalesce(public.forum_section_role(p_section, p_user) in ('admin', 'moderator'), false) or public.is_staff(p_user);
$$;
create or replace function public.is_section_banned(p_section bigint, p_user uuid) returns boolean
language sql stable security definer set search_path = public as $$
  select p_section is not null and exists (select 1 from public.forum_section_bans where section_id = p_section and user_id = p_user);
$$;

-- The creator becomes the section's admin.
create or replace function public.forum_section_owner_admin() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  insert into public.forum_section_members (section_id, user_id, user_name, role)
  values (new.id, new.owner, (select username from public.profiles where id = new.owner), 'admin')
  on conflict (section_id, user_id) do update set role = 'admin';
  return new;
end $$;
drop trigger if exists forum_section_owner_admin on public.forum_sections;
create trigger forum_section_owner_admin after insert on public.forum_sections
  for each row execute function public.forum_section_owner_admin();

alter table public.forum_sections        enable row level security;
alter table public.forum_section_members enable row level security;
alter table public.forum_section_bans    enable row level security;

drop policy if exists "sections readable"     on public.forum_sections;
drop policy if exists "create section"        on public.forum_sections;
drop policy if exists "admins edit section"   on public.forum_sections;
drop policy if exists "owner deletes section" on public.forum_sections;
create policy "sections readable"     on public.forum_sections for select using (true);
create policy "create section"        on public.forum_sections for insert with check (auth.uid() = owner);
create policy "admins edit section"   on public.forum_sections for update using (public.is_section_admin(id, auth.uid()));
create policy "owner deletes section" on public.forum_sections for delete using (auth.uid() = owner or public.is_staff(auth.uid()));

drop policy if exists "members readable"     on public.forum_section_members;
drop policy if exists "join section"         on public.forum_section_members;
drop policy if exists "admins set roles"     on public.forum_section_members;
drop policy if exists "leave or remove"      on public.forum_section_members;
create policy "members readable" on public.forum_section_members for select using (true);
-- Join yourself as a plain member (and not while banned); admins may add anyone.
create policy "join section" on public.forum_section_members for insert with check (
  (auth.uid() = user_id and role = 'member' and not public.is_section_banned(section_id, auth.uid()))
  or public.is_section_admin(section_id, auth.uid()));
create policy "admins set roles" on public.forum_section_members for update using (public.is_section_admin(section_id, auth.uid()));
create policy "leave or remove"  on public.forum_section_members for delete using (
  auth.uid() = user_id or public.is_section_mod(section_id, auth.uid()));

drop policy if exists "bans readable" on public.forum_section_bans;
drop policy if exists "mods ban"      on public.forum_section_bans;
drop policy if exists "mods unban"    on public.forum_section_bans;
create policy "bans readable" on public.forum_section_bans for select using (true);
create policy "mods ban"      on public.forum_section_bans for insert with check (public.is_section_mod(section_id, auth.uid()));
create policy "mods unban"    on public.forum_section_bans for delete using (public.is_section_mod(section_id, auth.uid()));

-- ---------------------------------------------------------------------------
-- Threads in sections + moderation
-- ---------------------------------------------------------------------------
alter table public.forum_threads add column if not exists section_id bigint references public.forum_sections on delete cascade;
alter table public.forum_threads add column if not exists pinned     boolean not null default false;
alter table public.forum_threads add column if not exists locked     boolean not null default false;
create index if not exists forum_threads_section on public.forum_threads (section_id);

-- Member / post counts for the section list.
create or replace function public.forum_section_counts()
returns table (section_id bigint, members bigint, threads bigint)
language sql stable security definer set search_path = public as $$
  select s.id,
         (select count(*) from public.forum_section_members m where m.section_id = s.id),
         (select count(*) from public.forum_threads t where t.section_id = s.id)
  from public.forum_sections s;
$$;

drop policy if exists "start thread"        on public.forum_threads;
drop policy if exists "mods moderate thread" on public.forum_threads;
drop policy if exists "mods remove thread"  on public.forum_threads;
create policy "start thread" on public.forum_threads for insert with check (
  auth.uid() = author and not public.is_section_banned(section_id, auth.uid()));
create policy "mods moderate thread" on public.forum_threads for update using (
  section_id is not null and public.is_section_mod(section_id, auth.uid()) or public.is_staff(auth.uid()));
create policy "mods remove thread" on public.forum_threads for delete using (
  section_id is not null and public.is_section_mod(section_id, auth.uid()) or public.is_staff(auth.uid()));

-- Moderators may flip pinned/locked only; the author's words stay theirs.
create or replace function public.forum_threads_mod_only() returns trigger
language plpgsql as $$
begin
  if auth.uid() is distinct from old.author then
    new.title := old.title; new.body := old.body; new.category := old.category;
    new.author := old.author; new.author_name := old.author_name;
    new.section_id := old.section_id; new.created_at := old.created_at;
  end if;
  return new;
end $$;
drop trigger if exists forum_threads_mod_only on public.forum_threads;
create trigger forum_threads_mod_only before update on public.forum_threads
  for each row execute function public.forum_threads_mod_only();

-- Replies: not in locked threads, not while banned from the section.
drop policy if exists "write post"       on public.forum_posts;
drop policy if exists "mods remove post" on public.forum_posts;
create policy "write post" on public.forum_posts for insert with check (
  auth.uid() = author and not exists (
    select 1 from public.forum_threads t
    where t.id = thread_id and (t.locked or public.is_section_banned(t.section_id, auth.uid()))));
create policy "mods remove post" on public.forum_posts for delete using (
  exists (select 1 from public.forum_threads t
          where t.id = thread_id and t.section_id is not null and public.is_section_mod(t.section_id, auth.uid()))
  or public.is_staff(auth.uid()));
