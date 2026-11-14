-- LOCAL test of docs/sql/anticheat.sql (review items #270.1-4). Each check prints
-- "PASS <name>" or "FAIL <name>: <why>" as a WARNING.
\set QUIET on
set client_min_messages = warning;

-- Users: admin, two players (cheater, and the cheater's new account), one bystander.
reset role;
insert into auth.users (id, email) values
  ('00000000-0000-0000-0000-00000000000a', 'admin@x'),
  ('00000000-0000-0000-0000-0000000000c1', 'cheater@x'),
  ('00000000-0000-0000-0000-0000000000c2', 'newacct@x'),
  ('00000000-0000-0000-0000-0000000000b1', 'bystander@x');
insert into public.profile_badges (owner, badge) values ('00000000-0000-0000-0000-00000000000a', 'admin');

create schema if not exists t;
create or replace function t.ok(name text, cond boolean, why text default '') returns void language plpgsql as $$
begin
  if cond then raise warning 'PASS %', name; else raise warning 'FAIL %: %', name, why; end if;
end $$;
create or replace function t.as_user(uid text) returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('sub', uid, 'role', 'authenticated')::text, false);
end $$;
create or replace function t.as_service() returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('role', 'service_role')::text, false);
end $$;
create or replace function t.as_anon() returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('role', 'anon')::text, false);
end $$;
grant usage on schema t to public; grant execute on all functions in schema t to public;

-- Admin bans the cheater: account + machine, and a separate account-only ban; a report and an appeal.
select t.as_user('00000000-0000-0000-0000-00000000000a');
set role authenticated;
insert into public.ac_bans (user_id, hw_hash, reason) values
  ('00000000-0000-0000-0000-0000000000c1', repeat('ab', 32), 'speedhack (machine)');
insert into public.ac_bans (user_id, reason) values ('00000000-0000-0000-0000-0000000000c1', 'speedhack (account)');
insert into public.ac_bans (user_id, reason) values ('00000000-0000-0000-0000-0000000000b1', 'to edit');
reset role;
select t.as_user('00000000-0000-0000-0000-0000000000b1');
set role authenticated;
insert into public.ac_reports (reported, reason) values ('00000000-0000-0000-0000-0000000000c1', 'flying');
select public.ac_file_appeal((select id from public.ac_my_bans() limit 1), 'please');
reset role;

-- ---- #270.2 ac_is_banned: admins and service_role only ----------------------------
do $$
declare v boolean; denied boolean := false;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000b1');
  set local role authenticated;
  begin
    v := public.ac_is_banned('00000000-0000-0000-0000-0000000000c1', null);
  exception when insufficient_privilege then denied := true;
  end;
  perform t.ok('ac_is_banned refused to a plain player', denied, 'a player could look up anyone''s ban status');
end $$;
do $$
declare v boolean; denied boolean := false;
begin
  perform t.as_anon();
  set local role anon;
  begin v := public.ac_is_banned('00000000-0000-0000-0000-0000000000c1', null);
  exception when insufficient_privilege then denied := true; end;
  perform t.ok('ac_is_banned refused to anon', denied);
end $$;
do $$
declare v boolean;
begin
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  set local role authenticated;
  v := public.ac_is_banned('00000000-0000-0000-0000-0000000000c1', null);
  perform t.ok('ac_is_banned works for an admin', v);
exception when others then perform t.ok('ac_is_banned works for an admin', false, sqlerrm);
end $$;
do $$
declare v boolean;
begin
  perform t.as_service();
  set local role service_role;
  v := public.ac_is_banned(null, repeat('ab', 32));
  perform t.ok('ac_is_banned works for service_role', v);
exception when others then perform t.ok('ac_is_banned works for service_role', false, sqlerrm);
end $$;
do $$
declare n int;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000b1');
  set local role authenticated;
  select count(*) into n from public.ac_my_bans();
  perform t.ok('players still see their own bans (ac_my_bans)', n = 1, n::text);
