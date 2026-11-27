-- LOCAL test of docs/sql/admin.sql (and the staff rule in forum_sections.sql).
-- Each check prints "PASS <name>" or "FAIL <name>: <why>" as a WARNING.
-- Players: alice, bob, carol, dave (four_players.sql). Only made-up values.
\ir four_players.sql

-- ---- Staff may verify a player, and change nothing else on their profile ------
reset role;
insert into public.profile_badges (owner, badge) values (:'C', 'staff');
update public.profiles set bio = 'bob bio', verified = false where id = :'B';

select t.as_user(:'C');
set role authenticated;
select t.ok('staff verifies a player', t.try(format($q$update public.profiles set verified = true where id = %L$q$, :'B')) = 'ok');
select t.ok('staff cannot change another player''s bio',
  t.try(format($q$update public.profiles set bio = 'hacked' where id = %L$q$, :'B')) <> 'ok');
select t.ok('staff cannot rename another player',
  t.try(format($q$update public.profiles set username = 'bob2' where id = %L$q$, :'B')) <> 'ok');
select t.ok('staff cannot sneak a bio change in with a verify',
  t.try(format($q$update public.profiles set verified = false, bio = 'x' where id = %L$q$, :'B')) <> 'ok');
reset role;
select t.ok('bob''s profile kept its text', (select bio = 'bob bio' and username = 'bob' and verified from public.profiles where id = :'B'));

select t.as_user(:'B');
set role authenticated;
select t.ok('a player still edits their own bio', t.try(format($q$update public.profiles set bio = 'mine' where id = %L$q$, :'B')) = 'ok');
reset role;

-- =============================================================================
-- Round A2 (docs/platform/okay_backend_admin.md sections 10 and 11): roles,
-- permissions, the extended admin_log, admin_gate, rate limits, TOTP storage.
-- Calls go through t.val / t.try so a missing piece is a FAIL, not a stop.
-- =============================================================================
reset role;
\set OW '00000000-0000-0000-0000-0000000000e1'
\set AD '00000000-0000-0000-0000-0000000000e2'
\set MO '00000000-0000-0000-0000-0000000000e3'
\set SU '00000000-0000-0000-0000-0000000000e4'
insert into auth.users (id, email) values (:'OW', 'owner@x'), (:'AD', 'adm@x'), (:'MO', 'mod@x'), (:'SU', 'sup@x');
update public.profiles set username = split_part((select email from auth.users u where u.id = profiles.id), '@', 1)
 where id in (:'OW', :'AD', :'MO', :'SU');
insert into public.profile_badges (owner, badge) values (:'AD', 'admin'), (:'MO', 'moderator');

-- ---- Roles: owner (4, SQL editor only) and support (0) ------------------------
select t.ok('the SQL editor makes an owner', t.try(format($q$insert into public.profile_badges (owner, badge) values (%L, 'owner')$q$, :'OW')) = 'ok');
select t.ok('the SQL editor makes a support member', t.try(format($q$insert into public.profile_badges (owner, badge) values (%L, 'support')$q$, :'SU')) = 'ok');
select t.ok('owner ranks 4', t.val(format('public.role_rank(%L)', :'OW')) = '4');
select t.ok('admin still ranks 3', t.val(format('public.role_rank(%L)', :'AD')) = '3');
select t.ok('support ranks 0 (no power over the row rules)', t.val(format('public.role_rank(%L)', :'SU')) = '0');
select t.ok('is_admin includes the owner', t.val(format('public.is_admin(%L)', :'OW')) = 'true');
select t.ok('support is not a moderator', t.val(format('public.is_global_mod(%L)', :'SU')) = 'false');

select t.as_user(:'AD'); set role authenticated;
select t.ok('an admin cannot make an owner', t.try(format($q$insert into public.profile_badges (owner, badge) values (%L, 'owner')$q$, :'A')) <> 'ok');
select t.ok('an admin cannot make another admin (owner only now)', t.try(format($q$insert into public.profile_badges (owner, badge) values (%L, 'admin')$q$, :'A')) <> 'ok');
select t.ok('an admin makes staff', t.try(format($q$insert into public.profile_badges (owner, badge) values (%L, 'staff')$q$, :'D')) = 'ok');
select t.ok('an admin cannot take away the owner badge',
  t.cnt(format($q$select 1 from public.profile_badges where owner = %L and badge = 'owner'$q$, :'OW')) = 1
  and t.try(format($q$delete from public.profile_badges where owner = %L and badge = 'owner'$q$, :'OW')) = 'ok'
  and (select count(*) from public.profile_badges where owner = :'OW' and badge = 'owner') = 1);
