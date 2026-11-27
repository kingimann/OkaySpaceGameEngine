-- OkaySpace admin tools (Supabase): site roles, site-wide bans, audit log.
-- Run once in the SQL editor AFTER social.sql, profile_extras.sql,
-- forum_sections.sql (and follows.sql / group_chats.sql if you use them).
-- Idempotent: safe to re-run.
--
-- Roles are profile badges, highest first:
--   owner      (rank 4) everything, including handing out admin; only ever
--              given in the SQL editor, never through the API
--   admin      (rank 3) assign staff, moderator, support and cosmetic badges,
--              ban staff
--   staff      (rank 2) assign moderator, support and cosmetic badges, verify
--              players, ban moderators and players, moderate everything
--   moderator  (rank 1) moderate every forum section, ban players
--   support    (rank 0) reads in the admin dashboard only; no power over any
--              row rule
-- You can only act on people ranked below you. Make yourself the owner here in
-- the SQL editor:
--   insert into public.profile_badges (owner, badge)
--   select id, 'owner' from public.profiles where username = 'you'
--   on conflict do nothing;
--
-- Admin dashboard (docs/platform/okay_backend_admin.md): what each role may do
-- there is a row in admin_role_permissions, read through admin_can(uid,
-- permission); admin_gate(permission, ...) checks it with a rate limit and logs
-- refusals. admin_log is append-only (the shared audit_append_only trigger,
-- also used by anticheat.sql's ac_audit) and records before / after values,
-- where the request came from and the outcome. admin_totp_secrets holds each
-- admin's authenticator secret for the okay-admin server function; no client
-- role can read it.
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
-- Rate limiting (used by admin_gate below and by anticheat.sql; nobody calls it directly)
-- ---------------------------------------------------------------------------
create table if not exists public.ac_rate_events (
  key text not null,
  at  timestamptz not null default now()
);
create index if not exists ac_rate_events_key on public.ac_rate_events (key, at desc);
alter table public.ac_rate_events enable row level security;   -- no policy: only the definer functions below touch it
revoke all on public.ac_rate_events from public, anon, authenticated;

-- Count one use of `p_key`; refuse when it was already used `p_limit` times in the last `p_window`
-- (at most a day is remembered). The advisory lock makes two simultaneous calls count one after the other.
create or replace function public.ac_rate_hit(p_key text, p_limit int, p_window interval) returns void
language plpgsql security definer set search_path = public as $$
begin
  perform pg_advisory_xact_lock(hashtext('ac_rate:' || p_key));
  delete from public.ac_rate_events where key = p_key and at < now() - interval '1 day';
  if (select count(*) from public.ac_rate_events where key = p_key and at > now() - p_window) >= p_limit then
    raise exception 'Too many requests; try again later' using errcode = 'check_violation';
  end if;
  insert into public.ac_rate_events (key) values (p_key);
end $$;
revoke execute on function public.ac_rate_hit(text, int, interval) from public, anon, authenticated;
grant execute on function public.ac_rate_hit(text, int, interval) to service_role;   -- the okay-admin function's sign-in limits

-- ---------------------------------------------------------------------------
-- Append-only logs (admin_log here, ac_audit in anticheat.sql): one trigger
-- function for both. Arguments: the log's name for the error, the session
-- setting that lets one maintenance job of ours through (ac_purge_audit's
-- delete, public_names.sql's one-off name clean-up; set for one transaction
-- in the SQL editor, never by a client), then the columns that may only ever
-- be EMPTIED (set to null or '[deleted]') when an account is deleted.
-- Everything else refuses update, delete and truncate, from any role.
-- ---------------------------------------------------------------------------
create or replace function public.audit_append_only() returns trigger
language plpgsql set search_path = public as $$
declare erasable text[]; o jsonb; n jsonb; k text;
begin
  if coalesce(current_setting(tg_argv[1], true), '') = 'on' then
    if tg_level = 'STATEMENT' then return null; end if;
    if tg_op = 'DELETE' then return old; end if;
    return new;
  end if;
  if tg_op = 'UPDATE' and tg_nargs > 2 then
    erasable := tg_argv[2:tg_nargs - 1];
    o := to_jsonb(old); n := to_jsonb(new);
    for k in select key from jsonb_each(n) loop
      if (n -> k) is distinct from (o -> k)
         and (not (k = any (erasable)) or not (jsonb_typeof(n -> k) = 'null' or n -> k = '"[deleted]"'::jsonb)) then
        raise exception '% is append-only (% can only be emptied when an account is deleted)', tg_argv[0], k
          using errcode = 'insufficient_privilege';
      end if;
    end loop;
    return new;
  end if;
  raise exception '% is append-only', tg_argv[0] using errcode = 'insufficient_privilege';
end $$;

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
  check (badge in ('owner', 'admin', 'staff', 'moderator', 'support', 'developer', 'beta', 'partner'));

-- 4 owner, 3 admin, 2 staff, 1 moderator, 0 support and everyone else.
create or replace function public.role_rank(p_user uuid) returns int
language sql stable security definer set search_path = public as $$
  select coalesce(max(case badge when 'owner' then 4 when 'admin' then 3 when 'staff' then 2 when 'moderator' then 1 else 0 end), 0)
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

-- ---------------------------------------------------------------------------
-- Permissions (admin dashboard): one row per permission, one row per role that
-- holds it. The owner changes the mapping through the okay-admin server
-- function (with a re-check) or here in the SQL editor; no client writes it.
-- Re-running this file adds only permissions it has not seen before, with
-- their default roles, so the owner's changes stay.
-- ---------------------------------------------------------------------------
create table if not exists public.admin_permissions (
  permission  text primary key check (permission ~ '^[a-z_]+\.[a-z_]+$'),
  kind        text not null check (kind in ('read', 'write', 'danger')),   -- picks the rate limit in admin_gate
  description text not null check (char_length(description) <= 200)
);
create table if not exists public.admin_role_permissions (
  role       text not null check (role in ('owner', 'admin', 'staff', 'moderator', 'support')),
  permission text not null references public.admin_permissions on delete cascade,
  primary key (role, permission)
);
alter table public.admin_permissions enable row level security;
alter table public.admin_role_permissions enable row level security;
revoke insert, update, delete, truncate on public.admin_permissions, public.admin_role_permissions from public, anon, authenticated;

with defaults (permission, kind, description, roles) as (values
  ('dashboard.view',     'read',   'Open the admin dashboard and its overview numbers',          '{owner,admin,staff,moderator,support}'),
  ('providers.read',     'read',   'See which provider runs each service',                       '{owner,admin,staff,moderator,support}'),
  ('users.read',         'read',   'Search players; see their roles and site bans',              '{owner,admin,staff,moderator,support}'),
  ('network.read',       'read',   'See listed game servers and the voice relay',                '{owner,admin,staff,moderator,support}'),
  ('audit.read',         'read',   'Read the admin log (without addresses)',                     '{owner,admin,staff,moderator}'),
  ('reports.review',     'write',  'Review player reports',                                      '{owner,admin,staff,moderator}'),
  ('market.review',      'write',  'Approve or reject marketplace items',                        '{owner,admin,staff,moderator}'),
  ('forums.moderate',    'write',  'Moderate forum sections',                                    '{owner,admin,staff,moderator}'),
  ('users.ban',          'write',  'Site-ban and unban people ranked below you',                 '{owner,admin,staff,moderator}'),
  ('users.verify',       'write',  'Verify players',                                             '{owner,admin,staff}'),
  ('roles.manage',       'write',  'Give and take roles (which ones: can_manage_badge)',         '{owner,admin,staff}'),
  ('users.read_private', 'read',   'See an account''s email and sign-in times',                  '{owner,admin}'),
  ('auth.reset',         'write',  'Send the normal password-reset email',                       '{owner,admin}'),
  ('auth.manage',        'write',  'Confirm an account''s email',                                '{owner,admin}'),
  ('ac.review',          'write',  'Anti-cheat reports, bans and appeals',                       '{owner,admin}'),
  ('errors.read',        'read',   'Read crash reports',                                         '{owner,admin}'),
  ('db.read',            'read',   'List tables and browse allowed rows',                        '{owner,admin}'),
  ('storage.read',       'read',   'List storage buckets and files',                             '{owner,admin}'),
  ('functions.read',     'read',   'See server functions and their health',                      '{owner,admin}'),
  ('providers.check',    'write',  'Run live provider checks',                                   '{owner,admin}'),
  ('config.write',       'write',  'Change app_config settings',                                 '{owner,admin}'),
  ('auth.ban',           'danger', 'Block an account from signing in',                           '{owner}'),
  ('auth.delete',        'danger', 'Delete an account and its data',                             '{owner}'),
  ('storage.delete',     'danger', 'Delete a stored file',                                       '{owner}'),
  ('permissions.manage', 'danger', 'Change which role holds which permission',                   '{owner}'),
  ('logs.read',          'read',   'Read the provider''s logs (owner''s computer only)',         '{owner}'),
  ('audit.read_ip',      'read',   'Read addresses and user agents in the admin log',            '{owner}'),
  -- Owner powers through the console on the owner's computer (section 18 of the design)
  ('sql.read',           'read',   'Run read-only SQL (owner''s computer only)',                   '{owner}'),
  ('sql.write',          'danger', 'Run SQL that changes data: preview and typed confirm',       '{owner}'),
  ('schema.manage',      'danger', 'Change tables and row rules',                                '{owner}'),
  ('backups.manage',     'danger', 'List backups; restore where the plan allows it',             '{owner}'),
  ('functions.deploy',   'danger', 'Deploy and roll back server functions',                      '{owner}'),
  ('functions.secrets',  'danger', 'Set and rotate server function secrets',                     '{owner}'),
  ('auth.settings',      'danger', 'Sign-up, email templates, password rules, sign-in providers', '{owner}'),
  ('auth.end_sessions',  'danger', 'End every session of an account',                            '{owner}'),
  ('storage.manage',     'danger', 'Create buckets, change their rules, upload and replace files', '{owner}'),
  ('network.manage',     'danger', 'Network restrictions, API rate limits, SSL and domain',      '{owner}'),
  ('releases.pull',      'danger', 'Pull a bad release from "latest"',                           '{owner}'),
  -- Game and community controls
  ('game.maintenance',   'danger', 'Turn maintenance mode on or off',                            '{owner,admin}'),
  ('game.announce',      'write',  'Set the launcher-wide announcement banner',                  '{owner,admin,staff}'),
  ('game.min_version',   'danger', 'Set the minimum launcher and game version',                  '{owner,admin}'),
  ('game.features',      'danger', 'Turn chat, uploads, matchmaking or sign-ups on or off',      '{owner,admin}'),
  ('users.mass_action',  'danger', 'Mute, ban or unban a list of players: preview, at most 50',  '{owner,admin}'),
  ('users.mute',         'write',  'Mute a player in chat',                                      '{owner,admin,staff,moderator}'),
  ('chat.filter',        'write',  'Edit the chat word filter',                                  '{owner,admin,staff}'),
  ('chat.moderate',      'write',  'Lock or clear a chat room or forum thread',                  '{owner,admin,staff,moderator}'),
  ('profiles.edit',      'write',  'Edit a player''s profile fields from the allowed list',      '{owner,admin,staff,support}'),
  ('users.view_as',      'read',   'See what a player sees, read only (never their session)',    '{owner,admin,staff,support}'),
  ('servers.control',    'danger', 'Shut down or restart a listed game server',                  '{owner,admin}'),
  ('rooms.kick',         'write',  'Kick everyone from a room',                                  '{owner,admin,staff,moderator}')
), added as (
  insert into public.admin_permissions (permission, kind, description)
  select permission, kind, description from defaults
  on conflict (permission) do nothing
  returning permission
)
insert into public.admin_role_permissions (role, permission)
select r, d.permission from defaults d join added a on a.permission = d.permission, unnest(d.roles::text[]) r
on conflict do nothing;

-- Does this account hold this permission? The owner holds every known one
-- (so the owner can never lock themselves out); everyone else through the
-- roles they hold.
create or replace function public.admin_can(p_user uuid, p_permission text) returns boolean
language sql stable security definer set search_path = public as $$
  select p_user is not null
     and exists (select 1 from public.admin_permissions where permission = p_permission)
     and (public.role_rank(p_user) >= 4
          or exists (select 1 from public.profile_badges b
                     join public.admin_role_permissions rp on rp.role = b.badge
                     where b.owner = p_user and rp.permission = p_permission));
$$;
revoke execute on function public.admin_can(uuid, text) from public, anon;
grant execute on function public.admin_can(uuid, text) to authenticated, service_role;

-- Every permission an account holds (the dashboard's /me): your own, or anyone's for a users.read holder.
create or replace function public.admin_permissions_of(p_user uuid) returns setof text
language sql stable security definer set search_path = public as $$
  select p.permission from public.admin_permissions p
   where (auth.uid() is null or p_user = auth.uid() or public.admin_can(auth.uid(), 'users.read'))
     and public.admin_can(p_user, p.permission)
   order by 1;
$$;
revoke execute on function public.admin_permissions_of(uuid) from public, anon;
grant execute on function public.admin_permissions_of(uuid) to authenticated, service_role;

drop policy if exists "admins read permissions" on public.admin_permissions;
create policy "admins read permissions" on public.admin_permissions for select
  using (public.admin_can(auth.uid(), 'dashboard.view'));
drop policy if exists "admins read role permissions" on public.admin_role_permissions;
create policy "admins read role permissions" on public.admin_role_permissions for select
  using (public.admin_can(auth.uid(), 'dashboard.view'));

-- Who may hand out which badge (needs roles.manage): the owner anything but
-- owner; admins staff and below; staff moderator, support and cosmetic badges.
-- Nobody hands out owner through the API.
create or replace function public.can_manage_badge(p_actor uuid, p_badge text) returns boolean
language sql stable security definer set search_path = public as $$
  select public.admin_can(p_actor, 'roles.manage') and case
    when p_badge = 'owner' then false
    when public.role_rank(p_actor) >= 4 then true
    when public.role_rank(p_actor) >= 3 then p_badge <> 'admin'
    when public.role_rank(p_actor) >= 2 then p_badge in ('moderator', 'support', 'developer', 'beta', 'partner')
    else false end;
$$;
drop policy if exists "staff award badges"  on public.profile_badges;
drop policy if exists "staff remove badges" on public.profile_badges;
create policy "staff award badges"  on public.profile_badges for insert
  with check (public.can_manage_badge(auth.uid(), badge));
create policy "staff remove badges" on public.profile_badges for delete
  using (public.can_manage_badge(auth.uid(), badge) and (owner <> auth.uid() or badge not in ('admin', 'owner')));

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
-- Admin dashboard columns (docs/platform/okay_backend_admin.md section 11). Old rows keep nulls.
alter table public.admin_log add column if not exists before     jsonb;
alter table public.admin_log add column if not exists after      jsonb;
alter table public.admin_log add column if not exists source     text check (source in ('dashboard', 'launcher', 'console', 'sql'));
alter table public.admin_log add column if not exists request_id uuid;
alter table public.admin_log add column if not exists permission text check (char_length(permission) <= 64);
alter table public.admin_log add column if not exists outcome    text not null default 'ok' check (outcome in ('ok', 'refused', 'failed'));
alter table public.admin_log add column if not exists ip         inet;          -- personal data: emptied after 90 days
alter table public.admin_log add column if not exists user_agent text check (char_length(user_agent) <= 300);
create index if not exists admin_log_actor on public.admin_log (actor, created_at desc);
alter table public.admin_log enable row level security;
drop policy if exists "mods read log"  on public.admin_log;
drop policy if exists "mods write log" on public.admin_log;
create policy "mods read log"  on public.admin_log for select using (public.admin_can(auth.uid(), 'audit.read'));
create policy "mods write log" on public.admin_log for insert
  with check (public.is_global_mod(auth.uid()) and actor = auth.uid() and action = 'note');
-- Clients read every column but the address and the user agent (audit.read_ip:
-- the owner, through the server function with the service role).
revoke select, update, delete, truncate on public.admin_log from public, anon, authenticated;
grant select (id, actor, actor_name, action, target, target_name, detail, created_at,
              before, after, source, request_id, permission, outcome) on public.admin_log to authenticated;

-- Where the request came from, as the request itself claims it: the
-- x-okay-source / x-okay-request-id / x-okay-client-ip / x-okay-user-agent
-- headers the okay-admin function sends, else the plain User-Agent and the
-- first X-Forwarded-For address. A signed-in client talking to the API
-- directly can set these headers; the actor column it cannot. 'sql' is never
-- taken from a header. Values that do not parse are dropped.
create or replace function public.admin_log_context(out source text, out request_id uuid, out ip inet, out user_agent text)
language plpgsql stable set search_path = public as $$
declare h jsonb; v text;
begin
  begin h := nullif(current_setting('request.headers', true), '')::jsonb; exception when others then h := null; end;
  h := coalesce(h, '{}'::jsonb);
  v := h ->> 'x-okay-source';
  source := case when auth.uid() is null then 'sql' when v in ('dashboard', 'launcher', 'console') then v else 'launcher' end;
  begin request_id := (h ->> 'x-okay-request-id')::uuid; exception when others then request_id := null; end;
  v := coalesce(h ->> 'x-okay-client-ip', btrim(split_part(h ->> 'x-forwarded-for', ',', 1)));
  begin ip := nullif(v, '')::inet; exception when others then ip := null; end;
  user_agent := left(coalesce(h ->> 'x-okay-user-agent', h ->> 'user-agent'), 300);
end $$;

-- The one writer every log row goes through. The service role calls it
-- directly (the okay-admin function, for actions with no row rule of their
-- own, giving the address it saw); the triggers reach it through admin_log_write.
create or replace function public.admin_log_record(p_actor uuid, p_action text, p_target uuid, p_detail text,
  p_before jsonb, p_after jsonb, p_source text, p_request_id uuid, p_permission text, p_outcome text,
  p_ip inet, p_user_agent text) returns bigint
language plpgsql security definer set search_path = public as $$
declare v_id bigint;
begin
  insert into public.admin_log (actor, actor_name, action, target, target_name, detail, before, after,
                                source, request_id, permission, outcome, ip, user_agent)
  values (p_actor, (select username from public.profiles where id = p_actor), left(p_action, 64), p_target,
          (select username from public.profiles where id = p_target), left(p_detail, 500), p_before, p_after,
          p_source, p_request_id, p_permission, coalesce(p_outcome, 'ok'), p_ip, left(p_user_agent, 300))
  returning id into v_id;
  return v_id;
end $$;
revoke execute on function public.admin_log_record(uuid, text, uuid, text, jsonb, jsonb, text, uuid, text, text, inet, text)
  from public, anon, authenticated;
grant execute on function public.admin_log_record(uuid, text, uuid, text, jsonb, jsonb, text, uuid, text, text, inet, text)
  to service_role;

-- As the signed-in caller, with the request's context (triggers and RPCs).
create or replace function public.admin_log_write(p_action text, p_target uuid, p_detail text,
  p_before jsonb, p_after jsonb, p_permission text, p_outcome text) returns void
language plpgsql security definer set search_path = public as $$
declare c record;
begin
  select * into c from public.admin_log_context();
  perform public.admin_log_record(auth.uid(), p_action, p_target, p_detail, p_before, p_after,
                                  c.source, c.request_id, p_permission, p_outcome, c.ip, c.user_agent);
end $$;
revoke execute on function public.admin_log_write(text, uuid, text, jsonb, jsonb, text, text) from public, anon, authenticated;
-- The short form older files use (reports.sql, anticheat.sql, member_moderation.sql).
create or replace function public.admin_log_write(p_action text, p_target uuid, p_detail text) returns void
language plpgsql security definer set search_path = public as $$
begin
  perform public.admin_log_write(p_action, p_target, p_detail, null::jsonb, null::jsonb, null::text, 'ok');
end $$;
revoke execute on function public.admin_log_write(text, uuid, text) from public, anon, authenticated;

-- A row a client inserts itself (a moderator's note) gets the server's
-- values for everything the server owns. When an account is deleted
-- (supabase/functions/delete-account empties actor / target), its address,
-- user agent and the before / after values about it go too.
create or replace function public.admin_log_guard() returns trigger
language plpgsql set search_path = public as $$
declare c record;
begin
  if tg_op = 'INSERT' then
    if current_user::text in ('anon', 'authenticated') then
      select * into c from public.admin_log_context();
      new.source := c.source; new.request_id := c.request_id; new.ip := c.ip; new.user_agent := c.user_agent;
      new.actor_name := (select username from public.profiles where id = new.actor);
      new.target_name := (select username from public.profiles where id = new.target);
      new.before := null; new.after := null; new.permission := null; new.outcome := 'ok'; new.created_at := now();
    end if;
    return new;
  end if;
  if old.actor is not null and new.actor is null then new.ip := null; new.user_agent := null; end if;
  if old.target is not null and new.target is null then new.before := null; new.after := null; end if;
  return new;
end $$;
drop trigger if exists admin_log_guard on public.admin_log;
create trigger admin_log_guard before insert or update on public.admin_log
  for each row execute function public.admin_log_guard();
-- Append-only (fires after admin_log_guard: triggers run in name order).
drop trigger if exists admin_log_immutable on public.admin_log;
create trigger admin_log_immutable before update or delete on public.admin_log
  for each row execute function public.audit_append_only('The admin log', 'okay.admin_log_maintenance',
    'actor', 'actor_name', 'target', 'target_name', 'ip', 'user_agent', 'before', 'after');
drop trigger if exists admin_log_no_truncate on public.admin_log;
create trigger admin_log_no_truncate before truncate on public.admin_log
  for each statement execute function public.audit_append_only('The admin log', 'okay.admin_log_maintenance');

-- Retention: addresses and user agents older than 90 days are emptied; the
-- rows stay. The SQL editor, pg_cron or the owner (audit.read_ip).
create or replace function public.admin_log_purge_old() returns bigint
language plpgsql security definer set search_path = public as $$
declare n bigint;
begin
  if auth.uid() is not null and not public.admin_can(auth.uid(), 'audit.read_ip') then
    raise exception 'Only the owner runs the admin log retention' using errcode = 'insufficient_privilege';
  end if;
  update public.admin_log set ip = null, user_agent = null
   where created_at < now() - interval '90 days' and (ip is not null or user_agent is not null);
  get diagnostics n = row_count;
  if n > 0 then
    perform public.admin_log_write('log_retention', null, n || ' addresses older than 90 days emptied',
                                   null, jsonb_build_object('emptied', n), 'audit.read_ip', 'ok');
  end if;
  return n;
end $$;
revoke execute on function public.admin_log_purge_old() from public, anon;
grant execute on function public.admin_log_purge_old() to authenticated;
do $$
begin
  if exists (select 1 from pg_extension where extname = 'pg_cron') then
    if exists (select 1 from cron.job where jobname = 'okay-admin-log-retention') then
      perform cron.unschedule('okay-admin-log-retention');
    end if;
    perform cron.schedule('okay-admin-log-retention', '29 3 * * *', 'select public.admin_log_purge_old()');
  end if;
end $$;

-- ---------------------------------------------------------------------------
-- The gate every admin action passes: permission, then the rate limit of the
-- permission's kind (read 120 a minute, write 30 a minute, danger 10 an hour,
-- per admin). A refusal is logged (at most 20 refusal rows per account per 10
-- minutes, so probing cannot flood the log). Answers 'ok', 'forbidden',
-- 'rate_limited' or 'signed_out'. A second factor and a typed confirmation
-- are checked by the okay-admin function before it calls here (round A5).
-- admin_gate: as the signed-in caller (admin's token).
-- admin_gate_as: the service role on behalf of an admin the function has
-- already checked (actions that need the second factor).
-- ---------------------------------------------------------------------------
create or replace function public.admin_gate_check(p_user uuid, p_permission text, p_action text, p_target uuid,
  p_source text, p_request_id uuid, p_ip inet, p_user_agent text) returns text
language plpgsql security definer set search_path = public as $$
declare k text; why text;
begin
  if p_user is null then return 'signed_out'; end if;
  select kind into k from public.admin_permissions where permission = p_permission;
  if k is null or not public.admin_can(p_user, p_permission) then
    why := 'forbidden';
  else
    begin
      perform public.ac_rate_hit('admin-' || k || ':' || p_user,
                                 case k when 'read' then 120 when 'write' then 30 else 10 end,
                                 case k when 'danger' then interval '1 hour' else interval '1 minute' end);
      return 'ok';
    exception when check_violation then why := 'rate_limited';
    end;
  end if;
  begin
    perform public.ac_rate_hit('admin-refused-log:' || p_user, 20, interval '10 minutes');
    perform public.admin_log_record(p_user, coalesce(left(p_action, 64), left(p_permission, 64)), p_target, why, null, null,
                                    p_source, p_request_id, left(p_permission, 64), 'refused', p_ip, p_user_agent);
  exception when check_violation then null;   -- over the refusal-log limit: still refused, not logged again
  end;
  return why;
end $$;
revoke execute on function public.admin_gate_check(uuid, text, text, uuid, text, uuid, inet, text) from public, anon, authenticated;

create or replace function public.admin_gate(p_permission text, p_action text default null, p_target uuid default null) returns text
language plpgsql security definer set search_path = public as $$
declare c record;
begin
  select * into c from public.admin_log_context();
  return public.admin_gate_check(auth.uid(), p_permission, p_action, p_target, c.source, c.request_id, c.ip, c.user_agent);
end $$;
revoke execute on function public.admin_gate(text, text, uuid) from public, anon;
grant execute on function public.admin_gate(text, text, uuid) to authenticated, service_role;

create or replace function public.admin_gate_as(p_actor uuid, p_permission text, p_action text, p_target uuid,
  p_source text, p_request_id uuid, p_ip inet, p_user_agent text) returns text
language sql security definer set search_path = public as $$
  select public.admin_gate_check(p_actor, p_permission, p_action, p_target, p_source, p_request_id, p_ip, p_user_agent);
$$;
revoke execute on function public.admin_gate_as(uuid, text, text, uuid, text, uuid, inet, text) from public, anon, authenticated;
grant execute on function public.admin_gate_as(uuid, text, text, uuid, text, uuid, inet, text) to service_role;

create or replace function public.profile_badges_log() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if auth.uid() is null then return coalesce(new, old); end if;   -- SQL editor: not logged
  if tg_op = 'INSERT' then
    perform public.admin_log_write('role_add', new.owner, new.badge, null, jsonb_build_object('badge', new.badge), 'roles.manage', 'ok');
  else
    perform public.admin_log_write('role_remove', old.owner, old.badge, jsonb_build_object('badge', old.badge), null, 'roles.manage', 'ok');
  end if;
  return coalesce(new, old);
end $$;
drop trigger if exists profile_badges_log on public.profile_badges;
create trigger profile_badges_log after insert or delete on public.profile_badges
  for each row execute function public.profile_badges_log();

create or replace function public.profiles_log_verified() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if auth.uid() is not null and new.verified is distinct from old.verified then
    perform public.admin_log_write(case when new.verified then 'verify' else 'unverify' end, new.id, null,
                                   jsonb_build_object('verified', old.verified), jsonb_build_object('verified', new.verified),
                                   'users.verify', 'ok');
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
-- users.read holders see every ban; a banned player sees their own (the launcher shows why).
create policy "see bans" on public.site_bans for select
  using (user_id = auth.uid() or public.admin_can(auth.uid(), 'users.read'));
-- users.ban, and only people ranked below you, never yourself.
create policy "ban players" on public.site_bans for insert with check (
  public.admin_can(auth.uid(), 'users.ban') and user_id <> auth.uid()
  and public.role_rank(auth.uid()) > public.role_rank(user_id));
create policy "edit bans" on public.site_bans for update using (
  public.admin_can(auth.uid(), 'users.ban') and public.role_rank(auth.uid()) > public.role_rank(user_id));
create policy "unban" on public.site_bans for delete using (public.admin_can(auth.uid(), 'users.ban'));

create or replace function public.site_bans_log() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if auth.uid() is null then return coalesce(new, old); end if;
  if tg_op = 'DELETE' then
    perform public.admin_log_write('unban', old.user_id, null, to_jsonb(old) - 'user_id', null, 'users.ban', 'ok');
  else perform public.admin_log_write('ban', new.user_id,
         coalesce(new.reason, '') || case when new.until is null then ' (permanent)'
                                          else ' (until ' || to_char(new.until at time zone 'utc', 'YYYY-MM-DD HH24:MI') || ' UTC)' end,
         case when tg_op = 'UPDATE' then to_jsonb(old) - 'user_id' end, to_jsonb(new) - 'user_id', 'users.ban', 'ok');
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
                                       new.author, new.title || coalesce(': ' || nullif(new.review_note, ''), ''),
                                       jsonb_build_object('status', old.status, 'review_note', old.review_note),
                                       jsonb_build_object('status', new.status, 'review_note', new.review_note), 'market.review', 'ok');
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
-- Dashboard numbers (dashboard.view)
-- ---------------------------------------------------------------------------
drop function if exists public.admin_stats();
create or replace function public.admin_stats()
returns table (players bigint, new_week bigint, online_now bigint, banned bigint, staff bigint,
               threads_week bigint, items bigint, pending bigint)
language plpgsql stable security definer set search_path = public as $$
begin
  if not public.admin_can(auth.uid(), 'dashboard.view') then return; end if;
  return query select
    (select count(*) from public.profiles),
    (select count(*) from public.profiles where created_at > now() - interval '7 days'),
    (select count(*) from public.profiles where last_seen > now() - interval '3 minutes'),
    (select count(*) from public.site_bans where until is null or until > now()),
    (select count(distinct owner) from public.profile_badges where badge in ('owner', 'admin', 'staff', 'moderator', 'support')),
    (select count(*) from public.forum_threads where created_at > now() - interval '7 days'),
    (select count(*) from public.workshop_items),
    (select count(*) from public.workshop_items where status = 'pending');
end $$;

-- ---------------------------------------------------------------------------
-- Changes made straight through the API under a permission (the word filter
-- below, app_config's free keys): counted against the admin's write limit
-- (the same 30 a minute as admin_gate) and logged with before and after.
-- Argument: the permission. The SQL editor and the service role are not
-- counted (no caller); the service role logs its own changes.
-- ---------------------------------------------------------------------------
create or replace function public.admin_row_log() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if auth.uid() is null then return coalesce(new, old); end if;
  perform public.ac_rate_hit('admin-write:' || auth.uid(), 30, interval '1 minute');
  perform public.admin_log_write(tg_table_name || '_' || lower(tg_op), null, null,
                                 case when tg_op <> 'INSERT' then to_jsonb(old) end,
                                 case when tg_op <> 'DELETE' then to_jsonb(new) end, tg_argv[0], 'ok');
  return coalesce(new, old);
end $$;

-- ---------------------------------------------------------------------------
-- Chat word filter (chat.filter edits it; chat.moderate reads it). Only the
-- list is stored here: using it on messages is a later round.
-- ---------------------------------------------------------------------------
create table if not exists public.chat_word_filter (
  id         bigint generated always as identity primary key,
  pattern    text not null unique check (char_length(pattern) between 1 and 64 and pattern = lower(pattern)),
  action     text not null default 'mask' check (action in ('mask', 'block')),
  added_by   uuid default auth.uid(),
  created_at timestamptz not null default now()
);
alter table public.chat_word_filter enable row level security;
revoke update, truncate on public.chat_word_filter from public, anon, authenticated;
drop policy if exists "mods read word filter" on public.chat_word_filter;
drop policy if exists "filter editors add"    on public.chat_word_filter;
drop policy if exists "filter editors remove" on public.chat_word_filter;
create policy "mods read word filter" on public.chat_word_filter for select
  using (public.admin_can(auth.uid(), 'chat.moderate') or public.admin_can(auth.uid(), 'chat.filter'));
create policy "filter editors add" on public.chat_word_filter for insert
  with check (public.admin_can(auth.uid(), 'chat.filter') and added_by = auth.uid());
create policy "filter editors remove" on public.chat_word_filter for delete
  using (public.admin_can(auth.uid(), 'chat.filter'));
drop trigger if exists chat_word_filter_log on public.chat_word_filter;
create trigger chat_word_filter_log after insert or delete on public.chat_word_filter
  for each row execute function public.admin_row_log('chat.filter');

-- ---------------------------------------------------------------------------
-- Authenticator secrets (TOTP, RFC 6238) for the okay-admin server function.
-- Only the service role reads or writes them: no client role, the owner's
-- included, has any privilege on the table, and no function a client can call
-- touches it. The code check itself (HMAC through Web Crypto) is in the
-- function, never in SQL. `secret` is opaque bytes: the function may store it
-- sealed with a key from its own secrets (decided in round A5).
-- ---------------------------------------------------------------------------
create table if not exists public.admin_totp_secrets (
  user_id        uuid primary key references auth.users on delete cascade,
  secret         bytea not null check (octet_length(secret) between 16 and 256),
  algorithm      text not null default 'SHA1' check (algorithm in ('SHA1', 'SHA256')),
  digits         int not null default 6 check (digits in (6, 8)),
  period         int not null default 30 check (period between 15 and 120),
  confirmed_at   timestamptz,          -- set once the admin typed a first good code
  last_used_step bigint,               -- the newest time step accepted: a code works once
  last_used_at   timestamptz,
  created_at     timestamptz not null default now()
);
alter table public.admin_totp_secrets enable row level security;   -- no policy: clients get nothing
revoke all on public.admin_totp_secrets from public, anon, authenticated;
grant select, insert, update, delete on public.admin_totp_secrets to service_role;

-- Accept a time step once: true when it is newer than the last one used.
-- Two requests with the same code: the row lock lets only one through.
create or replace function public.admin_totp_use_step(p_user uuid, p_step bigint) returns boolean
language sql volatile set search_path = public as $$
  with u as (
    update public.admin_totp_secrets set last_used_step = p_step, last_used_at = now()
     where user_id = p_user and (last_used_step is null or last_used_step < p_step)
    returning 1)
  select exists (select 1 from u);
$$;
revoke execute on function public.admin_totp_use_step(uuid, bigint) from public, anon, authenticated;
grant execute on function public.admin_totp_use_step(uuid, bigint) to service_role;
