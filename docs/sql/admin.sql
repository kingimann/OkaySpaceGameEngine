-- OkaySpace admin tools (Supabase): site roles, site-wide bans, audit log.
-- Run once in the SQL editor AFTER social.sql, profile_extras.sql,
-- forum_sections.sql (and follows.sql / group_chats.sql if you use them).
-- Idempotent: safe to re-run.
--
-- Roles are profile badges, highest first:
--   admin      everything: assign any role (incl. staff / admin), ban staff
--   staff      assign moderator + cosmetic badges, verify players, ban
--              moderators and players, moderate everything
--   moderator  moderate every forum section, ban players
-- You can only act on people ranked below you, and only admins hand out
-- staff / admin. Make the first admin here in the SQL editor:
--   insert into public.profile_badges (owner, badge)
--   select id, 'admin' from public.profiles where username = 'you'
--   on conflict do nothing;
--
-- Marketplace files must be uploaded to OkaySpace's own storage: no outside
-- download links (existing link-only listings are sent back to their authors).
--
-- Marketplace review: new listings wait as "pending" until a moderator approves
-- them (rejected ones stay visible only to their author, with the reason).
-- Changing an approved listing's file, title or description sends it back to
-- review. Authors and mods can lock an item's reviews.
--
-- Forums: global moderators moderate every section; staff and admins can also
-- change or remove section admins. Locked threads take no replies.
--
-- Site bans block a player from posting anywhere (DMs, groups, servers,
-- forums, marketplace, reviews, friend requests, follows, activity) until they
-- expire (or forever). The launcher shows the banned player why and until when.
-- Every role change, verification and ban is written to admin_log.

-- ---------------------------------------------------------------------------
-- Roles
-- ---------------------------------------------------------------------------
do $$
declare c record;
begin
  for c in select conname from pg_constraint
           where conrelid = 'public.profile_badges'::regclass and contype = 'c' loop
    execute format('alter table public.profile_badges drop constraint %I', c.conname);
  end loop;
end $$;
alter table public.profile_badges add constraint profile_badges_badge_check
  check (badge in ('admin', 'staff', 'moderator', 'developer', 'beta', 'partner'));

-- 3 admin, 2 staff, 1 moderator, 0 everyone else.
create or replace function public.role_rank(p_user uuid) returns int
language sql stable security definer set search_path = public as $$
  select coalesce(max(case badge when 'admin' then 3 when 'staff' then 2 when 'moderator' then 1 else 0 end), 0)
  from public.profile_badges where owner = p_user;
$$;
create or replace function public.is_admin(p_user uuid) returns boolean
language sql stable security definer set search_path = public as $$ select public.role_rank(p_user) >= 3; $$;
create or replace function public.is_staff(p_user uuid) returns boolean
language sql stable security definer set search_path = public as $$ select public.role_rank(p_user) >= 2; $$;
create or replace function public.is_global_mod(p_user uuid) returns boolean
language sql stable security definer set search_path = public as $$ select public.role_rank(p_user) >= 1; $$;
create or replace function public.my_role_rank() returns int
language sql stable security definer set search_path = public as $$ select public.role_rank(auth.uid()); $$;

-- Global moderators moderate every forum section.
create or replace function public.is_section_mod(p_section bigint, p_user uuid) returns boolean
language sql stable security definer set search_path = public as $$
  select coalesce(public.forum_section_role(p_section, p_user) in ('admin', 'moderator'), false) or public.is_global_mod(p_user);
$$;

-- Who may hand out which badge: admins anything; staff moderator + cosmetic.
create or replace function public.can_manage_badge(p_actor uuid, p_badge text) returns boolean
language sql stable security definer set search_path = public as $$
  select case
    when public.role_rank(p_actor) >= 3 then true
    when public.role_rank(p_actor) >= 2 then p_badge in ('moderator', 'developer', 'beta', 'partner')
    else false end;