reset role;
select t.ok('the owner badge is still there', (select count(*) from public.profile_badges where owner = :'OW' and badge = 'owner') = 1);

select t.as_user(:'OW'); set role authenticated;
select t.ok('my_role_rank is 4 for the owner', t.val('public.my_role_rank()') = '4');
select t.ok('even the owner cannot make an owner through the API', t.try(format($q$insert into public.profile_badges (owner, badge) values (%L, 'owner')$q$, :'A')) <> 'ok');
select t.ok('the owner makes an admin', t.try(format($q$insert into public.profile_badges (owner, badge) values (%L, 'admin')$q$, :'A')) = 'ok');
select t.ok('the owner removes that admin', t.try(format($q$delete from public.profile_badges where owner = %L and badge = 'admin'$q$, :'A')) = 'ok');
reset role;

select t.as_user(:'C'); set role authenticated;   -- carol is staff
select t.ok('staff make a support member', t.try(format($q$insert into public.profile_badges (owner, badge) values (%L, 'support')$q$, :'A')) = 'ok');
select t.ok('staff cannot make staff', t.try(format($q$insert into public.profile_badges (owner, badge) values (%L, 'staff')$q$, :'A')) <> 'ok');
reset role;
delete from public.profile_badges where owner = :'A';

-- ---- Permissions: a table, read through admin_can ----------------------------
select t.ok('support sees the dashboard', t.val(format($q$public.admin_can(%L, 'dashboard.view')$q$, :'SU')) = 'true');
select t.ok('support does not read the audit log', t.val(format($q$public.admin_can(%L, 'audit.read')$q$, :'SU')) = 'false');
select t.ok('a moderator bans players', t.val(format($q$public.admin_can(%L, 'users.ban')$q$, :'MO')) = 'true');
select t.ok('a moderator does not verify', t.val(format($q$public.admin_can(%L, 'users.verify')$q$, :'MO')) = 'false');
select t.ok('staff verify', t.val(format($q$public.admin_can(%L, 'users.verify')$q$, :'C')) = 'true');
select t.ok('an admin cannot delete accounts', t.val(format($q$public.admin_can(%L, 'auth.delete')$q$, :'AD')) = 'false');
select t.ok('the owner deletes accounts', t.val(format($q$public.admin_can(%L, 'auth.delete')$q$, :'OW')) = 'true');
select t.ok('a player has no admin permission', t.val(format($q$public.admin_can(%L, 'dashboard.view')$q$, :'A')) = 'false');
select t.ok('nobody holds an unknown permission', t.val(format($q$public.admin_can(%L, 'no.such')$q$, :'OW')) = 'false');
select t.ok('no caller, no permission', t.val($q$public.admin_can(null, 'dashboard.view')$q$) = 'false');
select t.ok('admin_permissions_of lists support''s reads',
  t.val(format($q$(select string_agg(p, ',' order by p) from public.admin_permissions_of(%L) p)$q$, :'SU'))
  = 'dashboard.view,network.read,profiles.edit,providers.read,users.read,users.view_as');

-- The table decides: take audit.read away from moderators, and they lose the log.
delete from public.admin_role_permissions where role = 'moderator' and permission = 'audit.read';
select t.ok('a permission removed from the table is gone', t.val(format($q$public.admin_can(%L, 'audit.read')$q$, :'MO')) = 'false');
-- The owner holds every permission even with no rows (cannot lock themselves out).
delete from public.admin_role_permissions where role = 'owner';
select t.ok('the owner keeps every permission', t.val(format($q$public.admin_can(%L, 'permissions.manage')$q$, :'OW')) = 'true');
-- Re-running admin.sql keeps the owner's edits (only new permissions are seeded).
\ir ../../docs/sql/admin.sql
set client_min_messages = warning;
reset role;
select t.ok('re-running admin.sql keeps a removed permission removed',
  (select count(*) from public.admin_role_permissions where role = 'moderator' and permission = 'audit.read') = 0);
insert into public.admin_role_permissions (role, permission) values ('moderator', 'audit.read');

select t.as_user(:'SU'); set role authenticated;
select t.ok('support reads the role table', t.cnt('select 1 from public.admin_role_permissions') > 10);
select t.ok('support cannot change the role table', t.try($q$insert into public.admin_role_permissions (role, permission) values ('support', 'auth.delete')$q$) <> 'ok');
reset role;
select t.as_user(:'OW'); set role authenticated;
select t.ok('the owner changes the table only through the server, not the API', t.try($q$delete from public.admin_role_permissions where role = 'support'$q$) <> 'ok'
  or (select count(*) from public.admin_role_permissions where role = 'support') > 0);
