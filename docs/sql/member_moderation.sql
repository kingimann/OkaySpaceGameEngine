-- OkaySpace member moderation (Supabase): kick / ban / unban / admin role for
-- community servers and group chats.
-- Run once in the Supabase SQL editor AFTER social.sql and group_chats.sql
-- (and after admin.sql if you have it: then kicks, bans, unbans and role changes
-- are also written to its admin_log). Idempotent: re-running is safe.
--
-- Roles:   owner (servers.owner / group_convos.owner)  >  admin  >  member
--   * only owner and admin may kick, ban or unban;
--   * nobody can act on an equal or higher role, or on themselves;
--   * an owner can never be removed or banned by anyone else;
--   * only the owner changes roles (member <-> admin), through set_member_role.
-- A banned player is removed from the server / group and cannot rejoin (invite
-- code or direct insert), be added to a group, read, or post until the ban ends
-- (until = null means permanent) or an owner/admin lifts it.
-- The launcher calls the four RPCs below; plain table writes are held to the
-- same rules by row-level security, so there is no way around them.

-- ---------------------------------------------------------------------------
-- Roles on group members (servers already have server_members.role).
-- ---------------------------------------------------------------------------
alter table public.group_members add column if not exists role text default 'member';
update public.group_members gm set role = 'owner'
  from public.group_convos c
  where c.id = gm.convo_id and c.owner = gm.member and gm.role is distinct from 'owner';
do $$ begin
  if not exists (select 1 from pg_constraint where conname = 'server_members_role_check') then
    alter table public.server_members add constraint server_members_role_check
      check (role is null or role in ('owner','admin','member')) not valid;
  end if;
  if not exists (select 1 from pg_constraint where conname = 'group_members_role_check') then
    alter table public.group_members add constraint group_members_role_check
      check (role is null or role in ('owner','admin','member')) not valid;
  end if;
end $$;

-- ---------------------------------------------------------------------------
-- Bans: one row per (server|group, user). until null = permanent.
-- ---------------------------------------------------------------------------
create table if not exists public.community_bans (
  scope        text   not null check (scope in ('server','group')),
  container_id bigint not null,
  user_id      uuid   not null references auth.users on delete cascade,
  reason       text check (char_length(reason) <= 300),
  until        timestamptz,
  banned_by    uuid default auth.uid(),
  created_at   timestamptz default now(),
  primary key (scope, container_id, user_id)
);
create index if not exists community_bans_user on public.community_bans (user_id);
alter table public.community_bans enable row level security;

create or replace function public.is_community_banned(p_scope text, p_cid bigint, p_uid uuid) returns boolean
language sql stable security definer set search_path = public as $$
  select exists (select 1 from public.community_bans b
                 where b.scope = p_scope and b.container_id = p_cid and b.user_id = p_uid
                   and (b.until is null or b.until > now()));
$$;

-- 3 owner, 2 admin, 1 member, 0 not in it. The owner comes from the server /
-- group row itself, never from a role string a member could have written.
create or replace function public.member_rank(p_scope text, p_cid bigint, p_uid uuid) returns int
language plpgsql stable security definer set search_path = public as $$
declare o uuid; r text; found_row boolean := false;
begin
  if p_uid is null or p_cid is null then return 0; end if;
  if p_scope = 'server' then
    select s.owner into o from public.servers s where s.id = p_cid;
    if o is not null and o = p_uid then return 3; end if;
    select m.role, true into r, found_row from public.server_members m where m.server_id = p_cid and m.member = p_uid;
  elsif p_scope = 'group' then
    select c.owner into o from public.group_convos c where c.id = p_cid;
    if o is not null and o = p_uid then return 3; end if;
    select m.role, true into r, found_row from public.group_members m where m.convo_id = p_cid and m.member = p_uid;
  else
    return 0;
  end if;
  if not coalesce(found_row, false) then return 0; end if;
  return case r when 'admin' then 2 else 1 end;