$$;
drop policy if exists "staff award badges"  on public.profile_badges;
drop policy if exists "staff remove badges" on public.profile_badges;
create policy "staff award badges"  on public.profile_badges for insert
  with check (public.can_manage_badge(auth.uid(), badge));
create policy "staff remove badges" on public.profile_badges for delete
  using (public.can_manage_badge(auth.uid(), badge) and (owner <> auth.uid() or badge not in ('admin')));

-- ---------------------------------------------------------------------------
-- Audit log
-- ---------------------------------------------------------------------------
create table if not exists public.admin_log (
  id          bigint generated always as identity primary key,
  actor       uuid,
  actor_name  text,
  action      text not null,          -- ban | unban | role_add | role_remove | verify | unverify | approve | reject | note
  target      uuid,
  target_name text,
  detail      text check (char_length(detail) <= 500),
  created_at  timestamptz default now()
);
create index if not exists admin_log_created on public.admin_log (created_at desc);
alter table public.admin_log enable row level security;
drop policy if exists "mods read log"  on public.admin_log;
drop policy if exists "mods write log" on public.admin_log;
create policy "mods read log"  on public.admin_log for select using (public.is_global_mod(auth.uid()));
create policy "mods write log" on public.admin_log for insert
  with check (public.is_global_mod(auth.uid()) and actor = auth.uid() and action = 'note');

create or replace function public.admin_log_write(p_action text, p_target uuid, p_detail text) returns void
language plpgsql security definer set search_path = public as $$
begin
  insert into public.admin_log (actor, actor_name, action, target, target_name, detail)
  values (auth.uid(), (select username from public.profiles where id = auth.uid()), p_action, p_target,
          (select username from public.profiles where id = p_target), left(p_detail, 500));
end $$;
revoke execute on function public.admin_log_write(text, uuid, text) from public, anon, authenticated;

create or replace function public.profile_badges_log() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if auth.uid() is null then return coalesce(new, old); end if;   -- SQL editor: not logged
  if tg_op = 'INSERT' then perform public.admin_log_write('role_add', new.owner, new.badge);
  else perform public.admin_log_write('role_remove', old.owner, old.badge); end if;
  return coalesce(new, old);
end $$;
drop trigger if exists profile_badges_log on public.profile_badges;
create trigger profile_badges_log after insert or delete on public.profile_badges
  for each row execute function public.profile_badges_log();

create or replace function public.profiles_log_verified() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if auth.uid() is not null and new.verified is distinct from old.verified then
    perform public.admin_log_write(case when new.verified then 'verify' else 'unverify' end, new.id, null);
  end if;
  return new;
end $$;
drop trigger if exists profiles_log_verified on public.profiles;
create trigger profiles_log_verified after update on public.profiles
  for each row execute function public.profiles_log_verified();

-- ---------------------------------------------------------------------------
-- Site bans
-- ---------------------------------------------------------------------------
create table if not exists public.site_bans (
  user_id    uuid primary key references auth.users on delete cascade,
  reason     text check (char_length(reason) <= 300),
  until      timestamptz,             -- null = permanent
  banned_by  uuid default auth.uid(),
  created_at timestamptz default now()
);
alter table public.site_bans enable row level security;

create or replace function public.is_site_banned(p_user uuid) returns boolean
language sql stable security definer set search_path = public as $$
  select exists (select 1 from public.site_bans b
                 where b.user_id = p_user and (b.until is null or b.until > now()));
$$;

drop policy if exists "see bans"    on public.site_bans;
drop policy if exists "ban players" on public.site_bans;
drop policy if exists "edit bans"   on public.site_bans;
drop policy if exists "unban"       on public.site_bans;
-- Mods see every ban; a banned player sees their own (the launcher shows why).
create policy "see bans" on public.site_bans for select
  using (user_id = auth.uid() or public.is_global_mod(auth.uid()));