reset role;
select t.ok('support rows survived', (select count(*) from public.admin_role_permissions where role = 'support') = 6);
select t.as_user(:'A'); set role authenticated;
select t.ok('a player sees no permission rows', t.cnt('select 1 from public.admin_role_permissions') = 0);
reset role;

-- ---- admin_log: new columns, filled by the triggers ---------------------------
select t.ok('admin_log has the new columns',
  (select count(*) from information_schema.columns where table_schema = 'public' and table_name = 'admin_log'
     and column_name in ('before', 'after', 'source', 'request_id', 'permission', 'outcome', 'ip', 'user_agent')) = 8);

select t.as_user(:'OW'); set role authenticated;
select set_config('request.headers', '{"x-okay-source":"dashboard","x-okay-request-id":"11111111-2222-3333-4444-555555555555","x-okay-client-ip":"203.0.113.7","x-okay-user-agent":"TestAgent/1.0"}', false);
insert into public.profile_badges (owner, badge) values (:'B', 'beta');
select set_config('request.headers', '', false);
reset role;
select t.ok('role_add row: permission, after, outcome',
  (select permission = 'roles.manage' and outcome = 'ok' and after ->> 'badge' = 'beta' and before is null
     from public.admin_log where action = 'role_add' and target = :'B' order by id desc limit 1));
select t.ok('role_add row: source, request id, address, agent from the request',
  (select source = 'dashboard' and request_id = '11111111-2222-3333-4444-555555555555' and ip = '203.0.113.7'::inet and user_agent = 'TestAgent/1.0'
     from public.admin_log where action = 'role_add' and target = :'B' order by id desc limit 1));

select t.as_user(:'MO'); set role authenticated;
select set_config('request.headers', '{"x-okay-source":"sql","x-okay-request-id":"nope","x-okay-client-ip":"not-an-ip","user-agent":"Launcher/9"}', false);
insert into public.site_bans (user_id, reason) values (:'B', 'spam');
select set_config('request.headers', '', false);
reset role;
select t.ok('a client cannot claim source sql; bad id and address are dropped',
  (select source = 'launcher' and request_id is null and ip is null and user_agent = 'Launcher/9'
     from public.admin_log where action = 'ban' and target = :'B' order by id desc limit 1));
select t.ok('ban row: permission and after',
  (select permission = 'users.ban' and after ->> 'reason' = 'spam' and before is null
     from public.admin_log where action = 'ban' and target = :'B' order by id desc limit 1));
select t.as_user(:'MO'); set role authenticated;
delete from public.site_bans where user_id = :'B';
reset role;
select t.ok('unban row: before',
  (select before ->> 'reason' = 'spam' and after is null from public.admin_log where action = 'unban' and target = :'B' order by id desc limit 1));

select t.as_user(:'C'); set role authenticated;
update public.profiles set verified = false where id = :'B';
reset role;
select t.ok('unverify row: before and after',
  (select (before ->> 'verified')::boolean and not (after ->> 'verified')::boolean and permission = 'users.verify'
     from public.admin_log where action = 'unverify' and target = :'B' order by id desc limit 1));

-- A moderator's note: the server fills every field it owns.
select t.as_user(:'MO'); set role authenticated;
select t.ok('a moderator writes a note', t.try(format($q$insert into public.admin_log (actor, action, detail, outcome, created_at, ip, before, permission)
  values (%L, 'note', 'looked at bob', 'refused', now() - interval '400 days', '198.51.100.1', '{"x":1}', 'auth.delete')$q$, :'MO')) = 'ok');
reset role;
select t.ok('the note''s outcome, time, address, before and permission are the server''s',
  (select outcome = 'ok' and created_at > now() - interval '1 minute' and ip is null and before is null and permission is null and actor_name = 'mod'
     from public.admin_log where action = 'note' order by id desc limit 1));

-- Who reads what.
select t.as_user(:'MO'); set role authenticated;
select t.ok('the launcher''s log query still works', t.try('select actor_name, action, target_name, detail, created_at from public.admin_log') = 'ok');
select t.ok('the dashboard columns are readable', t.try('select before, after, source, request_id, permission, outcome from public.admin_log') = 'ok');
select t.ok('addresses are not readable by clients', t.try('select ip from public.admin_log') like '42501:%');
select t.ok('user agents are not readable by clients', t.try('select user_agent from public.admin_log') like '42501:%');
reset role;
select t.as_user(:'SU'); set role authenticated;
select t.ok('support does not read the log', t.cnt('select id from public.admin_log') = 0);
reset role;