end $$;

-- A banned person is not a member: every existing read / post policy is built on
-- these two helpers, so reading and posting stop with the ban.
create or replace function public.is_server_member(sid bigint) returns boolean
  language sql security definer stable set search_path = public as $$
    select exists (select 1 from public.server_members m
                   where m.server_id = sid and m.member = auth.uid())
       and not public.is_community_banned('server', sid, auth.uid());
  $$;
create or replace function public.is_group_member(cid bigint) returns boolean
  language sql security definer stable set search_path = public as $$
    select exists (select 1 from public.group_members m
                   where m.convo_id = cid and m.member = auth.uid())
       and not public.is_community_banned('group', cid, auth.uid());
  $$;

-- ---------------------------------------------------------------------------
-- Row-level security.
-- ---------------------------------------------------------------------------
drop policy if exists "see bans"    on public.community_bans;
drop policy if exists "ban members" on public.community_bans;
drop policy if exists "edit bans"   on public.community_bans;
drop policy if exists "lift bans"   on public.community_bans;
-- Owner / admin see the list; a banned person sees their own row (the launcher shows why).
create policy "see bans" on public.community_bans for select
  using (user_id = auth.uid() or public.member_rank(scope, container_id, auth.uid()) >= 2);
create policy "ban members" on public.community_bans for insert with check (
  user_id <> auth.uid() and banned_by = auth.uid()
  and public.member_rank(scope, container_id, auth.uid()) >= 2
  and public.member_rank(scope, container_id, auth.uid()) > public.member_rank(scope, container_id, user_id));
create policy "edit bans" on public.community_bans for update
  using (public.member_rank(scope, container_id, auth.uid()) >= 2
         and public.member_rank(scope, container_id, auth.uid()) > public.member_rank(scope, container_id, user_id))
  with check (user_id <> auth.uid() and banned_by = auth.uid());
create policy "lift bans" on public.community_bans for delete
  using (public.member_rank(scope, container_id, auth.uid()) >= 2);

-- Joining: yourself only, as a plain member, and never while banned.
drop policy if exists "join self"                 on public.server_members;
drop policy if exists "not banned from server"    on public.server_members;
drop policy if exists "leave or kick"             on public.server_members;
create policy "join self" on public.server_members for insert
  with check (member = auth.uid() and coalesce(role, 'member') = 'member');
create policy "not banned from server" on public.server_members as restrictive for insert
  with check (not public.is_community_banned('server', server_id, member));
-- Leave yourself, or remove someone ranked strictly below you (owner > admin > member).
create policy "leave or kick" on public.server_members for delete using (
  member = auth.uid()
  or (public.member_rank('server', server_id, auth.uid()) >= 2
      and public.member_rank('server', server_id, auth.uid()) > public.member_rank('server', server_id, member)));

drop policy if exists "not banned from group" on public.group_members;
drop policy if exists "leave or kick"         on public.group_members;
create policy "not banned from group" on public.group_members as restrictive for insert
  with check (not public.is_community_banned('group', convo_id, member));
create policy "leave or kick" on public.group_members for delete using (
  member = auth.uid()
  or (public.member_rank('group', convo_id, auth.uid()) >= 2
      and public.member_rank('group', convo_id, auth.uid()) > public.member_rank('group', convo_id, member)));

-- Roles: never writable straight from a client. New rows are 'owner' only for
-- the real owner, otherwise 'member'; a change needs set_member_role.
create or replace function public.member_role_guard() returns trigger
language plpgsql security definer set search_path = public as $$
declare o uuid;
begin
  if tg_op = 'INSERT' then
    if tg_table_name = 'server_members' then select s.owner into o from public.servers s where s.id = new.server_id;
    else select c.owner into o from public.group_convos c where c.id = new.convo_id; end if;
    new.role := case when o is not null and o = new.member then 'owner' else 'member' end;
    return new;
  end if;
  if new.role is distinct from old.role and auth.uid() is not null
     and coalesce(current_setting('okay.role_rpc', true), '') <> 'on' then
    raise exception 'Roles can only be changed by the owner.' using errcode = '42501';
  end if;
  return new;
