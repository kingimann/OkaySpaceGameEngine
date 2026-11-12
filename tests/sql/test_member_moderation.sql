-- LOCAL test of docs/sql/member_moderation.sql. Each check prints "PASS <name>" or
-- "FAIL <name>: <why>" as a WARNING. Users: O owner, A / A2 admins, M / M2 members,
-- B bystander (never joins), X outsider with an invite.
\set QUIET on
set client_min_messages = warning;
reset role;
insert into auth.users (id, email)
  select ('00000000-0000-0000-0000-0000000000' || lpad(i::text, 2, '0'))::uuid, 'u' || i || '@x' from generate_series(1, 7) i;
insert into public.profiles (id, username)
  select ('00000000-0000-0000-0000-0000000000' || lpad(i::text, 2, '0'))::uuid, (array['own','adm','adm2','mem','mem2','byst','outs'])[i]
  from generate_series(1, 7) i on conflict do nothing;

create schema if not exists t;
create table if not exists t.kv (k text primary key, v bigint);
create table if not exists t.txt (k text primary key, v text);
create or replace function t.u(i int) returns uuid language sql as $$ select ('00000000-0000-0000-0000-0000000000' || lpad(i::text, 2, '0'))::uuid $$;
create or replace function t.ok(name text, cond boolean, why text default '') returns void language plpgsql as $$
begin if cond then raise warning 'PASS %', name; else raise warning 'FAIL %: %', name, why; end if; end $$;
create or replace function t.as_user(i int) returns void language plpgsql as $$
begin perform set_config('request.jwt.claims', json_build_object('sub', t.u(i), 'role', 'authenticated')::text, false); end $$;
-- Runs SQL as the current role; 'ok:<rows>' or the error message.
create or replace function t.run(q text) returns text language plpgsql as $$
declare n bigint;
begin execute q; get diagnostics n = row_count; return 'ok:' || n; exception when others then return sqlerrm; end $$;
create or replace function t.n(q text) returns bigint language plpgsql as $$
declare n bigint; begin execute q into n; return n; end $$;
create or replace function t.k(key text) returns bigint language sql as $$ select v from t.kv where k = key $$;
create or replace function t.kick(sc text, i int) returns text language sql as $$ select t.run(format('select public.kick_member(%L,%s,%L)', sc, t.k(sc), t.u(i))) $$;
create or replace function t.ban(sc text, i int, secs bigint default null) returns text language sql as $$
  select t.run(format('select public.ban_member(%L,%s,%L,%L,%s)', sc, t.k(sc), t.u(i), 'rude', coalesce(secs::text, 'null'))) $$;
create or replace function t.unban(sc text, i int) returns text language sql as $$ select t.run(format('select public.unban_member(%L,%s,%L)', sc, t.k(sc), t.u(i))) $$;
create or replace function t.role(sc text, i int, r text) returns text language sql as $$ select t.run(format('select public.set_member_role(%L,%s,%L,%L)', sc, t.k(sc), t.u(i), r)) $$;
create or replace function t.ismem(sc text, i int) returns boolean language plpgsql as $$
declare c bigint; begin
  if sc = 'server' then select count(*) into c from public.server_members where server_id = t.k('server') and member = t.u(i);
  else select count(*) into c from public.group_members where convo_id = t.k('group') and member = t.u(i); end if;
  return c > 0; end $$;
grant usage on schema t to public; grant all on t.kv, t.txt to public; grant execute on all functions in schema t to public;

-- ---- Setup: server (O creates; A, A2, M, M2 join; B stays out; X has the code) ----
select t.as_user(1); set role authenticated;
insert into t.kv select 'server', id from public.create_server('Test server');
reset role;
insert into t.kv select 'srvchan', id from public.channels where server_id = t.k('server');
insert into t.txt select 'code', invite_code from public.servers where id = t.k('server');
do $$ declare code text; i int; begin
  select v into code from t.txt where k = 'code';
  foreach i in array array[2,3,4,5,7] loop
    perform t.as_user(i); set local role authenticated;
    perform public.join_server(code);
    reset role;
  end loop;