-- ---- admin_log is append-only (the shared ac_audit trigger) -------------------
select t.as_user(:'AD'); set role authenticated;
select t.ok('an admin cannot edit a log row', t.try($q$update public.admin_log set detail = 'x'$q$) <> 'ok'
  or (select count(*) from public.admin_log where detail = 'x') = 0);
select t.ok('an admin cannot delete log rows', t.try('delete from public.admin_log') <> 'ok' or t.cnt('select id from public.admin_log') > 0);
reset role;
select t.ok('the SQL editor cannot edit a log row', t.try($q$update public.admin_log set action = 'x' where action = 'note'$q$) like '42501:%append-only%');
select t.ok('the SQL editor cannot delete a log row', t.try($q$delete from public.admin_log where action = 'note'$q$) like '42501:%append-only%');
select t.ok('the SQL editor cannot empty the log', t.try('truncate public.admin_log') like '42501:%append-only%');
do $$ begin
  perform t.as_service(); set local role service_role;
  perform t.ok('the service role cannot rewrite a name', t.try($q$update public.admin_log set actor_name = 'someone' where action = 'note'$q$) like '42501:%');
  perform t.ok('the service role cannot backdate a row', t.try($q$update public.admin_log set created_at = now() - interval '1 year' where action = 'note'$q$) like '42501:%');
end $$;
-- Account deletion (supabase/functions/delete-account) may still empty the link to a person.
do $$ begin
  perform t.as_service(); set local role service_role;
  perform t.ok('account deletion empties the actor', t.try($q$update public.admin_log set actor = null, actor_name = '[deleted]' where actor = '00000000-0000-0000-0000-0000000000e1'$q$) = 'ok');
  perform t.ok('account deletion empties the target', t.try($q$update public.admin_log set target = null, target_name = '[deleted]' where target = '00000000-0000-0000-0000-0000000000b2'$q$) = 'ok');
end $$;
select t.ok('emptying the actor also drops their address and agent',
  (select count(*) from public.admin_log where actor_name = '[deleted]' and (ip is not null or user_agent is not null)) = 0
  and (select count(*) from public.admin_log where actor_name = '[deleted]') > 0);
select t.ok('emptying the target also drops before and after',
  (select count(*) from public.admin_log where target_name = '[deleted]' and (before is not null or after is not null)) = 0);
select t.ok('ac_audit still refuses edits through the shared trigger', t.try('truncate public.ac_audit') like '42501:The anti-cheat audit log is append-only%');
select t.ok('the old ac_audit_immutable copy is gone', to_regprocedure('public.ac_audit_immutable()') is null);
select t.ok('one append-only function serves both logs',
  (select count(distinct tgfoid) from pg_trigger where tgrelid in ('public.admin_log'::regclass, 'public.ac_audit'::regclass)
     and tgfoid = to_regprocedure('public.audit_append_only()')) = 1
  and (select count(*) from pg_trigger where tgfoid = to_regprocedure('public.audit_append_only()')) = 4);

-- ---- admin_gate: permission + rate limit, refusals logged ---------------------
select t.as_user(:'A'); set role authenticated;
select t.ok('a player is refused', t.val($q$public.admin_gate('dashboard.view', 'open dashboard', null)$q$) = 'forbidden');
reset role;
select t.ok('the refusal is logged',
  (select outcome = 'refused' and permission = 'dashboard.view' and detail = 'forbidden' and actor = :'A'
     from public.admin_log where actor = :'A' order by id desc limit 1));
select t.as_user(:'SU'); set role authenticated;
select t.ok('support passes the dashboard gate', t.val($q$public.admin_gate('dashboard.view', null, null)$q$) = 'ok');
select t.ok('an unknown permission is refused', t.val($q$public.admin_gate('no.such', null, null)$q$) = 'forbidden');
reset role;
select t.as_user(:'OW'); set role authenticated;
select t.ok('ten dangerous actions an hour pass',
  (select bool_and(public.admin_gate('auth.delete', 'delete account', null) = 'ok') from generate_series(1, 10)));
select t.ok('the eleventh is rate limited', t.val($q$public.admin_gate('auth.delete', 'delete account', null)$q$) = 'rate_limited');
select t.ok('reads have their own limit', t.val($q$public.admin_gate('users.read', null, null)$q$) = 'ok');
reset role;
select t.ok('the rate-limited attempt is logged',
  (select outcome = 'refused' and detail = 'rate_limited' and permission = 'auth.delete'
     from public.admin_log where actor = :'OW' order by id desc limit 1));