end $$;
drop trigger if exists server_members_role_guard on public.server_members;
create trigger server_members_role_guard before insert or update of role on public.server_members
  for each row execute function public.member_role_guard();
drop trigger if exists group_members_role_guard on public.group_members;
create trigger group_members_role_guard before insert or update of role on public.group_members
  for each row execute function public.member_role_guard();

-- ---------------------------------------------------------------------------
-- A ban removes the person from the server / group, whatever wrote the ban.
-- ---------------------------------------------------------------------------
create or replace function public.community_bans_apply() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  perform set_config('okay.skip_kick_log', 'on', true);
  if new.scope = 'server' then delete from public.server_members where server_id = new.container_id and member = new.user_id;
  else delete from public.group_members where convo_id = new.container_id and member = new.user_id; end if;
  perform set_config('okay.skip_kick_log', '', true);
  return new;
end $$;
drop trigger if exists community_bans_apply on public.community_bans;
create trigger community_bans_apply after insert or update on public.community_bans
  for each row execute function public.community_bans_apply();

-- A deleted server / group takes its bans with it.
create or replace function public.community_bans_cleanup() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  delete from public.community_bans where scope = tg_argv[0] and container_id = old.id;
  return old;
end $$;
drop trigger if exists servers_bans_cleanup on public.servers;
create trigger servers_bans_cleanup after delete on public.servers
  for each row execute function public.community_bans_cleanup('server');
drop trigger if exists group_convos_bans_cleanup on public.group_convos;
create trigger group_convos_bans_cleanup after delete on public.group_convos
  for each row execute function public.community_bans_cleanup('group');

-- ---------------------------------------------------------------------------
-- Audit: admin_log (admin.sql) when present. Only calls made by a signed-in
-- user are logged (the SQL editor is not). admin_log is readable by site staff.
-- ---------------------------------------------------------------------------
create or replace function public.member_mod_audit(p_action text, p_target uuid, p_detail text) returns void
language plpgsql security definer set search_path = public as $$
begin
  if auth.uid() is null then return; end if;
  if to_regprocedure('public.admin_log_write(text,uuid,text)') is null then return; end if;
  perform public.admin_log_write(p_action, p_target, p_detail);
end $$;
revoke execute on function public.member_mod_audit(text, uuid, text) from public, anon, authenticated;

create or replace function public.member_removed_log() returns trigger
language plpgsql security definer set search_path = public as $$
declare scope text; cid bigint; alive boolean;
begin
  if auth.uid() is null or old.member = auth.uid() or coalesce(current_setting('okay.skip_kick_log', true), '') = 'on' then return old; end if;
  if tg_table_name = 'server_members' then
    scope := 'server'; cid := old.server_id; alive := exists (select 1 from public.servers where id = cid);
  else
    scope := 'group'; cid := old.convo_id; alive := exists (select 1 from public.group_convos where id = cid);
  end if;
  if alive then perform public.member_mod_audit('kick', old.member, scope || ' ' || cid); end if;
  return old;
end $$;
drop trigger if exists server_members_removed_log on public.server_members;
create trigger server_members_removed_log after delete on public.server_members
  for each row execute function public.member_removed_log();
drop trigger if exists group_members_removed_log on public.group_members;
create trigger group_members_removed_log after delete on public.group_members
  for each row execute function public.member_removed_log();