end $$;
select t.as_user(1); set role authenticated;
select t.ok('server: owner promotes A to admin', t.role('server', 2, 'admin') = 'ok:1');
select t.ok('server: owner promotes A2 to admin', t.role('server', 3, 'admin') = 'ok:1');
reset role;
select t.ok('server: owner row has role owner', (select role from public.server_members where server_id = t.k('server') and member = t.u(1)) = 'owner');
select t.ok('server: join_server gives role member', (select role from public.server_members where server_id = t.k('server') and member = t.u(4)) = 'member');

-- ---- Server: role pairs -------------------------------------------------------
select t.as_user(4); set role authenticated;
select t.ok('server: member cannot kick member', t.kick('server', 5) like '%owner or an admin%', t.kick('server', 5));
select t.ok('server: member cannot ban member', t.ban('server', 5) like '%owner or an admin%');
select t.ok('server: member cannot kick admin', t.kick('server', 2) like '%owner or an admin%');
select t.ok('server: member cannot promote', t.role('server', 5, 'admin') like '%Only the owner can change roles%');
select t.ok('server: member cannot self-promote by update',
  t.run(format('update public.server_members set role = %L where server_id = %s and member = %L', 'admin', t.k('server'), t.u(4))) like '%Roles can only be changed%'
  or t.run(format('update public.server_members set role = %L where server_id = %s and member = %L', 'admin', t.k('server'), t.u(4))) = 'ok:0');
select t.ok('server: member cannot insert self as admin',
  t.run(format('insert into public.server_members (server_id, member, role) values (%s, %L, %L)', t.k('server'), t.u(6), 'admin')) like '%row-level security%');
select t.ok('server: member direct delete of another member removes nothing',
  t.run(format('delete from public.server_members where server_id = %s and member = %L', t.k('server'), t.u(5))) = 'ok:0');
reset role;
select t.ok('server: ...and M2 is still a member', t.ismem('server', 5));

select t.as_user(2); set role authenticated;
select t.ok('server: admin cannot kick admin', t.kick('server', 3) like '%below your role%', t.kick('server', 3));
select t.ok('server: admin cannot ban admin', t.ban('server', 3) like '%below your role%');
select t.ok('server: admin cannot kick owner', t.kick('server', 1) like '%owner can''t be removed%', t.kick('server', 1));
select t.ok('server: admin cannot ban owner', t.ban('server', 1) like '%owner can''t be removed%');
select t.ok('server: admin cannot kick self', t.kick('server', 2) like '%yourself%');
select t.ok('server: admin cannot promote', t.role('server', 5, 'admin') like '%Only the owner%');
select t.ok('server: admin direct delete of admin removes nothing',
  t.run(format('delete from public.server_members where server_id = %s and member = %L', t.k('server'), t.u(3))) = 'ok:0');
select t.ok('server: admin direct delete of owner removes nothing',
  t.run(format('delete from public.server_members where server_id = %s and member = %L', t.k('server'), t.u(1))) = 'ok:0');
select t.ok('server: admin direct ban insert on admin refused',
  t.run(format('insert into public.community_bans (scope, container_id, user_id) values (%L, %s, %L)', 'server', t.k('server'), t.u(3))) like '%row-level security%');
select t.ok('server: kick of a non-member is a clear error', t.kick('server', 6) like '%not in this server%');
select t.ok('server: admin kicks member M2', t.kick('server', 5) = 'ok:1');
reset role;
select t.ok('server: kicked member is gone', not t.ismem('server', 5));
select t.ok('server: kick is audited', (select count(*) from public.admin_log where action = 'kick' and target = t.u(5) and actor = t.u(2) and detail = 'server ' || t.k('server')) = 1);