end $$;

-- ---- #270.1 an empty hash never matches a machine ban ------------------------------
do $$
declare v boolean; n int; refused boolean := false;
begin
  perform t.as_service();
  v := public.ac_is_banned(null, '');
  perform t.ok('ac_is_banned(null, '''') is false', not v);
  perform t.as_user('00000000-0000-0000-0000-0000000000c2');
  set local role authenticated;
  select count(*) into n from public.ac_my_bans('');
  perform t.ok('ac_my_bans('''') finds no machine ban', n = 0, n::text);
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  begin insert into public.ac_bans (hw_hash, reason) values ('', 'empty');
  exception when check_violation then refused := true; end;
  perform t.ok('a ban on an empty hash is refused', refused);
end $$;

-- ---- #270.3 every admin action is logged -------------------------------------------
do $$
declare before_n int; b bigint; r bigint; a bigint;
begin
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  set local role authenticated;
  select id into b from public.ac_bans where reason = 'to edit';
  reset role;
  select count(*) into before_n from public.admin_log;
  set local role authenticated;
  update public.ac_bans set until = now() + interval '3 days' where id = b;
  reset role;
  perform t.ok('changing a ban''s until is logged', (select count(*) from public.admin_log) = before_n + 1
                     and exists (select 1 from public.admin_log where detail like '%ban #' || b || '%' and action like 'ac_ban%' and detail like '%until%'),
                     (select string_agg(action || ':' || coalesce(detail, ''), ' | ') from public.admin_log));
  select count(*) into before_n from public.admin_log;
  set local role authenticated;
  update public.ac_bans set reason = 'edited reason' where id = b;
  reset role;
  perform t.ok('changing a ban''s reason is logged', (select count(*) from public.admin_log) = before_n + 1);
  set local role authenticated;
  update public.ac_bans set lifted = true, lift_reason = 'first' where id = b;
  reset role;
  select count(*) into before_n from public.admin_log;
  set local role authenticated;
  update public.ac_bans set lift_reason = 'second thoughts' where id = b;
  reset role;
  perform t.ok('changing a lifted ban''s lift_reason is logged', (select count(*) from public.admin_log) = before_n + 1);
  select count(*) into before_n from public.admin_log;
  set local role authenticated;
  update public.ac_bans set reason = reason where id = b;   -- no change: nothing to log
  reset role;
  perform t.ok('a no-op update logs nothing', (select count(*) from public.admin_log) = before_n);

  select id into r from public.ac_reports limit 1;
  select count(*) into before_n from public.admin_log;
  set local role authenticated;
  delete from public.ac_reports where id = r;
  reset role;
  perform t.ok('an admin deleting a report is logged',
                     exists (select 1 from public.admin_log where action = 'ac_report_delete' and detail like '%#' || r || '%'));

  select id into a from public.ac_appeals limit 1;
  set local role authenticated;
  delete from public.ac_appeals where id = a;
  reset role;
  perform t.ok('an admin deleting an appeal is logged',
                     exists (select 1 from public.admin_log where action = 'ac_appeal_delete' and detail like '%#' || a || '%'));

  set local role authenticated;
  delete from public.ac_bans where id = b;
  reset role;
  perform t.ok('an admin deleting a ban is logged',
                     exists (select 1 from public.admin_log where action = 'ac_ban_delete' and detail like '%#' || b || '%'));
end $$;
-- Only admins can delete at all (RLS): a player's delete touches nothing.
do $$
declare n int;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000c1');
  set local role authenticated;
  delete from public.ac_bans;
  reset role;
  select count(*) into n from public.ac_bans where user_id = '00000000-0000-0000-0000-0000000000c1';
  perform t.ok('a player can''t delete bans', n = 2, n::text);
end $$;