create or replace function public.community_bans_log() returns trigger
language plpgsql security definer set search_path = public as $$
declare alive boolean; r public.community_bans;
begin
  if tg_op = 'DELETE' then
    r := old;
    alive := case r.scope when 'server' then exists (select 1 from public.servers where id = r.container_id)
                          else exists (select 1 from public.group_convos where id = r.container_id) end;
    if alive then perform public.member_mod_audit('unban', r.user_id, r.scope || ' ' || r.container_id); end if;
    return old;
  end if;
  r := new;
  perform public.member_mod_audit('ban', r.user_id,
    r.scope || ' ' || r.container_id || ': ' || coalesce(r.reason, '') ||
    case when r.until is null then ' (permanent)'
         else ' (until ' || to_char(r.until at time zone 'utc', 'YYYY-MM-DD HH24:MI') || ' UTC)' end);
  return new;
end $$;
drop trigger if exists community_bans_log on public.community_bans;
create trigger community_bans_log after insert or update or delete on public.community_bans
  for each row execute function public.community_bans_log();

-- ---------------------------------------------------------------------------
-- RPCs the launcher calls. They raise a readable message on any refusal.
-- ---------------------------------------------------------------------------
create or replace function public.member_mod_check(p_scope text, p_cid bigint, p_target uuid) returns int
language plpgsql stable security definer set search_path = public as $$
declare me uuid := auth.uid(); mine int; theirs int;
begin
  if me is null then raise exception 'Sign in first.'; end if;
  if p_scope not in ('server','group') then raise exception 'Unknown scope.'; end if;
  mine := public.member_rank(p_scope, p_cid, me);
  if mine < 2 then raise exception 'Only the owner or an admin can do this.'; end if;
  if p_target = me then raise exception 'You can''t do this to yourself.'; end if;
  theirs := public.member_rank(p_scope, p_cid, p_target);
  if theirs = 3 then raise exception 'The owner can''t be removed or banned.'; end if;
  if mine <= theirs then raise exception 'You can only act on members below your role.'; end if;
  return theirs;
end $$;

create or replace function public.kick_member(p_scope text, p_container bigint, p_target uuid) returns boolean
language plpgsql security definer set search_path = public as $$
declare theirs int;
begin
  theirs := public.member_mod_check(p_scope, p_container, p_target);
  if theirs = 0 then raise exception 'That person is not in this %.', p_scope; end if;
  if p_scope = 'server' then delete from public.server_members where server_id = p_container and member = p_target;
  else delete from public.group_members where convo_id = p_container and member = p_target; end if;
  return true;
end $$;

create or replace function public.ban_member(p_scope text, p_container bigint, p_target uuid,
                                             p_reason text default null, p_seconds bigint default null) returns boolean
language plpgsql security definer set search_path = public as $$
begin
  perform public.member_mod_check(p_scope, p_container, p_target);
  if p_seconds is not null and p_seconds <= 0 then raise exception 'A ban length must be positive (or empty for permanent).'; end if;
  insert into public.community_bans (scope, container_id, user_id, reason, until, banned_by)
    values (p_scope, p_container, p_target, nullif(left(btrim(coalesce(p_reason, '')), 300), ''),
            case when p_seconds is null then null else now() + make_interval(secs => p_seconds) end, auth.uid())
  on conflict (scope, container_id, user_id) do update
    set reason = excluded.reason, until = excluded.until, banned_by = excluded.banned_by, created_at = now();
  return true;   -- the community_bans_apply trigger removes the membership
end $$;

create or replace function public.unban_member(p_scope text, p_container bigint, p_target uuid) returns boolean
language plpgsql security definer set search_path = public as $$
begin
  if auth.uid() is null then raise exception 'Sign in first.'; end if;
  if p_scope not in ('server','group') then raise exception 'Unknown scope.'; end if;
  if public.member_rank(p_scope, p_container, auth.uid()) < 2 then raise exception 'Only the owner or an admin can do this.'; end if;
  delete from public.community_bans where scope = p_scope and container_id = p_container and user_id = p_target;
  if not found then raise exception 'That person is not banned.'; end if;
  return true;
end $$;