-- a kicked (not banned) member may come back with the invite
select t.as_user(5); set role authenticated;
select t.ok('server: kicked member may rejoin by code', t.run(format('select public.join_server(%L)', (select v from t.txt where k = 'code'))) = 'ok:1');
reset role;
select t.ok('server: ...and is a member again', t.ismem('server', 5));

-- owner acts on admins
select t.as_user(1); set role authenticated;
select t.ok('server: owner cannot kick self', t.kick('server', 1) like '%yourself%');
select t.ok('server: owner cannot change own role', t.role('server', 1, 'member') like '%yourself%');
select t.ok('server: owner demotes A2', t.role('server', 3, 'member') = 'ok:1');
select t.ok('server: owner cannot set role owner', t.role('server', 4, 'owner') like '%admin or member%');
select t.ok('server: owner kicks admin A2', t.kick('server', 3) = 'ok:1');
select t.ok('server: owner role-changes a non-member refused', t.role('server', 6, 'admin') like '%not in this server%');
reset role;
select t.ok('server: role changes audited', (select count(*) from public.admin_log where action in ('role_add','role_remove') and detail like '%server%') >= 2);

-- ---- Server: ban, rejoin, post, read, bystander --------------------------------
select t.as_user(4); set role authenticated;
select t.ok('server: member can post before ban', t.run(format('insert into public.channel_messages (channel_id, body) values (%s, %L)', t.k('srvchan'), 'hello')) = 'ok:1');
select t.ok('server: member can read before ban', t.n(format('select count(*) from public.channel_messages where channel_id = %s', t.k('srvchan'))) >= 1);
reset role;
select t.as_user(2); set role authenticated;
select t.ok('server: admin bans M for an hour', t.ban('server', 4, 3600) = 'ok:1');
select t.ok('server: ban with zero seconds refused', t.ban('server', 5, 0) like '%positive%');
reset role;
select t.ok('server: ban removed the membership', not t.ismem('server', 4));
select t.ok('server: ban stores reason and end', (select reason = 'rude' and until > now() from public.community_bans where scope = 'server' and user_id = t.u(4)));
select t.ok('server: ban audited once, no extra kick row', (select count(*) from public.admin_log where action = 'ban' and target = t.u(4)) = 1
   and (select count(*) from public.admin_log where action = 'kick' and target = t.u(4)) = 0);
select t.as_user(4); set role authenticated;
select t.ok('server: banned cannot rejoin by code', t.run(format('select public.join_server(%L)', (select v from t.txt where k = 'code'))) like 'You are banned from this server until %: rude', 
  t.run(format('select public.join_server(%L)', (select v from t.txt where k = 'code'))));