-- ---- #270.4 machine bans survive account deletion ----------------------------------
reset role;
select set_config('request.jwt.claims', '', false);
do $$
declare n_machine int; n_account int; ok boolean;
begin
  delete from auth.users where id = '00000000-0000-0000-0000-0000000000c1';
  select count(*) into n_machine from public.ac_bans where hw_hash = repeat('ab', 32);
  select count(*) into n_account from public.ac_bans where reason = 'speedhack (account)';
  perform t.ok('deleting the account keeps its machine ban', n_machine = 1, n_machine::text);
  perform t.ok('...with user_id set to null',
                     (select user_id is null from public.ac_bans where hw_hash = repeat('ab', 32)));
  perform t.ok('...and drops its account-only ban', n_account = 0, n_account::text);
exception when others then perform t.ok('deleting a banned account', false, sqlerrm);
end $$;
do $$
declare n int; v boolean; ap bigint;
begin
  -- The cheater's new account on the same machine still sees (and can appeal) the machine ban.
  perform t.as_user('00000000-0000-0000-0000-0000000000c2');
  set local role authenticated;
  select count(*) into n from public.ac_my_bans(repeat('ab', 32));
  perform t.ok('the machine ban (null user_id) shows in ac_my_bans by hash', n = 1, n::text);
  select count(*) into n from public.ac_bans;
  perform t.ok('RLS: a null-user ban isn''t readable by other players', n = 0, n::text);
  ap := public.ac_file_appeal((select id from public.ac_my_bans(repeat('ab', 32)) limit 1), 'new me', repeat('ab', 32));
  perform t.ok('the machine ban can be appealed from the new account', ap is not null);
  reset role;
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  set local role authenticated;
  v := public.ac_is_banned(null, repeat('ab', 32));
  perform t.ok('the machine is still banned', v);
  select count(*) into n from public.ac_bans where user_id is null;
  perform t.ok('admins see the null-user ban', n = 1, n::text);
  update public.ac_appeals set status = 'accepted', decision_note = 'ok' where id = ap;
  reset role;
  perform t.ok('accepting its appeal lifts the null-user ban',
                     (select lifted from public.ac_bans where hw_hash = repeat('ab', 32)));
exception when others then perform t.ok('null-user machine ban', false, sqlerrm);
end $$;
-- A purge still works with null users and isn't blocked by the delete logging.
do $$
declare r record;
begin
  select * into r from public.ac_purge_old();
  perform t.ok('ac_purge_old runs', true);
exception when others then perform t.ok('ac_purge_old runs', false, sqlerrm);
end $$;
-- An admin's purge: one summary line, no line per purged row.
reset role;
select set_config('request.jwt.claims', '', false);
insert into public.ac_reports (reporter, reported, reason, created_at)
values ('00000000-0000-0000-0000-0000000000b1', '00000000-0000-0000-0000-0000000000c2', 'old one', now() - interval '200 days');
do $$
declare r record; before_del int;
begin
  select count(*) into before_del from public.admin_log where action = 'ac_report_delete';
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  set local role authenticated;
  select * into r from public.ac_purge_old();
  reset role;
  perform t.ok('an admin purge removes the old report', r.reports = 1, r.reports::text);
  perform t.ok('...logs one summary line', exists (select 1 from public.admin_log where action = 'ac_purge'));
  perform t.ok('...and no per-row delete lines',
               (select count(*) from public.admin_log where action = 'ac_report_delete') = before_del);
  perform t.ok('...and the purge flag is cleared', coalesce(current_setting('okay.ac_purge', true), '') <> 'on');
end $$;

-- =====================================================================================
-- Hardening: audit log, rate limits, report / appeal spam guards, ban evasion
-- =====================================================================================
reset role;
select set_config('request.jwt.claims', '', false);
insert into auth.users (id, email) values
  ('00000000-0000-0000-0000-0000000000d1', 'd1@x'), ('00000000-0000-0000-0000-0000000000d2', 'd2@x'),
  ('00000000-0000-0000-0000-0000000000d3', 'd3@x'), ('00000000-0000-0000-0000-0000000000e1', 'spammer@x'),
  ('00000000-0000-0000-0000-0000000000e2', 'honest@x'), ('00000000-0000-0000-0000-0000000000e3', 'reporter3@x');