select t.as_user(:'D'); set role authenticated;   -- dave is staff now; probing what he lacks
select t.ok('repeated probing is still refused',
  (select bool_and(public.admin_gate('auth.delete', null, null) = 'forbidden') from generate_series(1, 30)));
reset role;
select t.ok('but fills the log with at most 20 rows in 10 minutes',
  (select count(*) from public.admin_log where actor = :'D' and outcome = 'refused') = 20);
do $$ begin
  perform t.as_service(); set local role service_role;
  perform t.ok('the service role has no admin identity at the gate', public.admin_gate('dashboard.view', null, null) = 'signed_out');
end $$;
select t.as_anon(); set role anon;
select t.ok('anon cannot call the gate', t.try($q$select public.admin_gate('dashboard.view', null, null)$q$) like '42501:%');
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('clients cannot call the rate limiter', t.try($q$select public.ac_rate_hit('x', 1, interval '1 minute')$q$) like '42501:%');
select t.ok('clients cannot call the full log writer', t.try($q$select public.admin_log_write('x', null, 'y', null, null, null, 'ok')$q$) like '42501:%');
reset role;

-- ---- admin_log_record: the server function's writer (service role only) ------
select t.as_user(:'OW'); set role authenticated;
select t.ok('even the owner cannot call admin_log_record', t.try($q$select public.admin_log_record(null, 'x', null, null, null, null, 'dashboard', null, null, 'ok', null, null)$q$) like '42501:%');
reset role;
do $$ begin
  perform t.as_service(); set local role service_role;
  perform t.ok('the service role records a server-side action',
    t.try($q$select public.admin_log_record('00000000-0000-0000-0000-0000000000e2', 'auth_ban', '00000000-0000-0000-0000-0000000000c3', '7 days',
           '{"banned_until":null}', '{"banned_until":"2026-12-04"}', 'dashboard', '99999999-8888-7777-6666-555555555555', 'auth.ban', 'refused', '192.0.2.5', 'Agent')$q$) = 'ok');
  perform t.ok('a bad outcome is refused', t.try($q$select public.admin_log_record(null, 'x', null, null, null, null, 'dashboard', null, null, 'maybe', null, null)$q$) <> 'ok');
  perform t.ok('a bad source is refused', t.try($q$select public.admin_log_record(null, 'x', null, null, null, null, 'web', null, null, 'ok', null, null)$q$) <> 'ok');
  perform t.ok('the service role reads addresses', t.cnt($q$select ip from public.admin_log where ip = '192.0.2.5'$q$) = 1);
end $$;
select t.ok('the recorded row has every field',
  (select actor_name = 'adm' and target_name = 'carol' and source = 'dashboard' and outcome = 'refused' and permission = 'auth.ban'
          and after ->> 'banned_until' = '2026-12-04' and request_id = '99999999-8888-7777-6666-555555555555'
     from public.admin_log where action = 'auth_ban'));

-- ---- Retention: addresses go after 90 days, the rows stay ---------------------
insert into public.admin_log (actor, action, detail, source, outcome, ip, user_agent, created_at)
values (:'AD', 'old_action', 'old', 'dashboard', 'ok', '198.51.100.9', 'OldAgent', now() - interval '100 days'),
       (:'AD', 'new_action', 'new', 'dashboard', 'ok', '198.51.100.10', 'NewAgent', now() - interval '10 days');
select t.as_user(:'AD'); set role authenticated;
select t.ok('an admin cannot run the retention job', t.try('select public.admin_log_purge_old()') <> 'ok');
reset role;
select set_config('request.jwt.claims', '', false);   -- the SQL editor: no caller
select t.ok('the SQL editor runs the retention job', t.val('public.admin_log_purge_old()') = '1', t.val('public.admin_log_purge_old()'));
select t.ok('old addresses are gone, the row stays',
  (select ip is null and user_agent is null and detail = 'old' from public.admin_log where action = 'old_action'));
select t.ok('recent addresses stay', (select ip = '198.51.100.10'::inet from public.admin_log where action = 'new_action'));
select t.ok('the retention job leaves one line', (select count(*) from public.admin_log where action = 'log_retention') = 1);

-- ---- Overview numbers and bans by permission ----------------------------------
select t.as_user(:'SU'); set role authenticated;
select t.ok('support sees the overview numbers', t.cnt('select * from public.admin_stats()') = 1);
select t.ok('support cannot ban', t.try(format($q$insert into public.site_bans (user_id, reason) values (%L, 'x')$q$, :'A')) <> 'ok');
reset role;