create or replace function public.set_member_role(p_scope text, p_container bigint, p_target uuid, p_role text) returns boolean
language plpgsql security definer set search_path = public as $$
declare theirs int;
begin
  if auth.uid() is null then raise exception 'Sign in first.'; end if;
  if p_scope not in ('server','group') then raise exception 'Unknown scope.'; end if;
  if p_role not in ('admin','member') then raise exception 'A role must be admin or member.'; end if;
  if public.member_rank(p_scope, p_container, auth.uid()) < 3 then raise exception 'Only the owner can change roles.'; end if;
  if p_target = auth.uid() then raise exception 'You can''t do this to yourself.'; end if;
  theirs := public.member_rank(p_scope, p_container, p_target);
  if theirs = 0 then raise exception 'That person is not in this %.', p_scope; end if;
  if theirs = 3 then raise exception 'The owner''s role can''t be changed.'; end if;
  perform set_config('okay.role_rpc', 'on', true);
  if p_scope = 'server' then update public.server_members set role = p_role where server_id = p_container and member = p_target;
  else update public.group_members set role = p_role where convo_id = p_container and member = p_target; end if;
  perform set_config('okay.role_rpc', '', true);
  perform public.member_mod_audit(case when p_role = 'admin' then 'role_add' else 'role_remove' end, p_target,
                                  'admin in ' || p_scope || ' ' || p_container);
  return true;
end $$;

-- The invite-code and add-member paths are SECURITY DEFINER, so they check bans themselves.
create or replace function public.join_server(p_code text)
  returns public.servers language plpgsql security definer set search_path = public as $$
  declare s public.servers; b public.community_bans;
  begin
    select * into s from public.servers where invite_code = p_code;
    if s.id is null then return null; end if;
    select * into b from public.community_bans
      where scope = 'server' and container_id = s.id and user_id = auth.uid() and (until is null or until > now());
    if found then
      raise exception 'You are banned from this server%',
        (case when b.until is null then ' (permanently)'
              else ' until ' || to_char(b.until at time zone 'utc', 'YYYY-MM-DD HH24:MI') || ' UTC' end)
        || coalesce(': ' || b.reason, '.');
    end if;
    insert into public.server_members (server_id, member) values (s.id, auth.uid())
      on conflict do nothing;
    return s;
  end; $$;

create or replace function public.add_group_member(p_convo bigint, p_member uuid)
  returns boolean language plpgsql security definer set search_path = public as $$
  begin
    if not exists (select 1 from public.group_convos c where c.id = p_convo and c.owner = auth.uid()) then
      return false;
    end if;
    if public.is_community_banned('group', p_convo, p_member) then
      raise exception 'That person is banned from this group. Lift the ban first.';
    end if;
    insert into public.group_members (convo_id, member) values (p_convo, p_member)
      on conflict do nothing;
    return true;
  end; $$;

revoke all on function public.kick_member(text, bigint, uuid)                     from public, anon;
revoke all on function public.ban_member(text, bigint, uuid, text, bigint)        from public, anon;
revoke all on function public.unban_member(text, bigint, uuid)                    from public, anon;
revoke all on function public.set_member_role(text, bigint, uuid, text)           from public, anon;
revoke all on function public.member_mod_check(text, bigint, uuid)                from public, anon, authenticated;
grant execute on function public.kick_member(text, bigint, uuid)                  to authenticated;
grant execute on function public.ban_member(text, bigint, uuid, text, bigint)     to authenticated;
grant execute on function public.unban_member(text, bigint, uuid)                 to authenticated;
grant execute on function public.set_member_role(text, bigint, uuid, text)        to authenticated;
grant execute on function public.member_rank(text, bigint, uuid)                  to anon, authenticated;
grant execute on function public.is_community_banned(text, bigint, uuid)          to anon, authenticated;
grant execute on function public.join_server(text)                                to anon, authenticated;
grant execute on function public.add_group_member(bigint, uuid)                   to anon, authenticated;