-- ---- audit: every change is recorded, including ones made with no signed-in admin --------------------
do $$
declare n int; b bigint;
begin
  select count(*) into n from public.ac_audit where table_name = 'ac_bans' and action = 'ban_insert';
  perform t.ok('audit: the admin bans made earlier are recorded', n >= 3, n::text);
  perform t.ok('audit: who made them is recorded',
               exists (select 1 from public.ac_audit where action = 'ban_insert' and actor = '00000000-0000-0000-0000-00000000000a'
                       and actor_role = 'authenticated'));
  perform t.ok('audit: an unban / edit is recorded with old and new',
               exists (select 1 from public.ac_audit where action = 'ban_update' and detail ? 'old' and detail ? 'new'));
  perform t.ok('audit: appeal decisions are recorded', exists (select 1 from public.ac_audit where action = 'appeal_decision'));
  perform t.ok('audit: deleting a report / appeal / ban is recorded',
               exists (select 1 from public.ac_audit where action = 'report_delete')
               and exists (select 1 from public.ac_audit where action = 'appeal_delete')
               and exists (select 1 from public.ac_audit where action = 'ban_delete'));
  -- The SQL editor / service key has no auth.uid(): admin_log misses it, the audit log does not.
  select count(*) into n from public.admin_log;
  insert into public.ac_bans (user_id, reason) values ('00000000-0000-0000-0000-0000000000d3', 'by the sql editor') returning id into b;
  perform t.ok('audit: a ban made from the SQL editor is recorded with no actor',
               exists (select 1 from public.ac_audit where action = 'ban_insert' and row_id = b and actor is null));
  perform t.ok('...which admin_log did not see', (select count(*) from public.admin_log) = n);
  update public.ac_bans set lifted = true, lift_reason = 'editor unban' where id = b;
  perform t.ok('audit: an unban from the SQL editor is recorded',
               exists (select 1 from public.ac_audit where action = 'ban_update' and row_id = b and actor is null));
  select count(*) into n from public.ac_audit;
  update public.ac_bans set reason = reason where id = b;
  perform t.ok('audit: a no-op update adds nothing', (select count(*) from public.ac_audit) = n);
end $$;
do $$
declare n int; blocked boolean := false;
begin
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  set local role authenticated;
  select count(*) into n from public.ac_audit;
  perform t.ok('audit: an admin can read it', n > 5, n::text);
  begin update public.ac_audit set action = 'x'; get diagnostics n = row_count; exception when others then n := -1; end;
  perform t.ok('audit: an admin cannot edit it through the API', n <= 0, n::text);
  begin delete from public.ac_audit; get diagnostics n = row_count; exception when others then n := -1; end;
  perform t.ok('audit: an admin cannot delete from it', n <= 0, n::text);
  begin insert into public.ac_audit (action, table_name) values ('forged', 'x'); exception when others then blocked := true; end;
  perform t.ok('audit: an admin cannot write entries by hand', blocked);
  reset role;
  perform t.as_user('00000000-0000-0000-0000-0000000000e2');
  set local role authenticated;
  select count(*) into n from public.ac_audit;
  perform t.ok('audit: a player sees nothing', n = 0, n::text);
end $$;
do $$
declare blocked boolean := false; blocked2 boolean := false;
begin
  -- Even the table owner can't quietly rewrite or empty it (a trigger), only ac_purge_audit() can trim it.
  begin update public.ac_audit set action = 'tamper'; exception when insufficient_privilege then blocked := true; end;
  perform t.ok('audit: the owner can''t update it either', blocked);
  blocked := false;
  begin delete from public.ac_audit; exception when insufficient_privilege then blocked := true; end;
  perform t.ok('audit: ...nor delete', blocked);
  begin truncate public.ac_audit; exception when insufficient_privilege then blocked2 := true; end;
  perform t.ok('audit: ...nor truncate', blocked2);