select t.ok('server: banned cannot rejoin by direct insert', t.run(format('insert into public.server_members (server_id, member) values (%s, %L)', t.k('server'), t.u(4))) like '%row-level security%');
select t.ok('server: banned cannot post', t.run(format('insert into public.channel_messages (channel_id, body) values (%s, %L)', t.k('srvchan'), 'still here')) like '%row-level security%');
select t.ok('server: banned cannot read', t.n(format('select count(*) from public.channel_messages where channel_id = %s', t.k('srvchan'))) = 0);
select t.ok('server: banned sees own ban row only', t.n('select count(*) from public.community_bans') = 1);
select t.ok('server: banned cannot lift own ban', t.unban('server', 4) like '%owner or an admin%');
select t.ok('server: banned cannot delete own ban row', t.run(format('delete from public.community_bans where user_id = %L', t.u(4))) = 'ok:0');
reset role;
select t.as_user(6); set role authenticated;
select t.ok('server: bystander sees no bans', t.n('select count(*) from public.community_bans') = 0);
select t.ok('server: bystander cannot ban', t.ban('server', 7) like '%owner or an admin%');
select t.ok('server: bystander cannot insert a ban', t.run(format('insert into public.community_bans (scope, container_id, user_id) values (%L, %s, %L)', 'server', t.k('server'), t.u(7))) like '%row-level security%');
select t.ok('server: bystander cannot unban', t.unban('server', 4) like '%owner or an admin%');
select t.ok('server: bystander delete of ban removes nothing', t.run(format('delete from public.community_bans where user_id = %L', t.u(4))) = 'ok:0');
select t.ok('server: bystander can not read channel', t.n(format('select count(*) from public.channel_messages where channel_id = %s', t.k('srvchan'))) = 0);
reset role;
select t.ok('server: ban row survived the bystander', (select count(*) from public.community_bans where user_id = t.u(4)) = 1);
select t.as_user(5); set role authenticated;
select t.ok('server: ordinary member unaffected by another ban', t.run(format('insert into public.channel_messages (channel_id, body) values (%s, %L)', t.k('srvchan'), 'fine')) = 'ok:1');
select t.ok('server: member cannot see bans', t.n('select count(*) from public.community_bans') = 0);
reset role;
select t.as_user(2); set role authenticated;
select t.ok('server: admin sees the banned list', t.n(format('select count(*) from public.community_bans where container_id = %s', t.k('server'))) = 1);
select t.ok('server: owner/admin cannot ban an already-higher admin A via update path', t.ban('server', 1) like '%owner can''t%');
reset role;
-- expiry
update public.community_bans set until = now() - interval '1 minute' where user_id = t.u(4);
select t.as_user(4); set role authenticated;
select t.ok('server: expired ban lets the person rejoin', t.run(format('select public.join_server(%L)', (select v from t.txt where k = 'code'))) = 'ok:1');
reset role;
select t.ok('server: ...as a member', t.ismem('server', 4));
-- permanent ban + unban
select t.as_user(1); set role authenticated;
select t.ok('server: owner bans M permanently', t.ban('server', 4) = 'ok:1');
select t.ok('server: unban of a not-banned person is a clear error', t.unban('server', 5) like '%not banned%');
select t.ok('server: owner bans admin A', t.ban('server', 2) = 'ok:1');
reset role;
select t.ok('server: permanent ban has no end', (select until is null from public.community_bans where scope = 'server' and user_id = t.u(4)));
select t.as_user(2); set role authenticated;
select t.ok('server: banned admin cannot act any more', t.kick('server', 5) like '%owner or an admin%');
reset role;
select t.as_user(3); set role authenticated;
select t.ok('server: demoted A2 (kicked) outside cannot ban', t.ban('server', 5) like '%owner or an admin%');
reset role;
select t.as_user(1); set role authenticated;
select t.ok('server: owner unbans M', t.unban('server', 4) = 'ok:1');
select t.ok('server: owner unbans A', t.unban('server', 2) = 'ok:1');
reset role;
select t.ok('server: unban audited', (select count(*) from public.admin_log where action = 'unban' and target = t.u(4)) = 1);
select t.as_user(4); set role authenticated;
select t.ok('server: unbanned person can rejoin', t.run(format('select public.join_server(%L)', (select v from t.txt where k = 'code'))) = 'ok:1');
reset role;