-- Only people ranked above the target, never yourself.
create policy "ban players" on public.site_bans for insert with check (
  public.is_global_mod(auth.uid()) and user_id <> auth.uid()
  and public.role_rank(auth.uid()) > public.role_rank(user_id));
create policy "edit bans" on public.site_bans for update using (
  public.is_global_mod(auth.uid()) and public.role_rank(auth.uid()) > public.role_rank(user_id));
create policy "unban" on public.site_bans for delete using (public.is_global_mod(auth.uid()));

create or replace function public.site_bans_log() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if auth.uid() is null then return coalesce(new, old); end if;
  if tg_op = 'DELETE' then perform public.admin_log_write('unban', old.user_id, null);
  else perform public.admin_log_write('ban', new.user_id,
         coalesce(new.reason, '') || case when new.until is null then ' (permanent)'
                                          else ' (until ' || to_char(new.until at time zone 'utc', 'YYYY-MM-DD HH24:MI') || ' UTC)' end);
  end if;
  return coalesce(new, old);
end $$;
drop trigger if exists site_bans_log on public.site_bans;
create trigger site_bans_log after insert or update or delete on public.site_bans
  for each row execute function public.site_bans_log();

-- Enforce: a RESTRICTIVE insert policy on everything a player can post to
-- (ANDed with the table's normal policies).
do $$
declare t text;
begin
  foreach t in array array['messages', 'message_reactions', 'group_messages', 'channel_messages',
                           'forum_threads', 'forum_posts', 'forum_votes', 'workshop_items', 'workshop_reviews',
                           'friendships', 'follows', 'activity', 'servers', 'forum_sections'] loop
    if to_regclass('public.' || t) is not null then
      execute format('drop policy if exists "not site banned" on public.%I', t);
      execute format('create policy "not site banned" on public.%I as restrictive for insert to authenticated
                      with check (not public.is_site_banned(auth.uid()))', t);
    end if;
  end loop;
end $$;

-- Staff clean-up powers over content.
drop policy if exists "staff remove item" on public.workshop_items;
create policy "staff remove item" on public.workshop_items for delete using (public.is_staff(auth.uid()));
drop policy if exists "staff remove review" on public.workshop_reviews;
create policy "staff remove review" on public.workshop_reviews for delete using (public.is_global_mod(auth.uid()));

-- ---------------------------------------------------------------------------
-- Marketplace review
-- ---------------------------------------------------------------------------
alter table public.workshop_items add column if not exists status text not null default 'approved';
alter table public.workshop_items alter column status set default 'pending';   -- existing listings stay approved
alter table public.workshop_items add column if not exists review_note    text;
alter table public.workshop_items add column if not exists reviewed_by    uuid;
alter table public.workshop_items add column if not exists reviewed_at    timestamptz;
alter table public.workshop_items add column if not exists reviews_locked boolean not null default false;
alter table public.workshop_items add column if not exists featured       boolean not null default false;
do $$ begin
  if not exists (select 1 from pg_constraint where conname = 'workshop_items_status_check') then
    alter table public.workshop_items add constraint workshop_items_status_check check (status in ('pending', 'approved', 'rejected'));
  end if;
end $$;
create index if not exists workshop_items_status on public.workshop_items (status);

create or replace function public.workshop_items_guard() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  -- Files must be uploaded to OkaySpace (the 'workshop' bucket, in the
  -- uploader's own folder): no outside links on the marketplace.
  if auth.uid() is not null and (tg_op = 'INSERT' or new.storage_path is distinct from old.storage_path) then
    if new.storage_path is null
       or position('/storage/v1/object/public/workshop/' in new.storage_path) = 0
       or not exists (select 1 from storage.objects o
                      where o.bucket_id = 'workshop'
                        and o.name like auth.uid()::text || '/%'
                        and right(new.storage_path, length(o.name) + 1) = '/' || o.name) then
      raise exception 'Upload the file to OkaySpace: outside links aren''t allowed on the marketplace'
        using errcode = 'check_violation';
    end if;
  end if;
  if auth.uid() is null or public.is_global_mod(auth.uid()) then
    if tg_op = 'UPDATE' and new.status is distinct from old.status then
      new.reviewed_by := auth.uid(); new.reviewed_at := now();
      if auth.uid() is not null then
        perform public.admin_log_write(case new.status when 'approved' then 'approve' when 'rejected' then 'reject' else 'requeue' end,
                                       new.author, new.title || coalesce(': ' || nullif(new.review_note, ''), ''));
      end if;
    end if;
    return new;
  end if;
  if tg_op = 'INSERT' then
    new.status := 'pending'; new.review_note := null; new.reviewed_by := null; new.reviewed_at := null; new.featured := false;
  else
    new.featured := old.featured; new.reviewed_by := old.reviewed_by; new.reviewed_at := old.reviewed_at;
    if new.storage_path is distinct from old.storage_path or new.title is distinct from old.title
       or new.description is distinct from old.description then
      new.status := 'pending'; new.review_note := null;   -- changed content: review again
    else
      new.status := old.status; new.review_note := old.review_note;
    end if;
  end if;
  return new;
end $$;
drop trigger if exists workshop_items_guard on public.workshop_items;
create trigger workshop_items_guard before insert or update on public.workshop_items
  for each row execute function public.workshop_items_guard();

-- Listings that still point outside OkaySpace go back to their authors.
update public.workshop_items
   set status = 'rejected',
       review_note = 'Marketplace files must be uploaded to OkaySpace now. Edit the listing and upload the file.'
 where status <> 'rejected'
   and (storage_path is null or position('/storage/v1/object/public/workshop/' in storage_path) = 0);

drop policy if exists "workshop is readable" on public.workshop_items;
create policy "workshop is readable" on public.workshop_items for select using (
  status = 'approved' or author = auth.uid() or public.is_global_mod(auth.uid()));
drop policy if exists "mods review items" on public.workshop_items;
create policy "mods review items" on public.workshop_items for update using (public.is_global_mod(auth.uid()));

-- No new reviews on locked or unapproved items.
drop policy if exists "reviews open" on public.workshop_reviews;
create policy "reviews open" on public.workshop_reviews as restrictive for insert to authenticated with check (
  exists (select 1 from public.workshop_items i where i.id = item_id and i.status = 'approved' and not i.reviews_locked));

-- ---------------------------------------------------------------------------
-- Forums: section moderators can't remove the section's admins
-- ---------------------------------------------------------------------------
drop policy if exists "leave or remove" on public.forum_section_members;
create policy "leave or remove" on public.forum_section_members for delete using (
  auth.uid() = user_id
  or public.is_section_admin(section_id, auth.uid())
  or (public.is_section_mod(section_id, auth.uid()) and role = 'member'));

-- ---------------------------------------------------------------------------
-- Dashboard numbers (mods only)
-- ---------------------------------------------------------------------------
drop function if exists public.admin_stats();
create or replace function public.admin_stats()
returns table (players bigint, new_week bigint, online_now bigint, banned bigint, staff bigint,
               threads_week bigint, items bigint, pending bigint)
language plpgsql stable security definer set search_path = public as $$
begin
  if not public.is_global_mod(auth.uid()) then return; end if;
  return query select
    (select count(*) from public.profiles),
    (select count(*) from public.profiles where created_at > now() - interval '7 days'),
    (select count(*) from public.profiles where last_seen > now() - interval '3 minutes'),
    (select count(*) from public.site_bans where until is null or until > now()),
    (select count(distinct owner) from public.profile_badges where badge in ('admin', 'staff', 'moderator')),
    (select count(*) from public.forum_threads where created_at > now() - interval '7 days'),
    (select count(*) from public.workshop_items),
    (select count(*) from public.workshop_items where status = 'pending');
end $$;