end $$;
do $$
declare n bigint; before_n int;
begin
  insert into public.ac_audit (at, action, table_name) values (now() - interval '400 days', 'old_entry', 'x');
exception when insufficient_privilege then
  null;   -- (the insert is by the owner: allowed; only update / delete are blocked)
end $$;
do $$
declare n bigint;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000e2');
  set local role authenticated;
  begin perform public.ac_purge_audit(); perform t.ok('a player can''t purge the audit log', false);
  exception when insufficient_privilege then perform t.ok('a player can''t purge the audit log', true); end;
  reset role;
  perform set_config('request.jwt.claims', '', false);
  select public.ac_purge_audit() into n;
  perform t.ok('ac_purge_audit removes only entries over a year old and says so',
               n >= 1 and exists (select 1 from public.ac_audit where action = 'audit_purge')
               and not exists (select 1 from public.ac_audit where action = 'old_entry'), n::text);
end $$;

-- ---- report spam guards ---------------------------------------------------------------------------------
do $$
declare refused boolean;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000e1');
  set local role authenticated;
  insert into public.ac_reports (reported, reason) values ('00000000-0000-0000-0000-0000000000d1', 'first');
  refused := false;
  begin insert into public.ac_reports (reported, reason) values ('00000000-0000-0000-0000-0000000000d1', 'again at once');
  exception when check_violation then refused := true; end;
  perform t.ok('reports: the same player again within 10 minutes is refused', refused);
  insert into public.ac_reports (reported, reason) values ('00000000-0000-0000-0000-0000000000d2', 'someone else is fine');
  perform t.ok('reports: a different player is fine', true);
exception when others then perform t.ok('reports: spam guard setup', false, sqlerrm);
end $$;
reset role;
select set_config('request.jwt.claims', '', false);
-- Back-date two of e1's reports on d1 so only the daily cap (3) can refuse the next one.
insert into public.ac_reports (reporter, reported, reason, created_at) values
  ('00000000-0000-0000-0000-0000000000e1', '00000000-0000-0000-0000-0000000000d1', 'older 1', now() - interval '3 hours'),
  ('00000000-0000-0000-0000-0000000000e1', '00000000-0000-0000-0000-0000000000d1', 'older 2', now() - interval '5 hours');
update public.ac_reports set created_at = now() - interval '2 hours' where reason = 'first';
do $$
declare refused boolean := false;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000e1');
  set local role authenticated;
  begin insert into public.ac_reports (reported, reason) values ('00000000-0000-0000-0000-0000000000d1', 'fourth today');
  exception when check_violation then refused := true; end;
  perform t.ok('reports: a 4th report on one player in a day is refused', refused);
end $$;
-- Ten dismissed reports and no actioned one: reporting is paused. One actioned report lifts the pause.
reset role;
select set_config('request.jwt.claims', '', false);
insert into public.ac_reports (reporter, reported, reason, status, created_at)
select '00000000-0000-0000-0000-0000000000e3', '00000000-0000-0000-0000-0000000000d2', 'x' || g, 'dismissed', now() - interval '5 days'
  from generate_series(1, 10) g;
do $$
declare refused boolean := false;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000e3');
  set local role authenticated;
  begin insert into public.ac_reports (reported, reason) values ('00000000-0000-0000-0000-0000000000d1', 'more noise');
  exception when check_violation then refused := true; end;
  perform t.ok('reports: ten dismissed and none actioned pauses reporting', refused);
  reset role;
  update public.ac_reports set status = 'actioned' where reporter = '00000000-0000-0000-0000-0000000000e3' and reason = 'x1';
  perform t.as_user('00000000-0000-0000-0000-0000000000e3');
  set local role authenticated;
  insert into public.ac_reports (reported, reason) values ('00000000-0000-0000-0000-0000000000d1', 'a real one');
  perform t.ok('reports: one actioned report lifts the pause', true);