-- ---- Groups -----------------------------------------------------------------
select t.as_user(1); set role authenticated;
insert into t.kv select 'group', id from public.create_group('Test group', array[t.u(2), t.u(3), t.u(4), t.u(5)]);
reset role;
insert into t.kv select 'grpmsgs', 0;
select t.ok('group: owner row has role owner', (select role from public.group_members where convo_id = t.k('group') and member = t.u(1)) = 'owner');
select t.ok('group: added members are plain members', (select count(*) from public.group_members where convo_id = t.k('group') and role = 'member') = 4);
select t.as_user(1); set role authenticated;
select t.ok('group: owner promotes A', t.role('group', 2, 'admin') = 'ok:1');
select t.ok('group: owner promotes A2', t.role('group', 3, 'admin') = 'ok:1');
reset role;
select t.as_user(4); set role authenticated;
select t.ok('group: member cannot kick member', t.kick('group', 5) like '%owner or an admin%');
select t.ok('group: member cannot ban', t.ban('group', 5) like '%owner or an admin%');
select t.ok('group: member cannot self-promote', t.run(format('update public.group_members set role = %L where convo_id = %s and member = %L', 'admin', t.k('group'), t.u(4))) like '%Roles can only be changed%');
select t.ok('group: member can still update own last_read', t.run(format('update public.group_members set last_read = 5 where convo_id = %s and member = %L', t.k('group'), t.u(4))) = 'ok:1');
select t.ok('group: member direct delete of another removes nothing', t.run(format('delete from public.group_members where convo_id = %s and member = %L', t.k('group'), t.u(5))) = 'ok:0');
reset role;
select t.as_user(2); set role authenticated;
select t.ok('group: admin cannot kick admin', t.kick('group', 3) like '%below your role%');
select t.ok('group: admin cannot kick owner', t.kick('group', 1) like '%owner can''t be removed%');
select t.ok('group: admin cannot ban owner', t.ban('group', 1) like '%owner can''t be removed%');
select t.ok('group: admin cannot promote', t.role('group', 5, 'admin') like '%Only the owner%');
select t.ok('group: admin direct delete of owner removes nothing', t.run(format('delete from public.group_members where convo_id = %s and member = %L', t.k('group'), t.u(1))) = 'ok:0');
select t.ok('group: admin kicks member M2', t.kick('group', 5) = 'ok:1');
select t.ok('group: admin bans member M', t.ban('group', 4, 86400) = 'ok:1');
reset role;
select t.ok('group: kicked and banned are gone', not t.ismem('group', 5) and not t.ismem('group', 4));
select t.ok('group: kick audited', (select count(*) from public.admin_log where action = 'kick' and target = t.u(5) and detail = 'group ' || t.k('group')) = 1);
select t.as_user(4); set role authenticated;
select t.ok('group: banned cannot post', t.run(format('insert into public.group_messages (convo_id, body) values (%s, %L)', t.k('group'), 'hi')) like '%row-level security%');
select t.ok('group: banned cannot read', t.n(format('select count(*) from public.group_messages where convo_id = %s', t.k('group'))) = 0);
select t.ok('group: banned cannot rejoin by direct insert', t.run(format('insert into public.group_members (convo_id, member) values (%s, %L)', t.k('group'), t.u(4))) like '%row-level security%');
select t.ok('group: banned cannot unban self', t.unban('group', 4) like '%owner or an admin%');
reset role;
select t.as_user(1); set role authenticated;
select t.ok('group: owner cannot re-add a banned person', t.run(format('select public.add_group_member(%s,%L)', t.k('group'), t.u(4))) like '%banned from this group%');
select t.ok('group: owner may re-add a kicked person', t.run(format('select public.add_group_member(%s,%L)', t.k('group'), t.u(5))) = 'ok:1');
select t.ok('group: owner lifts the ban', t.unban('group', 4) = 'ok:1');
select t.ok('group: owner re-adds the unbanned person', t.run(format('select public.add_group_member(%s,%L)', t.k('group'), t.u(4))) = 'ok:1');
select t.ok('group: owner kicks admin', t.kick('group', 3) = 'ok:1');
reset role;
select t.as_user(6); set role authenticated;
select t.ok('group: bystander cannot ban', t.ban('group', 4) like '%owner or an admin%');
select t.ok('group: bystander sees no bans', t.n('select count(*) from public.community_bans') = 0);
reset role;

-- ---- Cleanup / idempotence ---------------------------------------------------
insert into t.kv select 'logn', count(*) from public.admin_log;
select t.as_user(1); set role authenticated;
select t.ok('server: ban to be cleaned up', t.ban('server', 5) = 'ok:1');
select t.ok('server: owner deletes the server', t.run(format('delete from public.servers where id = %s', t.k('server'))) = 'ok:1');
reset role;
select t.ok('deleting a server removes its bans', (select count(*) from public.community_bans where scope = 'server') = 0);
select t.ok('deleting a server logs no kick or unban (only the ban itself)', (select count(*) from public.admin_log) = t.k('logn') + 1);