-- ---- TOTP secrets: stored for the server function, never readable by a client -
select t.ok('no client role has any privilege on the TOTP table',
  not has_any_column_privilege('anon', 'public.admin_totp_secrets', 'select, insert, update, references')
  and not has_any_column_privilege('authenticated', 'public.admin_totp_secrets', 'select, insert, update, references')
  and not has_table_privilege('authenticated', 'public.admin_totp_secrets', 'delete, truncate'));
do $$ begin
  perform t.as_service(); set local role service_role;
  perform t.ok('the service role stores a secret',
    t.try($q$insert into public.admin_totp_secrets (user_id, secret) values ('00000000-0000-0000-0000-0000000000e1', decode(repeat('5a', 20), 'hex'))$q$) = 'ok');
  perform t.ok('a too-short secret is refused',
    t.try($q$insert into public.admin_totp_secrets (user_id, secret) values ('00000000-0000-0000-0000-0000000000e2', decode('0102', 'hex'))$q$) <> 'ok');
  perform t.ok('a code step is accepted once', public.admin_totp_use_step('00000000-0000-0000-0000-0000000000e1', 100));
  perform t.ok('the same step is refused (replay)', not public.admin_totp_use_step('00000000-0000-0000-0000-0000000000e1', 100));
  perform t.ok('an older step is refused', not public.admin_totp_use_step('00000000-0000-0000-0000-0000000000e1', 99));
  perform t.ok('a newer step is accepted', public.admin_totp_use_step('00000000-0000-0000-0000-0000000000e1', 101));
end $$;
select t.as_user(:'OW'); set role authenticated;
select t.ok('the owner cannot read their own secret', t.try('select secret from public.admin_totp_secrets') like '42501:%');
select t.ok('the owner cannot read any column', t.try('select user_id from public.admin_totp_secrets') like '42501:%');
select t.ok('a client cannot write a secret', t.try(format($q$insert into public.admin_totp_secrets (user_id, secret) values (%L, decode(repeat('00', 20), 'hex'))$q$, :'OW')) like '42501:%');
select t.ok('a client cannot mark a step used', t.try(format($q$select public.admin_totp_use_step(%L, 500)$q$, :'OW')) like '42501:%');
reset role;
select t.as_anon(); set role anon;
select t.ok('anon cannot read secrets', t.try('select secret from public.admin_totp_secrets') like '42501:%');
reset role;
select t.ok('no function hands a secret to a client',
  not exists (select 1 from pg_proc p join pg_namespace n on n.oid = p.pronamespace
              where n.nspname = 'public' and p.prosrc ilike '%admin_totp_secrets%'
                and (has_function_privilege('authenticated', p.oid, 'execute') or has_function_privilege('anon', p.oid, 'execute'))));
delete from auth.users where id = :'OW';
select t.ok('deleting the account deletes its secret', (select count(*) from public.admin_totp_secrets) = 0);