exception when others then perform t.ok('reports: pause lifted by an actioned report', false, sqlerrm);
end $$;
-- The hourly cap (20) and the table's own RLS still hold.
reset role;
select set_config('request.jwt.claims', '', false);
do $$
declare refused boolean := false; i int;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000e2');
  set local role authenticated;
  begin
    for i in 1..25 loop
      insert into public.ac_reports (kind, reason) values ('client_check', 'check ' || i);
    end loop;
  exception when check_violation then refused := true; end;
  perform t.ok('reports: the hourly cap of 20 still refuses a flood', refused);
end $$;

-- ---- appeal limits ---------------------------------------------------------------------------------------
reset role;
select set_config('request.jwt.claims', '', false);
insert into public.ac_bans (user_id, reason) values ('00000000-0000-0000-0000-0000000000d1', 'ban one'), ('00000000-0000-0000-0000-0000000000d1', 'ban two');
do $$
declare b1 bigint; b2 bigint; refused boolean := false; ids bigint[];
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000d1');
  set local role authenticated;
  select array_agg(id order by id) into ids from public.ac_my_bans();
  b1 := ids[1]; b2 := ids[2];
  perform public.ac_file_appeal(b1, 'first appeal');
  begin perform public.ac_file_appeal(b2, 'second one right away');
  exception when check_violation then refused := true; end;
  perform t.ok('appeals: a second appeal within the hour is refused (any ban)', refused);
  refused := false;
  begin perform public.ac_file_appeal(999999, 'not mine');
  exception when others then refused := true; end;
  perform t.ok('appeals: someone else''s / unknown ban id is still refused', refused);
end $$;
reset role;
select set_config('request.jwt.claims', '', false);
-- Back-date the appeal and use the daily cap: 5 appeals a day.
update public.ac_appeals set created_at = now() - interval '2 hours', status = 'rejected', decided_at = now() - interval '90 minutes'
 where user_id = '00000000-0000-0000-0000-0000000000d1';
delete from public.ac_rate_events where key like 'appeal-day:%';
insert into public.ac_rate_events (key) select 'appeal-day:00000000-0000-0000-0000-0000000000d1' from generate_series(1, 5);
do $$
declare refused boolean := false; b2 bigint;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000d1');
  set local role authenticated;
  select id into b2 from public.ac_my_bans() where reason = 'ban two';
  begin perform public.ac_file_appeal(b2, 'sixth today');
  exception when check_violation then refused := true; end;
  perform t.ok('appeals: 5 a day per player', refused);
end $$;

-- ---- ac_my_bans lookups are rate limited ----------------------------------------------------------------
do $$
declare refused boolean := false; i int;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000e2');
  set local role authenticated;
  begin
    for i in 1..125 loop perform 1 from public.ac_my_bans(); end loop;
  exception when check_violation then refused := true; end;
  perform t.ok('ac_my_bans: 120 lookups an hour, then refused', refused);
end $$;
reset role;
do $$
declare ok boolean := false;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000e2');
  set local role authenticated;
  begin perform count(*) from public.ac_rate_events; exception when insufficient_privilege then ok := true; end;
  perform t.ok('the rate table is not readable by players', ok);
  ok := false;
  begin perform public.ac_rate_hit('x', 1, interval '1 hour'); exception when insufficient_privilege then ok := true; end;
  perform t.ok('ac_rate_hit can''t be called by players (no resetting their own limits)', ok);
end $$;

-- ---- ban evasion: devices, links, ban-with-devices -------------------------------------------------------
reset role;
select set_config('request.jwt.claims', '', false);
do $$
declare ev boolean; denied boolean := false; n int; hw1 text := repeat('1a', 32); hw2 text := repeat('2b', 32);
begin
  -- A player can't write devices; the service key can.
  perform t.as_user('00000000-0000-0000-0000-0000000000e2');
  set local role authenticated;
  begin perform public.ac_note_device('00000000-0000-0000-0000-0000000000e2', hw1); exception when insufficient_privilege then denied := true; end;
  perform t.ok('devices: a player can''t record devices', denied);
  denied := false;
  begin insert into public.ac_devices (user_id, hw_hash) values ('00000000-0000-0000-0000-0000000000e2', hw1);
  exception when insufficient_privilege then denied := true; end;
  perform t.ok('devices: ...nor insert them directly', denied);
  reset role;
  perform t.as_service();
  set local role service_role;
  perform public.ac_note_device('00000000-0000-0000-0000-0000000000d2', hw1);
  perform public.ac_note_device('00000000-0000-0000-0000-0000000000d2', hw2);
  perform public.ac_note_device('00000000-0000-0000-0000-0000000000d3', hw1);   -- d3 shares hw1 with d2
  perform public.ac_note_device('00000000-0000-0000-0000-0000000000d2', 'not-a-hash');
  reset role;
  select count(*) into n from public.ac_devices where user_id = '00000000-0000-0000-0000-0000000000d2';
  perform t.ok('devices: recorded by the service key (a bad hash is ignored)', n = 2, n::text);
  -- Admin: linked accounts, ban with devices.
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  set local role authenticated;
  select count(*) into n from public.ac_linked_accounts('00000000-0000-0000-0000-0000000000d2')
   where other_user = '00000000-0000-0000-0000-0000000000d3' and shared_machines = 1;
  perform t.ok('devices: an admin sees accounts sharing a machine', n = 1, n::text);
  select count(*) into n from public.ac_devices;
  perform t.ok('devices: an admin can read them', n = 3, n::text);
  select public.ac_ban_with_devices('00000000-0000-0000-0000-0000000000d2', 'cheat suite', null, null) into n;
  perform t.ok('ac_ban_with_devices bans the account and each machine (1 + 2)', n = 3, n::text);
  reset role;
  perform t.ok('...so both machines are banned', public.ac_is_banned(null, hw1) and public.ac_is_banned(null, hw2));
  -- d3 uses the banned machine: refused at the ticket, and noted as evasion.
  perform t.as_service();
  set local role service_role;
  ev := public.ac_note_device('00000000-0000-0000-0000-0000000000d3', hw1);
  reset role;
  perform t.ok('evasion: another account on a banned machine is flagged', ev);
  perform t.ok('...and written to the audit log',
               exists (select 1 from public.ac_audit where action = 'evasion_signal' and target = '00000000-0000-0000-0000-0000000000d3'));
  perform t.as_service();
  set local role service_role;
  ev := public.ac_note_device('00000000-0000-0000-0000-0000000000d2', hw1);
  reset role;
  perform t.ok('evasion: the banned account itself on its own machine is not flagged as evasion', not ev);
exception when others then perform t.ok('ban evasion flow', false, sqlerrm);
end $$;
do $$
declare denied boolean := false; n int;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000e2');
  set local role authenticated;
  begin perform * from public.ac_linked_accounts('00000000-0000-0000-0000-0000000000d2'); exception when insufficient_privilege then denied := true; end;
  perform t.ok('devices: a player can''t look up linked accounts', denied);
  select count(*) into n from public.ac_devices;
  perform t.ok('devices: RLS hides them from players', n = 0, n::text);
  denied := false;
  begin perform public.ac_ban_with_devices('00000000-0000-0000-0000-0000000000d3', 'sneaky', null, null);
  exception when others then denied := true; end;
  perform t.ok('a player can''t ban with devices (RLS on ac_bans)', denied);