-- =============================================================================
-- Game and community controls (design section 18): permissions, the word
-- filter, and app_config's controlled keys (maintenance, banner, switches,
-- minimum versions). AD is admin, MO moderator, SU support, C and D staff.
-- (The owner's account was deleted above; the owner is checked through admin_can.)
-- =============================================================================
reset role;
select set_config('request.jwt.claims', '', false);
insert into auth.users (id, email) values ('00000000-0000-0000-0000-0000000000f1', 'owner2@x');
update public.profiles set username = 'owner2' where id = '00000000-0000-0000-0000-0000000000f1';
insert into public.profile_badges (owner, badge) values ('00000000-0000-0000-0000-0000000000f1', 'owner');
\set O2 '00000000-0000-0000-0000-0000000000f1'

select t.ok('owner-only console powers stay with the owner',
  (select bool_and(public.admin_can(:'O2', p) and not public.admin_can(:'AD', p))
     from unnest(array['sql.read', 'sql.write', 'schema.manage', 'backups.manage', 'functions.deploy', 'functions.secrets',
                       'auth.settings', 'auth.end_sessions', 'storage.manage', 'network.manage', 'releases.pull']) p));
select t.ok('admins hold the game and community controls',
  (select bool_and(public.admin_can(:'AD', p))
     from unnest(array['game.maintenance', 'game.announce', 'game.min_version', 'game.features', 'users.mass_action', 'users.mute',
                       'chat.filter', 'chat.moderate', 'profiles.edit', 'users.view_as', 'servers.control', 'rooms.kick']) p));
select t.ok('moderators: mute, chat moderation, room kicks; not switches or mass actions',
  public.admin_can(:'MO', 'users.mute') and public.admin_can(:'MO', 'chat.moderate') and public.admin_can(:'MO', 'rooms.kick')
  and not public.admin_can(:'MO', 'game.features') and not public.admin_can(:'MO', 'users.mass_action') and not public.admin_can(:'MO', 'chat.filter'));
select t.ok('support: profile fields and read-only player view only',
  public.admin_can(:'SU', 'profiles.edit') and public.admin_can(:'SU', 'users.view_as')
  and not public.admin_can(:'SU', 'users.mute') and not public.admin_can(:'SU', 'game.announce'));
select t.ok('every dangerous power is danger-kind (10 an hour)',
  (select bool_and(kind = 'danger') from public.admin_permissions
    where permission in ('sql.write', 'game.maintenance', 'game.min_version', 'game.features', 'users.mass_action', 'servers.control', 'releases.pull')));

-- ---- Word filter ----------------------------------------------------------------
select t.as_user(:'C'); set role authenticated;   -- staff
select t.ok('staff add a filtered word', t.try(format($q$insert into public.chat_word_filter (pattern, action, added_by) values ('badword', 'block', %L)$q$, :'C')) = 'ok');
select t.ok('a filtered word must be lower case', t.try(format($q$insert into public.chat_word_filter (pattern, added_by) values ('BadWord', %L)$q$, :'C')) <> 'ok');
select t.ok('a word cannot be added in someone else''s name', t.try(format($q$insert into public.chat_word_filter (pattern, added_by) values ('other', %L)$q$, :'AD')) <> 'ok');
select t.ok('words are not edited in place', t.try($q$update public.chat_word_filter set action = 'mask'$q$) like '42501:%');
reset role;
select t.ok('adding a word is logged with after',
  (select permission = 'chat.filter' and after ->> 'pattern' = 'badword' and actor = :'C'
     from public.admin_log where action = 'chat_word_filter_insert' order by id desc limit 1));
select t.as_user(:'MO'); set role authenticated;
select t.ok('moderators read the filter', t.cnt('select 1 from public.chat_word_filter') = 1);
select t.ok('moderators cannot change it', t.try(format($q$insert into public.chat_word_filter (pattern, added_by) values ('x', %L)$q$, :'MO')) <> 'ok');
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('players cannot read the filter', t.cnt('select 1 from public.chat_word_filter') = 0);
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('word filter edits count against the write limit',
  (select count(*) from generate_series(1, 35) i
    where t.try(format($q$insert into public.chat_word_filter (pattern, added_by) values ('w%s', %L)$q$, i, :'C')) = 'ok') between 25 and 29);
reset role;

-- ---- app_config: public reads, controlled keys only through the server --------
select t.as_anon(); set role anon;
select t.ok('anyone reads maintenance, banner, switches, minimum versions',
  t.cnt($q$select 1 from public.app_config where key in ('maintenance', 'announcement', 'announcement.level', 'feature.chat', 'feature.signups', 'min_version.launcher', 'min_version.game')$q$) = 7);
select t.ok('anyone reads which keys are controlled', t.cnt('select 1 from public.app_config_keys') = 11);
select t.ok('anon cannot change a setting', t.try($q$update public.app_config set value = 'on' where key = 'maintenance'$q$) like '42501:%'
  or (select value from public.app_config where key = 'maintenance') = 'off');
reset role;
select t.as_user(:'O2'); set role authenticated;
select t.ok('even the owner cannot set a controlled key through the API',
  t.try($q$update public.app_config set value = 'on' where key = 'maintenance'$q$) <> 'ok'
  or (select value from public.app_config where key = 'maintenance') = 'off');
reset role;
select t.ok('maintenance is still off', (select value from public.app_config where key = 'maintenance') = 'off');
select t.as_user(:'AD'); set role authenticated;
select t.ok('an admin still sets a free key (the launcher''s voice server)',
  t.try($q$insert into public.app_config (key, value) values ('voice_relay', 'relay.example.test:4000') on conflict (key) do update set value = excluded.value$q$) = 'ok');
reset role;
select t.ok('the free key change is logged with before and after',
  (select before ->> 'value' = '' and after ->> 'value' = 'relay.example.test:4000' and permission = 'config.write'
     from public.admin_log where action = 'app_config_update' order by id desc limit 1));
select t.as_user(:'C'); set role authenticated;
select t.ok('staff cannot set a free key (config.write is admin)',
  t.try($q$update public.app_config set value = 'x' where key = 'voice_relay'$q$) <> 'ok'
  or (select value from public.app_config where key = 'voice_relay') = 'relay.example.test:4000');
reset role;

do $$
declare ad uuid := '00000000-0000-0000-0000-0000000000e2'; st uuid := '00000000-0000-0000-0000-0000000000d4';   -- dave, staff (carol used up her write limit above)
        su uuid := '00000000-0000-0000-0000-0000000000e4';
begin
  perform t.as_service(); set local role service_role;
  perform t.ok('maintenance needs the key typed back',
    public.admin_set_config(ad, 'maintenance', 'on', null, 'dashboard', null, '192.0.2.1', 'Agent') = 'confirm_needed');
  perform t.ok('maintenance on with the typed confirm',
    public.admin_set_config(ad, 'maintenance', 'on', 'maintenance', 'dashboard', null, '192.0.2.1', 'Agent') = 'ok');
  perform t.ok('a value outside the pattern is refused',
    public.admin_set_config(ad, 'maintenance', 'maybe', 'maintenance', 'dashboard', null, null, null) = 'bad_value');
  perform t.ok('a bad version is refused',
    public.admin_set_config(ad, 'min_version.launcher', '1.2', 'min_version.launcher', 'dashboard', null, null, null) = 'bad_value');
  perform t.ok('a minimum version is set',
    public.admin_set_config(ad, 'min_version.launcher', '0.9.12', 'min_version.launcher', 'dashboard', null, null, null) = 'ok');
  perform t.ok('a two-line banner is refused',
    public.admin_set_config(st, 'announcement', E'line one\nline two', null, 'dashboard', null, null, null) = 'bad_value');
  perform t.ok('staff set the banner without a typed confirm (not dangerous)',
    public.admin_set_config(st, 'announcement', 'Servers restart at 18:00 UTC', null, 'dashboard', null, null, null) = 'ok');
  perform t.ok('staff cannot switch features off',
    public.admin_set_config(st, 'feature.chat', 'off', 'feature.chat', 'dashboard', null, null, null) = 'forbidden');
  perform t.ok('support cannot set the banner',
    public.admin_set_config(su, 'announcement', 'hi', null, 'dashboard', null, null, null) = 'forbidden');
  perform t.ok('an unknown key is refused',
    public.admin_set_config(ad, 'voice_relay', 'x', 'voice_relay', 'dashboard', null, null, null) = 'unknown_key');
  perform t.ok('no caller, no change',
    public.admin_set_config(null, 'feature.chat', 'off', 'feature.chat', 'dashboard', null, null, null) = 'signed_out');
end $$;
select t.ok('maintenance is on and the banner is set',
  (select value from public.app_config where key = 'maintenance') = 'on'
  and (select value from public.app_config where key = 'announcement') = 'Servers restart at 18:00 UTC');
select t.ok('the change is logged with before, after, permission and address',
  (select before ->> 'maintenance' = 'off' and after ->> 'maintenance' = 'on' and permission = 'game.maintenance'
          and ip = '192.0.2.1'::inet and actor = '00000000-0000-0000-0000-0000000000e2'
     from public.admin_log where action = 'config_set' and detail = 'maintenance' order by id limit 1));
select t.ok('the forbidden attempt is logged as refused (dave''s is not: his probing above used up his 20 refusal rows)',
  (select count(*) from public.admin_log where action = 'config_set' and outcome = 'refused') = 1
  and (select actor_name from public.admin_log where action = 'config_set' and outcome = 'refused') = 'sup');
select t.as_user(:'AD'); set role authenticated;
select t.ok('clients cannot call admin_set_config', t.try($q$select public.admin_set_config(null, 'maintenance', 'off', 'maintenance', 'dashboard', null, null, null)$q$) like '42501:%');
select t.ok('clients cannot call admin_gate_as', t.try($q$select public.admin_gate_as(null, 'auth.delete', null, null, 'dashboard', null, null, null)$q$) like '42501:%');
reset role;
do $$
declare ad uuid := '00000000-0000-0000-0000-0000000000e2'; n int := 0;
begin
  perform t.as_service(); set local role service_role;
  for i in 1..12 loop
    if public.admin_set_config(ad, 'feature.uploads', case when i % 2 = 0 then 'on' else 'off' end, 'feature.uploads',
                               'dashboard', null, null, null) = 'rate_limited' then n := n + 1; end if;
  end loop;
  perform t.ok('dangerous switches are rate limited (10 an hour, counting the earlier ones)', n >= 2, n::text);
end $$;
do $$ begin
  perform t.as_service(); set local role service_role;
  perform t.ok('a banner over 300 characters is refused',
    public.admin_set_config('00000000-0000-0000-0000-0000000000f1', 'announcement', repeat('a', 301), null, 'dashboard', null, null, null) = 'bad_value');
end $$;