end $$;
do $$
declare refused boolean := false;
begin
  -- An admin can't ban themselves this way either (the "admins ban" policy).
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  set local role authenticated;
  begin perform public.ac_ban_with_devices('00000000-0000-0000-0000-00000000000a', 'self', null, null);
  exception when others then refused := true; end;
  perform t.ok('an admin can''t ban their own account with devices', refused);
end $$;
-- Deleting an account removes its device links (they are about that account), not the machine bans.
reset role;
select set_config('request.jwt.claims', '', false);
do $$
begin
  delete from auth.users where id = '00000000-0000-0000-0000-0000000000d2';
  perform t.ok('deleting an account drops its device links', not exists (select 1 from public.ac_devices where user_id = '00000000-0000-0000-0000-0000000000d2'));
  perform t.ok('...but the machine bans stay', public.ac_is_banned(null, repeat('1a', 32)));
exception when others then perform t.ok('deleting a banned account with devices', false, sqlerrm);
end $$;

-- ---- Evidence bundles on server-kick reports ----------------------------------------
reset role;
select set_config('request.jwt.claims', '', false);
insert into auth.users (id, email) values ('00000000-0000-0000-0000-0000000000f1', 'evserver@x'), ('00000000-0000-0000-0000-0000000000f2', 'evcheater@x'), ('00000000-0000-0000-0000-0000000000f3', 'evplayer@x');
insert into public.ac_report_servers (user_id, note) values ('00000000-0000-0000-0000-0000000000f1', 'test server');
select t.as_user('00000000-0000-0000-0000-0000000000f1');
set role authenticated;
insert into public.ac_reports (reported, kind, reason, evidence)
  values ('00000000-0000-0000-0000-0000000000f2', 'server_kick', '[server check] kicked', '{"version":1,"risk":9.5,"counts":[["speed",3]]}');
do $$
declare refused boolean := false;
begin
  -- Too big: refused by the size cap.
  begin
    insert into public.ac_reports (reported, kind, reason, evidence)
      values ('00000000-0000-0000-0000-0000000000f2', 'server_kick', '[server check] big', jsonb_build_object('pad', repeat('x', 17000)));
  exception when check_violation then refused := true; end;
  perform t.ok('evidence: over 16 KiB is refused', refused);
  refused := false;
  begin
    insert into public.ac_reports (reported, kind, reason, evidence)
      values ('00000000-0000-0000-0000-0000000000f2', 'server_kick', '[server check] arr', '[1,2]');
  exception when check_violation then refused := true; end;
  perform t.ok('evidence: must be a JSON object', refused);
end $$;
reset role;
select t.as_user('00000000-0000-0000-0000-0000000000f3');
set role authenticated;
insert into public.ac_reports (reported, reason, evidence)
  values ('00000000-0000-0000-0000-0000000000f2', 'player report with evidence', '{"sneaky":true}');
do $$
declare n int;
begin
  select count(*) into n from public.ac_reports;
  perform t.ok('evidence: a player can not read reports (or evidence)', n = 0, n::text);
end $$;
reset role;
select t.as_user('00000000-0000-0000-0000-00000000000a');
set role authenticated;
do $$
declare ev jsonb; n int;
begin
  select evidence into ev from public.ac_reports where reason = '[server check] kicked';
  perform t.ok('evidence: an admin reads the server bundle', ev is not null and (ev->>'risk')::numeric = 9.5, coalesce(ev::text, 'null'));
  select count(*) into n from public.ac_reports where reason = 'player report with evidence' and evidence is null;
  perform t.ok('evidence: a non-server account''s bundle is dropped', n = 1, n::text);
  update public.ac_reports set status = 'reviewing', evidence = '{"tampered":1}' where reason = '[server check] kicked';
  select evidence into ev from public.ac_reports where reason = '[server check] kicked';
  perform t.ok('evidence: a review does not change it', (ev->>'risk')::numeric = 9.5, coalesce(ev::text, 'null'));
end $$;
reset role;
